CC = gcc
FLEX ?= flex
PYTHON ?= python3

ifeq ($(wildcard /opt/homebrew/opt/bison/bin/bison),)
    BISON ?= bison
else
    BISON ?= /opt/homebrew/opt/bison/bin/bison
endif

CFLAGS = -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic \
         -Iinclude -Isrc -Ibuild -I.

ANALYSIS_SRC    = $(wildcard src/analysis/*.c)
LL1_SRC         = $(wildcard src/analysis/ll1/*.c)
CODEOPGEN_SRC   = $(wildcard src/codeOPgen/*.c)
ASSEMBLER_SRC   = $(wildcard src/assembler/*.c)
SCHEDULER_SRC   = $(wildcard src/scheduler/*.c)
EMIT_SRC        = $(wildcard src/emit/*.c)
IR_SRC          = $(wildcard src/ir/*.c)
CFG_SRC         = $(wildcard src/cfg/*.c)
REGALLOC_SRC    = $(wildcard src/regalloc/*.c)
LIVENESS_SRC    = $(wildcard src/liveness/*.c)
DCE_SRC         = $(wildcard src/dce/*.c)

ANALYSIS_OBJ    = $(patsubst src/analysis/%.c,build/analysis/%.o,$(ANALYSIS_SRC))
LL1_OBJ         = $(patsubst src/analysis/ll1/%.c,build/analysis/ll1/%.o,$(LL1_SRC))
CODEOPGEN_OBJ   = $(patsubst src/codeOPgen/%.c,build/codeOPgen/%.o,$(CODEOPGEN_SRC))
ASSEMBLER_OBJ   = $(patsubst src/assembler/%.c,build/assembler/%.o,$(ASSEMBLER_SRC))
SCHEDULER_OBJ   = $(patsubst src/scheduler/%.c,build/scheduler/%.o,$(SCHEDULER_SRC))
EMIT_OBJ        = $(patsubst src/emit/%.c,build/emit/%.o,$(EMIT_SRC))
IR_OBJ          = $(patsubst src/ir/%.c,build/ir/%.o,$(IR_SRC))
CFG_OBJ         = $(patsubst src/cfg/%.c,build/cfg/%.o,$(CFG_SRC))
REGALLOC_OBJ    = $(patsubst src/regalloc/%.c,build/regalloc/%.o,$(REGALLOC_SRC))
LIVENESS_OBJ    = $(patsubst src/liveness/%.c,build/liveness/%.o,$(LIVENESS_SRC))
DCE_OBJ         = $(patsubst src/dce/%.c,build/dce/%.o,$(DCE_SRC))

IDE_APP = ide/piper_ide/app.py
IDE_LOG = build/ide.log

LL1_GRAMMAR           = src/analysis/ll1/piper.ll1
LL1_GENERATOR         = tools/generate_ll1.py
LL1_GENERATED_SYMBOLS = build/analysis/ll1/grammar_symbols.h
LL1_GENERATED_DATA    = build/analysis/ll1/grammar_data.inc
LL1_REPORT            = build/analysis/ll1/table.html

SCHEDULER_CLI_SRC = src/scheduler/schedule_main.c
SCHEDULER_LIB_SRC = $(filter-out $(SCHEDULER_CLI_SRC),$(SCHEDULER_SRC))
SCHEDULER_LIB_OBJ = $(patsubst src/scheduler/%.c,build/scheduler/%.o,$(SCHEDULER_LIB_SRC))

# --- Compilador --------------------------------------------------------------
TARGET = bin/pipoc

OBJ = build/parser.o \
      build/lexer.o \
      build/main.o \
      $(ANALYSIS_OBJ) \
      $(LL1_OBJ) \
      $(CODEOPGEN_OBJ) \
      $(ASSEMBLER_OBJ) \
      $(SCHEDULER_LIB_OBJ) \
      $(EMIT_OBJ) \
      $(IR_OBJ) \
      $(CFG_OBJ) \
      $(REGALLOC_OBJ) \
      $(LIVENESS_OBJ) \
      $(DCE_OBJ)

.DELETE_ON_ERROR:
.PHONY: all generate ll1-table check-regalloc-machine-graph check-regalloc-assignment check-regalloc-coloring check-regalloc-lower-symbols check-regalloc-lower-calls check-regalloc-lower-order check-regalloc-lower-checked check-regalloc-lower-checked-unary check-regalloc-pipeline run clean

all: $(TARGET) bin/porygon-schedule

.PHONY: check-rename check-rename-sim
check-rename-sim: RENAME_TEST_ARGS = --sim
check-rename check-rename-sim: all build/runs/rename/test_rename build/runs/rename/asm_hazards
	build/runs/rename/test_rename
	$(PYTHON) tests/archivadas/rename/check.py $(RENAME_TEST_ARGS)

build/runs/rename/test_rename: tests/archivadas/regalloc/regalloc_rename.c build/regalloc/rename.o \
        build/regalloc/assignment.o build/regalloc/interference_machine.o \
        build/liveness/liveness.o build/liveness/liveness_machine.o
	@mkdir -p build/runs/rename
	$(CC) $(CFLAGS) $^ -o $@

build/runs/rename/asm_hazards: tests/archivadas/rename/asm_hazards.c $(ASSEMBLER_OBJ)
	@mkdir -p build/runs/rename
	$(CC) $(CFLAGS) $^ -o $@

.PHONY: check-codeOPgen check-codeOPgen-sim
check-codeOPgen: $(TARGET) build/runs/codeOPgen/test_contract
	build/runs/codeOPgen/test_contract
	bash tests/archivadas/codeOPgen/check.sh

check-codeOPgen-sim: $(TARGET) build/runs/codeOPgen/test_contract
	build/runs/codeOPgen/test_contract
	bash tests/archivadas/codeOPgen/check.sh --sim

.PHONY: check-import
check-import: $(TARGET)
	bash tests/archivadas/import/check.sh

build/runs/codeOPgen/test_contract: tests/archivadas/codeOPgen/test_contract.c $(filter-out build/main.o,$(OBJ))
	@mkdir -p build/runs/codeOPgen
	$(CC) $(CFLAGS) $^ -o $@

run: all
	@mkdir -p build
	nohup $(PYTHON) $(IDE_APP) > $(CURDIR)/$(IDE_LOG) 2>&1 & echo "IDE ON (PID $$!), log: $(IDE_LOG)"

# Ruta reproducible Piper -> IR/CFG -> asignacion, con y sin spill virtual.
check-regalloc-pipeline: build/parser.o build/lexer.o $(ANALYSIS_OBJ) \
                         $(IR_OBJ) build/cfg/cfg.o build/cfg/cfg_validate.o \
                         $(REGALLOC_OBJ) $(LIVENESS_OBJ)
	@mkdir -p build/runs/regalloc
	$(CC) $(CFLAGS) tests/archivadas/regalloc/regalloc_pipeline.c $^ \
	      -o build/runs/regalloc/test_pipeline
	build/runs/regalloc/test_pipeline

# Grafo de temporales virtuales: no necesita frontend ni emisor ASM.
check-regalloc-machine-graph: build/regalloc/interference_machine.o \
                              build/liveness/liveness.o build/liveness/liveness_machine.o
	@mkdir -p build/runs/regalloc
	$(CC) $(CFLAGS) tests/archivadas/regalloc/regalloc_machine_graph.c \
	      build/regalloc/interference_machine.o \
	      build/liveness/liveness.o build/liveness/liveness_machine.o \
	      -o build/runs/regalloc/test_machine_graph
	build/runs/regalloc/test_machine_graph

# Contrato de entrega al emisor: colores, frame y ranuras de spill.
check-regalloc-assignment: build/regalloc/assignment.o \
                           build/regalloc/interference_machine.o \
                           build/liveness/liveness.o build/liveness/liveness_machine.o
	@mkdir -p build/runs/regalloc
	$(CC) $(CFLAGS) tests/archivadas/regalloc/regalloc_assignment.c \
	      build/regalloc/assignment.o build/regalloc/interference_machine.o \
	      build/liveness/liveness.o build/liveness/liveness_machine.o \
	      -o build/runs/regalloc/test_assignment
	build/runs/regalloc/test_assignment

# Contrasta liveness/grafo con un ejemplo publicado y comprueba toda la pila.
check-regalloc-coloring: build/regalloc/color_machine.o \
                         build/regalloc/interference_machine.o \
                         build/liveness/liveness.o build/liveness/liveness_machine.o
	@mkdir -p build/runs/regalloc
	$(CC) $(CFLAGS) tests/archivadas/regalloc/regalloc_coloring.c \
	      build/regalloc/color_machine.o build/regalloc/interference_machine.o \
	      build/liveness/liveness.o build/liveness/liveness_machine.o \
	      -o build/runs/regalloc/test_coloring
	build/runs/regalloc/test_coloring

# R2b escalar: contrato de registros fijos para simbolos e inmediatos grandes.
check-regalloc-lower-symbols: build/regalloc/machine.o build/regalloc/assignment.o \
                              build/regalloc/color_machine.o \
                              build/regalloc/interference_machine.o \
                              build/liveness/liveness.o build/liveness/liveness_machine.o
	@mkdir -p build/runs/regalloc
	$(CC) $(CFLAGS) tests/archivadas/regalloc/regalloc_lower_symbols.c \
	      build/regalloc/machine.o build/regalloc/assignment.o \
	      build/regalloc/color_machine.o build/regalloc/interference_machine.o \
	      build/liveness/liveness.o build/liveness/liveness_machine.o \
	      -o build/runs/regalloc/test_lower_symbols
	build/runs/regalloc/test_lower_symbols

# R2b llamadas: argumentos fijos, valores vivos y slots de argumentos en pila.
check-regalloc-lower-calls: build/regalloc/machine.o build/regalloc/assignment.o \
                           build/regalloc/color_machine.o \
                           build/regalloc/interference_machine.o \
                           build/liveness/liveness.o build/liveness/liveness_machine.o
	@mkdir -p build/runs/regalloc
	$(CC) $(CFLAGS) tests/archivadas/regalloc/regalloc_lower_calls.c \
	      build/regalloc/machine.o build/regalloc/assignment.o \
	      build/regalloc/color_machine.o build/regalloc/interference_machine.o \
	      build/liveness/liveness.o build/liveness/liveness_machine.o \
	      -o build/runs/regalloc/test_lower_calls
	build/runs/regalloc/test_lower_calls

# R2b orden tipado: signo independiente en cada operando y recorrido del grafo.
check-regalloc-lower-order: build/regalloc/machine.o build/regalloc/assignment.o \
                           build/regalloc/color_machine.o \
                           build/regalloc/interference_machine.o \
                           build/liveness/liveness.o build/liveness/liveness_machine.o
	@mkdir -p build/runs/regalloc
	$(CC) $(CFLAGS) tests/archivadas/regalloc/regalloc_lower_order.c \
	      build/regalloc/machine.o build/regalloc/assignment.o \
	      build/regalloc/color_machine.o build/regalloc/interference_machine.o \
	      build/liveness/liveness.o build/liveness/liveness_machine.o \
	      -o build/runs/regalloc/test_lower_order
	build/runs/regalloc/test_lower_order

# R2b suma/resta/multiplicacion comprobadas y efectos visibles para el grafo.
check-regalloc-lower-checked: build/regalloc/machine.o build/regalloc/assignment.o \
                             build/regalloc/color_machine.o \
                             build/regalloc/interference_machine.o \
                             build/liveness/liveness.o build/liveness/liveness_machine.o
	@mkdir -p build/runs/regalloc
	$(CC) $(CFLAGS) tests/archivadas/regalloc/regalloc_lower_checked.c \
	      build/regalloc/machine.o build/regalloc/assignment.o \
	      build/regalloc/color_machine.o build/regalloc/interference_machine.o \
	      build/liveness/liveness.o build/liveness/liveness_machine.o \
	      -o build/runs/regalloc/test_lower_checked
	build/runs/regalloc/test_lower_checked

# R2b conversiones y negacion comprobadas, incluido store despues del chequeo.
check-regalloc-lower-checked-unary: build/regalloc/machine.o build/regalloc/assignment.o \
                                   build/regalloc/color_machine.o \
                                   build/regalloc/interference_machine.o \
                                   build/liveness/liveness.o build/liveness/liveness_machine.o
	@mkdir -p build/runs/regalloc
	$(CC) $(CFLAGS) tests/archivadas/regalloc/regalloc_lower_checked_unary.c \
	      build/regalloc/machine.o build/regalloc/assignment.o \
	      build/regalloc/color_machine.o build/regalloc/interference_machine.o \
	      build/liveness/liveness.o build/liveness/liveness_machine.o \
	      -o build/runs/regalloc/test_lower_checked_unary
	build/runs/regalloc/test_lower_checked_unary

generate: build/parser.c build/parser.h build/lexer.c \
          $(LL1_GENERATED_SYMBOLS) $(LL1_GENERATED_DATA)

ll1-table: $(LL1_REPORT)

$(LL1_REPORT): $(LL1_GRAMMAR) $(LL1_GENERATOR) tools/ll1_report.html
	$(PYTHON) $(LL1_GENERATOR) $(LL1_GRAMMAR) --kind html --output $@

# La gramática legible es la fuente de verdad. FIRST, FOLLOW y la tabla se
# calculan durante la construcción y quedan incorporados en el ejecutable.
$(LL1_GENERATED_SYMBOLS): $(LL1_GRAMMAR) $(LL1_GENERATOR)
	@mkdir -p build/analysis/ll1
	$(PYTHON) $(LL1_GENERATOR) $(LL1_GRAMMAR) \
	          --kind symbols --output $@

$(LL1_GENERATED_DATA): $(LL1_GRAMMAR) $(LL1_GENERATOR)
	@mkdir -p build/analysis/ll1
	$(PYTHON) $(LL1_GENERATOR) $(LL1_GRAMMAR) \
	          --kind data --output $@

# Bison debe ejecutarse antes de Flex porque lexer.l incluye parser.h.
build/parser.c: src/analysis/parser.y include/ast.h include/symbol_table.h
	@mkdir -p build
	$(BISON) -d -v -o build/parser.c src/analysis/parser.y

build/parser.h: build/parser.c
	@test -f build/parser.h

build/lexer.c: src/analysis/lexer.l build/parser.h include/ast.h
	@mkdir -p build
	$(FLEX) -o build/lexer.c src/analysis/lexer.l

build/parser.o: build/parser.c build/parser.h include/ast.h include/symbol_table.h include/diagnostic.h
	$(CC) $(CFLAGS) -c build/parser.c -o $@

# Flex 2.6.4 genera una comparación signed/unsigned fuera del código del equipo.
build/lexer.o: build/lexer.c build/parser.h include/ast.h include/diagnostic.h
	$(CC) $(CFLAGS) -Wno-sign-compare -c build/lexer.c -o $@

# --- main.c: orquestador. Depende de todos los headers públicos de etapa. ----
build/main.o: src/main.c \
              include/ast.h include/symbol_table.h include/semantic.h include/module_loader.h \
              include/codeOPgen.h include/regalloc_machine.h include/ir_unroll.h \
              include/ast_to_json.h include/diagnostic.h include/ir.h include/cfg.h include/regalloc.h src/dce/dce.h \
              src/assembler/asm_program_parser.h \
              src/scheduler/scheduler.h \
              src/emit/binary_format.h \
              src/analysis/ll1/ll1.h \
              build/parser.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c src/main.c -o $@

# --- Reglas de patrón por etapa ---------------------------------------------
build/ir/%.o: src/ir/%.c include/ir.h include/ir_unroll.h src/ir/ir_internal.h src/ir/ir_format.h include/ast.h include/symbol_table.h
	@mkdir -p build/ir
	$(CC) $(CFLAGS) -c $< -o $@

build/cfg/%.o: src/cfg/%.c include/cfg.h include/ir.h src/cfg/cfg_internal.h src/ir/ir_format.h include/ast.h include/symbol_table.h
	@mkdir -p build/cfg
	$(CC) $(CFLAGS) -c $< -o $@

build/regalloc/%.o: src/regalloc/%.c include/regalloc.h include/regalloc_machine.h src/liveness/liveness.h include/cfg.h include/ir.h
	@mkdir -p build/regalloc
	$(CC) $(CFLAGS) -c $< -o $@

build/liveness/%.o: src/liveness/%.c src/liveness/liveness_internal.h src/liveness/liveness.h include/regalloc_machine.h include/cfg.h include/ir.h
	@mkdir -p build/liveness
	$(CC) $(CFLAGS) -c $< -o $@

build/dce/%.o: src/dce/%.c $(wildcard src/dce/*.h) src/liveness/liveness.h include/ir.h include/cfg.h include/symbol_table.h
	@mkdir -p build/dce
	$(CC) $(CFLAGS) -c $< -o $@

build/analysis/ll1/%.o: src/analysis/ll1/%.c \
                         $(wildcard src/analysis/ll1/*.h) \
                         $(LL1_GENERATED_SYMBOLS) build/parser.h
	@mkdir -p build/analysis/ll1
	$(CC) $(CFLAGS) -c $< -o $@

build/analysis/ll1/grammar.o: $(LL1_GENERATED_DATA)

build/analysis/module_loader.o: build/parser.h

build/analysis/%.o: src/analysis/%.c $(wildcard include/*.h)
	@mkdir -p build/analysis
	$(CC) $(CFLAGS) -c $< -o $@

build/codeOPgen/%.o: src/codeOPgen/%.c src/codeOPgen/internal.h \
                   $(wildcard include/*.h)
	@mkdir -p build/codeOPgen
	$(CC) $(CFLAGS) -c $< -o $@

build/assembler/%.o: src/assembler/%.c $(wildcard src/assembler/*.h)
	@mkdir -p build/assembler
	$(CC) $(CFLAGS) -c $< -o $@

build/scheduler/%.o: src/scheduler/%.c $(wildcard src/scheduler/*.h) \
                     $(wildcard src/assembler/*.h)
	@mkdir -p build/scheduler
	$(CC) $(CFLAGS) -c $< -o $@

build/emit/%.o: src/emit/%.c $(wildcard src/emit/*.h) \
                $(wildcard src/scheduler/*.h) $(wildcard src/assembler/*.h)
	@mkdir -p build/emit
	$(CC) $(CFLAGS) -c $< -o $@

# --- Binarios ----------------------------------------------------------------
$(TARGET): $(OBJ)
	@mkdir -p bin
	$(CC) $(CFLAGS) $(OBJ) -o $@

bin/porygon-schedule: src/scheduler/schedule_main.c \
                      $(ASSEMBLER_OBJ) $(SCHEDULER_LIB_OBJ) \
                      src/scheduler/scheduler.h \
                      src/assembler/asm_program_parser.h
	@mkdir -p bin
	$(CC) $(CFLAGS) src/scheduler/schedule_main.c \
	      $(ASSEMBLER_OBJ) $(SCHEDULER_LIB_OBJ) -o $@

clean:
	rm -rf build bin
