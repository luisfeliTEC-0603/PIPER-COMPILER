# Implementación del parser y CLI de Piper

Este documento describe el código agregado para implementar la gramática
básica de Piper, las extensiones de `loop`, arreglos, matrices e importaciones,
y el recorrido archivo → lexer → parser → AST de los issues #3, #4 y #6.

Los cambios están concentrados en:

- `src/parser.y`
- `src/main.c`
- `Makefile`

No se modificó `include/ast.h`, `src/ast.c` ni `src/lexer.l`. El parser utiliza
las funciones públicas declaradas por el AST y espera los tokens declarados en
la interfaz compartida con el lexer.

## 1. Cambios en `src/parser.y`

## 1.1. Encabezados utilizados

```c
#include "ast.h"

#include <stdio.h>
#include <stdlib.h>
```

`ast.h` aporta los tipos y constructores usados por las acciones de la
gramática. `stdio.h` se utiliza para imprimir errores sintácticos y `stdlib.h`
para liberar las cadenas que el lexer entrega al parser.

También se agregó:

```bison
%code requires {
#include "ast.h"
}
```

Este bloque se copia al encabezado generado por Bison. Es necesario porque el
valor semántico de los símbolos contiene tipos declarados en `ast.h`.

## 1.2. Conversión de ubicaciones

Bison representa la ubicación de cada símbolo mediante cuatro coordenadas:
línea inicial, columna inicial, línea final y columna final. El AST recibe las
mismas coordenadas mediante `SourceLocation`.

Para evitar repetir la conversión en cada producción se agregó:

```c
#define AST_LOCATION(location)                                             \
    source_location_make((location).first_line, (location).first_column,   \
                         (location).last_line, (location).last_column)
```

Ejemplos de uso:

```c
AST_LOCATION(@1)  /* ubicación del primer símbolo de la producción */
AST_LOCATION(@$)  /* ubicación completa del no terminal producido */
```

Una declaración usa `@$` porque el nodo abarca desde `create` hasta el
identificador. Un literal usa `@1` porque su ubicación corresponde a un solo
token.

## 1.3. Resultado producido por `yyparse`

Se agregó el parámetro:

```bison
%parse-param { AstNode **result }
```

Por esta directiva, la función generada recibe un puntero donde debe almacenar
la raíz del AST:

```c
int yyparse(AstNode **result);
```

La regla inicial realiza la transferencia:

```bison
input:
    program
    {
        *result = $1;
        $1 = NULL;
    }
;
```

`$1` es el nodo producido por `program`. Después de copiarlo a `*result` se
coloca en `NULL` para indicar que el parser ya no es propietario del nodo. A
partir de ese momento `main.c` es responsable de llamar `ast_free()`.

Bison solo acepta `input` cuando después encuentra el fin del archivo. Por
eso una instrucción válida seguida de contenido sobrante produce error.

## 1.4. Valores semánticos

La unión semántica contiene los datos que pueden transportar tokens y no
terminales:

```bison
%union {
    char *text;
    unsigned long long integer_value;
    PiperType type;
    AstOperator operator;
    AstNode *node;
    AstNodeList *node_list;
}
```

- `text`: identificadores, rutas y unidades inválidas.
- `integer_value`: valor numérico de `TOK_CONST_INT`.
- `type`: uno de los tipos escalares de Piper.
- `operator`: operadores representados por `AstOperator`.
- `node`: nodo individual del AST.
- `node_list`: lista de instrucciones u otros nodos.

Los no terminales se asociaron con el miembro correspondiente:

```bison
%type <node> program statement declaration assignment assignment_target
%type <node> block expression import_declaration loop_statement
%type <node> array_literal index_expression index_item dimension
%type <node_list> statement_list dimensions_opt dimension_list index_list
%type <node_list> parameter_list expression_list
%type <type> type
```

Esto permite que Bison verifique el uso de `$1`, `$2`, `$3` y `$$` dentro de
las acciones.

## 1.5. Precedencia y asociatividad

