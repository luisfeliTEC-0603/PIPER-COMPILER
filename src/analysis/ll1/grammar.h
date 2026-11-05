#ifndef PIPER_LL1_GRAMMAR_H
#define PIPER_LL1_GRAMMAR_H

/**
 * @file grammar.h
 * @brief Representación declarativa y tabla predictiva fija de Piper.
 */

#include <stddef.h>
#include <stdio.h>

#define LL1_MAX_RHS 12

/** Terminales consumidos por el analizador predictivo. */
typedef enum {
    LL1_T_EOF,
    LL1_T_CREATE,
    LL1_T_ROUTINE,
    LL1_T_LOOP,
    LL1_T_ALWHEN,
    LL1_T_WHEN,
    LL1_T_FALLBACK,
    LL1_T_STORE,
    LL1_T_BRING,
    LL1_T_FROM,
    LL1_T_BREAK,
    LL1_T_OUTPUT,
    LL1_T_NONE,
    LL1_T_TRUE,
    LL1_T_FALSE,
    LL1_T_BOOL,
    LL1_T_B8,
    LL1_T_UB8,
    LL1_T_B16,
    LL1_T_UB16,
    LL1_T_B32,
    LL1_T_UB32,
    LL1_T_PLUS,
    LL1_T_MINUS,
    LL1_T_STAR,
    LL1_T_SLASH,
    LL1_T_PERCENT,
    LL1_T_CARET,
    LL1_T_INCREMENT,
    LL1_T_DECREMENT,
    LL1_T_EQUAL,
    LL1_T_NOT_EQUAL,
    LL1_T_LESS,
    LL1_T_LESS_EQUAL,
    LL1_T_GREATER,
    LL1_T_GREATER_EQUAL,
    LL1_T_AND,
    LL1_T_OR,
    LL1_T_XOR,
    LL1_T_NOT,
    LL1_T_LBRACE,
    LL1_T_RBRACE,
    LL1_T_LBRACKET,
    LL1_T_RBRACKET,
    LL1_T_DOT,
    LL1_T_COMMA,
    LL1_T_AMPERSAND,
    LL1_T_PIPE,
    LL1_T_ARROW,
    LL1_T_CONST_INT,
    LL1_T_IDENTIFIER,
    LL1_T_FILE_PATH,
    LL1_T_INVALID,
    LL1_T_COUNT
} Ll1Terminal;

/** No terminales de la gramática factorizada de Piper. */
typedef enum {
    LL1_N_PROGRAM,
    LL1_N_STATEMENT_LIST,
    LL1_N_STATEMENT,
    LL1_N_ROUTINE_DECLARATION,
    LL1_N_ROUTINE_BODY,
    LL1_N_PARAMETER_LIST,
    LL1_N_PARAMETER_LIST_TAIL,
    LL1_N_PARAMETER,
    LL1_N_DECLARATION,
    LL1_N_DIMENSIONS_OPT,
    LL1_N_DIMENSION_LIST,
    LL1_N_DIMENSION_LIST_TAIL,
    LL1_N_IMPORT_DECLARATION,
    LL1_N_TYPE,
    LL1_N_WHEN_STATEMENT,
    LL1_N_FALLBACK_OPT,
    LL1_N_ALWHEN_STATEMENT,
    LL1_N_LOOP_STATEMENT,
    LL1_N_OUTPUT_STATEMENT,
    LL1_N_BREAK_STATEMENT,
    LL1_N_BLOCK,
    LL1_N_IDENTIFIER_STATEMENT_TAIL,
    LL1_N_INDEXED_STATEMENT_TAIL,
    LL1_N_NON_IDENTIFIER_EXPRESSION,
    LL1_N_EXPRESSION,
    LL1_N_OR_EXPRESSION,
    LL1_N_OR_TAIL,
    LL1_N_XOR_EXPRESSION,
    LL1_N_XOR_TAIL,
    LL1_N_AND_EXPRESSION,
    LL1_N_AND_TAIL,
    LL1_N_COMPARISON_EXPRESSION,
    LL1_N_COMPARISON_TAIL,
    LL1_N_COMPARISON_OPERATOR,
    LL1_N_ADDITIVE_EXPRESSION,
    LL1_N_ADDITIVE_TAIL,
    LL1_N_MULTIPLICATIVE_EXPRESSION,
    LL1_N_MULTIPLICATIVE_TAIL,
    LL1_N_POWER_EXPRESSION,
    LL1_N_POWER_TAIL,
    LL1_N_UNARY_EXPRESSION,
    LL1_N_PRIMARY,
    LL1_N_IDENTIFIER_PRIMARY_TAIL,
    LL1_N_ARRAY_LITERAL,
    LL1_N_EXPRESSION_LIST,
    LL1_N_EXPRESSION_LIST_TAIL,
    LL1_N_INDEX_LIST,
    LL1_N_INDEX_LIST_TAIL,
    LL1_N_INDEX_ITEM,
    LL1_N_EXPRESSION_AFTER_PRIMARY,
    LL1_N_COUNT
} Ll1Nonterminal;

