# Contrato del generador de IR

Especificación para implementar; I1 implementa almacenamiento/liberación, I2 los enlaces semánticos del AST e I3–I5 la traducción TAC escalar con rutinas, llamadas, retornos y control. I6 completa efectos, revisión estructural y entrega pública del modelo; arreglos/intrínsecas desde AST y exportación siguen pendientes. Plan: [todoIR.md](todoIR.md), con tareas I1–I5 detalladas y sus entradas/salidas. Definiciones de términos, campos y operaciones: [glosario-IR.md](glosario-IR.md).

## 1. Entrada y precondiciones

| Entrada | Regla |
| --- | --- |
| `const AstNode *root` | Raíz `AST_PROGRAM` de un único archivo; `yyparse` terminó con 0 y produjo AST. |
| `const SymbolTable *symbols` | Tabla final de la misma ejecución; `analyze_semantics_with_trace` retornó 1. |
| Anotaciones | Usar tipos, dimensiones, ámbitos semánticos y `AstNode.resolved_symbol`. Sus referencias a declaraciones pertenecen a la tabla final. Para el cuerpo de rutina, tomar el ámbito de la rutina o `Symbol.function_scope`: el semántico visita su lista sin anotar necesariamente el nodo `AST_BLOCK` del cuerpo. |
| Control del flujo | `compile_piper` ejecuta los análisis una sola vez. `--check`, clones y consultas LL(1) conservan sus salidas actuales, sin generar IR. |

El generador no ejecuta análisis ni interpreta stdout/stderr. `main.c` comprueba los retornos antes de invocarlo.

### Uso de la tabla de símbolos

| Dato consultado | Para qué se usa |
| --- | --- |
| Declaración exacta y ámbito | Distinguir variables homónimas y globales/locales sin resolver el nombre nuevamente. |
| Tipo declarado | Conservar ancho y signo de la variable o parámetro. |
| Dimensiones de arreglo | Calcular direcciones y comprobar índices. |
| Firma de rutina | Conservar orden/tipos de parámetros y retorno, incluidos arreglos por referencia. |
| Declaraciones globales y locales | Imprimir datos y firmas sin crear otra tabla dentro de la IR. |

El AST conserva nombres, tipos y la referencia `resolved_symbol` a la declaración final. La tabla aporta las declaraciones y sus metadatos sin duplicarlos en el AST ni en IR. Se presta sin modificarla; sus direcciones físicas no se usan para asignar temporales. Sus símbolos deben permanecer vivos durante la generación y todos los consumidores del IR; reconstruir o destruir la tabla invalida los préstamos anteriores.


## 2. API pública propuesta

```c
IrStatus ir_generate(const AstNode *root, const SymbolTable *symbols,
                     IrProgram **out, IrDiagnostic *diagnostic);
IrStatus ir_validate(const IrProgram *program, IrDiagnostic *diagnostic);
IrStatus ir_write(FILE *output, const IrProgram *program);
void ir_program_free(IrProgram *program);
```

| Resultado | Contrato |
| --- | --- |
| `IR_OK` | `ir_generate` publica en `*out` una IR completa y validada. |
| `IR_INVALID_INPUT` | Entrada nula, raíz incorrecta o metadatos obligatorios ausentes. |
| `IR_UNSUPPORTED` | Construcción sin traducción definida para esta implementación. |
| `IR_OUT_OF_MEMORY` | No fue posible reservar memoria. |
| `IR_INVALID_PROGRAM` | Invariante estructural o de tipos incumplida. |
| `IR_IO_ERROR` | Falló la impresión; el llamador también comprueba el cierre del archivo. |

`out` debe ser válido y `*out == NULL` al entrar. Ante error permanece nulo y se libera la construcción parcial. El diagnóstico contiene estado, ubicación fuente, rutina y mensaje; bloque/instrucción cuando existan. `ir_program_free(NULL)` es válido.

## 3. Modelo de datos

Usar estructuras C y arreglos dinámicos durante la compilación. Operaciones simples: cuádruplos `(op, arg1, arg2, result)` con operandos tipados; los nombres impresos no se usan como identidad interna.

