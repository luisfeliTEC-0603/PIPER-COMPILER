# División, residuo y potencia

`src/codegen/codegen_arithmetic.c` genera ASM lineal para `/`, `%` y `^`.
Las expresiones formadas exclusivamente por constantes se resuelven al
compilar. División y residuo con divisor literal potencia de dos usan
desplazamientos y máscaras si hay temporales disponibles; los demás casos
usan los algoritmos generales.

- División: algoritmo restaurador de 32 iteraciones sobre magnitudes sin signo.
  El cociente trunca hacia cero. El residuo conserva el signo del dividendo;
  un residuo cero no conserva signo negativo.
- Potencia: cuadrados sucesivos, con exponente entero no negativo. La base y
  el exponente se evalúan una sola vez. Exponente cero produce 1, incluido
  `0 ^ 0`, igual que la implementación anterior.
- Tipos: se usa el tipo exacto decorado por el semántico (`B8`, `uB8`, `B16`,
  `uB16`, `B32`, `uB32`). `mulh` comprueba el producto completo y se corrige
  su mitad alta firmada para multiplicaciones sin signo. Los tipos estrechos
  también se comprueban. `INT_MIN / -1` se detiene por desborde;
  `INT_MIN % -1` produce cero.
- Literales: `load_immediate` construye cualquier valor entero de Piper de
  32 bits mediante dos mitades, `shli` y `sumi`, sin depender de la extensión de
  inmediatos lógicos.

Los errores de ejecución saltan a bucles de parada `error_division_cero`,
`error_exponente_negativo` o `error_aritmetico`, siguiendo el comportamiento
existente de `error_pila`. No se imprime un mensaje desde el procesador.

Cada operación general usa una ranura de 32 bytes debajo del frame y guarda
solo los registros que pueden contener valores pendientes. Una expresión
directa de `output` no guarda registros que el llamador ya conserva.
Se usa el mismo contador de reservas que las llamadas. `r7` permanece fijo
durante la evaluación de operandos. Los registros guardados se restauran;
el registro destino recibe el resultado. El semántico calcula una reserva
conservadora por rutina y para la entrada global, con un mínimo de 64 bytes.
Incluye llamadas, aritmética, índices, stores indexados y spills de expresiones
binarias. El prólogo comprueba esa reserva junto con el frame. Ya no se limita
la profundidad a dos ranuras; se conserva el presupuesto de 4096 bytes de pila.

Las comparaciones de magnitudes usan `ltn`/`gtn` sin signo, como están
implementadas en `PORYGON/hardware/src/alu_unit.sv`; el signo se extrae con
`sari`. No se cambia la ISA, el mapa de memoria ni el mecanismo de retornos.

Después de `make`, generar los ejemplos desde la terminal:

```sh
bin/pipoc -x -o /tmp/div_general.asm tests/codegen/div_general.rwt
bin/pipoc -x -o /tmp/mod_general.asm tests/codegen/mod_general.rwt
bin/pipoc -x -o /tmp/pow_general.asm tests/codegen/pow_general.rwt
bin/pipoc -x -o /tmp/arithmetic_optimized.asm tests/codegen/arithmetic_optimized.rwt
bin/pipoc -x -o /tmp/global_assignment.asm tests/codegen/global_assignment.rwt
```

Estos casos cubren operandos variables, signos y valores límite de 32 bits.
La generación de ASM no comprueba los resultados durante la ejecución,
no calendariza bundles ni simula las latencias del procesador.

Las sentencias globales se generan antes de la entrada a `main`. Si no existe
un `main` sin parámetros, se ejecutan y se salta a `fin_programa`. Se respeta
la restricción semántica que prohíbe locales en bloques globales.

## Cálculo de constantes durante la compilación

El parser conserva `4 / 2` como un nodo binario del AST con dos hijos literales.
En codegen, `constant_integer()` recorre ese árbol y calcula el resultado
usando operaciones de C. `codegen_general_arithmetic()` puede entonces llamar
a `load_immediate()` con el valor 2, sin emitir el ciclo de división:

```text
create B32 resultado.
resultado store 4 / 2.
```

La asignación genera:

```asm
    mov r5, r0
    sumi r5, #2
    stwi r5, 0(r0)
```

Esto se llama plegado de constantes. El semántico ya validó los tipos y rangos
antes de codegen. El cálculo solo reconoce literales enteros, negación y los
operadores aritméticos admitidos por `constant_integer()`. No sustituye
variables por sus valores, aunque antes se les haya asignado un literal.
Tampoco pliega llamadas ni incrementos/decrementos: sus efectos deben ocurrir
durante la ejecución.

Por ejemplo, `a / b` con parámetros requiere el algoritmo general; `a / 4`
puede usar el camino de desplazamientos. En ambos casos se evalúa `a`. Si el
camino corto necesita un temporal y no hay uno libre, se usa el general.

## Qué cambió en `codegen_regs.c`

`alloc_temp()`, `free_temp()` y `reset_temps()` mantienen la administración de
`r5` y `r6`. La modificación está en `load_immediate()`: antes rechazaba valores
fuera de `-32768..32767`; ahora puede construir cualquier valor entero de Piper
de 32 bits, desde `-2147483648` hasta `4294967295`.

