#ifndef PIPER_LL1_GRAMMAR_H
#define PIPER_LL1_GRAMMAR_H

/**
 * @file grammar.h
 * @brief API de la gramática predictiva generada de Piper.
 *
 * Los símbolos, producciones y la tabla provienen de piper.ll1. `make`
 * genera grammar_symbols.h y grammar_data.inc dentro de build/.
 */

#include <stddef.h>
#include <stdio.h>

#include "analysis/ll1/grammar_symbols.h"

/** Producción sin acciones semánticas; rhs_count cero representa epsilon. */
typedef struct {
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

/**
 * @brief Consulta una celda de la tabla predictiva generada.
 * @return Producción aplicable o NULL si la celda está vacía.
 */
const Ll1Production *ll1_table_lookup(Ll1Nonterminal nonterminal,
                                      Ll1Terminal terminal);

/** @return Nombre legible de un terminal. */
const char *ll1_terminal_name(Ll1Terminal terminal);

/** @return Nombre legible de un no terminal. */
const char *ll1_nonterminal_name(Ll1Nonterminal nonterminal);

/** @return Nombre legible de un símbolo terminal o no terminal. */
const char *ll1_symbol_name(int symbol);

/** Imprime una producción en notación legible. */
void ll1_print_production(FILE *output, const Ll1Production *production);

/** Imprime solamente las celdas definidas de la tabla predictiva. */
void ll1_print_table(FILE *output);

#endif
