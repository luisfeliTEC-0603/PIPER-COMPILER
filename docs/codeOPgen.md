# codeOPgen: de la asignación final a ASM

`codeOPgen` recibe `RegallocAssignedProgram`, escribe ASM y devuelve un diagnóstico.
No consulta el AST, no elige registros, no decide spills y no calendariza.

```text
AST → IR → unrolling opcional → DCE opcional → CFG vigente
    → bajar a máquina → colorear / reescribir spills
    → RegallocAssignedProgram → codeOPgen → ASM → ensamblador → scheduler → bin/mem
```

## Leer el código en este orden

| Archivo | Responsabilidad | Qué mirar |
| --- | --- | --- |
| [codeOPgen.h](../include/codeOPgen.h) | Entrada pública, opciones y diagnóstico localizado. | Propiedad y errores. |
| [codeOPgen.c](../src/codeOPgen/codeOPgen.c) | Único recorrido de rutinas/bloques/instrucciones. | `codeOPgen_generate` y el `switch` de `emit_operation`. |
| [internal.h](../src/codeOPgen/internal.h) | Estado de la emision y funciones auxiliares. | El estado no contiene ámbitos, AST ni asignador de temporales. |
| [emit.c](../src/codeOPgen/emit.c) | Escritura, movimientos, constantes y consulta de registros asignados. | `op_register`, `op_immediate`, primer error. |
| [memory.c](../src/codeOPgen/memory.c) | Símbolos, arreglos, límites, spills y crypto. | Anchos de carga/store y registros fijos. |
| [checked.c](../src/codeOPgen/checked.c) | Conversiones, negación, suma/resta/multiplicación y orden firmado. | Cada secuencia conserva los otros registros asignados. |
| [division.c](../src/codeOPgen/division.c) | División, residuo y potencia por software. | Reserva de trabajo y restauración de r1–r4. |
| [calls.c](../src/codeOPgen/calls.c) | Preparacion de entrada, parametros, llamadas y salida de funciones. | Area de pila de la funcion y datos guardados para cada llamada. |
| [validate.c](../src/codeOPgen/validate.c) | Revisión de firmas y secuencias de llamada antes de escribir. | SAVE/ARG/CALL completos e IDs únicos. |

## Qué contiene la entrada

- `assigned->machine`: operaciones virtuales finales, con las cargas/stores de spill ya insertadas.
- `assigned->routines[r].register_for_temp[t]`: registro físico del temporal `t`.
- `assigned->routines[r].frame_bytes`: tamaño de locales y spills de esa rutina.
- `machine->source`: IR prestada con entrada, símbolos y firmas. Debe permanecer viva.

Ejemplo conceptual:

```text
Piper: output 7.
IR:    return 7

Máquina: IMM t0, 7; TO_RETURN t0; RETURN
Mapa:    t0 → r1
ASM:     mov r1, r0; sumi r1, #7; j piper_salida_funcion_0
```

Una operación virtual puede producir varias instrucciones. `TO_RETURN` no necesita
un `mov` si su valor ya está en r1. La salida comun de la funcion se escribe una sola vez y todos sus retornos saltan a ella.

## Recorrido y etiquetas

1. Revisar registros asignados, conflictos entre valores, espacio de pila, parametros y llamadas.
2. Inicializar r7, guardar cero como numero de retorno y saltar a la inicializacion global o a main.
3. Por funcion: preparar su area de pila, saltar al bloque inicial, emitir los bloques y su salida comun.
4. Por instrucción virtual: resolver operandos con el mapa y llamar a la funcion auxiliar que emite esa operacion.
5. Emitir el codigo que busca donde volver despues de una llamada y los marcadores de terminación/error.

Los bloques tienen saltos explícitos: no se deduce el flujo por su posición física.
Un `BRANCH` se expande a `jnz verdadero` y `j falso`. El scheduler divide esos
saltos en bloques máquina y añade los bundles NOP que exige PORYGON.

| Prefijo ASM | Identidad |
| --- | --- |
| `piper_funcion_0` | Entrada de la funcion de indice 0. |
| `piper_funcion_0_bloque_2` | Bloque de ID 2 dentro de la funcion de indice 0. |
| `piper_salida_funcion_0` | Lugar comun para liberar el espacio de la funcion 0 y regresar. |
| `piper_volver_llamada_3` | Lugar donde continuar despues de regresar de la llamada de ID 3. |
| `piper_division_*`, `piper_potencia_*`, `piper_comparar_*`, `piper_negacion_*` | Pasos internos de calculos que necesitan varias instrucciones. |
| `piper_error_*` | Lugar de parada que identifica un error durante la ejecucion. |

No se usan identificadores del fuente como etiquetas ASM, evitando colisiones.

## Registros y memoria

