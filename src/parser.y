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
#include "ast.h"

#include <stdio.h>
#include <stdlib.h>

/* Convierte una ubicación de Bison al tipo público utilizado por el AST. */
#define AST_LOCATION(location)                                             \
    source_location_make((location).first_line, (location).first_column,   \
                         (location).last_line, (location).last_column)

/*
 * yylex() será generado por Flex. El parser lo llama cada vez que necesita
 * obtener el siguiente token de la entrada.
 */
int yylex(void);

/*
 * Bison llama a yyerror() cuando la secuencia de tokens no coincide con
 * ninguna producción válida de la gramática.
 */
void yyerror(AstNode **result, const char *message);
%}

/* parser.h debe conocer los tipos usados por YYSTYPE sin depender del orden
 * de includes de sus consumidores. */
%code requires {
#include "ast.h"
}

/*
 * Habilita el seguimiento de ubicaciones mediante yylloc: línea y columna
 * inicial y final de cada token. Más adelante el lexer actualizará esos
 * datos y el AST podrá conservarlos para reportar errores útiles.
 */
%locations

/* yyparse() entrega la raíz construida mediante el puntero del llamador. */
%parse-param { AstNode **result }

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

    /* Valores que producirán las acciones sintácticas. */
    PiperType type;
    AstOperator operator;
    AstNode *node;
    AstNodeList *node_list;
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

/* Token interno para que el lexer reporte una unidad no reconocida. */
%token <text> TOK_INVALID

/* Palabras reservadas que representan los tipos definidos por Piper. */
%token TOK_BOOL
%token TOK_B8
%token TOK_UB8
%token TOK_B16
%token TOK_UB16
%token TOK_B32
%token TOK_UB32

/*
 * Precedencia de menor a mayor. Las operaciones aritméticas son asociativas
 * a la izquierda, salvo la potencia, que es asociativa a la derecha. Las
 * comparaciones no se pueden encadenar sin una construcción explícita.
 *
 */
%left TOK_OR
%left TOK_XOR
%left TOK_AND
%nonassoc TOK_EQUAL TOK_NOT_EQUAL TOK_LESS TOK_LESS_EQUAL TOK_GREATER TOK_GREATER_EQUAL
%left TOK_PLUS TOK_MINUS
%left TOK_STAR TOK_SLASH TOK_PERCENT
%right TOK_CARET
%precedence UNARY

/* Recursos que Bison debe liberar al descartar símbolos durante un error. */
%destructor { free($$); } <text>
%destructor { ast_free($$); } <node>
%destructor { ast_node_list_free($$); } <node_list>

/* ------------------------------------------------------------
 * No terminales
 * ------------------------------------------------------------ */

/* Basicos: declaraciones, asignaciones y expresiones */

%type <node> program statement declaration assignment assignment_target
%type <node> block expression import_declaration loop_statement
%type <node> array_literal index_expression index_item dimension
%type <node_list> statement_list dimensions_opt dimension_list index_list
%type <type> type

/* Flujo: estructuras de control y rutinas */

%type <node> routine_declaration
%type <node> when_statement
%type <node> alwhen_statement
%type <node> break_statement
%type <node> output_statement
%type <node> call_expression
%type <node> parameter
%type <node> fallback_opt

%type <node_list> parameter_list
%type <node_list> expression_list

/* Cualquier conflicto nuevo debe fallar la generación y ser investigado. */
%expect 0

/* La gramática comenzará su análisis en el no terminal llamado input. */
%start input

/* Fin de las declaraciones e inicio de las producciones gramaticales. */
%%

/*
 * Bison exige implícitamente el fin de archivo después del símbolo inicial.
 * La acción transfiere la raíz al parámetro recibido por yyparse().
 */
input:
    program
    {
        *result = $1;
        $1 = NULL;
    }
;

program:
    statement_list
    {
        $$ = ast_new_program($1, AST_LOCATION(@$));
        $1 = NULL;

        if ($$ == NULL) {
            YYNOMEM;
        }
    }
;

