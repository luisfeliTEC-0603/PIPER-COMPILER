# Contrato del generador de IR

Especificación para implementar; las APIs y operaciones siguientes todavía no existen. Plan: [todoIR.md](todoIR.md).

## 1. Entrada y precondiciones

| Entrada | Regla |
| --- | --- |
| `const AstNode *root` | Raíz `AST_PROGRAM` de un único archivo; `yyparse` terminó con 0 y produjo AST. |
| `const SymbolTable *symbols` | Tabla final de la misma ejecución; `analyze_semantics_with_trace` retornó 1. |
| Anotaciones | Usar tipos, dimensiones y ámbitos semánticos. Las referencias a declaraciones deben pertenecer a la tabla final. Para el cuerpo de rutina, tomar el ámbito de la rutina o `Symbol.function_scope`: el semántico visita su lista sin anotar necesariamente el nodo `AST_BLOCK` del cuerpo. |
| Control del flujo | `compile_piper` ejecuta los análisis una sola vez. `--check`, clones y consultas LL(1) conservan sus salidas actuales, sin generar IR. |

El generador no ejecuta análisis ni interpreta stdout/stderr. `main.c` comprueba los retornos antes de invocarlo.

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
| `IrProgram` | Símbolos referenciados, globales, rutinas y rutina de arranque opcional. |
| `IrSymbol` | ID estable, referencia prestada a `Symbol`, ámbito propietario, nombre, clase, tipo y dimensiones. |
| `IrRoutine` | ID, símbolo o marca de arranque sintético, retorno, parámetros ordenados, temporales, bloques y bloque de entrada. |
| `IrType` | Tipo Piper exacto o dirección interna de 32 bits; para direcciones, tipo de elemento y dimensiones cuando apliquen. |
| `IrOperand` | Variante: constante, símbolo, temporal o ausente. Constantes enteras conservan valor/signo sin truncarlo a `int`. |
| `IrInstruction` | ID, operación, operandos, resultado opcional, tipos de operandos/resultado, `SourceLocation` y efectos. |
| Datos adicionales | `CALL`: rutina y lista ordenada de argumentos; intrínsecas: identificador y argumentos; terminadores: destinos explícitos. |
| `IrBlock` | ID estable dentro de la rutina, instrucciones ordenadas y exactamente un terminador. |

IDs de símbolos únicos por programa; IDs de temporales, instrucciones y bloques únicos por rutina, asignados en orden determinista. Usar IDs al referenciar arreglos que puedan moverse por `realloc`. Variables de usuario pueden reasignarse; cada temporal tiene una única definición. No implementar SSA.

## 4. Operaciones y efectos

| Familia | Operaciones | Reglas |
| --- | --- | --- |
| Escalares | `COPY` | Temporal ← valor o símbolo escalar ← valor. Leer un símbolo crea un temporal que captura su valor en ese punto. |
| Aritmética | `ADD`, `SUB`, `MUL`, `DIV`, `MOD`, `POW`, `NEG` | Una operación por instrucción; conservar tipos y comprobaciones actuales mediante atributos, incluido `may_trap`. |
| Comparación/lógica | `EQ`, `NE`, `LT`, `LE`, `GT`, `GE`, `AND`, `OR`, `XOR`, `NOT`, `TO_BOOL` | Comparaciones conservan signo de sus operandos; resultado lógico normalizado a bool. `TO_BOOL` convierte condiciones enteras mediante comparación con cero. |
| Memoria | `ADDR`, `LOAD`, `STORE`, `CHECK_BOUNDS` | `ADDR` obtiene la dirección de datos, también para parámetros de arreglo. `LOAD`/`STORE` usan dirección y tipo de elemento. `CHECK_BOUNDS(index, length)` exige `0 <= index < length`; tiene `may_trap`. |
| Llamadas | `CALL`, `INTRINSIC` | Argumentos evaluados antes de emitir la llamada, resultado temporal solo si retorna valor. Funciones `none` no producen valor. |
| Control | `JUMP`, `BRANCH`, `RETURN` | Destinos de bloque explícitos; `BRANCH` tiene condición bool y destinos verdadero/falso; `RETURN` lleva valor compatible o ninguno. |