| Estructura | Campos mínimos |
| --- | --- |
| `IrProgram` | Tabla semántica final prestada, rutinas y arranque opcional. |
| `IrRoutine` | `Symbol` de función prestado, bloques, bloque de entrada y contador de temporales. Símbolo nulo identifica el arranque sintético. |
| `IrType` | Tipo Piper exacto y bandera de dirección de 32 bits hacia elementos de ese tipo. |
| `IrOperand` | Constante `int64_t`, referencia directa a `Symbol`, ID temporal o ausencia de valor; con tipo. |
| `IrInstruction` | Opcode, dos argumentos tipados, resultado tipado, ubicación, flags y datos adicionales para llamadas/control. |
| `IrBlock` | ID estable y una sola lista de instrucciones; el terminador es su último elemento. |

Reutilizar `Symbol`/`SymbolTable` para nombres, firmas, parámetros, ámbitos y dimensiones. No crear otra tabla `IrSymbol`, mapas de nombres ni copias de esos metadatos. Todos los enteros válidos de Piper caben en `int64_t`; el tipo Piper sigue limitando el valor a su dominio original.

Usar `IrId` para IDs de bloque y temporal; no conservar punteros a arreglos que puedan moverse por `realloc`. Los temporales tienen una única definición y su tipo se registra en los operandos. No hace falta una tabla adicional `IrTemporary`. Las posiciones de rutina/instrucción se usan para localizar diagnósticos, no para identificar valores. Variables de usuario pueden reasignarse; no implementar SSA.

La instrucción tiene tres formas de almacenamiento: cuádruplo normal, destinos de control o lista de argumentos de llamada. Comparaciones producen bool y `BRANCH` consume ese valor. Memoria e intrínsecas usan la misma estructura, sin un modelo distinto para cada opcode.

## 4. Operaciones y efectos

| Familia | Notación impresa | Representación interna / regla |
| --- | --- | --- |
| Asignación | `x = y` | `IR_OP_COPY`; también captura una lectura en un temporal. |
| Aritmética | `+`, `-`, `*`, `/`, `%`, `^`, `-x` | Operaciones distintas por opcode; conservar tipo y comprobaciones. `^` es potencia de Piper, no XOR de C. |
| Comparación | `==`, `!=`, `<`, `<=`, `>`, `>=` | Resultado bool; preservar signo de los operandos. |
| Lógica | `and`, `or`, `xor`, `not` | Ambos operandos se evalúan según las reglas actuales; resultado bool. |
| Condición entera | `t = x != 0` | `IR_OP_NE` con cero; no se necesita un opcode adicional de conversión a bool. |
| Dirección y acceso | `t = &a`, `t = *p`, `*p = x` | `IR_OP_ADDR`, `IR_OP_LOAD`, `IR_OP_STORE`; el tipo de dirección incluye el del elemento. |
| Índice | `check 0 <= i < n` | `IR_OP_CHECK_BOUNDS`; no es un cálculo booleano descartable: falla si el índice no es válido. |
| Llamada | `t = call f(x, y)` o `call f(x, y)` | Rutina y argumentos ya evaluados; sin temporal si retorna none. |
| Intrínseca | `crypto_auth(...)`, `crypto_feistel(...)`, etc. | `IR_OP_INTRINSIC`; preservar sus efectos. |
| Salto | `jump B1` | `IR_OP_JUMP`; un destino. |
| Condición | `if t jump B1 else B2` | `IR_OP_BRANCH`; dos destinos explícitos. |
| Retorno | `return t` o `return` | `IR_OP_RETURN`; valor opcional. |

Los nombres C `IR_OP_*` identifican operaciones internamente: no se imprimen como sustitutos de `+`, `=`, etc. Los atributos se muestran en comentarios, por ejemplo `t = a + b  ; comprueba rango del resultado B32`. Efectos como lectura/escritura y posible error son metadatos, no nuevas instrucciones aritméticas. Cada opcode y cada bandera se definen por separado en el [glosario](glosario-IR.md).

