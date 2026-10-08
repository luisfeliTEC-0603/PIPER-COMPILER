#include "ast.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

/*
 * Implementación del AST de Piper.
 *
 * El archivo se organiza en representación privada, utilidades comunes,
 * constructores por familia, inspección, impresión y liberación recursiva.
 * Las estructuras permanecen opacas fuera de este módulo.
 */

/* Representación privada del árbol y de las listas que poseen sus nodos. */

/* Arreglo dinámico que posee todos los nodos agregados exitosamente. */
struct AstNodeList {
    AstNode **items;
    size_t count;
    size_t capacity;
};

/*
 * kind identifica el miembro activo de data. Los campos kind y location son
 * comunes a todas las variantes.
 */
struct AstNode {
    AstKind kind;
    SourceLocation location;

    union {
        struct {
            AstNodeList *elements;
        } program;

        struct {
            char *symbol;
            char *file_path;
        } import_declaration;

        struct {
            PiperType type;
            char *name;
            AstNodeList *dimensions;
        } variable_declaration;

        struct {
            PiperType type;
            char *name;
            AstNodeList *dimensions;
        } parameter;

        struct {
            PiperType return_type;
            char *name;
            AstNodeList *parameters;
            AstNode *body;
        } routine_declaration;

        struct {
            AstNodeList *statements;
        } block;

        struct {
            AstNode *value;
        } output_statement;

        struct {
            char *routine_name;
            AstNodeList *arguments;
        } call_expression;

        struct {
            unsigned long long value;
        } integer_literal;

        struct {
            bool value;
        } boolean_literal;

        struct {
            AstNodeList *elements;
        } array_literal;

        struct {
            AstNode *base;
            AstNodeList *indices;
        } index_expression;

        struct {
            char *name;
        } identifier;

        struct {
            AstOperator operator;
            AstNode *operand;
        } unary_expression;

        struct {
            AstOperator operator;
            AstNode *left;
            AstNode *right;
        } binary_expression;

        struct {
            AstNode *target;
            AstNode *value;
        } assignment;

        struct {
            AstNode *condition;
            AstNode *then_block;
            AstNode *fallback_block;
        } when_statement;

        struct {
            AstNode *condition;
            AstNode *body;
        } alwhen_statement;

        struct {
            char *control_name;
            AstNode *begin;
            AstNode *end;
            AstNode *step;
            AstNode *body;
        } loop_statement;
    } data;
};

/* Reserva e inicialización de los campos comunes a todos los nodos. */
static AstNode *ast_node_create(AstKind kind, SourceLocation location)
{
    AstNode *node = malloc(sizeof(*node));
    if (node == NULL) {
        return NULL;
    }

    node->kind = kind;
    node->location = location;

    return node;
}

/* Construcción de intervalos de ubicación en el código fuente. */

/* Construye una ubicación sin interpretar ni validar sus coordenadas. */
SourceLocation source_location_make(int first_line, int first_column,
                                    int last_line, int last_column)
{
    SourceLocation location = {
        .first_line = first_line,
        .first_column = first_column,
        .last_line = last_line,
        .last_column = last_column
    };

    return location;
}

/* Administración de listas dinámicas que poseen sus nodos. */

AstNodeList *ast_node_list_create(void)
{
    AstNodeList *list = malloc(sizeof(*list));
    if (list == NULL) {
        return NULL;
    }

    list->items = NULL;
    list->count = 0;
    list->capacity = 0;

    return list;
}

bool ast_node_list_append(AstNodeList *list, AstNode *node)
{
    if (list == NULL || node == NULL) {
        return false;
    }

    if (list->count == list->capacity) {
        size_t new_capacity;

        if (list->capacity == 0) {
            new_capacity = 4;
        } else {
            if (list->capacity > SIZE_MAX / 2) {
                return false;
            }
            new_capacity = list->capacity * 2;
        }

        if (new_capacity > SIZE_MAX / sizeof(*list->items)) {
            return false;
        }

        AstNode **new_items =
            realloc(list->items, new_capacity * sizeof(*new_items));
        if (new_items == NULL) {
            return false;
        }

        list->items = new_items;
        list->capacity = new_capacity;
    }

    list->items[list->count] = node;
    list->count++;

    return true;
}

