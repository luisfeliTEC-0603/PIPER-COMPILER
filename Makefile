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
OBJ = build/parser.o build/lexer.o build/ast.o build/ast_to_json.o build/symbol_table.o build/main.o build/semantic.o $(PORYGON_ASM_OBJ)
TARGET = bin/pipoc

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

# El ensamblador PORYGON se compila aqui para que el generador de ASM y el
# scheduler puedan compartir sus estructuras y funciones desde el compilador.
build/porygon_assembler/%.o: $(PORYGON_ASM_DIR)/%.c $(wildcard $(PORYGON_ASM_DIR)/*.h)
	@mkdir -p build/porygon_assembler
	$(CC) $(CFLAGS) -c $< -o $@

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
