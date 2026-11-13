/** @file machine.c
 * Primera bajada escalar de IR a instrucciones virtuales de dos direcciones.
 *
 * Las operaciones binarias de PORYGON modifican su primer registro. Por eso
 * primero copiamos/materializamos el operando izquierdo en el resultado y
 * despues emitimos ADD/SUB/AND/OR/XOR que leen y redefinen ese resultado.
 * Los operandos constantes y simbolos que necesitan registro generan IDs
 * nuevos a partir de ir_temp_count. El control conserva los IDs de bloque.
 *
 * Esta fase no emite ASM. Rechaza opcodes y efectos cuya expansion fisica aun
 * no esta modelada; publicar un resultado incompleto seria peligroso.
 */
#include "regalloc_machine.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    RegallocMachineRoutine *routine;
    RegallocMachineBlock *block;
    size_t capacity;
    size_t origin;
    size_t routine_index;
    IrId block_id;
    RegallocDiagnostic *diagnostic;
} LowerContext;

static RegallocStatus machine_report(RegallocDiagnostic *diagnostic,
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

void regalloc_machine_program_free(RegallocMachineProgram *program)
{
    if (program == NULL) return;
    for (size_t r = 0; r < program->routine_count; r++) {
        RegallocMachineRoutine *routine = &program->routines[r];
        for (size_t b = 0; b < routine->block_count; b++)
            free(routine->blocks[b].instructions);
        free(routine->blocks);
    }
    free(program->routines);
    free(program);
}

/** La capacidad es cuatro veces las instrucciones IR del bloque: COPY ocupa
 * como maximo 2, binaria 3, BRANCH 2 y RETURN 3. Se comprueba antes de reservar.
 * No se deja una instruccion a medias si la capacidad se agota. */
static RegallocStatus emit(LowerContext *ctx, RegallocMachineInstruction in)
{
    if (ctx->block->instruction_count >= ctx->capacity)
        return machine_report(ctx->diagnostic, REGALLOC_INVALID_IR,
                              ctx->routine_index, ctx->block_id,
                              "La bajada escalar excedio su capacidad prevista.");
    in.origin_instruction = ctx->origin;
    ctx->block->instructions[ctx->block->instruction_count++] = in;
    return REGALLOC_OK;
}

static RegallocStatus new_temp(LowerContext *ctx, IrId *out)
{
    if (ctx->routine->temp_count == SIZE_MAX)
        return machine_report(ctx->diagnostic, REGALLOC_OUT_OF_MEMORY,
                              ctx->routine_index, ctx->block_id,
                              "Se agotaron los IDs de temporales virtuales.");
    *out = ctx->routine->temp_count++;
    return REGALLOC_OK;
}

static bool scalar(IrOperand value)
{
    if (value.kind == IR_OPERAND_NONE || value.type.address ||
        value.type.piper == PIPER_TYPE_NONE) return false;
    return value.kind != IR_OPERAND_SYMBOL ||
           (value.value.symbol != NULL && !value.value.symbol->is_array);
}

/** Produce un registro virtual con el valor solicitado. Si preferred es un
 * temporal, se define alli; si no, se reutiliza un operando TEMP o se crea un
 * auxiliar nuevo. El tipo de LOAD_SYMBOL queda disponible para la emision
 * tipada futura. */
static RegallocStatus materialize(LowerContext *ctx, IrOperand value,
                                 IrId preferred, IrId *out,
                                 SourceLocation location)
{
    if (!scalar(value))
        return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                              ctx->routine_index, ctx->block_id,
                              "R2 solo materializa operandos escalares.");
    if (value.kind == IR_OPERAND_TEMP && preferred == IR_NO_ID) {
        *out = value.value.temp;
        return REGALLOC_OK;
    }
    IrId target = preferred;
    RegallocStatus status = REGALLOC_OK;
    if (target == IR_NO_ID) status = new_temp(ctx, &target);
    if (status != REGALLOC_OK) return status;

    RegallocMachineInstruction in = {
        .def = target, .use1 = IR_NO_ID, .type = value.type,
        .location = location
    };
    switch (value.kind) {
    case IR_OPERAND_TEMP:
        if (target == value.value.temp) { *out = target; return REGALLOC_OK; }
        in.op = REGALLOC_M_MOVE;
        in.use1 = value.value.temp;
        break;
    case IR_OPERAND_CONSTANT:
        in.op = REGALLOC_M_IMM;
        in.immediate = value.value.constant;
        break;
    case IR_OPERAND_SYMBOL:
        in.op = REGALLOC_M_LOAD_SYMBOL;
        in.symbol = value.value.symbol;
        in.flags = IR_READ_MEMORY;
        break;
    default:
        return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                              ctx->routine_index, ctx->block_id,
                              "Operando escalar no reconocido.");
    }
    status = emit(ctx, in);
    if (status == REGALLOC_OK) *out = target;
    return status;
}

