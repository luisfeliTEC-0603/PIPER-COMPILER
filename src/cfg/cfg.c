/** @file cfg.c
 * C1: almacenamiento de los grafos sobre IR prestada. No conecta bloques todavía.
 */
#include "cfg_internal.h"
#include <stdlib.h>

/* Un único descarte de reservas parciales y copia del primer error localizado.
 * program es propio o NULL; nunca pasar aquí un programa previo del llamador. */
static CfgStatus creation_error(CfgProgram *program, CfgDiagnostic *out,
                                CfgDiagnostic error, const char *message)
{
    cfg_program_free(program);
    snprintf(error.message, sizeof(error.message), "%s", message);
    if (out) *out = error;
    return error.status;
}

/** Reservar solo lo conocido por C1: rutinas y nodos. Los vecinos/aristas quedan
 * vacíos; C2 reservará sus tamaños al conocer las conexiones de terminadores.
 * Cada elemento se inicializa antes de que la limpieza pueda recorrerlo. */
CfgStatus cfg_program_create(const IrProgram *ir, CfgProgram **out,
                             CfgDiagnostic *diagnostic)
{
    CfgDiagnostic error = {.status = CFG_INVALID_INPUT,
        .routine = CFG_NO_INDEX, .block = IR_NO_ID,
        .instruction = CFG_NO_INDEX, .edge = CFG_NO_INDEX};
    if (out == NULL || *out != NULL || ir == NULL || ir->routine_count == 0 || ir->routines == NULL)
        return creation_error(NULL, diagnostic, error, "Se requiere IR validada y salida CFG inicialmente NULL.");
    error.status = CFG_OUT_OF_MEMORY;
    if (ir->routine_count > SIZE_MAX / sizeof(CfgRoutine))
        return creation_error(NULL, diagnostic, error, "Demasiadas rutinas para reservar CFG.");
    CfgProgram *program = calloc(1, sizeof(*program));
    if (program == NULL)
        return creation_error(NULL, diagnostic, error, "No se pudo reservar el programa CFG.");
    program->ir = ir;
    program->routines = calloc(ir->routine_count, sizeof(*program->routines));
    if (program->routines == NULL)
        return creation_error(program, diagnostic, error, "No se pudieron reservar las rutinas CFG.");
    program->routine_count = ir->routine_count;
    for (size_t r = 0; r < program->routine_count; r++) {
        const IrRoutine *source = &ir->routines[r];
        CfgRoutine *routine = &program->routines[r];
        error.routine = r;
        routine->routine = r;
        routine->entry = routine->exit = CFG_NO_INDEX;
        if (source->block_count == 0 || source->blocks == NULL) {
            error.status = CFG_INVALID_INPUT;
            return creation_error(program, diagnostic, error, "La rutina IR no tiene bloques.");
        }
        if (source->block_count > SIZE_MAX - 2 ||
            source->block_count + 2 > SIZE_MAX / sizeof(CfgNode))
            return creation_error(program, diagnostic, error, "Demasiados bloques para reservar nodos CFG.");
        size_t count = source->block_count + 2;
        routine->nodes = calloc(count, sizeof(*routine->nodes));
        if (routine->nodes == NULL)
            return creation_error(program, diagnostic, error, "No se pudieron reservar los nodos CFG.");
        routine->node_count = count;
        routine->entry = 0;
        routine->exit = count - 1;
        routine->nodes[0] = (CfgNode){.kind = CFG_NODE_ENTRY, .block = IR_NO_ID};
        for (size_t b = 0; b < source->block_count; b++)
            routine->nodes[b + 1] = (CfgNode){.kind = CFG_NODE_BLOCK, .block = source->blocks[b].id};
        routine->nodes[routine->exit] = (CfgNode){.kind = CFG_NODE_EXIT, .block = IR_NO_ID};
    }
    *out = program;
    if (diagnostic) *diagnostic = (CfgDiagnostic){.status = CFG_OK,
        .routine = CFG_NO_INDEX, .block = IR_NO_ID,
        .instruction = CFG_NO_INDEX, .edge = CFG_NO_INDEX};
    return CFG_OK;
}

/** Aceptar NULL y reservas parciales. Solo liberar datos propios: nunca IR,
 * sus bloques, sus instrucciones ni símbolos. Los vecinos se liberan aunque
 * C2 solo haya alcanzado a reservar una de las dos listas de un nodo. */
void cfg_program_free(CfgProgram *cfg)
{
    if (cfg == NULL) return;
    for (size_t r = 0; r < cfg->routine_count; r++) {
        CfgRoutine *routine = &cfg->routines[r];
        for (size_t n = 0; n < routine->node_count; n++) {
            free(routine->nodes[n].successors);
            free(routine->nodes[n].predecessors);
        }
        free(routine->nodes);
        free(routine->edges);
    }
    free(cfg->routines);
    free(cfg);
}
