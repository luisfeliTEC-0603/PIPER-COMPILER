/** @file ir_validate.c
 * I6: revisar el modelo generado, sin repetir reglas semánticas del fuente.
 */
#include "ir.h"
#include <stdlib.h>

/* Copiar el primer incumplimiento con la posición que llevaba el recorrido. */
static IrStatus validation_error(IrDiagnostic *out, IrDiagnostic error, const char *message)
{
    snprintf(error.message, sizeof(error.message), "%s", message);
    if (out) *out = error;
    return error.status;
}

static bool has_block(const IrRoutine *routine, IrId id)
{
    if (id == IR_NO_ID) return false;
    for (size_t i = 0; i < routine->block_count; i++)
        if (routine->blocks[i].id == id) return true;
    return false;
}

/* Solo forma de la instrucción: argumentos presentes/ausentes y clase de
 * resultado. No revisa promociones, compatibilidad Piper ni firmas completas. */
static bool instruction_shape(const IrInstruction *in)
{
    bool a = in->arg1.kind != IR_OPERAND_NONE, b = in->arg2.kind != IR_OPERAND_NONE;
    bool temp = in->result.kind == IR_OPERAND_TEMP, absent = in->result.kind == IR_OPERAND_NONE;
    switch (in->op) {
    case IR_OP_COPY:
        return a && !b && (temp || in->result.kind == IR_OPERAND_SYMBOL);
    case IR_OP_ADD: case IR_OP_SUB: case IR_OP_MUL: case IR_OP_DIV: case IR_OP_MOD: case IR_OP_POW:
    case IR_OP_EQ: case IR_OP_NE: case IR_OP_LT: case IR_OP_LE: case IR_OP_GT: case IR_OP_GE:
    case IR_OP_AND: case IR_OP_OR: case IR_OP_XOR:
        return a && b && temp;
    case IR_OP_NEG: case IR_OP_NOT:
        return a && !b && temp;
    case IR_OP_ADDR:
        return in->arg1.kind == IR_OPERAND_SYMBOL && !b && temp && in->result.type.address;
    case IR_OP_LOAD:
        return a && in->arg1.type.address && !b && temp;
    case IR_OP_STORE:
        return a && in->arg1.type.address && b && absent;
    case IR_OP_CHECK_BOUNDS:
        return a && b && absent;
    case IR_OP_CALL: case IR_OP_INTRINSIC:
        if (a || b || (!temp && !absent) ||
            (in->extra.call.argument_count && in->extra.call.arguments == NULL)) return false;
        if (in->op == IR_OP_CALL) return in->extra.call.callee != NULL;
        return in->extra.call.intrinsic >= IR_INTRINSIC_AUTH &&
               in->extra.call.intrinsic <= IR_INTRINSIC_REVOKE;
    case IR_OP_JUMP:
        return !a && !b && absent;
    case IR_OP_BRANCH:
        return a && !b && absent && !in->arg1.type.address && in->arg1.type.piper == PIPER_TYPE_BOOL;
    case IR_OP_RETURN:
        return !b && absent;
    default: return false;
    }
}

/* El índice temporal permite encontrar su definición sin depender del orden
 * físico de bloques. No demuestra disponibilidad entre caminos ni dominancia. */
static bool operand_valid(IrOperand operand, const IrOperand *const *definitions, size_t count)
{
    if (operand.kind == IR_OPERAND_NONE) return true;
    if (operand.type.piper < PIPER_TYPE_BOOL || operand.type.piper >= PIPER_TYPE_NONE) return false;
    switch (operand.kind) {
    case IR_OPERAND_CONSTANT: return !operand.type.address;
    case IR_OPERAND_SYMBOL: return operand.value.symbol != NULL;
    case IR_OPERAND_TEMP: {
        if (operand.value.temp >= count || definitions[operand.value.temp] == NULL) return false;
        IrType type = definitions[operand.value.temp]->type;
        return type.piper == operand.type.piper && type.address == operand.type.address;
    }
    default: return false;
    }
}

/** Dos pasadas por rutina: registrar definiciones y después revisar sus usos.
 * Solo reserva un índice temporal de inspección; se libera antes de retornar.
 * No modifica IR/símbolos ni calcula CFG, liveness o compatibilidad de tipos. */
