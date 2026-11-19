#include "dce.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    const Symbol *symbol;
    size_t routine;
} RoutineReference;

/* Mismo criterio que el mapa de locales: identidad de simbolo, no su nombre. */
static int compare_routines(const void *left, const void *right)
{
    uintptr_t a = (uintptr_t)((const RoutineReference *)left)->symbol;
    uintptr_t b = (uintptr_t)((const RoutineReference *)right)->symbol;
    return (a > b) - (a < b);
}

static size_t find_callee(const RoutineReference *references, size_t count,
                           const Symbol *symbol)
{
    uintptr_t identity = (uintptr_t)symbol;
    size_t first = 0, end = count;
    while (first < end) {
        size_t middle = first + (end - first) / 2;
        uintptr_t candidate = (uintptr_t)references[middle].symbol;
        if (identity == candidate)
            return references[middle].routine;
        if (identity < candidate)
            end = middle;
        else
            first = middle + 1;
    }
    return IR_NO_ID;
}

static size_t program_entry(const IrProgram *ir)
{
    if (ir->startup != IR_NO_ID)
        return ir->startup;
    for (size_t r = 0; r < ir->routine_count; r++)
        if (strcmp(ir->routines[r].symbol->name, "main") == 0)
            return r;
    return IR_NO_ID;
}

/* Cada rutina entra una sola vez en la cola. La recursion no agrega trabajo
 * repetido; tampoco hace falta almacenar por separado un grafo de llamadas. */
static bool mark_reachable_routines(const IrProgram *ir, size_t entry,
                                     const RoutineReference *references,
                                     bool *reachable, size_t *queue)
{
    size_t pending_end = 1;
    reachable[entry] = true;
    queue[0] = entry;
    for (size_t next = 0; next < pending_end; next++) {
        const IrRoutine *routine = &ir->routines[queue[next]];
        for (size_t b = 0; b < routine->block_count; b++) {
            const IrBlock *block = &routine->blocks[b];
            for (size_t i = 0; i < block->instruction_count; i++) {
                const IrInstruction *instruction = &block->instructions[i];
                if (instruction->op != IR_OP_CALL)
                    continue;
                size_t callee = find_callee(references, ir->routine_count,
                                             instruction->extra.call.callee);
                if (callee == IR_NO_ID)
                    return false;
                if (!reachable[callee]) {
                    reachable[callee] = true;
                    queue[pending_end++] = callee;
                }
            }
        }
    }
    return true;
}

/* Contar antes de modificar permite fallar sin publicar una poda parcial.
 * El total tambien garantiza que los conteos combinados con liveness caben. */
static bool count_removed(const IrProgram *ir, const bool *reachable,
                            DceRoutineReport *report)
{
    size_t total = 0;
    for (size_t r = 0; r < ir->routine_count; r++) {
        const IrRoutine *routine = &ir->routines[r];
        if (!reachable[r])
            report->removed_routines++;
        for (size_t b = 0; b < routine->block_count; b++) {
            size_t count = routine->blocks[b].instruction_count;
            if (count > SIZE_MAX - total)
                return false;
            total += count;
            if (!reachable[r])
                report->removed_instructions += count;
        }
    }
    return true;
}

static void compact_routines(IrProgram *ir, const bool *reachable)
{
    size_t kept_count = 0;
    for (size_t r = 0; r < ir->routine_count; r++) {
        IrRoutine *routine = &ir->routines[r];
        if (!reachable[r]) {
            ir_routine_dispose(routine);
            continue;
        }
        if (r == ir->startup)
            ir->startup = kept_count;
        if (kept_count != r) {
            ir->routines[kept_count] = *routine;
            *routine = (IrRoutine){0};
        }
        kept_count++;
    }
    ir->routine_count = kept_count;
}

DceStatus dce_remove_unreachable_routines(IrProgram *ir, DceRoutineReport *report)
{
    *report = (DceRoutineReport){0};
    size_t entry = program_entry(ir);
    if (entry == IR_NO_ID)
        return DCE_ANALYSIS_ERROR;

    DceStatus status = DCE_OUT_OF_MEMORY;
    RoutineReference *references = calloc(ir->routine_count, sizeof(*references));
    bool *reachable = calloc(ir->routine_count, sizeof(*reachable));
    size_t *queue = calloc(ir->routine_count, sizeof(*queue));
    if (references == NULL || reachable == NULL || queue == NULL)
        goto cleanup;

    for (size_t r = 0; r < ir->routine_count; r++)
        references[r] = (RoutineReference){ir->routines[r].symbol, r};
    qsort(references, ir->routine_count, sizeof(*references), compare_routines);
    if (!mark_reachable_routines(ir, entry, references, reachable, queue)) {
        status = DCE_ANALYSIS_ERROR;
        goto cleanup;
    }
    DceRoutineReport removed = {0};
    if (!count_removed(ir, reachable, &removed))
        goto cleanup;

    if (removed.removed_routines != 0)
        compact_routines(ir, reachable);
    *report = removed;
    status = DCE_OK;

cleanup:
    free(queue);
    free(reachable);
    free(references);
    return status;
}
