# Implementación del generador de IR

Estado: estructura declarada, implementación pendiente. Contrato normativo: [contrato-IR.md](contrato-IR.md). Esta guía cubre AST validado → TAC tipado organizado en bloques básicos.

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

La estructura mantiene seis archivos C y dos encabezados; sigue sin cuerpos de funciones ni conexión con `main`.

| Módulo | Prioridad | Métodos y responsabilidad |
| --- | --- | --- |
| `include/ir.h` | Alta | Modelo compacto y cuatro entradas públicas: `ir_generate`, `ir_validate`, `ir_write`, `ir_program_free`. |
| `src/ir/ir_internal.h` | Alta | Contexto de generación y siete auxiliares compartidos; sin mapas ni metadatos semánticos duplicados. |
| `src/ir/ir.c` | Alta | `ir_new_block`, `ir_emit`, `ir_fail` y liberación. Un único emisor para instrucciones normales y terminadores. |
| `src/ir/ir_generate.c` | Alta | `ir_generate` e `ir_build_program`: registro/recorrido de rutinas y arranque, validación y publicación. |
| `src/ir/ir_expr.c` | Alta | `ir_expr` e `ir_target`: valores/destinos, incluyendo llamadas, intrínsecas e índices. Helpers específicos serán `static`. |
| `src/ir/ir_stmt.c` | Alta | `ir_stmt`: bloques léxicos, asignaciones, ramas, ciclos y retornos. Inicialización/copia de arreglos se resuelve aquí. |
| `src/ir/ir_validate.c` | Alta | `ir_validate`: invariantes del modelo, sin requerir CFG/liveness. |
| `src/ir/ir_print.c` | Alta | `ir_write`: cabeceras de tipos, TAC sencillo y comentarios con todos los flags activos. |

Las expresiones devuelven `IrOperand` directamente; los otros auxiliares modifican el contexto. `ctx->error.status` distingue ausencia válida de valor de un error y conserva el primer fallo. Los auxiliares dejan de emitir tras un error; el API público devuelve `IrStatus` y solo publica el modelo en éxito.

## 4. Secuencia de implementación

| ID | Prioridad | Trabajo | Regla de cierre |
| --- | --- | --- | --- |
| I1 | Alta | Implementar modelo y propiedad de memoria del contrato. | Creación/liberación segura, IDs estables y operandos tipados. |
| I2 | Alta | Transportar referencias semánticas finales. | Cada uso apunta a su declaración incluso con sombreado o declaraciones posteriores en el mismo bloque. No identificar símbolos por dirección de memoria asignada. |
| I3 | Alta | Literales, lecturas, asignación y expresiones escalares. | Evaluación izquierda a derecha; ninguna lectura queda diferida a través de una llamada que pueda modificarla. |
| I4 | Alta | Rutinas, llamadas y retornos. | Parámetros en orden; `none` sin resultado; declaraciones globales como datos y arranque explícito cuando haya sentencias globales. |
| I5 | Alta | `when`, `alwhen`, `loop` y `break`; finalizar bloques. | Destinos explícitos y terminadores únicos. Aplicar las restricciones iniciales del contrato para `loop` e incrementos. |
| I6 | Alta | Efectos, comprobaciones y validador. | Conservar errores de aritmética, división, exponente e índice del backend actual. No eliminar llamadas aunque su resultado no se use. |
| I7 | Media | Arreglos/matrices e intrínsecas. | Direcciones y parámetros por referencia; mantener orden por filas y efectos criptográficos. |
| I8 | Alta | CLI, impresión y Makefile. | Exportación solo IR o adicional sin cambiar el backend actual; fallos de E/S comunicados. |
| I9 | Alta | Ejecutar las tres pruebas de esta guía y construcción habitual. | IR esperada e invariantes correctas; comandos actuales conservan su comportamiento. |

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
    t1 = 3 * 4  ; comprueba rango, puede fallar
    t2 = t0 + t1 ; comprueba rango, puede fallar
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
