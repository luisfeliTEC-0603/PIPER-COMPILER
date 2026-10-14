# Entender el scheduler PORYGON, paso a paso

Esta guía está pensada para leer con el código abierto al lado. Vamos a tomar
un programa pequeño, seguir las decisiones del scheduler y llegar hasta sus
bundles y sus bits. Después veremos por qué los arreglos, las máscaras y las
funciones de C tienen la forma que tienen.

No hace falta conocer el frontend de Piper para seguir el recorrido. Nuestro
punto de partida es un programa PORYGON que el parser ya convirtió en estructuras.

## Índice

1. [Qué problema resolvemos](#1-qué-problema-resolvemos)
2. [Dónde mirar en el código](#2-dónde-mirar-en-el-código)
3. [Nuestro programa de ejemplo](#3-nuestro-programa-de-ejemplo)
4. [Cómo decidimos si una instrucción está lista](#4-cómo-decidimos-si-una-instrucción-está-lista)
5. [Construir el calendario a mano](#5-construir-el-calendario-a-mano)
6. [Tres detalles que explican las esperas](#6-tres-detalles-que-explican-las-esperas)
7. [Bloques, saltos y etiquetas](#7-bloques-saltos-y-etiquetas)
8. [Validar antes y verificar después](#8-validar-antes-y-verificar-después)
9. [Del calendario al archivo](#9-del-calendario-al-archivo)
10. [Por qué está escrito así en C](#10-por-qué-está-escrito-así-en-c)
11. [Cómo recorrerlo y usarlo por tu cuenta](#11-cómo-recorrerlo-y-usarlo-por-tu-cuenta)

## 1. Qué problema resolvemos

En el ASM lineal las instrucciones aparecen una detrás de otra. En PORYGON
el procesador recibe **bundles de cuatro slots**, y las operaciones de un mismo
bundle se emiten juntas. No podemos agrupar cuatro instrucciones cualesquiera:
algunas necesitan resultados anteriores, otras compiten por la misma unidad y
otras usan dos slots porque llevan un inmediato.

El trabajo del scheduler es decidir **en qué ciclo, en qué slot y en qué unidad**
va cada operación, conservando el significado del programa original.

```text
ASM lineal
    ↓ parser de PORYGON
MachineProgram: instrucciones + etiquetas + bloques
    ↓ validación de entrada
    ↓ asignación de ciclos, slots y unidades
    ↓ resolución final de saltos
    ↓ verificación del calendario
Schedule: bundles + posiciones finales de etiquetas
    ├── archivo legible .bundles
    └── imagen hexadecimal .mem
```

Conviene separar cinco palabras desde el inicio:

| Palabra | Qué significa aquí |
|---|---|
| Instrucción | Una operación lógica del programa original, como `sumi r1, #1`. |
| Slot | Una posición de 16 bits. Puede contener una operación, un literal o un NOP. |
| Bundle | Cuatro slots que ocupan una posición de emisión. |
| `issue` | Ciclo en que una operación captura sus operandos y se emite. |
| WB / RF | Escritura del resultado / banco de registros donde queda disponible. |

**El número de slot no determina la unidad.** Una MATH puede estar en slot 0,
1, 2 o 3. El formato y, para MATH, el bit `pipe` determinan adónde se despacha.
Un literal ocupa un slot pero no utiliza una unidad funcional.

Hay otra distinción importante: estos ciclos son posiciones de emisión del
calendario. No son el número de ticks desde reset. Cuando un bucle vuelve a un
bundle, vuelve a ejecutar esa posición en un instante posterior.

## 2. Dónde mirar en el código

No es necesario leer todos los archivos en orden alfabético. Esta es la ruta
más cómoda para entender la parte central:

| Archivo | Qué buscar al abrirlo |
|---|---|
| [scheduler.h](../scheduler/scheduler.h) | Las estructuras de salida y la API pública. |
| [scheduler.c](../scheduler/scheduler.c) | `porygon_schedule_program`, luego `schedule_block`, `dependencies_ready` y `choose_unit`. |
| [instruction.c](../scheduler/instruction.c) | Cómo se obtienen latencias, codificaciones y validaciones de entrada. |
| [verify.c](../scheduler/verify.c) | Cómo se revisa el resultado sin reutilizar las decisiones del planificador. |
| [output.c](../scheduler/output.c) | Cómo se convierte el calendario en texto y en palabras de 64 bits. |
| [internal.h](../scheduler/internal.h) | Declaraciones compartidas entre esos archivos, no una segunda API de usuario. |
| [schedule_main.c](../tools/schedule_main.c) | El recorrido completo desde los argumentos de consola hasta los archivos. |

También usamos piezas anteriores del ensamblador:

- [machine_program.h](../porygon_assembler/machine_program.h) define la entrada.
- [machine_program.c](../porygon_assembler/machine_program.c) calcula lecturas,
  escrituras, efectos, recurso permitido y tamaño de cada instrucción.
- [asm_program_parser.c](../porygon_assembler/asm_program_parser.c) reconoce las
  etiquetas y construye bloques básicos.
- [encoder.c](../porygon_assembler/encoder.c) coloca los campos en sus bits.
- [jump_fixup.c](../porygon_assembler/jump_fixup.c) calcula los offsets finales.
- [mnemonics.c](../porygon_assembler/mnemonics.c) comparte los nombres de las
  instrucciones entre parser y escritor.

## 3. Nuestro programa de ejemplo

Vamos a usar estas ocho instrucciones. Los valores iniciales de los registros
son datos de entrada; para ejecutar el store, el programa que nos llama debe
proporcionar en `r6` una dirección de datos válida y alineada.

```asm
inicio: sumi r1, #1
sum r3, r1
sumi r2, #2
mul r4, r2
shl r5, r6
sum r7, r4
j fin
fin: stw r7, 0(r6)
```

Recordá que las operaciones MATH suelen tener **dos direcciones**: el primer
registro es fuente y destino. `sum r3, r1` significa `r3 = r3 + r1`, no una suma
con un tercer destino implícito. `sumi r1, #1` incrementa el valor previo de `r1`;
no carga la constante uno. `mov` es la excepción: lee el segundo registro y
sobrescribe el primero.

### Lo que entrega el parser

El scheduler no vuelve a separar cadenas como `"r3"` ni a buscar comas. Recibe
un arreglo de instrucciones reconocidas. Para hablar de ellas les asignaremos
los nombres `I0` a `I7`; son índices de origen, no números de bundle.

| Índice | Operación | Registros leídos (`uses`) | Escritos (`defs`) | Slots | Unidad permitida |
|---|---|---|---|---:|---|
| I0 | `sumi r1, #1` | r1 | r1 | 2 | ALU0 o ALU1 |
| I1 | `sum r3, r1` | r3, r1 | r3 | 1 | ALU0 o ALU1 |
| I2 | `sumi r2, #2` | r2 | r2 | 2 | ALU0 o ALU1 |
| I3 | `mul r4, r2` | r4, r2 | r4 | 1 | ALU1 |
| I4 | `shl r5, r6` | r5, r6 | r5 | 1 | ALU1 |
| I5 | `sum r7, r4` | r7, r4 | r7 | 1 | ALU0 o ALU1 |
| I6 | `j fin` | ninguno | ninguno | 2 | BRUH |
| I7 | `stw r7, 0(r6)` | r7, r6 | ninguno | 1 | LSU |

Un salto directo con etiqueta se representa desde el parser en su forma larga:
consume una instrucción y un literal. Eso fija su tamaño antes de conocer el
desplazamiento definitivo.

El parser también identifica estos bloques:

| Bloque | Instrucciones | Motivo del límite |
|---|---|---|
| 0 | I0 a I6 | Comienza en `inicio` y termina con `j fin`. |
| 1 | I7 | Comienza en la etiqueta `fin`. |

Todavía `fin` apunta a **I7**, no a un bundle. El scheduler podrá insertar esperas
y agrupar operaciones, así que esa posición física aún no existe.

### Leer las máscaras sin miedo

`uses` y `defs` son enteros de ocho bits. El bit `n` representa a `rn`:

```text
r1                  → 00000010
r3                  → 00001000
r1 y r3             → 00001010
I0.defs & I1.uses    → 00000010 & 00001010 = 00000010
```

La intersección no es cero: I1 lee algo que I0 escribe. Con ocho registros, una
máscara permite responder eso directamente, sin listas de nombres ni búsquedas
por texto. `define_register` omite `r0` de `defs` porque sus escrituras se descartan.

## 4. Cómo decidimos si una instrucción está lista

Abrí `schedule_block` en [scheduler.c](../scheduler/scheduler.c). La idea del
algoritmo, simplificada, es esta:

```text
mientras queden instrucciones pendientes del bloque:
    añadir un bundle lleno de NOPs
    recorrer las instrucciones en su orden original:
        si ya fue emitida: continuar
        si no caben sus slots: continuar
        si alguna dependencia exige esperar: continuar
        si no hay unidad/canal disponible: continuar
        codificar y colocar la instrucción y su posible literal
        registrar su ciclo y las reservas que acaba de ocupar
    si se emitió un salto:
        añadir dos bundles completamente NOP
```

Es una **planificación de lista estable**: la prioridad es el orden original.
El detalle que permite reordenar es el `continue`. Si I1 está esperando un
resultado, no detenemos el recorrido: revisamos si I2 u otra posterior pueden
aprovechar el espacio.

No usamos prioridades de camino crítico, una cola sofisticada ni búsqueda del
calendario óptimo. Se elige la primera opción legal durante el recorrido.

### ¿Dónde está el grafo de dependencias?

Podemos dibujar conceptualmente algunas dependencias RAW del ejemplo:

```text
I0 ──r1──→ I1
I2 ──r2──→ I3 ──r4──→ I5 ──r7──→ I7
I4 no depende de esas cadenas.
```

**Ese dibujo no se almacena como un grafo en C.** `dependencies_ready` calcula
las relaciones al comparar la candidata con todas las instrucciones anteriores
por índice original. Las máscaras representan los registros involucrados y
`issued` registra cuándo se emitió cada operación.

El parser sí construye información de flujo de control entre bloques. Eso es
otra cosa: un grafo de flujo responde qué bloque puede ejecutarse después de
otro; las dependencias responden qué lecturas, escrituras o efectos deben
respetarse. Esta versión usa los límites de los bloques y un tratamiento
conservador de los saltos, sin hacer una optimización global sobre ese grafo.

### Los tres arreglos que conviene tener a la vista

Dentro de `scheduler_t` vas a encontrar:

| Estado | Índice | Significado |
|---|---|---|
| `issued[i]` | Instrucción original | Ciclo de emisión, o `SIZE_MAX` si sigue pendiente. |
| `alu1_wb[t]` | Ciclo de WB | Hay un resultado de ALU1 que ya reservó ese ciclo. |
| `output.bundles[b]` | Bundle físico | Los cuatro slots que estamos construyendo. |

Además, `used[5]` es local a cada vuelta de `schedule_block`: recuerda las
unidades ocupadas **en ese bundle**. Se reinicia por ciclo; `issued` y `alu1_wb`
se conservan durante todo el programa.

`remaining` cuenta instrucciones lógicas pendientes, no slots. Un `sumi` reduce
`remaining` en uno aunque ocupe dos posiciones físicas.

## 5. Construir el calendario a mano

Antes de empezar, esta es la regla temporal que vamos a usar:

| Productor | WB en RF | Primera lectura dependiente segura |
|---|---:|---:|
| ALU rápida, load o FEISTEL | issue + 2 | issue + 3 |
| MUL/MULH | issue + 4 | issue + 5 |

Escribir y leer el nuevo valor en el mismo flanco no está permitido. Por eso
aparece ese ciclo adicional entre WB y la primera lectura segura. No hay forwarding.

### Ciclo 0: saltamos una candidata bloqueada

I0 está lista. Es MATH básica y ALU0 está libre, así que va a slot 0 con
`pipe=0`. Su literal ocupa slot 1. Registramos `issued[0] = 0`.

I1 lee `r1`, que I0 acaba de producir. Su primera emisión segura será 3.
Aunque quedan slots, I1 todavía no puede usarlos.

Seguimos con I2. Es independiente de I0 e I1. ALU0 ya está ocupada, pero ALU1
puede ejecutar una suma rápida. I2 va a slot 2 con `pipe=1` y su literal a slot 3.
Ahora `issued[2] = 0` y `alu1_wb[2] = true`.

```text
BUNDLE 0
    SLOT 0 MATH pipe=0 sumi r1, #1
    SLOT 1 LITERAL 0x0001
    SLOT 2 MATH pipe=1 sumi r2, #2
    SLOT 3 LITERAL 0x0002
END_BUNDLE
```

Esto ya muestra reordenamiento: I2 salió antes que I1, conservando las dependencias.

### Ciclo 1: aprovechamos una instrucción más lejana

I1 sigue esperando a I0. I3 espera el `r2` producido por I2. I4, en cambio, solo
lee `r5` y `r6`; no necesita ninguna de esas escrituras y puede usar ALU1.

Emitimos I4 en slot 0. Un shift requiere `pipe=1`, pero sigue siendo una operación
rápida: su WB será en 3. Que use la misma ALU que MUL no le da latencia de MUL.

I5 no puede salir porque I3 ni siquiera se ha emitido. I6 tampoco puede abandonar
un bloque que todavía tiene operaciones pendientes. Los otros tres slots quedan NOP.

### Ciclo 2: un bundle vacío también hace trabajo temporal

No hay ninguna candidata lista. El bundle se queda completamente NOP y avanzamos
al siguiente ciclo. `remaining` no cambia; el tiempo sí.

I0 e I2 escriben RF en este ciclo. Sus consumidores todavía deben esperar al 3.

### Ciclo 3: dos cadenas avanzan juntas

Ahora I1 puede leer el nuevo `r1`: la emitimos en ALU0, slot 0.
I3 también puede leer el nuevo `r2`: la emitimos en ALU1, slot 1.

```text
issued[1] = 3        → WB de I1 en 5
issued[3] = 3        → WB de I3 en 7
alu1_wb[7] = true
```

I5 depende de I3, así que tendrá que esperar hasta 8.

### Ciclos 4 a 7: esperar al producto

Solo quedan I5 y el salto en este bloque. I5 necesita el producto y el salto
no puede salir antes de ella. Se emiten cuatro bundles NOP.

En particular, el ciclo 7 **todavía no sirve** para I5: es el flanco de escritura
del producto. La comparación `cycle <= old_issue + latency` mantiene el RAW bloqueado.

### Ciclo 8: una operación puede compartir bundle con el salto

I5 ya puede leer `r4`. La emitimos en ALU0, slot 0.

Después examinamos I6. Todas las operaciones anteriores ya se emitieron y el
salto no lee `r7`. Además, el WB de I5 será en 10, y el destino del salto no
comenzará antes de 11. Por eso I6 puede compartir el bundle con I5.

El salto usa slot 1 y reserva slot 2 para su literal. Slot 3 queda NOP.

### Ciclos 9 y 10: esperas de control obligatorias

Se agregan dos bundles completamente NOP. No se intentan llenar con instrucciones
del siguiente bloque. Esta regla vale para saltos tomados y no tomados.

### Ciclo 11: llegamos a `fin`

Comenzamos el bloque siguiente. `label_bundles[fin]` queda en 11 y el store I7
puede leer `r7`: su productor I5 escribió en 10. El store usa LSU, slot 0.

La salida completa queda resumida así:

| Bundle | Slot 0 | Slot 1 | Slot 2 | Slot 3 |
|---:|---|---|---|---|
| 0 | I0, ALU0 | literal 1 | I2, ALU1 | literal 2 |
| 1 | I4, ALU1 | NOP | NOP | NOP |
| 2 | NOP | NOP | NOP | NOP |
| 3 | I1, ALU0 | I3, ALU1 | NOP | NOP |
| 4–7 | NOP | NOP | NOP | NOP |
| 8 | I5, ALU0 | I6, salto | literal 3 | NOP |
| 9–10 | NOP | NOP | NOP | NOP |
| 11 | I7, LSU | NOP | NOP | NOP |

El literal 3 del salto se calcula al final: `11 - 8 = 3` bundles.
La salida empieza con `LABEL inicio 0` y `LABEL fin 11`.

## 6. Tres detalles que explican las esperas

### RAW, WAR y WAW hacen preguntas distintas

En `dependencies_ready`, `old` es una instrucción anterior en el **texto original**,
y `current` es la candidata. No significa necesariamente que ya se emitieron
en ese mismo orden.

| Relación | Máscaras | Condición necesaria |
|---|---|---|
| RAW: el lector necesita un resultado | `old.defs & current.uses` | `current_issue > old_WB` |
| WAR: el lector necesita el valor anterior | `old.uses & current.defs` | `current_issue >= old_issue` |
| WAW: dos escrituras del mismo registro | `old.defs & current.defs` | `current_WB > old_WB` |

Si la instrucción anterior necesaria sigue con `issued == SIZE_MAX`, primero
hay que emitirla. Tener slots o una unidad libre no resuelve una dependencia.

Probá seguir mentalmente este WAR:

```asm
mov r3, r1
mov r1, r2
```

Ambas operaciones pueden compartir bundle en ALUs distintas. La primera captura
el valor viejo de `r1` al emitir; la segunda escribe su resultado después. No
necesitamos NOPs adicionales para proteger una lectura ya capturada.

Ahora mirá este WAW:

```asm
mul r1, r2
mov r1, r3
```

Si MUL sale en 0, escribe en 4. El MOV no puede escribir antes ni en 4; como
su latencia es dos, puede emitirse a partir de 3 y escribir en 5. Comparar solo
el orden de emisión sería insuficiente.

La implementación compara con **todos los anteriores relevantes**, no mantiene
un mapa sofisticado de versiones de registros. Esa elección simplifica el código,
aunque puede conservar restricciones que un análisis más preciso descartaría.

### Estar libre para emitir no significa estar libre para terminar

Este ejemplo explica `alu1_wb`:

```asm
mul r1, r7
shl r2, r7
shl r3, r7
```

| Ciclo | Decisión |
|---:|---|
| 0 | MUL emite y reserva WB de ALU1 en 4. |
| 1 | Primer shift emite y reserva WB en 3. |
| 2 | Segundo shift no puede emitir: también necesitaría WB en 4. |
| 3 | Segundo shift puede emitir y reservar WB en 5. |

Los tres destinos son distintos. El problema no es un registro compartido:
es el **canal físico de resultado**. Consultar `used[1]` solamente no detectaría
ese conflicto, porque ALU1 está libre para emitir en el ciclo 2.

MUL consecutivos sí son legales: uno emitido en 1 reserva WB en 5, no en 4.
Por eso ALU1 se modela como una unidad con reservas futuras, no como una unidad
bloqueada durante toda la duración de una multiplicación.

`choose_unit` consulta esas condiciones. La reserva se escribe en `schedule_block`
solo después de colocar la operación. No queremos reservar recursos por candidatas
que finalmente no se emitieron.

### Memoria tiene orden aunque los registros no coincidan

Dos accesos pueden apuntar a la misma dirección aun si usan registros base
distintos. Como todavía no hacemos análisis de alias, mantenemos el orden
original de todos los loads y stores. LSU además admite una sola operación por bundle.

Un store lee **el dato y la base**. Un load lee la base y escribe el destino.
Eso se modela en `porygon_describe_instruction`, antes de hablar de ciclos.
Los efectos de estado también conservan su orden; no se tratan como cálculos puros.

## 7. Bloques, saltos y etiquetas

`porygon_schedule_program` recorre los bloques en el orden en que vienen del
parser. Puede reordenar operaciones dentro de cada uno, pero no trasladarlas a
otro bloque. Así evita cambiar los caminos en los que se ejecuta un store u
otro efecto observable.

Los índices de instrucción nunca cambian: reordenar significa elegir diferentes
ciclos en `issued`, no mover elementos del arreglo original.

### Por qué el salto puede esperar antes de sus dos NOPs

Considerá `mul r1, r2`, seguido de un salto a un bloque que podría leer `r1`.
Si ambos salieran en 0, el destino empezaría en 3, pero el producto no sería
legible hasta 5.

Esta versión exige que todo resultado GPR anterior esté listo al entrar al
sucesor. En ese ejemplo puede emitir MUL en 0, esperar en 1, saltar en 2,
colocar sus NOPs en 3 y 4 y llegar al destino en 5.

La condición en C se deriva de esta cuenta:

```text
primera emisión del sucesor = ciclo_del_salto + 2 NOPs + 1
necesitamos                 = WB_anterior < primera_emisión_del_sucesor
equivalente                 = WB_anterior <= ciclo_del_salto + 2
```

Es una solución conservadora para saltos hacia delante, hacia atrás y `jr`.
No analiza qué registros realmente leerá cada sucesor; por eso puede esperar
por un resultado que ese camino no usará. Esa es una oportunidad de optimización
futura, no una suposición oculta de esta versión.

### Una etiqueta puede apuntar a una espera

Supongamos que una etiqueta empieza justo después de una multiplicación, sin
salto entre ambas, y su primera operación consume el producto. El bloque nuevo
puede comenzar con bundles de espera.

La etiqueta apunta al **comienzo del bloque incluyendo esas esperas**, no al
primer slot con una operación. `issued` y las reservas no se reinician al entrar
en otro bloque, de modo que también se protegen los caminos secuenciales.

### Resolver etiquetas al final evita perseguir números cambiantes

`resolve_jumps` corre después de calendarizar todos los bloques. En nuestro
caso ya sabemos que el salto está en 8 y `fin` está en 11:

```text
offset = bundle_destino - bundle_origen = 11 - 8 = 3
```

`porygon_resolve_long_jump` entrega el slot del salto y el literal final. No hace
falta multiplicar el literal por ocho: el hardware interpreta el desplazamiento
en bundles. La dirección en bytes sí es ocho veces el índice del bundle.

Un `jr` funciona distinto: usa una dirección final en bytes que está en un
registro. El scheduler no reubica constantes cargadas como direcciones de código;
esa correspondencia debe garantizarla quien genera el programa.

## 8. Validar antes y verificar después

Son dos preguntas diferentes:

- **Validación de entrada:** ¿las estructuras e instrucciones tienen sentido?
- **Verificación de salida:** ¿la distribución elegida conserva el programa y
  cumple las reglas temporales y físicas?

### Antes: `instruction.c`

`schedule_validate_program` comprueba operaciones, operandos y rangos; luego
los bloques y las etiquetas. Para saber si una instrucción tiene una forma
válida intenta codificarla. En MATH basta que alguna de las dos ALUs sea legal;
la elección final ocurre durante la planificación.

También hace una propagación sencilla de constantes dentro del bloque. Por ejemplo:

```asm
mov r1, r0
sumi r1, #1
shli r1, #16
low r2, 0(r1)
```

Puede demostrar `r1 = 0`, luego `1`, luego `0x10000`. El acceso de palabra está
alineado y dentro de datos. Si el desplazamiento del load fuera 1, demostraría
una desalineación y rechazaría el programa.

`known[8]` indica qué registros tienen un valor demostrado; `value[8]` guarda
ese valor. Al iniciar cada bloque solo `r0` es conocido. Un load invalida el
conocimiento anterior de su destino porque no sabemos qué dato traerá.

Esto **no ejecuta el programa completo** ni reemplaza instrucciones. Si una base
es dinámica, no se inventa un valor ni se asegura que sea válida. Se comprueba
lo demostrable con el bloque disponible.

`schedule_encode` produce dos valores separados: `word` y `literal`. El caller
solo usa el segundo como slot cuando `immediate` lo requiere. Se validan los
rangos antes de convertir a 16 bits para no esconder un desbordamiento mediante
truncamiento.

### Después: `verify.c`

`porygon_verify_schedule` vuelve a recorrer los bundles reales. No acepta el
calendario solo porque lo construyó nuestro propio código.

1. Comprueba cada slot contra su instrucción de origen y su `pipe`.
2. Si hay inmediato, verifica el literal adyacente y lo salta como dato.
3. Reconstruye en un arreglo nuevo el ciclo de cada instrucción.
4. Comprueba unidades por bundle y reservas de canales/registros por flanco WB.
5. Detecta instrucciones repetidas o perdidas.
6. Contrasta RAW, WAR, WAW, orden de efectos, límites de bloques y saltos.

El arreglo `writebacks` separa una máscara de **canales** de una de **registros**.
Un resultado puede chocar con otro por usar el mismo puerto aunque los destinos
sean distintos. También dos puertos diferentes pueden intentar escribir el
mismo registro. FEISTEL aporta dos canales, y sus destinos iguales distintos
de `r0` se rechazan desde la validación de operandos.

El verificador es independiente del estado de planificación: no reutiliza su
`issued`, su `alu1_wb` ni llama a `dependencies_ready`. Sí comparte las latencias
y los codificadores. Por tanto, es una comprobación independiente del calendario,
no una segunda implementación completa de la ISA.

## 9. Del calendario al archivo

Una `porygon_schedule_t` contiene bundles y posiciones de etiquetas. Cada slot
guarda `kind`, `instruction_index`, `pipe` y `word`.

| Campo | Para qué hace falta |
|---|---|
| `kind` | Distinguir operación, literal y NOP, incluso si tienen los mismos bits. |
| `instruction_index` | Recuperar los operandos originales y atribuir errores. En un literal identifica a su dueño. |
| `pipe` | Conservar la ALU elegida para MATH. No es un operando del ASM de entrada. |
| `word` | Guardar los 16 bits ya codificados. |

Un literal `0x0000` es dato; no se transforma en NOP. Tampoco se intenta decodificar
si sus bits parecen un opcode. Esa es la razón de guardar `kind` además de `word`.

### Contrato del archivo `.bundles`

El archivo empieza con las etiquetas y después los bundles numerados desde cero:

```text
LABEL inicio 0
LABEL fin 11
BUNDLE 0
    SLOT 0 MATH pipe=0 sumi r1, #1
    SLOT 1 LITERAL 0x0001
    SLOT 2 MATH pipe=1 sumi r2, #2
    SLOT 3 LITERAL 0x0002
END_BUNDLE
```

Cada bundle tiene exactamente cuatro líneas `SLOT` en orden 0 a 3. Su contenido
es `NOP`, `LITERAL 0xHHHH` o una instrucción precedida por su formato. Solo MATH
lleva `pipe`. Los bundles aparecen consecutivamente y las etiquetas nombran sus
índices finales.

El inmediato se ve dos veces a propósito: como operando legible en `sumi` y como
patrón de bits en el slot literal. **Ya ocupa ese segundo slot**; un futuro
consumidor no debe añadir otro. Para saltos se conserva la etiqueta en el texto
y se muestra el desplazamiento resuelto en el literal adyacente.

`write_instruction` consulta la tabla de mnemónicos existente, en lugar de
mantener otra lista de nombres paralela al parser.

### Cómo se forma una línea `.mem`

Para el bundle 0 del ejemplo tenemos:

```text
slot 0 = 0x2024     sumi r1, #1, pipe 0
slot 1 = 0x0001     literal
slot 2 = 0x402C     sumi r2, #2, pipe 1
slot 3 = 0x0002     literal
```

`porygon_write_mem` los reúne así:

```c
word |= (uint64_t)slot_word << (16 * slot_index);
```

Por eso el token hexadecimal se lee de slot 3 a slot 0:

```text
0002 402C 0001 2024  →  0002402C00012024
```

El archivo real lleva un token de 16 dígitos por línea, sin espacios ni `0x`.
El cast a `uint64_t` ocurre **antes** de desplazar: para slot 3 necesitamos
mover los bits 48 posiciones.

Ambos escritores verifican el calendario antes de emitir. El escritor de `.mem`
usa directamente los slots en memoria; no vuelve a leer `.bundles`. Actualmente
existe el escritor de ese formato textual, pero todavía no su lector.

## 10. Por qué está escrito así en C

### Índices en lugar de punteros entre instrucciones

`instruction_index` y los IDs de etiquetas permanecen estables. Además, `realloc`
puede mover el arreglo de bundles: guardar índices evita punteros persistentes
hacia posiciones que podrían cambiar de dirección.

Los punteros locales a un slot se usan después de reservar el bundle, mientras
no ocurre otro crecimiento del arreglo. No se guardan como enlaces permanentes.

### Arreglos pequeños para recursos pequeños

Tenemos ocho GPR, cinco unidades y cuatro slots. Máscaras y arreglos directos
expresan esas cantidades sin contenedores genéricos, tablas hash ni objetos por
cada dependencia. El calendario de ALU1 está indexado por ciclo, así que consultar
una reserva es leer un `bool`.

La estrategia de dependencias ahorra construir y mantener aristas, pero repite
comparaciones. El verificador revisa `N(N-1)/2` pares de instrucciones; el scheduler
puede consultar los mismos pares en varios ciclos. No es una implementación
pensada para optimizar tiempo de compilación en programas enormes. Prioriza que
se pueda seguir a mano y revisar con facilidad.

### `SIZE_MAX` no es un ciclo enorme que vamos a ejecutar

Es un centinela: significa «todavía no emitida» o «ubicación no aplicable», según
el campo. No usamos cero porque bundle 0 e instrucción 0 son posiciones válidas.

Antes de sumar latencias, `dependencies_ready` comprueba ese centinela. El
verificador también detecta instrucciones ausentes antes de comparar sus tiempos.

### `calloc`, `{0}` y crecimiento por capacidad

`(porygon_bundle_t){0}` deja sus slots como NOP de relleno y sus palabras en cero.
Eso permite construir un bundle escribiendo solamente las posiciones ocupadas.

`alu1_wb` se reserva con `calloc` porque todas las reservas deben comenzar libres.
Tiene margen de cuatro ciclos más allá del límite de bundles: el último MUL
puede terminar después de la última emisión.

`output.bundle_count` cuenta los bundles usados. `capacity` cuenta cuánto espacio
se reservó. Cuando se agota, `append_bundle` duplica capacidad hasta el límite
de texto. Así no tiene que redimensionar y copiar por cada bundle nuevo.

El resultado de `realloc` va primero a un puntero temporal. Si falla, el puntero
viejo sigue disponible para liberar correctamente lo que ya existía.

### Quién es dueño de qué

| Objeto | Memoria que posee | Cómo se libera |
|---|---|---|
| `MachineProgram` | Instrucciones, etiquetas, nombres y bloques | `porygon_machine_program_free` |
| Contexto `scheduler_t` | Copias de metadatos, ciclos y reservas temporales | Dentro de `porygon_schedule_program` |
| `porygon_schedule_t` publicado | Bundles y posiciones finales de etiquetas | `porygon_schedule_free` |

El calendario conserva índices hacia el programa original. Por eso `MachineProgram`
debe seguir vivo mientras verificamos o imprimimos la salida. El caller inicializa
el calendario vacío antes de llamar y no debe sobrescribir uno anterior sin liberarlo.

### Por qué hay un `goto done`

Hay varias etapas que pueden fallar después de reservar memoria. En vez de copiar
la misma secuencia de `free` en cada error, todas llegan a un punto de limpieza.
No es el mecanismo que controla la planificación; únicamente evita duplicar la
liberación de recursos.

Al finalizar correctamente ocurre esto:

```c
*out = s.output;
s.output = (porygon_schedule_t){0};
```

La asignación copia la estructura con sus punteros; **no duplica los arreglos**.
Su propiedad pasa al caller. Vaciar `s.output` evita que la limpieza local libere
los mismos arreglos que acabamos de entregar. Si algo falla antes, no se modifica
`out` y se descarta el calendario parcial.

### Por qué distinguimos API pública y auxiliares

Un consumidor normalmente incluye `scheduler.h`: necesita planificar, verificar,
consultar o escribir un calendario. `internal.h` conecta los módulos de la
implementación y evita repetir declaraciones de codificación, latencia y validación.

Las funciones `static` solo se usan dentro de su archivo. Por ejemplo, otro
módulo no debería llamar directamente a `choose_unit` ni manipular a medias el
estado de una planificación.

## 11. Cómo recorrerlo y usarlo por tu cuenta

### Primera lectura del código

Una forma práctica es seguir estas llamadas en lugar de leer cada archivo completo:

```text
main — tools/schedule_main.c
  porygon_parse_program_file
  porygon_schedule_program
    schedule_validate_program
    schedule_describe
    schedule_block — una vez por bloque
      append_bundle
      dependencies_ready
      choose_unit
      schedule_encode
    resolve_jumps
    porygon_verify_schedule
  porygon_write_bundles / porygon_write_mem
  liberación del calendario y del programa
```

En `schedule_block`, anotá en papel cuatro valores después de cada emisión:
`index`, `cycle`, `position` y el WB reservado. Con el ejemplo de esta guía,
vas a ver por qué `issued[2]` queda en cero mientras `issued[1]` sigue pendiente.

### Ejecutar el ejemplo

Desde la raíz del repositorio:

```sh
make
bin/porygon-schedule ejemplo.asm ejemplo.bundles ejemplo.mem
```

Copiá a `ejemplo.asm` las ocho instrucciones de la sección 3, sin los identificadores
I0–I7 ni comentarios agregados. El parser actual admite etiquetas y líneas vacías,
pero no comentarios ni bundles como entrada. Deberías obtener **8 instrucciones
y 12 bundles**, con `inicio` en 0 y `fin` en 11.

El argumento `.mem` es opcional. El ejemplo más largo `control_memory.asm`, con
inicialización de registros, un bucle y accesos de memoria, se conserva en la
carpeta externa de pruebas. `pipoc` mantiene su interfaz para Piper;
esta CLI permite trabajar sin esperar al generador de código del frontend.

### Si algo te sorprende, empezá por aquí

| Lo que observás | Dónde investigarlo |
|---|---|
| Una instrucción espera aunque haya slots | `dependencies_ready`; después `choose_unit`. |
| Una suma terminó en ALU1 | Preferencia ALU0 y alternativa ALU1 en `choose_unit`. |
| Un MUL impide un shift dos ciclos después | Reserva `alu1_wb[cycle + latency]`. |
| Hay un NOP antes de un salto | Condición de resultados pendientes en `dependencies_ready`. |
| Una etiqueta apunta a un bundle vacío | Asignación de `label_bundles` antes de `schedule_block`. |
| El literal del salto parece extraño | `resolve_jumps`: resta en bundles desde el bundle actual. |
| Se rechaza una dirección constante | `valid_constant_address` y `update_constants`. |
| Se rechaza un calendario editado manualmente | `porygon_verify_schedule` y su diagnóstico. |
| El hexadecimal parece estar al revés | Slot 0 ocupa los bits bajos; `porygon_write_mem`. |

### Qué recordar del alcance actual

El objetivo es un scheduler funcional y legible, no encontrar siempre el menor
número posible de bundles. No renombra registros, no mueve operaciones entre
bloques, no prueba alias de memoria y no usa análisis global de valores vivos
para acortar las esperas antes de saltos.

Las direcciones dinámicas siguen siendo responsabilidad del programa. Un `nop`
del ASM lineal ocupa un slot, no fuerza por sí solo un ciclo entero. No se
sintetiza una instrucción de parada; el fragmento didáctico termina tras el store,
y un programa que necesite detener efectos debe definir su terminación.

El perfil actual tiene cuatro slots de 16 bits, como máximo 8192 bundles en los
64 KiB de texto y dos bundles NOP por salto. Si cambia el pipeline del hardware,
hay que revisar las latencias de `instruction.c`, las constantes públicas y las
reglas del verificador. Las mismas palabras de 16 bits pueden seguir siendo una
codificación válida y, aun así, necesitar un calendario temporal diferente.
