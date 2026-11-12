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
CODEGEN_SRC     = $(wildcard src/codegen/*.c)
ASSEMBLER_SRC   = $(wildcard src/assembler/*.c)
SCHEDULER_SRC   = $(wildcard src/scheduler/*.c)
EMIT_SRC        = $(wildcard src/emit/*.c)
IR_SRC          = $(wildcard src/ir/*.c)
CFG_SRC         = $(wildcard src/cfg/*.c)

ANALYSIS_OBJ    = $(patsubst src/analysis/%.c,build/analysis/%.o,$(ANALYSIS_SRC))
LL1_OBJ         = $(patsubst src/analysis/ll1/%.c,build/analysis/ll1/%.o,$(LL1_SRC))
CODEGEN_OBJ     = $(patsubst src/codegen/%.c,build/codegen/%.o,$(CODEGEN_SRC))
ASSEMBLER_OBJ   = $(patsubst src/assembler/%.c,build/assembler/%.o,$(ASSEMBLER_SRC))
SCHEDULER_OBJ   = $(patsubst src/scheduler/%.c,build/scheduler/%.o,$(SCHEDULER_SRC))
EMIT_OBJ        = $(patsubst src/emit/%.c,build/emit/%.o,$(EMIT_SRC))
IR_OBJ          = $(patsubst src/ir/%.c,build/ir/%.o,$(IR_SRC))
CFG_OBJ         = $(patsubst src/cfg/%.c,build/cfg/%.o,$(CFG_SRC))

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
      $(CODEGEN_OBJ) \
      $(ASSEMBLER_OBJ) \
      $(SCHEDULER_LIB_OBJ) \
      $(EMIT_OBJ) \
      $(IR_OBJ) \
      $(CFG_OBJ)

.DELETE_ON_ERROR:
.PHONY: all generate ll1-table clean

all: $(TARGET) bin/porygon-schedule

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
              include/ast.h include/symbol_table.h include/semantic.h \
              include/codegen.h include/ast_to_json.h include/diagnostic.h include/ir.h include/cfg.h \
              src/assembler/asm_program_parser.h \
              src/scheduler/scheduler.h \
              src/emit/binary_format.h \
              src/analysis/ll1/ll1.h \
              build/parser.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c src/main.c -o $@

# --- Reglas de patrón por etapa ---------------------------------------------
build/ir/%.o: src/ir/%.c include/ir.h src/ir/ir_internal.h src/ir/ir_format.h include/ast.h include/symbol_table.h
	@mkdir -p build/ir
	$(CC) $(CFLAGS) -c $< -o $@

build/cfg/%.o: src/cfg/%.c include/cfg.h include/ir.h src/cfg/cfg_internal.h src/ir/ir_format.h include/ast.h include/symbol_table.h
	@mkdir -p build/cfg
	$(CC) $(CFLAGS) -c $< -o $@

build/analysis/ll1/%.o: src/analysis/ll1/%.c \
                         $(wildcard src/analysis/ll1/*.h) \
                         $(LL1_GENERATED_SYMBOLS) build/parser.h
	@mkdir -p build/analysis/ll1
	$(CC) $(CFLAGS) -c $< -o $@

build/analysis/ll1/grammar.o: $(LL1_GENERATED_DATA)

build/analysis/%.o: src/analysis/%.c $(wildcard include/*.h)
	@mkdir -p build/analysis
	$(CC) $(CFLAGS) -c $< -o $@

build/codegen/%.o: src/codegen/%.c src/codegen/codegen_internal.h \
                   $(wildcard include/*.h)
	@mkdir -p build/codegen
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