/* El programa y los bloques pueden estar vacíos. */
statement_list:
    %empty
    {
        $$ = ast_node_list_create();
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | statement_list statement
    {
        if (!ast_node_list_append($1, $2)) {
            ast_node_list_free($1);
            ast_free($2);
            $1 = NULL;
            $2 = NULL;
            YYNOMEM;
        }

        $$ = $1;
        $1 = NULL;
        $2 = NULL;
    }
;

/* Las instrucciones simples requieren punto; un bloque se cierra con }. */
statement:
    routine_declaration
    {
        $$ = $1;
        $1 = NULL;
    }
  | declaration TOK_DOT
    {
        $$ = $1;
        $1 = NULL;
    }
  | import_declaration TOK_DOT
    {
        $$ = $1;
        $1 = NULL;
    }
  | assignment TOK_DOT
    {
        $$ = $1;
        $1 = NULL;
    }
  | expression TOK_DOT
    {
        $$ = $1;
        $1 = NULL;
    }
  | when_statement
    {
        $$ = $1;
        $1 = NULL;
    }
  | alwhen_statement
    {
        $$ = $1;
        $1 = NULL;
    }
  | loop_statement
    {
        $$ = $1;
        $1 = NULL;
    }
  | output_statement TOK_DOT
    {
        $$ = $1;
        $1 = NULL;
    }
  | break_statement TOK_DOT
    {
        $$ = $1;
        $1 = NULL;
    }
  | block
    {
        $$ = $1;
        $1 = NULL;
    }
;

/* ------------------------------------------------------------
 * Routine
 * ------------------------------------------------------------ */

routine_declaration:
    TOK_ROUTINE type TOK_IDENTIFIER TOK_LBRACKET parameter_list TOK_RBRACKET block
    {
        $$ = ast_new_routine_declaration($2, $3, $5, $7, AST_LOCATION(@$));
        $3 = NULL;
        $5 = NULL;
        $7 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
;

parameter_list:
    %empty
    {
        $$ = ast_node_list_create();
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | parameter
    {
        $$ = ast_node_list_create();
        if ($$ == NULL) {
            YYNOMEM;
        }
        if (!ast_node_list_append($$, $1)) {
            ast_node_list_free($$);
            ast_free($1);
            $$ = NULL;
            $1 = NULL;
            YYNOMEM;
        }
        $1 = NULL;
    }
  | parameter_list TOK_COMMA parameter
    {
        if (!ast_node_list_append($1, $3)) {
            ast_node_list_free($1);
            ast_free($3);
            $1 = NULL;
            $3 = NULL;
            YYNOMEM;
        }
        $$ = $1;
        $1 = NULL;
        $3 = NULL;
    }
;

parameter:
    type dimensions_opt TOK_IDENTIFIER
    {
        $$ = ast_new_parameter($1, $3, $2, AST_LOCATION(@$));
        $2 = NULL;
        $3 = NULL;

        if ($$ == NULL) {
            YYNOMEM;
        }
    }
;

declaration:
    TOK_CREATE type dimensions_opt TOK_IDENTIFIER
    {
        $$ = ast_new_variable_declaration($2, $4, $3,
                                          AST_LOCATION(@$));
        $3 = NULL;
        $4 = NULL;

        if ($$ == NULL) {
            YYNOMEM;
        }
    }
;

/*
 * Los valores entre |...| son dimensiones de una declaracion, no
 * accesos. La fase semantica validara despues que cada tamano sea valido.
 */
dimensions_opt:
    %empty
    {
        $$ = ast_node_list_create();
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | TOK_PIPE dimension_list TOK_PIPE
    {
        $$ = $2;
        $2 = NULL;
    }
;

dimension_list:
    dimension
    {
        $$ = ast_node_list_create();
        if ($$ == NULL) {
            YYNOMEM;
        }
        if (!ast_node_list_append($$, $1)) {
            ast_node_list_free($$);
            ast_free($1);
            $$ = NULL;
            $1 = NULL;
            YYNOMEM;
        }
        $1 = NULL;
    }
  | dimension_list TOK_AMPERSAND dimension
    {
        if (!ast_node_list_append($1, $3)) {
            ast_node_list_free($1);
            ast_free($3);
            $1 = NULL;
            $3 = NULL;
            YYNOMEM;
        }
        $$ = $1;
        $1 = NULL;
        $3 = NULL;
    }
;

dimension:
    TOK_CONST_INT
    {
        $$ = ast_new_integer_literal($1, AST_LOCATION(@1));
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
;

/* TOK_FILE_PATH se acepta exclusivamente en una importacion. */
import_declaration:
    TOK_BRING TOK_IDENTIFIER TOK_FROM TOK_FILE_PATH
    {
        $$ = ast_new_import_declaration($2, $4, AST_LOCATION(@$));
        $2 = NULL;
        $4 = NULL;

        if ($$ == NULL) {
            YYNOMEM;
        }
    }
;

type:
    TOK_BOOL { $$ = PIPER_TYPE_BOOL; }
  | TOK_B8   { $$ = PIPER_TYPE_B8; }
  | TOK_UB8  { $$ = PIPER_TYPE_UB8; }
  | TOK_B16  { $$ = PIPER_TYPE_B16; }
  | TOK_UB16 { $$ = PIPER_TYPE_UB16; }
  | TOK_B32  { $$ = PIPER_TYPE_B32; }
  | TOK_UB32 { $$ = PIPER_TYPE_UB32; }
;

assignment:
    assignment_target TOK_STORE expression
    {
        $$ = ast_new_assignment($1, $3, AST_LOCATION(@$));
        $1 = NULL;
        $3 = NULL;

        if ($$ == NULL) {
            YYNOMEM;
        }
    }
;

/* Tanto una variable como un elemento indexado pueden recibir store. */
assignment_target:
    TOK_IDENTIFIER
    {
        $$ = ast_new_identifier($1, AST_LOCATION(@1));
        $1 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | index_expression
    {
        $$ = $1;
        $1 = NULL;
    }
;

/* ------------------------------------------------------------
 * When / Fallback
 * ------------------------------------------------------------ */

when_statement:
    TOK_WHEN TOK_LBRACKET expression TOK_RBRACKET block fallback_opt
    {
        $$ = ast_new_when($3, $5, $6, AST_LOCATION(@$));
        $3 = NULL;
        $5 = NULL;
        $6 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
;

fallback_opt:
    %empty
    {
        $$ = NULL;
    }
  | TOK_FALLBACK block
    {
        $$ = $2;
        $2 = NULL;
    }
;

/* ------------------------------------------------------------
 * Alwhen / Break
 * ------------------------------------------------------------ */

alwhen_statement:
    TOK_ALWHEN TOK_LBRACKET expression TOK_RBRACKET block
    {
        $$ = ast_new_alwhen($3, $5, AST_LOCATION(@$));
        $3 = NULL;
        $5 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
;

/*
 * Sintaxis definitiva:
 *     loop[control & inicio -> fin & paso] { instrucciones }
 */
loop_statement:
    TOK_LOOP TOK_LBRACKET TOK_IDENTIFIER TOK_AMPERSAND expression
    TOK_ARROW expression TOK_AMPERSAND expression TOK_RBRACKET block
    {
        $$ = ast_new_loop($3, $5, $7, $9, $11, AST_LOCATION(@$));
        $3 = NULL;
        $5 = NULL;
        $7 = NULL;
        $9 = NULL;
        $11 = NULL;

        if ($$ == NULL) {
            YYNOMEM;
        }
    }
;

break_statement:
    TOK_BREAK
    {
        $$ = ast_new_break(AST_LOCATION(@$));
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
;


/* ------------------------------------------------------------
 * Output
 * ------------------------------------------------------------ */

output_statement:
    TOK_OUTPUT expression
    {
        $$ = ast_new_output($2, AST_LOCATION(@$));
        $2 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
;

block:
    TOK_LBRACE statement_list TOK_RBRACE
    {
        $$ = ast_new_block($2, AST_LOCATION(@$));
        $2 = NULL;

        if ($$ == NULL) {
            YYNOMEM;
        }
    }
;

expression:
    TOK_CONST_INT
    {
        $$ = ast_new_integer_literal($1, AST_LOCATION(@1));
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | TOK_TRUE
    {
        $$ = ast_new_boolean_literal(true, AST_LOCATION(@1));
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | TOK_FALSE
    {
        $$ = ast_new_boolean_literal(false, AST_LOCATION(@1));
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | TOK_IDENTIFIER
    {
        $$ = ast_new_identifier($1, AST_LOCATION(@1));
        $1 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | index_expression
    {
        $$ = $1;
        $1 = NULL;
    }
  | array_literal
    {
        $$ = $1;
        $1 = NULL;
    }
  | TOK_MINUS expression %prec UNARY
    {
        $$ = ast_new_unary_expression(AST_OP_NEGATE, $2, AST_LOCATION(@$));
        $2 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | TOK_NOT expression %prec UNARY
    {
        $$ = ast_new_unary_expression(AST_OP_LOGICAL_NOT, $2,
                                      AST_LOCATION(@$));
        $2 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | TOK_INCREMENT expression %prec UNARY
    {
        $$ = ast_new_unary_expression(AST_OP_INCREMENT, $2,
                                      AST_LOCATION(@$));
        $2 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | TOK_DECREMENT expression %prec UNARY
    {
        $$ = ast_new_unary_expression(AST_OP_DECREMENT, $2,
                                      AST_LOCATION(@$));
        $2 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | expression TOK_PLUS expression
    {
        $$ = ast_new_binary_expression(AST_OP_ADD, $1, $3,
                                       AST_LOCATION(@$));
        $1 = NULL;
        $3 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | expression TOK_MINUS expression
    {
        $$ = ast_new_binary_expression(AST_OP_SUBTRACT, $1, $3,
                                       AST_LOCATION(@$));
        $1 = NULL;
        $3 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | expression TOK_STAR expression
    {
        $$ = ast_new_binary_expression(AST_OP_MULTIPLY, $1, $3,
                                       AST_LOCATION(@$));
        $1 = NULL;
        $3 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | expression TOK_SLASH expression
    {
        $$ = ast_new_binary_expression(AST_OP_DIVIDE, $1, $3,
                                       AST_LOCATION(@$));
        $1 = NULL;
        $3 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | expression TOK_PERCENT expression
    {
        $$ = ast_new_binary_expression(AST_OP_REMAINDER, $1, $3,
                                       AST_LOCATION(@$));
        $1 = NULL;
        $3 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | expression TOK_CARET expression
    {
        $$ = ast_new_binary_expression(AST_OP_POWER, $1, $3,
                                       AST_LOCATION(@$));
        $1 = NULL;
        $3 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | expression TOK_EQUAL expression
    {
        $$ = ast_new_binary_expression(AST_OP_EQUAL, $1, $3,
                                       AST_LOCATION(@$));
        $1 = NULL;
        $3 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | expression TOK_NOT_EQUAL expression
    {
        $$ = ast_new_binary_expression(AST_OP_NOT_EQUAL, $1, $3,
                                       AST_LOCATION(@$));
        $1 = NULL;
        $3 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | expression TOK_LESS expression
    {
        $$ = ast_new_binary_expression(AST_OP_LESS, $1, $3,
                                       AST_LOCATION(@$));
        $1 = NULL;
        $3 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | expression TOK_LESS_EQUAL expression
    {
        $$ = ast_new_binary_expression(AST_OP_LESS_EQUAL, $1, $3,
                                       AST_LOCATION(@$));
        $1 = NULL;
        $3 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | expression TOK_GREATER expression
    {
        $$ = ast_new_binary_expression(AST_OP_GREATER, $1, $3,
                                       AST_LOCATION(@$));
        $1 = NULL;
        $3 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | expression TOK_GREATER_EQUAL expression
    {
        $$ = ast_new_binary_expression(AST_OP_GREATER_EQUAL, $1, $3,
                                       AST_LOCATION(@$));
        $1 = NULL;
        $3 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | expression TOK_AND expression
    {
        $$ = ast_new_binary_expression(AST_OP_LOGICAL_AND, $1, $3,
                                       AST_LOCATION(@$));
        $1 = NULL;
        $3 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | expression TOK_OR expression
    {
        $$ = ast_new_binary_expression(AST_OP_LOGICAL_OR, $1, $3,
                                       AST_LOCATION(@$));
        $1 = NULL;
        $3 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | expression TOK_XOR expression
    {
        $$ = ast_new_binary_expression(AST_OP_LOGICAL_XOR, $1, $3,
                                       AST_LOCATION(@$));
        $1 = NULL;
        $3 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | call_expression
    {
        $$ = $1;
        $1 = NULL;
    }
;

/* ------------------------------------------------------------
 * Arreglos: literales y acceso indexado
 * ------------------------------------------------------------ */

/* Lista literal: los corchetes delimitan y las comas separan elementos. */
array_literal:
    TOK_LBRACKET expression_list TOK_RBRACKET
    {
        $$ = ast_new_array_literal($2, AST_LOCATION(@$));
        $2 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
;

/*
 * Un acceso usa |...| para no confundirse con una llamada nombre[...].
 * Cada indice es un entero o una variable; las validaciones de tipo y rango
 * pertenecen a la fase semantica.
 */
index_expression:
    TOK_IDENTIFIER TOK_PIPE index_list TOK_PIPE
    {
        AstNode *base = ast_new_identifier($1, AST_LOCATION(@1));
        $1 = NULL;

        if (base == NULL) {
            YYNOMEM;
        }

        $$ = ast_new_index_expression(base, $3, AST_LOCATION(@$));
        $3 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
;

index_list:
    index_item
    {
        $$ = ast_node_list_create();
        if ($$ == NULL) {
            YYNOMEM;
        }
        if (!ast_node_list_append($$, $1)) {
            ast_node_list_free($$);
            ast_free($1);
            $$ = NULL;
            $1 = NULL;
            YYNOMEM;
        }
        $1 = NULL;
    }
  | index_list TOK_AMPERSAND index_item
    {
        if (!ast_node_list_append($1, $3)) {
            ast_node_list_free($1);
            ast_free($3);
            $1 = NULL;
            $3 = NULL;
            YYNOMEM;
        }
        $$ = $1;
        $1 = NULL;
        $3 = NULL;
    }
;

index_item:
    TOK_CONST_INT
    {
        $$ = ast_new_integer_literal($1, AST_LOCATION(@1));
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | TOK_IDENTIFIER
    {
        $$ = ast_new_identifier($1, AST_LOCATION(@1));
        $1 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
;

/* ------------------------------------------------------------
 * Call expression
 * ------------------------------------------------------------ */

call_expression:
    TOK_IDENTIFIER TOK_LBRACKET expression_list TOK_RBRACKET
    {
        $$ = ast_new_call($1, $3, AST_LOCATION(@$));
        $1 = NULL;
        $3 = NULL;
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
;

/* Lista generica usada como elementos de un literal o argumentos de llamada. */
expression_list:
    %empty
    {
        $$ = ast_node_list_create();
        if ($$ == NULL) {
            YYNOMEM;
        }
    }
  | expression
    {
        $$ = ast_node_list_create();
        if ($$ == NULL) {
            YYNOMEM;
        }
        if (!ast_node_list_append($$, $1)) {
            ast_node_list_free($$);
            ast_free($1);
            $$ = NULL;
            $1 = NULL;
            YYNOMEM;
        }
        $1 = NULL;
    }
  | expression_list TOK_COMMA expression
    {
        if (!ast_node_list_append($1, $3)) {
            ast_node_list_free($1);
            ast_free($3);
            $1 = NULL;
            $3 = NULL;
            YYNOMEM;
        }
        $$ = $1;
        $1 = NULL;
        $3 = NULL;
    }
;

/* Fin de la gramática e inicio del código C auxiliar. */
%%

/*
 * Implementación mínima del manejador de errores requerido por Bison.
 * El lexer será responsable de mantener yylloc actualizado.
 */
void yyerror(AstNode **result, const char *message)
{
    (void)result;
    fprintf(stderr, "%d:%d: error sintáctico: %s\n",
            yylloc.first_line, yylloc.first_column, message);
}
