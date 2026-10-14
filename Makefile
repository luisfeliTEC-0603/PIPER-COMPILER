CC = gcc
FLEX ?= flex

# Si el bison de Homebrew existe (macOS Apple Silicon), usarlo por defecto para evitar
# usar el bison desactualizado del sistema. En Linux/Windows usará 'bison' del PATH.
ifeq ($(wildcard /opt/homebrew/opt/bison/bin/bison),)
    BISON ?= bison
else
    BISON ?= /opt/homebrew/opt/bison/bin/bison
endif

CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Iinclude -Ibuild -I.
PORYGON_ASM_DIR = porygon_assembler
PORYGON_ASM_SRC = $(wildcard $(PORYGON_ASM_DIR)/*.c)
PORYGON_ASM_OBJ = $(patsubst $(PORYGON_ASM_DIR)/%.c,build/porygon_assembler/%.o,$(PORYGON_ASM_SRC))
SCHEDULER_SRC = $(wildcard scheduler/*.c)
SCHEDULER_OBJ = $(patsubst scheduler/%.c,build/scheduler/%.o,$(SCHEDULER_SRC))
TARGET = bin/pipoc

OBJ = build/parser.o \
      build/lexer.o \
      build/ast.o \
      build/ast_to_json.o \
      build/symbol_table.o \
      build/main.o \
      build/semantic.o \
      build/binary_format.o \
      build/codegen.o \
      build/codegen_context.o \
      build/codegen_emit.o \
      build/codegen_labels.o \
      build/codegen_regs.o \
      build/codegen_routine.o \
      build/codegen_expr.o \
      build/codegen_arithmetic.o \
      build/codegen_stmt.o \
      build/codegen_array.o \
      build/codegen_call.o \
      build/codegen_crypto.o \
      $(PORYGON_ASM_OBJ) \
      $(SCHEDULER_OBJ)

.DELETE_ON_ERROR:
.PHONY: all generate conflicts clean test-scheduler test-scheduler-rtl test-cli-bin \
	test-integration test-extract-wrapper

all: $(TARGET) bin/porygon-schedule

generate: build/parser.c build/parser.h build/lexer.c

# Bison debe ejecutarse antes de Flex porque lexer.l incluye parser.h.
build/parser.c: src/parser.y include/ast.h include/symbol_table.h
	@mkdir -p build
	$(BISON) -d -v -o build/parser.c src/parser.y

build/parser.h: build/parser.c
	@test -f build/parser.h

build/lexer.c: src/lexer.l build/parser.h include/ast.h
	@mkdir -p build
	$(FLEX) -o build/lexer.c src/lexer.l

build/parser.o: build/parser.c build/parser.h include/ast.h include/symbol_table.h
	$(CC) $(CFLAGS) -c build/parser.c -o $@

# Flex 2.6.4 genera una comparación signed/unsigned fuera del código del equipo.
build/lexer.o: build/lexer.c build/parser.h include/ast.h
	$(CC) $(CFLAGS) -Wno-sign-compare -c build/lexer.c -o $@

build/ast.o: src/ast.c include/ast.h
	$(CC) $(CFLAGS) -c src/ast.c -o $@

build/ast_to_json.o: src/ast_to_json.c include/ast_to_json.h include/ast.h
	$(CC) $(CFLAGS) -c src/ast_to_json.c -o $@

build/symbol_table.o: src/symbol_table.c include/symbol_table.h
	$(CC) $(CFLAGS) -c src/symbol_table.c -o $@

build/semantic.o: src/semantic.c include/semantic.h include/ast.h include/symbol_table.h
	$(CC) $(CFLAGS) -c src/semantic.c -o $@

build/main.o: include/semantic.h include/binary_format.h src/main.c build/parser.h include/ast.h include/symbol_table.h
	$(CC) $(CFLAGS) -c src/main.c -o $@

build/binary_format.o: src/binary_format.c include/binary_format.h scheduler/scheduler.h
	$(CC) $(CFLAGS) -c src/binary_format.c -o $@

# Code Gen
build/codegen.o: src/codegen/codegen.c include/codegen.h include/codegen_internal.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c src/codegen/codegen.c -o $@

build/codegen_context.o: src/codegen/codegen_context.c include/codegen.h include/codegen_internal.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c src/codegen/codegen_context.c -o $@

build/codegen_emit.o: src/codegen/codegen_emit.c include/codegen.h include/codegen_internal.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c src/codegen/codegen_emit.c -o $@

build/codegen_labels.o: src/codegen/codegen_labels.c include/codegen.h include/codegen_internal.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c src/codegen/codegen_labels.c -o $@

build/codegen_regs.o: src/codegen/codegen_regs.c include/codegen.h include/codegen_internal.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c src/codegen/codegen_regs.c -o $@

build/codegen_routine.o: src/codegen/codegen_routine.c include/codegen.h include/codegen_internal.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c src/codegen/codegen_routine.c -o $@

build/codegen_expr.o: src/codegen/codegen_expr.c include/codegen.h include/codegen_internal.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c src/codegen/codegen_expr.c -o $@

build/codegen_arithmetic.o: src/codegen/codegen_arithmetic.c include/codegen.h include/codegen_internal.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c src/codegen/codegen_arithmetic.c -o $@

build/codegen_stmt.o: src/codegen/codegen_stmt.c include/codegen.h include/codegen_internal.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c src/codegen/codegen_stmt.c -o $@

build/codegen_array.o: src/codegen/codegen_array.c include/codegen.h include/codegen_internal.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c src/codegen/codegen_array.c -o $@

build/codegen_call.o: src/codegen/codegen_call.c include/codegen.h include/codegen_internal.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c src/codegen/codegen_call.c -o $@

build/codegen_crypto.o: src/codegen/codegen_crypto.c include/codegen.h include/codegen_internal.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c src/codegen/codegen_crypto.c -o $@

# El ensamblador PORYGON se compila aqui para que el generador de ASM y el
# scheduler puedan compartir sus estructuras y funciones desde el compilador.
build/porygon_assembler/%.o: $(PORYGON_ASM_DIR)/%.c $(wildcard $(PORYGON_ASM_DIR)/*.h)
	@mkdir -p build/porygon_assembler
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJ)
	@mkdir -p bin
	$(CC) $(CFLAGS) $(OBJ) -o $@

build/scheduler/%.o: scheduler/%.c $(wildcard scheduler/*.h) $(wildcard $(PORYGON_ASM_DIR)/*.h)
	@mkdir -p build/scheduler
	$(CC) $(CFLAGS) -c $< -o $@

bin/porygon-schedule: tools/schedule_main.c $(PORYGON_ASM_OBJ) $(SCHEDULER_OBJ) scheduler/scheduler.h
	@mkdir -p bin
	$(CC) $(CFLAGS) tools/schedule_main.c $(PORYGON_ASM_OBJ) $(SCHEDULER_OBJ) -o $@

bin/test_scheduler: tests/scheduler/test_scheduler.c $(PORYGON_ASM_OBJ) $(SCHEDULER_OBJ) scheduler/scheduler.h
	@mkdir -p bin
	$(CC) $(CFLAGS) tests/scheduler/test_scheduler.c $(PORYGON_ASM_OBJ) $(SCHEDULER_OBJ) -o $@

test-scheduler: bin/test_scheduler
	./bin/test_scheduler

test-scheduler-rtl: bin/porygon-schedule
	python3 tests/scheduler/test_scheduler_rtl.py "$(PORYGON_RTL)"

test-cli-bin: bin/pipoc bin/porygon-schedule
	@set -eu; \
	tmp=$$(mktemp -d "$${TMPDIR:-/tmp}/pipoc-cli-bin.XXXXXX"); \
	trap 'rm -rf "$$tmp"' EXIT HUP INT TERM; \
	cp tests/codegen/call_simple.rwt "$$tmp/input.rwt"; \
	./bin/pipoc "$$tmp/input.rwt" -o "$$tmp/program.bin" \
		--emit-mem "$$tmp/direct.mem" --emit-asm "$$tmp/program.asm" \
		--emit-bundles "$$tmp/program.bundles" --ast-json "$$tmp/ast.json"; \
	header=$$(od -An -tx1 -N16 "$$tmp/program.bin" | tr -d ' \n'); \
	test "$$header" = "59524f50010000002000000000000000"; \
	tail=$$(od -An -tx1 -j20 -N12 "$$tmp/program.bin" | tr -d ' \n'); \
	test "$$tail" = "000001000000000000000000"; \
	text_size=$$(od -An -tu4 -j16 -N4 "$$tmp/program.bin" | tr -d ' '); \
	test "$$text_size" -gt 0; test $$((text_size % 8)) -eq 0; \
	file_size=$$(wc -c <"$$tmp/program.bin" | tr -d ' '); \
	test "$$file_size" -eq $$((text_size + 32)); \
	./bin/pipoc --bin-to-mem "$$tmp/program.bin" -o "$$tmp/from-bin.mem"; \
	cmp "$$tmp/direct.mem" "$$tmp/from-bin.mem"; \
	./bin/porygon-schedule "$$tmp/program.asm" "$$tmp/scheduler.bundles" \
		"$$tmp/scheduler.mem" >/dev/null; \
	cmp "$$tmp/direct.mem" "$$tmp/scheduler.mem"; \
	dd if=/dev/zero of="$$tmp/bad.bin" bs=32 count=1 2>/dev/null; \
	if ./bin/pipoc --bin-to-mem "$$tmp/bad.bin" -o "$$tmp/bad.mem" >/dev/null 2>&1; then exit 1; fi; \
	test ! -e "$$tmp/bad.mem"; \
	./bin/pipoc -x "$$tmp/program.asm" -o "$$tmp/from-asm.bin" \
		--emit-mem "$$tmp/from-asm.mem"; \
	cmp "$$tmp/direct.mem" "$$tmp/from-asm.mem"; \
	cp tests/codegen/call_simple.rwt "$$tmp/default.rwt"; \
	./bin/pipoc "$$tmp/default.rwt" -s -t -m -p >/dev/null; \
	test -f "$$tmp/default.bin"; test -f "$$tmp/default.asm"; \
	cp tests/codegen/call_simple.rwt "$$tmp/simple.rwt"; \
	./bin/pipoc asm "$$tmp/simple.rwt" >/dev/null; \
	test -s "$$tmp/simple.asm"; test ! -e "$$tmp/simple.bin"; \
	./bin/pipoc sch "$$tmp/simple.rwt" >/dev/null; test -s "$$tmp/simple.bundles"; \
	./bin/pipoc mem "$$tmp/simple.rwt" >/dev/null; test -s "$$tmp/simple.mem"; \
	./bin/pipoc ins "$$tmp/simple.rwt" >/dev/null; \
	test -s "$$tmp/simple.ast.json"; test -s "$$tmp/simple.symbols.txt"; \
	./bin/pipoc all "$$tmp/simple.rwt" >/dev/null; \
	test -s "$$tmp/simple.bin"; test -s "$$tmp/simple.asm"; \
	test -s "$$tmp/simple.bundles"; test -s "$$tmp/simple.mem"; \
	cmp "$$tmp/simple.mem" "$$tmp/direct.mem"; \
	./bin/pipoc asm "$$tmp/simple.rwt" -o "$$tmp/custom.asm" >/dev/null; \
	test -s "$$tmp/custom.asm"; \
	before=$$(cksum "$$tmp/default.rwt"); \
	if ./bin/pipoc "$$tmp/default.rwt" -o "$$tmp/default.rwt" >/dev/null 2>&1; then exit 1; fi; \
	after=$$(cksum "$$tmp/default.rwt"); test "$$before" = "$$after"; \
	echo "test-cli-bin: encabezado, round-trip, comandos simples y B04 correctos"

test-integration: bin/pipoc
	./run.sh tests/codegen/call_simple.rwt 1000 \
		--expect r1=10 --expect r7=0x20000 --expect errors=0

test-extract-wrapper: test-integration
	@set -eu; \
	tmp=$$(mktemp -d "$${TMPDIR:-/tmp}/piper-extract.XXXXXX"); \
	trap 'rm -rf "$$tmp"' EXIT HUP INT TERM; \
	porygon_dir=$${PORYGON_DIR:-../../ProyectoArqui/PORYGON}; \
	test -x "$$porygon_dir/extract.sh"; \
	dump=$$(cd build/runs/call_simple && pwd)/memory.dump.mem; \
	"$$porygon_dir/extract.sh" "$$dump" \
		0x10000 8 "$$tmp/output.bin" >/dev/null; \
	test "$$(wc -c <"$$tmp/output.bin" | tr -d ' ')" = 8; \
	test "$$(od -An -tx1 "$$tmp/output.bin" | tr -d ' \n')" = 0000000000000000; \
	"$$porygon_dir/extract.sh" "$$dump" 0x10000 8 \
		"$$tmp/explicit.bin" >/dev/null; \
	cmp "$$tmp/output.bin" "$$tmp/explicit.bin"; \
	if "$$porygon_dir/extract.sh" "$$dump" 0x10000 8 \
		"$$dump" >/dev/null 2>&1; then exit 1; fi; \
	echo "test-extract-wrapper: ruta explícita, rango y salida correctos"

# No silencia conflictos: inspecciona el informe que Bison genera con -v.
conflicts: build/parser.c
	@if grep -Eq '[1-9][0-9]* (shift/reduce|reduce/reduce)' build/parser.output; then \
		echo "Bison reportó conflictos:"; \
		grep -E '[1-9][0-9]* (shift/reduce|reduce/reduce)' build/parser.output; \
		exit 1; \
	else \
		echo "Bison reportó 0 conflictos."; \
	fi

clean:
	rm -rf build bin
