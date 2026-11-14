/** @file liveness_machine.c
 * Adaptador de instrucciones virtuales de maquina al solver compartido.
 *
 * El dominio son IDs de temporales, no registros fisicos. La ISA puede leer y
 * escribir el mismo destino en una instruccion de dos direcciones: por eso
 * Las operaciones ALU aportan def a USE antes de definirlo de nuevo.
 * Los sucesores se obtienen de los terminadores de cada bloque, cuyos destinos
 * son IDs de bloque y se convierten aqui a indices de la rutina.
 */
#include "liveness_internal.h"
#include "regalloc_machine.h"
#include <stdint.h>
#include <stdlib.h>

typedef struct {
    bool has_def, has_use, def_is_use, terminator;
    size_t target_count;
} MachineShape;

static bool instruction_shape(RegallocMachineOp op, MachineShape *shape)
{
    *shape = (MachineShape){0};
    switch (op) {
    case REGALLOC_M_IMM:
    case REGALLOC_M_LOAD_SYMBOL:
    case REGALLOC_M_SPILL_LOAD:
        shape->has_def = true;
        return true;
    case REGALLOC_M_STORE_SYMBOL:
    case REGALLOC_M_SPILL_STORE:
    case REGALLOC_M_TO_RETURN:
        shape->has_use = true;
        return true;
    case REGALLOC_M_MOVE:
        shape->has_def = shape->has_use = true;
        return true;
    case REGALLOC_M_ADD: case REGALLOC_M_SUB:
    case REGALLOC_M_AND: case REGALLOC_M_OR: case REGALLOC_M_XOR:
    case REGALLOC_M_EQ:
        shape->has_def = shape->has_use = shape->def_is_use = true;
        return true;
    case REGALLOC_M_ADDI: case REGALLOC_M_SUBI: case REGALLOC_M_BOOL_NOT:
        shape->has_def = shape->def_is_use = true;
        return true;
    case REGALLOC_M_JUMP:
        shape->terminator = true;
        shape->target_count = 1;
        return true;
    case REGALLOC_M_BRANCH:
        shape->has_use = shape->terminator = true;
        shape->target_count = 2;
        return true;
    case REGALLOC_M_RETURN:
        shape->terminator = true;
        return true;
    }
    return false;
}

static LivenessStatus machine_error(LivenessDiagnostic *diagnostic,
                                    LivenessStatus status, size_t routine,
                                    IrId block, const char *message)
{
    return live_report(diagnostic, status, routine, block, message);
}

static bool find_block(const RegallocMachineRoutine *routine, IrId id,
                       size_t *index)
{
    for (size_t b = 0; b < routine->block_count; b++) {
        if (routine->blocks[b].id == id) {
            *index = b;
            return true;
        }
    }
    return false;
}

/** Se valida antes de reservar usos. Un terminador fuera del final haria que
 * el solver analizara instrucciones inalcanzables como si fueran secuenciales. */
static LivenessStatus validate_routine(const RegallocMachineRoutine *routine,
                                       size_t r, size_t *use_count,
                                       LivenessDiagnostic *diagnostic)
{
    if (routine->block_count == 0 || routine->blocks == NULL ||
        routine->ir_temp_count > routine->temp_count)
        return machine_error(diagnostic, LIVENESS_INVALID_INPUT, r, IR_NO_ID,
                             "Rutina virtual sin bloques o contador de temporales invalido.");

    *use_count = 0;
    for (size_t b = 0; b < routine->block_count; b++) {
        const RegallocMachineBlock *block = &routine->blocks[b];
        if (block->id == IR_NO_ID || block->instruction_count == 0 ||
            block->instructions == NULL)
            return machine_error(diagnostic, LIVENESS_INVALID_INPUT, r, block->id,
                                 "Bloque virtual sin ID o instrucciones.");
        for (size_t previous = 0; previous < b; previous++)
            if (routine->blocks[previous].id == block->id)
                return machine_error(diagnostic, LIVENESS_INVALID_INPUT, r, block->id,
                                     "ID de bloque virtual repetido.");

        for (size_t i = 0; i < block->instruction_count; i++) {
            const RegallocMachineInstruction *in = &block->instructions[i];
            MachineShape shape;
            if (!instruction_shape(in->op, &shape) ||
                (in->def != IR_NO_ID) != shape.has_def ||
                (in->use1 != IR_NO_ID) != shape.has_use ||
                (shape.has_def && in->def >= routine->temp_count) ||
                (shape.has_use && in->use1 >= routine->temp_count) ||
                ((in->op == REGALLOC_M_SPILL_LOAD ||
                  in->op == REGALLOC_M_SPILL_STORE) &&
                 in->slot >= routine->spill_slot_count) ||
                ((in->op == REGALLOC_M_ADDI || in->op == REGALLOC_M_SUBI) &&
                 (in->immediate < -32768 || in->immediate > 32767)) ||
                shape.terminator != (i + 1 == block->instruction_count))
                return machine_error(diagnostic, LIVENESS_INVALID_INPUT, r, block->id,
                                     "Forma o ID de instruccion virtual invalido.");

            size_t uses = (size_t)shape.has_use + (size_t)shape.def_is_use;
            if (uses > SIZE_MAX - *use_count)
                return machine_error(diagnostic, LIVENESS_OUT_OF_MEMORY, r, block->id,
                                     "Demasiados usos virtuales.");
            *use_count += uses;
            for (size_t t = 0; t < shape.target_count; t++) {
                size_t successor;
                if (!find_block(routine, in->targets[t], &successor))
                    return machine_error(diagnostic, LIVENESS_INVALID_INPUT, r, block->id,
                                         "Destino de bloque virtual inexistente.");
            }
        }
    }
    return LIVENESS_OK;
}