void ast_node_list_free(AstNodeList *list)
{
    if (list == NULL) {
        return;
    }

    for (size_t index = 0; index < list->count; index++) {
        ast_free(list->items[index]);
    }

    free(list->items);
    free(list);
}

/* Constructores para la raíz del programa y sus importaciones. */

AstNode *ast_new_program(AstNodeList *elements, SourceLocation location)
{
    if (elements == NULL) {
        return NULL;
    }

    AstNode *node = ast_node_create(AST_PROGRAM, location);
    if (node == NULL) {
        ast_node_list_free(elements);
        return NULL;
    }

    node->data.program.elements = elements;

    return node;
}

AstNode *ast_new_import_declaration(char *symbol, char *file_path,
                                    SourceLocation location)
{
    if (symbol == NULL || file_path == NULL) {
        free(symbol);
        free(file_path);
        return NULL;
    }

    AstNode *node = ast_node_create(AST_IMPORT_DECLARATION, location);
    if (node == NULL) {
        free(symbol);
        free(file_path);
        return NULL;
    }

    node->data.import_declaration.symbol = symbol;
    node->data.import_declaration.file_path = file_path;

    return node;
}

/* Constructores para declaraciones, parámetros, rutinas y bloques. */

AstNode *ast_new_variable_declaration(PiperType type, char *name,
                                      AstNodeList *dimensions,
                                      SourceLocation location)
{
    if (name == NULL || dimensions == NULL) {
        free(name);
        ast_node_list_free(dimensions);
        return NULL;
    }

    AstNode *node = ast_node_create(AST_VARIABLE_DECLARATION, location);
    if (node == NULL) {
        free(name);
        ast_node_list_free(dimensions);
        return NULL;
    }

    node->data.variable_declaration.type = type;
    node->data.variable_declaration.name = name;
    node->data.variable_declaration.dimensions = dimensions;

    return node;
}

AstNode *ast_new_parameter(PiperType type, char *name,
                           AstNodeList *dimensions,
                           SourceLocation location)
{
    if (name == NULL || dimensions == NULL) {
        free(name);
        ast_node_list_free(dimensions);
        return NULL;
    }

    AstNode *node = ast_node_create(AST_PARAMETER, location);
    if (node == NULL) {
        free(name);
        ast_node_list_free(dimensions);
        return NULL;
    }

    node->data.parameter.type = type;
    node->data.parameter.name = name;
    node->data.parameter.dimensions = dimensions;

    return node;
}

AstNode *ast_new_routine_declaration(PiperType return_type, char *name,
                                     AstNodeList *parameters, AstNode *body,
                                     SourceLocation location)
{
    if (name == NULL || parameters == NULL || body == NULL) {
        free(name);
        ast_node_list_free(parameters);
        ast_free(body);
        return NULL;
    }

    AstNode *node = ast_node_create(AST_ROUTINE_DECLARATION, location);
    if (node == NULL) {
        free(name);
        ast_node_list_free(parameters);
        ast_free(body);
        return NULL;
    }

    node->data.routine_declaration.return_type = return_type;
    node->data.routine_declaration.name = name;
    node->data.routine_declaration.parameters = parameters;
    node->data.routine_declaration.body = body;

    return node;
}

AstNode *ast_new_block(AstNodeList *statements, SourceLocation location)
{
    if (statements == NULL) {
        return NULL;
    }

    AstNode *node = ast_node_create(AST_BLOCK, location);
    if (node == NULL) {
        ast_node_list_free(statements);
        return NULL;
    }

    node->data.block.statements = statements;

    return node;
}

/* Constructores para retornos y llamadas a rutinas. */

AstNode *ast_new_output(AstNode *value, SourceLocation location)
{
    if (value == NULL) {
        return NULL;
    }

    AstNode *node = ast_node_create(AST_OUTPUT_STATEMENT, location);
    if (node == NULL) {
        ast_free(value);
        return NULL;
    }

    node->data.output_statement.value = value;

    return node;
}

