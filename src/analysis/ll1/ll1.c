/**
 * @file ll1.c
 * @brief Adaptador del lexer de Piper y analizador predictivo con pila.
 */

#include "analysis/ll1/ll1.h"
#include "analysis/ll1/grammar.h"
#include "parser.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void yyrestart(FILE *input_file);
int yylex(void);
int yylex_destroy(void);
void lexer_set_trace(int enabled);
extern char *yytext;

/** Token independiente de los valores semánticos que administra Bison. */
typedef struct {
    Ll1Terminal terminal;
    char *lexeme;
    int line;
    int column;
} Ll1Token;

/** Secuencia completa necesaria para mostrar la entrada restante en la traza. */
typedef struct {
    Ll1Token *items;
    size_t count;
    size_t capacity;
} Ll1TokenList;

/** Pila dinámica de símbolos gramaticales. */
typedef struct {
    int *items;
    size_t count;
    size_t capacity;
} Ll1Stack;

/** Traduce las categorías generadas por Bison a los terminales de la tabla. */
static Ll1Terminal terminal_from_lexer(int token)
{
    switch (token) {
    case 0: return LL1_T_EOF;
    case TOK_CREATE: return LL1_T_CREATE;
    case TOK_ROUTINE: return LL1_T_ROUTINE;
    case TOK_LOOP: return LL1_T_LOOP;
    case TOK_ALWHEN: return LL1_T_ALWHEN;
    case TOK_WHEN: return LL1_T_WHEN;
    case TOK_FALLBACK: return LL1_T_FALLBACK;
    case TOK_STORE: return LL1_T_STORE;
    case TOK_BRING: return LL1_T_BRING;
    case TOK_FROM: return LL1_T_FROM;
    case TOK_BREAK: return LL1_T_BREAK;
    case TOK_OUTPUT: return LL1_T_OUTPUT;
    case TOK_NONE: return LL1_T_NONE;
    case TOK_TRUE: return LL1_T_TRUE;
    case TOK_FALSE: return LL1_T_FALSE;
    case TOK_BOOL: return LL1_T_BOOL;
    case TOK_B8: return LL1_T_B8;
    case TOK_UB8: return LL1_T_UB8;
    case TOK_B16: return LL1_T_B16;
    case TOK_UB16: return LL1_T_UB16;
    case TOK_B32: return LL1_T_B32;
    case TOK_UB32: return LL1_T_UB32;
    case TOK_PLUS: return LL1_T_PLUS;
    case TOK_MINUS: return LL1_T_MINUS;
    case TOK_STAR: return LL1_T_STAR;
    case TOK_SLASH: return LL1_T_SLASH;
    case TOK_PERCENT: return LL1_T_PERCENT;
    case TOK_CARET: return LL1_T_CARET;
    case TOK_INCREMENT: return LL1_T_INCREMENT;
    case TOK_DECREMENT: return LL1_T_DECREMENT;
    case TOK_EQUAL: return LL1_T_EQUAL;
    case TOK_NOT_EQUAL: return LL1_T_NOT_EQUAL;
    case TOK_LESS: return LL1_T_LESS;
    case TOK_LESS_EQUAL: return LL1_T_LESS_EQUAL;
    case TOK_GREATER: return LL1_T_GREATER;
    case TOK_GREATER_EQUAL: return LL1_T_GREATER_EQUAL;
    case TOK_AND: return LL1_T_AND;
    case TOK_OR: return LL1_T_OR;
    case TOK_XOR: return LL1_T_XOR;
    case TOK_NOT: return LL1_T_NOT;
    case TOK_LBRACE: return LL1_T_LBRACE;
    case TOK_RBRACE: return LL1_T_RBRACE;
    case TOK_LBRACKET: return LL1_T_LBRACKET;
    case TOK_RBRACKET: return LL1_T_RBRACKET;
    case TOK_DOT: return LL1_T_DOT;
    case TOK_COMMA: return LL1_T_COMMA;
    case TOK_AMPERSAND: return LL1_T_AMPERSAND;
    case TOK_PIPE: return LL1_T_PIPE;
    case TOK_ARROW: return LL1_T_ARROW;
    case TOK_CONST_INT: return LL1_T_CONST_INT;
    case TOK_IDENTIFIER: return LL1_T_IDENTIFIER;
    case TOK_FILE_PATH: return LL1_T_FILE_PATH;
    case TOK_INVALID: return LL1_T_INVALID;
    default: return LL1_T_INVALID;
    }
}

