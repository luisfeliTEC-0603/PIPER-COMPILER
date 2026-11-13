/** @file regalloc.c
 * Preparacion del futuro backend desde IR, en tres pasos:
 * 1. comprobar que la IR y el CFG se refieren al mismo programa y son validos;
 * 2. reservar una entrada por rutina y bloque, conservando sus IDs reales;
 * 3. publicar la salida solo cuando todas las reservas terminan.
 *
 * Ninguna de estas operaciones cambia el programa Piper o emite ASM.
 */
#include "regalloc.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

/** Centraliza la salida de exito/error. No reserva memoria ni cambia *out;
 * por eso los caminos de fallo pueden limpiar primero y luego reportar. */
static RegallocStatus report(RegallocDiagnostic *diagnostic,
                             RegallocStatus status, size_t routine,
                             IrId block, const char *message)
{
    if (diagnostic != NULL) {
        *diagnostic = (RegallocDiagnostic){
            .status = status, .routine = routine, .block = block
        };
        snprintf(diagnostic->message, sizeof(diagnostic->message), "%s", message);
    }
    return status;
}

/** routine_count solo se establece despues de reservar routines. Cada bloque
 * aun no reservado permanece NULL gracias a calloc, de modo que esta misma
 * funcion limpia tambien los fallos ocurridos a mitad del bucle de rutinas. */
void regalloc_analysis_free(RegallocAnalysis *analysis)
{
    if (analysis == NULL) return;
    for (size_t r = 0; r < analysis->routine_count; r++)
        free(analysis->routines[r].blocks);
    free(analysis->routines);
    free(analysis);
}

/** Entrada R0. La identidad del puntero es necesaria: dos IR con el mismo texto
 * no comparten necesariamente los mismos Symbol ni los mismos bloques en memoria.
 * Validar antes de reservar evita construir mapas sobre IDs o aristas invalidos. */
RegallocStatus regalloc_prepare(const IrProgram *ir, const CfgProgram *cfg,
                               RegallocAnalysis **out,
                               RegallocDiagnostic *diagnostic)
{
    if (out == NULL || *out != NULL || ir == NULL || cfg == NULL || cfg->ir != ir)
        return report(diagnostic, REGALLOC_INVALID_INPUT, SIZE_MAX, IR_NO_ID,
                      "Se requiere IR/CFG correspondientes y salida inicialmente NULL.");

    /* IR verifica definiciones/tipos de temporales y terminadores. CFG verifica
     * que cada nodo BLOCK represente el bloque IR correspondiente. */
    IrDiagnostic ir_error;
    IrStatus ir_status = ir_validate(ir, &ir_error);
    if (ir_status != IR_OK)
        return report(diagnostic,
                      ir_status == IR_OUT_OF_MEMORY ? REGALLOC_OUT_OF_MEMORY : REGALLOC_INVALID_IR,
                      ir_error.routine, ir_error.block, ir_error.message);

    CfgDiagnostic cfg_error;
    CfgStatus cfg_status = cfg_validate(cfg, &cfg_error);
    if (cfg_status != CFG_OK)
        return report(diagnostic,
                      cfg_status == CFG_OUT_OF_MEMORY ? REGALLOC_OUT_OF_MEMORY : REGALLOC_INVALID_CFG,
                      cfg_error.routine, cfg_error.block, cfg_error.message);

    if (ir->routine_count > SIZE_MAX / sizeof(RegallocRoutine))
        return report(diagnostic, REGALLOC_OUT_OF_MEMORY, SIZE_MAX, IR_NO_ID,
                      "Demasiadas rutinas para preparar la asignacion.");

    /* Todo lo reservado desde aqui pertenece al analisis. Los prestamos ir/cfg
     * solo se copian como punteros y nunca se liberan en un camino de fallo. */
    RegallocAnalysis *analysis = calloc(1, sizeof(*analysis));
    if (analysis == NULL)
        return report(diagnostic, REGALLOC_OUT_OF_MEMORY, SIZE_MAX, IR_NO_ID,
                      "No se pudo reservar el analisis de registros.");
    analysis->ir = ir;
    analysis->cfg = cfg;
    analysis->routines = calloc(ir->routine_count, sizeof(*analysis->routines));
    if (analysis->routines == NULL) {
        regalloc_analysis_free(analysis);
        return report(diagnostic, REGALLOC_OUT_OF_MEMORY, SIZE_MAX, IR_NO_ID,
                      "No se pudieron reservar las rutinas del analisis.");
    }
    /* A partir de aqui free puede recorrer todas las entradas, incluso las que
     * aun no se llenaron: calloc las dejo inicializadas en cero. */
    analysis->routine_count = ir->routine_count;

    for (size_t r = 0; r < analysis->routine_count; r++) {
        const IrRoutine *source = &ir->routines[r];
        const CfgRoutine *graph = &cfg->routines[r];
        RegallocRoutine *target = &analysis->routines[r];
        target->ir_routine = r;
        target->temp_count = source->temp_count;
        if (source->block_count > SIZE_MAX / sizeof(*target->blocks)) {
            regalloc_analysis_free(analysis);
            return report(diagnostic, REGALLOC_OUT_OF_MEMORY, r, IR_NO_ID,
                          "Demasiados bloques para preparar la asignacion.");
        }
        target->blocks = calloc(source->block_count, sizeof(*target->blocks));
        if (target->blocks == NULL) {
            regalloc_analysis_free(analysis);
            return report(diagnostic, REGALLOC_OUT_OF_MEMORY, r, IR_NO_ID,
                          "No se pudieron reservar los bloques del analisis.");
        }
        target->block_count = source->block_count;
        /* cfg_validate garantiza el orden ENTRY, bloques IR, EXIT. Por eso la
         * posicion fisica b corresponde al nodo entry+1+b. Guardamos tambien
         * el ID original: en el caso B9 ubicado primero, cfg_node vale 1,
         * mientras id sigue siendo 9. */
        for (size_t b = 0; b < target->block_count; b++) {
            target->blocks[b].id = source->blocks[b].id;
            target->blocks[b].cfg_node = graph->entry + 1 + b;
        }
    }
    /* Publicacion atomica: hasta esta linea *out permanecio NULL. */
    *out = analysis;
    return report(diagnostic, REGALLOC_OK, SIZE_MAX, IR_NO_ID, "");
}
