/** @file interference.c
 * Grafo preliminar de interferencia de temporales IR.
 *
 * Una arista significa que dos valores pueden necesitar almacenamiento en
 * registros al mismo tiempo. Se parte de los conjuntos calculados en R1:
 *   - todos los valores vivos a la entrada de un bloque son incompatibles;
 *   - un resultado definido es incompatible con cada valor vivo justo despues.
 * Las aristas son simetricas y nunca conectan un temporal consigo mismo.
 *
 * La union de sucesores del liveness puede producir aristas conservadoras en
 * ramas. Este grafo NO es aun el que se coloreara para emitir codigo: faltan
 * instrucciones virtuales de maquina y sus registros implicitos.
 */
#include "regalloc_internal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define GRAPH_BITS_PER_WORD 64u

static RegallocStatus graph_report(RegallocDiagnostic *diagnostic,
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

void regalloc_ir_graph_free(RegallocIrGraph *graph)
{
    if (graph == NULL) return;
    free(graph->neighbors);
    free(graph->degree);
    free(graph);
}

bool regalloc_ir_graph_has_edge(const RegallocIrGraph *graph, IrId a, IrId b)
{
    if (graph == NULL || a >= graph->temp_count || b >= graph->temp_count ||
        graph->neighbors == NULL) return false;
    return (graph->neighbors[a * graph->word_count + b / GRAPH_BITS_PER_WORD] &
            (UINT64_C(1) << (b % GRAPH_BITS_PER_WORD))) != 0;
}

/** Inserta ambos sentidos una sola vez, incluso si el mismo par aparece en
 * varios bloques. El grado cuenta vecinos distintos y servira para estudiar
 * la simplificacion, no para elegir todavia un color. */
static void add_edge(RegallocIrGraph *graph, IrId a, IrId b)
{
    if (a == b || regalloc_ir_graph_has_edge(graph, a, b)) return;
    graph->neighbors[a * graph->word_count + b / GRAPH_BITS_PER_WORD] |=
        UINT64_C(1) << (b % GRAPH_BITS_PER_WORD);
    graph->neighbors[b * graph->word_count + a / GRAPH_BITS_PER_WORD] |=
        UINT64_C(1) << (a % GRAPH_BITS_PER_WORD);
    graph->degree[a]++;
    graph->degree[b]++;
}

/** Al entrar en un bloque no hay una definicion local que permita descubrir
 * todos los pares. Este clique conserva valores que llegan vivos desde
 * predecesores distintos, incluidos los que atraviesan un ciclo. */
static void add_live_in_pairs(RegallocIrGraph *graph, const RegallocSet *live_in)
{
    for (IrId a = 0; a < graph->temp_count; a++) {
        if (!regalloc_set_contains(live_in, a)) continue;
        for (IrId b = a + 1; b < graph->temp_count; b++)
            if (regalloc_set_contains(live_in, b)) add_edge(graph, a, b);
    }
}

RegallocStatus regalloc_build_ir_graph(const RegallocAnalysis *analysis,
                                      size_t routine, RegallocIrGraph **out,
                                      RegallocDiagnostic *diagnostic)
{
    if (analysis == NULL || !analysis->liveness_ready ||
        routine >= analysis->routine_count || out == NULL || *out != NULL)
        return graph_report(diagnostic, REGALLOC_INVALID_INPUT, routine,
                            IR_NO_ID, "Se requiere liveness completo y salida NULL.");

    const RegallocRoutine *live = &analysis->routines[routine];
    size_t count = live->temp_count;
    size_t words = count / GRAPH_BITS_PER_WORD +
                   (count % GRAPH_BITS_PER_WORD != 0);
    if (count != 0 &&
        (count > SIZE_MAX / sizeof(size_t) ||
         count > SIZE_MAX / words ||
         count * words > SIZE_MAX / sizeof(uint64_t)))
        return graph_report(diagnostic, REGALLOC_OUT_OF_MEMORY, routine,
                            IR_NO_ID, "El grafo excede el tamano representable.");

    RegallocIrGraph *graph = calloc(1, sizeof(*graph));
    if (graph == NULL)
        return graph_report(diagnostic, REGALLOC_OUT_OF_MEMORY, routine,
                            IR_NO_ID, "No se pudo reservar el grafo.");
    graph->ir_routine = routine;
    graph->temp_count = count;
    graph->word_count = words;
    if (count != 0) {
        graph->neighbors = calloc(count * words, sizeof(*graph->neighbors));
        graph->degree = calloc(count, sizeof(*graph->degree));
        if (graph->neighbors == NULL || graph->degree == NULL) {
            regalloc_ir_graph_free(graph);
            return graph_report(diagnostic, REGALLOC_OUT_OF_MEMORY, routine,
                                IR_NO_ID, "No se pudo reservar la matriz del grafo.");
        }
    }

    const IrRoutine *source = &analysis->ir->routines[routine];
    for (size_t b = 0; b < live->block_count; b++) {
        const RegallocBlock *block = &live->blocks[b];
        const IrBlock *ir_block = &source->blocks[b];
        add_live_in_pairs(graph, &block->live_in);
        for (size_t i = 0; i < block->instruction_count; i++) {
            const IrInstruction *in = &ir_block->instructions[i];
            if (in->result.kind != IR_OPERAND_TEMP) continue;
            IrId defined = in->result.value.temp;
            const RegallocSet *after =
                &live->instructions[block->instruction_start + i].after;
            /* Un resultado que jamas se vuelve a leer no necesita conservar
             * registro. La operacion puede seguir teniendo efectos, pero su
             * valor muerto no bloquea colores de otros temporales. */
            if (!regalloc_set_contains(after, defined)) continue;
            for (IrId other = 0; other < count; other++)
                if (regalloc_set_contains(after, other))
                    add_edge(graph, defined, other);
        }
    }

    *out = graph;
    return graph_report(diagnostic, REGALLOC_OK, routine, IR_NO_ID, "");
}
