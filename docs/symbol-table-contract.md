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
ensamblador y `total_stack_frame_size` indica el maximo espacio para parametros
y variables locales de una activacion, incluidos los bloques anidados. Bloques
hermanos reutilizan offsets; la rutina conserva el mayor consumo simultaneo.
Una rutina anidada puede acceder a globales y llamar a otras rutinas, pero no
puede capturar variables o parametros locales de una rutina exterior: cada
activacion tiene su propio marco y no existe un enlace al marco exterior.

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

Una variable global tiene `address_or_offset` como desplazamiento dentro de
la memoria de datos de 64 KiB. Las globales comienzan en `0x0000` y se asignan
alineadas a palabras de 4 bytes. Se reservan los ultimos 4 KiB para la pila:
las globales solo pueden ocupar `0x0000` a `0xEFFF` (60 KiB). Si PORYGON
mantiene el mapa fisico propuesto, el generador suma `0x00010000` al
desplazamiento de una global para obtener su direccion fisica.

Un parametro o una variable local tiene `address_or_offset` como offset
relativo a `r7`. El ensamblador debe generar el acceso como `r7 + offset`, no
como una direccion absoluta. Las rutinas no usan ese campo como direccion de
datos.

Las variables locales y globales que son matrices reservan el almacenamiento
completo: una matriz de `filas x columnas` elementos de 32 bits ocupa
`filas * columnas * 4` bytes. Un parametro matricial se trata como una
referencia a la matriz y ocupa solamente 4 bytes para guardar su direccion
base; sus campos de dimensiones se conservan para validacion y calculo de
indices.

## Tipos enteros y retornos

Cada simbolo conserva su tipo exacto (`B8`, `uB8`, `B16`, `uB16`, `B32` o
`uB32`) en `declared_type`. Los literales enteros no tienen ancho propio:
pueden asignarse, pasarse como argumento o devolverse si su valor cabe en el
tipo esperado. Para variables de tipos diferentes se permite una conversion
solo si el rango completo del origen cabe en el destino; si el valor concreto
se conoce con seguridad, basta con que ese valor quepa. Los elementos de un
arreglo literal se comprueban uno por uno. Un arreglo almacenado y pasado por
referencia debe conservar el mismo tipo de elemento.

Los rangos son `B8` [-128, 127], `uB8` [0, 255], `B16` [-32768, 32767],
`uB16` [0, 65535], `B32` [-2147483648, 2147483647] y `uB32`
[0, 4294967295]. Por ahora todos los escalares ocupan una ranura alineada
de 4 bytes; el ancho del tipo controla los valores permitidos, no el espacio
reservado. Un literal que no cabe en ningun tipo entero es invalido. El
generador de codigo tendra que conservar el signo y el ancho. Una operacion
que produzca un valor fuera del rango de su tipo debe fallar: el semantico
rechaza los resultados que puede calcular y el generador debe detectar los
demas al ejecutar, sin envolver ni truncar el resultado.

Los escalares locales deben recibir un valor antes de leerse. Los parametros
ya llegan inicializados; una asignacion dentro de un `when` solo cuenta despues
si todos los caminos que continuan asignan la variable. Una asignacion dentro
de un ciclo no garantiza por si sola un valor despues del ciclo. Esta regla
todavia no sigue por separado los elementos de arreglos ni las variables
globales.

Toda rutina debe ejecutar `output` en todos sus caminos. La gramatica actual
solo permite declarar rutinas con un tipo de resultado; esta comprobacion
pertenece al analisis semantico porque depende de las ramas del cuerpo.

## Limite de pila

La pila tiene un presupuesto de 4096 bytes. El analisis semantico comprueba
que cada marco individual cabe y, para cadenas de llamadas sin recursión,
suma los marcos activos. Incluye una reserva de 64 bytes por llamada para
direccion de retorno, registros guardados y temporales. Rechaza las cadenas
de profundidad conocida que superan el presupuesto.
El asignador tambien impide que un offset local supere los 4096 bytes durante
el registro de simbolos; el analisis posterior incluye la reserva por llamada.

La recursión es válida. Su profundidad depende de los valores que aparezcan
durante la ejecución, así que el semántico no puede asegurar que la suma de
sus marcos quepa en 4096 bytes. El generador de código debe comprobar el
stack pointer antes de reservar cada nuevo marco y detener la ejecución si
cruzaría el límite inferior de la pila. Debe respetar los 64 bytes de reserva
por llamada; si necesita más, hay que aumentar la reserva y repetir esta
validación. El presupuesto también se aplica a rutinas que nunca se invocan
desde `main`.

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

El analisis semantico rechaza indices literales y variables cuyo valor conoce
en el punto del acceso si estan fuera de las dimensiones declaradas. Cuando
el valor no puede determinarse de forma segura, el futuro generador de codigo
debera comprobar `0 <= indice < dimension` antes de leer o escribir cada
elemento. En una matriz se comprueba cada eje por separado.