AstNode *ast_new_call(char *routine_name, AstNodeList *arguments,
                      SourceLocation location)
{
    if (routine_name == NULL || arguments == NULL) {
        free(routine_name);
        ast_node_list_free(arguments);
        return NULL;
    }

    AstNode *node = ast_node_create(AST_CALL_EXPRESSION, location);
    if (node == NULL) {
        free(routine_name);
        ast_node_list_free(arguments);
        return NULL;
    }

    node->data.call_expression.routine_name = routine_name;
    node->data.call_expression.arguments = arguments;

    return node;
}

/* Constructores para literales y expresiones. */

AstNode *ast_new_integer_literal(unsigned long long value,
                                 SourceLocation location)
{
    AstNode *node = ast_node_create(AST_INTEGER_LITERAL, location);
    if (node == NULL) {
        return NULL;
    }

    node->data.integer_literal.value = value;

    return node;
}

AstNode *ast_new_boolean_literal(bool value, SourceLocation location)
{
    AstNode *node = ast_node_create(AST_BOOLEAN_LITERAL, location);
    if (node == NULL) {
        return NULL;
    }

    node->data.boolean_literal.value = value;

    return node;
}

AstNode *ast_new_none_literal(SourceLocation location)
{
    /* El kind representa por completo al literal none. */
    return ast_node_create(AST_NONE_LITERAL, location);
}

AstNode *ast_new_array_literal(AstNodeList *elements,
                               SourceLocation location)
{
    if (elements == NULL) {
        return NULL;
    }

    AstNode *node = ast_node_create(AST_ARRAY_LITERAL, location);
    if (node == NULL) {
        ast_node_list_free(elements);
        return NULL;
    }

    node->data.array_literal.elements = elements;

    return node;
}

AstNode *ast_new_index_expression(AstNode *base, AstNodeList *indices,
                                  SourceLocation location)
{
    if (base == NULL || indices == NULL) {
        ast_free(base);
        ast_node_list_free(indices);
        return NULL;
    }

    AstNode *node = ast_node_create(AST_INDEX_EXPRESSION, location);
    if (node == NULL) {
        ast_free(base);
        ast_node_list_free(indices);
        return NULL;
    }

    node->data.index_expression.base = base;
    node->data.index_expression.indices = indices;

    return node;
}

AstNode *ast_new_identifier(char *name, SourceLocation location)
{
    if (name == NULL) {
        return NULL;
    }

    AstNode *node =
        ast_node_create(AST_IDENTIFIER_EXPRESSION, location);

    if (node == NULL) {
        free(name);
        return NULL;
    }

    node->data.identifier.name = name;

    return node;
}

AstNode *ast_new_unary_expression(AstOperator operator, AstNode *operand,
                                  SourceLocation location)
{
    if (operand == NULL) {
        return NULL;
    }

    AstNode *node = ast_node_create(AST_UNARY_EXPRESSION, location);
    if (node == NULL) {
        ast_free(operand);
        return NULL;
    }

    node->data.unary_expression.operator = operator;
    node->data.unary_expression.operand = operand;

    return node;
}

AstNode *ast_new_binary_expression(AstOperator operator, AstNode *left,
                                   AstNode *right, SourceLocation location)
{
    if (left == NULL || right == NULL) {
        ast_free(left);
        ast_free(right);
        return NULL;
    }

    AstNode *node = ast_node_create(AST_BINARY_EXPRESSION, location);
    if (node == NULL) {
        ast_free(left);
        ast_free(right);
        return NULL;
    }

    node->data.binary_expression.operator = operator;
    node->data.binary_expression.left = left;
    node->data.binary_expression.right = right;

    return node;
}

/* Constructores para instrucciones y estructuras de control. */

AstNode *ast_new_assignment(AstNode *target, AstNode *value,
                            SourceLocation location)
{
    if (target == NULL || value == NULL) {
        ast_free(target);
        ast_free(value);
        return NULL;
    }

    AstNode *node = ast_node_create(AST_ASSIGNMENT, location);
    if (node == NULL) {
        ast_free(target);
        ast_free(value);
        return NULL;
    }

    node->data.assignment.target = target;
    node->data.assignment.value = value;

    return node;
}

