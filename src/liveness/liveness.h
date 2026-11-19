#ifndef PIPER_LIVENESS_H
#define PIPER_LIVENESS_H

/** Analisis de vida independiente de optimizaciones y registros fisicos.
 * Cada resultado describe una instantanea: IR/CFG deben seguir vivos e intactos.
 * Antes de modificar instrucciones, terminar las consultas y liberar el resultado.
 * Se pueden compartir consultas; no se mantiene una cache global ni se detectan
 * modificaciones hechas directamente a las estructuras C prestadas.
 */
#include "cfg.h"

/* Declaracion adelantada: el adaptador de maquina comparte este resultado sin
 * hacer que el solver dependa de la estructura del backend. */
typedef struct RegallocMachineProgram RegallocMachineProgram;

typedef enum {
    LIVENESS_OK = 0,
    LIVENESS_INVALID_INPUT,
    LIVENESS_INVALID_IR,
    LIVENESS_INVALID_CFG,
    LIVENESS_OUT_OF_MEMORY
} LivenessStatus;

typedef struct {
    LivenessStatus status;
    size_t routine;          /* Indice de rutina; SIZE_MAX si no aplica. */
    IrId block;              /* ID IR de bloque; IR_NO_ID si no aplica. */
    char message[256];
} LivenessDiagnostic;

/* Tipos opacos: solo el modulo conoce sus campos y administra sus reservas. */
typedef struct LivenessResult LivenessResult;
typedef struct LivenessSet LivenessSet;

typedef enum {
    LIVENESS_USE, LIVENESS_DEF, LIVENESS_IN, LIVENESS_OUT
} LivenessBlockSet;
typedef enum {
    LIVENESS_BEFORE,
    LIVENESS_AFTER
} LivenessPoint;

/** Calcula una instantanea completa de temporales IR (no simbolos en memoria).
 * cfg debe pertenecer exactamente a ir. *out debe empezar en NULL; solo se
 * publica en exito. Acepta diagnostic=NULL y libera parciales ante cualquier fallo.
 * Los IDs de valor coinciden con los temporales de cada rutina; no se mezclan
 * entre rutinas. Valida IR/CFG aqui; los pasos internos confian en esa validacion.
 */
LivenessStatus liveness_analyze_ir(const IrProgram *ir, const CfgProgram *cfg,
                                 LivenessResult **out,
                                 LivenessDiagnostic *diagnostic);
/** Analiza IDs virtuales tras la bajada a instrucciones de maquina.
 * En operaciones de dos direcciones, def cuenta tambien como uso. Los bits
 * fixed_reads/fixed_writes son restricciones fisicas para el coloreador, no
 * IDs virtuales del conjunto; se procesaran por separado en su grafo.
 * Igual que con IR, el resultado debe liberarse antes de mutar machine. */
LivenessStatus liveness_analyze_machine(const RegallocMachineProgram *machine,
                                      LivenessResult **out,
                                      LivenessDiagnostic *diagnostic);
void liveness_result_free(LivenessResult *result);

/** Consultas sin modificar ni transferir propiedad. Los indices de bloque siguen
 * el orden fisico original; no son IDs IR. Una consulta fuera de rango devuelve
 * NULL, 0 o IR_NO_ID segun el tipo. Los conjuntos prestados mueren con result.
 */
size_t liveness_routine_count(const LivenessResult *result);
size_t liveness_value_count(const LivenessResult *result, size_t routine);
size_t liveness_block_count(const LivenessResult *result, size_t routine);
IrId liveness_block_id(const LivenessResult *result, size_t routine, size_t block);
size_t liveness_instruction_count(const LivenessResult *result, size_t routine,
                                  size_t block);
const LivenessSet *liveness_block_set(const LivenessResult *result, size_t routine,
                                     size_t block, LivenessBlockSet which);
const LivenessSet *liveness_instruction_set(const LivenessResult *result,
                                           size_t routine, size_t block,
                                           size_t instruction,
                                           LivenessPoint point);
bool liveness_set_contains(const LivenessSet *set, size_t value);

#endif
