/** @file graph_dot.c
 * Vista DOT del grafo preliminar de interferencia IR.
 *
 * El archivo usa graph (no digraph): la interferencia es simetrica. Los IDs
 * rN_tM impiden colisiones entre rutinas y se emite cada arista solo cuando
 * a < b. Los nodos aislados tambien se escriben para que no desaparezcan.
 * El resultado se etiqueta como IR preliminar: aun no incluye los auxiliares
 * ni las restricciones que aparezcan en la bajada final a maquina.
 */
#include "regalloc.h"
#include <stdio.h>

static RegallocStatus dot_report(RegallocDiagnostic *diagnostic,
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

/** Los nombres de funcion llegan del programa Piper. DOT requiere escapar
 * comillas y barras invertidas dentro de una etiqueta entre comillas. */
static void write_escaped(FILE *output, const char *name)
{
    for (const unsigned char *p = (const unsigned char *)name; *p != 0; p++) {
        if (*p == '"' || *p == '\\') fputc('\\', output);
        if (*p == '\n' || *p == '\r') fputc(' ', output);
        else fputc(*p, output);
    }
}

RegallocStatus regalloc_write_ir_graph_dot(FILE *output,
                                          const RegallocAnalysis *analysis,
                                          RegallocDiagnostic *diagnostic)
{
    if (output == NULL || analysis == NULL || analysis->liveness == NULL ||
        analysis->ir == NULL)
        return dot_report(diagnostic, REGALLOC_INVALID_INPUT, SIZE_MAX,
                          "Se requiere liveness IR completo y un stream abierto.");

    fputs("graph InterferenciaIR {\n"
          "  graph [label=\"Interferencia IR preliminar\", labelloc=t, fontname=\"sans-serif\"];\n"
          "  node [shape=circle, fontname=\"monospace\"];\n",
          output);
    for (size_t r = 0; r < analysis->routine_count; r++) {
        RegallocIrGraph *graph = NULL;
        RegallocStatus status = regalloc_build_ir_graph(analysis, r,
                                                        &graph, diagnostic);
        if (status != REGALLOC_OK) return status;
        const IrRoutine *routine = &analysis->ir->routines[r];
        fprintf(output, "  subgraph cluster_r%zu {\n    label=\"rutina %zu: ", r, r);
        if (routine->symbol != NULL && routine->symbol->name[0] != '\0')
            write_escaped(output, routine->symbol->name);
        else if (routine->symbol == NULL)
            fputs("<startup>", output);
        else
            fputs("<sin nombre>", output);
        fputs("\";\n", output);
        for (IrId t = 0; t < graph->temp_count; t++)
            fprintf(output, "    r%zu_t%zu [label=\"t%zu\\n(grado %zu)\"];\n",
                    r, t, t, graph->degree[t]);
        for (IrId a = 0; a < graph->temp_count; a++)
            for (IrId b = a + 1; b < graph->temp_count; b++)
                if (regalloc_ir_graph_has_edge(graph, a, b))
                    fprintf(output, "    r%zu_t%zu -- r%zu_t%zu;\n", r, a, r, b);
        fputs("  }\n", output);
        regalloc_ir_graph_free(graph);
        if (ferror(output))
            return dot_report(diagnostic, REGALLOC_IO_ERROR, r,
                              "No se pudo escribir el grafo DOT.");
    }
    fputs("}\n", output);
    return dot_report(diagnostic, ferror(output) ? REGALLOC_IO_ERROR : REGALLOC_OK,
                      SIZE_MAX, ferror(output) ? "No se pudo escribir el grafo DOT." : "");
}