AstNode *ast_new_when(AstNode *condition, AstNode *then_block,
                      AstNode *fallback_block, SourceLocation location)
{
    if (condition == NULL || then_block == NULL) {
        ast_free(condition);
        ast_free(then_block);
        ast_free(fallback_block);
        return NULL;
    }

    AstNode *node = ast_node_create(AST_WHEN_STATEMENT, location);
    if (node == NULL) {
        ast_free(condition);
        ast_free(then_block);
        ast_free(fallback_block);
        return NULL;
    }

    node->data.when_statement.condition = condition;
    node->data.when_statement.then_block = then_block;
    node->data.when_statement.fallback_block = fallback_block;

    return node;
}

AstNode *ast_new_alwhen(AstNode *condition, AstNode *body,
                        SourceLocation location)
{
    if (condition == NULL || body == NULL) {
        ast_free(condition);
        ast_free(body);
        return NULL;
    }

    AstNode *node = ast_node_create(AST_ALWHEN_STATEMENT, location);
    if (node == NULL) {
        ast_free(condition);
        ast_free(body);
        return NULL;
    }

    node->data.alwhen_statement.condition = condition;
    node->data.alwhen_statement.body = body;

    return node;
}

AstNode *ast_new_break(SourceLocation location)
{
    /* El kind representa por completo a la instrucción break. */
    return ast_node_create(AST_BREAK_STATEMENT, location);
}

AstNode *ast_new_loop(char *control_name, AstNode *begin, AstNode *end,
                      AstNode *step, AstNode *body,
                      SourceLocation location)
{
    if (control_name == NULL || begin == NULL || end == NULL
        || step == NULL || body == NULL) {
        free(control_name);
        ast_free(begin);
        ast_free(end);
        ast_free(step);
        ast_free(body);
        return NULL;
    }

    AstNode *node = ast_node_create(AST_LOOP_STATEMENT, location);
    if (node == NULL) {
        free(control_name);
        ast_free(begin);
        ast_free(end);
        ast_free(step);
        ast_free(body);
        return NULL;
    }

    node->data.loop_statement.control_name = control_name;
    node->data.loop_statement.begin = begin;
    node->data.loop_statement.end = end;
    node->data.loop_statement.step = step;
    node->data.loop_statement.body = body;

    return node;
}

/* Consultas de solo lectura y nombres legibles para los enums públicos. */

AstKind ast_node_kind(const AstNode *node)
{
    assert(node != NULL);
    return node->kind;
}

SourceLocation ast_node_location(const AstNode *node)
{
    assert(node != NULL);
    return node->location;
}

const char *piper_type_name(PiperType type)
{
    switch (type) {
    case PIPER_TYPE_BOOL:
        return "bool";
    case PIPER_TYPE_B8:
        return "B8";
    case PIPER_TYPE_UB8:
        return "uB8";
    case PIPER_TYPE_B16:
        return "B16";
    case PIPER_TYPE_UB16:
        return "uB16";
    case PIPER_TYPE_B32:
        return "B32";
    case PIPER_TYPE_UB32:
        return "uB32";
    case PIPER_TYPE_NONE:
        return "none";
    default:
        return "<invalid-type>";
    }
}

const char *ast_operator_name(AstOperator operator)
{
    switch (operator) {
    case AST_OP_ADD:
        return "add";
    case AST_OP_SUBTRACT:
        return "subtract";
    case AST_OP_MULTIPLY:
        return "multiply";
    case AST_OP_DIVIDE:
        return "divide";
    case AST_OP_REMAINDER:
        return "remainder";
    case AST_OP_POWER:
        return "power";
    case AST_OP_INCREMENT:
        return "increment";
    case AST_OP_DECREMENT:
        return "decrement";
    case AST_OP_EQUAL:
        return "equal";
    case AST_OP_NOT_EQUAL:
        return "not_equal";
    case AST_OP_LESS:
        return "less";
    case AST_OP_LESS_EQUAL:
        return "less_equal";
    case AST_OP_GREATER:
        return "greater";
    case AST_OP_GREATER_EQUAL:
        return "greater_equal";
    case AST_OP_LOGICAL_AND:
        return "logical_and";
    case AST_OP_LOGICAL_OR:
        return "logical_or";
    case AST_OP_LOGICAL_XOR:
        return "logical_xor";
    case AST_OP_LOGICAL_NOT:
        return "logical_not";
    case AST_OP_NEGATE:
        return "negate";
    default:
        return "<invalid-operator>";
    }
}

