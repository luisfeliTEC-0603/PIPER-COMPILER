#include "internal.h"

#include <stdlib.h>

/**
 * Ocupación de un flanco de WB reconstruida desde la salida.
 * channels representa puertos físicos; registers representa destinos GPR.
 * Son restricciones distintas: también puede colisionar un canal con destinos diferentes.
 */
typedef struct {
    unsigned int channels;
    uint8_t registers;
} writeback_t;

/** Identifica la unidad desde el formato y el pipe ya codificado. */
static int instruction_unit(const porygon_instruction_t *in, unsigned int pipe)
{
    switch (in->format) {
    case PORYGON_FORMAT_MATH: return (int)pipe;
    case PORYGON_FORMAT_LOST: return 2;
    case PORYGON_FORMAT_JUMP: return 3;
    case PORYGON_FORMAT_CRYPTO: return 4;
    case PORYGON_FORMAT_NOP: return -1;
    }
    return -1;
}

/** Comprueba que ningún puerto compartido ni registro reciba dos resultados en un flanco. */
static bool record_writeback(writeback_t *calendar, size_t cycle,
                             const porygon_machine_instruction_t *mi, unsigned int pipe)
{
    unsigned int channels = 0;
    size_t wb = cycle + schedule_wb_latency(&mi->instruction);
    if (mi->instruction.format == PORYGON_FORMAT_MATH) channels = 1u << pipe;
    if (mi->memory_effect == PORYGON_MEMORY_READ) channels = 1u << 2;
    if (mi->instruction.format == PORYGON_FORMAT_CRYPTO &&
        mi->instruction.funct == PORYGON_CRYPTO_FEISTEL) channels = (1u << 3) | (1u << 4);
    /* Una FEISTEL aporta hasta dos escrituras, no un único canal por instrucción.
       Las defs omiten r0, pero la reserva del productor sigue existiendo. */
    if ((calendar[wb].channels & channels) || (calendar[wb].registers & mi->defs)) return false;
    calendar[wb].channels |= channels;
    calendar[wb].registers |= mi->defs;
    return true;
}

/**
 * Contrasta cada par en orden original con sus emisiones y WB reconstruidos.
 * No llama a dependencies_ready: detecta violaciones en un calendario ya formado,
 * aunque haya sido modificado después de que el planificador lo aceptara.
 */
static porygon_schedule_result_t verify_dependencies(
    const porygon_machine_instruction_t *instructions, const size_t *issued, size_t count)
{
    size_t i, j;
    for (i = 0; i < count; ++i) {
        const porygon_machine_instruction_t *current = &instructions[i];
        size_t time = issued[i];
        size_t wb = time + schedule_wb_latency(&current->instruction);
        for (j = 0; j < i; ++j) {
            const porygon_machine_instruction_t *old = &instructions[j];
            size_t old_wb = issued[j] + schedule_wb_latency(&old->instruction);
            const char *error = NULL;
            if ((old->defs & current->uses) && time <= old_wb) error = "RAW antes de disponibilidad en RF";
            if ((old->uses & current->defs) && time < issued[j]) error = "WAR: se adelantó la escritura a la lectura";
            if ((old->defs & current->defs) && wb <= old_wb) error = "WAW fuera de orden o simultáneo";
            if (old->memory_effect != PORYGON_MEMORY_NONE &&
                current->memory_effect != PORYGON_MEMORY_NONE && time <= issued[j]) {
                error = "Se alteró el orden de memoria potencialmente aliasada";
            }
            if ((old->state_defs & current->state_uses) && time <= issued[j]) {
                error = "Se alteró el orden de estado CRYPTO";
            }
            if (error != NULL) {
                return schedule_result(PORYGON_SCHEDULE_DEPENDENCY, i, time, SIZE_MAX, error);
            }
            if (current->ends_block && (issued[j] > time ||
                (old->defs && old_wb >= time + PORYGON_BRANCH_NOP_BUNDLES + 1))) {
                return schedule_result(PORYGON_SCHEDULE_CONTROL, i, time, SIZE_MAX,
                                       "El salto abandona operaciones o resultados pendientes");
            }
        }
    }
    return schedule_result(PORYGON_SCHEDULE_OK, SIZE_MAX, SIZE_MAX, SIZE_MAX, "OK");
}

/**
 * Reconstruye dónde comienza y termina cada bloque en el calendario físico.
 * La entrada puede incluir esperas anteriores a su primera operación; una etiqueta
 * debe apuntar a esa entrada y no saltarse dichas esperas.
 */
