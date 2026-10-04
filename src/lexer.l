%option noyywrap nounput noinput never-interactive nodefault

/*
 * Estado exclusivo reservado para comentarios delimitados por ***.
 * TODO(lexer-comments): entrar, consumir contenido, salir y detectar EOF.
 */
%x BLOCK_COMMENT

%{
/*
 * ast.h debe aparecer antes de parser.h porque YYSTYPE contiene tipos del
 * AST. parser.h es generado por Bison a partir de parser.y.
 */
#include "ast.h"
#include "parser.h"

#include <stddef.h>

/*
 * Flex llama YY_USER_ACTION antes de ejecutar la acción de cada regla.
 * El skeleton conserva el punto de extensión sin implementar todavía el
 * cálculo real de línea y columna.
 */
static void lexer_update_location(const char *text, int length)
{
    (void)text;
    (void)length;

    /* TODO(lexer-locations): actualizar first/last line/column en yylloc. */
}

#define YY_USER_ACTION lexer_update_location(yytext, yyleng);
%}

/*--- Expresiones Regulares para Patrones Basicos ---*/
DIGIT       [0-9]
IDENTIFIER  [A-Za-z_][A-Za-z0-9_]*
FILE_PATH   \"[A-Za-z0-9_./-]+\.rwt\"
WHITESPACE  [ \t\r\n]+

%%

/* ============================================================
 * Comentarios
 * ============================================================ */

"~~"[^\n]*          { /* comentario de una línea: ignorar */ }

"***"               { BEGIN(BLOCK_COMMENT); }

<BLOCK_COMMENT>"***"  { BEGIN(INITIAL); }

<BLOCK_COMMENT>\n     { /* consumir nueva línea dentro del bloque */ }

<BLOCK_COMMENT>.      { /* consumir cualquier otro carácter */ }

<BLOCK_COMMENT><<EOF>> {
    fprintf(stderr,
            "%d:%d: error léxico: comentario de bloque sin cerrar\n",
            yylloc.first_line, yylloc.first_column);
    return TOK_INVALID;
}

/* ============================================================
 * Espacios en blanco
 * ============================================================ */

{WHITESPACE}        { /* ignorar */ }

/* ============================================================
 * Palabras reservadas y tipos
 * ============================================================ */

"create"            { return TOK_CREATE; }
"routine"           { return TOK_ROUTINE; }
"loop"              { return TOK_LOOP; }
"alwhen"            { return TOK_ALWHEN; }
"when"              { return TOK_WHEN; }
"fallback"          { return TOK_FALLBACK; }
"store"             { return TOK_STORE; }
"bring"             { return TOK_BRING; }
"from"              { return TOK_FROM; }
"break"             { return TOK_BREAK; }
"output"            { return TOK_OUTPUT; }
"none"              { return TOK_NONE; }
"true"              { return TOK_TRUE; }
"false"             { return TOK_FALSE; }

"bool"              { return TOK_BOOL; }
"B8"                { return TOK_B8; }
"uB8"               { return TOK_UB8; }
"B16"               { return TOK_B16; }
"uB16"              { return TOK_UB16; }
"B32"               { return TOK_B32; }
"uB32"              { return TOK_UB32; }

/* ============================================================
 * Operadores 
 * ============================================================ */

"++"                { return TOK_INCREMENT; }
"--"                { return TOK_DECREMENT; }
"->"                { return TOK_ARROW; }
"!="                { return TOK_NOT_EQUAL; }
"<="                { return TOK_LESS_EQUAL; }
">="                { return TOK_GREATER_EQUAL; }

"+"                 { return TOK_PLUS; }
"-"                 { return TOK_MINUS; }
"*"                 { return TOK_STAR; }
"/"                 { return TOK_SLASH; }
"%"                 { return TOK_PERCENT; }
"^"                 { return TOK_CARET; }
"="                 { return TOK_EQUAL; }
"<"                 { return TOK_LESS; }
">"                 { return TOK_GREATER; }
"~"                 { return TOK_NOT; }

"and"               { return TOK_AND; }
"or"                { return TOK_OR; }
"xor"               { return TOK_XOR; }

/* ============================================================
 * Delimitadores
 * ============================================================ */

"{"                 { return TOK_LBRACE; }
"}"                 { return TOK_RBRACE; }
"["                 { return TOK_LBRACKET; }
"]"                 { return TOK_RBRACKET; }
"."                 { return TOK_DOT; }
","                 { return TOK_COMMA; }
"&"                 { return TOK_AMPERSAND; }
"|"                 { return TOK_PIPE; }

/* ============================================================
 * Valores: rutas, enteros e identificadores
 * ============================================================ */

{FILE_PATH} {
    yylval.text = lexer_strdup_unquoted(yytext, yyleng);
    return TOK_FILE_PATH;
}

{DIGIT}+ {
    errno = 0;
    char *endptr = NULL;
    unsigned long long value = strtoull(yytext, &endptr, 10);

    if (errno == ERANGE || endptr == yytext || *endptr != '\0') {
        fprintf(stderr,
                "%d:%d: error léxico: entero fuera de rango '%s'\n",
                yylloc.first_line, yylloc.first_column, yytext);
        return TOK_INVALID;
    }

    yylval.integer_value = value;
    return TOK_CONST_INT;
}

{IDENTIFIER} {
    yylval.text = lexer_strdup(yytext);
    return TOK_IDENTIFIER;
}

/* ============================================================
 * Otros
 * ============================================================ */

. {
    fprintf(stderr,
            "%d:%d: error léxico: carácter no reconocido '%s'\n",
            yylloc.first_line, yylloc.first_column, yytext);
    yylval.text = NULL;
    return TOK_INVALID;
}

<<EOF>>             { return 0; }

%%

/*
 * TODO(lexer-memory): añadir una función que copie yytext. Su resultado debe
 * pasar al parser; nunca se debe guardar el puntero de yytext directamente.
 */
