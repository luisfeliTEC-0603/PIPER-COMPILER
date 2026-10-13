CC = gcc
FLEX ?= flex

# Si el bison de Homebrew existe (macOS Apple Silicon), usarlo por defecto para evitar
# usar el bison desactualizado del sistema. En Linux/Windows usará 'bison' del PATH.
ifeq ($(wildcard /opt/homebrew/opt/bison/bin/bison),)
    BISON ?= bison
else
    BISON ?= /opt/homebrew/opt/bison/bin/bison
endif

CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Iinclude -Ibuild
TARGET = bin/pipoc

OBJ = build/parser.o \
      build/lexer.o \
      build/ast.o \
      build/ast_to_json.o \
      build/symbol_table.o \
      build/main.o \
      build/semantic.o \
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
      build/codegen_crypto.o

.DELETE_ON_ERROR:
.PHONY: all generate conflicts clean

all: $(TARGET)

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

build/main.o: include/semantic.h src/main.c build/parser.h include/ast.h include/symbol_table.h
	$(CC) $(CFLAGS) -c src/main.c -o $@

# Code Gen
build/codegen.o: src/codegen/codegen.c include/codegen.h include/codegen_internal.h
	$(CC) $(CFLAGS) -c src/codegen/codegen.c -o $@

build/codegen_context.o: src/codegen/codegen_context.c include/codegen.h include/codegen_internal.h
	$(CC) $(CFLAGS) -c src/codegen/codegen_context.c -o $@

build/codegen_emit.o: src/codegen/codegen_emit.c include/codegen.h include/codegen_internal.h
	$(CC) $(CFLAGS) -c src/codegen/codegen_emit.c -o $@

build/codegen_labels.o: src/codegen/codegen_labels.c include/codegen.h include/codegen_internal.h
	$(CC) $(CFLAGS) -c src/codegen/codegen_labels.c -o $@

build/codegen_regs.o: src/codegen/codegen_regs.c include/codegen.h include/codegen_internal.h
	$(CC) $(CFLAGS) -c src/codegen/codegen_regs.c -o $@

build/codegen_routine.o: src/codegen/codegen_routine.c include/codegen.h include/codegen_internal.h
	$(CC) $(CFLAGS) -c src/codegen/codegen_routine.c -o $@

build/codegen_expr.o: src/codegen/codegen_expr.c include/codegen.h include/codegen_internal.h
	$(CC) $(CFLAGS) -c src/codegen/codegen_expr.c -o $@

build/codegen_arithmetic.o: src/codegen/codegen_arithmetic.c include/codegen.h include/codegen_internal.h
	$(CC) $(CFLAGS) -c src/codegen/codegen_arithmetic.c -o $@

build/codegen_stmt.o: src/codegen/codegen_stmt.c include/codegen.h include/codegen_internal.h
	$(CC) $(CFLAGS) -c src/codegen/codegen_stmt.c -o $@

build/codegen_array.o: src/codegen/codegen_array.c include/codegen.h include/codegen_internal.h
	$(CC) $(CFLAGS) -c src/codegen/codegen_array.c -o $@

build/codegen_call.o: src/codegen/codegen_call.c include/codegen.h include/codegen_internal.h
	$(CC) $(CFLAGS) -c src/codegen/codegen_call.c -o $@

build/codegen_crypto.o: src/codegen/codegen_crypto.c include/codegen.h include/codegen_internal.h
	$(CC) $(CFLAGS) -c src/codegen/codegen_crypto.c -o $@

$(TARGET): $(OBJ)
	@mkdir -p bin
	$(CC) $(CFLAGS) $(OBJ) -o $@

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
