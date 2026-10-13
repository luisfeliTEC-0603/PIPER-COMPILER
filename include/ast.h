#ifndef PIPER_AST_H
#define PIPER_AST_H

/*
 * Contrato público del AST de Piper.
 *
 * Las estructuras son públicas para que el análisis semántico y la generación
 * de código puedan recorrer los hijos de cada nodo a partir de la raíz.
 * Los constructores y ast_free() administran la memoria del árbol.
 */

#include <stdbool.h>
#include <stdio.h>
#include <symbol_table.h>

typedef struct AstNode AstNode;
typedef struct AstNodeList AstNodeList;

typedef struct {
    int first_line;
    int first_column;
    int last_line;
    int last_column;
} SourceLocation;

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

/* Lista que posee los nodos agregados exitosamente. */
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
    /* Ámbito creado para este nodo por el análisis semántico; no es dueño. */
    Scope *semantic_scope;
    DataType inferred_type;
    DataType element_type;
    PiperType piper_type;
    PiperType element_piper_type;
    /* Un literal entero obtiene su tipo exacto del uso; no es un tipo declarable. */
    bool has_piper_type;
    bool has_element_piper_type;
    int array_rank;
    int array_rows, array_cols;

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

/* node debe ser distinto de NULL. */
AstKind ast_node_kind(const AstNode *node);
SourceLocation ast_node_location(const AstNode *node);
DataType ast_node_inferred_type(const AstNode *node);

/* Devuelven literales estáticos; los valores inválidos tienen nombre propio. */
const char *piper_type_name(PiperType type);
const char *ast_operator_name(AstOperator operator);

/* output y node deben ser distintos de NULL. */
void ast_print(FILE *output, const AstNode *node);

/* Escribe node como JSON válido en output. */
void ast_print_json(FILE *output, const AstNode *node);

/* Libera recursivamente node y todo lo que sea propiedad del nodo. */
void ast_free(AstNode *node);

/* Extrae las dimensiones (filas y columnas) de una lista de nodos de dimensión y las registra en la tabla de símbolos. */
void ast_register_symbol_with_dimensions(SymbolTable *symbols, const char *name,
                                         SymbolClass symbol_class, PiperType type,
                                         const AstNodeList *dimensions, int line);

#endif  
