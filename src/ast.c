#include "ast.h"

#include <stdlib.h>

/*
 * Implementación del AST de Piper.
 *
 * Este archivo queda deliberadamente como skeleton. El responsable del AST
 * implementará aquí las estructuras opacas declaradas en ast.h y garantizará
 * que cada constructor respete el contrato de propiedad documentado.
 */

/* TODO(ast-core): definir struct AstNodeList. */
struct AstNode {
    AstKind kind;
    SourceLocation location;

    union {
        struct {
            unsigned long long value;
        } integer_literal;

        struct {
            char *name;
        } identifier;

        struct {
            AstNode *target;
            AstNode *value;
        } assignment;
    } data;
};

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

/* TODO(ast-core): implementar creación, append y liberación de listas. */

/* TODO(ast-expressions): implementar literales, nombres y operaciones. */
//Constructor de literales
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

//Constructor de identificadores
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

/* TODO(ast-statements): implementar declaraciones, asignaciones y bloques. */
//Constructor de asiganciones
AstNode *ast_new_assignment(AstNode *target, AstNode *value,
                            SourceLocation location)
{
       
    if (target == NULL || value == NULL) { 
        //usamos ast_free para no generar memory leaks con lo que hay dentro de los nodos
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

    case AST_INTEGER_LITERAL:
        break;

    default:
        break;
    }

    free(node);
}