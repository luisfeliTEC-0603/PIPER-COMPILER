/** Adaptador IR: solo aqui se interpretan operandos/opcodes del TAC.
 * Los simbolos en memoria no forman parte de este primer dominio de temporales.
 */
#include "liveness_internal.h"
#include <stdlib.h>

static void input_free(LiveInput *input)
{
    for (size_t routine_index = 0; routine_index < input->routine_count; routine_index++) {
        LiveInputRoutine *routine = &input->routines[routine_index];
        for (size_t block_index = 0; block_index < routine->block_count; block_index++)
            free(routine->blocks[block_index].instructions);
        free(routine->blocks);
        free(routine->use_storage);
    }
    free(input->routines);
}

/* Con use_ids=NULL solo cuenta; con almacenamiento tambien escribe los IDs.
 * Los usos repetidos se conservan: el solver los reune en sus bitsets.
 */
static size_t collect_instruction_uses(const IrInstruction *instruction, size_t *use_ids)
{
    size_t use_count = 0;
    IrOperand operands[] = {instruction->arg1, instruction->arg2};
    for (size_t operand_index = 0; operand_index < 2; operand_index++) {
        if (operands[operand_index].kind == IR_OPERAND_TEMP) {
            if (use_ids != NULL)
                use_ids[use_count] = operands[operand_index].value.temp;
            use_count++;
        }
    }
    if (instruction->op == IR_OP_CALL || instruction->op == IR_OP_INTRINSIC) {
        for (size_t argument_index = 0; argument_index < instruction->extra.call.argument_count;
             argument_index++) {
            IrOperand argument = instruction->extra.call.arguments[argument_index];
            if (argument.kind == IR_OPERAND_TEMP) {
                if (use_ids != NULL)
                    use_ids[use_count] = argument.value.temp;
                use_count++;
            }
        }
    }
    return use_count;
}

/* Primera pasada: medir para reservar una sola lista de usos por rutina. */
static LivenessStatus count_routine_uses(const IrRoutine *routine, size_t routine_index,
                                        size_t *total_uses, LivenessDiagnostic *diagnostic)
{
    size_t use_count = 0;
    for (size_t block_index = 0; block_index < routine->block_count; block_index++) {
        const IrBlock *block = &routine->blocks[block_index];
        for (size_t instruction_index = 0; instruction_index < block->instruction_count;
             instruction_index++) {
            size_t instruction_uses = collect_instruction_uses(&block->instructions[instruction_index], NULL);
            if (instruction_uses > SIZE_MAX - use_count)
                return live_report(diagnostic, LIVENESS_OUT_OF_MEMORY, routine_index, block->id,
                                   "Demasiados usos para liveness.");
            use_count += instruction_uses;
        }
    }
    *total_uses = use_count;
    return LIVENESS_OK;
}

static LivenessStatus allocate_routine_input(LiveInputRoutine *routine,
                                            const IrRoutine *source, size_t use_count,
                                            size_t routine_index, LivenessDiagnostic *diagnostic)
{
    if (source->block_count > SIZE_MAX / sizeof(*routine->blocks) ||
        use_count > SIZE_MAX / sizeof(*routine->use_storage))
        return live_report(diagnostic, LIVENESS_OUT_OF_MEMORY, routine_index, IR_NO_ID,
                           "La entrada de liveness excede el tamano representable.");
    routine->blocks = calloc(source->block_count, sizeof(*routine->blocks));
    if (use_count != 0)
        routine->use_storage = calloc(use_count, sizeof(*routine->use_storage));
    if (routine->blocks == NULL || (use_count != 0 && routine->use_storage == NULL))
        return live_report(diagnostic, LIVENESS_OUT_OF_MEMORY, routine_index, IR_NO_ID,
                           "No se pudo reservar la entrada de liveness.");
    /* Publicar el tamaño despues de reservar permite liberar entradas parciales. */
    routine->block_count = source->block_count;
    routine->value_count = source->temp_count;
    return LIVENESS_OK;
}

/* Segunda pasada: cada instruccion recibe un segmento de la lista de usos.
 * No hacer aritmetica con NULL cuando toda la rutina carece de usos TEMP.
 */
static void fill_block_transfers(LiveInputBlock *block, const IrBlock *source,
                                 size_t *use_storage, size_t *next_use)
{
    for (size_t instruction_index = 0; instruction_index < block->instruction_count;
         instruction_index++) {
        const IrInstruction *instruction = &source->instructions[instruction_index];
        LiveInputInstruction *transfer = &block->instructions[instruction_index];
        size_t *use_ids = use_storage == NULL ? NULL : use_storage + *next_use;
        size_t use_count = collect_instruction_uses(instruction, use_ids);

        transfer->definition = instruction->result.kind == IR_OPERAND_TEMP
            ? instruction->result.value.temp : IR_NO_ID;
        transfer->uses = use_count == 0 ? NULL : use_ids;
        transfer->use_count = use_count;
        *next_use += use_count;
    }
}

