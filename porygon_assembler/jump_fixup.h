#ifndef PORYGON_ASSEMBLER_JUMP_FIXUP_H
#define PORYGON_ASSEMBLER_JUMP_FIXUP_H

#include "encoder.h"

#include <stddef.h>
#include <stdint.h>

/* Una etiqueta nombra el comienzo de un bundle ya ubicado por el scheduler. */
typedef struct {
    const char *name;
    size_t bundle_index;
} porygon_label_t;

typedef struct {
    uint16_t instruction_slot;
    uint16_t literal_slot;
    int16_t offset_bundles;
} porygon_resolved_jump_t;

typedef enum {
    PORYGON_FIXUP_OK,
    PORYGON_FIXUP_INVALID_ARGUMENT,
    PORYGON_FIXUP_INVALID_JUMP,
    PORYGON_FIXUP_INVALID_BUNDLE_INDEX,
    PORYGON_FIXUP_UNKNOWN_LABEL,
    PORYGON_FIXUP_DUPLICATE_LABEL,
    PORYGON_FIXUP_OUT_OF_RANGE
} porygon_jump_fixup_result_t;

/*
 * Resuelve un salto simbólico largo (ji, jzi o jnzi) después de calendarizar.
 * source_bundle es el índice del bundle que contiene el salto; labels aporta
 * los índices definitivos y bundle_count permite detectar referencias fuera
 * del programa. Los nombres de etiquetas se comparan exactamente.
 *
 * El desplazamiento se mide desde el bundle actual, como hace la BRUH:
 * offset = índice_destino - índice_origen. Debe caber con signo en 16 bits.
 * La función codifica el bit imm del slot JUMP y devuelve también el patrón
 * de 16 bits del literal que debe ocupar el slot siguiente. El llamador debe
 * haber reservado ese slot; aquí no se colocan ni empaquetan bundles.
 *
 * Por ejemplo, desde el bundle 3 hacia una etiqueta en el bundle 1 se obtiene
 * offset_bundles = -2 y literal_slot = 0xFFFE. En cualquier error, out queda
 * intacto. jr no admite esta forma porque toma el destino de un registro.
 */
porygon_jump_fixup_result_t porygon_resolve_long_jump(
    porygon_jump_op_t op, int rd, size_t source_bundle,
    const char *target_label, const porygon_label_t *labels,
    size_t label_count, size_t bundle_count, porygon_resolved_jump_t *out);

#endif
