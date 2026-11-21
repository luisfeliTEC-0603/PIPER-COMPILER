/** @file codeOPgen.c
 * Unico recorrido del programa: rutinas → bloques → operaciones. Cada
 * selector elige la funcion que escribe UNA operacion; nunca vuelve al AST.
 * Los saltos explicitos conservan la semantica aunque el orden de bloques
 * no coincida con el flujo de ejecucion. El scheduler viene despues.
 */
#include "internal.h"
#include <string.h>

static void emit_operation(CodeOPgenContext *context, const RegallocMachineInstruction *operation)
{
    unsigned destination_register = operation->def == IR_NO_ID ? 0 : op_register(context, operation->def);
    unsigned source_register = operation->use1 == IR_NO_ID ? 0 : op_register(context, operation->use1);
    switch (operation->op) {
    case REGALLOC_M_IMM:
        op_immediate(context, destination_register, operation->immediate);
        break;
    case REGALLOC_M_MOVE:
        op_move(context, destination_register, source_register);
        break;
    case REGALLOC_M_ADD:
        op_emit(context, "sum r%u, r%u", destination_register, source_register);
        break;
    case REGALLOC_M_SUB:
        op_emit(context, "diff r%u, r%u", destination_register, source_register);
        break;
    case REGALLOC_M_MUL:
        op_emit(context, "mul r%u, r%u", destination_register, source_register);
        break;
    case REGALLOC_M_AND:
        op_emit(context, "and r%u, r%u", destination_register, source_register);
        break;
    case REGALLOC_M_OR:
        op_emit(context, "or r%u, r%u", destination_register, source_register);
        break;
    case REGALLOC_M_XOR:
        op_emit(context, "xor r%u, r%u", destination_register, source_register);
        break;
    case REGALLOC_M_EQ:
        op_emit(context, "eor r%u, r%u", destination_register, source_register);
        break;
    case REGALLOC_M_ADDI:
        op_emit(context, "sumi r%u, #%" PRId64, destination_register, operation->immediate);
        break;
    case REGALLOC_M_SUBI:
        op_emit(context, "diffi r%u, #%" PRId64, destination_register, operation->immediate);
        break;
    case REGALLOC_M_BOOL_NOT:
        op_emit(context, "xori r%u, #1", destination_register);
        break;
    case REGALLOC_M_LT: case REGALLOC_M_GT: op_compare(context, operation); break;
    case REGALLOC_M_CHECKED_COPY: case REGALLOC_M_CHECKED_NEG:
    case REGALLOC_M_CHECKED_ADD: case REGALLOC_M_CHECKED_SUB:
    case REGALLOC_M_CHECKED_MUL:
        op_checked(context, operation);
        break;
    case REGALLOC_M_DIV: case REGALLOC_M_MOD: case REGALLOC_M_POW:
        op_general_arithmetic(context, operation); break;
    case REGALLOC_M_LOAD_SYMBOL: case REGALLOC_M_STORE_SYMBOL:
    case REGALLOC_M_ADDR: case REGALLOC_M_CHECK_BOUNDS:
    case REGALLOC_M_LOAD_INDIRECT: case REGALLOC_M_STORE_INDIRECT:
    case REGALLOC_M_SPILL_LOAD: case REGALLOC_M_SPILL_STORE:
        op_memory(context, operation); break;
    case REGALLOC_M_CALL_SAVE: case REGALLOC_M_CALL_ARG:
    case REGALLOC_M_CALL_VOID: case REGALLOC_M_CALL_RESULT: op_call(context, operation); break;
    case REGALLOC_M_CRYPTO_WORD: case REGALLOC_M_CRYPTO_AUTH:
    case REGALLOC_M_CRYPTO_WRITE: case REGALLOC_M_CRYPTO_FEISTEL:
    case REGALLOC_M_CRYPTO_REVOKE:
        op_crypto(context, operation);
        break;
    case REGALLOC_M_JUMP:
        op_emit(context, "j piper_funcion_%zu_bloque_%zu", context->routine_index, operation->targets[0]); break;
    case REGALLOC_M_BRANCH:
        op_emit(context, "jnz r%u, piper_funcion_%zu_bloque_%zu", source_register, context->routine_index, operation->targets[0]);
        op_emit(context, "j piper_funcion_%zu_bloque_%zu", context->routine_index, operation->targets[1]);
        break;
    case REGALLOC_M_TO_RETURN:
        op_move(context, 1, source_register);
        break;
    case REGALLOC_M_RETURN:
        op_emit(context, "j piper_salida_funcion_%zu", context->routine_index);
        break;
    default: op_fail(context, CODEOPGEN_INVALID_MACHINE, "Opcode sin emisor ASM.");
    }
}

