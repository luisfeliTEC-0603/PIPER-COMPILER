/*
 * parser.y contiene la especificación que Bison utilizará para generar
 * el analizador sintáctico en C y el encabezado compartido con Flex.
 *
 * Este  archivo de Bison se divide en tres secciones:
 *   1. declaraciones y configuración;
 *   2. reglas de la gramática;
 *   3. código C auxiliar.
 *
 * Las secciones están separadas por las líneas que contienen %%.
 */

%{
#include <stdio.h>

/*
 * yylex() será generado por Flex. El parser lo llama cada vez que necesita
 * obtener el siguiente token de la entrada.
 */
int yylex(void);

/*
 * Bison llama a yyerror() cuando la secuencia de tokens no coincide con
 * ninguna producción válida de la gramática.
 */
void yyerror(const char *message);
%}

/*
 * Habilita el seguimiento de ubicaciones mediante yylloc: línea y columna
 * inicial y final de cada token. Más adelante el lexer actualizará esos
 * datos y el AST podrá conservarlos para reportar errores útiles.
 */
%locations

/*
 * Además de su categoría, algunos tokens transportan un valor.
 * YYSTYPE será una unión con todas las clases de valores posibles.
 */
%union {
    /* Identificadores y rutas de importación. */
    char *text;

    /*
     * Valor de un literal entero sin signo. El signo negativo se reconocerá
     * como TOK_MINUS separado del número.
     */
    unsigned long long integer_value;
}

/*
 * Palabras reservadas. El lexer devolverá uno de estos tokens cuando
 * encuentre exactamente la palabra correspondiente.
 */
%token TOK_CREATE
%token TOK_ROUTINE
%token TOK_LOOP
%token TOK_ALWHEN
%token TOK_WHEN
%token TOK_FALLBACK
%token TOK_STORE
%token TOK_BRING
%token TOK_FROM
%token TOK_BREAK
%token TOK_OUTPUT
%token TOK_NONE

/* Operadores aritméticos, comparativos y lógicos. */
%token TOK_PLUS
%token TOK_MINUS
%token TOK_STAR
%token TOK_SLASH
%token TOK_PERCENT
%token TOK_CARET
%token TOK_INCREMENT
%token TOK_DECREMENT
%token TOK_EQUAL
%token TOK_NOT_EQUAL
%token TOK_LESS
%token TOK_LESS_EQUAL
%token TOK_GREATER
%token TOK_GREATER_EQUAL
%token TOK_AND
%token TOK_OR
%token TOK_XOR
%token TOK_NOT

/* Símbolos que delimitan bloques, listas, instrucciones e índices. */
%token TOK_LBRACE
%token TOK_RBRACE
%token TOK_LBRACKET
%token TOK_RBRACKET
%token TOK_DOT
%token TOK_COMMA
%token TOK_AMPERSAND
%token TOK_PIPE
%token TOK_ARROW

/*
 * Literales e identificadores.
 *
 * El texto entre <...> selecciona el campo correspondiente de %union.
 * Por ejemplo, TOK_IDENTIFIER transportará su lexema mediante yylval.text,
 * mientras TOK_CONST_INT transportará el número mediante
 * yylval.integer_value.
 */
%token TOK_TRUE
%token TOK_FALSE
%token <integer_value> TOK_CONST_INT
%token <text> TOK_IDENTIFIER
%token <text> TOK_FILE_PATH

/* Palabras reservadas que representan los tipos definidos por Piper. */
%token TOK_BOOL
%token TOK_B8
%token TOK_UB8
%token TOK_B16
%token TOK_UB16
%token TOK_B32
%token TOK_UB32

/* La gramática comenzará su análisis en el no terminal llamado input. */
%start input

/* Fin de las declaraciones e inicio de las producciones gramaticales. */
%%

/*
 * Regla temporal para que Bison pueda generar parser.c y parser.h antes de
 * implementar la gramática real. Por ahora solo acepta una entrada vacía.
 */
input:
    /* vacío */
;

/* Fin de la gramática e inicio del código C auxiliar. */
%%

/*
 * Implementación mínima del manejador de errores requerido por Bison.
 * Después incorporaremos la ubicación guardada en yylloc.
 */
void yyerror(const char *message)
{
    fprintf(stderr, "Error sintáctico: %s\n", message);
}
