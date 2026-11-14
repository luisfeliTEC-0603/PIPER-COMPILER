/** @file color_machine.c
 * Simplificacion y seleccion de colores del grafo virtual de una rutina.
 *
 * Se trabaja sobre grados activos: retirar un nodo no cambia el grafo original.
 * La pila conserva el orden de retiro y se recorre al reves para colorear.
 * Elegir un candidato de spill solo permite seguir simplificando; el spill
 * efectivo se conoce al intentar asignarle un color durante la seleccion.
 */
#include "regalloc_machine.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static RegallocStatus color_error(RegallocDiagnostic *diagnostic,
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

void regalloc_coloring_free(RegallocColoring *coloring)
{
    if (coloring == NULL) return;
    free(coloring->steps);
    free(coloring->colors);
    free(coloring);
}

static unsigned eligible_count(unsigned forbidden)
{
    unsigned allowed = REGALLOC_COLOR_MASK & ~forbidden;
    unsigned count = 0;
    for (unsigned c = 1; c <= REGALLOC_COLOR_COUNT; c++)
        if ((allowed & (1u << c)) != 0) count++;
    return count;
}

RegallocStatus regalloc_color_machine_graph(const RegallocMachineGraph *graph,
                                            RegallocColoring **out,
                                            RegallocDiagnostic *diagnostic)
{
    if (graph == NULL || out == NULL || *out != NULL)
        return color_error(diagnostic, REGALLOC_INVALID_INPUT, SIZE_MAX,
                           "Se requiere grafo y salida NULL para colorear.");

    size_t count = graph->temp_count;
    RegallocColoring *result = calloc(1, sizeof(*result));
    bool *active = NULL;
    size_t *degree = NULL;
    if (result == NULL)
        return color_error(diagnostic, REGALLOC_OUT_OF_MEMORY, graph->routine,
                           "No se pudo reservar el resultado del coloreo.");
    result->temp_count = count;
    if (count != 0) {
        result->steps = calloc(count, sizeof(*result->steps));
        result->colors = calloc(count, sizeof(*result->colors));
        active = calloc(count, sizeof(*active));
        degree = malloc(count * sizeof(*degree));
        if (result->steps == NULL || result->colors == NULL ||
            active == NULL || degree == NULL) {
            free(active);
            free(degree);
            regalloc_coloring_free(result);
            return color_error(diagnostic, REGALLOC_OUT_OF_MEMORY, graph->routine,
                               "No se pudo reservar la pila de simplificacion.");
        }
        for (size_t t = 0; t < count; t++) {
            active[t] = true;
            degree[t] = graph->degree[t];
        }
    }

    for (size_t top = 0; top < count; top++) {
        size_t chosen = SIZE_MAX;
        bool potential = false;
        for (size_t t = 0; t < count; t++)
            if (active[t] && degree[t] < eligible_count(graph->forbidden_colors[t])) {
                chosen = t;
                break;
            }
        if (chosen == SIZE_MAX) {
            potential = true;
            for (size_t t = 0; t < count; t++)
                if (active[t] &&
                    (chosen == SIZE_MAX || degree[t] > degree[chosen]))
                    chosen = t;
        }
        assert(chosen != SIZE_MAX);
        result->steps[top] = (RegallocSimplifyStep){
            .temp = chosen, .active_degree = degree[chosen],
            .eligible_colors = eligible_count(graph->forbidden_colors[chosen]),
            .potential_spill = potential
        };
        active[chosen] = false;
        for (size_t t = 0; t < count; t++)
            if (active[t] && regalloc_machine_graph_has_edge(graph, chosen, t)) {
                assert(degree[t] != 0);
                degree[t]--;
            }
    }
    free(active);
    free(degree);

    for (size_t top = count; top != 0; top--) {
        size_t t = result->steps[top - 1].temp;
        unsigned allowed = REGALLOC_COLOR_MASK & ~graph->forbidden_colors[t];
        for (size_t neighbor = 0; neighbor < count; neighbor++)
            if (result->colors[neighbor] != 0 &&
                regalloc_machine_graph_has_edge(graph, t, neighbor))
                allowed &= ~(1u << result->colors[neighbor]);
        for (unsigned c = 1; c <= REGALLOC_COLOR_COUNT; c++)
            if ((allowed & (1u << c)) != 0) {
                result->colors[t] = (uint8_t)c;
                break;
            }
        if (result->colors[t] == 0) result->uncolored_count++;
    }
    *out = result;
    return color_error(diagnostic, REGALLOC_OK, graph->routine, "");
}