/** Duplica el lexema antes de que Flex reutilice yytext. */
static char *duplicate_text(const char *text)
{
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (copy != NULL) memcpy(copy, text, length + 1);
    return copy;
}

/** Libera los lexemas que posee una lista de tokens. */
static void token_list_destroy(Ll1TokenList *tokens)
{
    size_t index;
    for (index = 0; index < tokens->count; ++index)
        free(tokens->items[index].lexeme);
    free(tokens->items);
    memset(tokens, 0, sizeof(*tokens));
}

/** Agrega un token y adquiere la propiedad de su lexema. */
static int token_list_append(Ll1TokenList *tokens, Ll1Token token)
{
    Ll1Token *resized;
    size_t new_capacity;
    if (tokens->count == tokens->capacity) {
        if (tokens->capacity > SIZE_MAX / 2) return 0;
        new_capacity = tokens->capacity == 0 ? 32 : tokens->capacity * 2;
        if (new_capacity > SIZE_MAX / sizeof(*tokens->items)) return 0;
        resized = realloc(tokens->items, new_capacity * sizeof(*tokens->items));
        if (resized == NULL) return 0;
        tokens->items = resized;
        tokens->capacity = new_capacity;
    }
    tokens->items[tokens->count++] = token;
    return 1;
}

/** Agrega el marcador de fin usado tanto por la entrada como por la pila. */
static int append_end_token(Ll1TokenList *tokens, int line, int column)
{
    Ll1Token end = {LL1_T_EOF, duplicate_text("$"), line, column};
    if (end.lexeme == NULL || !token_list_append(tokens, end)) {
        free(end.lexeme);
        return 0;
    }
    return 1;
}

/**
 * @brief Consume Flex una sola vez y conserva categoría, lexema y ubicación.
 * @param input Fuente ya abierto.
 * @param tokens Lista que recibirá los tokens, incluido fin de entrada.
 * @return 1 si se pudo construir la secuencia; 0 ante falta de memoria.
 */
static int read_tokens(FILE *input, Ll1TokenList *tokens)
{
    yyrestart(input);
    lexer_set_trace(0);
    yylloc.first_line = yylloc.last_line = 1;
    yylloc.first_column = yylloc.last_column = 1;

    for (;;) {
        int lexer_token = yylex();
        Ll1Terminal terminal = terminal_from_lexer(lexer_token);
        Ll1Token token;
        if (lexer_token == 0)
            return append_end_token(tokens, yylloc.last_line, yylloc.last_column);

        token.terminal = terminal;
        token.lexeme = duplicate_text(yytext == NULL ? "" : yytext);
        token.line = yylloc.first_line;
        token.column = yylloc.first_column;

        /* Flex asigna estos dos valores semánticos en memoria dinámica. El
         * LL(1) solo necesita el lexema copiado y debe liberar el original. */
        if (lexer_token == TOK_IDENTIFIER || lexer_token == TOK_FILE_PATH) {
            free(yylval.text);
            yylval.text = NULL;
        }

        if (token.lexeme == NULL || !token_list_append(tokens, token)) {
            free(token.lexeme);
            return 0;
        }

        /* No se omite una unidad inválida ni se siguen produciendo errores
         * léxicos: queda en la entrada y el parser la rechaza como primer error. */
        if (terminal == LL1_T_INVALID)
            return append_end_token(tokens, token.line, token.column);
    }
}