/* Impresión indentada del árbol para diagnóstico y pruebas. */

static const char *ast_kind_name(AstKind kind)
{
    switch (kind) {
    case AST_PROGRAM:
        return "Program";
    case AST_IMPORT_DECLARATION:
        return "ImportDeclaration";
    case AST_VARIABLE_DECLARATION:
        return "VariableDeclaration";
    case AST_PARAMETER:
        return "Parameter";
    case AST_ROUTINE_DECLARATION:
        return "RoutineDeclaration";
    case AST_BLOCK:
        return "Block";
    case AST_ASSIGNMENT:
        return "Assignment";
    case AST_WHEN_STATEMENT:
        return "WhenStatement";
    case AST_ALWHEN_STATEMENT:
        return "AlwhenStatement";
    case AST_LOOP_STATEMENT:
        return "LoopStatement";
    case AST_BREAK_STATEMENT:
        return "BreakStatement";
    case AST_OUTPUT_STATEMENT:
        return "OutputStatement";
    case AST_CALL_EXPRESSION:
        return "CallExpression";
    case AST_IDENTIFIER_EXPRESSION:
        return "IdentifierExpression";
    case AST_INTEGER_LITERAL:
        return "IntegerLiteral";
    case AST_BOOLEAN_LITERAL:
        return "BooleanLiteral";
    case AST_NONE_LITERAL:
        return "NoneLiteral";
    case AST_ARRAY_LITERAL:
        return "ArrayLiteral";
    case AST_INDEX_EXPRESSION:
        return "IndexExpression";
    case AST_UNARY_EXPRESSION:
        return "UnaryExpression";
    case AST_BINARY_EXPRESSION:
        return "BinaryExpression";
    default:
        return "<invalid-node>";
    }
}

static void ast_print_indentation(FILE *output, size_t indentation)
{
    for (size_t level = 0; level < indentation; level++) {
        fputs("  ", output);
    }
}

static void ast_print_node(FILE *output, const AstNode *node,
                           size_t indentation);

static void ast_print_child(FILE *output, const char *label,
                            const AstNode *child, size_t indentation)
{
    assert(child != NULL);

    ast_print_indentation(output, indentation);
    fprintf(output, "%s:\n", label);
    ast_print_node(output, child, indentation + 1);
}

static void ast_print_list(FILE *output, const char *label,
                           const AstNodeList *list, size_t indentation)
{
    assert(list != NULL);

    ast_print_indentation(output, indentation);
    if (list->count == 0) {
        fprintf(output, "%s: []\n", label);
        return;
    }

    fprintf(output, "%s:\n", label);
    for (size_t index = 0; index < list->count; index++) {
        ast_print_node(output, list->items[index], indentation + 1);
    }
}