Las declaraciones aparecen de menor a mayor precedencia:

```bison
%left TOK_OR
%left TOK_XOR
%left TOK_AND
%nonassoc TOK_EQUAL TOK_NOT_EQUAL TOK_LESS TOK_LESS_EQUAL TOK_GREATER TOK_GREATER_EQUAL
%left TOK_PLUS TOK_MINUS
%left TOK_STAR TOK_SLASH TOK_PERCENT
%right TOK_CARET
%precedence UNARY
```

El comportamiento resultante es:

| Nivel | Operadores | Asociatividad |
| --- | --- | --- |
| 1 | `or` | izquierda |
| 2 | `xor` | izquierda |
| 3 | `and` | izquierda |
| 4 | `=`, `!=`, `<`, `<=`, `>`, `>=` | no asociativos |
| 5 | `+`, `-` | izquierda |
| 6 | `*`, `/`, `%` | izquierda |
| 7 | `^` | derecha |
| 8 | `-`, `~`, `++`, `--` unarios | prefijos |

Por ejemplo:

```piper
edad store 2 + 3 * 4.
```

construye primero `3 * 4` y después la suma.

Las comparaciones son no asociativas, así que se rechaza:

```piper
resultado store 1 < 2 < 3.
```

La precedencia actual interpreta:

```text
-2 ^ 2  como  (-2) ^ 2
```

Esta posición es provisional hasta que el grupo confirme el comportamiento de
la potencia y el menos unario.

## 1.6. Liberación de símbolos descartados

Se agregaron destructores por tipo semántico:

```bison
%destructor { free($$); } <text>
%destructor { ast_free($$); } <node>
%destructor { ast_node_list_free($$); } <node_list>
```

Si ocurre un error sintáctico, Bison puede retirar símbolos de su pila. Estos
destructores liberan las cadenas, nodos y listas retirados.

Las acciones siguen además el contrato de propiedad de `ast.h`. Cuando un
constructor recibe un puntero, pasa a ser su propietario incluso si no logra
crear el nodo. Por eso las acciones limpian las variables de la pila después
de transferirlas:

```c
$$ = ast_new_binary_expression(AST_OP_ADD, $1, $3, AST_LOCATION(@$));
$1 = NULL;
$3 = NULL;
```

Sin esas asignaciones, un camino de error podría intentar liberar dos veces el
mismo nodo.

Cuando una reserva o un constructor falla se ejecuta:

```c
YYNOMEM;
```

Esto termina `yyparse()` usando el estado de falta de memoria de Bison.

## 1.7. Programa y lista de instrucciones

La raíz se construye a partir de una lista:

```bison
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
```

La lista puede estar vacía:

```bison
statement_list:
    %empty
    {
        $$ = ast_node_list_create();
    }
```

Cada instrucción adicional se agrega con:

```c
ast_node_list_append($1, $2)
```

Si `append` falla, la lista y la instrucción continúan siendo propiedad de la
acción. La acción las libera antes de terminar con `YYNOMEM`.

## 1.8. Instrucciones y punto final

El no terminal `statement` reúne las construcciones que pueden aparecer en
el programa o dentro de un bloque:

```bison
statement:
    routine_declaration
  | declaration TOK_DOT
  | import_declaration TOK_DOT
  | assignment TOK_DOT
  | expression TOK_DOT
  | when_statement
  | alwhen_statement
  | loop_statement
  | output_statement TOK_DOT
  | break_statement TOK_DOT
  | block
;
```

Las instrucciones simples requieren punto. Las rutinas y estructuras que
terminan en un bloque no agregan un punto después de `}` porque el cierre del
bloque ya marca su final.

Ejemplos:

```piper
create B32 edad.
edad store 20.
{
    create bool activo.
}
```

## 1.9. Tipos y declaraciones

La declaración general es:

```bison
declaration:
    TOK_CREATE type dimensions_opt TOK_IDENTIFIER
;
```

`type` convierte cada token a un `PiperType`:

