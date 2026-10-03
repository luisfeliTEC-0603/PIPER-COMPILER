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

/* Patrones básicos compartidos por las futuras reglas. */
DIGIT       [0-9]
IDENTIFIER  [A-Za-z_][A-Za-z0-9_]*
FILE_PATH   \"[A-Za-z0-9_./-]+\.rwt\"
WHITESPACE  [ \t\r\n]+

%%

    /*
     * TODO(lexer-comments): comentarios antes de TOK_NOT y TOK_STAR:
     *   - ~~ hasta el final de la línea;
     *   - *** ... *** mediante BLOCK_COMMENT.
     */

    /* TODO(lexer-whitespace): ignorar WHITESPACE después de ubicarlo. */

    /*
     * TODO(lexer-keywords): palabras reservadas antes de IDENTIFIER cuando
     * tengan la misma longitud. Flex elige primero el match más largo y,
     * ante empate, la regla escrita primero.
     */

    /*
     * TODO(lexer-operators): operadores de varios caracteres y luego los de
     * uno. Incluir ++, --, ->, !=, <=, >= antes de sus componentes.
     */

    /* TODO(lexer-delimiters): llaves, corchetes, punto, coma, &, y |. */

    /*
     * TODO(lexer-values):
     *   - FILE_PATH copia el contenido sin las comillas a yylval.text;
     *   - {DIGIT}+ convierte con control de rango a integer_value;
     *   - IDENTIFIER copia yytext a memoria dinámica.
     */

    /*
     * Regla temporal del skeleton: ninguna entrada no vacía debe aceptarse
     * por accidente antes de implementar el catálogo. TOK_INVALID será
     * descartado y liberado por Bison; NULL es seguro para free().
     */
<BLOCK_COMMENT>(.|\n) { yylval.text = NULL; return TOK_INVALID; }
(.|\n)          { yylval.text = NULL; return TOK_INVALID; }

<<EOF>>         { return 0; }

%%

/*
 * TODO(lexer-memory): añadir una función que copie yytext. Su resultado debe
 * pasar al parser; nunca se debe guardar el puntero de yytext directamente.
 */
