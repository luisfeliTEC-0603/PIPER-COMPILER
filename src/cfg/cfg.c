/** @file cfg.c
 * C1–C3: almacenamiento, conexiones y recorrido sobre IR prestada.
 */
#include "cfg_internal.h"
#include <stdlib.h>

/* Un único descarte de reservas parciales y copia del primer error localizado.
 * program es propio o NULL; con NULL solo se informa y el llamador conserva
 * la construcción para descartarla. Nunca liberar un programa previo del llamador. */
static CfgStatus cfg_error(CfgProgram *program, CfgDiagnostic *out,
                                CfgDiagnostic error, const char *message)
{
    cfg_program_free(program);
    snprintf(error.message, sizeof(error.message), "%s", message);
    if (out) *out = error;
    return error.status;
}

/** Reservar solo lo conocido por C1: rutinas y nodos. Los vecinos/aristas quedan
 * vacíos; C2 reservará sus tamaños al conocer las conexiones de terminadores.
 * Cada elemento se inicializa antes de que la limpieza pueda recorrerlo. */
CfgStatus cfg_program_create(const IrProgram *ir, CfgProgram **out,
                             CfgDiagnostic *diagnostic)
{
    CfgDiagnostic error = {.status = CFG_INVALID_INPUT,
        .routine = CFG_NO_INDEX, .block = IR_NO_ID,
        .instruction = CFG_NO_INDEX, .edge = CFG_NO_INDEX};
    if (out == NULL || *out != NULL || ir == NULL || ir->routine_count == 0 || ir->routines == NULL)
        return cfg_error(NULL, diagnostic, error, "Se requiere IR validada y salida CFG inicialmente NULL.");
    error.status = CFG_OUT_OF_MEMORY;
    if (ir->routine_count > SIZE_MAX / sizeof(CfgRoutine))
        return cfg_error(NULL, diagnostic, error, "Demasiadas rutinas para reservar CFG.");
    CfgProgram *program = calloc(1, sizeof(*program));
    if (program == NULL)
        return cfg_error(NULL, diagnostic, error, "No se pudo reservar el programa CFG.");
    program->ir = ir;
    program->routines = calloc(ir->routine_count, sizeof(*program->routines));
    if (program->routines == NULL)
        return cfg_error(program, diagnostic, error, "No se pudieron reservar las rutinas CFG.");
    program->routine_count = ir->routine_count;
    for (size_t r = 0; r < program->routine_count; r++) {
        const IrRoutine *source = &ir->routines[r];
        CfgRoutine *routine = &program->routines[r];
        error.routine = r;
        routine->routine = r;
        routine->entry = routine->exit = CFG_NO_INDEX;
        if (source->block_count == 0 || source->blocks == NULL) {
            error.status = CFG_INVALID_INPUT;
            return cfg_error(program, diagnostic, error, "La rutina IR no tiene bloques.");
        }
        if (source->block_count > SIZE_MAX - 2 ||
            source->block_count + 2 > SIZE_MAX / sizeof(CfgNode))
            return cfg_error(program, diagnostic, error, "Demasiados bloques para reservar nodos CFG.");
        size_t count = source->block_count + 2;
        routine->nodes = calloc(count, sizeof(*routine->nodes));
        if (routine->nodes == NULL)
            return cfg_error(program, diagnostic, error, "No se pudieron reservar los nodos CFG.");
        routine->node_count = count;
        routine->entry = 0;
        routine->exit = count - 1;
        routine->nodes[0] = (CfgNode){.kind = CFG_NODE_ENTRY, .block = IR_NO_ID};
        for (size_t b = 0; b < source->block_count; b++)
            routine->nodes[b + 1] = (CfgNode){.kind = CFG_NODE_BLOCK, .block = source->blocks[b].id};
        routine->nodes[routine->exit] = (CfgNode){.kind = CFG_NODE_EXIT, .block = IR_NO_ID};
    }
    *out = program;
    if (diagnostic) *diagnostic = (CfgDiagnostic){.status = CFG_OK,
        .routine = CFG_NO_INDEX, .block = IR_NO_ID,
        .instruction = CFG_NO_INDEX, .edge = CFG_NO_INDEX};
    return CFG_OK;
}