```bison
TOK_BOOL { $$ = PIPER_TYPE_BOOL; }
TOK_B8   { $$ = PIPER_TYPE_B8; }
TOK_UB8  { $$ = PIPER_TYPE_UB8; }
TOK_B16  { $$ = PIPER_TYPE_B16; }
TOK_UB16 { $$ = PIPER_TYPE_UB16; }
TOK_B32  { $$ = PIPER_TYPE_B32; }
TOK_UB32 { $$ = PIPER_TYPE_UB32; }
```

`dimensions_opt` siempre produce una lista. Para una variable escalar produce
una lista vacía; para un arreglo o matriz contiene sus tamaños:

```piper
create B32 edad.
create B32|10| edades.
create B32|3 & 4| tablero.
```

En la declaración, los `|` delimitan la lista de dimensiones y `&` separa
cada dimensión. Actualmente cada dimensión debe escribirse como literal
entero. Comprobar que sea mayor que cero corresponde al análisis semántico.

La acción entrega el nombre y la lista a:

```c
$$ = ast_new_variable_declaration(
    $2,
    $4,
    $3,
    AST_LOCATION(@$));
```

Los parámetros de rutina reutilizan `dimensions_opt`, por lo que también se
pueden declarar parámetros de arreglo:

```piper
routine B32 primero[B32|10| valores] {
    output valores|0|.
}
```

## 1.10. Asignaciones

La asignación separa el destino del valor:

```bison
assignment:
    assignment_target TOK_STORE expression
;
```

Un destino puede ser un identificador o un acceso indexado:

```bison
assignment_target:
    TOK_IDENTIFIER
  | index_expression
;
```

Después se construye la asignación:

```c
$$ = ast_new_assignment(target, $3, AST_LOCATION(@$));
```

Esto permite ambas formas:

```piper
edad store 10.
valores|i| store 10.
```

pero continúa rechazando una expresión arbitraria como destino:

```piper
2 store 10.
```

## 1.11. Bloques

Un bloque contiene otra lista de instrucciones:

```bison
block:
    TOK_LBRACE statement_list TOK_RBRACE
;
```

La acción transfiere la lista a:

```c
ast_new_block($2, AST_LOCATION(@$));
```

Como `statement_list` también acepta vacío, `{}` es un bloque válido.

## 1.12. Expresiones primarias escalares

Las formas escalares básicas son:

```bison
TOK_CONST_INT
TOK_TRUE
TOK_FALSE
TOK_IDENTIFIER
```

Cada forma construye el nodo correspondiente:

```c
ast_new_integer_literal(...)
ast_new_boolean_literal(...)
ast_new_identifier(...)
```

`expression` también acepta llamadas, literales de arreglo y accesos
indexados, descritos en las secciones posteriores.

## 1.13. Expresiones unarias

Se implementaron como operadores prefijos:

```bison
TOK_MINUS expression     %prec UNARY
TOK_NOT expression       %prec UNARY
TOK_INCREMENT expression %prec UNARY
TOK_DECREMENT expression %prec UNARY
```

Se convierten respectivamente en:

```text
AST_OP_NEGATE
AST_OP_LOGICAL_NOT
AST_OP_INCREMENT
AST_OP_DECREMENT
```

`%prec UNARY` hace que las cuatro producciones utilicen la precedencia
declarada para operadores unarios, en lugar de la precedencia normal del token
que aparece en la regla.

## 1.14. Expresiones binarias

Se agregaron producciones para:

```text
+  -  *  /  %  ^
=  !=  <  <=  >  >=
and  or  xor
```

Cada producción llama `ast_new_binary_expression()` usando el valor de
`AstOperator` correspondiente. Por ejemplo:

```c
$$ = ast_new_binary_expression(
    AST_OP_MULTIPLY,
    $1,
    $3,
    AST_LOCATION(@$));
```

## 1.15. Literales y listas de expresiones

Un literal de arreglo se escribe entre corchetes:

```piper
[1, 2, 3]
[]
```

Su producción es:

```bison
array_literal:
    TOK_LBRACKET expression_list TOK_RBRACKET
;
```

