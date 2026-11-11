# Implementación del generador de IR

Estado: I1–I6 implementados y comprobados de forma aislada; I7–I9 pendientes. Contrato normativo: [contrato-IR.md](contrato-IR.md). Esta guía cubre AST validado → TAC tipado organizado en bloques básicos.

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
| `include/ast.h` / `src/analysis/semantic.c` | I2 implementado: `resolved_symbol` conserva la declaración resuelta durante el análisis existente; no repetir búsquedas en IR. |
| `Makefile` | Agregar fuentes/objetos de `src/ir/`, dependencias del encabezado y enlace de `pipoc`. No enlazar IR en `porygon-schedule`. |

## 3. Módulos y métodos

La estructura mantiene seis archivos C y dos encabezados. `ir.c` implementa el almacenamiento de I1; `ir_expr.c` y `ir_stmt.c`, la traducción escalar, llamadas, retornos y control de I3–I5. `ir_build_program` organiza rutinas y arranque. `ir_generate` construye y revisa la estructura antes de entregar el programa. La impresión sigue declarada, sin conexión con `main`. Los campos de cantidad/capacidad indican qué contienen (`instruction_count`, `block_count`, `routine_count` y sus capacidades); el contexto selecciona `current_routine` y `current_block`. Las firmas mantienen cuatro funciones públicas. Hay once auxiliares compartidos: los de recorrido/emisión, los tres de ciclo de vida de I1 y `ir_emit_value`, compartido por expresiones y control para definir temporales.

