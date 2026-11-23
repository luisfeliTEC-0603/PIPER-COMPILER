/** Recoloreo tras la seleccion completa. Se visita cada temporal una vez.
 * El mapa de colores es la unica entrada del emisor que cambia. */
#include "regalloc_machine.h"
#include <stdlib.h>

typedef struct {
    size_t block;
    uint8_t original;
} TempLocation;

static IrId second_use(const RegallocMachineInstruction *in)
{
    return in->op == REGALLOC_M_STORE_INDIRECT ? in->use2 : IR_NO_ID;
}

/* Copias que op_move omite si los registros coinciden. Separar los extremos
 * de una sola copia asi ya anula el candidato, incluso si ahorra otra copia. */
static bool splits_elided_move(const RegallocMachineInstruction *in,
                              IrId temp, const uint8_t *colors)
{
    if (in->use1 != temp && in->def != temp) return false;
    switch (in->op) {
    case REGALLOC_M_MOVE:
    case REGALLOC_M_CHECKED_COPY:
        return in->def != in->use1 && colors[in->def] == colors[in->use1];
    case REGALLOC_M_CALL_ARG:
        return in->call_arg_index < REGALLOC_REGISTER_ARGS &&
               colors[in->use1] == in->call_arg_index + 1;
    case REGALLOC_M_TO_RETURN:
    case REGALLOC_M_CRYPTO_WORD:
    case REGALLOC_M_CRYPTO_WRITE:
        return colors[in->use1] == 1;
    case REGALLOC_M_CRYPTO_FEISTEL:
        return colors[in->use1] == 3;
    default:
        return false;
    }
}

/* El bit separado identifica AL temporal candidato aunque otra identidad tenga
 * su color original. Una dependencia del temporal consigo mismo nunca puntua. */
enum { TEMP_BIT = 1u << (REGALLOC_COLOR_COUNT + 1) };

static unsigned operand_mask(IrId operand, IrId temp, const uint8_t *colors)
{
    if (operand == IR_NO_ID) return 0;
    return operand == temp ? TEMP_BIT : 1u << colors[operand];
}

static unsigned other_colors(unsigned earlier, unsigned later)
{
    unsigned others = 0;
    if (earlier & TEMP_BIT) others |= later;
    if (later & TEMP_BIT) others |= earlier;
    return others & REGALLOC_COLOR_MASK;
}

static uint8_t choose_register(const RegallocMachineBlock *block, IrId temp,
                               const uint8_t *colors, unsigned allowed)
{
    uint8_t original = colors[temp];
    if (allowed == (1u << original)) return original;
    unsigned prior_reads = 0, prior_writes = 0, conflicts = 0;
    for (size_t i = 0; i < block->instruction_count; i++) {
        const RegallocMachineInstruction *in = &block->instructions[i];
        if (splits_elided_move(in, temp, colors)) return original;
        if (in->op == REGALLOC_M_MOVE && colors[in->def] == colors[in->use1])
            continue;
        unsigned writes = operand_mask(in->def, temp, colors);
        unsigned reads = operand_mask(in->use1, temp, colors) |
                         operand_mask(second_use(in), temp, colors);
        if (regalloc_machine_reads_def(in->op)) reads |= writes;
        conflicts |= other_colors(prior_reads, writes);  /* WAR */
        conflicts |= other_colors(prior_writes, writes); /* WAW */
        prior_reads |= reads;
        prior_writes |= writes;
    }
    /* Exigir un conflicto real entre identidades en el color de partida y
     * ninguno en el nuevo. En un empate sin mejora se conserva el coloreo. */
    if (conflicts & (1u << original))
        for (uint8_t c = 1; c <= REGALLOC_COLOR_COUNT; c++)
            if ((allowed & ~conflicts) & (1u << c)) return c;
    return original;
}

static bool changed(IrId t, const TempLocation *temps, const uint8_t *colors)
{
    return t != IR_NO_ID && temps[t].original != colors[t];
}

bool regalloc_rename_locals(const RegallocMachineRoutine *routine,
                            const LivenessResult *live, const RegallocMachineGraph *graph,
                            uint8_t *colors, RegallocRoutineReport *report)
{
    size_t count = graph->temp_count;
    if (count == 0) return true;
    TempLocation *temps = calloc(count, sizeof(*temps));
    if (temps == NULL) return false;
    for (IrId t = 0; t < count; t++)
        temps[t] = (TempLocation){.block = SIZE_MAX, .original = colors[t]};

    /* USE incluye las lecturas antes de la primera definicion. Junto con DEF
     * cubre todas las menciones, incluso redefiniciones muertas en otro bloque. */
    for (size_t b = 0; b < routine->block_count; b++) {
        const LivenessSet *use = liveness_block_set(live, graph->routine, b, LIVENESS_USE);
        const LivenessSet *def = liveness_block_set(live, graph->routine, b, LIVENESS_DEF);
        const LivenessSet *in = liveness_block_set(live, graph->routine, b, LIVENESS_IN);
        const LivenessSet *out = liveness_block_set(live, graph->routine, b, LIVENESS_OUT);
        for (IrId t = 0; t < count; t++) {
            if (!liveness_set_contains(use, t) && !liveness_set_contains(def, t)) continue;
            if (temps[t].block == SIZE_MAX && !liveness_set_contains(in, t) &&
                !liveness_set_contains(out, t))
                temps[t].block = b;
            else
                temps[t].block = routine->block_count;
        }
    }
    size_t changes = 0;
    for (IrId t = 0; t < count; t++) {
        if (temps[t].block >= routine->block_count) continue;
        unsigned allowed = regalloc_available_colors(graph, t, colors);
        colors[t] = choose_register(&routine->blocks[temps[t].block], t, colors, allowed);
        changes += changed(t, temps, colors);
    }
    if (changes != 0) {
        RegallocRename *renames = calloc(changes, sizeof(*renames));
        if (renames == NULL) {
            for (IrId t = 0; t < count; t++) colors[t] = temps[t].original;
            free(temps);
            return false;
        }
        report->renames = renames;
        for (IrId t = 0; t < count; t++)
            if (changed(t, temps, colors))
                renames[report->rename_count++] = (RegallocRename){
                    .block = routine->blocks[temps[t].block].id, .temp = t,
                    .old_register = temps[t].original, .new_register = colors[t]
                };
        for (size_t b = 0; b < routine->block_count; b++) {
            const RegallocMachineBlock *block = &routine->blocks[b];
            for (size_t i = 0; i < block->instruction_count; i++) {
                const RegallocMachineInstruction *in = &block->instructions[i];
                if (changed(in->def, temps, colors) || changed(in->use1, temps, colors) ||
                    changed(second_use(in), temps, colors))
                    report->affected_operations++;
            }
        }
    }
    free(temps);
    return true;
}