`expression_list` es una lista genérica de cero o más expresiones separadas
por comas. No obliga a utilizar sintaxis de llamada; son las producciones que
la rodean las que colocan los delimitadores y determinan su significado:

```bison
array_literal:
    TOK_LBRACKET expression_list TOK_RBRACKET
;

call_expression:
    TOK_IDENTIFIER TOK_LBRACKET expression_list TOK_RBRACKET
;
```

Así, `[1, 2]` crea `AST_ARRAY_LITERAL`, mientras `procesar[1, 2]` crea
`AST_CALL_EXPRESSION`. La misma lógica de lista se reutiliza para no mantener
dos producciones idénticas.

Los `|` no delimitan el contenido literal. En la sintaxis elegida tienen dos
usos relacionados con la forma del arreglo:

```piper
create B32|10| valores.  ~~ dimensiones de la declaración
valores|i|               ~~ acceso mediante índice
valores store [1, 2, 3]. ~~ valor literal asignado
```

Esta forma también coincide con los inicializadores mostrados en los ejemplos
del lenguaje. El constructor del literal recibe la lista con:

```c
ast_new_array_literal($2, AST_LOCATION(@$));
```

No se agregó una sintaxis especial que combine declaración e inicialización.
La compatibilidad entre dimensiones, número de elementos y tipos se revisará
en la fase semántica.

## 1.16. Acceso e indexación

La regla de acceso es:

```bison
index_expression:
    TOK_IDENTIFIER TOK_PIPE index_list TOK_PIPE
;
```

Los índices se separan con `&`:

```piper
valores|i|
matriz|fila & columna|
```

Cada índice aceptado actualmente es un literal entero o un identificador.
`ast_new_index_expression()` recibe por separado el nodo base y la lista de
índices. Por eso las dimensiones y los índices no se confunden en el AST:

- las dimensiones pertenecen a `AST_VARIABLE_DECLARATION` o `AST_PARAMETER`;
- un acceso produce `AST_INDEX_EXPRESSION`.

Como `index_expression` aparece en `expression` y en `assignment_target`, un
acceso puede leerse o recibir una asignación:

```piper
resultado store matriz|fila & columna| + 1.
matriz|fila & columna| store resultado.
```

## 1.17. `loop`

La sintaxis definitiva implementada es:

```text
loop[<control> & <inicio> -> <fin> & <paso>] <bloque>
```

Ejemplo:

```piper
loop[i & 0 -> 10 & 1] {
    valores|i| store valores|i| + 1.
}
```

Se usan corchetes porque la tabla de tokens actual no contiene paréntesis y
porque `when` y `alwhen` ya delimitan sus encabezados de esa manera. La acción
crea:

```c
ast_new_loop(control_name, begin, end, step, body, location);
```

El parser conserva inicio, fin y paso como expresiones. Decidir si el límite
final es inclusivo o exclusivo corresponde a la definición semántica del
lenguaje.

## 1.18. Importaciones

La producción implementada es:

```bison
import_declaration:
    TOK_BRING TOK_IDENTIFIER TOK_FROM TOK_FILE_PATH
;
```

La instrucción requiere el punto que agrega `statement`:

```piper
bring ordenar from "algoritmos/sort.rwt".
```

`TOK_FILE_PATH` no aparece en ninguna otra producción. Por eso una ruta solo
se acepta como parte de una importación y una ruta aislada produce error
sintáctico. La acción transfiere símbolo y ruta a
`ast_new_import_declaration()`.

## 1.19. Preparación para la tabla de símbolos

No se agregó una API provisional de tabla de símbolos porque todavía deben
acordarse su representación, propiedad, ámbitos y tratamiento de errores.
Cuando exista ese contrato, se puede construir la tabla al mismo tiempo que
el AST pasando a `yyparse()` un contexto como este:

```c
typedef struct {
    AstNode *root;
    SymbolTable *symbols;
} ParserContext;
```

