# Matrices en ASM

El generador admite arreglos enteros de una o dos dimensiones. Una matriz
`create B32|2&3| m.` ocupa seis palabras de 4 bytes, en orden por filas.
`m store [[1,2,3],[4,5,6]].` escribe los seis valores en direcciones
consecutivas; no existe una instrucción especial de matriz en PORYGON.

La dirección de `m|i&j|` se calcula como:

```text
base + (i * columnas + j) * 4
```

Por ejemplo, `m|1&2|` está a 20 bytes de la base. Las globales usan su dirección
en memoria de datos; las locales usan `r7 + offset`. Un parámetro matricial
contiene un puntero, cargado desde su ranura en el frame. Las dimensiones
validadas por el semántico determinan las columnas y los límites.

`codegen_array_index_address()` genera esa dirección. Una lectura emite
`lowi` y un store emite `stwi`. Se comprueba cada índice con `ltn` sin signo
antes de multiplicarlo: esto rechaza índices mayores o iguales a la dimensión
y valores negativos en complemento a dos. Un acceso inválido salta al bucle
de parada `error_indice`, sin realizar el acceso solicitado. Los índices
constantes inválidos se rechazan antes, durante el semántico.

## Stores y temporales

La dirección del destino se evalúa antes del valor y se guarda temporalmente
en pila. Esto conserva el destino si una llamada en el valor cambia las
variables usadas como índices, y libera temporales para expresiones como:

```text
c|i&j| store a|i&j| + b|i&j|.
acumulado store acumulado + a|i&k| * b|k&j|.
```

Los accesos guardan los registros vivos mientras calculan la dirección.
Si una operación binaria necesita un tercer temporal, guarda el resultado
izquierdo en pila, evalúa el derecho con temporales libres y los combina.
No vuelve a evaluar los operandos. El semántico cuenta conservadoramente
estas ranuras de 32 bytes, las llamadas y la aritmética, y conserva la reserva
máxima en `Symbol.call_reserve_size` o `SymbolTable.global_call_reserve_size`.
El prólogo comprueba el frame más esa reserva contra los 4096 bytes de pila.

## Copia y operaciones entre matrices

`destino store origen.` copia las palabras de una matriz a otra cuando sus
tipos y dimensiones son compatibles. No reemplaza el destino por un puntero
al origen. Funciona también con parámetros por referencia y autocopia.

El lenguaje actual no acepta `c store a + b.` ni `c store a * b.` con matrices
completas. La suma y el producto se escriben mediante ciclos y operaciones
escalares sobre sus elementos. `matrix_operations.rwt` prueba:

```text
A = [[1,2,3],[4,5,6]]
B = [[7,8],[9,10],[11,12]]
producto = [[58,64],[139,154]]
suma = producto + producto = [[116,128],[278,308]]
output = 116 + 128 + 278 + 308 = 830
```

La multiplicación usa tres parámetros matriciales. El límite de cuatro
parámetros permanece intacto, por petición del usuario. Por eso
`tests/valid/ejemplo1.rwt`, cuya rutina auxiliar tiene siete parámetros, sigue
siendo rechazado por codegen aunque sus matrices ya estén soportadas.
Las matrices booleanas siguen pendientes.

## Pruebas

Generar con `bin/pipoc -x -o /tmp/nombre.asm archivo.rwt`.
Los casos persistentes son archivos Piper en `tests/codegen/`.

| Archivo | Qué comprueba | Resultado esperado |
| --- | --- | --- |
| `matrix_global.rwt` | Base global mayor que 32767, literal rectangular y primera/última celda | 67331 |
| `matrix_store_edges.rwt` | Expresión compuesta, división, límites B8/uB32 y llamada que cambia el índice | 17 |
| `matrix_copy.rwt` | Copia global/local/por referencia y autocopia | 17 |
| `matrix_operations.rwt` | Producto rectangular y suma por elementos | 830 |
| `matrix_shadow.rwt` | Ámbitos y matrices 1xN, Nx1 | 19 |
| `matrix_bounds_dynamic.rwt` | Lectura con fila igual a la cantidad de filas | `error_indice` |
| `matrix_store_bounds_dynamic.rwt` | Store con columna igual a la cantidad de columnas | `error_indice` |
| `matrix_negative_dynamic.rwt` | Lectura con fila negativa recibida como parámetro | `error_indice` |
| `matrix_invalid_index.rwt` | Índice constante fuera de rango | Error semántico, salida 1 |
| `matrix_invalid_shape.rwt` | Literal de dimensiones incompatibles | Error semántico, salida 1 |
| `matrix_invalid_ragged.rwt` | Filas de distinta longitud | Error semántico, salida 1 |
| `matrix_invalid_copy.rwt` | Copia entre dimensiones distintas | Error semántico, salida 1 |
| `matrix_invalid_operator.rwt` | Suma directa de matrices completas | Error semántico, salida 1 |

La generación con `-x` comprueba el pipeline del compilador hasta ASM. Las
trazas secuenciales temporales con el parser ASM de PORYGON comprueban valores
y direcciones usando el mapa actual del generador; no sustituyen la
calendarización de bundles ni la simulación de latencias del procesador.
