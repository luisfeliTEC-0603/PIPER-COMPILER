#include "internal.h"

#include <inttypes.h>

/**
 * Recupera la instrucción de origen y agrega los metadatos elegidos al calendarizar.
 * El texto conserva etiquetas; el literal del slot vecino ya contiene su offset final.
 */
static void write_instruction(FILE *file, const porygon_machine_program_t *program,
                              const porygon_scheduled_slot_t *slot)
{
    const porygon_machine_instruction_t *mi = &program->instructions[slot->instruction_index];
    const porygon_instruction_t *in = &mi->instruction;
    /* Reutilizar la tabla del parser evita mantener una segunda lista de nombres
       que podría desincronizarse al añadir una instrucción a la ISA. */
    const char *name = porygon_mnemonic_name(in->format, in->funct, in->immediate);
    switch (in->format) {
    case PORYGON_FORMAT_NOP:
        fprintf(file, "NOP");
        break;
    case PORYGON_FORMAT_MATH:
        fprintf(file, "MATH pipe=%u %s r%u", slot->pipe, name, in->operands.math.rs1);
        if (in->immediate) fprintf(file, ", #%" PRId64, in->operands.math.literal);
        else if (in->funct != PORYGON_MATH_NOT && in->funct != PORYGON_MATH_NEZ) {
            fprintf(file, ", r%u", in->operands.math.rs2);
        }
        break;
    case PORYGON_FORMAT_LOST:
        fprintf(file, "LOST %s r%u, %" PRId64 "(r%u)", name, in->operands.lost.rd,
                in->operands.lost.offset, in->operands.lost.rs);
        break;
    case PORYGON_FORMAT_JUMP:
        fprintf(file, "JUMP %s ", name);
        if (in->funct != PORYGON_JUMP_J) fprintf(file, "r%u", in->operands.jump.rd);
        if (mi->target_label != PORYGON_NO_LABEL) {
            if (in->funct != PORYGON_JUMP_J) fprintf(file, ", ");
            fprintf(file, "%s", program->labels[mi->target_label].name);
        }
        break;
    case PORYGON_FORMAT_CRYPTO:
        fprintf(file, "CRYPTO %s", name);
        if (in->funct == PORYGON_CRYPTO_WRITE || in->funct == PORYGON_CRYPTO_LOPW ||
            in->funct == PORYGON_CRYPTO_FEISTEL) {
            fprintf(file, " r%u", in->operands.crypto.rs1);
            if (in->funct == PORYGON_CRYPTO_FEISTEL) fprintf(file, ", r%u", in->operands.crypto.rs2);
            fprintf(file, ", %u", in->operands.crypto.item_index);
            if (in->funct != PORYGON_CRYPTO_LOPW) fprintf(file, ", %u", in->operands.crypto.key_index);
        }
        break;
    }
}

/**
 * Escribe el formato legible después de validar todo el calendario.
 * Mostrar el literal como línea propia deja explícito el espacio que ya consume:
 * un futuro lector no debe volver a añadirlo al leer el inmediato del mnemónico.
 */
porygon_schedule_result_t porygon_write_bundles(FILE *file,
    const porygon_machine_program_t *program, const porygon_schedule_t *schedule)
{
    size_t b, s, i;
    porygon_schedule_result_t result;
    if (file == NULL) return schedule_result(PORYGON_SCHEDULE_IO_ERROR, SIZE_MAX, SIZE_MAX,
                                             SIZE_MAX, "Archivo de salida nulo");
    /* El llamador puede haber editado los bundles después de planificar. Validar
       antes del primer fprintf evita emitir un calendario estructuralmente inválido. */
    result = porygon_verify_schedule(program, schedule);
    if (result.status != PORYGON_SCHEDULE_OK) return result;
    for (i = 0; i < program->label_count; ++i) {
        fprintf(file, "LABEL %s %zu\n", program->labels[i].name, schedule->label_bundles[i]);
    }
    for (b = 0; b < schedule->bundle_count; ++b) {
        fprintf(file, "BUNDLE %zu\n", b);
        for (s = 0; s < 4; ++s) {
            const porygon_scheduled_slot_t *slot = &schedule->bundles[b].slots[s];
            fprintf(file, "    SLOT %zu ", s);
            if (slot->kind == PORYGON_SLOT_NOP) fprintf(file, "NOP");
            else if (slot->kind == PORYGON_SLOT_LITERAL) fprintf(file, "LITERAL 0x%04" PRIX16, slot->word);
            else write_instruction(file, program, slot);
            fputc('\n', file);
        }
        fprintf(file, "END_BUNDLE\n");
    }
    return ferror(file) ? schedule_result(PORYGON_SCHEDULE_IO_ERROR, SIZE_MAX, SIZE_MAX,
                                          SIZE_MAX, "No se pudo escribir el archivo de bundles") : result;
}

/**
 * Escribe la misma salida codificada como palabras de 64 bits para readmemh.
 * No replanifica ni interpreta el archivo textual: usa directamente los slots verificados.
 */
porygon_schedule_result_t porygon_write_mem(FILE *file,
    const porygon_machine_program_t *program, const porygon_schedule_t *schedule)
{
    size_t b, s;
    porygon_schedule_result_t result;
    if (file == NULL) return schedule_result(PORYGON_SCHEDULE_IO_ERROR, SIZE_MAX, SIZE_MAX,
                                             SIZE_MAX, "Archivo de salida nulo");
    result = porygon_verify_schedule(program, schedule);
    if (result.status != PORYGON_SCHEDULE_OK) return result;
    for (b = 0; b < schedule->bundle_count; ++b) {
        uint64_t word = 0;
        /* El cast se hace antes del shift para poder desplazar hasta 48 bits.
           En el hexadecimal impreso, slot 0 queda en los cuatro dígitos derechos. */
        for (s = 0; s < 4; ++s) word |= (uint64_t)schedule->bundles[b].slots[s].word << (16 * s);
        fprintf(file, "%016" PRIX64 "\n", word);
    }
    return ferror(file) ? schedule_result(PORYGON_SCHEDULE_IO_ERROR, SIZE_MAX, SIZE_MAX,
                                          SIZE_MAX, "No se pudo escribir la imagen .mem") : result;
}