Ese contexto sustituiría el `AstNode **result` de `%parse-param`. Las acciones
de declaración, parámetro, rutina e importación registrarían símbolos al
reducirse; la entrada y salida de `block` administrarían ámbitos. Antes de
integrarlo también debe decidirse cómo revertir o destruir los símbolos
insertados si el análisis termina con error.

## 1.20. Errores sintácticos

Bison invoca esta función cuando no encuentra una producción válida:

```c
void yyerror(AstNode **result, const char *message)
{
    (void)result;
    fprintf(stderr, "%d:%d: error sintáctico: %s\n",
            yylloc.first_line, yylloc.first_column, message);
}
```

El parámetro `result` forma parte de la firma porque también se pasó a
`yyparse()` mediante `%parse-param`. No se necesita para imprimir el error.

## 2. Cambios en `src/main.c`

## 2.1. Encabezados e interfaz generada

```c
#include "ast.h"
#include "parser.h"
```

`ast.h` proporciona `AstNode`, `ast_print()` y `ast_free()`. `parser.h`
proporciona la declaración de `yyparse()` generada desde `parser.y`.

Flex no genera un encabezado en la configuración actual, por lo que se
declaran las dos funciones utilizadas:

```c
void yyrestart(FILE *input_file);
int yylex_destroy(void);
```

## 2.2. Códigos de salida

Se definieron códigos diferentes para distinguir cada clase de resultado:

```c
enum {
    PIPOC_EXIT_SUCCESS = 0,
    PIPOC_EXIT_FRONTEND_ERROR = 1,
    PIPOC_EXIT_USAGE = 2,
    PIPOC_EXIT_IO_ERROR = 3
};
```

- `0`: análisis exitoso.
- `1`: error léxico, sintáctico o interno del frontend.
- `2`: argumentos incorrectos o extensión inválida.
- `3`: fallo al abrir o cerrar el archivo.

## 2.3. Mensaje de uso

```c
static void print_usage(FILE *output, const char *program_name)
{
    fprintf(output, "Uso: %s <archivo.rwt>\n", program_name);
}
```

Se imprime en `stderr` cuando la cantidad de argumentos no es exactamente dos:
el nombre del ejecutable y la ruta de entrada.

## 2.4. Validación de la extensión

`has_rwt_extension()` compara los últimos cuatro caracteres de la ruta con
`.rwt`:

```c
return path_length > extension_length
    && strcmp(path + path_length - extension_length, extension) == 0;
```

La comparación es sensible a mayúsculas. `programa.rwt` es válido y
`programa.RWT` no lo es.

## 2.5. Apertura del archivo

```c
input = fopen(input_path, "rb");
```

El modo `rb` abre el archivo para lectura sin modificarlo. Si `fopen()` falla,
se utiliza `strerror(errno)` para mostrar la causa reportada por el sistema.

## 2.6. Ejecución de Flex y Bison

```c
yyrestart(input);
parse_status = yyparse(&program);
(void)yylex_destroy();
```

1. `yyrestart(input)` configura el archivo como entrada del scanner.
2. `yyparse(&program)` solicita tokens al lexer y construye el AST.
3. `yylex_destroy()` libera los buffers internos creados por Flex.

`program` comienza en `NULL`. Si el parser acepta la entrada, la regla inicial
coloca allí el nodo `AST_PROGRAM`.

## 2.7. Impresión y liberación del AST

```c
if (parse_status == 0 && program != NULL) {
    ast_print(stdout, program);
    exit_status = PIPOC_EXIT_SUCCESS;
}
```

La impresión solo ocurre si Bison reportó éxito y entregó una raíz.

Después se ejecuta siempre:

```c
ast_free(program);
program = NULL;
```

Esto también es seguro ante errores porque `ast_free(NULL)` forma parte del
comportamiento contemplado por la implementación del AST.

## 2.8. Cierre del archivo

Todo camino posterior a una apertura exitosa llega a:

```c
fclose(input);
```

Si el cierre falla después de un análisis exitoso, el código de salida cambia
a `PIPOC_EXIT_IO_ERROR`.

