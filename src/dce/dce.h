#ifndef PIPER_DCE_H
#define PIPER_DCE_H

#include "ir.h"
#include "liveness/liveness.h"

typedef enum {
    DCE_OK = 0,
    DCE_ANALYSIS_ERROR,
    DCE_OUT_OF_MEMORY
} DceStatus;

typedef struct {
    size_t instructions_before;
    size_t instructions_after;
    size_t removed_instructions;
    size_t removed_routines;
    double optimization_seconds;  /* Tiempo CPU; negativo si no esta disponible. */
    double compilation_seconds;   /* Frontend -> IR optimizada; lo completa la CLI. */
} DceReport;

typedef struct {
    size_t removed_routines;
    size_t removed_instructions;
} DceRoutineReport;

/* Programa cerrado del frontend, con llamadas directas y entrada startup o
 * main. Marca las rutinas alcanzables por llamadas y elimina las demas sin
 * analizar efectos ni eliminar llamadas dentro de las rutinas conservadas.
 * Revisa todas las llamadas, incluso las de ramas que podrian no ejecutarse.
 *
 * Precondiciones: IR validada, simbolos vivos y report escribible. Las llamadas
 * alcanzables deben apuntar a rutinas del programa. No soporta entradas externas
 * ni llamadas indirectas. No consultar CFG/liveness anteriores tras una poda:
 * hay que liberarlos y reconstruirlos porque cambian los indices de rutinas.
 * En fallo: IR intacta y report en cero. ANALYSIS_ERROR indica entrada ausente
 * o una llamada alcanzable sin cuerpo IR; OUT_OF_MEMORY indica falta de espacio.
 */
DceStatus dce_remove_unreachable_routines(IrProgram *ir, DceRoutineReport *report);

/* Decide si se puede eliminar la instruccion; no modifica IR ni liveness.
 * La instruccion pertenece a una IR validada y live_after contiene los valores
 * necesarios despues de ella, considerando las eliminaciones ya decididas.
 * Por ahora solo cubre TEMP.
 */
bool dce_can_remove_instruction(const IrInstruction *instruction,
                                const LivenessSet *live_after);

/* Ejecuta DCE sobre temporales y locales escalares seguros hasta estabilizar
 * los bloques afectados. Globales, arreglos y simbolos expuestos se conservan.
 * Valida IR/CFG una vez y calcula liveness de las instrucciones necesarias
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

/* Mismo contrato que run_pass, con metricas. No elimina rutinas: ejecutar la
 * poda anterior y reconstruir CFG antes de llamar si se desean ambas pasadas.
 * report es obligatorio; se publica solo en exito. No confundir IR con ISA. */
DceStatus dce_optimize(IrProgram *ir, const CfgProgram *cfg, DceReport *report,
                        LivenessDiagnostic *analysis_diagnostic);
/* Escribe JSON en un stream prestado; no lo cierra. Metricas ISA no disponibles
 * quedan explicitamente en null hasta conectar la emision desde IR. */
bool dce_write_report(FILE *output, const DceReport *report);

#endif
