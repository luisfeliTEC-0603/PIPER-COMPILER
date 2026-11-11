/** @file cfg_print.c
 * C4: conexiones CFG y etiquetas del mismo IR, con formato textual compartido.
 * No lee .ir, modifica grafos ni depende de Graphviz para escribir DOT.
 */
#include "cfg.h"
#include "ir/ir_format.h"
#include <stdlib.h>

/* Los IDs de bloque se repiten entre rutinas; el prefijo r evita colisiones. */
static void write_node_id(FILE *output, size_t routine, const CfgNode *node)
{
    fprintf(output, "r%zu_", routine);
    if (node->kind == CFG_NODE_BLOCK) fprintf(output, "b%zu", node->block);
    else fputs(node->kind == CFG_NODE_ENTRY ? "entry" : "exit", output);
}

/* Formatear directamente desde estructuras y escapar el texto resultante.
 * El buffer solo dura esta etiqueta; nunca se interpreta como otro lenguaje IR.
 * block=NULL selecciona la cabecera de rutina; otro valor imprime ese bloque. */
static CfgStatus write_label(FILE *output, const IrPrintView *view, const IrBlock *block)
{
    char *text = NULL;
    size_t length = 0;
    FILE *buffer = open_memstream(&text, &length);
    if (buffer == NULL) return CFG_OUT_OF_MEMORY;
    IrStatus status = block ? ir_print_block(buffer, view, block) : ir_print_header(buffer, view);
    if (fclose(buffer) != 0) status = IR_IO_ERROR;
    if (status != IR_OK) {
        free(text);
        return status == IR_OUT_OF_MEMORY ? CFG_OUT_OF_MEMORY :
            status == IR_IO_ERROR ? CFG_IO_ERROR : CFG_INVALID_INPUT;
    }
    fputc('"', output);
    for (size_t i = 0; i < length; i++) {
        switch (text[i]) {
        case '"': fputs("\\\"", output); break;
        case '\\': fputs("\\\\", output); break;
        case '\n': fputs("\\l", output); break;
        case '\r': fputs("\\r", output); break;
        default: fputc((unsigned char)text[i], output); break;
        }
    }
    fputc('"', output);
    free(text);
    return ferror(output) ? CFG_IO_ERROR : CFG_OK;
}

/** Entrada: CFG completo de cfg_build e IR aún viva, sin modificaciones.
 * Salida: subgrafo por rutina, etiquetas TAC y aristas; no cierra el stream.
 * Alcance falso solo cambia estilo visual, no elimina nodos ni conexiones. */
CfgStatus cfg_write_dot(FILE *output, const CfgProgram *cfg)
{
    if (output == NULL || cfg == NULL || cfg->ir == NULL) return CFG_INVALID_INPUT;
    const char *labels[] = {"ENTRY", "JUMP", "TRUE", "FALSE", "RETURN"};
    fputs("digraph CFG {\n  rankdir=TB;\n  node [shape=box, fontname=\"monospace\"];\n", output);
    for (size_t r = 0; r < cfg->routine_count; r++) {
        const CfgRoutine *routine = &cfg->routines[r];
        IrPrintView view;
        IrStatus prepared = ir_print_prepare(&view, cfg->ir, routine->routine);
        if (prepared != IR_OK) return prepared == IR_OUT_OF_MEMORY ? CFG_OUT_OF_MEMORY : CFG_INVALID_INPUT;
        fprintf(output, "  subgraph cluster_r%zu {\n    label=", r);
        CfgStatus status = write_label(output, &view, NULL);
        if (status == CFG_OK) fputs(";\n    labelloc=\"t\";\n", output);
        for (size_t n = 0; n < routine->node_count && status == CFG_OK; n++) {
            const CfgNode *node = &routine->nodes[n];
            fputs("    ", output); write_node_id(output, r, node);
            fputs(" [", output);
            if (node->kind == CFG_NODE_BLOCK) {
                fputs("label=", output);
                /* C1/C3 garantizan que el nodo n representa blocks[n-1]. */
                status = write_label(output, &view, &view.routine->blocks[n - 1]);
            } else {
                fprintf(output, "shape=oval, label=\"%s\"", node->kind == CFG_NODE_ENTRY ? "ENTRY" : "EXIT");
            }
            if (!node->reachable) fputs(", style=dashed, color=gray, fontcolor=gray", output);
            fputs("];\n", output);
        }
        for (size_t e = 0; e < routine->edge_count && status == CFG_OK; e++) {
            const CfgEdge *edge = &routine->edges[e];
            fputs("    ", output); write_node_id(output, r, &routine->nodes[edge->source]);
            fputs(" -> ", output); write_node_id(output, r, &routine->nodes[edge->target]);
            fprintf(output, " [label=\"%s\"];\n", labels[edge->kind]);
        }
        free(view.values);
        if (status != CFG_OK) return status;
        fputs("  }\n", output);
    }
    fputs("}\n", output);
    return ferror(output) ? CFG_IO_ERROR : CFG_OK;
}