static void ast_print_node(FILE *output, const AstNode *node,
                           size_t indentation)
{
    ast_print_indentation(output, indentation);
    fprintf(output, "%s [%d:%d-%d:%d]\n",
            ast_kind_name(node->kind),
            node->location.first_line, node->location.first_column,
            node->location.last_line, node->location.last_column);

    switch (node->kind) {
    case AST_PROGRAM:
        ast_print_list(output, "elements", node->data.program.elements,
                       indentation + 1);
        break;

    case AST_IMPORT_DECLARATION:
        ast_print_indentation(output, indentation + 1);
        fprintf(output, "symbol: \"%s\"\n",
                node->data.import_declaration.symbol);
        ast_print_indentation(output, indentation + 1);
        fprintf(output, "file_path: \"%s\"\n",
                node->data.import_declaration.file_path);
        break;

    case AST_VARIABLE_DECLARATION:
        ast_print_indentation(output, indentation + 1);
        fprintf(output, "type: %s\n",
                piper_type_name(node->data.variable_declaration.type));
        ast_print_indentation(output, indentation + 1);
        fprintf(output, "name: \"%s\"\n",
                node->data.variable_declaration.name);
        ast_print_list(output, "dimensions",
                       node->data.variable_declaration.dimensions,
                       indentation + 1);
        break;

    case AST_PARAMETER:
        ast_print_indentation(output, indentation + 1);
        fprintf(output, "type: %s\n",
                piper_type_name(node->data.parameter.type));
        ast_print_indentation(output, indentation + 1);
        fprintf(output, "name: \"%s\"\n", node->data.parameter.name);
        ast_print_list(output, "dimensions",
                       node->data.parameter.dimensions, indentation + 1);
        break;

    case AST_ROUTINE_DECLARATION:
        ast_print_indentation(output, indentation + 1);
        fprintf(output, "return_type: %s\n",
                piper_type_name(node->data.routine_declaration.return_type));
        ast_print_indentation(output, indentation + 1);
        fprintf(output, "name: \"%s\"\n",
                node->data.routine_declaration.name);
        ast_print_list(output, "parameters",
                       node->data.routine_declaration.parameters,
                       indentation + 1);
        ast_print_child(output, "body",
                        node->data.routine_declaration.body,
                        indentation + 1);
        break;

    case AST_BLOCK:
        ast_print_list(output, "statements", node->data.block.statements,
                       indentation + 1);
        break;

    case AST_ASSIGNMENT:
        ast_print_child(output, "target", node->data.assignment.target,
                        indentation + 1);
        ast_print_child(output, "value", node->data.assignment.value,
                        indentation + 1);
        break;

    case AST_WHEN_STATEMENT:
        ast_print_child(output, "condition",
                        node->data.when_statement.condition,
                        indentation + 1);
        ast_print_child(output, "then_block",
                        node->data.when_statement.then_block,
                        indentation + 1);
        if (node->data.when_statement.fallback_block == NULL) {
            ast_print_indentation(output, indentation + 1);
            fputs("fallback_block: <absent>\n", output);
        } else {
            ast_print_child(output, "fallback_block",
                            node->data.when_statement.fallback_block,
                            indentation + 1);
        }
        break;

    case AST_ALWHEN_STATEMENT:
        ast_print_child(output, "condition",
                        node->data.alwhen_statement.condition,
                        indentation + 1);
        ast_print_child(output, "body", node->data.alwhen_statement.body,
                        indentation + 1);
        break;

    case AST_LOOP_STATEMENT:
        ast_print_indentation(output, indentation + 1);
        fprintf(output, "control_name: \"%s\"\n",
                node->data.loop_statement.control_name);
        ast_print_child(output, "begin", node->data.loop_statement.begin,
                        indentation + 1);
        ast_print_child(output, "end", node->data.loop_statement.end,
                        indentation + 1);
        ast_print_child(output, "step", node->data.loop_statement.step,
                        indentation + 1);
        ast_print_child(output, "body", node->data.loop_statement.body,
                        indentation + 1);
        break;

    case AST_BREAK_STATEMENT:
        break;

    case AST_OUTPUT_STATEMENT:
        ast_print_child(output, "value", node->data.output_statement.value,
                        indentation + 1);
        break;

    case AST_CALL_EXPRESSION:
        ast_print_indentation(output, indentation + 1);
        fprintf(output, "routine_name: \"%s\"\n",
                node->data.call_expression.routine_name);
        ast_print_list(output, "arguments",
                       node->data.call_expression.arguments,
                       indentation + 1);
        break;

    case AST_IDENTIFIER_EXPRESSION:
        ast_print_indentation(output, indentation + 1);
        fprintf(output, "name: \"%s\"\n", node->data.identifier.name);
        break;

    case AST_INTEGER_LITERAL:
        ast_print_indentation(output, indentation + 1);
        fprintf(output, "value: %llu\n", node->data.integer_literal.value);
        break;

    case AST_BOOLEAN_LITERAL:
        ast_print_indentation(output, indentation + 1);
        fprintf(output, "value: %s\n",
                node->data.boolean_literal.value ? "true" : "false");
        break;

    case AST_NONE_LITERAL:
        break;

    case AST_ARRAY_LITERAL:
        ast_print_list(output, "elements", node->data.array_literal.elements,
                       indentation + 1);
        break;

    case AST_INDEX_EXPRESSION:
        ast_print_child(output, "base", node->data.index_expression.base,
                        indentation + 1);
        ast_print_list(output, "indices", node->data.index_expression.indices,
                       indentation + 1);
        break;

    case AST_UNARY_EXPRESSION:
        ast_print_indentation(output, indentation + 1);
        fprintf(output, "operator: %s\n",
                ast_operator_name(node->data.unary_expression.operator));
        ast_print_child(output, "operand",
                        node->data.unary_expression.operand,
                        indentation + 1);
        break;

    case AST_BINARY_EXPRESSION:
        ast_print_indentation(output, indentation + 1);
        fprintf(output, "operator: %s\n",
                ast_operator_name(node->data.binary_expression.operator));
        ast_print_child(output, "left", node->data.binary_expression.left,
                        indentation + 1);
        ast_print_child(output, "right", node->data.binary_expression.right,
                        indentation + 1);
        break;

    default:
        break;
    }
}