/* Resolver ID de bloque IR -> índice de nodo CFG dentro de una sola rutina.
 * No buscar por posición, nombre, etiqueta textual ni condición de la rama. */
static size_t block_node(const CfgRoutine *routine, IrId block)
{
    for (size_t n = 0; n < routine->node_count; n++)
        if (routine->nodes[n].kind == CFG_NODE_BLOCK && routine->nodes[n].block == block)
            return n;
    return CFG_NO_INDEX;
}

/* Derivar ambos sentidos desde las mismas aristas. Los grados reservan espacio
 * suficiente; luego contar solo vecinos distintos. Dos ramas al mismo destino
 * conservan ambas aristas, sin repetir vecino ni añadir dos predecesores iguales. */
static CfgStatus build_neighbors(CfgRoutine *routine, CfgDiagnostic *diagnostic)
{
    CfgDiagnostic error = {.status = CFG_OUT_OF_MEMORY, .routine = routine->routine,
        .block = IR_NO_ID, .instruction = CFG_NO_INDEX, .edge = CFG_NO_INDEX};
    for (size_t e = 0; e < routine->edge_count; e++) {
        routine->nodes[routine->edges[e].source].successor_count++;
        routine->nodes[routine->edges[e].target].predecessor_count++;
    }
    for (size_t n = 0; n < routine->node_count; n++) {
        CfgNode *node = &routine->nodes[n];
        error.block = node->block;
        if (node->successor_count > SIZE_MAX / sizeof(size_t) ||
            node->predecessor_count > SIZE_MAX / sizeof(size_t))
            return cfg_error(NULL, diagnostic, error, "Demasiados vecinos CFG para reservar memoria.");
        if (node->successor_count)
            node->successors = malloc(node->successor_count * sizeof(*node->successors));
        if (node->predecessor_count)
            node->predecessors = malloc(node->predecessor_count * sizeof(*node->predecessors));
        if ((node->successor_count && node->successors == NULL) ||
            (node->predecessor_count && node->predecessors == NULL))
            return cfg_error(NULL, diagnostic, error, "No se pudieron reservar los vecinos CFG.");
        node->successor_count = node->predecessor_count = 0;
    }
    for (size_t e = 0; e < routine->edge_count; e++) {
        CfgEdge edge = routine->edges[e];
        CfgNode *source = &routine->nodes[edge.source];
        CfgNode *target = &routine->nodes[edge.target];
        bool repeated = false;
        for (size_t n = 0; n < source->successor_count; n++)
            if (source->successors[n] == edge.target) repeated = true;
        if (!repeated) {
            source->successors[source->successor_count++] = edge.target;
            target->predecessors[target->predecessor_count++] = edge.source;
        }
    }
    return CFG_OK;
}

/** Completar almacenamiento nuevo de C1. No volver a validar tipos, firmas ni
 * temporales: solo consumir los terminadores necesarios para crear conexiones.
 * En fallo conserva la construcción parcial; su dueño debe llamar free.
 * cfg_build añade revisión/alcance antes de entregar el programa. */
