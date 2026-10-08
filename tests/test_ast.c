#include "ast.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require_true(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "test_ast: %s\n", message);
        exit(EXIT_FAILURE);
    }
}

static char *copy_text(const char *source)
{
    size_t length = strlen(source) + 1;
    char *copy = malloc(length);
    require_true(copy != NULL, "no se pudo copiar una cadena");
    memcpy(copy, source, length);
    return copy;
}

static AstNodeList *new_list(void)
{
    AstNodeList *list = ast_node_list_create();
    require_true(list != NULL, "no se pudo crear una lista");
    return list;
}

static void append_node(AstNodeList *list, AstNode *node)
{
    require_true(node != NULL, "no se pudo crear un nodo para la lista");

    if (!ast_node_list_append(list, node)) {
        ast_free(node);
        require_true(false, "no se pudo agregar un nodo a la lista");
    }
}

static AstNode *make_empty_block(SourceLocation location)
{
    AstNode *block = ast_new_block(new_list(), location);
    require_true(block != NULL, "no se pudo crear un bloque vacío");
    return block;
}

static AstNode *make_block_with(AstNode *statement,
                                SourceLocation location)
{
    AstNodeList *statements = new_list();
    append_node(statements, statement);

    AstNode *block = ast_new_block(statements, location);
    require_true(block != NULL, "no se pudo crear un bloque");
    return block;
}

static char *read_stream(FILE *stream)
{
    require_true(fflush(stream) == 0,
                 "no se pudo vaciar el flujo de impresión");
    require_true(fseek(stream, 0, SEEK_END) == 0,
                 "no se pudo buscar el final del flujo");

    long length = ftell(stream);
    require_true(length >= 0, "no se pudo obtener el tamaño del flujo");
    require_true(fseek(stream, 0, SEEK_SET) == 0,
                 "no se pudo regresar al inicio del flujo");

    char *text = malloc((size_t)length + 1);
    require_true(text != NULL, "no se pudo reservar la salida impresa");

    size_t bytes_read = fread(text, 1, (size_t)length, stream);
    require_true(bytes_read == (size_t)length,
                 "no se pudo leer la salida impresa");
    text[(size_t)length] = '\0';

    return text;
}

static void test_locations_and_names(void)
{
    SourceLocation expected = source_location_make(2, 3, 4, 5);
    AstNode *literal = ast_new_integer_literal(42, expected);
    require_true(literal != NULL, "no se pudo crear el literal entero");
    require_true(ast_node_kind(literal) == AST_INTEGER_LITERAL,
                 "el literal tiene un kind incorrecto");

    SourceLocation actual = ast_node_location(literal);
    require_true(actual.first_line == expected.first_line,
                 "first_line no coincide");
    require_true(actual.first_column == expected.first_column,
                 "first_column no coincide");
    require_true(actual.last_line == expected.last_line,
                 "last_line no coincide");
    require_true(actual.last_column == expected.last_column,
                 "last_column no coincide");

    require_true(strcmp(piper_type_name(PIPER_TYPE_B32), "B32") == 0,
                 "nombre incorrecto para PIPER_TYPE_B32");
    require_true(strcmp(piper_type_name(PIPER_TYPE_BOOL), "bool") == 0,
                 "nombre incorrecto para PIPER_TYPE_BOOL");
    require_true(strcmp(ast_operator_name(AST_OP_ADD), "add") == 0,
                 "nombre incorrecto para AST_OP_ADD");
    require_true(strcmp(ast_operator_name(AST_OP_LOGICAL_AND),
                        "logical_and") == 0,
                 "nombre incorrecto para AST_OP_LOGICAL_AND");
    require_true(strcmp(piper_type_name((PiperType)-1),
                        "<invalid-type>") == 0,
                 "un tipo inválido no fue identificado");
    require_true(strcmp(ast_operator_name((AstOperator)-1),
                        "<invalid-operator>") == 0,
                 "un operador inválido no fue identificado");

    ast_free(literal);
}