## 3. Cambios en el Makefile

El Makefile actual construye el frontend completo con GCC, Bison y Flex. Sus
rutas principales son:

```make
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Iinclude -Ibuild
OBJ = build/parser.o build/lexer.o build/ast.o build/main.o
TARGET = bin/pipoc
```

`build/` contiene archivos generados y objetos; `bin/` contiene el ejecutable.

## 3.1. Eliminación de archivos incompletos

```make
.DELETE_ON_ERROR:
```

Si una receta falla mientras construye un destino, Make elimina ese destino
incompleto. Esto evita reutilizar un `parser.c`, objeto o ejecutable producido
parcialmente.

## 3.2. Orden de Bison y Flex

`parser.y` genera primero `build/parser.c` y `build/parser.h`:

```make
build/parser.c: src/parser.y include/ast.h
	@mkdir -p build
	$(BISON) -d -v -o build/parser.c src/parser.y
```

Después Flex puede generar el lexer, porque `lexer.l` incluye el encabezado
producido por Bison:

```make
build/lexer.c: src/lexer.l build/parser.h include/ast.h
	@mkdir -p build
	$(FLEX) -o build/lexer.c src/lexer.l
```

La opción `-v` de Bison genera también `build/parser.output` para inspeccionar
los estados y conflictos de la gramática.

## 3.3. Dependencias de `main.o`

La regla quedó:

```make
build/main.o: src/main.c build/parser.h include/ast.h
	$(CC) $(CFLAGS) -c src/main.c -o $@
```

`main.c` incluye `parser.h` y `ast.h`. Declararlos como dependencias provoca
que `main.o` se reconstruya si cambia cualquiera de esas interfaces.

Además, depender de `build/parser.h` garantiza que Bison se ejecute antes de
compilar `main.c`. La documentación usa exactamente `$(CFLAGS)`, igual que el
Makefile actual; no existe una variable `CPPFLAGS` en este archivo.

## 3.4. Revisión de conflictos

El objetivo `conflicts` depende del parser generado y consulta el informe de
Bison:

```make
conflicts: build/parser.c
	@if grep -Eq '[1-9][0-9]* (shift/reduce|reduce/reduce)' build/parser.output; then \
		echo "Bison reportó conflictos:"; \
		grep -E '[1-9][0-9]* (shift/reduce|reduce/reduce)' build/parser.output; \
		exit 1; \
	else \
		echo "Bison reportó 0 conflictos."; \
	fi
```

Además, `parser.y` declara `%expect 0`; un conflicto nuevo también hace que la
generación de Bison falle en vez de aceptarse silenciosamente.

## 3.5. Objetivos disponibles

No se agregó un objetivo de pruebas. Se mantienen los objetivos existentes:

- `make`: genera y enlaza `bin/pipoc`.
- `make generate`: genera parser y lexer.
- `make conflicts`: revisa conflictos de Bison.
- `make clean`: elimina `build/` y `bin/`.

La receta de `clean` actual es exactamente:

```make
clean:
	rm -rf build bin
```

Por lo tanto no elimina fuentes, documentación ni otros archivos del
repositorio. Como las recetas usan herramientas POSIX (`mkdir`, `test`,
`grep` y `rm`), en Windows deben ejecutarse desde WSL; también funcionan en
Linux y macOS.

## 4. Ejecución y verificación

La estructura de la gramática y sus conflictos puede revisarse con:

```sh
make clean
make conflicts
```

El frontend completo se construye con:

```sh
make
```

Para una prueba manual válida se puede crear un archivo temporal:

```sh
printf 'create B32 edad.\n' > /tmp/declaration.rwt
bin/pipoc /tmp/declaration.rwt
echo $?
```

El código esperado es `0`.

Para comprobar el punto obligatorio:

```sh
printf 'create B32 edad\n' > /tmp/declaration-missing-dot.rwt
bin/pipoc /tmp/declaration-missing-dot.rwt
echo $?
```

El código esperado es `1`.