Reconocer lecturas/escrituras explícitas mediante opcode, `arg1`, `arg2`, `result` y argumentos de llamada. Guardar efectos como bits en `flags`: `IR_READ_MEMORY`, `IR_WRITE_MEMORY`, `IR_READ_CRYPTO`, `IR_WRITE_CRYPTO` e `IR_MAY_TRAP`. `IR_CHECKED` conserva la solicitud de comprobación. El emisor común calcula los efectos a partir de operación/atributos; no se expone una API separada de visitas para esta fase. Llamadas sin análisis interprocedimental se consideran conservadoramente lectoras/escritoras de memoria y estado criptográfico, y potencialmente fallidas. Las operaciones sobre símbolos globales también tienen efecto de memoria. Las locales escalares se representan por símbolo; las escrituras por direcciones conservan su posible alias.

Las comprobaciones y operaciones fallidas tienen salida de error implícita y no producen continuación normal cuando fallan. Esta fase no crea bloques de error ni elimina operaciones potencialmente fallidas. El backend futuro deberá reproducir el comportamiento de error actual, no sustituirlo por un retorno normal.

## 5. Reglas de traducción obligatorias

| Construcción | Regla |
| --- | --- |
| Identificador escalar | Emitir `t = símbolo` al evaluarlo. No retrasar la lectura hasta después de otra expresión con efectos. |
| Expresión binaria | Evaluar izquierda, capturar su valor, evaluar derecha y emitir la operación. `and`/`or` evalúan ambos lados, como el codegen actual. |
| Asignación escalar | Evaluar origen y escribir el símbolo de destino. La declaración solo registra almacenamiento; no introduce inicialización inexistente. |
| Asignación indexada | Evaluar índices y comprobar límites antes del origen; conservar la dirección calculada y emitir un solo `STORE`. |
| `++`/`--` | Prefijos: capturar valor, comprobar límite del tipo, actualizar destino y devolver el valor nuevo. En la primera versión se conserva el soporte actual de identificadores escalares; otros destinos retornan `IR_UNSUPPORTED`. |
| `when`/`fallback` | Condición, dos destinos, ramas y unión solo si alguna rama continúa. Cada rama que retorna termina sin salto posterior. |
| `alwhen` | Bloques de condición, cuerpo y salida. Reevaluar condición en cada vuelta; volver desde el cuerpo solo si no terminó. |
| `loop` | Compatibilidad inicial: paso literal entero positivo, inicio una vez, fin reevaluado por vuelta y límite superior exclusivo. Actualizar control después del cuerpo; conservar el comportamiento aritmético actual de esa actualización. Paso distinto retorna `IR_UNSUPPORTED`, sin cambiar el parser/semántico. |
| `break` | Saltar a la salida del ciclo más interno mediante pila de destinos; no emitir el resto de ese camino. |
| `output` | Emitir `RETURN` y cerrar el camino actual. `output none` retorna sin valor. |
| Rutinas | Primera implementación: rutinas globales y llamadas del mismo archivo, incluida recursión admitida por semántica. Rutinas anidadas/capturas sin soporte de backend definido retornan `IR_UNSUPPORTED`. |
| Arreglos/matrices | Una o dos dimensiones enteras, según soporte actual. Por filas: `base + (fila * columnas + columna) * ancho`; vector: `base + índice * ancho`. Usar `piper_type_storage_size`. |
| Inicialización/copia de arreglo | Emitir evaluaciones y cargas/escrituras en orden por filas. No introducir copia masiva ni reordenar operaciones que puedan tener alias o efectos. |
| Intrínsecas | Conservar `crypto_auth` (4 argumentos), `crypto_write_key` (3), `crypto_feistel` (3) y `crypto_revoke` (0). Feistel lee/escribe el arreglo y el estado criptográfico es observable. |
| Arranque | Globales se registran como datos; sentencias globales se traducen en orden a una rutina sintética que luego llama a `main[]`. No emitir prólogos, registros ni convenciones físicas. |

