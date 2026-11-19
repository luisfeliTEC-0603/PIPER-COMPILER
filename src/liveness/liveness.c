#include "liveness_internal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WORD_BITS = 64, BLOCK_SET_COUNT = LIVENESS_OUT + 1 };

struct LivenessSet {
    uint64_t *words;
    size_t word_count, value_count;
};

typedef struct { LivenessSet before, after; } LiveInstruction;
typedef struct {
    IrId id;
    LivenessSet sets[BLOCK_SET_COUNT];
    size_t instruction_start, instruction_count;
} LiveBlock;
typedef struct {
    size_t value_count, word_count, block_count;
    LiveBlock *blocks;
    LiveInstruction *instructions;
    uint64_t *storage;
} LiveRoutine;
struct LivenessResult {
    LiveRoutine *routines;
    size_t routine_count;
};

/** Ambos adaptadores poseen las listas de usos e instrucciones hasta que el
 * solver termina. Los resultados solo contienen bits propios, no estos datos. */
void live_input_free(LiveInput *input)
{
    if (input == NULL) return;
    for (size_t r = 0; r < input->routine_count; r++) {
        LiveInputRoutine *routine = &input->routines[r];
        for (size_t b = 0; b < routine->block_count; b++)
            free(routine->blocks[b].instructions);
        free(routine->blocks);
        free(routine->use_storage);
    }
    free(input->routines);
    *input = (LiveInput){0};
}

LivenessStatus live_report(LivenessDiagnostic *diagnostic, LivenessStatus status,
                           size_t routine, IrId block, const char *message)
{
    if (diagnostic != NULL) {
        *diagnostic = (LivenessDiagnostic){
            .status = status, .routine = routine, .block = block,
        };
        snprintf(diagnostic->message, sizeof(diagnostic->message), "%s", message);
    }
    return status;
}

bool liveness_set_contains(const LivenessSet *set, size_t value)
{
    return set != NULL && value < set->value_count &&
        (set->words[value / WORD_BITS] &
         (UINT64_C(1) << (value % WORD_BITS))) != 0;
}

void liveness_set_add(LivenessSet *set, size_t value)
{
    set->words[value / WORD_BITS] |= UINT64_C(1) << (value % WORD_BITS);
}

void liveness_set_remove(LivenessSet *set, size_t value)
{
    set->words[value / WORD_BITS] &= ~(UINT64_C(1) << (value % WORD_BITS));
}

static LivenessSet *allocate_set(size_t value_count, bool clear_words)
{
    LivenessSet *copy = calloc(1, sizeof(*copy));
    if (copy == NULL)
        return NULL;
    copy->word_count = value_count / WORD_BITS + (value_count % WORD_BITS != 0);
    copy->value_count = value_count;
    if (copy->word_count != 0) {
        copy->words = clear_words ? calloc(copy->word_count, sizeof(*copy->words))
                                 : malloc(copy->word_count * sizeof(*copy->words));
        if (copy->words == NULL) {
            free(copy);
            return NULL;
        }
    }
    return copy;
}

LivenessSet *liveness_set_create(size_t value_count)
{
    return allocate_set(value_count, true);
}

LivenessSet *liveness_set_clone(const LivenessSet *source)
{
    LivenessSet *copy = allocate_set(source->value_count, false);
    if (copy != NULL)
        liveness_set_copy(copy, source);
    return copy;
}

void liveness_set_copy(LivenessSet *target, const LivenessSet *source)
{
    if (target != source && source->word_count != 0)
        memcpy(target->words, source->words, source->word_count * sizeof(*source->words));
}

void liveness_set_clear(LivenessSet *set)
{
    if (set->word_count != 0)
        memset(set->words, 0, set->word_count * sizeof(*set->words));
}

void liveness_set_union(LivenessSet *target, const LivenessSet *source)
{
    for (size_t word = 0; word < target->word_count; word++)
        target->words[word] |= source->words[word];
}

bool liveness_set_equal(const LivenessSet *left, const LivenessSet *right)
{
    return left->word_count == 0 ||
        memcmp(left->words, right->words, left->word_count * sizeof(*left->words)) == 0;
}

void liveness_set_free(LivenessSet *set)
{
    if (set == NULL)
        return;
    free(set->words);
    free(set);
}

static LivenessSet take_set(uint64_t **cursor, const LiveRoutine *routine)
{
    LivenessSet set = {.word_count = routine->word_count,
                      .value_count = routine->value_count};
    if (routine->word_count != 0) {
        set.words = *cursor;
        *cursor += routine->word_count;
    }
    return set;
}