static LivenessStatus prepare_routine(LiveInputRoutine *target, const IrRoutine *source,
                                      const CfgRoutine *cfg, size_t routine_index,
                                      LivenessDiagnostic *diagnostic)
{
    size_t use_count;
    LivenessStatus status = count_routine_uses(source, routine_index, &use_count, diagnostic);
    if (status != LIVENESS_OK)
        return status;
    status = allocate_routine_input(target, source, use_count, routine_index, diagnostic);
    if (status != LIVENESS_OK)
        return status;

    size_t next_use = 0;
    for (size_t block_index = 0; block_index < source->block_count; block_index++) {
        LiveInputBlock *block = &target->blocks[block_index];
        const IrBlock *ir_block = &source->blocks[block_index];
        /* cfg_validate garantiza ENTRY, bloques IR, EXIT: nodo = indice + 1. */
        const CfgNode *node = &cfg->nodes[block_index + 1];
        block->id = ir_block->id;
        for (size_t successor_index = 0; successor_index < node->successor_count; successor_index++) {
            size_t successor = node->successors[successor_index];
            if (cfg->nodes[successor].kind == CFG_NODE_BLOCK)
                block->successors[block->successor_count++] = successor - 1;
        }
        if (ir_block->instruction_count > SIZE_MAX / sizeof(*block->instructions))
            return live_report(diagnostic, LIVENESS_OUT_OF_MEMORY, routine_index, block->id,
                               "Demasiadas instrucciones para la entrada de liveness.");
        block->instructions = calloc(ir_block->instruction_count, sizeof(*block->instructions));
        if (block->instructions == NULL)
            return live_report(diagnostic, LIVENESS_OUT_OF_MEMORY, routine_index, block->id,
                               "No se pudieron reservar las transferencias de liveness.");
        block->instruction_count = ir_block->instruction_count;
        fill_block_transfers(block, ir_block, target->use_storage, &next_use);
    }
    return LIVENESS_OK;
}

/* Validar una vez en el limite publico; las funciones internas confian en ello. */
static LivenessStatus validate_ir_and_cfg(const IrProgram *ir, const CfgProgram *cfg,
                                         LivenessDiagnostic *diagnostic)
{
    IrDiagnostic ir_error;
    IrStatus ir_status = ir_validate(ir, &ir_error);
    if (ir_status != IR_OK)
        return live_report(diagnostic, ir_status == IR_OUT_OF_MEMORY
            ? LIVENESS_OUT_OF_MEMORY : LIVENESS_INVALID_IR,
            ir_error.routine, ir_error.block, ir_error.message);
    CfgDiagnostic cfg_error;
    CfgStatus cfg_status = cfg_validate(cfg, &cfg_error);
    if (cfg_status != CFG_OK)
        return live_report(diagnostic, cfg_status == CFG_OUT_OF_MEMORY
            ? LIVENESS_OUT_OF_MEMORY : LIVENESS_INVALID_CFG,
            cfg_error.routine, cfg_error.block, cfg_error.message);
    return LIVENESS_OK;
}

static LivenessStatus prepare_input(LiveInput *input, const IrProgram *ir,
                                    const CfgProgram *cfg, LivenessDiagnostic *diagnostic)
{
    if (ir->routine_count > SIZE_MAX / sizeof(LiveInputRoutine))
        return live_report(diagnostic, LIVENESS_OUT_OF_MEMORY, SIZE_MAX, IR_NO_ID,
                           "Demasiadas rutinas para preparar liveness.");
    input->routines = calloc(ir->routine_count, sizeof(*input->routines));
    if (input->routines == NULL)
        return live_report(diagnostic, LIVENESS_OUT_OF_MEMORY, SIZE_MAX, IR_NO_ID,
                           "No se pudieron reservar las rutinas de entrada.");
    input->routine_count = ir->routine_count;
    for (size_t routine_index = 0; routine_index < input->routine_count; routine_index++) {
        LivenessStatus status = prepare_routine(&input->routines[routine_index],
            &ir->routines[routine_index], &cfg->routines[routine_index], routine_index, diagnostic);
        if (status != LIVENESS_OK)
            return status;
    }
    return LIVENESS_OK;
}

LivenessStatus liveness_analyze_ir(const IrProgram *ir, const CfgProgram *cfg,
                                 LivenessResult **out,
                                 LivenessDiagnostic *diagnostic)
{
    if (ir == NULL || cfg == NULL || cfg->ir != ir || out == NULL || *out != NULL)
        return live_report(diagnostic, LIVENESS_INVALID_INPUT, SIZE_MAX, IR_NO_ID,
                           "Se requiere IR/CFG correspondientes y salida NULL.");
    LivenessStatus status = validate_ir_and_cfg(ir, cfg, diagnostic);
    if (status != LIVENESS_OK)
        return status;

    LiveInput input = {0};
    status = prepare_input(&input, ir, cfg, diagnostic);
    if (status == LIVENESS_OK)
        status = live_solve(&input, out, diagnostic);
    /* Unico dueño de la entrada temporal, tanto en exito como en fallo. */
    input_free(&input);
    return status;
}
