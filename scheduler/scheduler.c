#include "internal.h"
#include "porygon_assembler/jump_fixup.h"

#include <stdlib.h>

/**
 * Estado de construcción, separado del calendario que recibirá el llamador.
 * issued usa índices de instrucción; alu1_wb usa ciclos futuros de escritura.
 * Las instrucciones son copias con metadatos recalculados, no texto ASM.
 */
typedef struct {
    porygon_machine_instruction_t *instructions;
    size_t *issued;
    bool *alu1_wb;
    porygon_schedule_t output;
    size_t capacity;
} scheduler_t;

/**
 * Añade un bundle inicialmente lleno de NOPs y crece el arreglo cuando hace falta.
 * El llamador comprueba el límite de bundles antes de entrar aquí.
 */
static bool append_bundle(scheduler_t *s)
{
    if (s->output.bundle_count == s->capacity) {
        size_t capacity = s->capacity ? s->capacity * 2 : 32;
        porygon_bundle_t *bundles;
        if (capacity > PORYGON_MAX_BUNDLES) capacity = PORYGON_MAX_BUNDLES;
        /* El temporal conserva el arreglo anterior si realloc falla. Duplicar la
           capacidad evita copiar todo el calendario por cada bundle añadido. */
        bundles = realloc(s->output.bundles, capacity * sizeof(*bundles));
        if (bundles == NULL) return false;
        s->output.bundles = bundles;
        s->capacity = capacity;
    }
    /* El cero representa SLOT_NOP y también su codificación canónica. Solo se
       reemplazan los slots ocupados; los huecos ya son una salida válida. */
    s->output.bundles[s->output.bundle_count++] = (porygon_bundle_t){0};
    return true;
}

/**
 * Decide si index puede emitirse ahora respetando el orden del programa original.
 * Las dependencias se calculan con máscaras, sin almacenar un grafo de aristas.
 * También se consultan instrucciones de bloques anteriores para el fallthrough.
 */
static bool dependencies_ready(const scheduler_t *s, size_t index, size_t cycle)
{
    const porygon_machine_instruction_t *current = &s->instructions[index];
    size_t previous;
    size_t wb = cycle + schedule_wb_latency(&current->instruction);
    for (previous = 0; previous < index; ++previous) {
        const porygon_machine_instruction_t *old = &s->instructions[previous];
        /* La intersección es distinta de cero si ambas instrucciones comparten
           algún registro con los roles que forman esa dependencia. */
        bool raw = (old->defs & current->uses) != 0;
        bool war = (old->uses & current->defs) != 0;
        bool waw = (old->defs & current->defs) != 0;
        bool memory = old->memory_effect != PORYGON_MEMORY_NONE &&
                      current->memory_effect != PORYGON_MEMORY_NONE;
        bool state = (old->state_defs & current->state_uses) != 0;
        size_t issued = s->issued[previous];
        if (!(raw || war || waw || memory || state || current->ends_block)) continue;
        /* Una dependencia sin emitir sigue bloqueando, aunque quede una ALU libre. */
        if (issued == SIZE_MAX) return false;
        /* RAW exige un ciclo posterior al WB. WAW compara los WB, no los issue:
           una operación rápida más joven podría terminar antes que un MUL. */
        if (raw && cycle <= issued + schedule_wb_latency(&old->instruction)) return false;
        if (waw && wb <= issued + schedule_wb_latency(&old->instruction)) return false;
        /* WAR permite igualdad: las lecturas del bundle capturan el valor viejo. */
        if (war && cycle < issued) return false;
        if ((memory || state) && cycle <= issued) return false;
        /* El sucesor empieza en cycle + 3: salto, dos NOPs y luego destino.
           Exigir WB <= cycle + 2 lo deja listo incluso en backedges y JR,
           sin necesitar un análisis global de valores vivos por cada destino. */
        if (current->ends_block && old->defs &&
            cycle + PORYGON_BRANCH_NOP_BUNDLES < issued + schedule_wb_latency(&old->instruction)) {
            return false;
        }
    }
    return true;
}

/**
 * Consulta una unidad disponible; schedule_block hará la reserva al emitir.
 * Devuelve 0..4 para unidades, 5 para NOP sin unidad y -1 si debe esperar.
 */
static int choose_unit(const scheduler_t *s, const porygon_machine_instruction_t *mi,
                       size_t cycle, const bool used[5])
{
    /* Son dos restricciones distintas: poder emitir hoy y poder escribir después.
       MUL en i y una rápida en i+2 competirían por el mismo WB en i+4. */
    bool alu1_available = !used[1] &&
        !s->alu1_wb[cycle + schedule_wb_latency(&mi->instruction)];
    switch (mi->resource) {
    case PORYGON_RESOURCE_NONE: return 5;
    case PORYGON_RESOURCE_MATH:
        /* Preferir ALU0 deja ALU1 disponible para instrucciones que la exigen. */
        if (!used[0]) return 0;
        return alu1_available ? 1 : -1;
    case PORYGON_RESOURCE_MATH_SPECIAL: return alu1_available ? 1 : -1;
    case PORYGON_RESOURCE_LOST: return used[2] ? -1 : 2;
    case PORYGON_RESOURCE_JUMP: return used[3] ? -1 : 3;
    case PORYGON_RESOURCE_CRYPTO: return used[4] ? -1 : 4;
    }
    return -1;
}