| Módulo | Prioridad | Métodos y responsabilidad |
| --- | --- | --- |
| `include/ir.h` | Alta | Modelo compacto y cuatro entradas públicas: `ir_generate`, `ir_validate`, `ir_write`, `ir_program_free`. |
| `src/ir/ir_internal.h` | Alta | Contexto de generación y once auxiliares compartidos; sin mapas ni metadatos semánticos duplicados. |
| `src/ir/ir.c` | Alta | `ir_context_init`, `ir_context_dispose`, `ir_new_routine`, `ir_new_block`, `ir_emit`, `ir_fail` y liberación implementados. I6 completa efectos al emitir, conservando los atributos solicitados. |
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
| I2 — completado | Alta | Registrar en `AstNode.resolved_symbol` la declaración que el semántico ya resolvió para cada identificador, llamada y control de `loop`. | Dos usos de la misma variable comparten el mismo `const Symbol *`; dos declaraciones homónimas tienen referencias distintas. |
| I3 — completado | Alta | Traducir literales, lecturas, operadores escalares y asignaciones a operandos e instrucciones TAC; capturar cada valor antes de evaluar el siguiente operando. | `b store a + 3` produce lectura de `a`, suma y escritura de `b`, con tipos correctos y orden preservado. |
| I4 — completado | Alta | Crear una rutina IR por función, conservar su firma, emitir `call` con argumentos ordenados y `return` para `output`; separar sentencias globales en arranque si existen. | Las instrucciones de I3 quedan dentro de su función y las llamadas/retornos son explícitos, sin registros ni prólogos PORYGON. |
| I5 — completado | Alta | Construir bloques y saltos para condicionales/ciclos; apilar la salida de cada ciclo para `break` y cerrar cada camino una sola vez. | `when`, `alwhen`, `loop` y `break` siguen las restricciones y esquemas del [ejemplo de control](ejemplo-flujo-IR.md#10-estructuras-de-control-para-i5). |
| I6 — completado | Alta | Completar flags por operación y comprobar estructura del IR: destinos, temporales, terminadores y coherencia básica de operandos. No repetir tipos ni firmas del semántico. | Efectos conservados y estructura revisada antes de publicar; sin un segundo análisis semántico. |
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
4. `ir_emit` almacena el descriptor y sus flags recibidos. Los saltos/condiciones/retornos cierran el camino; I6 ya completa los efectos y revisa la estructura.
5. `ir_context_dispose(&ctx)` libera pila y programa propios. Para transferir un programa al llamador, extraer `ctx.program` y poner ese campo en NULL antes de descartar el contexto. El diagnóstico se conserva.

Cada contexto se inicia una sola vez mientras tenga recursos; para reutilizarlo, descartarlo primero. Descartar uno ya inicializado o fallido más de una vez es seguro.

Los tres programas funcionales IR-T1–IR-T3 siguen pendientes para comprobar la traducción cuando el módulo esté completo. I3 ya genera TAC escalar de forma aislada; IR todavía no se enlaza con `pipoc`.

El almacenamiento mantiene solo dos auxiliares privados compartidos:

| Función privada | Qué hace y por qué se comparte |
| --- | --- |
| `grow_array` | Si el arreglo C está lleno, reserva más espacio preservando los elementos; por ejemplo, pasa de capacidad 4 a 8. Solo cambia la capacidad en éxito. La usan las listas de rutinas, bloques e instrucciones. |
| `free_call_arguments` | Libera el arreglo de operandos de `call f(x,y)`, no las variables x/y ni los operandos de una suma. Se usa al descartar una llamada y al destruir el programa que ya la almacenó. |

Crear bloques y emitir instrucciones sigue en `ir_new_block`/`ir_emit`; estas son las operaciones que usarán `when`, `alwhen` y `loop`. Las comprobaciones de rutina activa y el almacenamiento se leen directamente dentro de ellas, sin pasar por `source_origin`, `active_routine` ni `store_instruction`.


### I2. Enlazar usos con declaraciones, no ordenar el AST

Entrada: el semántico está visitando un nodo y ya obtuvo el `Symbol *` correcto. Acción implementada: conservarlo en `AstNode.resolved_symbol`, de tipo `const Symbol *`. Salida: el generador podrá copiar la referencia a `IrOperand.value.symbol` sin buscar otra vez por nombre.

| Nodo | Referencia que debe conservar |
| --- | --- |
| Declaración de variable, parámetro o rutina | Símbolo creado en la tabla semántica final. |
| Identificador `a` | Declaración exacta de `a` en su ámbito. |
| Destino de asignación / base de acceso indexado | El identificador correspondiente conserva la declaración; no se duplica en el nodo de asignación/índice. |
| Llamada ordinaria `f[...]` | Declaración de la función aprobada y su firma. |
| Control `i` de `loop` | Declaración de la variable/parámetro que se actualizará. |

No se extraen, mueven ni reordenan nodos. Si hay un `a` exterior y otro `a` local declarado después de un uso, ese uso anterior conserva el símbolo que era visible al analizarlo; no se cambia por el símbolo local que aparece en la tabla final.

El constructor inicializa la referencia en NULL. Semántica la renueva al analizar cada nodo, incluidos los parámetros; queda en NULL cuando no aplica, la resolución falla o la llamada es intrínseca. La tabla final es dueña de los símbolos: el AST no los libera. Al repetir semántica se reemplaza la tabla y se renuevan los enlaces; no conservar IR de una ejecución anterior.

Comprobación aislada realizada: pertenencia a la tabla final, sombreado antes/después de una declaración, parámetros/control de `loop`, llamadas ordinarias/intrínsecas, base de índices y reconstrucción del análisis. No se agregaron pruebas permanentes ni cambios al Makefile.

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

I3 prepara esa regla, que I4 mantiene al incorporar llamadas. No aplica propagación de constantes, reordenamiento ni evaluación de la función durante compilación.

#### I3 implementado: alcance y reglas de uso

| Entrada / operación | Comportamiento actual |
| --- | --- |
| Contexto | Seleccionar una rutina y un bloque antes de traducir; I4 ya realiza esa orquestación mediante `ir_build_program`. |
| Literales | Entero/bool devuelve constante tipada sin instrucción; `none` devuelve operando ausente con estado `IR_OK`. |
| Lectura / destino | `ir_expr(a)` emite copia a temporal; `ir_target(a)` devuelve el símbolo de I2 sin leerlo ni buscar por nombre. |
| Operadores | Aritmética, comparaciones y lógica; ambos lados se evalúan en orden, sin cortocircuito ni plegado. |
| `++` / `--` | Capturar identificador escalar, sumar/restar 1 con comprobación, escribir y devolver el temporal nuevo. |
| Asignación | Resolver destino, evaluar origen y emitir `COPY`; una constante adopta el tipo del destino aprobado. Un temporal conserva su tipo previo. |
| Bloque / declaración | Recorrer sentencias en orden; una declaración no emite inicialización. También se admiten expresiones como sentencias. |
| Atributos | Aritmética solicita `IR_CHECKED` y `IR_MAY_TRAP`; copia entre tipos distintos también conserva comprobación del destino. I6 ya completa el cálculo común de efectos y la revisión estructural. |
| Extensiones posteriores a I3 | I4 agrega llamadas ordinarias y retornos; I5, control estructurado. Índices e intrínsecas siguen pendientes; devuelven `IR_UNSUPPORTED`. |

Los tres auxiliares privados de expresiones y el emisor compartido tienen una responsabilidad cada uno:

| Auxiliar | Propósito |
| --- | --- |
| `constant_integer` | Consultar únicamente árboles de literales para decidir B32/uB32 si falta anotación exacta, sin eliminar sus operaciones TAC. |
| `expression_type` | Priorizar el tipo semántico; usar la regla B32/uB32 solo como respaldo. |
| `operator_opcode` | Relacionar cada operador AST con su opcode TAC. |
| `ir_emit_value` | En `ir.c`, asignar el siguiente ID temporal y emitir su definición; compartido con control desde I5, sin asignar memoria de Piper. |

Comprobación aislada con fuentes pequeñas: secuencia TAC, identidad de símbolos, tipos B8/B16/B32/uB32/bool, operadores, límites enteros, orden con incrementos, evaluación completa de `and`/`or` y preservación del primer error. Sin pruebas permanentes ni cambios al Makefile.

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

#### I4 implementado: entradas, salidas y límites

| Punto | Regla actual |
| --- | --- |
| Entrada | Contexto nuevo de `ir_context_init`, AST aprobado y tabla final de I2. Llamar a `ir_build_program(&ctx)` una vez. |
| Salida interna | `ctx.program` contiene rutinas con firmas prestadas, bloque de entrada y temporales propios. `ctx.error.status` conserva el primer fallo. Todavía no se publica como IR validada. |
| Recorrido | Registrar funciones globales en orden y traducir cada cuerpo. `begin_routine` selecciona rutina, crea entrada y vacía la pila de ciclos. |
| Llamada | `call_expression` usa `resolved_symbol`, reserva una lista de operandos y evalúa argumentos de izquierda a derecha. Un fallo libera la lista; al emitir, la propiedad pasa a `ir_emit`. |
| Tipos | Constantes adoptan el tipo del parámetro/retorno aprobado; temporales conservan el tipo del cálculo. La firma describe el tipo de entrega sin estrechar operaciones previas. |
| Resultado de llamada | Temporal del tipo de retorno, aunque no se use. Si retorna none, no crear temporal. Las llamadas conservan lectura/escritura de memoria y posible fallo. |
| Retorno | `output` evalúa el valor y emite `RETURN`; no traducir instrucciones posteriores del camino cerrado. Un camino abierto con retorno de valor es error; no inventar `return 0`. |
| Arranque | Solo si hay sentencias globales: crear rutina sintética, traducirlas en orden, emitir llamada a main y retorno sin valor. Declaraciones globales solas no requieren arranque. |
| Recursión | Conservar llamadas al símbolo de la propia función; no expandir cuerpos ni volver a analizarlos. |
| Fuera de alcance | Rutinas anidadas, argumentos de arreglo e intrínsecas se rechazan mientras no exista su soporte correspondiente. I5 ya incorpora control. |
| Publicación | `ir_generate` ya construye, revisa estructura y entrega propiedad solo en éxito desde I6. Sin integración con main, opciones CLI ni archivo `.ir` todavía. |

El modelo admite retorno none explícito o implícito al final de una rutina none. El parser y registro semántico actuales no admiten declarar esas rutinas desde el fuente; se comprobó este caso mediante AST/símbolos preparados internamente, sin modificar el frontend.

Comprobaciones aisladas: TAC completo del ejemplo escalar, parámetros, llamadas anidadas y recursivas, argumentos con incremento, retornos, temporales por rutina, arranque global, caminos cerrados y errores de construcciones pendientes. Sin pruebas permanentes ni cambios al Makefile.


### I5. Control estructurado a bloques y saltos

Entrada: nodos de condición/ciclo ya aprobados. Salida: bloques con transferencias explícitas. Cada destino es un ID de bloque de la misma rutina. La pila de `break` contiene las salidas de ciclos, no salidas de `when`.

Las reglas, fuentes pequeñas y TAC esperado se describen en [estructuras de control para I5](ejemplo-flujo-IR.md#10-estructuras-de-control-para-i5). Describen el control implementado internamente; no amplían los tres tests finales acordados ni habilitan todavía la exportación.

#### I5 implementado: métodos y reglas

| Método / elemento | Regla concreta |
| --- | --- |
| `when_statement` | Evaluar condición una vez, crear destinos verdadero/falso y traducir ambas ramas. Sin fallback, el falso es la continuación. Crear unión solo si alguna rama continúa; usar el bloque final de cada rama, también cuando contiene control anidado. |
| `condition_value` | Usar bool directamente; para entero emitir temporal bool con `valor != 0`. No elegir ramas ni plegar condiciones constantes. |
| `loop_statement` | Compartir entrada, condición, cuerpo y salida de alwhen/loop. Reevaluar condición o fin en el bloque de prueba de cada vuelta. |
| `loop` | Usar el símbolo de I2; inicio una vez; paso literal positivo. Evaluar fin antes de leer el control, como hace el backend, por si fin lo modifica. Condición `control < fin`. |
| Avance de `loop` | Crear bloque solo si el cuerpo continúa. Leer control, sumar paso y almacenar sin `IR_CHECKED`, igual que el backend actual. La copia inicial también conserva el almacenamiento sin nueva comprobación de rango; los operandos mantienen sus tipos, sin retipar constantes fuera de rango al tipo del control. |
| `push_break` | Apilar salida del ciclo; ampliar la lista C si se llena. Retirar la entrada tras traducir el cuerpo incluso si este falla, para restaurar la salida exterior. |
| `break` | Saltar al último destino apilado. Sin ciclo activo, error localizado. No ejecutar avance ni sentencias posteriores en ese camino. |
| `jump_to` | Emitir salto solo si el camino sigue abierto; no añadir otro terminador tras return/break. |
| `ir_emit_value` | Emisor de temporales ahora compartido en ir.c; expresiones, conversión de condición y operaciones del loop usan la misma reserva/emisión. Sin nueva interfaz pública. |
| Bloques | Mantener IDs, sin guardar punteros a bloques mientras se crean otros. No construir predecesores/sucesores ni CFG. |

La salida del ciclo se reserva antes del avance; sus etiquetas pueden aparecer antes en el listado sin que se ejecuten en ese orden. Cada bloque se cierra con su transferencia explícita. No crear avance cuando todos los caminos del cuerpo retornan o hacen break. Un ciclo todavía conserva su salida por condición falsa, aunque su cuerpo siempre termine.

Comprobaciones aisladas con fuentes pequeñas e intérprete temporal del TAC: when con/sin fallback, ambas ramas con return, condiciones enteras, alwhen, loop con fin cambiante o llamada, break, seis ciclos anidados, destinos/terminadores y pasos no admitidos. Sin pruebas permanentes ni cambios al Makefile. I6 ya revisa estructura y entrega el modelo; la exportación sigue pendiente de I8.

No agregar inicializaciones implícitas ni asignar almacenamiento a temporales. `piper_type_storage_size` determina el ancho de los elementos, no un tamaño fijo de cuatro bytes para todos los tipos.

### I6. Alcance reducido: efectos y estructura — implementado

Se reemplaza la validación amplia de tipos y firmas por una revisión pequeña del modelo generado. El AST y la tabla ya fueron aprobados: no volver a resolver nombres, calcular promociones ni comprobar compatibilidad de asignaciones, parámetros o retornos.

| Comprobación / trabajo | Alcance concreto |
| --- | --- |
| Flags por operación | Completar los efectos de memoria/criptografía y conservar las comprobaciones de rango/fallo solicitadas. Son metadatos para optimizador y codegen, no otra validación del fuente. |
| Destinos | IDs de bloques únicos dentro de la rutina; entrada y destinos de salto existentes en esa misma rutina. |
| Temporales | IDs dentro del contador de su rutina, definición única y definición existente para cada uso; conservar el mismo tipo en definición y usos. No añadir análisis de dominancia, CFG ni liveness. |
| Transferencias | Cada bloque contiene instrucciones y termina en jump, branch o return; no hay instrucciones posteriores a un terminador. `ir_emit` ya previene emisiones tras cerrar un bloque, pero no garantiza que todos los bloques creados hayan recibido un cierre. |
| Coherencia básica | Revisar la forma de la instrucción: por ejemplo, branch recibe un valor bool y una llamada con argumentos tiene su lista. No recalcular el tipo que semántica asignó a una expresión. |
| Fuera de esta validación | Compatibilidad de tipos Piper, promociones, resolución de símbolos y revisión completa de firmas de llamadas/retornos. Se reutilizan los resultados semánticos. |

Estas comprobaciones buscan fallos de construcción del IR, cuyos bloques y temporales no existían durante semántica. No demuestran por sí solas que la traducción conserve todo el comportamiento del fuente. I6 implementa este alcance reducido, sin agregar otra etapa al flujo.

| Implementación | Entrada → salida / regla |
| --- | --- |
| `instruction_flags` en ir.c | Opcode, atributos y operandos → flags completos. Preserva `IR_CHECKED`; este implica posible fallo, pero no se añade a sumas sin comprobación como el avance de loop. |
| Globales | Comparar la referencia del símbolo con declaraciones globales: leer valor marca lectura de memoria; escribir marca escritura. Obtener dirección con ADDR no lee el valor. |
| Memoria y criptografía | LOAD/STORE marcan lectura/escritura; límites y división/residuo/potencia conservan posible fallo. Intrínsecas marcan estado criptográfico; Feistel también memoria. Su generación desde AST sigue pendiente de I7. |
| Llamadas | Sin análisis interprocedimental, conservar lectura/escritura de memoria y estado criptográfico, más posible fallo; una función llamada puede ejecutar otras llamadas o intrínsecas. |
| `ir_validate` | Dos pasadas por rutina: registrar definiciones y después comprobar usos. Reserva un índice temporal de inspección, que libera antes de retornar; no agrega una tabla persistente al modelo. |
| Diagnóstico | Retornar el primer error con rutina, bloque, instrucción y ubicación disponibles. No imprimir mensajes desde IR. |
| `ir_generate` | Validar entrada de API, construir, llamar a `ir_validate` y transferir programa solo en éxito. Ante fallo, descartar construcción y dejar salida NULL. El parámetro de salida debe empezar en NULL; si ya contiene un programa, rechazar la llamada sin sobrescribirlo ni liberarlo. |

Comprobación aislada: fuentes pequeñas con control/llamadas generadas mediante la API pública, ejecución del TAC en intérprete temporal, alteraciones deliberadas de destinos/IDs/terminadores/operandos y flags de globales/memoria/criptografía. También se verificó que la revisión no vuelve a exigir compatibilidad de firmas. Sin pruebas permanentes, cambios al Makefile ni integración con main. La exportación .ir sigue pendiente.


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