static RegallocStatus lower_copy(LowerContext *ctx, const IrInstruction *source)
{
    if (source->flags != 0 || !scalar(source->result))
        return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                              ctx->routine_index, ctx->block_id,
                              "COPY con efecto especial o destino no escalar.");
    IrId target = source->result.kind == IR_OPERAND_TEMP
        ? source->result.value.temp : IR_NO_ID;
    IrId value;
    RegallocStatus status = materialize(ctx, source->arg1, target, &value,
                                        source->location);
    if (status != REGALLOC_OK || source->result.kind == IR_OPERAND_TEMP)
        return status;
    if (source->result.kind != IR_OPERAND_SYMBOL)
        return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                              ctx->routine_index, ctx->block_id,
                              "COPY solo escribe temporales o simbolos.");
    return emit(ctx, (RegallocMachineInstruction){
        .op = REGALLOC_M_STORE_SYMBOL, .def = IR_NO_ID, .use1 = value,
        .symbol = source->result.value.symbol, .type = source->result.type,
        .flags = IR_WRITE_MEMORY, .location = source->location
    });
}

static RegallocStatus lower_binary(LowerContext *ctx, const IrInstruction *source,
                                   RegallocMachineOp op)
{
    if (source->flags != 0 || source->result.kind != IR_OPERAND_TEMP ||
        !scalar(source->result) || !scalar(source->arg1) ||
        !scalar(source->arg2))
        return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                              ctx->routine_index, ctx->block_id,
                              "Binaria con comprobacion/efecto o tipo no soportado en R2.");
    IrId target = source->result.value.temp;
    /* La IR generada usa un resultado fresco. Rechazar una reasignacion aqui
     * evita destruir un operando antes de consumirlo en la forma de 2 registros. */
    if ((source->arg1.kind == IR_OPERAND_TEMP &&
         source->arg1.value.temp == target) ||
        (source->arg2.kind == IR_OPERAND_TEMP &&
         source->arg2.value.temp == target))
        return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                              ctx->routine_index, ctx->block_id,
                              "La binaria requiere un temporal de resultado fresco.");
    IrId left, right;
    RegallocStatus status = materialize(ctx, source->arg2, IR_NO_ID, &right,
                                        source->location);
    if (status != REGALLOC_OK) return status;
    status = materialize(ctx, source->arg1, target, &left, source->location);
    if (status != REGALLOC_OK) return status;
    return emit(ctx, (RegallocMachineInstruction){
        .op = op, .def = left, .use1 = right, .type = source->result.type,
        .location = source->location
    });
}

static RegallocStatus lower_control(LowerContext *ctx,
                                    const IrInstruction *source)
{
    if (source->flags != 0)
        return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                              ctx->routine_index, ctx->block_id,
                              "Control con efecto especial no soportado en R2.");
    if (source->op == IR_OP_JUMP)
        return emit(ctx, (RegallocMachineInstruction){
            .op = REGALLOC_M_JUMP, .def = IR_NO_ID, .use1 = IR_NO_ID,
            .targets = {source->extra.targets[0], IR_NO_ID},
            .location = source->location
        });
    if (source->op == IR_OP_BRANCH) {
        IrId condition;
        RegallocStatus status = materialize(ctx, source->arg1, IR_NO_ID,
                                            &condition, source->location);
        if (status != REGALLOC_OK) return status;
        return emit(ctx, (RegallocMachineInstruction){
            .op = REGALLOC_M_BRANCH, .def = IR_NO_ID, .use1 = condition,
            .targets = {source->extra.targets[0], source->extra.targets[1]},
            .type = source->arg1.type, .location = source->location
        });
    }
    unsigned reads = 0;
    if (source->arg1.kind != IR_OPERAND_NONE) {
        IrId value;
        RegallocStatus status = materialize(ctx, source->arg1, IR_NO_ID,
                                            &value, source->location);
        if (status != REGALLOC_OK) return status;
        status = emit(ctx, (RegallocMachineInstruction){
            .op = REGALLOC_M_TO_RETURN, .def = IR_NO_ID, .use1 = value,
            .fixed_writes = REGALLOC_PHYS_R1, .type = source->arg1.type,
            .location = source->location
        });
        if (status != REGALLOC_OK) return status;
        reads = REGALLOC_PHYS_R1;
    }
    return emit(ctx, (RegallocMachineInstruction){
        .op = REGALLOC_M_RETURN, .def = IR_NO_ID, .use1 = IR_NO_ID,
        .fixed_reads = reads, .location = source->location
    });
}

