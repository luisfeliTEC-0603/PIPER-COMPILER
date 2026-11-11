# Implementación del generador de IR

Estado: I1 implementado y comprobado de forma aislada; I2–I9 pendientes. Contrato normativo: [contrato-IR.md](contrato-IR.md). Esta guía cubre AST validado → TAC tipado organizado en bloques básicos.

## 1. Alcance

| Incluido | Excluido |
| --- | --- |
| Modelo, generación, validación e impresión de IR. | Enlazado, IDE y nuevas pruebas generales del frontend. |
| División en bloques durante la generación. | Construcción del grafo: [todoCFG.md](todoCFG.md). |
| Tipos, símbolos, efectos y lecturas/escrituras identificables. | Liveness, optimizaciones, asignación de registros y spilling. |
| Exportación independiente o adicional mediante CLI. | Migración del codegen, cambios del scheduler o de la ISA. |

**Alta:** requerida para la primera integración. **Media:** incorporar después del núcleo escalar/control; requerida antes de declarar completa la cobertura del soporte actual.

## 2. Entrada, salida y puntos de integración

| Punto | Implementación requerida |
| --- | --- |
| Entrada | `AstNode` validado y tabla semántica final, de una misma ejecución. Mantener vivos ambos. |
| Éxito | `IrProgram` validado; rutinas con parámetros, temporales, bloques y terminadores explícitos. |
| Error | Estado y diagnóstico localizado; no devolver programa parcial. |
| `src/main.c` | Generar después del éxito semántico y de las salidas tempranas de revisión/clones; antes del retorno que omite ASM cuando no hay salidas de máquina. NO METER A MAIN HASTA QUE MODULO ESTE COMPLETO|
| `include/ast.h` / `src/analysis/semantic.c` | Agregar referencia al símbolo resuelto para declaraciones/usos/llamadas/control de ciclo. Inicializarla y asignarla durante la resolución existente; no repetir análisis. |
| `Makefile` | Agregar fuentes/objetos de `src/ir/`, dependencias del encabezado y enlace de `pipoc`. No enlazar IR en `porygon-schedule`. |

## 3. Módulos y métodos

La estructura mantiene seis archivos C y dos encabezados. `ir.c` ya implementa el almacenamiento de I1; el resto sigue declarado, sin conexión con `main`. Los campos de cantidad/capacidad indican qué contienen (`instruction_count`, `block_count`, `routine_count` y sus capacidades); el contexto selecciona `current_routine` y `current_block`. Las firmas mantienen cuatro funciones públicas. Los siete auxiliares de recorrido/emisión se complementan con tres de ciclo de vida necesarios para I1: iniciar/descartar contexto y reservar rutina, todos en `ir.c`.

| Módulo | Prioridad | Métodos y responsabilidad |
| --- | --- | --- |
| `include/ir.h` | Alta | Modelo compacto y cuatro entradas públicas: `ir_generate`, `ir_validate`, `ir_write`, `ir_program_free`. |
| `src/ir/ir_internal.h` | Alta | Contexto de generación y diez auxiliares compartidos; sin mapas ni metadatos semánticos duplicados. |
| `src/ir/ir.c` | Alta | `ir_context_init`, `ir_context_dispose`, `ir_new_routine`, `ir_new_block`, `ir_emit`, `ir_fail` y liberación implementados. Los flags se almacenan; se calcularán en I6. |
| `src/ir/ir_generate.c` | Alta | `ir_generate` e `ir_build_program`: registro/recorrido de rutinas y arranque, validación y publicación. |
| `src/ir/ir_expr.c` | Alta | `ir_expr` e `ir_target`: valores/destinos, incluyendo llamadas, intrínsecas e índices. Helpers específicos serán `static`. |
| `src/ir/ir_stmt.c` | Alta | `ir_stmt`: bloques léxicos, asignaciones, ramas, ciclos y retornos. Inicialización/copia de arreglos se resuelve aquí. |
| `src/ir/ir_validate.c` | Alta | `ir_validate`: invariantes del modelo, sin requerir CFG/liveness. |
| `src/ir/ir_print.c` | Alta | `ir_write`: cabeceras de tipos, TAC sencillo y comentarios con todos los flags activos. |

