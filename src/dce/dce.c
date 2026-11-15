#include "dce_internal.h"
#include <stdlib.h>

bool dce_can_remove_instruction(const IrInstruction *instruction,
                                const LivenessSet *live_after)
{
    /* Una escritura a simbolo no se decide con liveness de temporales. */
    if (instruction->result.kind != IR_OPERAND_TEMP)
        return false;

    /* Conservar efectos y comprobaciones aunque nadie utilice el resultado. */
    if (instruction->flags != 0)
        return false;

    switch (instruction->op) {
    case IR_OP_COPY:
    case IR_OP_ADDR:
    case IR_OP_ADD: case IR_OP_SUB: case IR_OP_MUL: case IR_OP_NEG:
    case IR_OP_EQ: case IR_OP_NE: case IR_OP_LT:
    case IR_OP_LE: case IR_OP_GT: case IR_OP_GE:
    case IR_OP_AND: case IR_OP_OR: case IR_OP_XOR: case IR_OP_NOT:
        return !liveness_set_contains(live_after, instruction->result.value.temp);
    default:
        /* Memoria, llamadas, control y operaciones con posibles fallos quedan
         * fuera de la lista, incluso si sus flags no anuncian esos efectos. */
        return false;
    }
}

/* Una marca por instruccion, en orden de rutina, bloque e instruccion. */
static bool count_instructions(const IrProgram *ir, size_t *total)
{
    size_t count = 0;
    for (size_t r = 0; r < ir->routine_count; r++) {
        const IrRoutine *routine = &ir->routines[r];
        for (size_t b = 0; b < routine->block_count; b++) {
            size_t block_instruction_count = routine->blocks[b].instruction_count;
            if (block_instruction_count > SIZE_MAX - count)
                return false;
            count += block_instruction_count;
        }
    }
    *total = count;
    return true;
}

static void compact_instructions(IrProgram *ir, const bool *remove)
{
    size_t mark_index = 0;
    for (size_t r = 0; r < ir->routine_count; r++) {
        IrRoutine *routine = &ir->routines[r];
        for (size_t b = 0; b < routine->block_count; b++) {
            IrBlock *block = &routine->blocks[b];
            size_t kept_count = 0;
            for (size_t i = 0; i < block->instruction_count; i++) {
                if (remove[mark_index++])
                    continue;
                if (kept_count != i)
                    block->instructions[kept_count] = block->instructions[i];
                kept_count++;
            }
            /* Las instrucciones eliminables no poseen memoria dinamica.
             * Las conservadas se mueven con sus operandos, sin duplicar reservas. */
            block->instruction_count = kept_count;
        }
    }
}

DceStatus dce_run_pass(IrProgram *ir, const CfgProgram *cfg, size_t *removed_count,
                        LivenessDiagnostic *analysis_diagnostic)
{
    *removed_count = 0;
    LivenessResult *liveness = NULL;
    bool *remove = NULL;
    DceStatus status = DCE_OUT_OF_MEMORY;

    LivenessStatus analysis_status = liveness_analyze_ir(ir, cfg, &liveness, analysis_diagnostic);
    if (analysis_status != LIVENESS_OK) {
        status = analysis_status == LIVENESS_OUT_OF_MEMORY ? DCE_OUT_OF_MEMORY : DCE_ANALYSIS_ERROR;
        goto cleanup;
    }

    size_t instruction_count;
    if (!count_instructions(ir, &instruction_count))
        goto cleanup;
    remove = calloc(instruction_count, sizeof(*remove));
    if (remove == NULL)
        goto cleanup;

    size_t removed;
    if (!dce_mark_dead_instructions(ir, cfg, liveness, remove, &removed))
        goto cleanup;
    /* Terminar todas las consultas antes de modificar la IR que describen. */
    liveness_result_free(liveness);
    liveness = NULL;

    if (removed != 0)
        compact_instructions(ir, remove);
    *removed_count = removed;
    status = DCE_OK;

cleanup:
    free(remove);
    liveness_result_free(liveness);
    return status;
}