static RegallocStatus lower_instruction(LowerContext *ctx,
                                        const IrInstruction *source)
{
    switch (source->op) {
    case IR_OP_COPY: return lower_copy(ctx, source);
    case IR_OP_ADD: return lower_binary(ctx, source, REGALLOC_M_ADD);
    case IR_OP_SUB: return lower_binary(ctx, source, REGALLOC_M_SUB);
    case IR_OP_AND: return lower_binary(ctx, source, REGALLOC_M_AND);
    case IR_OP_OR:  return lower_binary(ctx, source, REGALLOC_M_OR);
    case IR_OP_XOR: return lower_binary(ctx, source, REGALLOC_M_XOR);
    case IR_OP_JUMP: case IR_OP_BRANCH: case IR_OP_RETURN:
        return lower_control(ctx, source);
    default:
        return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                              ctx->routine_index, ctx->block_id,
                              "Opcode IR aun sin bajada escalar en R2.");
    }
}

RegallocStatus regalloc_lower_scalar(const RegallocAnalysis *analysis,
                                    RegallocMachineProgram **out,
                                    RegallocDiagnostic *diagnostic)
{
    if (analysis == NULL || analysis->ir == NULL || analysis->cfg == NULL ||
        analysis->routines == NULL || out == NULL || *out != NULL)
        return machine_report(diagnostic, REGALLOC_INVALID_INPUT, SIZE_MAX,
                              IR_NO_ID, "Se requiere un analisis preparado y salida NULL.");
    const IrProgram *source = analysis->ir;
    if (analysis->routine_count > SIZE_MAX / sizeof(RegallocMachineRoutine))
        return machine_report(diagnostic, REGALLOC_OUT_OF_MEMORY, SIZE_MAX,
                              IR_NO_ID, "Demasiadas rutinas de maquina.");
    RegallocMachineProgram *program = calloc(1, sizeof(*program));
    if (program == NULL)
        return machine_report(diagnostic, REGALLOC_OUT_OF_MEMORY, SIZE_MAX,
                              IR_NO_ID, "No se pudo reservar el programa de maquina.");
    program->source = source;
    program->routines = calloc(analysis->routine_count,
                               sizeof(*program->routines));
    if (program->routines == NULL) {
        regalloc_machine_program_free(program);
        return machine_report(diagnostic, REGALLOC_OUT_OF_MEMORY, SIZE_MAX,
                              IR_NO_ID, "No se pudieron reservar las rutinas de maquina.");
    }
    program->routine_count = analysis->routine_count;

    for (size_t r = 0; r < program->routine_count; r++) {
        const IrRoutine *ir_routine = &source->routines[r];
        RegallocMachineRoutine *target = &program->routines[r];
        target->ir_routine = r;
        target->ir_temp_count = target->temp_count = ir_routine->temp_count;
        if (ir_routine->block_count > SIZE_MAX / sizeof(*target->blocks)) {
            regalloc_machine_program_free(program);
            return machine_report(diagnostic, REGALLOC_OUT_OF_MEMORY, r,
                                  IR_NO_ID, "Demasiados bloques de maquina.");
        }
        target->blocks = calloc(ir_routine->block_count,
                                sizeof(*target->blocks));
        if (target->blocks == NULL) {
            regalloc_machine_program_free(program);
            return machine_report(diagnostic, REGALLOC_OUT_OF_MEMORY, r,
                                  IR_NO_ID, "No se pudieron reservar los bloques de maquina.");
        }
        target->block_count = ir_routine->block_count;
        for (size_t b = 0; b < target->block_count; b++) {
            const IrBlock *input = &ir_routine->blocks[b];
            RegallocMachineBlock *output = &target->blocks[b];
            output->id = analysis->routines[r].blocks[b].id;
            output->cfg_node = analysis->routines[r].blocks[b].cfg_node;
            if (input->instruction_count >
                SIZE_MAX / 4 / sizeof(*output->instructions)) {
                regalloc_machine_program_free(program);
                return machine_report(diagnostic, REGALLOC_OUT_OF_MEMORY, r,
                                      input->id, "Demasiadas instrucciones de maquina.");
            }
            size_t capacity = input->instruction_count * 4;
            output->instructions = calloc(capacity, sizeof(*output->instructions));
            if (output->instructions == NULL) {
                regalloc_machine_program_free(program);
                return machine_report(diagnostic, REGALLOC_OUT_OF_MEMORY, r,
                                      input->id, "No se pudieron reservar las instrucciones de maquina.");
            }
            LowerContext ctx = {.routine = target, .block = output,
                .capacity = capacity, .routine_index = r, .block_id = input->id,
                .diagnostic = diagnostic};
            for (size_t i = 0; i < input->instruction_count; i++) {
                ctx.origin = i;
                RegallocStatus status = lower_instruction(&ctx,
                                                          &input->instructions[i]);
                if (status != REGALLOC_OK) {
                    regalloc_machine_program_free(program);
                    return status;
                }
            }
        }
    }
    *out = program;
    return machine_report(diagnostic, REGALLOC_OK, SIZE_MAX, IR_NO_ID, "");
}