static porygon_schedule_result_t verify_control(const porygon_machine_program_t *p,
    const porygon_schedule_t *s, const size_t *issued)
{
    size_t b, i, start = 0;
    for (b = 0; b < p->block_count; ++b) {
        const porygon_machine_block_t *block = &p->blocks[b];
        size_t end = block->first_instruction + block->instruction_count;
        size_t last_cycle = start;
        bool branch = p->instructions[end - 1].instruction.format == PORYGON_FORMAT_JUMP;
        for (i = block->first_instruction; i < end; ++i) {
            if (issued[i] < start) {
                return schedule_result(PORYGON_SCHEDULE_CONTROL, i, issued[i], SIZE_MAX,
                                       "Una instrucción cruzó el límite del bloque");
            }
            if (issued[i] > last_cycle) last_cycle = issued[i];
        }
        for (i = 0; i < p->label_count; ++i) {
            if (p->labels[i].block_index == b && s->label_bundles[i] != start) {
                return schedule_result(PORYGON_SCHEDULE_CONTROL, block->first_instruction,
                                       start, SIZE_MAX, "La etiqueta no apunta a la entrada del bloque");
            }
        }
        start = last_cycle + 1;
        if (branch) {
            size_t delay, slot;
            if (issued[end - 1] != last_cycle ||
                s->bundle_count - start < PORYGON_BRANCH_NOP_BUNDLES) {
                return schedule_result(PORYGON_SCHEDULE_CONTROL, end - 1, last_cycle, SIZE_MAX,
                                       "Faltan los dos bundles NOP del salto");
            }
            /* Se exige padding sin instrucciones de origen. Un literal cero o
               una instrucción que el hardware descarte no sustituye estos NOPs. */
            for (delay = 0; delay < PORYGON_BRANCH_NOP_BUNDLES; ++delay) {
                for (slot = 0; slot < 4; ++slot) {
                    const porygon_scheduled_slot_t *entry = &s->bundles[start + delay].slots[slot];
                    if (entry->kind != PORYGON_SLOT_NOP || entry->word != 0) {
                        return schedule_result(PORYGON_SCHEDULE_CONTROL, end - 1, start + delay,
                                               slot, "Un delay de salto contiene una operación o literal");
                    }
                }
            }
            start += PORYGON_BRANCH_NOP_BUNDLES;
        }
    }
    if (start != s->bundle_count) {
        return schedule_result(PORYGON_SCHEDULE_CONTROL, SIZE_MAX, start, SIZE_MAX,
                               "Bundles adicionales después del programa");
    }
    return schedule_result(PORYGON_SCHEDULE_OK, SIZE_MAX, SIZE_MAX, SIZE_MAX, "OK");
}

/**
 * Verifica slots, reconstruye issue/WB y luego comprueba dependencias y control.
 * Comparte codificadores y latencias con el scheduler, pero no sus decisiones
 * ni sus reservas temporales; conserva intactos programa y calendario.
 */