| Recurso | Regla |
| --- | --- |
| r0 | Cero; nunca recibe resultados. |
| r1–r4 | Registros elegidos por regalloc; codeOPgen no los reasigna. r1 es además retorno. |
| r5/r6 | Registros auxiliares para las secuencias ASM; regalloc no los elige para temporales. |
| r7 | Direccion inicial del area de pila de la funcion que se ejecuta. |
| Locales/parámetros | Desplazamientos en bytes desde r7, con ancho y extensión de signo del tipo. |
| Spills | Palabras de cuatro bytes después de los locales: `base_frame_bytes + 4*slot`. |
| Globales | Dirección absoluta ya resuelta por lowering; stores usan r5 como dirección. |

`op_immediate` construye los 32 bits usando solo destino y r0. Los literales
grandes no necesitan otro registro asignado. Las posiciones reservadas para spills no se agregan como variables a la tabla de simbolos.

## Preparacion de entrada, salida y espacio de pila

**Prologo** es el nombre habitual de las instrucciones que preparan una funcion
al entrar. En el codigo se llama `op_prepare_function`: comprueba que haya espacio
en la pila, lo reserva moviendo r7 y copia los parametros recibidos.

**Epilogo** son las instrucciones que limpian al salir. En el codigo se llama
`op_emit_function_exit`: devuelve r7 a su posicion anterior y recupera el numero
que indica a que llamada debe regresar. El resultado queda en r1.

**Frame** significa el area de pila propia de una ejecucion de una funcion.
Aqui diremos **area de pila de la funcion**. Contiene parametros, variables
locales y posiciones adicionales donde regalloc guarda valores por spilling.

**Frame final** no es otra estructura: era una forma abreviada de decir
**tamano del area de pila despues de agregar los spills**. Por ejemplo:

```text
Parametros y variables locales:   8 bytes
Dos posiciones de spill:      2 × 4 bytes
Total reservado para la funcion: 16 bytes
```

Este total esta en `frame_bytes` de la API compartida y en la variable local
`function_stack_bytes` del emisor. El espacio temporal para futuras llamadas se
comprueba ademas, pero no forma parte de esos 16 bytes.

Ejemplo de entrada/salida: si r7 vale `0x1FFE0` antes de entrar y la funcion
necesita 16 bytes, la preparacion resta 16 y deja r7 en `0x1FFD0`. Al salir,
la limpieza suma esos mismos 16 y recupera `0x1FFE0`. Despues se vuelve a la
instruccion que sigue a la llamada. El valor devuelto permanece en r1.

Los primeros cuatro parametros llegan en r1–r4. Los restantes se leen de la
pila usando desplazamientos que incluyen todo el espacio de la funcion.

```text
Datos guardados para cada llamada (32 bytes):
  0      Numero que indica donde volver
  4..24  r1..r6 del llamador
  28     valor devuelto
  32..   argumentos quinto y posteriores
```

- `CALL_SAVE`: guarda r1–r6 debajo del area de pila de quien hace la llamada.
- `CALL_ARG`: coloca valores ya evaluados. Primero los argumentos de pila, después los de registros.
- `CALL_RESULT/VOID`: escribe el ID con r5, baja r7 y salta al llamado.
- Al volver: guarda r1 en la ranura de retorno, restaura registros y carga el resultado en su registro asignado.
- La salida de la funcion libera su area de pila, lee el numero de retorno en r6 y salta al codigo que busca donde volver.

El grafo protege los registros de argumentos entre su colocación y CALL:
una recarga posterior no puede pisarlos. También hace interferir cualquier
definición, aunque su resultado no se use, con los valores vivos después.
Estas restricciones se comprueban antes de entregar la asignación al emisor.

## Aritmética y fallos

- Orden: `ltn/gtn` son unsigned en la ISA. La funcion auxiliar adapta operandos firmados y restaura el operando derecho.
- Suma/resta: comprueba acarreo/préstamo unsigned o el bit de overflow firmado; luego verifica ancho B8/B16.
- Multiplicación: comprueba la mitad alta; para unsigned corrige el resultado firmado de `mulh`.
- Conversión/negación: comprueba rango antes de publicar el resultado; conserva el operando fuente.
- DIV/MOD: división restauradora de 32 pasos; el residuo tiene el signo del dividendo y cero no conserva signo negativo.
- POW: exponenciación binaria con productos comprobados; evita el último cuadrado que ya no se utilizará.

DIV/MOD/POW conservan r1–r4 en 32 bytes bajo r7 y restauran todos salvo el
resultado asignado. No sobrescriben variables ni spills, y no cambian r7.

Los fallos saltan a `piper_error_pila`, `piper_error_division_cero`,
`piper_error_exponente_negativo`, `piper_error_aritmetico` o `piper_error_indice`.
Son paradas de software: para verificarlas se compara `halt_pc` con la etiqueta;
el contador de errores de acceso del hardware puede seguir en cero.