/**
 * Construye el calendario de un bloque, un bundle por vuelta del while.
 * El recorrido estable da prioridad al orden original, pero salta candidatos
 * bloqueados para aprovechar trabajo independiente que aparezca después.
 */
static porygon_schedule_result_t schedule_block(scheduler_t *s,
    const porygon_machine_block_t *block)
{
    size_t remaining = block->instruction_count;
    size_t end = block->first_instruction + block->instruction_count;
    while (remaining) {
        size_t cycle = s->output.bundle_count;
        size_t index, position = 0;
        bool used[5] = {false};
        bool branch = false;
        if (cycle == PORYGON_MAX_BUNDLES) {
            return schedule_result(PORYGON_SCHEDULE_TOO_LARGE, block->first_instruction,
                                   cycle, SIZE_MAX, "El calendario excede 8192 bundles");
        }
        if (!append_bundle(s)) {
            return schedule_result(PORYGON_SCHEDULE_OUT_OF_MEMORY, SIZE_MAX, cycle, SIZE_MAX,
                                   "No se pudo reservar un bundle");
        }
        /* Una vuelta que no elige ninguna instrucción deja un bundle NOP completo:
           el tiempo avanza aunque remaining todavía no haya disminuido. */
        for (index = block->first_instruction; index < end && position < 4; ++index) {
            const porygon_machine_instruction_t *mi = &s->instructions[index];
            porygon_scheduled_slot_t *slot;
            uint16_t word, literal;
            unsigned int pipe;
            int unit;
            /* El tamaño se revisa junto: instrucción e inmediato deben caber
               contiguos. Un candidato grande bloqueado no descarta uno menor. */
            if (s->issued[index] != SIZE_MAX || position + mi->slot_count > 4 ||
                !dependencies_ready(s, index, cycle)) continue;
            unit = choose_unit(s, mi, cycle, used);
            if (unit < 0) continue;
            pipe = unit == 1 ? 1u : 0u;
            if (schedule_encode(&mi->instruction, pipe, &word, &literal) != PORYGON_ENCODE_OK) {
                return schedule_result(PORYGON_SCHEDULE_INVALID_INSTRUCTION, index, cycle,
                                       position, "No se puede codificar la operación");
            }
            /* Guardar el índice de origen permite verificar preservación y volver
               a imprimir operandos sin copiar toda la instrucción en cada slot. */
            slot = &s->output.bundles[cycle].slots[position++];
            *slot = (porygon_scheduled_slot_t){PORYGON_SLOT_INSTRUCTION, index, pipe, word};
            if (mi->slot_count == 2) {
                slot = &s->output.bundles[cycle].slots[position++];
                *slot = (porygon_scheduled_slot_t){PORYGON_SLOT_LITERAL, index, 0, literal};
            }
            /* used dura un bundle; alu1_wb y issued sobreviven a todos los bloques. */
            if (unit < 5) used[unit] = true;
            if (unit == 1) s->alu1_wb[cycle + schedule_wb_latency(&mi->instruction)] = true;
            s->issued[index] = cycle;
            branch = branch || mi->ends_block;
            --remaining;
        }
        /* Estos bundles son una exigencia del control, no espacios que el próximo
           bloque pueda rellenar con trabajo útil. */
        if (branch) {
            size_t delay;
            for (delay = 0; delay < PORYGON_BRANCH_NOP_BUNDLES; ++delay) {
                if (s->output.bundle_count == PORYGON_MAX_BUNDLES) {
                    return schedule_result(PORYGON_SCHEDULE_TOO_LARGE, end - 1, cycle, SIZE_MAX,
                                           "No caben los NOPs obligatorios del salto");
                }
                if (!append_bundle(s)) {
                    return schedule_result(PORYGON_SCHEDULE_OUT_OF_MEMORY, end - 1, cycle,
                                           SIZE_MAX, "No se pudo reservar el delay del salto");
                }
            }
        }
    }
    return schedule_result(PORYGON_SCHEDULE_OK, SIZE_MAX, SIZE_MAX, SIZE_MAX, "OK");
}

/**
 * Reemplaza los literales provisionales de saltos por offsets entre bundles.
 * Hacerlo al final evita recalcular destinos cada vez que se inserta una espera.
 */
