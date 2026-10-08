# Implementación de los issues #3 y #4

Este documento describe el código agregado para implementar la gramática
básica de Piper y conectar el recorrido archivo → lexer → parser → AST.

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
%type <node> program statement declaration assignment block expression
%type <node_list> statement_list
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

Se implementaron tres formas de instrucción:

```bison
statement:
    declaration TOK_DOT
  | assignment TOK_DOT
  | block
;
```

Las declaraciones y asignaciones requieren punto. El bloque no agrega un
punto después de `}` porque su delimitador de cierre ya marca el final.

Ejemplos:

```piper
create B32 edad.
edad store 20.
{
    create bool activo.
}
```

## 1.9. Tipos y declaraciones

La declaración reconocida es:

```bison
declaration:
    TOK_CREATE type TOK_IDENTIFIER
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

El constructor de declaración requiere una lista de dimensiones. Como los
arreglos están fuera del alcance actual, se crea una lista vacía:

```c
AstNodeList *dimensions = ast_node_list_create();

$$ = ast_new_variable_declaration(
    $2,
    $3,
    dimensions,
    AST_LOCATION(@$));
```

## 1.10. Asignaciones

La asignación básica es:

```bison
assignment:
    TOK_IDENTIFIER TOK_STORE expression
;
```

El identificador se convierte primero en el nodo destino:

```c
AstNode *target = ast_new_identifier($1, AST_LOCATION(@1));
```

Después se construye la asignación:

```c
$$ = ast_new_assignment(target, $3, AST_LOCATION(@$));
```

El destino se restringe a un identificador. Esto permite:

```piper
edad store 10.
```

y rechaza:

```piper
2 store 10.
```

Los destinos con índices se agregarán junto con el soporte de arreglos.

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

## 1.12. Expresiones primarias

Se implementaron:

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

## 1.15. Errores sintácticos

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

El Makefile ya contenía el flujo principal de generación y compilación. Se
agregaron dos ajustes para los cambios del frontend.

## 3.1. Eliminación de archivos incompletos

```make
.DELETE_ON_ERROR:
```

Si una receta falla mientras construye un destino, Make elimina ese destino
incompleto. Esto evita reutilizar un `parser.c`, objeto o ejecutable producido
parcialmente.

## 3.2. Dependencias de `main.o`

La regla quedó:

```make
build/main.o: src/main.c build/parser.h include/ast.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c src/main.c -o $@
```

`main.c` incluye `parser.h` y `ast.h`. Declararlos como dependencias provoca
que `main.o` se reconstruya si cambia cualquiera de esas interfaces.

Además, depender de `build/parser.h` garantiza que Bison se ejecute antes de
compilar `main.c`.

No se agregó un objetivo de pruebas. Se mantienen los objetivos existentes:

- `make`: genera y enlaza `bin/pipoc`.
- `make generate`: genera parser y lexer.
- `make conflicts`: revisa conflictos de Bison.
- `make clean`: elimina `build/` y `bin/`.

## 4. Ejecución y verificación

La estructura de la gramática y sus conflictos puede revisarse con:

```sh
make clean
make conflicts
```

Cuando estén integradas las implementaciones del lexer y del AST, el frontend
completo se construye con:

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
