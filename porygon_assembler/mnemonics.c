#include "mnemonics.h"

#include "encoder.h"

#include <stddef.h>
#include <string.h>

/*
 * MATH, LOST y JUMP siguen docs/isa.md de esta rama. CRYPTO sigue la revisión
 * de la ISA entregada por el equipo, que añade LOPW y cambia los bits de funct.
 * Las variantes con sufijo "i" comparten funct con su forma normal y se
 * distinguen mediante immediate. El parser usa format para leer operandos.
 */
static const porygon_mnemonic_t MNEMONICS[] = {
    {"nop",     PORYGON_FORMAT_NOP,    0,                       false},

    {"mov",     PORYGON_FORMAT_MATH,   PORYGON_MATH_MOV,        false},
    {"sum",     PORYGON_FORMAT_MATH,   PORYGON_MATH_SUM,        false},
    {"sumi",    PORYGON_FORMAT_MATH,   PORYGON_MATH_SUM,        true},
    {"diff",    PORYGON_FORMAT_MATH,   PORYGON_MATH_DIFF,       false},
    {"diffi",   PORYGON_FORMAT_MATH,   PORYGON_MATH_DIFF,       true},
    {"and",     PORYGON_FORMAT_MATH,   PORYGON_MATH_AND,        false},
    {"andi",    PORYGON_FORMAT_MATH,   PORYGON_MATH_AND,        true},
    {"or",      PORYGON_FORMAT_MATH,   PORYGON_MATH_OR,         false},
    {"ori",     PORYGON_FORMAT_MATH,   PORYGON_MATH_OR,         true},
    {"xor",     PORYGON_FORMAT_MATH,   PORYGON_MATH_XOR,        false},
    {"xori",    PORYGON_FORMAT_MATH,   PORYGON_MATH_XOR,        true},
    {"not",     PORYGON_FORMAT_MATH,   PORYGON_MATH_NOT,        false},
    {"shl",     PORYGON_FORMAT_MATH,   PORYGON_MATH_SHL,        false},
    {"shli",    PORYGON_FORMAT_MATH,   PORYGON_MATH_SHL,        true},
    {"shr",     PORYGON_FORMAT_MATH,   PORYGON_MATH_SHR,        false},
    {"shri",    PORYGON_FORMAT_MATH,   PORYGON_MATH_SHR,        true},
    {"sar",     PORYGON_FORMAT_MATH,   PORYGON_MATH_SAR,        false},
    {"sari",    PORYGON_FORMAT_MATH,   PORYGON_MATH_SAR,        true},
    {"mul",     PORYGON_FORMAT_MATH,   PORYGON_MATH_MUL,        false},
    {"muli",    PORYGON_FORMAT_MATH,   PORYGON_MATH_MUL,        true},
    {"mulh",    PORYGON_FORMAT_MATH,   PORYGON_MATH_MULH,       false},
    {"mulhi",   PORYGON_FORMAT_MATH,   PORYGON_MATH_MULH,       true},
    {"gtn",     PORYGON_FORMAT_MATH,   PORYGON_MATH_GTN,        false},
    {"ltn",     PORYGON_FORMAT_MATH,   PORYGON_MATH_LTN,        false},
    {"eor",     PORYGON_FORMAT_MATH,   PORYGON_MATH_EOR,        false},
    {"nez",     PORYGON_FORMAT_MATH,   PORYGON_MATH_NEZ,        false},

    {"low",     PORYGON_FORMAT_LOST,   PORYGON_LOST_LOW,        false},
    {"lowi",    PORYGON_FORMAT_LOST,   PORYGON_LOST_LOW,        true},
    {"lob",     PORYGON_FORMAT_LOST,   PORYGON_LOST_LOB,        false},
    {"lobi",    PORYGON_FORMAT_LOST,   PORYGON_LOST_LOB,        true},
    {"lobu",    PORYGON_FORMAT_LOST,   PORYGON_LOST_LOBU,       false},
    {"lobui",   PORYGON_FORMAT_LOST,   PORYGON_LOST_LOBU,       true},
    {"loh",     PORYGON_FORMAT_LOST,   PORYGON_LOST_LOH,        false},
    {"lohi",    PORYGON_FORMAT_LOST,   PORYGON_LOST_LOH,        true},
    {"lohu",    PORYGON_FORMAT_LOST,   PORYGON_LOST_LOHU,       false},
    {"lohui",   PORYGON_FORMAT_LOST,   PORYGON_LOST_LOHU,       true},
    {"stw",     PORYGON_FORMAT_LOST,   PORYGON_LOST_STW,        false},
    {"stwi",    PORYGON_FORMAT_LOST,   PORYGON_LOST_STW,        true},
    {"stb",     PORYGON_FORMAT_LOST,   PORYGON_LOST_STB,        false},
    {"stbi",    PORYGON_FORMAT_LOST,   PORYGON_LOST_STB,        true},
    {"sth",     PORYGON_FORMAT_LOST,   PORYGON_LOST_STH,        false},
    {"sthi",    PORYGON_FORMAT_LOST,   PORYGON_LOST_STH,        true},

    {"j",       PORYGON_FORMAT_JUMP,   PORYGON_JUMP_J,          false},
    {"ji",      PORYGON_FORMAT_JUMP,   PORYGON_JUMP_J,          true},
    {"jz",      PORYGON_FORMAT_JUMP,   PORYGON_JUMP_JZ,         false},
    {"jzi",     PORYGON_FORMAT_JUMP,   PORYGON_JUMP_JZ,         true},
    {"jnz",     PORYGON_FORMAT_JUMP,   PORYGON_JUMP_JNZ,        false},
    {"jnzi",    PORYGON_FORMAT_JUMP,   PORYGON_JUMP_JNZ,        true},
    {"jr",      PORYGON_FORMAT_JUMP,   PORYGON_JUMP_JR,         false},

    {"auth",    PORYGON_FORMAT_CRYPTO, PORYGON_CRYPTO_AUTH,    false},
    {"write",   PORYGON_FORMAT_CRYPTO, PORYGON_CRYPTO_WRITE,   false},
    {"feistel", PORYGON_FORMAT_CRYPTO, PORYGON_CRYPTO_FEISTEL, false},
    {"pwd",     PORYGON_FORMAT_CRYPTO, PORYGON_CRYPTO_PWD,     false},
    {"rvk",     PORYGON_FORMAT_CRYPTO, PORYGON_CRYPTO_RVK,     false},
    {"lopw",    PORYGON_FORMAT_CRYPTO, PORYGON_CRYPTO_LOPW,    false}
};

const porygon_mnemonic_t *porygon_find_mnemonic(const char *name)
{
    size_t index;

    if (name == NULL) {
        return NULL;
    }

    for (index = 0; index < sizeof(MNEMONICS) / sizeof(MNEMONICS[0]); ++index) {
        if (strcmp(name, MNEMONICS[index].name) == 0) {
            return &MNEMONICS[index];
        }
    }

    return NULL;
}