CfgStatus cfg_connect(CfgProgram *cfg, CfgDiagnostic *diagnostic)
{
    CfgDiagnostic error = {.status = CFG_INVALID_INPUT,
        .routine = CFG_NO_INDEX, .block = IR_NO_ID,
        .instruction = CFG_NO_INDEX, .edge = CFG_NO_INDEX};
    if (cfg == NULL || cfg->ir == NULL || cfg->routines == NULL ||
        cfg->routine_count != cfg->ir->routine_count)
        return cfg_error(NULL, diagnostic, error, "Se requiere el almacenamiento CFG nuevo de C1.");
    for (size_t r = 0; r < cfg->routine_count; r++) {
        CfgRoutine *routine = &cfg->routines[r];
        const IrRoutine *ir = &cfg->ir->routines[r];
        error.routine = r;
        error.block = IR_NO_ID;
        error.instruction = error.edge = CFG_NO_INDEX;
        error.location = (SourceLocation){0};
        if (routine->edges != NULL || routine->edge_count != 0)
            return cfg_error(NULL, diagnostic, error, "No conectar dos veces el mismo CFG.");
        for (size_t n = 0; n < routine->node_count; n++)
            if (routine->nodes[n].successors || routine->nodes[n].predecessors ||
                routine->nodes[n].successor_count || routine->nodes[n].predecessor_count)
                return cfg_error(NULL, diagnostic, error, "Los vecinos CFG deben estar vacíos al conectar.");
        size_t count = 1; /* ENTRY -> entrada IR. */
        for (size_t b = 0; b < ir->block_count; b++) {
            const IrBlock *block = &ir->blocks[b];
            error.block = block->id;
            if (block->instruction_count == 0 || block->instructions == NULL)
                return cfg_error(NULL, diagnostic, error, "No se puede conectar un bloque IR sin terminador.");
            error.instruction = block->instruction_count - 1;
            const IrInstruction *last = &block->instructions[error.instruction];
            error.location = last->location;
            if (last->op != IR_OP_JUMP && last->op != IR_OP_BRANCH && last->op != IR_OP_RETURN)
                return cfg_error(NULL, diagnostic, error, "El bloque IR no termina en jump, branch o return.");
            size_t outgoing = last->op == IR_OP_BRANCH ? 2 : 1;
            if (count > SIZE_MAX - outgoing) {
                error.status = CFG_OUT_OF_MEMORY;
                return cfg_error(NULL, diagnostic, error, "Demasiadas conexiones CFG.");
            }
            count += outgoing;
        }
        error.block = IR_NO_ID;
        error.instruction = CFG_NO_INDEX;
        error.location = (SourceLocation){0};
        if (count > SIZE_MAX / sizeof(CfgEdge)) {
            error.status = CFG_OUT_OF_MEMORY;
            return cfg_error(NULL, diagnostic, error, "Demasiadas aristas CFG para reservar memoria.");
        }
        routine->edges = malloc(count * sizeof(*routine->edges));
        if (routine->edges == NULL) {
            error.status = CFG_OUT_OF_MEMORY;
            return cfg_error(NULL, diagnostic, error, "No se pudieron reservar las aristas CFG.");
        }
        size_t entry = block_node(routine, ir->entry);
        error.edge = 0;
        error.block = ir->entry;
        if (entry == CFG_NO_INDEX)
            return cfg_error(NULL, diagnostic, error, "El bloque de entrada IR no existe en esta rutina.");
        routine->edges[routine->edge_count++] = (CfgEdge){
            .source = routine->entry, .target = entry, .kind = CFG_EDGE_ENTRY};
        for (size_t b = 0; b < ir->block_count; b++) {
            const IrBlock *block = &ir->blocks[b];
            error.block = block->id;
            error.instruction = block->instruction_count - 1;
            const IrInstruction *last = &block->instructions[error.instruction];
            error.location = last->location;
            size_t paths = last->op == IR_OP_BRANCH ? 2 : 1;
            for (size_t path = 0; path < paths; path++) {
                size_t target = routine->exit;
                CfgEdgeKind kind = CFG_EDGE_RETURN;
                if (last->op != IR_OP_RETURN) {
                    target = block_node(routine, last->extra.targets[path]);
                    kind = CFG_EDGE_JUMP;
                    if (last->op == IR_OP_BRANCH) kind = path == 0 ? CFG_EDGE_TRUE : CFG_EDGE_FALSE;
                }
                error.edge = routine->edge_count;
                if (target == CFG_NO_INDEX)
                    return cfg_error(NULL, diagnostic, error, "El destino de salto no existe en esta rutina.");
                /* C1 colocó el bloque de posición b en el nodo b+1. Este índice
                 * no es el ID IR: puede representar B9, B3 o cualquier otro ID. */
                routine->edges[routine->edge_count++] = (CfgEdge){
                    .source = b + 1, .target = target, .kind = kind};
            }
        }
        CfgStatus status = build_neighbors(routine, diagnostic);
        if (status != CFG_OK) return status;
    }
    if (diagnostic) *diagnostic = (CfgDiagnostic){.status = CFG_OK,
        .routine = CFG_NO_INDEX, .block = IR_NO_ID,
        .instruction = CFG_NO_INDEX, .edge = CFG_NO_INDEX};
    return CFG_OK;
}

