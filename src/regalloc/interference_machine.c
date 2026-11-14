/** @file interference_machine.c
 * Interferencias de temporales virtuales tras la bajada a maquina.
 *
 * Los conjuntos BEFORE/AFTER provienen del solver compartido. Dos valores
 * presentes en el mismo conjunto necesitan registros distintos. Una escritura
 * fisica impide ese color a los valores vivos a ambos lados de la instruccion.
 * Este modulo solo construye datos; no colorea ni inserta spills.
 */
#include "regalloc_machine.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum { GRAPH_WORD_BITS = 64 };

static RegallocStatus graph_error(RegallocDiagnostic *diagnostic,
                                  RegallocStatus status, size_t routine,
                                  const char *message)
{
    if (diagnostic != NULL) {
        *diagnostic = (RegallocDiagnostic){
            .status = status, .routine = routine, .block = IR_NO_ID
        };
        snprintf(diagnostic->message, sizeof(diagnostic->message), "%s", message);
    }
    return status;
}

void regalloc_machine_graph_free(RegallocMachineGraph *graph)
{
    if (graph == NULL) return;
    free(graph->neighbors);
    free(graph->degree);
    free(graph->forbidden_colors);
    free(graph);
}

bool regalloc_machine_graph_has_edge(const RegallocMachineGraph *graph,
                                    IrId a, IrId b)
{
    if (graph == NULL || a >= graph->temp_count || b >= graph->temp_count ||
        graph->neighbors == NULL) return false;
    return (graph->neighbors[a * graph->word_count + b / GRAPH_WORD_BITS] &
            (UINT64_C(1) << (b % GRAPH_WORD_BITS))) != 0;
}

/* Los IDs ya son validos por el contrato de liveness de maquina. */
static void add_edge(RegallocMachineGraph *graph, IrId a, IrId b)
{
    if (a == b || regalloc_machine_graph_has_edge(graph, a, b)) return;
    graph->neighbors[a * graph->word_count + b / GRAPH_WORD_BITS] |=
        UINT64_C(1) << (b % GRAPH_WORD_BITS);
    graph->neighbors[b * graph->word_count + a / GRAPH_WORD_BITS] |=
        UINT64_C(1) << (a % GRAPH_WORD_BITS);
    graph->degree[a]++;
    graph->degree[b]++;
}

/* BEFORE cubre operandos simultaneos, incluso cuando mueren en la operacion.
 * AFTER cubre resultados y valores que sobreviven a la operacion. */
static void add_live_pairs(RegallocMachineGraph *graph, const LivenessSet *live)
{
    for (IrId a = 0; a < graph->temp_count; a++) {
        if (!liveness_set_contains(live, a)) continue;
        for (IrId b = a + 1; b < graph->temp_count; b++)
            if (liveness_set_contains(live, b)) add_edge(graph, a, b);
    }
}

RegallocStatus regalloc_build_machine_graph(const RegallocMachineProgram *machine,
                                           const LivenessResult *live,
                                           size_t routine,
                                           RegallocMachineGraph **out,
                                           RegallocDiagnostic *diagnostic)
{
    if (machine == NULL || live == NULL || out == NULL || *out != NULL ||
        routine >= machine->routine_count ||
        liveness_routine_count(live) != machine->routine_count)
        return graph_error(diagnostic, REGALLOC_INVALID_INPUT, routine,
                           "Se requiere maquina, liveness correspondiente y salida NULL.");

    const RegallocMachineRoutine *source = &machine->routines[routine];
    size_t count = source->temp_count;
    if (liveness_value_count(live, routine) != count ||
        liveness_block_count(live, routine) != source->block_count)
        return graph_error(diagnostic, REGALLOC_INVALID_INPUT, routine,
                           "Liveness no corresponde a la rutina de maquina.");

    size_t words = count / GRAPH_WORD_BITS + (count % GRAPH_WORD_BITS != 0);
    if (count != 0 &&
        (count > SIZE_MAX / sizeof(size_t) ||
         count > SIZE_MAX / sizeof(unsigned) ||
         count > SIZE_MAX / words ||
         count * words > SIZE_MAX / sizeof(uint64_t)))
        return graph_error(diagnostic, REGALLOC_OUT_OF_MEMORY, routine,
                           "El grafo de maquina excede el tamano representable.");

    RegallocMachineGraph *graph = calloc(1, sizeof(*graph));
    if (graph == NULL)
        return graph_error(diagnostic, REGALLOC_OUT_OF_MEMORY, routine,
                           "No se pudo reservar el grafo de maquina.");
    graph->routine = routine;
    graph->temp_count = count;
    graph->word_count = words;
    if (count != 0) {
        graph->neighbors = calloc(count * words, sizeof(*graph->neighbors));
        graph->degree = calloc(count, sizeof(*graph->degree));
        graph->forbidden_colors = calloc(count, sizeof(*graph->forbidden_colors));
        if (graph->neighbors == NULL || graph->degree == NULL ||
            graph->forbidden_colors == NULL) {
            regalloc_machine_graph_free(graph);
            return graph_error(diagnostic, REGALLOC_OUT_OF_MEMORY, routine,
                               "No se pudo reservar la matriz del grafo de maquina.");
        }
    }

    for (size_t b = 0; b < source->block_count; b++) {
        const RegallocMachineBlock *block = &source->blocks[b];
        for (size_t i = 0; i < block->instruction_count; i++) {
            const LivenessSet *before = liveness_instruction_set(
                live, routine, b, i, LIVENESS_BEFORE);
            const LivenessSet *after = liveness_instruction_set(
                live, routine, b, i, LIVENESS_AFTER);
            add_live_pairs(graph, before);
            add_live_pairs(graph, after);

            /* Un valor que cruza una escritura fija no puede ocupar ese
             * registro. El resultado nuevo de la instruccion no cruza el
             * punto y se tratara segun la semantica de su opcode. */
            unsigned writes = block->instructions[i].fixed_writes &
                              REGALLOC_CANDIDATE_MASK;
            if (writes != 0)
                for (IrId t = 0; t < count; t++)
                    if (liveness_set_contains(before, t) &&
                        liveness_set_contains(after, t))
                        graph->forbidden_colors[t] |= writes;
        }
    }

    *out = graph;
    return graph_error(diagnostic, REGALLOC_OK, routine, "");
}