Resolver tipos con `has_piper_type`/`has_element_piper_type` antes de leer el campo correspondiente. Si una operación entera carece de tipo exacto, reproducir `result_type`/`unsigned_operand` de `codegen_arithmetic.c`: B32 por defecto o uB32 para el valor constante que requiere ese rango. El contexto de asignación/argumento/retorno no debe estrechar arbitrariamente un cálculo intermedio. Mantener el tipo en cada operando y resultado. El destino de una asignación conserva su tipo, sin cambiar el del temporal que contiene el cálculo previo; no agregar otro campo de tipo por instrucción.

La identidad de usos, declaraciones, llamadas ordinarias y control de `loop` se registra durante el semántico en `AstNode.resolved_symbol`, antes de que una búsqueda posterior pueda encontrar otra declaración. El generador no reconstruye esa resolución solamente a partir del nombre. Para asignaciones e índices, usar el enlace del identificador destino/base. Las llamadas intrínsecas no tienen símbolo de función: su enlace queda en NULL y se reconocen por su nombre reservado. Un análisis fallido no habilita la generación de IR.

## 6. Bloques y validación

Restricciones iniciales y ejemplos completos de control: [estructuras para I5](ejemplo-flujo-IR.md#10-estructuras-de-control-para-i5).

La presentación (diapositivas 9 y 14) usa etiquetas, saltos y continuación secuencial; no exige que todo bloque termine con un salto escrito. Esta IR explicita la continuación para que el destino no cambie al mover bloques. Es una convención del modelo, no una instrucción extra obligatoria de PORYGON; un backend podrá omitir un salto al bloque físicamente siguiente si respeta las reglas de control del procesador.

Una etiqueta nombra una posición de entrada, no un dato ni una instrucción. Si aparece sola antes de otras instrucciones, nombra el comienzo de ellas; varias etiquetas consecutivas pueden referir al mismo bloque. No se necesita un bloque vacío independiente por etiqueta. La salida de una rutina debe tener el retorno correspondiente, no una etiqueta vacía que invente un valor.

Ejemplo con ambos caminos explícitos:

```text
B0:
    t0 = x < 10
    if t0 jump B1 else B2
B1:
    x = 1
    jump B3
B2:
    x = 2
    jump B3
B3:
    t1 = x
    return t1
```

En el listado de los apuntes, el paso de la última rama a la continuación puede quedar implícito. Aquí se escribe `jump B3`. Usar `jump` en el texto es una elección de nombre para el mismo salto incondicional; no cambia su semántica.


1. Construir bloques dentro de `ir_generate`; las etiquetas/destinos se resuelven antes de devolver el programa.
2. Convención interna elegida: cada bloque pertenece a una sola rutina y su última instrucción es `IR_OP_JUMP`, `IR_OP_BRANCH` o `IR_OP_RETURN`. Se imprimen con la notación de la tabla anterior. No hay caída implícita ni otro campo que duplique el terminador.
3. Un destino inicia bloque; después de una transferencia no se agrega otra instrucción al mismo bloque. Omitir sentencias sin continuación alcanzable durante el recorrido, sin aplicar DCE general.
4. Limitar `ir_validate` a estructura: IDs de bloques únicos, entrada/destinos existentes dentro de la misma rutina, temporales con IDs válidos y definición única/existente, tipo coherente entre definición y usos, y terminadores finales. Comprobar formas básicas como branch con operando bool y listas presentes para llamadas con argumentos. No repetir compatibilidad de tipos Piper, promociones, resolución de símbolos ni revisión completa de firmas/retornos.
5. El generador conserva el contexto estructurado: un valor creado solo en una rama no debe escapar a la unión; allí se consulta el símbolo reasignado. La revisión reducida de I6 no agrega dominancia, CFG ni análisis de disponibilidad entre caminos; comprobar que existe una definición no demuestra que se ejecute antes de todo uso.
6. No calcular variables vivas, registros físicos, posiciones de spill ni dependencias de calendarización.

## 7. Propiedad y exportación

La IR posee arreglos de rutinas/bloques/instrucciones y listas de argumentos. Los temporales son IDs y los nombres/metadatos se consultan en los símbolos prestados. AST y símbolos se prestan; la tabla debe vivir hasta terminar consumidores y liberar la IR. Orden de limpieza: consumidores → IR → AST → tabla. No conservar punteros al contexto temporal del semántico ni a la tabla descartada del parser.

### Formato de salida acordado

`ir_write` imprime una vista fiel de la misma IR que recibirán los consumidores: las instrucciones son sencillas y los tipos se declaran aparte. No crea una segunda IR ni vuelve a recorrer el fuente para producir el texto.

| Sección | Información obligatoria |
| --- | --- |
| Cabecera de rutina | Nombre, parámetros en orden y tipo exacto de retorno. |
| Variables | Nombre inequívoco, clase global/local, tipo exacto y dimensiones cuando existan. Los parámetros ya se declaran en la firma. |
| Temporales | Cada temporal creado y su tipo exacto, una sola vez en la cabecera. Una dirección se escribe `&B32` para indicar dirección a elemento B32, por ejemplo. |
| Constantes | Valores y tipos usados, agrupados por rutina y sin repetir parejas valor/tipo. Si un mismo valor tiene varios tipos y el contexto no lo distingue, anotar el tipo en su aparición. |
| Bloques e instrucciones | Etiquetas y TAC con símbolos normales. No repetir tipos en cada operación salvo ambigüedad. |
| Atributos/efectos | Comentarios junto a la instrucción con todos sus flags activos; sin comentario significa flags=0. |

Imprimir en orden determinista. Para temporales, recorrer resultados y usos y comprobar coherencia de tipos; no agregar una segunda tabla persistente para el exportador. Para variables, consultar declaraciones finales y calificar el ámbito solo cuando haya homónimos o colisión con nombres de temporales.

| Flag interno | Comentario legible |
| --- | --- |
| `IR_CHECKED` | `comprueba rango del resultado <tipo>`; sustituir `<tipo>` por el tipo exacto de `result`. |
| `IR_READ_MEMORY` | `lee memoria` |
| `IR_WRITE_MEMORY` | `escribe memoria` |
| `IR_READ_CRYPTO` | `lee estado criptográfico` |
| `IR_WRITE_CRYPTO` | `escribe estado criptográfico` |
| `IR_MAY_TRAP` | `puede fallar` |

Cuando concurran, listar los comentarios en ese orden y separados por coma. La operación `check` conserva además su condición explícita. Constantes y temporales se imprimen con sus valores/IDs, nunca con direcciones C. Punteros, capacidades de arreglos y otros detalles de almacenamiento del compilador no forman parte del TAC exportado; la ubicación fuente sigue disponible para diagnósticos.

La salida en archivo no es la entrada que se vuelve a analizar: CFG/optimizador/codegen usarán las estructuras con sus tipos, referencias a símbolos y flags. Se puede comprobar la correspondencia leyendo cabeceras y comentarios del `.ir`. Recorrido completo y salidas de referencia: [ejemplo-flujo-IR.md](ejemplo-flujo-IR.md).

Las APIs y la sintaxis son decisiones de este contrato. El enunciado §4 exige TAC exportable y útil para optimizar; §7.2 exige identificar bloques. La presentación, diapositivas 5–9 y 14, fundamenta operaciones y partición. La construcción de conexiones se especifica aparte en [todoCFG.md](todoCFG.md).

## 8. Auxiliares y errores internos

Mantener las cuatro funciones públicas. El contexto privado agrupa entrada, programa, rutina/bloque activos, pila de `break` y primer diagnóstico. Los auxiliares de recorrido/emisión siguen siendo construir programa, evaluar expresión, resolver destino, traducir sentencia, crear bloque, emitir instrucción y registrar fallo. I1 añade únicamente el ciclo de vida: `ir_context_init`, `ir_context_dispose` e `ir_new_routine`; no añade módulos ni cambia las cuatro entradas públicas.

`ir_expr`/`ir_target` devuelven el operando directamente; el estado de error queda por separado en `ctx->error.status`. Un `NONE` con estado `IR_OK` es válido; con error detiene la generación. Todos los auxiliares preservan el primer fallo y dejan de emitir después de él. El API público conserva el retorno explícito `IrStatus`.

`ir_emit` agrega tanto operaciones normales como terminadores y consume los argumentos dinámicos del descriptor incluso si falla. En éxito, un terminador cierra el camino actual; la siguiente emisión requiere seleccionar otro bloque. Los helpers exclusivos de un archivo serán `static` al implementarse, sin añadir más interfaces compartidas anticipadamente.

### Almacenamiento de I1 disponible

`ir_context_init` recibe un contexto nuevo, raíz AST_PROGRAM y tabla final prestadas; retorna estado y crea un programa vacío. `ir_new_routine` reserva almacenamiento sin traducir la función. `ir_context_dispose` acepta NULL y descarte repetido, libera pila/modelo y conserva el diagnóstico. Para publicar posteriormente un modelo validado, el generador deberá retirar antes el programa del contexto, dejándolo en NULL.

`ir_emit` ya respeta transferencia de argumentos, primer fallo y cierre de bloque. Por alcance de I1, preserva los flags proporcionados, sin calcularlos ni validar todavía todos los tipos/firmas. Una rutina recién creada tiene entrada IR_NO_ID hasta que el recorrido la establezca.

### Construcción interna de I4 disponible

`ir_build_program` recibe un contexto nuevo y construye funciones globales y, si hay sentencias globales, un arranque que las ejecuta antes de llamar a main. Cada rutina tiene entrada y contador de temporales propios. Las llamadas usan la firma final prestada, argumentos evaluados en orden y efectos conservadores de memoria/fallo; los retornos cierran el camino.

Desde I6, `ir_generate` entrega el programa solo si `ir_validate` aprueba su estructura; en fallo libera la construcción parcial. Los casos none del modelo se comprueban con entradas internas preparadas porque el frontend actual no permite declarar rutinas none. No se amplía aquí el lenguaje fuente.

### Control de I5 disponible

`ir_stmt` construye bloques para when/fallback, alwhen y loop; break usa una pila de salidas de ciclos. Las condiciones enteras se convierten mediante comparación con cero. Los caminos cerrados no reciben instrucciones ni saltos adicionales. No se crea unión si ambas ramas terminan, ni avance de loop si su cuerpo no continúa.

En loop, evaluar fin antes de leer el control en cada prueba, igual que el backend actual. La inicialización y el avance conservan su almacenamiento sin comprobaciones de rango añadidas; el paso se mantiene como literal entero positivo y los operandos conservan sus tipos. Esto se distingue de ++/--, que sí solicitan comprobación. La salida se reserva antes del avance; el orden físico de bloques no determina el flujo.

`ir_emit_value` se comparte desde ir.c entre expresiones y control para reservar IDs temporales y emitir sus definiciones. Las cuatro interfaces públicas se conservan. No se construye CFG; I6 ya revisa la estructura antes de entregar IR.

### Efectos, revisión y entrega de I6 disponibles

`ir_emit` completa flags conservando los atributos solicitados. CHECKED implica MAY_TRAP; las sumas sin CHECKED del avance de loop no adquieren comprobación de rango. Las llamadas se consideran conservadoras también respecto al estado criptográfico. ADDR obtiene una dirección sin leer el contenido; LOAD/STORE y accesos a valores globales conservan sus efectos de memoria.

`ir_validate` usa un índice de definiciones temporal por rutina, con dos pasadas independientes del orden físico de bloques. Ese índice se libera al finalizar, no se incorpora a IrProgram. Revisa únicamente el alcance estructural de la sección 6, sin volver a exigir compatibilidad de firmas, promociones o resolución de símbolos.

`ir_generate` construye, revisa y entrega propiedad solo en éxito. Un fallo deja salida NULL y descarta recursos propios; un parámetro de salida ya ocupado se rechaza sin sobrescribir ni liberar el programa anterior. El diagnóstico es opcional, se copia por valor y conserva la primera posición de error. La integración con main y la exportación siguen pendientes.