Las expresiones devuelven `IrOperand` directamente; los otros auxiliares modifican el contexto. `ctx->error.status` distingue ausencia válida de valor de un error y conserva el primer fallo. Los auxiliares dejan de emitir tras un error; el API público devuelve `IrStatus` y solo publica el modelo en éxito.

## 4. Secuencia de implementación

| ID | Prioridad | Trabajo concreto | Resultado que permite avanzar |
| --- | --- | --- | --- |
| I1 — completado | Alta | Reservar/liberar `IrProgram`, arreglos de rutinas/bloques/instrucciones, argumentos de llamadas y pila de salidas de ciclos. Definir contadores, IDs y operandos con tipo. | El modelo puede construirse parcialmente y descartarse sin liberar AST/símbolos prestados ni perder recursos. Ninguna variable de Piper cambia de dirección. |
| I2 | Alta | Registrar en el AST la referencia a la declaración que el semántico ya resolvió para cada identificador, llamada y control de `loop`. | Dos usos de la misma variable comparten el mismo `Symbol *`; dos declaraciones homónimas tienen referencias distintas. |
| I3 | Alta | Traducir literales, lecturas, operadores escalares y asignaciones a operandos e instrucciones TAC; capturar cada valor antes de evaluar el siguiente operando. | `b store a + 3` produce lectura de `a`, suma y escritura de `b`, con tipos correctos y orden preservado. |
| I4 | Alta | Crear una rutina IR por función, conservar su firma, emitir `call` con argumentos ordenados y `return` para `output`; separar sentencias globales en arranque si existen. | Las instrucciones de I3 quedan dentro de su función y las llamadas/retornos son explícitos, sin registros ni prólogos PORYGON. |
| I5 | Alta | Construir bloques y saltos para condicionales/ciclos; apilar la salida de cada ciclo para `break` y cerrar cada camino una sola vez. | `when`, `alwhen`, `loop` y `break` siguen las restricciones y esquemas del [ejemplo de control](ejemplo-flujo-IR.md#10-estructuras-de-control-para-i5). |
| I6 | Alta | Asignar flags por operación y validar tipos, temporales, firmas, destinos y transferencias finales. | Los errores y efectos actuales están representados; no se publica IR malformada ni se eliminan llamadas con resultado sin uso. |
| I7 | Media | Calcular direcciones y accesos por elemento para arreglos/matrices; emitir las cuatro intrínsecas con efectos de memoria/criptografía. | Índices evaluados una vez, límites conservados, parámetros por referencia y orden por filas. |
| I8 | Alta | Implementar `ir_write` con cabeceras y TAC; preparar reglas de construcción y opciones de CLI cuando el módulo esté completo. | Exportación solo IR o adicional con rutas comprobadas, sin publicar archivo parcial ni cambiar el codegen actual. No conectar a `main` antes de completar el módulo. |
| I9 | Alta | Ejecutar los tres casos IR-T1–IR-T3 y la construcción habitual al completar el módulo. | Coinciden las instrucciones/tipos/bloques esperados y se conservan los comandos existentes. |

### I1. Qué memoria se administra y qué identifica cada ID

La memoria administrada es la RAM del equipo donde corre `pipoc`. No se mueven variables de Piper, no se calculan nuevas direcciones de PORYGON y no se reservan posiciones de pila para temporales.

| Recurso | Quién lo crea / libera | Regla concreta |
| --- | --- | --- |
| `IrProgram` y arreglos de rutinas/bloques/instrucciones | Generador / `ir_program_free`. | Inicializar punteros y cantidades; incrementar la cantidad correspondiente (`routine_count`, `block_count` o `instruction_count`) solo cuando el elemento queda registrado. |
| Arreglo que necesita crecer | Módulo que lo agrega. | Reservar más capacidad; si se usa `realloc`, conservar la referencia anterior hasta comprobar éxito. |
| Argumentos de `call`/intrínseca | Generación de llamada / emisor y luego programa. | `ir_emit` consume la lista incluso al fallar; el descriptor original no la vuelve a liberar. |
| Pila `break_targets` | Contexto de generación. | Liberarla al terminar o abortar el recorrido; no pertenece al programa publicado. |
| AST, tabla y objetos `Symbol` | Frontend/orquestador. | IR solo los consulta. No los libera ni cambia sus ubicaciones. |
| Fallo a mitad de generación | Orquestador IR. | Liberar lo propio construido hasta ese punto, conservar el primer diagnóstico y dejar `*out = NULL`. |

Un **ID estable** es una identidad que no cambia porque un arreglo C cambie de dirección: el bloque B3 sigue siendo B3 después de crecer o reordenar el arreglo. No guardar como identidad un puntero a un elemento del arreglo. Los IDs de bloque no se confunden con posiciones físicas de memoria.

Los temporales también tienen IDs propios de su rutina: t0, t1, etc. Son resultados virtuales, no variables asignadas a RAM. La posición de rutina/instrucción de un diagnóstico es un índice de inspección; no se exige que identifique un resultado estable durante futuras transformaciones.

Un **operando tipado** contiene la clase de valor, el valor/referencia y su tipo exacto:

| Operando de ejemplo | Contenido interno | Significado |
| --- | --- | --- |
| Variable `a` B32 | `kind=SYMBOL`, referencia a su `Symbol`, `type={B32,false}`. | Variable entera de 32 bits con signo. |
| Constante `3` usada como B32 | `kind=CONSTANT`, valor 3, `type={B32,false}`. | Literal interpretado como B32 en esta operación. |
| Resultado temporal t0 B32 | `kind=TEMP`, ID 0, `type={B32,false}`. | Resultado virtual B32, todavía sin registro ni pila. |
| Resultado de `a < b` | Temporal con `type={bool,false}`. | El resultado es booleano aunque los argumentos sean B32. |
| Dirección de elemento B32 | Temporal con `type={B32,true}`. | Dirección hacia un elemento B32; no es el valor entero almacenado allí. |

`SYMBOL`, `CONSTANT` y `TEMP` abrevian los enums `IR_OPERAND_*`; `{B32,false}` resume los campos `piper` y `address`, no es la escritura literal de C. Tipar no significa guardar solamente el carácter `+` ni escoger arbitrariamente el tipo mayor. Por ejemplo, B8 + B16 puede tener resultado B16 conforme al semántico, pero B32 < B32 produce bool. Mezclas con/sin signo deben respetar las reglas existentes. El contenedor C `int64_t` no convierte las operaciones de Piper a 64 bits.

#### I1 implementado: uso y comprobación

1. `ir_context_init(&ctx, root, symbols)` crea un programa vacío y reserva la pila de salidas; no ejecuta análisis ni publica IR.
2. `ir_new_routine(&ctx, symbol)` reserva una rutina vacía, devuelve su índice y no la selecciona. Un símbolo NULL crea el único arranque sintético.
3. Seleccionar `ctx.current_routine`; crear bloques con `ir_new_block` y seleccionar `ctx.current_block` antes de emitir.
4. `ir_emit` almacena el descriptor y sus flags recibidos. Los saltos/condiciones/retornos cierran el camino; el cálculo de efectos y la validación completa siguen en I6.
5. `ir_context_dispose(&ctx)` libera pila y programa propios. Para transferir un programa al llamador, extraer `ctx.program` y poner ese campo en NULL antes de descartar el contexto. El diagnóstico se conserva.

Cada contexto se inicia una sola vez mientras tenga recursos; para reutilizarlo, descartarlo primero. Descartar uno ya inicializado o fallido más de una vez es seguro.

Los tres programas funcionales IR-T1–IR-T3 siguen pendientes para comprobar la traducción cuando el módulo esté completo. I1 no implementa I2/I3 ni enlaza IR con `pipoc`.

El almacenamiento mantiene solo dos auxiliares privados compartidos:

| Función privada | Qué hace y por qué se comparte |
| --- | --- |
| `grow_array` | Si el arreglo C está lleno, reserva más espacio preservando los elementos; por ejemplo, pasa de capacidad 4 a 8. Solo cambia la capacidad en éxito. La usan las listas de rutinas, bloques e instrucciones. |
| `free_call_arguments` | Libera el arreglo de operandos de `call f(x,y)`, no las variables x/y ni los operandos de una suma. Se usa al descartar una llamada y al destruir el programa que ya la almacenó. |

Crear bloques y emitir instrucciones sigue en `ir_new_block`/`ir_emit`; estas son las operaciones que usarán `when`, `alwhen` y `loop`. Las comprobaciones de rutina activa y el almacenamiento se leen directamente dentro de ellas, sin pasar por `source_origin`, `active_routine` ni `store_instruction`.


### I2. Enlazar usos con declaraciones, no ordenar el AST

Entrada: el semántico está visitando un nodo y ya obtuvo el `Symbol *` correcto. Acción: conservar esa referencia en el nodo correspondiente. Salida: el generador puede copiar la referencia a `IrOperand.value.symbol` sin buscar otra vez por nombre.

| Nodo | Referencia que debe conservar |
| --- | --- |
| Declaración/identificador `a` | Declaración exacta de `a` en su ámbito. |
| Destino de asignación | Misma declaración que se va a escribir. |
| Llamada `f[...]` | Declaración de la función aprobada y su firma. |
| Control `i` de `loop` | Declaración de la variable/parámetro que se actualizará. |

No se extraen, mueven ni reordenan nodos. Si hay un `a` exterior y otro `a` local declarado después de un uso, ese uso anterior conserva el símbolo que era visible al analizarlo; no se cambia por el símbolo local que aparece en la tabla final.

### I3. Convertir valores y cálculos escalares a TAC

Entrada: nodo de expresión/asignación aprobado. Salida: operandos de `ir_expr` e instrucciones añadidas por `ir_emit` al bloque activo.

| Paso para `b store a + 3` | Resultado |
| --- | --- |
| `ir_target(b)` | Referencia al destino `b`; todavía no se escribe. |
| `ir_expr(a)` | Emite `t0 = a` y devuelve t0 con tipo de `a`. |
| `ir_expr(3)` | Devuelve el operando constante; no necesita emitir una instrucción solo por el literal. |
| Combinar el nodo `+` | Emite `t1 = t0 + 3`, con tipo/flags resueltos. |
| Completar asignación | Emite `b = t1`. |

Capturar una lectura fija el valor en el momento correcto. Para `a + cambiar[]`, si `a` vale 2 y la llamada cambia `a` a 10 y devuelve 1, el cálculo debe usar 2 + 1, no 10 + 1:

```text
t0 = a
t1 = call cambiar()
t2 = t0 + t1
```

I3 prepara esa regla incluso antes de implementar llamadas en I4. No aplica propagación de constantes, reordenamiento ni evaluación de la función durante compilación.

### I4. Agregar funciones, llamadas y retorno a la traducción

La generación de tres direcciones ya empieza en I3. I4 extiende esa traducción a la organización por rutinas, paso de argumentos y transferencia del resultado; no es una extracción genérica de todos los nodos.

| Construcción | Traducción requerida |
| --- | --- |
| `routine B32 incrementar[B32 x]` | Crear `IrRoutine` ligada al símbolo de función; firma `incrementar(x:B32) -> B32`. El parámetro `x` sigue siendo su símbolo, sin asignarle un registro aquí. |
| Cuerpo `output x + 1` | Capturar `x`, emitir suma y `return` del temporal resultante. |
| Llamada `incrementar[2]` | Evaluar argumento 2; emitir `t = call incrementar(2)`, donde t tiene el tipo de retorno B32. |
| Rutina de retorno none | Llamada sin temporal ficticio y `return` sin valor para `output none`. |
| Sentencias globales | Generarlas en una rutina sintética previa a la llamada de entrada a `main`; no mezclar sus instrucciones con el cuerpo de otra función. |

Cada rutina tiene sus temporales y bloque de entrada. La firma se consulta en el símbolo final; las convenciones de argumentos en registros, marcos de pila y bundles quedan fuera de I4.

### I5. Control estructurado a bloques y saltos

Entrada: nodos de condición/ciclo ya aprobados. Salida: bloques con transferencias explícitas. Cada destino es un ID de bloque de la misma rutina. La pila de `break` contiene las salidas de ciclos, no salidas de `when`.

Las reglas, fuentes pequeñas y TAC esperado se describen en [estructuras de control para I5](ejemplo-flujo-IR.md#10-estructuras-de-control-para-i5). Son ejemplos de documentación; no amplían los tres tests acordados ni implementan todavía el módulo.

No agregar inicializaciones implícitas ni asignar almacenamiento a temporales. `piper_type_storage_size` determina el ancho de los elementos, no un tamaño fijo de cuatro bytes para todos los tipos.

## 5. Reglas para generar expresiones y bloques

1. `ir_expr` evalúa hijos en orden y produce un valor estable. Para `a + llamada[]`, emitir primero una copia del valor de `a`, después la llamada y finalmente la suma.
2. Obtener tipos de las anotaciones/símbolos y considerar el destino, parámetro o retorno al convertir, sin pasar un tipo esperado por toda la recursión ni estrechar cálculos intermedios. Dar prioridad a las anotaciones y aplicar la regla B32/uB32 del contrato cuando falte tipo exacto. Una categoría `TYPE_INT` sola no determina ancho/signo.
3. `ir_target` devuelve símbolo escalar o dirección tipada; no carga el valor del destino salvo que la operación lo requiera.
4. Para un destino indexado, evaluar y comprobar cada índice en orden antes del origen. No volver a evaluar índices durante el `STORE`.
5. Crear bloques para entrada, destinos y continuaciones. Condiciones enteras se expresan con `t = x != 0`; `BRANCH` almacena ambos destinos y es la última instrucción del bloque.
6. El cuerpo de `alwhen` vuelve a condición. El de `loop` pasa al avance y luego a condición. `break` salta a salida; `RETURN` cierra el camino sin continuación.
7. Si ambas ramas retornan, no crear unión ejecutable. Omitir sentencias posteriores de un camino ya cerrado; no hacer eliminación global de bloques ni plegado de constantes.
8. Finalizar con `RETURN` sin valor solo cuando el retorno de la rutina sea `none`; un camino final sin retorno en una rutina con valor es error estructural. Nunca inventar `return 0`.
9. Antes de publicar, resolver IDs de destinos y ejecutar `ir_validate`. No producir predecesores/sucesores en esta etapa. No mantener `has_terminator` ni un campo de terminador separado: consultar la última operación de la lista.

Las comprobaciones que pueden fallar son atributos/operaciones con salida de error implícita según el contrato; no requieren dividir un bloque por cada comprobación.

## 6. CLI y publicación de archivos

| Comando/modo | Comportamiento requerido |
| --- | --- |
| `bin/pipoc ir programa.rwt [-o salida.ir]` | Generar únicamente IR; salida predeterminada junto al fuente: `programa.ir`. |
| `bin/pipoc programa.rwt --emit-ir salida.ir` | Exportar IR antes de optimizar y continuar con la salida principal actual. |
| `--check`, clones, LL(1), `-x`, conversión bin→mem | No generar IR. Rechazar combinaciones incompatibles con las nuevas opciones. |
| `all` | Mantener sus salidas actuales; incluir IR únicamente al solicitar `--emit-ir`. |

Actualizar enum de comandos/opciones, `cli_options_t`, ayuda, validaciones de combinaciones, rutas, salidas y limpieza. Extender la comprobación de colisiones para incluir `.ir`; rechazar dos rutas para la misma salida (`ir -o ... --emit-ir ...`).

Validar todo el programa antes de escribir. Publicar el archivo mediante temporal en el directorio destino y renombrado tras escribir/cerrar correctamente; borrar solo el temporal al fallar y conservar una salida anterior válida. No tocar un fuente ni otro artefacto como salida. El exportador es lectura del modelo: no modifica la IR.

## 7. Formato de inspección

Ejemplo esquemático para `a store 2`, `b store a + 3 * 4` y `output b`, con ambas variables B32:

```text
function main() -> B32
    variables: local a:B32, local b:B32
    temporales: t0:B32, t1:B32, t2:B32, t3:B32
    constantes: 2:B32, 3:B32, 4:B32
B0:
    a = 2
    t0 = a
    t1 = 3 * 4  ; comprueba rango del resultado B32, puede fallar
    t2 = t0 + t1 ; comprueba rango del resultado B32, puede fallar
    b = t2
    t3 = b
    return t3
```

La cabecera declara tipos sin recargar cada instrucción. Los comentarios muestran flags activos, no operaciones nuevas. No se pliegan constantes ni se omiten metadatos que afecten tipos/efectos; las comprobaciones potenciales son conservadoras antes de optimizar. El detalle de escritura es normativo en [contrato-IR.md](contrato-IR.md).

El ejemplo completo de fuente, AST, tabla semántica, recorrido y salidas está en [ejemplo-flujo-IR.md](ejemplo-flujo-IR.md). El inventario de nombres sigue en [glosario-IR.md](glosario-IR.md).

## 8. Pruebas de IR pendientes — tres casos

| Caso | Prioridad | Fuente pequeña | Comprobaciones |
| --- | --- | --- | --- |
| IR-T1 | Alta | `main` declara B32 `a`, `b`, asigna 2 a `a`, calcula `b = a + 3 * 4` y retorna `b`. | TAC del ejemplo, lecturas capturadas, tipos y comprobaciones; un bloque. Modo `ir` crea solo `.ir`. |
| IR-T2 | Alta | `main` declara bool `p` y B32 `x`, asigna `true` a `p`, usa `when [p]` para asignar 1 o 2 a `x` y retorna `x`. | Entrada con `BRANCH`, dos ramas y unión; ambos destinos válidos, mismo símbolo `x`, sin eliminar rama constante. |
| IR-T3 | Alta | `main` inicia B32 `i` en 0; `alwhen [i < 3]` incrementa `i` y hace `break` si llega a 2; retorna `i`. | Condición reevaluada, vuelta al ciclo y salida de `break`; ningún bloque tiene instrucciones posteriores a su terminador. |

Usar fuentes completas de aproximadamente 8–15 líneas, snapshots pequeños de TAC y comprobaciones de estructuras/tipos. No implementar todavía estas pruebas. Los tres casos cubren el núcleo inicial, no toda la cobertura posterior de llamadas, arreglos e intrínsecas.

## 9. Referencias de implementación

| Fuente revisada | Uso |
| --- | --- |
| Enunciado `proyecto-ii-grupal-perf-cache.pdf`, §4 y §7.2 | TAC definido por el grupo, bandera de exportación e identificación de bloques. |
| Presentación `Apuntes_PresentacionOptimizacion.pdf`, diapositivas 5–9 y 14 | Operaciones, cuádruplos, recorrido del AST y líderes de bloques. |
| [main.c](../../src/main.c), [Makefile](../../Makefile) | Orquestación, CLI, rutas y construcción. |
| [ast.h](../../include/ast.h), [semantic.c](../../src/analysis/semantic.c), [symbol_table.h](../../include/symbol_table.h) | Entrada, resolución, tipos, ámbitos y almacenamiento existente. |
| [codegen_expr.c](../../src/codegen/codegen_expr.c), [codegen_stmt.c](../../src/codegen/codegen_stmt.c) | Orden de evaluación, prefijos, ciclos y destinos. |
| [codegen_array.c](../../src/codegen/codegen_array.c), [codegen_arithmetic.c](../../src/codegen/codegen_arithmetic.c), [codegen_crypto.c](../../src/codegen/codegen_crypto.c) | Límites dinámicos, errores de aritmética e intrínsecas. |

Las APIs/formatos de esta guía son decisiones de implementación. No se presupone que el enunciado imponga esos nombres.