CodeOPgenResult codeOPgen_generate(const RegallocAssignedProgram *assigned,
                                  FILE *output, const CodeOPgenOptions *options,
                                  CodeOPgenDiagnostic *diagnostic)
{
    CodeOPgenContext context = {.assigned = assigned, .output = output,
        .routine_index = SIZE_MAX, .block_id = IR_NO_ID, .instruction_index = SIZE_MAX,
        .comments = options != NULL && options->comments};
    RegallocDiagnostic assignment_error;
    if (output == NULL || assigned == NULL) {
        op_fail(&context, CODEOPGEN_INVALID_INPUT, "Se requiere asignacion y stream de salida.");
    } else if (regalloc_assigned_program_validate(assigned, &assignment_error) != REGALLOC_OK) {
        context.routine_index = assignment_error.routine;
        context.block_id = assignment_error.block;
        op_fail(&context, CODEOPGEN_INVALID_MACHINE, assignment_error.message);
    } else {
        op_validate(&context);
    }
    if (context.error.status != CODEOPGEN_OK) goto done;
    const IrProgram *ir = assigned->machine->source;
    size_t entry = SIZE_MAX;
    for (size_t routine_index = 0; routine_index < assigned->routine_count; routine_index++) {
        size_t source_routine_index = assigned->machine->routines[routine_index].ir_routine;
        const Symbol *function = ir->routines[source_routine_index].symbol;
        if (source_routine_index == ir->startup || (ir->startup == IR_NO_ID && function != NULL &&
            strcmp(function->name, "main") == 0 && function->param_count == 0)) entry = routine_index;
    }
    if (entry == SIZE_MAX) {
        op_fail(&context, CODEOPGEN_INVALID_MACHINE, "Falta startup o main sin parametros.");
        goto done;
    }
    op_immediate(&context, 7, OP_STACK_TOP);
    op_emit(&context, "diffi r7, #%d", SYMBOL_CALL_RECORD_SIZE);
    op_store(&context, PIPER_TYPE_B32, 0, 0, 7); /* Continuacion 0: detener programa. */
    op_emit(&context, "j piper_funcion_%zu", entry);
    for (size_t routine_index = 0; routine_index < assigned->routine_count && context.error.status == CODEOPGEN_OK; routine_index++) {
        context.routine_index = routine_index;
        context.block_id = IR_NO_ID;
        context.instruction_index = SIZE_MAX;
        context.location = (SourceLocation){0};
        const RegallocMachineRoutine *routine = &assigned->machine->routines[routine_index];
        op_label(&context, "piper_funcion_%zu", routine_index);
        op_prepare_function(&context);
        op_emit(&context, "j piper_funcion_%zu_bloque_%zu", routine_index, op_source_routine(&context)->entry);
        for (size_t block_index = 0; block_index < routine->block_count; block_index++) {
            const RegallocMachineBlock *block = &routine->blocks[block_index];
            context.block_id = block->id;
            op_label(&context, "piper_funcion_%zu_bloque_%zu", routine_index, block->id);
            for (size_t instruction_index = 0; instruction_index < block->instruction_count; instruction_index++) {
                context.instruction_index = instruction_index;
                context.location = block->instructions[instruction_index].location;
                op_comment(&context, &block->instructions[instruction_index]);
                emit_operation(&context, &block->instructions[instruction_index]);
            }
        }
        op_emit_function_exit(&context);
    }
    op_finish(&context);
    if (ferror(output)) op_fail(&context, CODEOPGEN_IO_ERROR, "Fallo del stream ASM.");
done:
    if (diagnostic != NULL) *diagnostic = context.error;
    return context.error.status;
}
