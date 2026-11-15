/**
 * @file grammar.c
 * @brief Consultas sobre la gramática y tabla LL(1) generadas de Piper.
 */

#include "analysis/ll1/grammar.h"

#include <stdint.h>

/* Generado durante `make` a partir de piper.ll1. */
#include "analysis/ll1/grammar_data.inc"

const Ll1Production *ll1_table_lookup(Ll1Nonterminal nonterminal,
                                      Ll1Terminal terminal)
{
    uint16_t encoded_production;
    if (nonterminal < 0 || nonterminal >= LL1_N_COUNT ||
        terminal < 0 || terminal >= LL1_T_COUNT)
        return NULL;

    encoded_production = predictive_table[nonterminal][terminal];
    if (encoded_production == 0) return NULL;
    return &productions[encoded_production - 1];
}

const char *ll1_terminal_name(Ll1Terminal terminal)
{
    return terminal >= 0 && terminal < LL1_T_COUNT
        ? terminal_names[terminal] : "<terminal inválido>";
}

const char *ll1_nonterminal_name(Ll1Nonterminal nonterminal)
{
    return nonterminal >= 0 && nonterminal < LL1_N_COUNT
        ? nonterminal_names[nonterminal] : "<no terminal inválido>";
}

const char *ll1_symbol_name(int symbol)
{
    if (LL1_SYMBOL_IS_TERMINAL(symbol))
        return ll1_terminal_name((Ll1Terminal)symbol);
    if (LL1_SYMBOL_IS_NONTERMINAL(symbol))
        return ll1_nonterminal_name(LL1_SYMBOL_TO_NONTERMINAL(symbol));
    return "<símbolo inválido>";
}

void ll1_print_production(FILE *output, const Ll1Production *production)
{
    size_t index;
    fprintf(output, "%s ->", ll1_nonterminal_name(production->lhs));
    if (production->rhs_count == 0) {
        fputs(" ε", output);
        return;
    }
    for (index = 0; index < production->rhs_count; ++index)
        fprintf(output, " %s", ll1_symbol_name(production->rhs[index]));
}

void ll1_print_table(FILE *output)
{
    size_t nonterminal, terminal;
    fputs("Tabla predictiva LL(1) (celdas definidas):\n", output);
    for (nonterminal = 0; nonterminal < LL1_N_COUNT; ++nonterminal) {
        for (terminal = 0; terminal < LL1_T_COUNT; ++terminal) {
            const Ll1Production *production = ll1_table_lookup(
                (Ll1Nonterminal)nonterminal, (Ll1Terminal)terminal);
            if (production == NULL) continue;
            fprintf(output, "  M[%s, %s] = ",
                    ll1_nonterminal_name((Ll1Nonterminal)nonterminal),
                    ll1_terminal_name((Ll1Terminal)terminal));
            ll1_print_production(output, production);
            fputc('\n', output);
        }
    }
    fputs("Conflictos: 0\n", output);
}
