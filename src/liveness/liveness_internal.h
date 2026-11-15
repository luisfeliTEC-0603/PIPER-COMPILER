#ifndef PIPER_LIVENESS_INTERNAL_H
#define PIPER_LIVENESS_INTERNAL_H

#include "liveness.h"

/** Entrada temporal construida por el adaptador y prestada al solver.
 * Valores: IDs densos dentro de cada rutina. Sucesores: indices de bloques,
 * no IDs IR. ENTRY/EXIT no se incluyen; cada bloque tiene hasta dos sucesores.
 * Cada instruccion lee sus usos antes de escribir su definicion.
 */
typedef struct {
    size_t definition;       /* IR_NO_ID si no hay definicion. */
    const size_t *uses;
    size_t use_count;
} LiveInputInstruction;

typedef struct {
    IrId id;
    size_t successors[2];
    size_t successor_count;
    LiveInputInstruction *instructions;
    size_t instruction_count;
} LiveInputBlock;

typedef struct {
    size_t value_count;
    LiveInputBlock *blocks;
    size_t block_count;
    /* Lista propia; los punteros uses de las instrucciones apuntan dentro. */
    size_t *use_storage;
} LiveInputRoutine;

typedef struct {
    LiveInputRoutine *routines;
    size_t routine_count;
} LiveInput;

LivenessStatus live_report(LivenessDiagnostic *diagnostic, LivenessStatus status,
                           size_t routine, IrId block, const char *message);
/* Precondiciones garantizadas por el adaptador: entrada completa, IDs/sucesores
 * validos, out != NULL y *out == NULL. El solver solo reporta fallos de memoria.
 * Al retornar ya no conserva punteros a input: el adaptador puede liberarlo.
 */
LivenessStatus live_solve(const LiveInput *input, LivenessResult **out,
                          LivenessDiagnostic *diagnostic);

#endif
