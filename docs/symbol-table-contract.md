# Contrato de la tabla de simbolos

La tabla de simbolos se crea en `main.c` antes de llamar a `yyparse()` y se
mantiene viva durante todo el pipeline del compilador. El parser la recibe
como segundo parametro de `yyparse()`, registra las declaraciones mientras
construye el AST y deja la tabla disponible para las fases posteriores. La
tabla solo se destruye al final del proceso, despues del analisis semantico y
de la generacion de ensamblador.

## Organizacion

`SymbolTable.global_scope` apunta al ambito global. `SymbolTable.current_scope`
apunta al ambito activo durante el parseo. Cada `Scope` conserva un puntero
`parent` hacia el ambito que lo contiene. Ademas, `SymbolTable.all_scopes`
recorre todos los ambitos creados, incluso los que ya fueron cerrados por el
parser. Por eso cerrar un bloque ya no borra sus simbolos: solamente cambia
`current_scope` al padre. La liberacion ocurre en `symbol_table_destroy()`.

Cada `Scope` expone sus simbolos mediante `symbols_head` y cada `Symbol` se
recorre con su campo `next`. El orden de esa lista es el inverso del orden de
insercion.

## Rutinas

Un simbolo con clase `SYMBOL_FUNC` representa una etiqueta de codigo, no una
direccion de datos. Su campo `name` es la etiqueta que debe emitir el
ensamblador y `total_stack_frame_size` indica el tamaño del frame local.

El campo `function_scope` del simbolo de rutina apunta al scope que contiene
sus parametros y variables locales. Para recorrer una rutina se puede usar:

```c
Scope *scope = function_symbol->function_scope;
for (Symbol *symbol = scope->symbols_head;
     symbol != NULL;
     symbol = symbol->next) {
    /* parametro o variable local */
}
```

## Direcciones

Una variable global tiene `address_or_offset` como direccion absoluta dentro
de la memoria de datos de 64 KiB. Las direcciones comienzan en `0x0000` y se
asignan alineadas a palabras de 4 bytes.

Un parametro o una variable local tiene `address_or_offset` como offset
relativo a `r7`. El ensamblador debe generar el acceso como `r7 + offset`, no
como una direccion absoluta. Las rutinas no usan ese campo como direccion de
datos.

## Uso por fase

El parser registra nombres, clases, tipos base y ubicaciones. Las listas de
dimensiones siguen siendo propiedad del AST y no se reinterpretan en la
gramatica. El analisis semantico debe consultar esas listas en el AST,
resolver las dimensiones de cada matriz y completar o validar la informacion
de tamaño de los simbolos antes de generar ensamblador.

El analisis semantico puede comenzar en `table->global_scope`, buscar nombres
con `symbol_table_lookup()`, revisar ambitos de rutina mediante
`function_scope` y consultar tipo y offsets desde cada `Symbol`.

El ensamblador puede recorrer los simbolos globales para emitir referencias a
datos y etiquetas de rutina. Para cada rutina usa `function_scope` para
conocer parametros, variables locales y el frame. El AST sigue siendo la
fuente del orden y de la estructura de las instrucciones; la tabla aporta la
resolucion de nombres y las ubicaciones.