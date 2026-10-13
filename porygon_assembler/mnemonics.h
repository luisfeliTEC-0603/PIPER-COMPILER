#ifndef PORYGON_ASSEMBLER_MNEMONICS_H
#define PORYGON_ASSEMBLER_MNEMONICS_H

#include <stdbool.h>

typedef enum {
    PORYGON_FORMAT_NOP,
    PORYGON_FORMAT_MATH,
    PORYGON_FORMAT_LOST,
    PORYGON_FORMAT_JUMP,
    PORYGON_FORMAT_CRYPTO
} porygon_format_t;

typedef struct {
    const char *name;
    porygon_format_t format;
    unsigned int funct;
    bool immediate;
} porygon_mnemonic_t;

/*
 * Busca un mnemónico de la ISA escrito en minúsculas. La tabla relaciona el
 * nombre con su formato, funct y bit imm; todavía no interpreta operandos.
 * Devuelve NULL si name es NULL o si el mnemónico no existe. El puntero
 * devuelto pertenece a una tabla constante y no debe liberarse ni modificarse.
 */
const porygon_mnemonic_t *porygon_find_mnemonic(const char *name);

/** Devuelve el nombre canónico de una operación reconocida, o NULL. */
const char *porygon_mnemonic_name(porygon_format_t format, unsigned int funct,
                                 bool immediate);

#endif
