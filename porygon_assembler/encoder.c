#include "encoder.h"

#include <stddef.h>

/* Solo se codifican campos de un slot; otro módulo leerá los mnemónicos ASM. */

static bool valid_register(int reg)
{
    return reg >= 0 && reg <= 7;
}

static bool valid_two_bit_field(int value)
{
    return value >= 0 && value <= 3;
}

porygon_encode_result_t porygon_encode_math(porygon_math_op_t op, int rs1,
                                            int rs2, bool immediate,
                                            uint16_t *encoded)
{
    bool allows_immediate = false;
    bool uses_rs2 = true;
    unsigned int pipe = 0;

    if (encoded == NULL) {
        return PORYGON_ENCODE_INVALID_ARGUMENT;
    }

    /*
     * El enum ya contiene funct. Esta clasificación fija el bit pipe y
     * determina si existe la variante con sufijo "i" para la operación.
     */
    switch (op) {
    case PORYGON_MATH_SUM:
    case PORYGON_MATH_DIFF:
    case PORYGON_MATH_AND:
    case PORYGON_MATH_OR:
    case PORYGON_MATH_XOR:
        allows_immediate = true;
        break;
    case PORYGON_MATH_SHL:
    case PORYGON_MATH_SHR:
    case PORYGON_MATH_SAR:
    case PORYGON_MATH_MUL:
    case PORYGON_MATH_MULH:
        allows_immediate = true;
        pipe = 1;
        break;
    case PORYGON_MATH_NOT:
    case PORYGON_MATH_NEZ:
        uses_rs2 = false;
        break;
    case PORYGON_MATH_MOV:
    case PORYGON_MATH_GTN:
    case PORYGON_MATH_LTN:
    case PORYGON_MATH_EOR:
        break;
    default:
        return PORYGON_ENCODE_INVALID_OPERATION;
    }

    if (!valid_register(rs1) || !valid_register(rs2)) {
        return PORYGON_ENCODE_OUT_OF_RANGE;
    }
    /* El campo rs2 no contiene un registro cuando se usa un literal o unario. */
    if ((immediate && !allows_immediate) ||
        ((immediate || !uses_rs2) && rs2 != 0)) {
        return PORYGON_ENCODE_INVALID_FORM;
    }

    /*
     * MATH: rs1[15:13], rs2[12:10], funct[9:4], pipe[3], imm[2].
     * El opcode MATH es 00, por eso no añade bits en [1:0].
     */
    *encoded = (uint16_t)(((unsigned int)rs1 << 13) |
                          ((unsigned int)rs2 << 10) |
                          ((unsigned int)op << 4) |
                          (pipe << 3) |
                          ((unsigned int)immediate << 2));
    return PORYGON_ENCODE_OK;
}

/** Conserva la validación de MATH y permite usar ALU1 para operaciones básicas. */
porygon_encode_result_t porygon_encode_math_pipe(porygon_math_op_t op, int rs1,
                                               int rs2, bool immediate,
                                               unsigned int pipe,
                                               uint16_t *encoded)
{
    uint16_t word;
    porygon_encode_result_t status;

    if (encoded == NULL) {
        return PORYGON_ENCODE_INVALID_ARGUMENT;
    }
    status = porygon_encode_math(op, rs1, rs2, immediate, &word);
    if (status != PORYGON_ENCODE_OK) {
        return status;
    }
    if (pipe > 1 || ((word & 8u) != 0 && pipe == 0)) {
        return PORYGON_ENCODE_INVALID_FORM;
    }
    *encoded = (uint16_t)((word & ~8u) | (pipe << 3));
    return PORYGON_ENCODE_OK;
}

