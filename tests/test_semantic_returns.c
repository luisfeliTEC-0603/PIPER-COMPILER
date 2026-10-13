#include "ast.h"
#include "semantic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SourceLocation location = {1, 1, 1, 1};

static void check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "test_semantic_returns: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static char *copy(const char *text)
{
    size_t size = strlen(text) + 1;
    char *result = malloc(size);
    check(result != NULL, "sin memoria para nombre");
    memcpy(result, text, size);
    return result;
}

static AstNodeList *new_list(void)
{
    AstNodeList *result = ast_node_list_create();
    check(result != NULL, "sin memoria para lista");
    return result;
}

static void append(AstNodeList *list, AstNode *node)
{
    check(node != NULL && ast_node_list_append(list, node), "no se pudo agregar un nodo");
}

static AstNode *block(AstNodeList *statements)
{
    AstNode *result = ast_new_block(statements, location);
    check(result != NULL, "sin memoria para bloque");
    return result;
}

static AstNode *output(void)
{
    AstNode *result = ast_new_output(ast_new_integer_literal(1, location), location);
    check(result != NULL, "sin memoria para output");
    return result;
}

static AstNode *block_with_output(void)
{
    AstNodeList *statements = new_list();
    append(statements, output());
    return block(statements);
}

static AstNode *when_with(int then_returns, int fallback_present, int fallback_returns)
{
    AstNode *then_block = then_returns ? block_with_output() : block(new_list());
    AstNode *fallback = fallback_present ?
        (fallback_returns ? block_with_output() : block(new_list())) : NULL;
    AstNode *result = ast_new_when(ast_new_identifier(copy("condition"), location),
                                   then_block, fallback, location);
    check(result != NULL, "sin memoria para when");
    return result;
}

static AstNode *when_literal(int condition)
{
    AstNode *result = ast_new_when(ast_new_boolean_literal(condition != 0, location),
                                   block_with_output(), NULL, location);
    check(result != NULL, "sin memoria para when literal");
    return result;
}

static void expect_result(const char *name, AstNode *body, int expected)
{
    AstNodeList *parameters = new_list();
    append(parameters, ast_new_parameter(PIPER_TYPE_BOOL, copy("condition"),
                                         new_list(), location));
    AstNode *routine = ast_new_routine_declaration(PIPER_TYPE_B32, copy("f"),
                                                   parameters, body, location);
    check(routine != NULL, "sin memoria para rutina");
    AstNodeList *elements = new_list();
    append(elements, routine);
    AstNode *program = ast_new_program(elements, location);
    SymbolTable *symbols = symbol_table_create();
    check(program != NULL && symbols != NULL, "sin memoria para programa");
    int actual = analyze_semantics(program, symbols);
    if (actual != expected) {
        fprintf(stderr, "test_semantic_returns: fallo en %s (esperado %d, recibido %d)\n",
                name, expected, actual);
        exit(EXIT_FAILURE);
    }
    ast_free(program);
    symbol_table_destroy(symbols);
}

int main(void)
{
    expect_result("output directo", block_with_output(), 1);
    expect_result("sin output", block(new_list()), 0);

    AstNodeList *statements = new_list();
    append(statements, when_with(1, 0, 0));
    expect_result("when sin fallback", block(statements), 0);

    statements = new_list();
    append(statements, when_literal(1));
    expect_result("when true siempre devuelve", block(statements), 1);

    statements = new_list();
    append(statements, when_literal(0));
    expect_result("when false no devuelve", block(statements), 0);

    statements = new_list();
    append(statements, when_with(1, 1, 1));
    expect_result("ambas ramas devuelven", block(statements), 1);

    statements = new_list();
    append(statements, when_with(1, 1, 0));
    expect_result("fallback sin output", block(statements), 0);

    statements = new_list();
    append(statements, when_with(1, 0, 0));
    append(statements, output());
    expect_result("output posterior", block(statements), 1);

    statements = new_list();
    append(statements, ast_new_alwhen(ast_new_identifier(copy("condition"), location),
                                      block_with_output(), location));
    expect_result("ciclo que puede no ejecutarse", block(statements), 0);

    statements = new_list();
    append(statements, ast_new_alwhen(ast_new_identifier(copy("condition"), location),
                                      block_with_output(), location));
    append(statements, output());
    expect_result("output despues del ciclo", block(statements), 1);

    statements = new_list();
    append(statements, ast_new_routine_declaration(PIPER_TYPE_B32, copy("inner"),
                                                   new_list(), block_with_output(), location));
    expect_result("rutina anidada no devuelve por la exterior", block(statements), 0);

    statements = new_list();
    append(statements, block_with_output());
    expect_result("output en bloque interior", block(statements), 1);

    puts("test_semantic_returns: 12 casos correctos");
    return EXIT_SUCCESS;
}