/* El grafo ya pasó cfg_validate: visitar vecinos por índice es seguro.
 * Marcar al apilar evita repetir nodos y limita la pila a node_count incluso
 * con ciclos. EXIT puede quedar sin alcanzar; no agregar conexiones artificiales. */
static CfgStatus mark_reachable(CfgProgram *cfg, CfgDiagnostic *diagnostic)
{
    for (size_t r = 0; r < cfg->routine_count; r++) {
        CfgRoutine *routine = &cfg->routines[r];
        CfgDiagnostic error = {.status = CFG_OUT_OF_MEMORY, .routine = r,
            .block = IR_NO_ID, .instruction = CFG_NO_INDEX, .edge = CFG_NO_INDEX};
        if (routine->node_count > SIZE_MAX / sizeof(size_t))
            return cfg_error(NULL, diagnostic, error, "Demasiados nodos para recorrer CFG.");
        size_t *pending = malloc(routine->node_count * sizeof(*pending));
        if (pending == NULL)
            return cfg_error(NULL, diagnostic, error, "No se pudo reservar la pila de recorrido CFG.");
        for (size_t n = 0; n < routine->node_count; n++) routine->nodes[n].reachable = false;
        size_t count = 0;
        pending[count++] = routine->entry;
        routine->nodes[routine->entry].reachable = true;
        while (count) {
            const CfgNode *node = &routine->nodes[pending[--count]];
            for (size_t s = 0; s < node->successor_count; s++) {
                size_t next = node->successors[s];
                if (!routine->nodes[next].reachable) {
                    routine->nodes[next].reachable = true;
                    pending[count++] = next;
                }
            }
        }
        free(pending);
    }
    return CFG_OK;
}

/** Entrada pública: reservar, conectar, revisar estructura y marcar alcance.
 * El llamador recibe propiedad solo en éxito; cualquier fallo descarta el
 * programa parcial y conserva el diagnóstico. IR y su tabla siguen prestadas. */
CfgStatus cfg_build(const IrProgram *ir, CfgProgram **out, CfgDiagnostic *diagnostic)
{
    CfgDiagnostic error = {.status = CFG_INVALID_INPUT,
        .routine = CFG_NO_INDEX, .block = IR_NO_ID,
        .instruction = CFG_NO_INDEX, .edge = CFG_NO_INDEX};
    if (out == NULL || *out != NULL)
        return cfg_error(NULL, diagnostic, error, "La salida CFG debe apuntar inicialmente a NULL.");
    CfgProgram *program = NULL;
    CfgStatus status = cfg_program_create(ir, &program, &error);
    if (status == CFG_OK) status = cfg_connect(program, &error);
    if (status == CFG_OK) status = cfg_validate(program, &error);
    if (status == CFG_OK) status = mark_reachable(program, &error);
    if (status == CFG_OK) *out = program;
    else cfg_program_free(program);
    if (diagnostic) *diagnostic = error;
    return status;
}

/** Aceptar NULL y reservas parciales. Solo liberar datos propios: nunca IR,
 * sus bloques, sus instrucciones ni símbolos. Los vecinos se liberan aunque
 * C2 solo haya alcanzado a reservar una de las dos listas de un nodo. */
void cfg_program_free(CfgProgram *cfg)
{
    if (cfg == NULL) return;
    for (size_t r = 0; r < cfg->routine_count; r++) {
        CfgRoutine *routine = &cfg->routines[r];
        for (size_t n = 0; n < routine->node_count; n++) {
            free(routine->nodes[n].successors);
            free(routine->nodes[n].predecessors);
        }
        free(routine->nodes);
        free(routine->edges);
    }
    free(cfg->routines);
    free(cfg);
}