static void test_list_growth(void)
{
    SourceLocation location = source_location_make(1, 1, 1, 1);
    AstNodeList *list = new_list();

    for (unsigned long long value = 0; value < 9; value++) {
        append_node(list, ast_new_integer_literal(value, location));
    }

    AstNode *retained = ast_new_integer_literal(99, location);
    require_true(retained != NULL, "no se pudo crear el nodo retenido");
    require_true(!ast_node_list_append(NULL, retained),
                 "append aceptó una lista nula");
    ast_free(retained);

    require_true(!ast_node_list_append(list, NULL),
                 "append aceptó un nodo nulo");
    ast_node_list_free(list);
}

static void test_declaration_program_and_print(void)
{
    SourceLocation location = source_location_make(1, 1, 1, 18);
    AstNode *declaration = ast_new_variable_declaration(
        PIPER_TYPE_B32, copy_text("edad"), new_list(), location);
    require_true(declaration != NULL,
                 "no se pudo crear la declaración escalar");

    AstNodeList *elements = new_list();
    append_node(elements, declaration);
    AstNode *program = ast_new_program(elements, location);
    require_true(program != NULL, "no se pudo crear el programa");

    FILE *stream = tmpfile();
    require_true(stream != NULL, "no se pudo crear el flujo temporal");
    ast_print(stream, program);

    char *text = read_stream(stream);
    require_true(strstr(text, "Program [1:1-1:18]") != NULL,
                 "la impresión no contiene Program");
    require_true(strstr(text, "VariableDeclaration") != NULL,
                 "la impresión no contiene VariableDeclaration");
    require_true(strstr(text, "type: B32") != NULL,
                 "la impresión no contiene el tipo B32");
    require_true(strstr(text, "name: \"edad\"") != NULL,
                 "la impresión no contiene el nombre edad");
    require_true(strstr(text, "dimensions: []") != NULL,
                 "la impresión no muestra las dimensiones vacías");

    free(text);
    fclose(stream);
    ast_free(program);
}

static void test_expressions_and_assignment(void)
{
    SourceLocation location = source_location_make(3, 1, 3, 24);
    AstNode *left = ast_new_integer_literal(20, location);
    AstNode *right = ast_new_unary_expression(
        AST_OP_NEGATE, ast_new_integer_literal(2, location), location);
    AstNode *sum = ast_new_binary_expression(
        AST_OP_ADD, left, right, location);
    require_true(sum != NULL, "no se pudo crear la expresión binaria");

    AstNode *assignment = ast_new_assignment(
        ast_new_identifier(copy_text("edad"), location), sum, location);
    require_true(assignment != NULL, "no se pudo crear la asignación");
    require_true(ast_node_kind(assignment) == AST_ASSIGNMENT,
                 "la asignación tiene un kind incorrecto");

    ast_free(assignment);
}

static void test_routine_and_control_flow(void)
{
    SourceLocation location = source_location_make(1, 1, 12, 1);
    AstNode *parameter = ast_new_parameter(
        PIPER_TYPE_B32, copy_text("value"), new_list(), location);
    require_true(parameter != NULL, "no se pudo crear el parámetro");
    AstNodeList *parameters = new_list();
    append_node(parameters, parameter);

    AstNode *when_statement = ast_new_when(
        ast_new_boolean_literal(true, location),
        make_block_with(
            ast_new_output(
                ast_new_identifier(copy_text("value"), location), location),
            location),
        NULL, location);
    require_true(when_statement != NULL,
                 "no se pudo crear when sin fallback");

    AstNode *alwhen_statement = ast_new_alwhen(
        ast_new_boolean_literal(false, location),
        make_block_with(ast_new_break(location), location), location);
    require_true(alwhen_statement != NULL,
                 "no se pudo crear alwhen");

    AstNode *loop_statement = ast_new_loop(
        copy_text("position"),
        ast_new_integer_literal(0, location),
        ast_new_integer_literal(4, location),
        ast_new_integer_literal(1, location),
        make_block_with(ast_new_break(location), location), location);
    require_true(loop_statement != NULL, "no se pudo crear loop");

    AstNodeList *arguments = new_list();
    append_node(arguments,
                ast_new_identifier(copy_text("value"), location));
    AstNode *call = ast_new_call(copy_text("identity"), arguments, location);
    require_true(call != NULL, "no se pudo crear la llamada");
    AstNode *final_output = ast_new_output(call, location);
    require_true(final_output != NULL, "no se pudo crear output");

    AstNodeList *statements = new_list();
    append_node(statements, when_statement);
    append_node(statements, alwhen_statement);
    append_node(statements, loop_statement);
    append_node(statements, final_output);

    AstNode *routine = ast_new_routine_declaration(
        PIPER_TYPE_B32, copy_text("identity"), parameters,
        ast_new_block(statements, location), location);
    require_true(routine != NULL, "no se pudo crear la rutina");
    ast_free(routine);
}

