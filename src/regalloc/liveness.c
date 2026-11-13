/** @file liveness.c
 * Analisis de temporales virtuales de IR, independiente de registros fisicos.
 *
 * Para cada bloque B se resuelve hasta un punto fijo:
 *   OUT[B] = union de IN[S] para cada sucesor S
 *   IN[B]  = USE[B] union (OUT[B] - DEF[B])
 * Despues se recorre cada bloque hacia atras para obtener los conjuntos que
 * rodean cada instruccion. Los ciclos no necesitan un tratamiento especial:
 * las iteraciones terminan cuando ninguna palabra de bits cambia.
 */
#include "regalloc_internal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BITS_PER_WORD 64u

static RegallocStatus live_report(RegallocDiagnostic *diagnostic,
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

/** Un ID t ocupa el bit t%64 de la palabra t/64. La ultima palabra puede
 * contener bits sobrantes; permanecen en cero porque solo se insertan IDs
 * validados por ir_validate. */
bool regalloc_set_contains(const RegallocSet *set, IrId temp)
{
    if (set == NULL || temp / BITS_PER_WORD >= set->word_count ||
        set->words == NULL) return false;
    return (set->words[temp / BITS_PER_WORD] &
            (UINT64_C(1) << (temp % BITS_PER_WORD))) != 0;
}

static void set_add(RegallocSet *set, IrId temp)
{
    set->words[temp / BITS_PER_WORD] |= UINT64_C(1) << (temp % BITS_PER_WORD);
}

static void set_remove(RegallocSet *set, IrId temp)
{
    set->words[temp / BITS_PER_WORD] &= ~(UINT64_C(1) << (temp % BITS_PER_WORD));
}

/** Cada conjunto es una vista de un segmento disjunto de set_storage. Con
 * cero temporales, cursor es NULL y no hacemos aritmetica de punteros sobre el. */
static RegallocSet take_set(uint64_t **cursor, size_t word_count)
{
    RegallocSet set = {.word_count = word_count};
    if (word_count != 0) {
        set.words = *cursor;
        *cursor += word_count;
    }
    return set;
}

/** Descartar conjuntos sin tocar los mapas preparados ni IR/CFG prestados.
 * Sirve despues de fallar en la mitad de una rutina y en el destructor. */
void regalloc_liveness_clear(RegallocAnalysis *analysis)
{
    if (analysis == NULL) return;
    analysis->liveness_ready = false;
    if (analysis->routines == NULL) return;
    for (size_t r = 0; r < analysis->routine_count; r++) {
        RegallocRoutine *routine = &analysis->routines[r];
        free(routine->instructions);
        free(routine->set_storage);
        routine->instructions = NULL;
        routine->set_storage = NULL;
        routine->instruction_count = routine->word_count = 0;
        for (size_t b = 0; b < routine->block_count; b++) {
            RegallocBlock *block = &routine->blocks[b];
            block->use = block->def = block->live_in = block->live_out =
                (RegallocSet){0};
            block->instruction_start = block->instruction_count = 0;
        }
    }
}

/** Reserva una lista plana de instrucciones y una unica arena de bits por
 * rutina. Primero calcula todos los tamanos con comprobaciones de overflow;
 * asi ninguna reserva parcial publica un conjunto con longitud incorrecta. */
static RegallocStatus allocate_routine(RegallocRoutine *target,
                                       const IrRoutine *source,
                                       RegallocDiagnostic *diagnostic)
{
    size_t instruction_count = 0;
    for (size_t b = 0; b < source->block_count; b++) {
        size_t count = source->blocks[b].instruction_count;
        if (count > SIZE_MAX - instruction_count)
            return live_report(diagnostic, REGALLOC_OUT_OF_MEMORY,
                               target->ir_routine, source->blocks[b].id,
                               "Demasiadas instrucciones para liveness.");
        instruction_count += count;
    }
    size_t word_count = target->temp_count / BITS_PER_WORD +
                        (target->temp_count % BITS_PER_WORD != 0);
    if (instruction_count > SIZE_MAX / sizeof(*target->instructions) ||
        instruction_count > SIZE_MAX / 2 ||
        source->block_count > (SIZE_MAX - instruction_count * 2) / 4)
        return live_report(diagnostic, REGALLOC_OUT_OF_MEMORY,
                           target->ir_routine, IR_NO_ID,
                           "Los conjuntos de liveness exceden el tamano representable.");
    size_t set_count = source->block_count * 4 + instruction_count * 2;
    if (word_count != 0 &&
        (set_count > SIZE_MAX / word_count ||
         set_count * word_count > SIZE_MAX / sizeof(uint64_t)))
        return live_report(diagnostic, REGALLOC_OUT_OF_MEMORY,
                           target->ir_routine, IR_NO_ID,
                           "La reserva de bits para liveness excede el tamano representable.");

    target->instructions = calloc(instruction_count, sizeof(*target->instructions));
    if (target->instructions == NULL)
        return live_report(diagnostic, REGALLOC_OUT_OF_MEMORY,
                           target->ir_routine, IR_NO_ID,
                           "No se pudieron reservar las instrucciones de liveness.");
    target->instruction_count = instruction_count;
    target->word_count = word_count;
    if (word_count != 0) {
        target->set_storage = calloc(set_count * word_count,
                                     sizeof(*target->set_storage));
        if (target->set_storage == NULL)
            return live_report(diagnostic, REGALLOC_OUT_OF_MEMORY,
                               target->ir_routine, IR_NO_ID,
                               "No se pudieron reservar los conjuntos de liveness.");
    }

    uint64_t *cursor = target->set_storage;
    size_t instruction = 0;
    for (size_t b = 0; b < target->block_count; b++) {
        RegallocBlock *block = &target->blocks[b];
        block->use = take_set(&cursor, word_count);
        block->def = take_set(&cursor, word_count);
        block->live_in = take_set(&cursor, word_count);
        block->live_out = take_set(&cursor, word_count);
        block->instruction_start = instruction;
        block->instruction_count = source->blocks[b].instruction_count;
        for (size_t i = 0; i < block->instruction_count; i++) {
            target->instructions[instruction].before = take_set(&cursor, word_count);
            target->instructions[instruction].after = take_set(&cursor, word_count);
            instruction++;
        }
    }
    return REGALLOC_OK;
}

/** La misma clasificacion de usos sirve para bloques e instrucciones.
 * defined es DEF[B] al construir USE[B], para omitir valores definidos antes
 * en ese bloque. Es NULL al calcular BEFORE de una instruccion individual. */
static void add_use(RegallocSet *uses, const RegallocSet *defined,
                    IrOperand operand)
{
    if (operand.kind == IR_OPERAND_TEMP &&
        (defined == NULL || !regalloc_set_contains(defined, operand.value.temp)))
        set_add(uses, operand.value.temp);
}

static void add_instruction_uses(RegallocSet *uses, const RegallocSet *defined,
                                 const IrInstruction *in)
{
    add_use(uses, defined, in->arg1);
    add_use(uses, defined, in->arg2);
    if (in->op == IR_OP_CALL || in->op == IR_OP_INTRINSIC)
        for (size_t a = 0; a < in->extra.call.argument_count; a++)
            add_use(uses, defined, in->extra.call.arguments[a]);
}

/** El resultado TEMP entra en DEF[B] despues de sus lecturas: una instruccion
 * puede usar un valor anterior al escribir su nuevo resultado. */
static void collect_use_def(RegallocBlock *target, const IrBlock *source)
{
    for (size_t i = 0; i < source->instruction_count; i++) {
        const IrInstruction *in = &source->instructions[i];
        add_instruction_uses(&target->use, &target->def, in);
        if (in->result.kind == IR_OPERAND_TEMP)
            set_add(&target->def, in->result.value.temp);
    }
}

/** El CFG validado ordena ENTRY, nodos BLOCK como IrRoutine.blocks y EXIT.
 * Cada sucesor BLOCK de un bloque IR tiene por ello indice de bloque n-1;
 * EXIT no aporta valores vivos. Se recorren todos los bloques, aun si son
 * inalcanzables desde ENTRY, para que la salida cubra toda la rutina. */
static void solve_blocks(RegallocRoutine *target, const CfgRoutine *graph)
{
    bool changed;
    do {
        changed = false;
        for (size_t b = target->block_count; b-- > 0;) {
            RegallocBlock *block = &target->blocks[b];
            const CfgNode *node = &graph->nodes[block->cfg_node];
            for (size_t w = 0; w < target->word_count; w++) {
                uint64_t out = 0;
                for (size_t s = 0; s < node->successor_count; s++) {
                    size_t index = node->successors[s];
                    if (graph->nodes[index].kind == CFG_NODE_BLOCK)
                        out |= target->blocks[index - 1].live_in.words[w];
                }
                uint64_t in = block->use.words[w] |
                              (out & ~block->def.words[w]);
                if (out != block->live_out.words[w] ||
                    in != block->live_in.words[w]) changed = true;
                block->live_out.words[w] = out;
                block->live_in.words[w] = in;
            }
        }
    } while (changed);
}

/** Para una instruccion: BEFORE = USE union (AFTER - DEF). result TEMP es la
 * unica definicion posible; result SYMBOL escribe una variable Piper.
 * Copiar AFTER antes de quitar la definicion conserva ambas vistas para pruebas
 * y para el futuro constructor de interferencias. */
static bool solve_instructions(RegallocRoutine *target, const IrRoutine *source,
                               IrId *bad_block)
{
    for (size_t b = 0; b < target->block_count; b++) {
        const RegallocBlock *block = &target->blocks[b];
        const RegallocSet *next = &block->live_out;
        for (size_t i = block->instruction_count; i-- > 0;) {
            const IrInstruction *in = &source->blocks[b].instructions[i];
            RegallocInstructionLive *live =
                &target->instructions[block->instruction_start + i];
            if (target->word_count != 0) {
                size_t bytes = target->word_count * sizeof(uint64_t);
                memcpy(live->after.words, next->words, bytes);
                memcpy(live->before.words, next->words, bytes);
                if (in->result.kind == IR_OPERAND_TEMP)
                    set_remove(&live->before, in->result.value.temp);
                add_instruction_uses(&live->before, NULL, in);
            }
            next = &live->before;
        }
        for (size_t w = 0; w < target->word_count; w++) {
            if (next->words[w] != block->live_in.words[w]) {
                *bad_block = block->id;
                return false;
            }
        }
    }
    return true;
}

/** Conserva los mapas R0 tras un fallo; los conjuntos se publican juntos solo
 * cuando todas las rutinas terminaron. La IR y el CFG prestados deben seguir
 * intactos entre prepare y esta llamada. */
RegallocStatus regalloc_compute_liveness(RegallocAnalysis *analysis,
                                        RegallocDiagnostic *diagnostic)
{
    if (analysis == NULL || analysis->ir == NULL || analysis->cfg == NULL ||
        analysis->cfg->ir != analysis->ir || analysis->routines == NULL ||
        analysis->routine_count != analysis->ir->routine_count ||
        analysis->liveness_ready)
        return live_report(diagnostic, REGALLOC_INVALID_INPUT, SIZE_MAX, IR_NO_ID,
                           "Se requiere un analisis preparado, aun sin liveness.");

    IrDiagnostic ir_error;
    IrStatus ir_status = ir_validate(analysis->ir, &ir_error);
    if (ir_status != IR_OK)
        return live_report(diagnostic,
                           ir_status == IR_OUT_OF_MEMORY ? REGALLOC_OUT_OF_MEMORY : REGALLOC_INVALID_IR,
                           ir_error.routine, ir_error.block, ir_error.message);
    CfgDiagnostic cfg_error;
    CfgStatus cfg_status = cfg_validate(analysis->cfg, &cfg_error);
    if (cfg_status != CFG_OK)
        return live_report(diagnostic,
                           cfg_status == CFG_OUT_OF_MEMORY ? REGALLOC_OUT_OF_MEMORY : REGALLOC_INVALID_CFG,
                           cfg_error.routine, cfg_error.block, cfg_error.message);

    for (size_t r = 0; r < analysis->routine_count; r++) {
        RegallocRoutine *target = &analysis->routines[r];
        const IrRoutine *source = &analysis->ir->routines[r];
        const CfgRoutine *graph = &analysis->cfg->routines[r];
        if (target->temp_count != source->temp_count ||
            target->block_count != source->block_count || target->blocks == NULL ||
            target->instructions != NULL || target->set_storage != NULL) {
            regalloc_liveness_clear(analysis);
            return live_report(diagnostic, REGALLOC_INVALID_INPUT, r, IR_NO_ID,
                               "Los mapas preparados ya no corresponden a la IR.");
        }
        for (size_t b = 0; b < target->block_count; b++) {
            if (target->blocks[b].id != source->blocks[b].id ||
                target->blocks[b].cfg_node != graph->entry + 1 + b) {
                regalloc_liveness_clear(analysis);
                return live_report(diagnostic, REGALLOC_INVALID_INPUT, r,
                                   source->blocks[b].id,
                                   "Un bloque cambio despues de preparar el analisis.");
            }
        }
        RegallocStatus status = allocate_routine(target, source, diagnostic);
        if (status != REGALLOC_OK) {
            regalloc_liveness_clear(analysis);
            return status;
        }
        for (size_t b = 0; b < target->block_count; b++)
            collect_use_def(&target->blocks[b], &source->blocks[b]);
        solve_blocks(target, graph);
        IrId bad_block = IR_NO_ID;
        if (!solve_instructions(target, source, &bad_block)) {
            regalloc_liveness_clear(analysis);
            return live_report(diagnostic, REGALLOC_INVALID_IR, r, bad_block,
                               "Liveness por instruccion no coincide con IN del bloque.");
        }
    }
    analysis->liveness_ready = true;
    return live_report(diagnostic, REGALLOC_OK, SIZE_MAX, IR_NO_ID, "");
}
