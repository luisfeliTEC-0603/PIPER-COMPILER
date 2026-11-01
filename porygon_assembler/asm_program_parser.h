#ifndef PORYGON_ASSEMBLER_ASM_PROGRAM_PARSER_H
#define PORYGON_ASSEMBLER_ASM_PROGRAM_PARSER_H

#include "asm_parser.h"
#include "machine_program.h"

/* Estados del programa completo. INVALID_INSTRUCTION añade en
   instruction_status el motivo preciso del parser de una línea. */
typedef enum {
    PORYGON_PROGRAM_OK,
    PORYGON_PROGRAM_INVALID_ARGUMENT,
    PORYGON_PROGRAM_EMPTY,
    PORYGON_PROGRAM_INVALID_INSTRUCTION,
    PORYGON_PROGRAM_EXPECTED_LABEL,
    PORYGON_PROGRAM_DUPLICATE_LABEL,
    PORYGON_PROGRAM_UNKNOWN_LABEL,
    PORYGON_PROGRAM_LABEL_WITHOUT_INSTRUCTION,
    PORYGON_PROGRAM_INVALID_SOURCE,
    PORYGON_PROGRAM_IO_ERROR,
    PORYGON_PROGRAM_OUT_OF_MEMORY
} porygon_program_status_t;

/* line/column son base 1; valen 0 cuando el problema no corresponde a una
   ubicación del archivo, por ejemplo una ruta inexistente o falta de memoria. */
typedef struct {
    porygon_program_status_t status;
    porygon_parse_status_t instruction_status; /* Solo para INVALID_INSTRUCTION. */
    size_t line;
    size_t column;
} porygon_program_parse_result_t;

/*
 * Lee ASM lineal: una instrucción por línea, etiquetas `nombre:` opcionales
 * (también antes de una instrucción en la misma línea), líneas vacías y saltos
 * simbólicos `j`, `jz` o `jnz` hacia una etiqueta. No admite comentarios,
 * directivas, bundles explícitos ni offsets numéricos de salto: la posición
 * final de los bundles se conocerá después de calendarizar.
 *
 * Los nombres de etiquetas son sensibles a mayúsculas. No se exige ninguna
 * etiqueta llamada inicio o fin: solo se declaran las que necesite el código.
 * Los saltos simbólicos se representan con imm=1 y ocupan dos slots. El
 * llamador debe pasar un out inicializado a cero o liberado previamente;
 * si ocurre un error, out permanece intacto. Libérelo con
 * porygon_machine_program_free() cuando ya no se necesite.
 */
porygon_program_parse_result_t porygon_parse_program(
    const char *source, porygon_machine_program_t *out);

/* Lee un archivo ASM completo. path debe nombrar un archivo de texto sin NUL. */
porygon_program_parse_result_t porygon_parse_program_file(
    const char *path, porygon_machine_program_t *out);

#endif