IrStatus ir_validate(const IrProgram *program, IrDiagnostic *diagnostic)
{
    IrDiagnostic error = {.status = IR_INVALID_PROGRAM,
        .routine = IR_NO_ID, .block = IR_NO_ID, .instruction = IR_NO_ID};
    if (diagnostic) *diagnostic = (IrDiagnostic){.status = IR_OK,
        .routine = IR_NO_ID, .block = IR_NO_ID, .instruction = IR_NO_ID};
    if (program == NULL || program->symbols == NULL) {
        error.status = IR_INVALID_INPUT;
        return validation_error(diagnostic, error, "Falta el programa IR o su tabla final.");
    }
    if (program->routine_count == 0 || program->routines == NULL ||
        program->routine_count > program->routine_capacity ||
        (program->startup != IR_NO_ID && program->startup >= program->routine_count))
        return validation_error(diagnostic, error, "Lista de rutinas o arranque IR inválidos.");
    for (size_t r = 0; r < program->routine_count; r++) {
        const IrRoutine *routine = &program->routines[r];
        error.routine = r;
        error.block = error.instruction = IR_NO_ID;
        error.location = (SourceLocation){0};
        if ((routine->symbol == NULL) != (program->startup == r) ||
            routine->block_count == 0 || routine->blocks == NULL ||
            routine->block_count > routine->block_capacity)
            return validation_error(diagnostic, error, "Rutina sin bloques válidos o identidad de arranque incorrecta.");
        if (!has_block(routine, routine->entry))
            return validation_error(diagnostic, error, "No existe el bloque de entrada de la rutina.");
        if (routine->temp_count > SIZE_MAX / sizeof(IrOperand *)) {
            error.status = IR_OUT_OF_MEMORY;
            return validation_error(diagnostic, error, "Demasiados temporales para revisar IR.");
        }
        const IrOperand **definitions = NULL;
        if (routine->temp_count) {
            definitions = calloc(routine->temp_count, sizeof(*definitions));
            if (definitions == NULL) {
                error.status = IR_OUT_OF_MEMORY;
                return validation_error(diagnostic, error, "No se pudo reservar el índice temporal de validación.");
            }
        }
        const char *message = NULL;
        for (size_t b = 0; b < routine->block_count && message == NULL; b++) {
            const IrBlock *block = &routine->blocks[b];
            error.block = block->id;
            error.instruction = IR_NO_ID;
            error.location = (SourceLocation){0};
            if (block->id == IR_NO_ID || block->instructions == NULL || block->instruction_count == 0 ||
                block->instruction_count > block->instruction_capacity) {
                message = "Bloque vacío o almacenamiento de instrucciones inválido.";
                break;
            }
            for (size_t previous = 0; previous < b; previous++)
                if (routine->blocks[previous].id == block->id) message = "ID de bloque duplicado.";
            for (size_t i = 0; i < block->instruction_count && message == NULL; i++) {
                const IrInstruction *in = &block->instructions[i];
                error.instruction = i;
                error.location = in->location;
                bool terminal = in->op == IR_OP_JUMP || in->op == IR_OP_BRANCH || in->op == IR_OP_RETURN;
                if (!instruction_shape(in)) message = "Forma de instrucción IR inválida.";
                else if (terminal != (i + 1 == block->instruction_count))
                    message = "El bloque debe tener su única transferencia al final.";
                else if ((in->op == IR_OP_JUMP || in->op == IR_OP_BRANCH) &&
                    (!has_block(routine, in->extra.targets[0]) ||
                     (in->op == IR_OP_BRANCH && !has_block(routine, in->extra.targets[1]))))
                    message = "Destino de salto inexistente en esta rutina.";
                if (message == NULL && in->result.kind == IR_OPERAND_TEMP) {
                    IrId id = in->result.value.temp;
                    if (id >= routine->temp_count || definitions[id] != NULL)
                        message = "ID temporal fuera de rango o definición duplicada.";
                    else definitions[id] = &in->result;
                }
            }
        }
        for (size_t b = 0; b < routine->block_count && message == NULL; b++) {
            const IrBlock *block = &routine->blocks[b];
            error.block = block->id;
            for (size_t i = 0; i < block->instruction_count && message == NULL; i++) {
                const IrInstruction *in = &block->instructions[i];
                error.instruction = i;
                error.location = in->location;
                if (!operand_valid(in->arg1, definitions, routine->temp_count) ||
                    !operand_valid(in->arg2, definitions, routine->temp_count) ||
                    !operand_valid(in->result, definitions, routine->temp_count))
                    message = "Operando inválido o temporal sin definición/tipo coherente.";
                if (in->op == IR_OP_CALL || in->op == IR_OP_INTRINSIC)
                    for (size_t a = 0; a < in->extra.call.argument_count && message == NULL; a++)
                        if (in->extra.call.arguments[a].kind == IR_OPERAND_NONE ||
                            !operand_valid(in->extra.call.arguments[a], definitions, routine->temp_count))
                            message = "Argumento IR ausente o temporal inconsistente.";
            }
        }
        free(definitions);
        if (message) return validation_error(diagnostic, error, message);
    }
    return IR_OK;
}
