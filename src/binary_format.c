/**
 * @file binary_format.c
 * @brief Serialización y validación del ejecutable binario PORYGON.
 */

#include "binary_format.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static int write_u32_le(FILE *file, uint32_t value)
{
    unsigned char bytes[4] = {
        (unsigned char)value,
        (unsigned char)(value >> 8),
        (unsigned char)(value >> 16),
        (unsigned char)(value >> 24)
    };
    return fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
}

static int write_u64_le(FILE *file, uint64_t value)
{
    unsigned char bytes[8];
    size_t index;
    for (index = 0; index < sizeof(bytes); ++index) {
        bytes[index] = (unsigned char)(value >> (8 * index));
    }
    return fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
}

static uint32_t decode_u32_le(const unsigned char bytes[4])
{
    return (uint32_t)bytes[0] |
           (uint32_t)bytes[1] << 8 |
           (uint32_t)bytes[2] << 16 |
           (uint32_t)bytes[3] << 24;
}

static uint64_t decode_u64_le(const unsigned char bytes[8])
{
    uint64_t value = 0;
    size_t index;
    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (8 * index);
    }
    return value;
}

static uint64_t bundle_word(const porygon_bundle_t *bundle)
{
    uint64_t word = 0;
    size_t slot;
    for (slot = 0; slot < PORYGON_BUNDLE_SLOTS; ++slot) {
        word |= (uint64_t)bundle->slots[slot].word << (16 * slot);
    }
    return word;
}

porygon_binary_status_t porygon_binary_write(
    FILE *file, const porygon_machine_program_t *program,
    const porygon_schedule_t *schedule)
{
    porygon_schedule_result_t verified;
    uint32_t text_size;
    size_t index;
    if (file == NULL || program == NULL || schedule == NULL) {
        return PORYGON_BINARY_INVALID_ARGUMENT;
    }
    verified = porygon_verify_schedule(program, schedule);
    if (verified.status != PORYGON_SCHEDULE_OK) {
        return PORYGON_BINARY_INVALID_SCHEDULE;
    }
    if (schedule->bundle_count > PORYGON_BINARY_MAX_TEXT_SIZE / 8) {
        return PORYGON_BINARY_INVALID_SCHEDULE;
    }
    text_size = (uint32_t)(schedule->bundle_count * 8);
    if (!write_u32_le(file, PORYGON_BINARY_MAGIC) ||
        !write_u32_le(file, PORYGON_BINARY_VERSION) ||
        !write_u32_le(file, PORYGON_BINARY_HEADER_SIZE) ||
        !write_u32_le(file, PORYGON_BINARY_ENTRY_PC) ||
        !write_u32_le(file, text_size) ||
        !write_u32_le(file, PORYGON_BINARY_DATA_BASE) ||
        !write_u32_le(file, 0) || !write_u32_le(file, 0)) {
        return PORYGON_BINARY_IO_ERROR;
    }
    for (index = 0; index < schedule->bundle_count; ++index) {
        if (!write_u64_le(file, bundle_word(&schedule->bundles[index]))) {
            return PORYGON_BINARY_IO_ERROR;
        }
    }
    return ferror(file) ? PORYGON_BINARY_IO_ERROR : PORYGON_BINARY_OK;
}

porygon_binary_status_t porygon_binary_read(
    FILE *file, porygon_binary_image_t *image)
{
    unsigned char header[PORYGON_BINARY_HEADER_SIZE];
    porygon_binary_image_t parsed = {0};
    uint32_t magic, version, header_size, text_size;
    unsigned char bytes[8];
    size_t index;
    int trailing;
    if (file == NULL || image == NULL || image->bundles != NULL ||
        image->bundle_count != 0) {
        return PORYGON_BINARY_INVALID_ARGUMENT;
    }
    if (fread(header, 1, sizeof(header), file) != sizeof(header)) {
        return ferror(file) ? PORYGON_BINARY_IO_ERROR : PORYGON_BINARY_TRUNCATED;
    }
    magic = decode_u32_le(header);
    version = decode_u32_le(header + 4);
    header_size = decode_u32_le(header + 8);
    parsed.entry_pc = decode_u32_le(header + 12);
    text_size = decode_u32_le(header + 16);
    parsed.data_base = decode_u32_le(header + 20);
    parsed.data_init_size = decode_u32_le(header + 24);
    parsed.bss_size = decode_u32_le(header + 28);
    if (magic != PORYGON_BINARY_MAGIC || version != PORYGON_BINARY_VERSION ||
        header_size != PORYGON_BINARY_HEADER_SIZE || parsed.entry_pc % 8 != 0 ||
        text_size == 0 || text_size % 8 != 0 ||
        text_size > PORYGON_BINARY_MAX_TEXT_SIZE ||
        parsed.entry_pc >= text_size || parsed.data_base != PORYGON_BINARY_DATA_BASE ||
        parsed.data_init_size != 0 || parsed.bss_size != 0) {
        return PORYGON_BINARY_INVALID_HEADER;
    }
    parsed.bundle_count = text_size / 8;
    parsed.bundles = malloc(parsed.bundle_count * sizeof(*parsed.bundles));
    if (parsed.bundles == NULL) return PORYGON_BINARY_OUT_OF_MEMORY;
    for (index = 0; index < parsed.bundle_count; ++index) {
        if (fread(bytes, 1, sizeof(bytes), file) != sizeof(bytes)) {
            porygon_binary_image_free(&parsed);
            return ferror(file) ? PORYGON_BINARY_IO_ERROR : PORYGON_BINARY_TRUNCATED;
        }
        parsed.bundles[index] = decode_u64_le(bytes);
    }
    trailing = fgetc(file);
    if (trailing != EOF) {
        porygon_binary_image_free(&parsed);
        return PORYGON_BINARY_TRAILING_DATA;
    }
    if (ferror(file)) {
        porygon_binary_image_free(&parsed);
        return PORYGON_BINARY_IO_ERROR;
    }
    *image = parsed;
    return PORYGON_BINARY_OK;
}

porygon_binary_status_t porygon_binary_write_mem(
    FILE *file, const porygon_binary_image_t *image)
{
    size_t index;
    if (file == NULL || image == NULL || image->bundles == NULL ||
        image->bundle_count == 0) {
        return PORYGON_BINARY_INVALID_ARGUMENT;
    }
    for (index = 0; index < image->bundle_count; ++index) {
        if (fprintf(file, "%016" PRIX64 "\n", image->bundles[index]) < 0) {
            return PORYGON_BINARY_IO_ERROR;
        }
    }
    return ferror(file) ? PORYGON_BINARY_IO_ERROR : PORYGON_BINARY_OK;
}

void porygon_binary_image_free(porygon_binary_image_t *image)
{
    if (image == NULL) return;
    free(image->bundles);
    memset(image, 0, sizeof(*image));
}

const char *porygon_binary_status_message(porygon_binary_status_t status)
{
    switch (status) {
    case PORYGON_BINARY_OK: return "ok";
    case PORYGON_BINARY_INVALID_ARGUMENT: return "argumento inválido";
    case PORYGON_BINARY_INVALID_SCHEDULE: return "calendario inválido";
    case PORYGON_BINARY_INVALID_HEADER: return "encabezado PORYGON inválido";
    case PORYGON_BINARY_TRUNCATED: return "archivo PORYGON truncado";
    case PORYGON_BINARY_TRAILING_DATA: return "datos inesperados tras la sección de texto";
    case PORYGON_BINARY_OUT_OF_MEMORY: return "memoria insuficiente";
    case PORYGON_BINARY_IO_ERROR: return "error de E/S";
    }
    return "error binario desconocido";
}
