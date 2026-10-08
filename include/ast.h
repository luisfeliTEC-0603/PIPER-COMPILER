#ifndef PIPER_AST_H
#define PIPER_AST_H

/*
 * Contrato público del AST de Piper.
 *
 * Este encabezado permite que parser.y avance en paralelo con ast.c: el
 * parser puede llamar estas funciones aunque su implementación todavía esté
 * pendiente. Las estructuras internas son opacas para que solamente ast.c
 * decida cómo almacenar cada variante.
 */

#include <stdbool.h>
#include <stdio.h>

typedef struct AstNode AstNode;
typedef struct AstNodeList AstNodeList;

typedef struct {
    int first_line;
    int first_column;
    int last_line;
    int last_column;
} SourceLocation;

typedef enum {
    PIPER_TYPE_BOOL,
    PIPER_TYPE_B8,
    PIPER_TYPE_UB8,
    PIPER_TYPE_B16,
    PIPER_TYPE_UB16,
    PIPER_TYPE_B32,
    PIPER_TYPE_UB32,

    /* TODO(rutinas): decidir en qué construcciones representa ausencia. */
    PIPER_TYPE_NONE
} PiperType;

typedef enum {
    AST_PROGRAM,
    AST_IMPORT_DECLARATION,
    AST_VARIABLE_DECLARATION,
    AST_PARAMETER,
    AST_ROUTINE_DECLARATION,
    AST_BLOCK,
    AST_ASSIGNMENT,
    AST_WHEN_STATEMENT,
    AST_ALWHEN_STATEMENT,
    AST_LOOP_STATEMENT,
    AST_BREAK_STATEMENT,
    AST_OUTPUT_STATEMENT,
    AST_CALL_EXPRESSION,
    AST_IDENTIFIER_EXPRESSION,
    AST_INTEGER_LITERAL,
    AST_BOOLEAN_LITERAL,
    AST_NONE_LITERAL,
    AST_ARRAY_LITERAL,
    AST_INDEX_EXPRESSION,
    AST_UNARY_EXPRESSION,
    AST_BINARY_EXPRESSION
} AstKind;

typedef enum {
    AST_OP_ADD,
    AST_OP_SUBTRACT,
    AST_OP_MULTIPLY,
    AST_OP_DIVIDE,
    AST_OP_REMAINDER,
    AST_OP_POWER,
    AST_OP_INCREMENT,
    AST_OP_DECREMENT,
    AST_OP_EQUAL,
    AST_OP_NOT_EQUAL,
    AST_OP_LESS,
    AST_OP_LESS_EQUAL,
    AST_OP_GREATER,
    AST_OP_GREATER_EQUAL,
    AST_OP_LOGICAL_AND,
    AST_OP_LOGICAL_OR,
    AST_OP_LOGICAL_XOR,
    AST_OP_LOGICAL_NOT,
    AST_OP_NEGATE
} AstOperator;

/* -------------------------------------------------------------------------
 * Ubicaciones
 * ------------------------------------------------------------------------- */

SourceLocation source_location_make(int first_line, int first_column,
                                    int last_line, int last_column);

/* -------------------------------------------------------------------------
 * Listas
 * -------------------------------------------------------------------------
 *
 * AstNodeList se usará para instrucciones, parámetros, argumentos,
 * dimensiones e inicializadores. ast_node_list_append() adquiere node solo
 * cuando retorna true; si retorna false, node continúa siendo del llamador.
 */

AstNodeList *ast_node_list_create(void);
bool ast_node_list_append(AstNodeList *list, AstNode *node);
void ast_node_list_free(AstNodeList *list);

/* -------------------------------------------------------------------------
 * Constructores
 * -------------------------------------------------------------------------
 *
 * Contrato de propiedad propuesto para todos los constructores:
 *
 * - Los char * deben apuntar a memoria dinámica, nunca directamente a yytext.
 * - Al llamar un constructor, este adquiere incondicionalmente sus cadenas,
 *   nodos hijos y listas, incluso si no logra crear el nuevo nodo.
 * - El parser no debe liberar un argumento después de transferirlo.
 * - Una lista obligatoria nunca es NULL; se usa una lista vacía cuando no hay
 *   elementos (por ejemplo, cero parámetros o cero dimensiones).
 * - Solo los hijos documentados como opcionales pueden ser NULL. En este
 *   contrato fallback_block es opcional. Cualquier otro caso se decide en el
 *   issue de la construcción antes de implementarlo.
 *
 * TODO(ast): ast.c debe respetar este contrato también en errores de memoria.
 */

AstNode *ast_new_program(AstNodeList *elements, SourceLocation location);

AstNode *ast_new_import_declaration(char *symbol, char *file_path,
                                    SourceLocation location);

AstNode *ast_new_variable_declaration(PiperType type, char *name,
                                      AstNodeList *dimensions,
                                      SourceLocation location);

AstNode *ast_new_parameter(PiperType type, char *name,
                           AstNodeList *dimensions,
                           SourceLocation location);

AstNode *ast_new_routine_declaration(PiperType return_type, char *name,
                                     AstNodeList *parameters, AstNode *body,
                                     SourceLocation location);

AstNode *ast_new_block(AstNodeList *statements, SourceLocation location);

AstNode *ast_new_assignment(AstNode *target, AstNode *value,
                            SourceLocation location);

AstNode *ast_new_when(AstNode *condition, AstNode *then_block,
                      AstNode *fallback_block, SourceLocation location);

AstNode *ast_new_alwhen(AstNode *condition, AstNode *body,
                        SourceLocation location);

AstNode *ast_new_loop(char *control_name, AstNode *begin, AstNode *end,
                      AstNode *step, AstNode *body,
                      SourceLocation location);

AstNode *ast_new_break(SourceLocation location);
AstNode *ast_new_output(AstNode *value, SourceLocation location);

AstNode *ast_new_call(char *routine_name, AstNodeList *arguments,
                      SourceLocation location);

AstNode *ast_new_identifier(char *name, SourceLocation location);

AstNode *ast_new_integer_literal(unsigned long long value,
                                 SourceLocation location);

AstNode *ast_new_boolean_literal(bool value, SourceLocation location);
AstNode *ast_new_none_literal(SourceLocation location);

AstNode *ast_new_array_literal(AstNodeList *elements,
                               SourceLocation location);

AstNode *ast_new_index_expression(AstNode *base, AstNodeList *indices,
                                  SourceLocation location);

AstNode *ast_new_unary_expression(AstOperator operator, AstNode *operand,
                                  SourceLocation location);

AstNode *ast_new_binary_expression(AstOperator operator, AstNode *left,
                                   AstNode *right,
                                   SourceLocation location);

/* -------------------------------------------------------------------------
 * Inspección y ciclo de vida
 * ------------------------------------------------------------------------- */

AstKind ast_node_kind(const AstNode *node);
SourceLocation ast_node_location(const AstNode *node);
const char *piper_type_name(PiperType type);
const char *ast_operator_name(AstOperator operator);

/* Imprime el árbol completo con indentación legible. */
void ast_print(FILE *output, const AstNode *node);

/* Libera recursivamente node y todo lo que sea propiedad del nodo. */
void ast_free(AstNode *node);

#endif