void ast_print(FILE *output, const AstNode *node)
{
    assert(output != NULL);
    assert(node != NULL);

    ast_print_node(output, node, 0);
}

/* Liberación recursiva de los recursos internos y del nodo exterior. */
void ast_free(AstNode *node)
{
    if (node == NULL) {
        return;
    }

    switch (node->kind) {
    case AST_PROGRAM:
        ast_node_list_free(node->data.program.elements);
        break;

    case AST_IMPORT_DECLARATION:
        free(node->data.import_declaration.symbol);
        free(node->data.import_declaration.file_path);
        break;

    case AST_VARIABLE_DECLARATION:
        free(node->data.variable_declaration.name);
        ast_node_list_free(node->data.variable_declaration.dimensions);
        break;

    case AST_PARAMETER:
        free(node->data.parameter.name);
        ast_node_list_free(node->data.parameter.dimensions);
        break;

    case AST_ROUTINE_DECLARATION:
        free(node->data.routine_declaration.name);
        ast_node_list_free(node->data.routine_declaration.parameters);
        ast_free(node->data.routine_declaration.body);
        break;

    case AST_BLOCK:
        ast_node_list_free(node->data.block.statements);
        break;

    case AST_OUTPUT_STATEMENT:
        ast_free(node->data.output_statement.value);
        break;

    case AST_CALL_EXPRESSION:
        free(node->data.call_expression.routine_name);
        ast_node_list_free(node->data.call_expression.arguments);
        break;

    case AST_ARRAY_LITERAL:
        ast_node_list_free(node->data.array_literal.elements);
        break;

    case AST_INDEX_EXPRESSION:
        ast_free(node->data.index_expression.base);
        ast_node_list_free(node->data.index_expression.indices);
        break;

    case AST_IDENTIFIER_EXPRESSION:
        free(node->data.identifier.name);
        break;

    case AST_ASSIGNMENT:
        ast_free(node->data.assignment.target);
        ast_free(node->data.assignment.value);
        break;

    case AST_WHEN_STATEMENT:
        ast_free(node->data.when_statement.condition);
        ast_free(node->data.when_statement.then_block);
        ast_free(node->data.when_statement.fallback_block);
        break;

    case AST_ALWHEN_STATEMENT:
        ast_free(node->data.alwhen_statement.condition);
        ast_free(node->data.alwhen_statement.body);
        break;

    case AST_BREAK_STATEMENT:
        break;

    case AST_LOOP_STATEMENT:
        free(node->data.loop_statement.control_name);
        ast_free(node->data.loop_statement.begin);
        ast_free(node->data.loop_statement.end);
        ast_free(node->data.loop_statement.step);
        ast_free(node->data.loop_statement.body);
        break;

    case AST_UNARY_EXPRESSION:
        ast_free(node->data.unary_expression.operand);
        break;

    case AST_BINARY_EXPRESSION:
        ast_free(node->data.binary_expression.left);
        ast_free(node->data.binary_expression.right);
        break;

    case AST_INTEGER_LITERAL:
    case AST_BOOLEAN_LITERAL:
    case AST_NONE_LITERAL:
        break;

    default:
        break;
    }

    free(node);
}
