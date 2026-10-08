CC ?= cc
FLEX ?= flex
BISON ?= bison

CPPFLAGS = -Iinclude -Ibuild
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic

.PHONY: all generate conflicts clean

all: bin/pipoc

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
	$(CC) $(CPPFLAGS) $(CFLAGS) -c build/parser.c -o $@

# Flex 2.6.4 genera una comparación signed/unsigned fuera del código del equipo.
build/lexer.o: build/lexer.c build/parser.h include/ast.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-sign-compare -c build/lexer.c -o $@

build/ast.o: src/ast.c include/ast.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/ast.c -o $@

build/main.o: src/main.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/main.c -o $@

bin/pipoc: build/parser.o build/lexer.o build/ast.o build/main.o
	@mkdir -p bin
	$(CC) $(CFLAGS) $^ -o $@

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
