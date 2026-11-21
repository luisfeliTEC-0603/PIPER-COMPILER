#ifndef PIPER_IR_UNROLL_H
#define PIPER_IR_UNROLL_H

/** @file ir_unroll.h
 * @brief Desenrollado de ciclos (loop unrolling) sobre el IR de Piper.
 *
 * @details Transforma loops contados con begin/end/step constantes en una
 * secuencia de copias del cuerpo, reduciendo las vueltas del header y dando
 * al scheduler VLIW más instrucciones independientes por ventana. Opera
 * exclusivamente sobre IR: no lee AST ni modifica símbolos. La entrada se
 * presta intacta. La salida es un IrProgram nuevo, validado, que el llamador
 * libera con ir_program_free. La tabla de símbolos y el AST deben seguir
 * vivos hasta liberar la salida.
 */

#include "ir.h"

/** @brief Modo de desenrollado. */

typedef enum {
    IR_UNROLL_OFF = 0,   /**< Copia el IR sin transformar ningún loop. */
    IR_UNROLL_AUTO,      /**< Heurística por loop según N y presupuesto. */
    IR_UNROLL_FACTOR     /**< Usa opts->factor como F para todos los loops. */
} IrUnrollMode;

/** Decisión del módulo por header candidato; no representa un error de IR. */
typedef enum {
    IR_UNROLL_APPLIED = 0,
    IR_UNROLL_NON_CONSTANT,
    IR_UNROLL_STRUCTURE,
    IR_UNROLL_EFFECTS,
    IR_UNROLL_INTERNAL_CYCLE,
    IR_UNROLL_CONTROL_MODIFIED,
    IR_UNROLL_EXTERNAL_TEMP,
    IR_UNROLL_ITERATION_LIMIT,
    IR_UNROLL_FACTOR_LIMIT,
    IR_UNROLL_BLOCK_BUDGET,
    IR_UNROLL_SIZE_LIMIT
} IrUnrollReason;

typedef struct {
    size_t routine;
    IrId block;
    SourceLocation location;
    size_t factor;
    uint64_t iterations; /**< Cero si el análisis no llegó a calcular N. */
    IrUnrollReason reason;
} IrUnrollLoopReport;

/** Vista prestada durante la llamada; copiarla para conservarla. No modificar
 * IR desde el callback. Si la pasada falla, sus decisiones son provisionales. */
typedef void (*IrUnrollObserver)(const IrUnrollLoopReport *report, void *context);

const char *ir_unroll_reason_name(IrUnrollReason reason);

/** @brief Opciones de ir_unroll.
 *
 * @details En max_iterations, max_unrolled_blocks y max_factor, 0 significa
 * sin techo. max_iterations acota N (no N*step); el desbordamiento aritmético
 * del límite del header se descarta por construcción (ver apply). 
 * max_unrolled_blocks limita los bloques de la RUTINA tras desenrollar y AUTO
 * baja el factor para respetarlo.
 */

typedef struct {
    IrUnrollMode mode;          /**< Modo de desenrollado. */
    size_t factor;              /**< F fijo; solo se usa si mode == IR_UNROLL_FACTOR. */
    size_t max_iterations;      /**< Techo de N; 0 = sin techo. */
    size_t max_unrolled_blocks; /**< Techo de bloques por rutina tras unroll; 0 = sin techo. */
    size_t max_factor;          /**< Techo de F en AUTO; 0 = sin techo. */
    IrUnrollObserver observer;  /**< Opcional; una decisión por loop candidato. */
    void *observer_context;
} IrUnrollOptions;

/** @brief Métricas de una ejecución de ir_unroll.
 *
 * @details iterations_before suma N de los loops desenrollados;
 * iterations_after suma N/F + N%F (vueltas del header + copias del epílogo).
 */

typedef struct {
    size_t loops_analyzed;      /**< Loops con forma candidata detectados. */
    size_t loops_unrolled;      /**< Loops transformados con éxito. */
    size_t loops_skipped;       /**< Loops rechazados (N variable, presupuesto, etc.). */
    size_t iterations_before;   /**< Suma de N de loops desenrollados. */
    size_t iterations_after;    /**< Suma de N/F + N%F de esos loops. */
    size_t instructions_before; /**< Instrucciones totales antes del unroll. */
    size_t instructions_after;  /**< Instrucciones totales después del unroll. */
    size_t blocks_before;       /**< Bloques totales antes del unroll. */
    size_t blocks_after;        /**< Bloques totales después del unroll. */
} IrUnrollStats;

/** @brief Opciones por defecto: AUTO, F=4, max_iterations=4096,
 * max_unrolled_blocks=256, max_factor=4.
 *
 * @return IrUnrollOptions inicializado.
 */

IrUnrollOptions ir_unroll_default_options(void);

/** @brief Desenrolla los loops contados del IR.
 *
 * @param ir     IR validado; se presta y no se modifica.
 * @param opts   Opciones; no puede ser NULL.
 * @param out    Salida; debe apuntar a NULL. En éxito recibe un IrProgram
 *               nuevo y validado, propiedad del llamador.
 * @param diag   Diagnóstico opcional; puede ser NULL.
 * @param stats  Métricas opcionales; puede ser NULL.
 *
 * @return IR_OK en éxito; IR_INVALID_INPUT si ir/opts/out son inválidos o
 *         *out no es NULL; IR_OUT_OF_MEMORY si falla una reserva; el estado
 *         propagado si ir_validate falla sobre ir o sobre la copia.
 *
 * @details En fallo *out queda NULL y se libera lo parcial. La entrada
 * nunca se libera ni se modifica.
 */

IrStatus ir_unroll(const IrProgram *ir, const IrUnrollOptions *opts,
                   IrProgram **out, IrDiagnostic *diag, IrUnrollStats *stats);

/** @brief Escribe un resumen legible de las métricas (para -v).
 *
 * @param output Stream destino; no se cierra.
 * @param stats  Métricas a imprimir.
 */

void ir_unroll_stats_write(FILE *output, const IrUnrollStats *stats);

#endif