/**
 * @brief Identificadores estables de las producciones adaptadas de Piper.
 *
 * LL1_P_ERROR representa una celda vacía de la tabla. Las producciones
 * epsilon tienen identificador propio y se distinguen porque rhs_count es 0.
 */
typedef enum {
    LL1_P_ERROR,
    LL1_P_PROGRAM_STATEMENT_LIST,
    LL1_P_STATEMENT_LIST_STATEMENT,
    LL1_P_STATEMENT_LIST_EPSILON,
    LL1_P_STATEMENT_ROUTINE,
    LL1_P_STATEMENT_DECLARATION,
    LL1_P_STATEMENT_IMPORT,
    LL1_P_STATEMENT_WHEN,
    LL1_P_STATEMENT_ALWHEN,
    LL1_P_STATEMENT_LOOP,
    LL1_P_STATEMENT_OUTPUT,
    LL1_P_STATEMENT_BREAK,
    LL1_P_STATEMENT_BLOCK,
    LL1_P_STATEMENT_IDENTIFIER,
    LL1_P_STATEMENT_NON_IDENTIFIER_EXPRESSION,
    LL1_P_ROUTINE_DECLARATION,
    LL1_P_ROUTINE_BODY,
    LL1_P_PARAMETER_LIST_PARAMETER,
    LL1_P_PARAMETER_LIST_EPSILON,
    LL1_P_PARAMETER_LIST_TAIL_COMMA,
    LL1_P_PARAMETER_LIST_TAIL_EPSILON,
    LL1_P_PARAMETER,
    LL1_P_DECLARATION,
    LL1_P_DIMENSIONS,
    LL1_P_DIMENSIONS_EPSILON,
    LL1_P_DIMENSION_LIST,
    LL1_P_DIMENSION_LIST_TAIL_AMPERSAND,
    LL1_P_DIMENSION_LIST_TAIL_EPSILON,
    LL1_P_IMPORT_DECLARATION,
    LL1_P_TYPE_BOOL,
    LL1_P_TYPE_B8,
    LL1_P_TYPE_UB8,
    LL1_P_TYPE_B16,
    LL1_P_TYPE_UB16,
    LL1_P_TYPE_B32,
    LL1_P_TYPE_UB32,
    LL1_P_WHEN_STATEMENT,
    LL1_P_FALLBACK,
    LL1_P_FALLBACK_EPSILON,
    LL1_P_ALWHEN_STATEMENT,
    LL1_P_LOOP_STATEMENT,
    LL1_P_OUTPUT_STATEMENT,
    LL1_P_BREAK_STATEMENT,
    LL1_P_BLOCK,
    LL1_P_IDENTIFIER_STATEMENT_STORE,
    LL1_P_IDENTIFIER_STATEMENT_INDEX,
    LL1_P_IDENTIFIER_STATEMENT_CALL,
    LL1_P_IDENTIFIER_STATEMENT_EXPRESSION,
    LL1_P_INDEXED_STATEMENT_STORE,
    LL1_P_INDEXED_STATEMENT_EXPRESSION,
    LL1_P_NON_IDENTIFIER_INTEGER,
    LL1_P_NON_IDENTIFIER_TRUE,
    LL1_P_NON_IDENTIFIER_FALSE,
    LL1_P_NON_IDENTIFIER_ARRAY,
    LL1_P_NON_IDENTIFIER_MINUS,
    LL1_P_NON_IDENTIFIER_NOT,
    LL1_P_NON_IDENTIFIER_INCREMENT,
    LL1_P_NON_IDENTIFIER_DECREMENT,
    LL1_P_EXPRESSION_OR,
    LL1_P_OR_EXPRESSION,
    LL1_P_OR_TAIL_OR,
    LL1_P_OR_TAIL_EPSILON,
    LL1_P_XOR_EXPRESSION,
    LL1_P_XOR_TAIL_XOR,
    LL1_P_XOR_TAIL_EPSILON,
    LL1_P_AND_EXPRESSION,
    LL1_P_AND_TAIL_AND,
    LL1_P_AND_TAIL_EPSILON,
    LL1_P_COMPARISON_EXPRESSION,
    LL1_P_COMPARISON_TAIL_OPERATOR,
    LL1_P_COMPARISON_TAIL_EPSILON,
    LL1_P_COMPARISON_OPERATOR_EQUAL,
    LL1_P_COMPARISON_OPERATOR_NOT_EQUAL,
    LL1_P_COMPARISON_OPERATOR_LESS,
    LL1_P_COMPARISON_OPERATOR_LESS_EQUAL,
    LL1_P_COMPARISON_OPERATOR_GREATER,
    LL1_P_COMPARISON_OPERATOR_GREATER_EQUAL,
    LL1_P_ADDITIVE_EXPRESSION,
    LL1_P_ADDITIVE_TAIL_PLUS,
    LL1_P_ADDITIVE_TAIL_MINUS,
    LL1_P_ADDITIVE_TAIL_EPSILON,
    LL1_P_MULTIPLICATIVE_EXPRESSION,
    LL1_P_MULTIPLICATIVE_TAIL_STAR,
    LL1_P_MULTIPLICATIVE_TAIL_SLASH,
    LL1_P_MULTIPLICATIVE_TAIL_PERCENT,
    LL1_P_MULTIPLICATIVE_TAIL_EPSILON,
    LL1_P_POWER_EXPRESSION,
    LL1_P_POWER_TAIL_CARET,
    LL1_P_POWER_TAIL_EPSILON,
    LL1_P_UNARY_MINUS,
    LL1_P_UNARY_NOT,
    LL1_P_UNARY_INCREMENT,
    LL1_P_UNARY_DECREMENT,
    LL1_P_UNARY_PRIMARY,
    LL1_P_PRIMARY_INTEGER,
    LL1_P_PRIMARY_TRUE,
    LL1_P_PRIMARY_FALSE,
    LL1_P_PRIMARY_IDENTIFIER,
    LL1_P_PRIMARY_ARRAY,
    LL1_P_IDENTIFIER_PRIMARY_INDEX,
    LL1_P_IDENTIFIER_PRIMARY_CALL,
    LL1_P_IDENTIFIER_PRIMARY_EPSILON,
    LL1_P_ARRAY_LITERAL,
    LL1_P_EXPRESSION_LIST_EXPRESSION,
    LL1_P_EXPRESSION_LIST_EPSILON,
    LL1_P_EXPRESSION_LIST_TAIL_COMMA,
    LL1_P_EXPRESSION_LIST_TAIL_EPSILON,
    LL1_P_INDEX_LIST,
    LL1_P_INDEX_LIST_TAIL_AMPERSAND,
    LL1_P_INDEX_LIST_TAIL_EPSILON,
    LL1_P_INDEX_ITEM_INTEGER,
    LL1_P_INDEX_ITEM_IDENTIFIER,
    LL1_P_EXPRESSION_AFTER_PRIMARY,
    LL1_P_COUNT
} Ll1ProductionId;

