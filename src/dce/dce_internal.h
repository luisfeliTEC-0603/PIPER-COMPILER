#ifndef PIPER_DCE_INTERNAL_H
#define PIPER_DCE_INTERNAL_H

#include "dce.h"

/* IR/CFG y liveness corresponden a la misma version validada del programa.
 * remove tiene una marca inicialmente falsa por instruccion, en orden fisico
 * de rutinas/bloques/instrucciones. Solo se modifican las marcas, nunca la IR.
 * Devuelve false ante fallo de reserva; removed_count se publica solo en exito.
 */
bool dce_mark_dead_instructions(const IrProgram *ir, const CfgProgram *cfg,
                                const LivenessResult *liveness, bool *remove,
                                size_t *removed_count);

#endif
