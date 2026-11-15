#include "dce_internal.h"
#include <stdlib.h>

typedef struct {
    LivenessSet *live_in;
    size_t mark_start;
    bool queued;
} DceBlockState;

typedef struct {
    DceBlockState *blocks;
    size_t block_count;
    size_t *queue;
    size_t head, tail, pending_count;
    LivenessSet *live;
} DceWorklist;

/* Como no hay duplicados, alcanza una cola circular de block_count entradas. */
static void enqueue_block(DceWorklist *work, size_t block)
{
    if (work->blocks[block].queued)
        return;
    work->queue[work->tail++] = block;
    if (work->tail == work->block_count)
        work->tail = 0;
    work->blocks[block].queued = true;
    work->pending_count++;
}

static size_t dequeue_block(DceWorklist *work)
{
    size_t block = work->queue[work->head++];
    if (work->head == work->block_count)
        work->head = 0;
    work->blocks[block].queued = false;
    work->pending_count--;
    return block;
}

static void worklist_free(DceWorklist *work)
{
    for (size_t b = 0; b < work->block_count; b++)
        liveness_set_free(work->blocks[b].live_in);
    liveness_set_free(work->live);
    free(work->blocks);
    free(work->queue);
}

static bool prepare_worklist(DceWorklist *work, const IrRoutine *routine,
                             const LivenessResult *liveness, size_t routine_index,
                             size_t *next_mark)
{
    size_t block_count = routine->block_count;
    work->blocks = calloc(block_count, sizeof(*work->blocks));
    work->queue = calloc(block_count, sizeof(*work->queue));
    if (work->blocks == NULL || work->queue == NULL)
        return false;
    work->block_count = block_count;

    const LivenessSet *first_in = liveness_block_set(liveness, routine_index, 0, LIVENESS_IN);
    work->live = liveness_set_clone(first_in);
    if (work->live == NULL)
        return false;

    for (size_t b = 0; b < block_count; b++) {
        const LivenessSet *live_in = liveness_block_set(liveness, routine_index, b, LIVENESS_IN);
        work->blocks[b].live_in = liveness_set_clone(live_in);
        if (work->blocks[b].live_in == NULL)
            return false;
        /* Arrancar sin demandas evita que un ciclo conserve valores solo por
         * sus conjuntos anteriores. Las instrucciones necesarias agregan usos. */
        liveness_set_clear(work->blocks[b].live_in);
        work->blocks[b].mark_start = *next_mark;
        *next_mark += routine->blocks[b].instruction_count;
    }
    /* Empezar por el final favorece la propagacion hacia los predecesores. */
    for (size_t b = block_count; b-- > 0;)
        enqueue_block(work, b);
    return true;
}

static void add_operand_use(LivenessSet *live, IrOperand operand)
{
    if (operand.kind == IR_OPERAND_TEMP)
        liveness_set_add(live, operand.value.temp);
}

/* Solo se incorporan los usos de instrucciones que seguiremos ejecutando. */
static void update_live_before(LivenessSet *live, const IrInstruction *instruction)
{
    if (instruction->result.kind == IR_OPERAND_TEMP)
        liveness_set_remove(live, instruction->result.value.temp);
    add_operand_use(live, instruction->arg1);
    add_operand_use(live, instruction->arg2);
    if (instruction->op == IR_OP_CALL || instruction->op == IR_OP_INTRINSIC) {
        for (size_t argument = 0; argument < instruction->extra.call.argument_count; argument++)
            add_operand_use(live, instruction->extra.call.arguments[argument]);
    }
}

/* Con remove=NULL solo calcula IN; las marcas se escriben al estabilizarlo. */
static size_t scan_block_backward(const IrBlock *block, LivenessSet *live, bool *remove)
{
    size_t removed_count = 0;
    for (size_t i = block->instruction_count; i-- > 0;) {
        const IrInstruction *instruction = &block->instructions[i];
        bool can_remove = dce_can_remove_instruction(instruction, live);
        if (remove != NULL)
            remove[i] = can_remove;
        if (can_remove) {
            removed_count++;
            continue;
        }
        update_live_before(live, instruction);
    }
    return removed_count;
}

/* OUT actual es la union de los IN actuales de los sucesores reales. */
static void collect_live_out(DceWorklist *work, const CfgRoutine *cfg, const CfgNode *node)
{
    liveness_set_clear(work->live);
    for (size_t s = 0; s < node->successor_count; s++) {
        size_t successor = node->successors[s];
        if (cfg->nodes[successor].kind == CFG_NODE_BLOCK)
            liveness_set_union(work->live, work->blocks[successor - 1].live_in);
    }
}

static void enqueue_predecessors(DceWorklist *work, const CfgRoutine *cfg, const CfgNode *node)
{
    for (size_t p = 0; p < node->predecessor_count; p++) {
        size_t predecessor = node->predecessors[p];
        if (cfg->nodes[predecessor].kind == CFG_NODE_BLOCK)
            enqueue_block(work, predecessor - 1);
    }
}

static void solve_routine(DceWorklist *work, const IrRoutine *routine,
                           const CfgRoutine *cfg)
{
    while (work->pending_count != 0) {
        size_t block = dequeue_block(work);
        DceBlockState *state = &work->blocks[block];
        /* El CFG validado ordena ENTRY, bloques IR, EXIT. */
        const CfgNode *node = &cfg->nodes[block + 1];

        collect_live_out(work, cfg, node);
        scan_block_backward(&routine->blocks[block], work->live, NULL);
        if (!liveness_set_equal(state->live_in, work->live)) {
            liveness_set_copy(state->live_in, work->live);
            enqueue_predecessors(work, cfg, node);
        }
    }
}

static size_t mark_routine(DceWorklist *work, const IrRoutine *routine,
                           const CfgRoutine *cfg, bool *remove)
{
    size_t removed_count = 0;
    for (size_t block = 0; block < routine->block_count; block++) {
        const CfgNode *node = &cfg->nodes[block + 1];
        collect_live_out(work, cfg, node);
        removed_count += scan_block_backward(&routine->blocks[block], work->live,
                                             remove + work->blocks[block].mark_start);
    }
    return removed_count;
}

bool dce_mark_dead_instructions(const IrProgram *ir, const CfgProgram *cfg,
                                const LivenessResult *liveness, bool *remove,
                                size_t *removed_count)
{
    size_t next_mark = 0;
    size_t removed = 0;
    for (size_t r = 0; r < ir->routine_count; r++) {
        DceWorklist work = {0};
        if (!prepare_worklist(&work, &ir->routines[r], liveness, r, &next_mark)) {
            worklist_free(&work);
            return false;
        }
        solve_routine(&work, &ir->routines[r], &cfg->routines[r]);
        removed += mark_routine(&work, &ir->routines[r], &cfg->routines[r], remove);
        worklist_free(&work);
    }
    *removed_count = removed;
    return true;
}