## CLI y comprobación

Para estudiar dos programas pequenos, seguir primero
[las comprobaciones paso a paso](codeOPgen-pruebas-simples.md): funcion con
suma/multiplicacion y loop con acceso a matriz. Incluyen fuentes, IR inicial
y optimizada completas, comandos y resultados reales.

```bash
make all
bin/pipoc all programa.rwt --unroll 2 --dce --spill on -o build/programa.bin
bin/pipoc mem programa.rwt --spill off -o build/programa.mem
make check-codeOPgen
make check-codeOPgen-sim
```

`--spill off` permite las otras optimizaciones, pero rechaza la compilación si el
coloreo requiere derrames y pide habilitar «Permitir spilling». `-v` muestra las
decisiones de unroll, DCE y el reporte de asignación. El [IDE](IDE.md) permite
seleccionar las opciones junto a Compilar y muestra esos resultados en su panel.

Las pruebas usan C y Bash. La primera orden de pruebas valida la API, determinismo,
errores IO y emisión/ensamblado/calendarización. La segunda además ejecuta PORYGON
en programas con/sin optimizaciones, ocho combinaciones de opciones y paradas de
software. Resultados y logs en `build/runs/codeOPgen/`. Requiere el repositorio
hermano PORYGON con `iverilog` y `vvp`; no modifica RTL.

En concreto, `make all` solo construye las herramientas. `bin/pipoc ...` compila
un programa elegido. `make check-codeOPgen` comprueba el contrato del emisor y
compila una bateria de programas con ensamblado y scheduler; no ejecuta el
procesador. `make check-codeOPgen-sim` agrega su ejecucion en PORYGON y compara
resultados o lugares de parada por error. Es una regresion amplia, no la guia
paso a paso para aprender a seguir una compilacion.

## Por que se modificaron regalloc y DCE

En regalloc hubo correcciones necesarias para que el ASM real respetara los
valores calculados:

1. Primero se colocan los argumentos que viajan por pila y despues los que
   viajan en registros. Asi no queda una recarga pendiente cuando los cuatro
   registros ya contienen argumentos.
2. El grafo impide asignar a una recarga un registro que ya contiene un
   argumento pendiente de usar por CALL. Antes, la llamada de seis argumentos
   podia devolver 66 en lugar de 11 porque la ultima recarga pisaba el primero.
3. Aunque nadie lea el resultado de una instruccion, esta escribe un registro.
   Ese registro no puede contener otro valor que todavia se necesita.

### Ejemplo pequeno de sobrescritura de argumentos

```text
routine B32 primero[B32 a,B32 b,B32 c,B32 d,B32 e,B32 f] { output a. }
routine B32 main[] { output primero[11,22,33,44,55,66]. }
```

Debe devolver **11**. No necesita ser un programa grande. Este orden de acciones
ejemplifica el fallo anterior; no es el ASM que ahora se genera:

| Paso | Estado de r1 | Que sucede |
| --- | --- | --- |
| Preparar el primer argumento | 11 | r1 debe conservar 11 hasta entrar a `primero`. |
| Preparar otros argumentos | 11 | Los demas valores se colocan en registros o pila. |
| Recargar el sexto argumento desde un spill usando r1 | 66 | Se pierde el primer argumento. |
| Guardar 66 como sexto argumento en pila | 66 | El primer argumento continua incorrecto. |
| Entrar a `primero` | 66 | La funcion lee r1 como `a` y devuelve 66. |

El temporal que representaba 11 ya habia sido consumido por `CALL_ARG`. El
analisis anterior olvidaba que **el registro fisico r1 seguia ocupado por el
argumento**, aunque aquel temporal ya no tuviera usos pendientes.

La correccion conserva esa ocupacion hasta CALL y coloca primero los argumentos
de pila. No se permite reutilizar r1 para una recarga que lo pise.

No ocurre si no hay recargas en ese intervalo, si se hacen antes de colocar el
primer argumento o si usan otro registro libre. Una funcion sencilla de dos
argumentos puede no necesitar ningun spill. Un programa largo tambien puede
tener cero spills si mantiene pocos valores necesarios simultaneamente. El
problema depende de la coincidencia de valores y registros, no de las lineas.

DCE no cambio sus reglas de eliminacion durante esta sustitucion. Se cambio
el texto `target_metrics_status` del JSON y su comentario: ya no es cierto que
falte un backend que consuma IR optimizada. Lo pendiente ahora es **recoger
las metricas del codigo generado**. Por eso bytes y ciclos siguen en `null`;
no se inventan mediciones. El nuevo texto es `requires_target_metrics_collection`.

