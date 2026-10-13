#ifndef PORYGON_ASSEMBLER_ENCODER_H
#define PORYGON_ASSEMBLER_ENCODER_H

#include <stdbool.h>
#include <stdint.h>

/* Un NOP es una instrucción; un literal inmediato con valor cero no lo es. */
#define PORYGON_NOP_SLOT UINT16_C(0x0000)

/* Todas las funciones dejan la salida intacta cuando retornan un error. */
typedef enum {
    PORYGON_ENCODE_OK,
    PORYGON_ENCODE_INVALID_ARGUMENT,
    PORYGON_ENCODE_INVALID_OPERATION,
    PORYGON_ENCODE_OUT_OF_RANGE,
    PORYGON_ENCODE_INVALID_FORM
} porygon_encode_result_t;

/*
 * Cada valor es el campo funct de la ISA. El parser convierte nombres
 * como "sum" y "sumi" al mismo PORYGON_MATH_SUM; el bit immediate los distingue.
 * Este módulo recibe la operación ya identificada, no texto ensamblador.
 */
typedef enum {
    PORYGON_MATH_MOV = 1,
    PORYGON_MATH_SUM = 2,
    PORYGON_MATH_DIFF = 3,
    PORYGON_MATH_AND = 4,
    PORYGON_MATH_OR = 5,
    PORYGON_MATH_XOR = 6,
    PORYGON_MATH_NOT = 7,
    PORYGON_MATH_SHL = 8,
    PORYGON_MATH_SHR = 9,
    PORYGON_MATH_SAR = 10,
    PORYGON_MATH_MUL = 11,
    PORYGON_MATH_GTN = 12,
    PORYGON_MATH_LTN = 13,
    PORYGON_MATH_EOR = 14,
    PORYGON_MATH_NEZ = 15,
    PORYGON_MATH_MULH = 16
} porygon_math_op_t;

typedef enum {
    PORYGON_LOST_LOW = 0,
    PORYGON_LOST_LOB = 1,
    PORYGON_LOST_LOBU = 2,
    PORYGON_LOST_LOH = 3,
    PORYGON_LOST_LOHU = 4,
    PORYGON_LOST_STW = 5,
    PORYGON_LOST_STB = 6,
    PORYGON_LOST_STH = 7
} porygon_lost_op_t;

typedef enum {
    PORYGON_JUMP_J = 0,
    PORYGON_JUMP_JZ = 1,
    PORYGON_JUMP_JNZ = 2,
    PORYGON_JUMP_JR = 3
} porygon_jump_op_t;

typedef enum {
    PORYGON_CRYPTO_AUTH = 0,
    PORYGON_CRYPTO_WRITE = 1,
    PORYGON_CRYPTO_FEISTEL = 2,
    PORYGON_CRYPTO_PWD = 3,
    PORYGON_CRYPTO_RVK = 4,
    PORYGON_CRYPTO_LOPW = 5
} porygon_crypto_op_t;

/*
 * Codifica una instrucción MATH en un slot de 16 bits.
 * rs1 y rs2 son registros entre 0 y 7. En las formas con inmediato y en las
 * operaciones unarias, rs2 debe ser cero. immediate solo activa el bit imm:
 * el literal de 16 bits se colocará después, en el siguiente slot.
 */
porygon_encode_result_t porygon_encode_math(porygon_math_op_t op, int rs1,
                                            int rs2, bool immediate,
                                            uint16_t *encoded);

/*
 * op selecciona el funct de la carga o almacenamiento LOST. Sin inmediato,
 * offset admite valores de -4 a 3 bytes. Con inmediato, offset debe ser cero y el
 * desplazamiento de 16 bits se colocará en el siguiente slot.
 */
porygon_encode_result_t porygon_encode_lost(porygon_lost_op_t op, int rd,
                                            int rs, int offset, bool immediate,
                                            uint16_t *encoded);

/*
 * op selecciona el funct de JUMP. offset_bits representa el campo [12:5]
 * (0 a 255). La BRUH interpreta esos bits con signo en unidades de bundle,
 * tomando como base el PC del bundle actual. Si immediate es verdadero,
 * ese campo debe ser cero y el literal irá en el
 * siguiente slot. jr no admite inmediato ni offset; j no utiliza rd.
 */
porygon_encode_result_t porygon_encode_jump(porygon_jump_op_t op, int rd,
                                            int offset_bits, bool immediate,
                                            uint16_t *encoded);

/*
 * op selecciona funct[9:7] de CRYPTO. item_index representa la ronda,
 * subllave o palabra de credencial en [6:5], según la operación; key_index
 * representa la llave de la bóveda en [4:3]. Ambos índices admiten 0..3.
 * Los campos sin uso deben recibirse como cero; reserved[2] siempre se
 * codifica como cero y CRYPTO no consume un slot literal.
 */
porygon_encode_result_t porygon_encode_crypto(porygon_crypto_op_t op, int rs1,
                                              int rs2, int item_index,
                                              int key_index, uint16_t *encoded);

#endif
