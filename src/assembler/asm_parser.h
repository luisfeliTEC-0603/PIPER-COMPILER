#ifndef PORYGON_ASSEMBLER_ASM_PARSER_H
#define PORYGON_ASSEMBLER_ASM_PARSER_H

#include "machine_program.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PORYGON_PARSE_OK,
    PORYGON_PARSE_EMPTY,
    PORYGON_PARSE_INVALID_ARGUMENT,
    PORYGON_PARSE_UNKNOWN_MNEMONIC,
    PORYGON_PARSE_EXPECTED_REGISTER,
    PORYGON_PARSE_EXPECTED_NUMBER,
    PORYGON_PARSE_EXPECTED_SEPARATOR,
    PORYGON_PARSE_OUT_OF_RANGE,
    PORYGON_PARSE_TRAILING_TEXT,
    PORYGON_PARSE_UNSUPPORTED_FORMAT
} porygon_parse_status_t;

typedef struct {
    porygon_parse_status_t status;
    size_t column; /* Columna inicial en base 1; cero para argumentos inválidos. */
} porygon_parse_result_t;

/*
 * Analiza una instrucción en una línea. Acepta espacios, registros r0..r7
 * y enteros decimales o 0x hexadecimales. Los mnemónicos aceptan mayúsculas
 * o minúsculas. No admite comentarios ni etiquetas. Los offsets numéricos de
 * JUMP tienen signo: -128..127 en la forma corta y -32768..32767 con imm.
 * En CRYPTO, los índices escritos en ASM son 0..3, sin conversión.
 * Devuelve PORYGON_PARSE_EMPTY para una línea en blanco. En cualquier error,
 * out queda intacto y column señala el lugar donde se detectó el problema.
 */
porygon_parse_result_t porygon_parse_instruction(const char *line,
                                                  porygon_instruction_t *out);

#endif