void liveness_result_free(LivenessResult *result)
{
    if (result == NULL)
        return;
    for (size_t r = 0; r < result->routine_count; r++) {
        free(result->routines[r].blocks);
        free(result->routines[r].instructions);
        free(result->routines[r].storage);
    }
    free(result->routines);
    free(result);
}

static bool allocate_routine(LiveRoutine *routine, const LiveInputRoutine *input)
{
    size_t instruction_count = 0;
    for (size_t b = 0; b < input->block_count; b++) {
        size_t count = input->blocks[b].instruction_count;
        if (count > SIZE_MAX - instruction_count)
            return false;
        instruction_count += count;
    }

    size_t value_count = input->value_count;
    size_t word_count = value_count / WORD_BITS + (value_count % WORD_BITS != 0);
    size_t block_count = input->block_count;

    if (instruction_count > SIZE_MAX / 2 ||
        block_count > (SIZE_MAX - 2 * instruction_count) / BLOCK_SET_COUNT)
        return false;

    size_t set_count = BLOCK_SET_COUNT * block_count + 2 * instruction_count;
    size_t bytes_per_set = word_count * sizeof(uint64_t);

    routine->blocks = calloc(block_count, sizeof(*routine->blocks));
    routine->instructions = calloc(instruction_count, sizeof(*routine->instructions));
    if (bytes_per_set != 0 && set_count != 0)
        routine->storage = calloc(set_count, bytes_per_set);
    if ((block_count != 0 && routine->blocks == NULL) ||
        (instruction_count != 0 && routine->instructions == NULL) ||
        (bytes_per_set != 0 && set_count != 0 && routine->storage == NULL))
        return false;

    routine->value_count = value_count;
    routine->word_count = word_count;
    routine->block_count = block_count;

    /* Cada conjunto recibe su segmento de la misma reserva. */
    uint64_t *cursor = routine->storage;
    size_t next_instruction = 0;
    for (size_t b = 0; b < block_count; b++) {
        LiveBlock *block = &routine->blocks[b];
        block->id = input->blocks[b].id;
        block->instruction_start = next_instruction;
        block->instruction_count = input->blocks[b].instruction_count;

        for (size_t s = 0; s < BLOCK_SET_COUNT; s++)
            block->sets[s] = take_set(&cursor, routine);
        for (size_t i = 0; i < block->instruction_count; i++) {
            LiveInstruction *live = &routine->instructions[next_instruction++];
            live->before = take_set(&cursor, routine);
            live->after = take_set(&cursor, routine);
        }
    }
    return true;
}

/* USE reúne los valores leídos antes de su primera definición en el bloque. */
static void collect_use_def(LiveRoutine *routine, const LiveInputRoutine *input)
{
    for (size_t b = 0; b < routine->block_count; b++) {
        const LiveInputBlock *block = &input->blocks[b];
        LivenessSet *use = &routine->blocks[b].sets[LIVENESS_USE];
        LivenessSet *def = &routine->blocks[b].sets[LIVENESS_DEF];

        for (size_t i = 0; i < block->instruction_count; i++) {
            const LiveInputInstruction *instruction = &block->instructions[i];
            for (size_t u = 0; u < instruction->use_count; u++) {
                size_t value = instruction->uses[u];
                if (!liveness_set_contains(def, value))
                    liveness_set_add(use, value);
            }
            if (instruction->definition != IR_NO_ID)
                liveness_set_add(def, instruction->definition);
        }
    }
}

/* OUT reúne los IN de los sucesores. IN = USE ∪ (OUT − DEF). */
static void solve_blocks(LiveRoutine *routine, const LiveInputRoutine *input)
{
    bool changed;
    do {
        changed = false;
        for (size_t b = routine->block_count; b-- > 0;) {
            const LiveInputBlock *block = &input->blocks[b];
            LivenessSet *sets = routine->blocks[b].sets;

            for (size_t w = 0; w < routine->word_count; w++) {
                uint64_t live_out = 0;
                for (size_t s = 0; s < block->successor_count; s++) {
                    size_t successor = block->successors[s];
                    live_out |= routine->blocks[successor].sets[LIVENESS_IN].words[w];
                }

                uint64_t live_in = sets[LIVENESS_USE].words[w] |
                                   (live_out & ~sets[LIVENESS_DEF].words[w]);
                if (live_out != sets[LIVENESS_OUT].words[w] ||
                    live_in != sets[LIVENESS_IN].words[w])
                    changed = true;
                sets[LIVENESS_OUT].words[w] = live_out;
                sets[LIVENESS_IN].words[w] = live_in;
            }
        }
    } while (changed);
}