static porygon_schedule_result_t resolve_jumps(const porygon_machine_program_t *p,
                                              porygon_schedule_t *output)
{
    size_t b, s;
    for (b = 0; b < output->bundle_count; ++b) {
        for (s = 0; s < 4; ++s) {
            porygon_scheduled_slot_t *slot = &output->bundles[b].slots[s];
            const porygon_machine_instruction_t *mi;
            porygon_resolved_jump_t jump;
            porygon_label_t label;
            if (slot->kind != PORYGON_SLOT_INSTRUCTION) continue;
            mi = &p->instructions[slot->instruction_index];
            if (mi->target_label == PORYGON_NO_LABEL) continue;
            label.name = p->labels[mi->target_label].name;
            label.bundle_index = output->label_bundles[mi->target_label];
            if (porygon_resolve_long_jump((porygon_jump_op_t)mi->instruction.funct,
                mi->instruction.operands.jump.rd, b, label.name, &label, 1,
                output->bundle_count, &jump) != PORYGON_FIXUP_OK) {
                return schedule_result(PORYGON_SCHEDULE_CONTROL, slot->instruction_index, b, s,
                                       "No se pudo resolver el salto");
            }
            slot->word = jump.instruction_slot;
            output->bundles[b].slots[s + 1].word = jump.literal_slot;
        }
    }
    return schedule_result(PORYGON_SCHEDULE_OK, SIZE_MAX, SIZE_MAX, SIZE_MAX, "OK");
}

/**
 * Orquesta validación, planificación por bloques, fixups y verificación final.
 * out debe estar vacío. En caso de error se libera lo construido y no se publica
 * un calendario parcial; en caso de éxito su propiedad pasa al llamador.
 */
porygon_schedule_result_t porygon_schedule_program(
    const porygon_machine_program_t *program, porygon_schedule_t *out)
{
    scheduler_t s = {0};
    porygon_schedule_result_t result = schedule_validate_program(program);
    size_t i, b;
    if (out == NULL) {
        return schedule_result(PORYGON_SCHEDULE_INVALID_PROGRAM, SIZE_MAX, SIZE_MAX, SIZE_MAX,
                               "Salida nula");
    }
    if (result.status != PORYGON_SCHEDULE_OK) return result;
    s.instructions = malloc(program->instruction_count * sizeof(*s.instructions));
    s.issued = malloc(program->instruction_count * sizeof(*s.issued));
    /* El último MUL puede escribir cuatro ciclos después del último bundle. */
    s.alu1_wb = calloc(PORYGON_MAX_BUNDLES + 4, sizeof(*s.alu1_wb));
    s.output.label_count = program->label_count;
    if (program->label_count) {
        if (program->label_count > SIZE_MAX / sizeof(*s.output.label_bundles)) {
            result = schedule_result(PORYGON_SCHEDULE_OUT_OF_MEMORY, SIZE_MAX, SIZE_MAX,
                                     SIZE_MAX, "Demasiadas etiquetas");
            goto done;
        }
        s.output.label_bundles = malloc(program->label_count * sizeof(*s.output.label_bundles));
    }
    if (s.instructions == NULL || s.issued == NULL || s.alu1_wb == NULL ||
        (program->label_count && s.output.label_bundles == NULL)) {
        result = schedule_result(PORYGON_SCHEDULE_OUT_OF_MEMORY, SIZE_MAX, SIZE_MAX, SIZE_MAX,
                                 "No se pudo reservar el calendario");
        goto done;
    }
    for (i = 0; i < program->instruction_count; ++i) {
        s.instructions[i] = schedule_describe(&program->instructions[i]);
        /* El ciclo cero es válido; SIZE_MAX distingue lo que sigue pendiente. */
        s.issued[i] = SIZE_MAX;
    }
    /* No se reinician los tiempos al cambiar de bloque. Su etiqueta apunta a
       la entrada, incluso si el bloque comienza esperando un resultado previo. */
    for (b = 0; b < program->block_count; ++b) {
        for (i = 0; i < program->label_count; ++i) {
            if (program->labels[i].block_index == b) {
                s.output.label_bundles[i] = s.output.bundle_count;
            }
        }
        result = schedule_block(&s, &program->blocks[b]);
        if (result.status != PORYGON_SCHEDULE_OK) goto done;
    }
    result = resolve_jumps(program, &s.output);
    if (result.status == PORYGON_SCHEDULE_OK) result = porygon_verify_schedule(program, &s.output);
    if (result.status == PORYGON_SCHEDULE_OK) {
        /* Es una transferencia de propiedad, no una copia de los arreglos. */
        *out = s.output;
        s.output = (porygon_schedule_t){0};
    }
done:
    /* Una única salida de limpieza cubre errores de cualquier etapa. Tras el
       éxito, s.output vacío evita liberar los arreglos entregados al llamador. */
    free(s.instructions);
    free(s.issued);
    free(s.alu1_wb);
    porygon_schedule_free(&s.output);
    return result;
}

/** Permite liberar también un calendario vacío. */
void porygon_schedule_free(porygon_schedule_t *schedule)
{
    if (schedule == NULL) return;
    free(schedule->bundles);
    free(schedule->label_bundles);
    *schedule = (porygon_schedule_t){0};
}