static LivenessStatus prepare_routine(LiveInputRoutine *target,
                                      const RegallocMachineRoutine *source,
                                      size_t use_count, size_t r,
                                      LivenessDiagnostic *diagnostic)
{
    if (source->block_count > SIZE_MAX / sizeof(*target->blocks) ||
        use_count > SIZE_MAX / sizeof(*target->use_storage))
        return machine_error(diagnostic, LIVENESS_OUT_OF_MEMORY, r, IR_NO_ID,
                             "Entrada virtual demasiado grande.");
    target->blocks = calloc(source->block_count, sizeof(*target->blocks));
    if (use_count != 0)
        target->use_storage = calloc(use_count, sizeof(*target->use_storage));
    if (target->blocks == NULL ||
        (use_count != 0 && target->use_storage == NULL))
        return machine_error(diagnostic, LIVENESS_OUT_OF_MEMORY, r, IR_NO_ID,
                             "No se pudo reservar la entrada virtual.");

    target->block_count = source->block_count;
    target->value_count = source->temp_count;
    size_t next_use = 0;
    for (size_t b = 0; b < source->block_count; b++) {
        const RegallocMachineBlock *from = &source->blocks[b];
        LiveInputBlock *to = &target->blocks[b];
        to->id = from->id;
        if (from->instruction_count > SIZE_MAX / sizeof(*to->instructions))
            return machine_error(diagnostic, LIVENESS_OUT_OF_MEMORY, r, to->id,
                                 "Demasiadas instrucciones virtuales.");
        to->instructions = calloc(from->instruction_count, sizeof(*to->instructions));
        if (to->instructions == NULL)
            return machine_error(diagnostic, LIVENESS_OUT_OF_MEMORY, r, to->id,
                                 "No se pudieron reservar instrucciones virtuales.");
        to->instruction_count = from->instruction_count;

        const RegallocMachineInstruction *last =
            &from->instructions[from->instruction_count - 1];
        MachineShape terminator;
        instruction_shape(last->op, &terminator); /* Ya validado. */
        to->successor_count = terminator.target_count;
        for (size_t t = 0; t < terminator.target_count; t++)
            find_block(source, last->targets[t], &to->successors[t]);

        for (size_t i = 0; i < from->instruction_count; i++) {
            const RegallocMachineInstruction *in = &from->instructions[i];
            LiveInputInstruction *transfer = &to->instructions[i];
            MachineShape shape;
            instruction_shape(in->op, &shape); /* Ya validado. */
            transfer->definition = shape.has_def ? in->def : IR_NO_ID;
            transfer->use_count = (size_t)shape.has_use + (size_t)shape.def_is_use;
            if (transfer->use_count != 0) {
                transfer->uses = target->use_storage + next_use;
                if (shape.def_is_use) target->use_storage[next_use++] = in->def;
                if (shape.has_use) target->use_storage[next_use++] = in->use1;
            }
        }
    }
    return LIVENESS_OK;
}

LivenessStatus liveness_analyze_machine(const RegallocMachineProgram *machine,
                                      LivenessResult **out,
                                      LivenessDiagnostic *diagnostic)
{
    if (machine == NULL || out == NULL || *out != NULL ||
        (machine->routine_count != 0 && machine->routines == NULL))
        return machine_error(diagnostic, LIVENESS_INVALID_INPUT, SIZE_MAX,
                             IR_NO_ID, "Se requiere maquina virtual y salida NULL.");
    if (machine->routine_count > SIZE_MAX / sizeof(LiveInputRoutine))
        return machine_error(diagnostic, LIVENESS_OUT_OF_MEMORY, SIZE_MAX,
                             IR_NO_ID, "Demasiadas rutinas virtuales.");

    LiveInput input = {0};
    if (machine->routine_count != 0)
        input.routines = calloc(machine->routine_count, sizeof(*input.routines));
    if (machine->routine_count != 0 && input.routines == NULL)
        return machine_error(diagnostic, LIVENESS_OUT_OF_MEMORY, SIZE_MAX,
                             IR_NO_ID, "No se pudieron reservar rutinas virtuales.");
    input.routine_count = machine->routine_count;

    LivenessStatus status = LIVENESS_OK;
    for (size_t r = 0; r < input.routine_count; r++) {
        size_t use_count;
        status = validate_routine(&machine->routines[r], r, &use_count, diagnostic);
        if (status == LIVENESS_OK)
            status = prepare_routine(&input.routines[r], &machine->routines[r],
                                     use_count, r, diagnostic);
        if (status != LIVENESS_OK) break;
    }
    if (status == LIVENESS_OK)
        status = live_solve(&input, out, diagnostic);
    live_input_free(&input);
    return status;
}