/* BEFORE = (AFTER − definición) ∪ usos. */
static void solve_instructions(LiveRoutine *routine, const LiveInputRoutine *input)
{
    size_t set_bytes = routine->word_count * sizeof(uint64_t);
    if (set_bytes == 0)
        return;

    for (size_t b = 0; b < routine->block_count; b++) {
        LiveBlock *block = &routine->blocks[b];
        const LivenessSet *next = &block->sets[LIVENESS_OUT];

        for (size_t i = block->instruction_count; i-- > 0;) {
            const LiveInputInstruction *instruction = &input->blocks[b].instructions[i];
            LiveInstruction *live = &routine->instructions[block->instruction_start + i];

            memcpy(live->after.words, next->words, set_bytes);
            memcpy(live->before.words, next->words, set_bytes);
            if (instruction->definition != IR_NO_ID)
                liveness_set_remove(&live->before, instruction->definition);
            for (size_t u = 0; u < instruction->use_count; u++)
                liveness_set_add(&live->before, instruction->uses[u]);

            next = &live->before;
        }
    }
}

LivenessStatus live_solve(const LiveInput *input, LivenessResult **out,
                          LivenessDiagnostic *diagnostic)
{
    size_t r = SIZE_MAX;
    LivenessResult *result = calloc(1, sizeof(*result));
    if (result == NULL)
        goto out_of_memory;

    result->routines = calloc(input->routine_count, sizeof(*result->routines));
    if (input->routine_count != 0 && result->routines == NULL)
        goto out_of_memory;

    result->routine_count = input->routine_count;
    for (r = 0; r < result->routine_count; r++) {
        LiveRoutine *routine = &result->routines[r];
        const LiveInputRoutine *source = &input->routines[r];

        if (!allocate_routine(routine, source))
            goto out_of_memory;

        collect_use_def(routine, source);
        solve_blocks(routine, source);
        solve_instructions(routine, source);
    }

    *out = result;
    return live_report(diagnostic, LIVENESS_OK, SIZE_MAX, IR_NO_ID, "");

out_of_memory:
    liveness_result_free(result);
    return live_report(diagnostic, LIVENESS_OUT_OF_MEMORY, r, IR_NO_ID,
                       "No se pudo reservar el resultado de liveness.");
}

size_t liveness_routine_count(const LivenessResult *result)
{
    return result == NULL ? 0 : result->routine_count;
}

static const LiveRoutine *get_routine(const LivenessResult *result, size_t r)
{
    return result == NULL || r >= result->routine_count ? NULL : &result->routines[r];
}

static const LiveBlock *get_block(const LivenessResult *result, size_t r, size_t b)
{
    const LiveRoutine *routine = get_routine(result, r);
    return routine == NULL || b >= routine->block_count ? NULL : &routine->blocks[b];
}

size_t liveness_value_count(const LivenessResult *result, size_t r)
{
    const LiveRoutine *routine = get_routine(result, r);
    return routine == NULL ? 0 : routine->value_count;
}

size_t liveness_block_count(const LivenessResult *result, size_t r)
{
    const LiveRoutine *routine = get_routine(result, r);
    return routine == NULL ? 0 : routine->block_count;
}

IrId liveness_block_id(const LivenessResult *result, size_t r, size_t b)
{
    const LiveBlock *block = get_block(result, r, b);
    return block == NULL ? IR_NO_ID : block->id;
}

size_t liveness_instruction_count(const LivenessResult *result, size_t r, size_t b)
{
    const LiveBlock *block = get_block(result, r, b);
    return block == NULL ? 0 : block->instruction_count;
}

const LivenessSet *liveness_block_set(const LivenessResult *result, size_t r,
                                     size_t b, LivenessBlockSet which)
{
    const LiveBlock *block = get_block(result, r, b);
    return block == NULL || which < LIVENESS_USE || which > LIVENESS_OUT
        ? NULL : &block->sets[which];
}

const LivenessSet *liveness_instruction_set(const LivenessResult *result, size_t r,
                                           size_t b, size_t i, LivenessPoint point)
{
    const LiveBlock *block = get_block(result, r, b);
    if (block == NULL || i >= block->instruction_count ||
        (point != LIVENESS_BEFORE && point != LIVENESS_AFTER))
        return NULL;
    const LiveInstruction *in = &result->routines[r].instructions[block->instruction_start + i];
    return point == LIVENESS_BEFORE ? &in->before : &in->after;
}
