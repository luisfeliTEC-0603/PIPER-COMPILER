/** @file allocate.c
 * Cierra la asignacion de una maquina virtual admitida por R2b.
 * Cada reescritura invalida liveness y grafo; se liberan antes de repetir.
 */
#include "regalloc_machine.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct RegallocAllocation {
    RegallocMachineProgram *machine;
    RegallocAssignedRoutine *routines;
    RegallocRoutineReport *reports;
    RegallocAssignedProgram view;
};

static RegallocStatus allocation_error(RegallocDiagnostic *diagnostic,
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

void regalloc_allocation_free(RegallocAllocation *allocation)
{
    if (allocation == NULL) return;
    if (allocation->routines != NULL)
        for (size_t r = 0; r < allocation->view.routine_count; r++)
            free((void *)allocation->routines[r].register_for_temp);
    free(allocation->routines);
    if (allocation->reports != NULL)
        for (size_t r = 0; r < allocation->view.routine_count; r++)
            free((void *)allocation->reports[r].spills);
    free(allocation->reports);
    regalloc_machine_program_free(allocation->machine);
    free(allocation);
}

const RegallocAssignedProgram *regalloc_allocation_view(
    const RegallocAllocation *allocation)
{
    return allocation == NULL ? NULL : &allocation->view;
}

static bool def_is_use(RegallocMachineOp op)
{
    switch (op) {
    case REGALLOC_M_ADD: case REGALLOC_M_SUB: case REGALLOC_M_MUL:
    case REGALLOC_M_CHECKED_ADD: case REGALLOC_M_CHECKED_SUB:
    case REGALLOC_M_CHECKED_MUL:
    case REGALLOC_M_DIV: case REGALLOC_M_MOD: case REGALLOC_M_POW:
    case REGALLOC_M_ADDI: case REGALLOC_M_SUBI:
    case REGALLOC_M_AND: case REGALLOC_M_OR: case REGALLOC_M_XOR:
    case REGALLOC_M_EQ: case REGALLOC_M_LT: case REGALLOC_M_GT:
    case REGALLOC_M_BOOL_NOT:
        return true;
    default:
        return false;
    }
}

static size_t block_index(const RegallocMachineRoutine *routine, IrId id)
{
    for (size_t b = 0; b < routine->block_count; b++)
        if (routine->blocks[b].id == id) return b;
    return SIZE_MAX;
}

/* Un bloque pertenece a un ciclo si algun sucesor puede volver a el.
 * Los terminadores y sus destinos ya fueron validados por liveness. */
static bool cyclic_block(const RegallocMachineRoutine *routine, size_t start,
                         bool *seen, size_t *queue)
{
    memset(seen, 0, routine->block_count * sizeof(*seen));
    size_t head = 0, tail = 0;
    const RegallocMachineBlock *block = &routine->blocks[start];
    const RegallocMachineInstruction *last =
        &block->instructions[block->instruction_count - 1];
    size_t targets = last->op == REGALLOC_M_BRANCH ? 2
        : last->op == REGALLOC_M_JUMP ? 1 : 0;
    for (size_t i = 0; i < targets; i++) {
        size_t next = block_index(routine, last->targets[i]);
        if (next == start) return true;
        if (!seen[next]) { seen[next] = true; queue[tail++] = next; }
    }
    while (head != tail) {
        size_t b = queue[head++];
        block = &routine->blocks[b];
        last = &block->instructions[block->instruction_count - 1];
        targets = last->op == REGALLOC_M_BRANCH ? 2
            : last->op == REGALLOC_M_JUMP ? 1 : 0;
        for (size_t i = 0; i < targets; i++) {
            size_t next = block_index(routine, last->targets[i]);
            if (next == start) return true;
            if (!seen[next]) { seen[next] = true; queue[tail++] = next; }
        }
    }
    return false;
}

static void add_cost(uint64_t *costs, IrId temp, uint64_t weight)
{
    if (temp != IR_NO_ID)
        costs[temp] = costs[temp] > UINT64_MAX - weight
            ? UINT64_MAX : costs[temp] + weight;
}

/* Cada lectura/definicion cuesta una palabra potencial de memoria.
 * Un bloque dentro de cualquier ciclo pesa 10; fuera pesa 1. */
static RegallocStatus spill_costs(const RegallocMachineRoutine *routine,
                                  const bool *protected, uint64_t **out,
                                  size_t r, RegallocDiagnostic *diagnostic)
{
    size_t count = routine->temp_count;
    uint64_t *costs = calloc(count == 0 ? 1 : count, sizeof(*costs));
    bool *seen = calloc(routine->block_count, sizeof(*seen));
    size_t *queue = malloc(routine->block_count * sizeof(*queue));
    if (costs == NULL || seen == NULL || queue == NULL) {
        free(costs); free(seen); free(queue);
        return allocation_error(diagnostic, REGALLOC_OUT_OF_MEMORY, r,
                                IR_NO_ID, "No se pudieron calcular costos de spill.");
    }
    for (size_t b = 0; b < routine->block_count; b++) {
        uint64_t weight = cyclic_block(routine, b, seen, queue) ? 10 : 1;
        const RegallocMachineBlock *block = &routine->blocks[b];
        for (size_t i = 0; i < block->instruction_count; i++) {
            const RegallocMachineInstruction *in = &block->instructions[i];
            add_cost(costs, in->use1, weight);
            if (in->op == REGALLOC_M_STORE_INDIRECT)
                add_cost(costs, in->use2, weight);
            add_cost(costs, in->def, weight);
            if (def_is_use(in->op)) add_cost(costs, in->def, weight);
        }
    }
    for (IrId t = 0; t < count; t++)
        if (protected[t]) costs[t] = UINT64_MAX;
    free(seen);
    free(queue);
    *out = costs;
    return REGALLOC_OK;
}

static long double spill_ratio(const uint64_t *costs,
                               const RegallocMachineGraph *graph, IrId t)
{
    size_t degree = graph->degree[t];
    return (long double)costs[t] / (degree == 0 ? 1 : degree);
}

/* Los temporales creados alrededor de un spill son atomicos: derramarlos de
 * nuevo no reduce la presion local. Si uno falla, se derrama un vecino original. */
static IrId choose_victim(const RegallocMachineGraph *graph,
                          const RegallocColoring *coloring,
                          const uint64_t *costs, const bool *protected)
{
    IrId chosen = IR_NO_ID;
    for (IrId t = 0; t < graph->temp_count; t++)
        if (coloring->colors[t] == 0 && !protected[t] &&
            (chosen == IR_NO_ID ||
             spill_ratio(costs, graph, t) < spill_ratio(costs, graph, chosen)))
            chosen = t;
    if (chosen != IR_NO_ID) return chosen;
    for (IrId t = 0; t < graph->temp_count; t++) {
        if (coloring->colors[t] != 0 || !protected[t]) continue;
        for (IrId neighbor = 0; neighbor < graph->temp_count; neighbor++)
            if (!protected[neighbor] &&
                regalloc_machine_graph_has_edge(graph, t, neighbor) &&
                (chosen == IR_NO_ID || spill_ratio(costs, graph, neighbor) <
                                       spill_ratio(costs, graph, chosen)))
                chosen = neighbor;
    }
    return chosen;
}

static RegallocMachineInstruction spill_access(RegallocMachineOp op, IrId temp,
                                                size_t slot,
                                                const RegallocMachineInstruction *at)
{
    return (RegallocMachineInstruction){
        .op = op, .def = op == REGALLOC_M_SPILL_LOAD ? temp : IR_NO_ID,
        .use1 = op == REGALLOC_M_SPILL_STORE ? temp : IR_NO_ID,
        .slot = slot, .fixed_reads = REGALLOC_PHYS_R7,
        .flags = op == REGALLOC_M_SPILL_LOAD ? IR_READ_MEMORY : IR_WRITE_MEMORY,
        .type = at->type, .location = at->location,
        .origin_instruction = at->origin_instruction
    };
}

/* Reescribe UNA identidad virtual. Cada uso se recarga cerca de su operacion;
 * cada definicion se guarda despues. El caso de dos direcciones recarga el
 * valor anterior de def antes de operar. */
static RegallocStatus rewrite_spill(RegallocMachineProgram *machine, size_t r,
                                    IrId victim, bool **protected,
                                    RegallocDiagnostic *diagnostic)
{
    RegallocMachineRoutine *routine = &machine->routines[r];
    const IrRoutine *ir = &machine->source->routines[routine->ir_routine];
    int reserve = ir->symbol != NULL ? ir->symbol->call_reserve_size
                                     : machine->source->symbols->global_call_reserve_size;
    if (reserve < 0 || routine->base_frame_bytes > SYMBOL_STACK_SIZE ||
        (size_t)reserve > SYMBOL_STACK_SIZE - routine->base_frame_bytes ||
        routine->spill_slot_count >=
            (SYMBOL_STACK_SIZE - routine->base_frame_bytes - (size_t)reserve) /
            SYMBOL_WORD_SIZE)
        return allocation_error(diagnostic, REGALLOC_UNSUPPORTED, r,
                                IR_NO_ID, "No cabe otra ranura de spill en el frame.");
    size_t old_temp_count = routine->temp_count;
    size_t total = 0;
    for (size_t b = 0; b < routine->block_count; b++) {
        size_t n = routine->blocks[b].instruction_count;
        if (n > (SIZE_MAX - total) / 2)
            return allocation_error(diagnostic, REGALLOC_OUT_OF_MEMORY, r,
                                    routine->blocks[b].id,
                                    "Demasiadas instrucciones para reescribir spill.");
        total += n;
    }
    if (total > (SIZE_MAX - old_temp_count) / 2 ||
        old_temp_count + 2 * total > SIZE_MAX / sizeof(**protected))
        return allocation_error(diagnostic, REGALLOC_OUT_OF_MEMORY, r,
                                IR_NO_ID, "Demasiados temporales de spill.");
    size_t capacity = old_temp_count + 2 * total;
    bool *flags = realloc(*protected, capacity * sizeof(*flags));
    if (flags == NULL)
        return allocation_error(diagnostic, REGALLOC_OUT_OF_MEMORY, r,
                                IR_NO_ID, "No se pudo ampliar el mapa de spills.");
    memset(flags + old_temp_count, 0,
           (capacity - old_temp_count) * sizeof(*flags));
    *protected = flags;
    size_t next_temp = old_temp_count;
    size_t slot = routine->spill_slot_count;
    bool changed = false;
    for (size_t b = 0; b < routine->block_count; b++) {
        RegallocMachineBlock *block = &routine->blocks[b];
        size_t n = block->instruction_count;
        if (n > SIZE_MAX / 4 / sizeof(*block->instructions))
            return allocation_error(diagnostic, REGALLOC_OUT_OF_MEMORY, r,
                                    block->id, "Bloque de spill demasiado grande.");
        RegallocMachineInstruction *rewritten = calloc(n * 4,
                                                        sizeof(*rewritten));
        if (rewritten == NULL)
            return allocation_error(diagnostic, REGALLOC_OUT_OF_MEMORY, r,
                                    block->id, "No se pudo reservar la reescritura.");
        size_t used = 0;
        for (size_t i = 0; i < n; i++) {
            RegallocMachineInstruction in = block->instructions[i];
            bool spilled_def = in.def == victim;
            bool spilled_use = in.use1 == victim;
            bool spilled_use2 = in.op == REGALLOC_M_STORE_INDIRECT &&
                                in.use2 == victim;
            bool old_def = spilled_def && def_is_use(in.op);
            IrId def_temp = IR_NO_ID;
            if (old_def) {
                def_temp = next_temp++;
                flags[def_temp] = true;
                rewritten[used++] = spill_access(REGALLOC_M_SPILL_LOAD,
                                                 def_temp, slot, &in);
                in.def = def_temp;
            }
            if (spilled_use) {
                if (old_def) in.use1 = def_temp;
                else {
                    IrId use_temp = next_temp++;
                    flags[use_temp] = true;
                    rewritten[used++] = spill_access(REGALLOC_M_SPILL_LOAD,
                                                     use_temp, slot, &in);
                    in.use1 = use_temp;
                }
            }
            if (spilled_use2) {
                if (spilled_use) in.use2 = in.use1;
                else {
                    IrId use_temp = next_temp++;
                    flags[use_temp] = true;
                    rewritten[used++] = spill_access(REGALLOC_M_SPILL_LOAD,
                                                     use_temp, slot, &in);
                    in.use2 = use_temp;
                }
            }
            if (spilled_def && !old_def) {
                def_temp = next_temp++;
                flags[def_temp] = true;
                in.def = def_temp;
            }
            rewritten[used++] = in;
            if (spilled_def)
                rewritten[used++] = spill_access(REGALLOC_M_SPILL_STORE,
                                                  def_temp, slot, &in);
            changed |= spilled_def || spilled_use || spilled_use2;
        }
        free(block->instructions);
        block->instructions = rewritten;
        block->instruction_count = used;
    }
    if (!changed)
        return allocation_error(diagnostic, REGALLOC_INVALID_INPUT, r,
                                IR_NO_ID, "El candidato de spill no aparece en la maquina.");
    routine->temp_count = next_temp;
    routine->spill_slot_count++;
    return REGALLOC_OK;
}

RegallocStatus regalloc_allocate_with_options(const RegallocAnalysis *analysis,
                                              const RegallocOptions *options,
                                              RegallocAllocation **out,
                                              RegallocDiagnostic *diagnostic)
{
    if (options == NULL || out == NULL || *out != NULL)
        return allocation_error(diagnostic, REGALLOC_INVALID_INPUT, SIZE_MAX,
                                IR_NO_ID, "Se requieren opciones y salida inicialmente NULL.");
    RegallocAllocation *result = calloc(1, sizeof(*result));
    if (result == NULL)
        return allocation_error(diagnostic, REGALLOC_OUT_OF_MEMORY, SIZE_MAX,
                                IR_NO_ID, "No se pudo reservar la asignacion.");
    RegallocStatus status = regalloc_lower_scalar(analysis, &result->machine,
                                                  diagnostic);
    if (status != REGALLOC_OK) goto failed;
    result->view.machine = result->machine;
    result->view.routine_count = result->machine->routine_count;
    result->routines = calloc(result->view.routine_count,
                              sizeof(*result->routines));
    result->reports = calloc(result->view.routine_count, sizeof(*result->reports));
    if (result->routines == NULL || result->reports == NULL) {
        status = allocation_error(diagnostic, REGALLOC_OUT_OF_MEMORY, SIZE_MAX,
                                  IR_NO_ID, "No se pudieron reservar las rutinas coloreadas.");
        goto failed;
    }
    result->view.routines = result->routines;
    for (size_t r = 0; r < result->view.routine_count; r++) {
        RegallocMachineRoutine *routine = &result->machine->routines[r];
        bool *protected = calloc(routine->temp_count == 0 ? 1 : routine->temp_count,
                                 sizeof(*protected));
        if (protected == NULL) {
            status = allocation_error(diagnostic, REGALLOC_OUT_OF_MEMORY, r,
                                      IR_NO_ID, "No se pudo reservar el mapa de temporales.");
            goto failed;
        }
        for (;;) {
            LivenessResult *live = NULL;
            LivenessDiagnostic live_error;
            LivenessStatus live_status = liveness_analyze_machine(result->machine,
                                                                  &live, &live_error);
            if (live_status != LIVENESS_OK) {
                status = allocation_error(diagnostic,
                    live_status == LIVENESS_OUT_OF_MEMORY ? REGALLOC_OUT_OF_MEMORY
                                                          : REGALLOC_INVALID_INPUT,
                    live_error.routine, live_error.block, live_error.message);
                free(protected);
                goto failed;
            }
            RegallocMachineGraph *graph = NULL;
            status = regalloc_build_machine_graph(result->machine, live, r,
                                                  &graph, diagnostic);
            liveness_result_free(live);
            if (status != REGALLOC_OK) {
                free(protected);
                goto failed;
            }
            uint64_t *costs = NULL;
            status = spill_costs(routine, protected, &costs, r, diagnostic);
            if (status != REGALLOC_OK) {
                regalloc_machine_graph_free(graph);
                free(protected);
                goto failed;
            }
            RegallocColoring *coloring = NULL;
            status = regalloc_color_machine_graph_costed(graph, costs,
                                                         &coloring, diagnostic);
            if (status != REGALLOC_OK) {
                free(costs);
                regalloc_machine_graph_free(graph);
                free(protected);
                goto failed;
            }
            if (coloring->uncolored_count == 0) {
                result->routines[r].register_for_temp = coloring->colors;
                result->routines[r].frame_bytes = routine->base_frame_bytes +
                    SYMBOL_WORD_SIZE * routine->spill_slot_count;
                coloring->colors = NULL;
                regalloc_coloring_free(coloring);
                free(costs);
                regalloc_machine_graph_free(graph);
                break;
            }
            /* Un candidato de simplificacion no basta: solo se decide tras
             * reconstruir todos los colores. Nunca entregar ceros al emisor. */
            if (!options->allow_spill) {
                const Symbol *function = result->machine->source->routines[
                    routine->ir_routine].symbol;
                const char *name = function != NULL ? function->name : "<startup>";
                char message[256];
                snprintf(message, sizeof(message),
                    "No se pudo asignar registros en '%.64s': spilling esta deshabilitado. "
                    "Habilite \"Permitir spilling\" (spilling registers) para compilar este programa.",
                    name);
                status = allocation_error(diagnostic, REGALLOC_SPILL_REQUIRED, r,
                    IR_NO_ID, message);
                regalloc_coloring_free(coloring);
                free(costs);
                regalloc_machine_graph_free(graph);
                free(protected);
                goto failed;
            }
            IrId victim = choose_victim(graph, coloring, costs, protected);
            regalloc_coloring_free(coloring);
            free(costs);
            regalloc_machine_graph_free(graph);
            if (victim == IR_NO_ID) {
                status = allocation_error(diagnostic, REGALLOC_UNSUPPORTED, r,
                    IR_NO_ID, "La presion local no cabe en los cuatro registros.");
                free(protected);
                goto failed;
            }
            RegallocRoutineReport *report = &result->reports[r];
            size_t slot = routine->spill_slot_count;
            if (slot >= SIZE_MAX / sizeof(RegallocSpill) - 1) {
                status = allocation_error(diagnostic, REGALLOC_OUT_OF_MEMORY, r,
                                          IR_NO_ID, "Demasiados registros de spill.");
                free(protected);
                goto failed;
            }
            RegallocSpill *spills = realloc((void *)report->spills,
                                             (slot + 1) * sizeof(*spills));
            if (spills == NULL) {
                status = allocation_error(diagnostic, REGALLOC_OUT_OF_MEMORY, r,
                                          IR_NO_ID, "Sin memoria para reportar spill.");
                free(protected);
                goto failed;
            }
            report->spills = spills;
            spills[slot] = (RegallocSpill){.temp = victim, .slot = slot};
            status = rewrite_spill(result->machine, r, victim, &protected,
                                   diagnostic);
            if (status != REGALLOC_OK) {
                free(protected);
                goto failed;
            }
        }
        free(protected);
        RegallocRoutineReport *report = &result->reports[r];
        report->spilled_temporaries = report->spill_slots = routine->spill_slot_count;
        report->frame_bytes = result->routines[r].frame_bytes;
        for (size_t b = 0; b < routine->block_count; b++)
            for (size_t i = 0; i < routine->blocks[b].instruction_count; i++) {
                RegallocMachineOp op = routine->blocks[b].instructions[i].op;
                report->spill_loads += op == REGALLOC_M_SPILL_LOAD;
                report->spill_stores += op == REGALLOC_M_SPILL_STORE;
            }
    }
    status = regalloc_assigned_program_validate(&result->view, diagnostic);
    if (status != REGALLOC_OK) goto failed;
    *out = result;
    return REGALLOC_OK;
failed:
    regalloc_allocation_free(result);
    return status;
}

RegallocOptions regalloc_default_options(void)
{
    return (RegallocOptions){.allow_spill = true};
}

RegallocStatus regalloc_allocate(const RegallocAnalysis *analysis,
                                 RegallocAllocation **out,
                                 RegallocDiagnostic *diagnostic)
{
    RegallocOptions options = regalloc_default_options();
    return regalloc_allocate_with_options(analysis, &options, out, diagnostic);
}

const RegallocRoutineReport *regalloc_allocation_routine_report(
    const RegallocAllocation *allocation, size_t routine)
{
    return allocation == NULL || routine >= allocation->view.routine_count
        ? NULL : &allocation->reports[routine];
}

RegallocStatus regalloc_allocation_write_report(FILE *output,
                                               const RegallocAllocation *allocation)
{
    if (output == NULL || allocation == NULL) return REGALLOC_INVALID_INPUT;
    for (size_t r = 0; r < allocation->view.routine_count; r++) {
        const RegallocRoutineReport *report = &allocation->reports[r];
        const RegallocMachineRoutine *routine = &allocation->machine->routines[r];
        fprintf(output, "[regalloc] rutina %zu: temporales=%zu slots=%zu frame=%zu "
                "cargas=%zu stores=%zu\n", r, report->spilled_temporaries,
                report->spill_slots, report->frame_bytes, report->spill_loads,
                report->spill_stores);
        for (size_t s = 0; s < report->spill_slots; s++)
            fprintf(output, "  t%zu -> slot %zu offset=%zu(r7)\n",
                    report->spills[s].temp, report->spills[s].slot,
                    regalloc_assigned_spill_offset(&allocation->view, r, s));
        for (size_t b = 0; b < routine->block_count; b++)
            for (size_t i = 0; i < routine->blocks[b].instruction_count; i++) {
                const RegallocMachineInstruction *in = &routine->blocks[b].instructions[i];
                if (in->op != REGALLOC_M_SPILL_LOAD && in->op != REGALLOC_M_SPILL_STORE)
                    continue;
                fprintf(output, "  B%zu instruccion %zu: %s slot %zu "
                        "origen IR=%zu fuente=%d:%d\n", routine->blocks[b].id, i,
                        in->op == REGALLOC_M_SPILL_LOAD ? "SPILL_LOAD" : "SPILL_STORE",
                        in->slot, in->origin_instruction, in->location.first_line,
                        in->location.first_column);
            }
    }
    return ferror(output) ? REGALLOC_IO_ERROR : REGALLOC_OK;
}