Un registro tiene 32 bits, pero cada inmediato aritmético tiene solo 16 bits
con signo. Por eso `sumi r5, #100000` no es una instrucción válida. Para valores
pequeños se mantienen las secuencias anteriores; para los demás se construye
el patrón de 32 bits con una mitad alta y una mitad baja:

1. Extraer los 16 bits bajos del patrón del valor.
2. Si representan un número entre 32768 y 65535, restar 65536 para que el
   inmediato bajo quede dentro del rango con signo.
3. Calcular la mitad alta compensando ese ajuste: `(bits - low) / 65536`.
   Si no cabe como inmediato positivo, representarla también con signo.
4. Cargar la mitad alta, desplazarla 16 bits y sumar la mitad baja. Omitir
   instrucciones para mitades que sean cero.

Por ejemplo, para `100000`, la mitad baja ajustada es `-31072` y la alta es 2:

```asm
    mov r5, r0
    sumi r5, #2
    shli r5, #16
    sumi r5, #-31072
```

El registro termina con `2 * 65536 - 31072 = 100000`. Todos los inmediatos
emitidos caben en 16 bits con signo.

Para `4294967295`, el patrón es `0xFFFFFFFF`. La alta ajustada es cero y la baja
es -1, por lo que bastan:

```asm
    mov r5, r0
    sumi r5, #-1
```

El mismo patrón de bits representa -1 como `B32` o 4294967295 como `uB32`.
La carga construye bits; el tipo semántico determina cómo interpretarlos.
El procedimiento usa suma y desplazamiento, evitando depender de las
diferencias entre extensión de signo y de ceros de los inmediatos lógicos.
No es una conversión que trunque valores fuera del rango de Piper.

## Cómo se recorre el AST en `codegen.c`

El recorrido no se eliminó. `root->data.program.elements` sigue siendo la
lista de nodos hijos del programa, y `codegen.c` la recorre en varias pasadas
para ordenar correctamente el ASM. La búsqueda de `main` en la tabla de
símbolos es un recorrido adicional: sirve para comprobar que sea una rutina
sin parámetros, y no reemplaza el recorrido del AST.

| Pasada del AST | Qué hace |
| --- | --- |
| Detección | Revisa los nodos de nivel global para saber si hay sentencias ejecutables. No emite ASM. |
| Entrada global | Si hay sentencias ejecutables, procesa todos los nodos salvo las rutinas, en orden de aparición, usando `codegen_statement()`. |
| Rutinas y declaraciones restantes | Emite las rutinas con `codegen_routine()`. Si no hubo entrada global, procesa aquí las declaraciones e imports. |

La condición que excluye `AST_VARIABLE_DECLARATION`, `AST_IMPORT_DECLARATION`
y `AST_ROUTINE_DECLARATION` pertenece a la primera pasada. Solo significa
que esos nodos no activan por sí solos una entrada global ejecutable.

En la segunda pasada se procesan también declaraciones e imports:

- Las declaraciones escalares no necesitan instrucciones de reserva: la tabla
  de símbolos ya les asignó espacio. Las declaraciones de arreglos validan
  que su forma esté soportada.
- Los imports conservan el comportamiento anterior: codegen los ignora y
  puede emitir un comentario. No se implementó carga o enlace de imports.
- Las rutinas se posponen para emitir sus cuerpos después del código de
  entrada y del salto que impide caer accidentalmente dentro de ellas.

Por eso en el `switch` de la tercera pasada aparecen instrucciones como
`if (has_global_statements) break;`: evitan procesar por segunda vez una
declaración o import que ya pasó por `codegen_statement()`. Si no hay código
global ejecutable, esa segunda pasada no ocurre y el `switch` los procesa.

El recorrido de hijos continúa en los módulos correspondientes:
`codegen_statement()` recorre bloques, condiciones y asignaciones;
`codegen_routine()` recorre el cuerpo de una rutina;
`codegen_expression()` recorre los operandos de las expresiones. La función
de `codegen.c` organiza los nodos de nivel global y delega esos recorridos.

Antes, el `switch` global rechazaba una asignación con
`elemento de programa no manejado (kind=6)`. `kind=6` es `AST_ASSIGNMENT`.
Ahora esa asignación se visita en la entrada global. Si hay `main`, se ejecuta
antes del salto a `main`; si no hay, la entrada salta a `fin_programa` después
de ejecutar sus sentencias. Las rutinas anidadas siguen siendo una limitación
pendiente; estas pasadas no implementan su recolección plana.

## Registros y código de apoyo

`expression_depth` distingue una expresión exterior de otra que forma parte
de una expresión mayor. En el caso exterior, los parámetros de una rutina ya
están copiados en su frame, por lo que `r1`–`r4` no necesitan conservarse para
esas lecturas. En expresiones internas o argumentos de llamadas se conservan
por precaución. `r5` y `r6` se guardan si están ocupados, y el registro destino
se excluye porque debe recibir un nuevo valor.

La división sin signo no emite conversión a magnitud ni restauración de signo.
Una magnitud de 32 bits tampoco necesita compararse contra `4294967295` para
comprobar que cabe en `uB32`. Los tipos más estrechos y los resultados con
signo conservan sus comprobaciones necesarias.

El despachador de retornos y los bucles de error todavía se emiten al final,
incluso cuando un programa pequeño no los utiliza. `j fin_programa` evita que
las sentencias globales caigan en ese código de apoyo. Eliminar etiquetas y
ramas no utilizadas es una optimización adicional pendiente.
