/** @file cfg_validate.c
 * C3: revisión del grafo construido, sin repetir semántica ni resolver destinos IR.
 */
#include "cfg.h"
#include <stdlib.h>

static CfgStatus graph_error(CfgDiagnostic *out, CfgDiagnostic error, const char *message)
{
    snprintf(error.message, sizeof(error.message), "%s", message);
    if (out) *out = error;
    return error.status;
}

static bool contains(const size_t *items, size_t count, size_t value)
{
    for (size_t i = 0; i < count; i++) if (items[i] == value) return true;
    return false;
}

/** Revisar índices, correspondencia de nodos, etiquetas y vecinos derivados.
 * No reconstruye aristas, busca destinos por ID ni recalcula alcanzabilidad.
 * Una máscara local por origen detecta conexiones faltantes o repetidas. */
CfgStatus cfg_validate(const CfgProgram *cfg, CfgDiagnostic *diagnostic)
{
    CfgDiagnostic error = {.status = CFG_INVALID_GRAPH,
        .routine = CFG_NO_INDEX, .block = IR_NO_ID,
        .instruction = CFG_NO_INDEX, .edge = CFG_NO_INDEX};
    if (diagnostic) *diagnostic = (CfgDiagnostic){.status = CFG_OK,
        .routine = CFG_NO_INDEX, .block = IR_NO_ID,
        .instruction = CFG_NO_INDEX, .edge = CFG_NO_INDEX};
    if (cfg == NULL || cfg->ir == NULL || cfg->ir->routines == NULL) {
        error.status = CFG_INVALID_INPUT;
        return graph_error(diagnostic, error, "Falta CFG o su IR prestada.");
    }
    if (cfg->routine_count == 0 || cfg->routines == NULL || cfg->routine_count != cfg->ir->routine_count)
        return graph_error(diagnostic, error, "Las rutinas CFG no corresponden a IR.");
    for (size_t r = 0; r < cfg->routine_count; r++) {
        const CfgRoutine *g = &cfg->routines[r];
        const IrRoutine *ir = &cfg->ir->routines[r];
        error.routine = r;
        error.block = IR_NO_ID;
        error.instruction = error.edge = CFG_NO_INDEX;
        error.location = (SourceLocation){0};
        if (ir->blocks == NULL || ir->block_count == 0 || ir->block_count > SIZE_MAX - 2 ||
            g->routine != r || g->node_count != ir->block_count + 2 || g->nodes == NULL ||
            g->edges == NULL || g->edge_count == 0 || g->entry != 0 || g->exit != g->node_count - 1)
            return graph_error(diagnostic, error, "Almacenamiento o correspondencia de rutina CFG inválidos.");
        /* C1 define este orden: ENTRY, nodos de bloques IR, EXIT. No confundir
         * el índice n con el ID del bloque, que puede ser disperso/reordenado. */
        for (size_t n = 0; n < g->node_count; n++) {
            const CfgNode *node = &g->nodes[n];
            error.block = node->block;
            if (n == g->entry || n == g->exit) {
                CfgNodeKind kind = n == g->entry ? CFG_NODE_ENTRY : CFG_NODE_EXIT;
                if (node->kind != kind || node->block != IR_NO_ID)
                    return graph_error(diagnostic, error, "Nodo ENTRY/EXIT inválido.");
            } else if (node->kind != CFG_NODE_BLOCK || node->block != ir->blocks[n - 1].id ||
                       ir->blocks[n - 1].instructions == NULL || ir->blocks[n - 1].instruction_count == 0)
                return graph_error(diagnostic, error, "El nodo BLOCK no corresponde al bloque IR.");
            const size_t *lists[] = {node->successors, node->predecessors};
            size_t counts[] = {node->successor_count, node->predecessor_count};
            for (size_t direction = 0; direction < 2; direction++) {
                if (counts[direction] > g->node_count || (counts[direction] && lists[direction] == NULL))
                    return graph_error(diagnostic, error, "Lista de vecinos CFG inválida.");
                for (size_t i = 0; i < counts[direction]; i++)
                    if (lists[direction][i] >= g->node_count || contains(lists[direction], i, lists[direction][i]))
                        return graph_error(diagnostic, error, "Vecino CFG inexistente o repetido.");
            }
        }
        unsigned char *seen = calloc(g->node_count, sizeof(*seen));
        if (seen == NULL) {
            error.status = CFG_OUT_OF_MEMORY;
            return graph_error(diagnostic, error, "No se pudo reservar la revisión de aristas CFG.");
        }
        const char *message = NULL;
        size_t pairs = 0;
        for (size_t e = 0; e < g->edge_count && message == NULL; e++) {
            CfgEdge edge = g->edges[e];
            error.edge = e;
            error.block = IR_NO_ID;
            error.instruction = CFG_NO_INDEX;
            error.location = (SourceLocation){0};
            if (edge.source >= g->node_count || edge.target >= g->node_count || edge.source == g->exit) {
                message = "Índice de arista CFG inválido.";
                break;
            }
            const CfgNode *source = &g->nodes[edge.source], *target = &g->nodes[edge.target];
            unsigned char bit = 1;
            bool matches, repeated_pair = false;
            if (edge.source == g->entry) {
                matches = edge.kind == CFG_EDGE_ENTRY && target->kind == CFG_NODE_BLOCK && target->block == ir->entry;
            } else {
                const IrBlock *block = &ir->blocks[edge.source - 1];
                error.block = block->id;
                error.instruction = block->instruction_count - 1;
                const IrInstruction *last = &block->instructions[error.instruction];
                error.location = last->location;
                if (last->op == IR_OP_RETURN) matches = edge.kind == CFG_EDGE_RETURN && edge.target == g->exit;
                else if (last->op == IR_OP_JUMP)
                    matches = edge.kind == CFG_EDGE_JUMP && target->kind == CFG_NODE_BLOCK && target->block == last->extra.targets[0];
                else {
                    size_t path = edge.kind == CFG_EDGE_FALSE ? 1 : 0;
                    bit = (unsigned char)(1u << path);
                    matches = last->op == IR_OP_BRANCH && (edge.kind == CFG_EDGE_TRUE || edge.kind == CFG_EDGE_FALSE) &&
                        target->kind == CFG_NODE_BLOCK && target->block == last->extra.targets[path];
                    repeated_pair = last->op == IR_OP_BRANCH && last->extra.targets[0] == last->extra.targets[1] && seen[edge.source];
                }
            }
            if (!matches || (seen[edge.source] & bit)) message = "Arista CFG repetida o distinta de su terminador.";
            else if (!contains(source->successors, source->successor_count, edge.target) ||
                     !contains(target->predecessors, target->predecessor_count, edge.source))
                message = "La arista no aparece en ambos sentidos de vecinos.";
            else {
                seen[edge.source] |= bit;
                if (!repeated_pair) pairs++;
            }
        }
        /* Los pares únicos ya están presentes en ambas listas. Sus totales
         * exactos descartan vecinos sobrantes sin reconstruir la adyacencia. */
        size_t successors = pairs, predecessors = pairs;
        for (size_t n = 0; n < g->node_count && message == NULL; n++) {
            const CfgNode *node = &g->nodes[n];
            error.edge = error.instruction = CFG_NO_INDEX;
            error.block = node->block;
            error.location = (SourceLocation){0};
            unsigned char expected = n == g->exit ? 0 : 1;
            if (node->kind == CFG_NODE_BLOCK) {
                const IrBlock *block = &ir->blocks[n - 1];
                IrOpcode op = block->instructions[block->instruction_count - 1].op;
                if (op == IR_OP_BRANCH) expected = 3;
            }
            if (seen[n] != expected) message = "Faltan conexiones CFG para un nodo.";
            else if (node->successor_count > successors || node->predecessor_count > predecessors)
                message = "Hay vecinos CFG sin arista correspondiente.";
            else { successors -= node->successor_count; predecessors -= node->predecessor_count; }
        }
        if (message == NULL && (successors || predecessors)) message = "Cantidades de vecinos CFG inconsistentes.";
        free(seen);
        if (message) return graph_error(diagnostic, error, message);
    }
    return CFG_OK;
}