Por instrucción deben poder enumerarse lecturas y escrituras de símbolos/temporales. Guardar efectos de memoria (`read`, `write`), estado criptográfico y `may_trap`, calculados centralmente desde la operación y sus atributos. Llamadas sin análisis interprocedimental se consideran conservadoramente lectoras/escritoras de memoria y potencialmente fallidas. Las operaciones sobre símbolos globales también tienen efecto de memoria. Las locales escalares se representan por símbolo; las escrituras por direcciones conservan su posible alias.

Las comprobaciones y operaciones fallidas tienen salida de error implícita y no producen continuación normal cuando fallan. Esta fase no crea bloques de error ni elimina operaciones potencialmente fallidas. El backend futuro deberá reproducir el comportamiento de error actual, no sustituirlo por un retorno normal.

## 5. Reglas de traducción obligatorias

| Construcción | Regla |
| --- | --- |
| Identificador escalar | Emitir `t = COPY símbolo` al evaluarlo. No retrasar la lectura hasta después de otra expresión con efectos. |
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

Resolver tipos con `has_piper_type`/`has_element_piper_type` antes de leer el campo correspondiente. Si una operación entera carece de tipo exacto, reproducir `result_type`/`unsigned_operand` de `codegen_arithmetic.c`: B32 por defecto o uB32 para el valor constante que requiere ese rango. El contexto de asignación/argumento/retorno no debe estrechar arbitrariamente un cálculo intermedio. Registrar por separado tipo de operación y conversión al destino.

La identidad de usos, declaraciones, llamadas y control de `loop` se registra durante el semántico con referencias a símbolos finales, antes de que una búsqueda posterior pueda encontrar otra declaración. El generador no reconstruye esa resolución solamente a partir del nombre.

## 6. Bloques y validación

1. Construir bloques dentro de `ir_generate`; las etiquetas/destinos se resuelven antes de devolver el programa.
2. Cada bloque pertenece a una sola rutina y termina en `JUMP`, `BRANCH` o `RETURN`; no existe caída implícita entre bloques.
3. Un destino inicia bloque; después de una transferencia no se agrega otra instrucción al mismo bloque. Omitir sentencias sin continuación alcanzable durante el recorrido, sin aplicar DCE general.
4. Validar operandos, tipos, IDs, firma/retorno de llamadas, definición única de temporales, referencias existentes y destinos dentro de la misma rutina.
5. Verificar disponibilidad de temporales mediante el orden estructurado de emisión y su contexto de control: un valor creado solo en una rama no puede escapar a la unión; allí se consulta el símbolo reasignado. No exigir resultados de análisis de flujo futuros.
6. No calcular variables vivas, registros físicos, posiciones de spill ni dependencias de calendarización.

## 7. Propiedad y exportación

La IR posee arreglos, instrucciones, temporales, nombres auxiliares y listas de argumentos. AST y símbolos se prestan; la tabla debe vivir hasta terminar consumidores y liberar la IR. Orden de limpieza: consumidores → IR → AST → tabla. No conservar punteros al contexto temporal del semántico ni a la tabla descartada del parser.

El formato `.ir` imprime rutinas, declaraciones/tipos, bloques y TAC en orden estable. Usar nombres originales; solo ante ambigüedad añadir calificación de ámbito. Distinguir visualmente símbolos de usuario y temporales si sus nombres coinciden. Incluir atributos de comprobación/efectos en las operaciones donde sean necesarios para inspección.

Las APIs y la sintaxis son decisiones de este contrato. El enunciado §4 exige TAC exportable y útil para optimizar; §7.2 exige identificar bloques. La presentación, diapositivas 5–9 y 14, fundamenta operaciones y partición. La construcción de conexiones se especifica aparte en [todoCFG.md](todoCFG.md).
