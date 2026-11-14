/** @file assignment.c
 * Contrato de entrega entre asignador y emisor ASM.
 *
 * El asignador es dueno de la maquina y del arreglo de colores. Esta vista
 * prestada solo se valida una vez, al cerrar la asignacion, antes de emitir.
 * El emisor consulta esos colores y los offsets; no reconstruye el grafo.
 */
#include "regalloc_machine.h"
#include <assert.h>
#include <stdio.h>

_Static_assert(SYMBOL_WORD_SIZE == 4, "las ranuras de spill son palabras de 4 bytes");

static RegallocStatus assignment_error(RegallocDiagnostic *diagnostic,
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

/* El frame se cierra despues de insertar spills. Usar una resta evita
 * desbordar size_t al comprobar el limite fisico de 4096 bytes. */
static RegallocStatus check_frame(const RegallocAssignedProgram *assigned,
                                  size_t r, RegallocDiagnostic *diagnostic)
{
    const RegallocMachineRoutine *machine = &assigned->machine->routines[r];
    const IrProgram *source = assigned->machine->source;
    if (machine->ir_routine >= source->routine_count)
        return assignment_error(diagnostic, REGALLOC_INVALID_INPUT, r, IR_NO_ID,
                                "Indice de rutina IR invalido en la asignacion.");

    if (machine->base_frame_bytes > SYMBOL_STACK_SIZE ||
        machine->spill_slot_count >
            (SYMBOL_STACK_SIZE - machine->base_frame_bytes) / SYMBOL_WORD_SIZE)
        return assignment_error(diagnostic, REGALLOC_INVALID_INPUT, r, IR_NO_ID,
                                "El frame con spills excede la pila de PORYGON.");
    size_t frame = machine->base_frame_bytes +
                   SYMBOL_WORD_SIZE * machine->spill_slot_count;
    if (assigned->routines[r].frame_bytes != frame)
        return assignment_error(diagnostic, REGALLOC_INVALID_INPUT, r, IR_NO_ID,
                                "El tamano de frame no coincide con sus ranuras.");

    const Symbol *function = source->routines[machine->ir_routine].symbol;
    int call_reserve = function != NULL ? function->call_reserve_size
                                        : source->symbols->global_call_reserve_size;
    if (call_reserve < 0 || (size_t)call_reserve > SYMBOL_STACK_SIZE - frame)
        return assignment_error(diagnostic, REGALLOC_INVALID_INPUT, r, IR_NO_ID,
                                "El frame y la reserva de llamadas exceden la pila.");
    return REGALLOC_OK;
}

/* Liveness ya valido la forma y los IDs de las instrucciones virtuales. */
static RegallocStatus check_operands(const RegallocAssignedProgram *assigned,
                                     size_t r, RegallocDiagnostic *diagnostic)
{
    const RegallocMachineRoutine *routine = &assigned->machine->routines[r];
    const uint8_t *colors = assigned->routines[r].register_for_temp;
    for (size_t b = 0; b < routine->block_count; b++) {
        const RegallocMachineBlock *block = &routine->blocks[b];
        for (size_t i = 0; i < block->instruction_count; i++) {
            const RegallocMachineInstruction *in = &block->instructions[i];
            if ((in->def != IR_NO_ID && colors[in->def] == 0) ||
                (in->use1 != IR_NO_ID && colors[in->use1] == 0))
                return assignment_error(diagnostic, REGALLOC_INVALID_INPUT, r,
                                        block->id,
                                        "Una instruccion usa un temporal sin registro.");
            if (in->op == REGALLOC_M_SPILL_LOAD ||
                in->op == REGALLOC_M_SPILL_STORE) {
                unsigned effect = in->op == REGALLOC_M_SPILL_LOAD
                    ? IR_READ_MEMORY : IR_WRITE_MEMORY;
                if ((in->flags & effect) == 0 ||
                    (in->fixed_reads & REGALLOC_PHYS_R7) == 0)
                    return assignment_error(diagnostic, REGALLOC_INVALID_INPUT,
                                            r, block->id,
                                            "Un spill debe declarar memoria y lectura de r7.");
            }
        }
    }
    return REGALLOC_OK;
}

RegallocStatus regalloc_assigned_program_validate(
    const RegallocAssignedProgram *assigned, RegallocDiagnostic *diagnostic)
{
    if (assigned == NULL || assigned->machine == NULL ||
        assigned->machine->source == NULL ||
        assigned->machine->source->symbols == NULL ||
        assigned->routine_count != assigned->machine->routine_count ||
        (assigned->routine_count != 0 && assigned->routines == NULL))
        return assignment_error(diagnostic, REGALLOC_INVALID_INPUT, SIZE_MAX,
                                IR_NO_ID, "Asignacion o programa virtual incompleto.");

    const RegallocMachineProgram *machine = assigned->machine;
    for (size_t r = 0; r < assigned->routine_count; r++) {
        if (machine->routines[r].temp_count != 0 &&
            assigned->routines[r].register_for_temp == NULL)
            return assignment_error(diagnostic, REGALLOC_INVALID_INPUT, r,
                                    IR_NO_ID, "Falta el mapa de colores de la rutina.");
        RegallocStatus status = check_frame(assigned, r, diagnostic);
        if (status != REGALLOC_OK) return status;
    }

    LivenessResult *live = NULL;
    LivenessDiagnostic live_error;
    LivenessStatus live_status = liveness_analyze_machine(machine, &live,
                                                          &live_error);
    if (live_status != LIVENESS_OK)
        return assignment_error(diagnostic,
                                live_status == LIVENESS_OUT_OF_MEMORY
                                    ? REGALLOC_OUT_OF_MEMORY : REGALLOC_INVALID_INPUT,
                                live_error.routine, live_error.block,
                                live_error.message);

    RegallocStatus status = REGALLOC_OK;
    for (size_t r = 0; r < assigned->routine_count; r++) {
        const RegallocMachineRoutine *routine = &machine->routines[r];
        const uint8_t *colors = assigned->routines[r].register_for_temp;
        RegallocMachineGraph *graph = NULL;
        status = regalloc_build_machine_graph(machine, live, r, &graph,
                                              diagnostic);
        if (status != REGALLOC_OK) break;

        for (IrId t = 0; t < routine->temp_count; t++) {
            unsigned color = colors[t];
            if (color > REGALLOC_COLOR_COUNT ||
                (color != 0 &&
                 (graph->forbidden_colors[t] & (1u << color)) != 0)) {
                status = assignment_error(diagnostic, REGALLOC_INVALID_INPUT,
                                          r, IR_NO_ID,
                                          "Color fisico invalido o prohibido.");
                break;
            }
            if (color == 0) continue;
            for (IrId other = t + 1; other < routine->temp_count; other++)
                if (colors[other] == color &&
                    regalloc_machine_graph_has_edge(graph, t, other)) {
                    status = assignment_error(diagnostic, REGALLOC_INVALID_INPUT,
                                              r, IR_NO_ID,
                                              "Dos temporales que interfieren comparten registro.");
                    break;
                }
            if (status != REGALLOC_OK) break;
        }
        if (status == REGALLOC_OK)
            status = check_operands(assigned, r, diagnostic);
        regalloc_machine_graph_free(graph);
        if (status != REGALLOC_OK) break;
    }
    liveness_result_free(live);
    if (status == REGALLOC_OK)
        return assignment_error(diagnostic, REGALLOC_OK, SIZE_MAX, IR_NO_ID, "");
    return status;
}

size_t regalloc_assigned_spill_offset(const RegallocAssignedProgram *assigned,
                                     size_t routine, size_t slot)
{
    assert(assigned != NULL && routine < assigned->routine_count);
    const RegallocMachineRoutine *machine = &assigned->machine->routines[routine];
    assert(slot < machine->spill_slot_count);
    return machine->base_frame_bytes + SYMBOL_WORD_SIZE * slot;
}