porygon_encode_result_t porygon_encode_lost(porygon_lost_op_t op, int rd,
                                            int rs, int offset, bool immediate,
                                            uint16_t *encoded)
{
    if (encoded == NULL) {
        return PORYGON_ENCODE_INVALID_ARGUMENT;
    }
    /* Los valores del enum LOST se escriben directamente en funct[9:6]. */
    if (op < PORYGON_LOST_LOW || op > PORYGON_LOST_STH) {
        return PORYGON_ENCODE_INVALID_OPERATION;
    }
    if (!valid_register(rd) || !valid_register(rs)) {
        return PORYGON_ENCODE_OUT_OF_RANGE;
    }
    if (immediate) {
        if (offset != 0) {
            return PORYGON_ENCODE_INVALID_FORM;
        }
    } else if (offset < -4 || offset > 3) {
        return PORYGON_ENCODE_OUT_OF_RANGE;
    }

    /*
     * La máscara conserva los tres bits del offset en complemento a dos.
     * El 1 final es opcode LOST en [1:0]; imm ocupa el bit 2.
     */
    *encoded = (uint16_t)(((unsigned int)rd << 13) |
                          ((unsigned int)rs << 10) |
                          ((unsigned int)op << 6) |
                          (((unsigned int)offset & 7u) << 3) |
                          ((unsigned int)immediate << 2) | 1u);
    return PORYGON_ENCODE_OK;
}

porygon_encode_result_t porygon_encode_jump(porygon_jump_op_t op, int rd,
                                            int offset_bits, bool immediate,
                                            uint16_t *encoded)
{
    if (encoded == NULL) {
        return PORYGON_ENCODE_INVALID_ARGUMENT;
    }
    /* El enum JUMP ocupa funct[4:3]; el 2 final será opcode JUMP. */
    if (op < PORYGON_JUMP_J || op > PORYGON_JUMP_JR) {
        return PORYGON_ENCODE_INVALID_OPERATION;
    }
    if (!valid_register(rd) || offset_bits < 0 || offset_bits > 255) {
        return PORYGON_ENCODE_OUT_OF_RANGE;
    }
    if ((op == PORYGON_JUMP_J && rd != 0) ||
        (op == PORYGON_JUMP_JR && (immediate || offset_bits != 0)) ||
        (immediate && offset_bits != 0)) {
        return PORYGON_ENCODE_INVALID_FORM;
    }

    /* Se empaqueta el patrón crudo; la BRUH lo extiende con signo y lo escala
       a bytes. jump_fixup resuelve etiquetas para la forma con inmediato. */
    *encoded = (uint16_t)(((unsigned int)rd << 13) |
                          ((unsigned int)offset_bits << 5) |
                          ((unsigned int)op << 3) |
                          ((unsigned int)immediate << 2) | 2u);
    return PORYGON_ENCODE_OK;
}

porygon_encode_result_t porygon_encode_crypto(porygon_crypto_op_t op, int rs1,
                                              int rs2, int item_index,
                                              int key_index, uint16_t *encoded)
{
    if (encoded == NULL) {
        return PORYGON_ENCODE_INVALID_ARGUMENT;
    }
    /* El enum coincide con funct[9:7] de la nueva tabla CRYPTO. */
    if (op < PORYGON_CRYPTO_AUTH || op > PORYGON_CRYPTO_LOPW) {
        return PORYGON_ENCODE_INVALID_OPERATION;
    }
    if (!valid_register(rs1) || !valid_register(rs2) ||
        !valid_two_bit_field(item_index) ||
        !valid_two_bit_field(key_index)) {
        return PORYGON_ENCODE_OUT_OF_RANGE;
    }
    /* AUTH, PWD y RVK no reciben operandos. WRITE solo usa rs1, índice de
       subllave y llave; LOPW usa rs1 e índice de palabra. FEISTEL usa todos. */
    if (((op == PORYGON_CRYPTO_AUTH || op == PORYGON_CRYPTO_PWD ||
          op == PORYGON_CRYPTO_RVK) &&
         (rs1 != 0 || rs2 != 0 || item_index != 0 || key_index != 0)) ||
        (op == PORYGON_CRYPTO_FEISTEL && rs1 == rs2 && rs1 != 0) ||
        (op == PORYGON_CRYPTO_WRITE && rs2 != 0) ||
        (op == PORYGON_CRYPTO_LOPW && (rs2 != 0 || key_index != 0))) {
        return PORYGON_ENCODE_INVALID_FORM;
    }

    /* [15:13] rs1, [12:10] rs2, [9:7] funct, [6:5] índice compartido,
       [4:3] llave, [2] reservado=0, [1:0] opcode=11. */
    *encoded = (uint16_t)(((unsigned int)rs1 << 13) |
                          ((unsigned int)rs2 << 10) |
                          ((unsigned int)op << 7) |
                          ((unsigned int)item_index << 5) |
                          ((unsigned int)key_index << 3) | 3u);
    return PORYGON_ENCODE_OK;
}