/** Inserta un símbolo en la cima de la pila. */
static int stack_push(Ll1Stack *stack, int symbol)
{
    int *resized;
    size_t new_capacity;
    if (stack->count == stack->capacity) {
        if (stack->capacity > SIZE_MAX / 2) return 0;
        new_capacity = stack->capacity == 0 ? 32 : stack->capacity * 2;
        if (new_capacity > SIZE_MAX / sizeof(*stack->items)) return 0;
        resized = realloc(stack->items, new_capacity * sizeof(*stack->items));
        if (resized == NULL) return 0;
        stack->items = resized;
        stack->capacity = new_capacity;
    }
    stack->items[stack->count++] = symbol;
    return 1;
}

/** Informa las alternativas válidas de una fila de la tabla. */
static void print_expected_row(FILE *output, Ll1Nonterminal nonterminal)
{
    size_t terminal;
    int printed = 0;
    for (terminal = 0; terminal < LL1_T_COUNT; ++terminal) {
        if (ll1_table_lookup(nonterminal, (Ll1Terminal)terminal) == NULL)
            continue;
        fprintf(output, "%s%s", printed ? ", " : "",
                ll1_terminal_name((Ll1Terminal)terminal));
        printed = 1;
    }
    if (!printed) fputs("<ningún terminal>", output);
}

/** Reporta un error de celda vacía con token, ubicación y esperados. */
static void report_table_error(FILE *output, const Ll1Token *found,
                               Ll1Nonterminal nonterminal)
{
    fprintf(output,
            "ERROR: %d:%d se encontró '%s' (%s); para %s se esperaba: ",
            found->line, found->column, found->lexeme,
            ll1_terminal_name(found->terminal),
            ll1_nonterminal_name(nonterminal));
    print_expected_row(output, nonterminal);
    fputc('\n', output);
}

/** Reporta una discrepancia directa entre dos terminales. */
static void report_terminal_error(FILE *output, const Ll1Token *found,
                                  Ll1Terminal expected)
{
    fprintf(output,
            "ERROR: %d:%d se encontró '%s' (%s); se esperaba %s\n",
            found->line, found->column, found->lexeme,
            ll1_terminal_name(found->terminal), ll1_terminal_name(expected));
}

/**
 * @brief Comprueba si una anticipación puede atravesar epsilon y ser consumida.
 * @param source Estado completo de la pila en la posición del cursor.
 * @param lookahead Terminal candidato.
 * @return 1 si puede consumirse, 0 si se rechaza y -1 si falta memoria.
 */
static int stack_accepts_lookahead(const Ll1Stack *source,
                                   Ll1Terminal lookahead)
{
    Ll1Stack probe = {0};
    size_t index;
    int accepted = 0;

    for (index = 0; index < source->count; ++index) {
        if (!stack_push(&probe, source->items[index])) {
            accepted = -1;
            goto done;
        }
    }
    while (probe.count != 0) {
        int top = probe.items[probe.count - 1];
        if (LL1_SYMBOL_IS_TERMINAL(top)) {
            accepted = (Ll1Terminal)top == lookahead;
            goto done;
        }
        if (LL1_SYMBOL_IS_NONTERMINAL(top)) {
            const Ll1Production *production = ll1_table_lookup(
                LL1_SYMBOL_TO_NONTERMINAL(top), lookahead);
            if (production == NULL) goto done;
            probe.count--;
            for (index = production->rhs_count; index > 0; --index) {
                if (!stack_push(&probe, production->rhs[index - 1])) {
                    accepted = -1;
                    goto done;
                }
            }
            continue;
        }
        goto done;
    }

done:
    free(probe.items);
    return accepted;
}

/** Imprime las anticipaciones que realmente progresan en la pila completa. */
static int print_expected_lines(FILE *output, const Ll1Stack *stack)
{
    size_t terminal;
    for (terminal = 0; terminal < LL1_T_COUNT; ++terminal) {
        Ll1Terminal candidate = (Ll1Terminal)terminal;
        int accepted;
        if (candidate == LL1_T_EOF || candidate == LL1_T_INVALID)
            continue;
        accepted = stack_accepts_lookahead(stack, candidate);
        if (accepted < 0) return 0;
        if (accepted)
            fprintf(output, "%s\n", ll1_terminal_name(candidate));
    }
    return 1;
}

