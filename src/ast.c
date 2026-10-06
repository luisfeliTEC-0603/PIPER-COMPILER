#include "ast.h"

#include <stdint.h>
#include <stdlib.h>

/*
 * Implementación del AST de Piper.
 *
 * Este archivo queda deliberadamente como skeleton. El responsable del AST
 * implementará aquí las estructuras opacas declaradas en ast.h y garantizará
 * que cada constructor respete el contrato de propiedad documentado.
 */

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
            unsigned long long value;
        } integer_literal;

        struct {
            bool value;
        } boolean_literal;

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
    } data;
};

/* Reserva un nodo e inicializa únicamente sus campos comunes. */
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

/*
 * Constructores de expresiones. Cada constructor adquiere incondicionalmente
 * sus argumentos que sean punteros.
 */
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

/* Constructores de instrucciones. */
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

/* TODO(ast-control): implementar when, alwhen, loop y break. */

/* TODO(ast-routines): implementar parámetros, rutinas, llamadas y output. */

/* TODO(ast-composites): implementar arreglos, índices e importaciones. */

/* TODO(ast-print): implementar impresión indentada para todas las variantes. */

/* Libera primero los recursos propios de la variante y luego el nodo. */
void ast_free(AstNode *node)
{
    if (node == NULL) {
        return;
    }

    switch (node->kind) {
    case AST_IDENTIFIER_EXPRESSION:
        free(node->data.identifier.name);
        break;

    case AST_ASSIGNMENT:
        ast_free(node->data.assignment.target);
        ast_free(node->data.assignment.value);
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