static void test_arrays_indices_and_imports(void)
{
    SourceLocation location = source_location_make(1, 1, 4, 1);
    AstNode *import = ast_new_import_declaration(
        copy_text("transform"), copy_text("cipher.rwt"), location);
    require_true(import != NULL, "no se pudo crear la importación");
    ast_free(import);

    AstNodeList *elements = new_list();
    append_node(elements, ast_new_integer_literal(1, location));
    append_node(elements, ast_new_none_literal(location));
    AstNode *array = ast_new_array_literal(elements, location);
    require_true(array != NULL, "no se pudo crear el arreglo");

    AstNodeList *indices = new_list();
    append_node(indices, ast_new_integer_literal(0, location));
    append_node(indices, ast_new_integer_literal(1, location));
    AstNode *index = ast_new_index_expression(
        ast_new_identifier(copy_text("matrix"), location),
        indices, location);
    require_true(index != NULL, "no se pudo crear la indexación");

    AstNode *assignment = ast_new_assignment(index, array, location);
    require_true(assignment != NULL,
                 "no se pudo crear la asignación indexada");
    ast_free(assignment);
}

static void test_rejected_arguments(void)
{
    SourceLocation location = source_location_make(1, 1, 1, 1);

    require_true(ast_new_program(NULL, location) == NULL,
                 "program aceptó una lista nula");
    require_true(ast_new_import_declaration(
                     NULL, copy_text("invalid.rwt"), location) == NULL,
                 "import aceptó un símbolo nulo");
    require_true(ast_new_variable_declaration(
                     PIPER_TYPE_B32, copy_text("invalid"), NULL,
                     location) == NULL,
                 "variable aceptó dimensiones nulas");
    require_true(ast_new_block(NULL, location) == NULL,
                 "block aceptó una lista nula");
    require_true(ast_new_assignment(
                     ast_new_identifier(copy_text("target"), location),
                     NULL, location) == NULL,
                 "assignment aceptó un valor nulo");
    require_true(ast_new_when(
                     ast_new_boolean_literal(true, location),
                     NULL, make_empty_block(location), location) == NULL,
                 "when aceptó un bloque principal nulo");
    require_true(ast_new_loop(
                     copy_text("position"),
                     ast_new_integer_literal(0, location),
                     ast_new_integer_literal(4, location),
                     NULL, make_empty_block(location), location) == NULL,
                 "loop aceptó un paso nulo");
    require_true(ast_new_array_literal(NULL, location) == NULL,
                 "array aceptó una lista nula");
    require_true(ast_new_index_expression(
                     ast_new_identifier(copy_text("matrix"), location),
                     NULL, location) == NULL,
                 "index aceptó índices nulos");

    ast_free(NULL);
    ast_node_list_free(NULL);
}

int main(void)
{
    test_locations_and_names();
    test_list_growth();
    test_declaration_program_and_print();
    test_expressions_and_assignment();
    test_routine_and_control_flow();
    test_arrays_indices_and_imports();
    test_rejected_arguments();

    puts("test_ast: todas las pruebas básicas pasaron");
    return EXIT_SUCCESS;
}
