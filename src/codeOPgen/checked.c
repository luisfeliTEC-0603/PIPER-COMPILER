/** @file checked.c
 * Operaciones comprobadas usando EXCLUSIVAMENTE r5/r6 como auxiliares.
 * Los registros r1..r4 distintos del destino se conservan. Un fallo salta al error aritmetico
 * antes de que una operacion virtual posterior publique el valor en memoria.
 */
#include "internal.h"

void op_check_width(CodeOPgenContext *context, unsigned register_number, bool source_unsigned, PiperType type)
{
    unsigned bits = op_bits(type);
    if (op_unsigned(type)) {
        if (!source_unsigned) {
            op_move(context, 6, register_number);
            op_emit(context, "sari r6, #31");
            op_emit(context, "jnz r6, " OP_ERROR("aritmetico"));
        }
        if (bits == 32) return;
        op_move(context, 6, register_number);
        op_emit(context, "shri r6, #%u", bits);
    } else if (source_unsigned) {
        op_move(context, 6, register_number);
        op_emit(context, "shri r6, #%u", bits - 1);
    } else {
        if (bits == 32) return;
        /* La extension firmada debe reconstruir exactamente el valor. */
        op_move(context, 6, register_number);
        op_emit(context, "shli r6, #%u", 32 - bits);
        op_emit(context, "sari r6, #%u", 32 - bits);
        op_emit(context, "xor r6, r%u", register_number);
    }
    op_emit(context, "jnz r6, " OP_ERROR("aritmetico"));
}

void op_checked_multiply(CodeOPgenContext *context, unsigned destination_register, unsigned right_register, PiperType type)
{
    /* mulh es firmado. Para multiplicacion unsigned corregir su mitad alta
     * antes de reemplazar destination_register por la mitad baja. right_register queda intacto. */
    op_move(context, 5, destination_register);
    op_emit(context, "mulh r5, r%u", right_register);
    if (op_unsigned(type)) {
        op_move(context, 6, destination_register);
        op_emit(context, "sari r6, #31");
        op_emit(context, "and r6, r%u", right_register);
        op_emit(context, "sum r5, r6");
        op_move(context, 6, right_register);
        op_emit(context, "sari r6, #31");
        op_emit(context, "and r6, r%u", destination_register);
        op_emit(context, "sum r5, r6");
    }
    op_emit(context, "mul r%u, r%u", destination_register, right_register);
    if (!op_unsigned(type)) {
        op_move(context, 6, destination_register);
        op_emit(context, "sari r6, #31");
        op_emit(context, "xor r5, r6");
    }
    op_emit(context, "jnz r5, " OP_ERROR("aritmetico"));
    op_check_width(context, destination_register, op_unsigned(type), type);
}

static void negate(CodeOPgenContext *context, unsigned destination_register, unsigned source_register,
                     const RegallocMachineInstruction *operation)
{
    size_t label = context->next_label_id++;
    if (!op_unsigned(operation->source_type.piper)) {
        op_move(context, 6, source_register);
        op_emit(context, "sari r6, #31");
        op_emit(context, "jnz r6, piper_negacion_resultado_positivo_%zu", label);
    }
    /* Una entrada >=0 produce un resultado <=0. El limite negativo firmado
     * admite magnitud 2^(bits-1); en un destino unsigned solo cabe -0. */
    if (op_unsigned(operation->type.piper)) {
        op_emit(context, "jnz r%u, " OP_ERROR("aritmetico"), source_register);
    } else {
        op_move(context, 5, source_register);
        op_immediate(context, 6, INT64_C(1) << (op_bits(operation->type.piper) - 1));
        op_emit(context, "gtn r5, r6");
        op_emit(context, "jnz r5, " OP_ERROR("aritmetico"));
    }
    op_move(context, 5, 0);
    op_emit(context, "diff r5, r%u", source_register);
    op_emit(context, "j piper_negacion_fin_%zu", label);
    op_label(context, "piper_negacion_resultado_positivo_%zu", label);
    op_move(context, 5, 0);
    op_emit(context, "diff r5, r%u", source_register);
    op_check_width(context, 5, true, operation->type.piper);
    op_label(context, "piper_negacion_fin_%zu", label);
    op_move(context, destination_register, 5);
}

