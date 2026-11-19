#include "dce_internal.h"
#include <stdlib.h>
#include <time.h>

static bool is_discardable_operation(const IrInstruction *instruction)
{
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
        return true;
    default:
        /* Memoria, llamadas, control y operaciones con posibles fallos quedan
         * fuera de la lista, incluso si sus flags no anuncian esos efectos. */
        return false;
    }
}

bool dce_can_remove_instruction(const IrInstruction *instruction,
                                const LivenessSet *live_after)
{
    return instruction->result.kind == IR_OPERAND_TEMP &&
        is_discardable_operation(instruction) &&
        !liveness_set_contains(live_after, instruction->result.value.temp);
}

bool dce_can_remove_mapped_instruction(const IrInstruction *instruction,
                                       const LivenessSet *live_after,
                                       const DceValueMap *values)
{
    size_t defined = dce_value_id(values, instruction->result);
    return defined != IR_NO_ID && is_discardable_operation(instruction) &&
        !liveness_set_contains(live_after, defined);
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
    bool *remove = NULL;
    DceStatus status = DCE_OUT_OF_MEMORY;

    LivenessStatus analysis_status = liveness_validate_ir(ir, cfg, analysis_diagnostic);
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
    if (!dce_mark_dead_instructions(ir, cfg, remove, &removed))
        goto cleanup;
    /* La cola ya libero sus conjuntos de trabajo. Solo quedan las marcas. */

    if (removed != 0)
        compact_instructions(ir, remove);
    *removed_count = removed;
    status = DCE_OK;

cleanup:
    free(remove);
    return status;
}

DceStatus dce_optimize(IrProgram *ir, const CfgProgram *cfg, DceReport *report,
                        LivenessDiagnostic *analysis_diagnostic)
{
    *report = (DceReport){0};
    size_t before;
    if (!count_instructions(ir, &before))
        return DCE_OUT_OF_MEMORY;
    clock_t start = clock();
    size_t removed;
    DceStatus status = dce_run_pass(ir, cfg, &removed, analysis_diagnostic);
    clock_t end = clock();
    if (status == DCE_OK) {
        *report = (DceReport){.instructions_before = before,
            .instructions_after = before - removed, .removed_instructions = removed,
            .compilation_seconds = -1.0,
            .optimization_seconds = start == (clock_t)-1 || end == (clock_t)-1
                ? -1.0 : (double)(end - start) / CLOCKS_PER_SEC};
    }
    return status;
}

bool dce_write_report(FILE *output, const DceReport *report)
{
    fprintf(output, "{\n  \"pass\": \"dce\",\n  \"representation\": \"typed_ir\",\n"
        "  \"instructions_before\": %zu,\n  \"instructions_after\": %zu,\n"
        "  \"removed_instructions\": %zu,\n  \"removed_routines\": %zu,\n"
        "  \"optimization_cpu_seconds\": ",
        report->instructions_before, report->instructions_after,
        report->removed_instructions, report->removed_routines);
    if (report->optimization_seconds < 0)
        fputs("null", output);
    else
        fprintf(output, "%.9f", report->optimization_seconds);
    fputs(",\n  \"compilation_cpu_seconds\": ", output);
    if (report->compilation_seconds < 0) fputs("null", output);
    else fprintf(output, "%.9f", report->compilation_seconds);
    fputs(",\n  \"target_code_bytes\": null,\n  \"simulator_cycles\": null,\n"
        "  \"target_metrics_status\": \"requires_backend_consuming_optimized_ir\"\n}\n", output);
    return !ferror(output);
}
