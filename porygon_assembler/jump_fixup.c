#include "jump_fixup.h"

#include <stdbool.h>
#include <string.h>

porygon_jump_fixup_result_t porygon_resolve_long_jump(
    porygon_jump_op_t op, int rd, size_t source_bundle,
    const char *target_label, const porygon_label_t *labels,
    size_t label_count, size_t bundle_count, porygon_resolved_jump_t *out)
{
    porygon_resolved_jump_t resolved;
    porygon_encode_result_t encoded;
    bool found = false;
    size_t target_bundle = 0;
    size_t distance;
    size_t index;
    int32_t signed_offset;

    if (target_label == NULL || target_label[0] == '\0' || out == NULL ||
        (label_count != 0 && labels == NULL)) {
        return PORYGON_FIXUP_INVALID_ARGUMENT;
    }
    if (source_bundle >= bundle_count) {
        return PORYGON_FIXUP_INVALID_BUNDLE_INDEX;
    }

    /* Se codifica en una variable temporal para no modificar out si después
       falla la búsqueda de la etiqueta o el rango del desplazamiento. */
    encoded = porygon_encode_jump(op, rd, 0, true,
                                  &resolved.instruction_slot);
    if (encoded != PORYGON_ENCODE_OK) {
        return PORYGON_FIXUP_INVALID_JUMP;
    }

    for (index = 0; index < label_count; ++index) {
        if (labels[index].name == NULL || labels[index].name[0] == '\0') {
            return PORYGON_FIXUP_INVALID_ARGUMENT;
        }
        if (labels[index].bundle_index >= bundle_count) {
            return PORYGON_FIXUP_INVALID_BUNDLE_INDEX;
        }
        if (strcmp(labels[index].name, target_label) == 0) {
            if (found) {
                return PORYGON_FIXUP_DUPLICATE_LABEL;
            }
            found = true;
            target_bundle = labels[index].bundle_index;
        }
    }
    if (!found) {
        return PORYGON_FIXUP_UNKNOWN_LABEL;
    }

    /* Restar size_t en el orden correcto evita underflow. El límite se
       comprueba antes de convertir a entero con signo o a bits de literal. */
    if (target_bundle >= source_bundle) {
        distance = target_bundle - source_bundle;
        if (distance > (size_t)INT16_MAX) {
            return PORYGON_FIXUP_OUT_OF_RANGE;
        }
        signed_offset = (int32_t)distance;
    } else {
        distance = source_bundle - target_bundle;
        if (distance > (size_t)INT16_MAX + 1u) {
            return PORYGON_FIXUP_OUT_OF_RANGE;
        }
        signed_offset = -(int32_t)distance;
    }

    resolved.offset_bundles = (int16_t)signed_offset;
    /* La conversión a uint16_t conserva el complemento a dos de un negativo. */
    resolved.literal_slot = (uint16_t)resolved.offset_bundles;
    *out = resolved;
    return PORYGON_FIXUP_OK;
}