void op_checked(CodeOPgenContext *context, const RegallocMachineInstruction *operation)
{
    unsigned destination_register = op_register(context, operation->def), right_register = op_register(context, operation->use1);
    if (operation->op == REGALLOC_M_CHECKED_COPY) {
        op_check_width(context, right_register, op_unsigned(operation->source_type.piper), operation->type.piper);
        op_move(context, destination_register, right_register);
        return;
    }
    if (operation->op == REGALLOC_M_CHECKED_NEG) { negate(context, destination_register, right_register, operation); return; }
    if (operation->op == REGALLOC_M_CHECKED_MUL) {
        op_checked_multiply(context, destination_register, right_register, operation->type.piper);
        return;
    }
    bool add = operation->op == REGALLOC_M_CHECKED_ADD;
    op_move(context, 5, destination_register);
    op_move(context, 6, right_register);
    if (op_unsigned(operation->type.piper) && !add) {
        op_emit(context, "ltn r5, r6");
        op_emit(context, "jnz r5, " OP_ERROR("aritmetico"));
    } else if (!op_unsigned(operation->type.piper) && !add) {
        op_emit(context, "xor r6, r5"); /* block_index^a antes de perder a. */
    }
    op_emit(context, "%s r%u, r%u", add ? "sum" : "diff", destination_register, right_register);
    if (op_unsigned(operation->type.piper)) {
        if (add) {
            op_move(context, 6, destination_register);
            op_emit(context, "ltn r6, r5"); /* acarreo si resultado < a */
            op_emit(context, "jnz r6, " OP_ERROR("aritmetico"));
        }
    } else {
        op_emit(context, "xor r5, r%u", destination_register);
        if (add) op_emit(context, "xor r6, r%u", destination_register);
        op_emit(context, "and r5, r6");
        op_emit(context, "shri r5, #31");
        op_emit(context, "jnz r5, " OP_ERROR("aritmetico"));
    }
    op_check_width(context, destination_register, op_unsigned(operation->type.piper), operation->type.piper);
}

void op_compare(CodeOPgenContext *context, const RegallocMachineInstruction *operation)
{
    unsigned left_register = op_register(context, operation->def), right_register = op_register(context, operation->use1);
    const char *name = operation->op == REGALLOC_M_LT ? "ltn" : "gtn";
    if (left_register == right_register) { op_immediate(context, left_register, 0); return; }
    if (operation->left_unsigned && operation->right_unsigned) {
        op_emit(context, "%s r%u, r%u", name, left_register, right_register);
    } else if (!operation->left_unsigned && !operation->right_unsigned) {
        /* Bias de signo: orden firmado → unsigned. Restaurar right_register porque
         * puede seguir vivo; solo left_register y el auxiliar r5 cambian al salir. */
        op_immediate(context, 5, UINT32_C(0x80000000));
        op_emit(context, "xor r%u, r5", left_register);
        op_emit(context, "xor r%u, r5", right_register);
        op_emit(context, "%s r%u, r%u", name, left_register, right_register);
        op_emit(context, "xor r%u, r5", right_register);
    } else {
        size_t label = context->next_label_id++;
        op_move(context, 5, operation->left_unsigned ? right_register : left_register);
        op_emit(context, "shri r5, #31");
        op_emit(context, "jnz r5, piper_comparar_negativo_%zu", label);
        op_emit(context, "%s r%u, r%u", name, left_register, right_register);
        op_emit(context, "j piper_comparar_fin_%zu", label);
        op_label(context, "piper_comparar_negativo_%zu", label);
        op_immediate(context, left_register, operation->op == REGALLOC_M_GT ? operation->left_unsigned : operation->right_unsigned);
        op_label(context, "piper_comparar_fin_%zu", label);
    }
}
