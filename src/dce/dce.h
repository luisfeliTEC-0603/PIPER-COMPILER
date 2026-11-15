#ifndef PIPER_DCE_H
#define PIPER_DCE_H

#include "ir.h"
#include "liveness/liveness.h"

typedef enum {
    DCE_OK = 0,
    DCE_ANALYSIS_ERROR,
    DCE_OUT_OF_MEMORY
} DceStatus;

/* Decide si se puede eliminar la instruccion; no modifica IR ni liveness.
 * La instruccion pertenece a una IR validada y live_after contiene los valores
 * necesarios despues de ella, considerando las eliminaciones ya decididas.
 * Por ahora solo cubre TEMP.
 */
bool dce_can_remove_instruction(const IrInstruction *instruction,
                                const LivenessSet *live_after);

/* Ejecuta DCE sobre temporales hasta estabilizar los bloques afectados.
 * Obtiene liveness una vez y resuelve la demanda de las instrucciones necesarias
 * mediante una cola sin duplicados. Los IN de trabajo parten vacios y crecen
 * hacia los predecesores; las eliminaciones se marcan al alcanzar el punto fijo.
 * No repite el analisis completo del programa ni decide con conjuntos provisionales.
 *
 * Precondiciones: ir es valida, cfg corresponde a esa misma IR y removed_count
 * apunta a un contador escribible. No conservar otros resultados de liveness
 * sobre esta IR al llamar: la pasada puede modificar sus instrucciones.
 *
 * En exito: elimina solo instrucciones descartables, preserva orden, bloques,
 * terminadores e IDs de temporales. El CFG sigue siendo valido. removed_count
 * recibe la cantidad eliminada; puede ser cero.
 * En fallo: IR intacta y removed_count=0. analysis_diagnostic es opcional y
 * describe el analisis de liveness; los fallos de reservas de trabajo se
 * indican mediante DCE_OUT_OF_MEMORY.
 */
DceStatus dce_run_pass(IrProgram *ir, const CfgProgram *cfg, size_t *removed_count,
                        LivenessDiagnostic *analysis_diagnostic);

#endif