/**
 * @brief Consume un prefijo y publica las opciones válidas al llegar a EOF.
 * @details Reutiliza el mismo algoritmo de pila, pero detiene la consulta antes
 * de consumir el EOF sintético que representa la posición del cursor.
 */
static Ll1AnalysisResult expected_from_tokens(const Ll1TokenList *tokens,
                                              FILE *output)
{
    Ll1Stack stack = {0};
    size_t position = 0;
    Ll1AnalysisResult result = LL1_ANALYSIS_REJECTED;

    if (!stack_push(&stack, LL1_TERMINAL_SYMBOL(LL1_T_EOF)) ||
        !stack_push(&stack, LL1_NONTERMINAL_SYMBOL(LL1_N_PROGRAM))) {
        fputs("error interno: memoria insuficiente para la pila LL(1)\n", stderr);
        result = LL1_ANALYSIS_INTERNAL_ERROR;
        goto done;
    }

    while (stack.count != 0 && position < tokens->count) {
        int top = stack.items[stack.count - 1];
        const Ll1Token *current = &tokens->items[position];

        if (current->terminal == LL1_T_EOF) {
            if (!print_expected_lines(output, &stack))
                result = LL1_ANALYSIS_INTERNAL_ERROR;
            else
                result = LL1_ANALYSIS_ACCEPTED;
            goto done;
        }

        if (LL1_SYMBOL_IS_TERMINAL(top)) {
            if ((Ll1Terminal)top != current->terminal) {
                report_terminal_error(stderr, current, (Ll1Terminal)top);
                goto done;
            }
            stack.count--;
            position++;
            continue;
        }

        if (LL1_SYMBOL_IS_NONTERMINAL(top)) {
            Ll1Nonterminal nonterminal = LL1_SYMBOL_TO_NONTERMINAL(top);
            const Ll1Production *production =
                ll1_table_lookup(nonterminal, current->terminal);
            size_t index;
            if (production == NULL) {
                report_table_error(stderr, current, nonterminal);
                goto done;
            }
            stack.count--;
            for (index = production->rhs_count; index > 0; --index) {
                if (!stack_push(&stack, production->rhs[index - 1])) {
                    fputs("error interno: memoria insuficiente para la pila LL(1)\n",
                          stderr);
                    result = LL1_ANALYSIS_INTERNAL_ERROR;
                    goto done;
                }
            }
            continue;
        }

        result = LL1_ANALYSIS_INTERNAL_ERROR;
        goto done;
    }

done:
    free(stack.items);
    return result;
}

void ll1_print_predictive_table(FILE *output)
{
    ll1_print_table(output);
}

Ll1AnalysisResult ll1_expected_file(const char *source_path, FILE *output)
{
    FILE *input;
    Ll1TokenList tokens = {0};
    Ll1AnalysisResult result;
    int close_status;

    input = fopen(source_path, "rb");
    if (input == NULL) {
        fprintf(stderr, "error: no se pudo abrir '%s': %s\n",
                source_path, strerror(errno));
        return LL1_ANALYSIS_IO_ERROR;
    }
    if (!read_tokens(input, &tokens)) {
        fputs("error interno: memoria insuficiente para los tokens LL(1)\n", stderr);
        result = LL1_ANALYSIS_INTERNAL_ERROR;
        goto destroy_scanner;
    }
    result = expected_from_tokens(&tokens, output);

destroy_scanner:
    (void)yylex_destroy();
    token_list_destroy(&tokens);
    close_status = fclose(input);
    if (close_status != 0) {
        fprintf(stderr, "error: no se pudo cerrar '%s'\n", source_path);
        return LL1_ANALYSIS_IO_ERROR;
    }
    return result;
}