porygon_schedule_result_t porygon_verify_schedule(
    const porygon_machine_program_t *program, const porygon_schedule_t *schedule)
{
    porygon_schedule_result_t result = schedule_validate_program(program);
    porygon_machine_instruction_t *instructions = NULL;
    size_t *issued = NULL;
    writeback_t *writebacks = NULL;
    size_t b, slot, i;
    if (result.status != PORYGON_SCHEDULE_OK) return result;
    if (schedule == NULL || schedule->bundles == NULL || schedule->bundle_count == 0 ||
        schedule->bundle_count > PORYGON_MAX_BUNDLES || schedule->label_count != program->label_count ||
        (schedule->label_count && schedule->label_bundles == NULL)) {
        return schedule_result(PORYGON_SCHEDULE_INVALID_BUNDLE, SIZE_MAX, SIZE_MAX, SIZE_MAX,
                               "Calendario vacío, demasiado grande o incompleto");
    }
    instructions = malloc(program->instruction_count * sizeof(*instructions));
    issued = malloc(program->instruction_count * sizeof(*issued));
    writebacks = calloc(schedule->bundle_count + 4, sizeof(*writebacks));
    if (instructions == NULL || issued == NULL || writebacks == NULL) {
        result = schedule_result(PORYGON_SCHEDULE_OUT_OF_MEMORY, SIZE_MAX, SIZE_MAX, SIZE_MAX,
                                 "No se pudo reservar el verificador");
        goto done;
    }
    for (i = 0; i < program->instruction_count; ++i) {
        instructions[i] = schedule_describe(&program->instructions[i]);
        issued[i] = SIZE_MAX;
    }
    for (i = 0; i < schedule->label_count; ++i) {
        if (schedule->label_bundles[i] >= schedule->bundle_count) {
            result = schedule_result(PORYGON_SCHEDULE_CONTROL, SIZE_MAX, SIZE_MAX, SIZE_MAX,
                                     "Etiqueta fuera del calendario");
            goto done;
        }
    }
    /* issued sirve también como conjunto de instrucciones vistas: detecta tanto
       duplicaciones durante este recorrido como omisiones al terminarlo. */
    for (b = 0; b < schedule->bundle_count; ++b) {
        unsigned int units = 0;
        for (slot = 0; slot < 4; ++slot) {
            const porygon_scheduled_slot_t *entry = &schedule->bundles[b].slots[slot];
            const porygon_machine_instruction_t *mi;
            uint16_t word, literal;
            int unit;
            if (entry->kind == PORYGON_SLOT_NOP && entry->word == 0 && entry->pipe == 0) continue;
            i = entry->instruction_index;
            if (entry->kind != PORYGON_SLOT_INSTRUCTION || i >= program->instruction_count ||
                issued[i] != SIZE_MAX) {
                result = schedule_result(PORYGON_SCHEDULE_INVALID_BUNDLE, i, b, slot,
                                         "NOP inválido, literal huérfano o instrucción repetida");
                goto done;
            }
            mi = &instructions[i];
            /* Se contrasta con la instrucción original, no solo con bits que
               podrían formar otra instrucción válida pero semánticamente distinta. */
            if (schedule_encode(&mi->instruction, entry->pipe, &word, &literal) != PORYGON_ENCODE_OK ||
                word != entry->word) {
                result = schedule_result(PORYGON_SCHEDULE_INVALID_BUNDLE, i, b, slot,
                                         "Codificación, campos reservados o pipe inválidos");
                goto done;
            }
            if (mi->target_label != PORYGON_NO_LABEL) {
                int64_t offset = (int64_t)schedule->label_bundles[mi->target_label] - (int64_t)b;
                if (offset < INT16_MIN || offset > INT16_MAX) {
                    result = schedule_result(PORYGON_SCHEDULE_CONTROL, i, b, slot,
                                             "El desplazamiento del salto no cabe en 16 bits");
                    goto done;
                }
                literal = (uint16_t)offset;
            }
            if (mi->instruction.immediate) {
                const porygon_scheduled_slot_t *next;
                if (slot == 3) {
                    result = schedule_result(PORYGON_SCHEDULE_INVALID_BUNDLE, i, b, slot,
                                             "Inmediato en el último slot");
                    goto done;
                }
                next = &schedule->bundles[b].slots[slot + 1];
                if (next->kind != PORYGON_SLOT_LITERAL || next->instruction_index != i ||
                    next->word != literal || next->pipe != 0) {
                    result = schedule_result(PORYGON_SCHEDULE_INVALID_BUNDLE, i, b, slot + 1,
                                             "Literal ausente, incorrecto o no adyacente");
                    goto done;
                }
                /* El siguiente slot ya fue validado como dato: saltarlo impide
                   interpretar sus bits como una segunda operación. */
                ++slot;
            }
            unit = instruction_unit(&mi->instruction, entry->pipe);
            if (unit >= 0 && (units & (1u << unit))) {
                result = schedule_result(PORYGON_SCHEDULE_RESOURCE, i, b, slot,
                                         "Dos operaciones usan la misma unidad en un bundle");
                goto done;
            }
            if (unit >= 0) units |= 1u << unit;
            if (!record_writeback(writebacks, b, mi, entry->pipe)) {
                result = schedule_result(PORYGON_SCHEDULE_RESOURCE, i, b, slot,
                                         "Colisión de canal WB, salida ALU1 o registro destino");
                goto done;
            }
            issued[i] = b;
        }
    }
    for (i = 0; i < program->instruction_count; ++i) {
        if (issued[i] == SIZE_MAX) {
            result = schedule_result(PORYGON_SCHEDULE_INVALID_BUNDLE, i, SIZE_MAX, SIZE_MAX,
                                     "Se perdió una instrucción del programa");
            goto done;
        }
    }
    /* Solo se comparan tiempos cuando cada instrucción tiene exactamente una
       emisión válida; SIZE_MAX nunca debe entrar a la aritmética de latencias. */
    result = verify_dependencies(instructions, issued, program->instruction_count);
    if (result.status == PORYGON_SCHEDULE_OK) result = verify_control(program, schedule, issued);
done:
    free(instructions);
    free(issued);
    free(writebacks);
    return result;
}
