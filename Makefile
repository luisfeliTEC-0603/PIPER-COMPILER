CC = gcc
FLEX ?= flex
BISON ?= bison

CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Iinclude -Ibuild
OBJ = build/parser.o build/lexer.o build/ast.o build/main.o
TARGET = bin/pipoc

.DELETE_ON_ERROR:
.PHONY: all generate conflicts clean

all: $(TARGET)

generate: build/parser.c build/parser.h build/lexer.c

# Bison debe ejecutarse antes de Flex porque lexer.l incluye parser.h.
build/parser.c: src/parser.y include/ast.h
	@mkdir -p build
	$(BISON) -d -v -o build/parser.c src/parser.y

build/parser.h: build/parser.c
	@test -f build/parser.h

build/lexer.c: src/lexer.l build/parser.h include/ast.h
	@mkdir -p build
	$(FLEX) -o build/lexer.c src/lexer.l

build/parser.o: build/parser.c build/parser.h include/ast.h
	$(CC) $(CFLAGS) -c build/parser.c -o $@

# Flex 2.6.4 genera una comparación signed/unsigned fuera del código del equipo.
build/lexer.o: build/lexer.c build/parser.h include/ast.h
	$(CC) $(CFLAGS) -Wno-sign-compare -c build/lexer.c -o $@

build/ast.o: src/ast.c include/ast.h
	$(CC) $(CFLAGS) -c src/ast.c -o $@

build/main.o: src/main.c build/parser.h include/ast.h
	$(CC) $(CFLAGS) -c src/main.c -o $@

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