/** Producción sin acciones semánticas; rhs_count igual a cero representa epsilon. */
typedef struct {
    Ll1ProductionId id;
    Ll1Nonterminal lhs;
    size_t rhs_count;
    int rhs[LL1_MAX_RHS];
} Ll1Production;

#define LL1_TERMINAL_SYMBOL(terminal) ((int)(terminal))
#define LL1_NONTERMINAL_SYMBOL(nonterminal) \
    ((int)LL1_T_COUNT + (int)(nonterminal))
#define LL1_SYMBOL_IS_TERMINAL(symbol) \
    ((symbol) >= 0 && (symbol) < (int)LL1_T_COUNT)
#define LL1_SYMBOL_IS_NONTERMINAL(symbol) \
    ((symbol) >= (int)LL1_T_COUNT && \
     (symbol) < (int)LL1_T_COUNT + (int)LL1_N_COUNT)
#define LL1_SYMBOL_TO_NONTERMINAL(symbol) \
    ((Ll1Nonterminal)((symbol) - (int)LL1_T_COUNT))

/** @return Cantidad de producciones declaradas. */
size_t ll1_production_count(void);

/**
 * @brief Obtiene una producción por su índice.
 * @param id Identificador simbólico dentro de la gramática.
 * @return Producción o NULL si el identificador no existe.
 */
const Ll1Production *ll1_production_at(Ll1ProductionId id);

/**
 * @brief Consulta una celda de la tabla predictiva fija.
 * @param nonterminal No terminal que está en la cima de la pila.
 * @param terminal Token de anticipación.
 * @return Producción aplicable o LL1_P_ERROR si la celda está vacía.
 */
Ll1ProductionId ll1_table_lookup(Ll1Nonterminal nonterminal,
                                 Ll1Terminal terminal);

/** @return Nombre legible de un terminal. */
const char *ll1_terminal_name(Ll1Terminal terminal);

/** @return Nombre legible de un no terminal. */
const char *ll1_nonterminal_name(Ll1Nonterminal nonterminal);

/** @return Nombre legible de un símbolo terminal o no terminal. */
const char *ll1_symbol_name(int symbol);

/**
 * @brief Imprime una producción en notación legible.
 * @param output Flujo de salida.
 * @param production Producción que se imprimirá.
 */
void ll1_print_production(FILE *output, const Ll1Production *production);

/**
 * @brief Imprime solamente las celdas definidas de la tabla predictiva.
 * @param output Flujo de salida.
 */
void ll1_print_table(FILE *output);

#endif
