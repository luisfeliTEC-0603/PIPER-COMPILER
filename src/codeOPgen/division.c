/** @file division.c
 * DIV/MOD/POW por software. La ISA no ofrece estas operaciones directamente.
 * Los operandos ya fueron evaluados una vez. La reserva de 32 bytes bajo r7
 * conserva r1..r4 (offsets -28..-16), operandos/resultado (-8) y signo (-4).
 * r7 permanece fijo: el area de parametros/locales/spills de la funcion no se modifica.
 */
#include "internal.h"

static void check_magnitude(CodeOPgenContext *context, PiperType type)
{
    /* r1=magnitud, r4=mascara de signo (0 o -1). La ISA compara unsigned. */
    unsigned bits = op_bits(type);
    if (op_unsigned(type)) {
        op_emit(context, "jnz r4, " OP_ERROR("aritmetico"));
        if (bits == 32) return;
        op_immediate(context, 5, (INT64_C(1) << bits) - 1);
    } else {
        op_immediate(context, 5, (INT64_C(1) << (bits - 1)) - 1);
        op_emit(context, "diff r5, r4");
    }
    op_move(context, 6, 1);
    op_emit(context, "gtn r6, r5");
    op_emit(context, "jnz r6, " OP_ERROR("aritmetico"));
}

static void divide(CodeOPgenContext *context, const RegallocMachineInstruction *operation)
{
    size_t label = context->next_label_id++;
    bool remainder = operation->op == REGALLOC_M_MOD;
    bool signed_result = !operation->left_unsigned || (!remainder && !operation->right_unsigned);
    op_emit(context, "jz r2, " OP_ERROR("division_cero"));
    op_move(context, 4, 0);
    if (!operation->left_unsigned) {
        op_move(context, 4, 1);
        op_emit(context, "sari r4, #31");
        op_emit(context, "xor r1, r4");
        op_emit(context, "diff r1, r4");
    }
    if (!operation->right_unsigned) {
        op_move(context, 5, 2);
        op_emit(context, "sari r5, #31");
        op_emit(context, "xor r2, r5");
        op_emit(context, "diff r2, r5");
        if (!remainder) op_emit(context, "xor r4, r5");
    }
    op_store(context, PIPER_TYPE_B32, 4, -4, 7);
    op_move(context, 3, 0);
    op_immediate(context, 6, 32);
    /* Division restauradora de 32 pasos: r1 termina como cociente y r3 como
     * residuo. El bit extra evita perder el acarreo de r3 al desplazar. */
    op_label(context, "piper_division_repetir_%zu", label);
    op_move(context, 5, 3);
    op_emit(context, "shri r5, #31");
    op_move(context, 4, 1);
    op_emit(context, "shri r4, #31");
    op_emit(context, "shli r1, #1");
    op_emit(context, "shli r3, #1");
    op_emit(context, "or r3, r4");
    op_emit(context, "jnz r5, piper_division_restar_%zu", label);
    op_move(context, 4, 3);
    op_emit(context, "ltn r4, r2");
    op_emit(context, "jnz r4, piper_division_siguiente_%zu", label);
    op_label(context, "piper_division_restar_%zu", label);
    op_emit(context, "diff r3, r2");
    op_emit(context, "sumi r1, #1");
    op_label(context, "piper_division_siguiente_%zu", label);
    op_emit(context, "diffi r6, #1");
    op_emit(context, "jnz r6, piper_division_repetir_%zu", label);
    if (remainder) op_move(context, 1, 3);
    op_load(context, PIPER_TYPE_B32, 4, -4, 7);
    /* Cero no tiene signo negativo, incluido 1/-2 y -4%2. */
    op_emit(context, "jnz r1, piper_division_signo_%zu", label);
    op_move(context, 4, 0);
    op_label(context, "piper_division_signo_%zu", label);
    check_magnitude(context, operation->type.piper);
    if (signed_result) {
        op_emit(context, "xor r1, r4");
        op_emit(context, "diff r1, r4");
    }
}

static void power(CodeOPgenContext *context, const RegallocMachineInstruction *operation)
{
    size_t label = context->next_label_id++;
    if (!operation->right_unsigned) {
        op_move(context, 5, 2);
        op_emit(context, "shri r5, #31");
        op_emit(context, "jnz r5, " OP_ERROR("exponente_negativo"));
    }
    if (op_unsigned(operation->type.piper) && !operation->left_unsigned) {
        op_move(context, 4, 1);
        op_emit(context, "sari r4, #31");
        op_emit(context, "jz r4, piper_potencia_magnitud_%zu", label);
        op_move(context, 5, 2);
        op_emit(context, "andi r5, #1");
        op_emit(context, "jnz r5, " OP_ERROR("aritmetico"));
        op_emit(context, "xor r1, r4");
        op_emit(context, "diff r1, r4");
        op_label(context, "piper_potencia_magnitud_%zu", label);
    }
    op_immediate(context, 3, 1);
    op_label(context, "piper_potencia_repetir_%zu", label);
    op_emit(context, "jz r2, piper_potencia_fin_%zu", label);
    op_move(context, 4, 2);
    op_emit(context, "andi r4, #1");
    op_emit(context, "jz r4, piper_potencia_cuadrado_%zu", label);
    op_checked_multiply(context, 3, 1, operation->type.piper);
    op_label(context, "piper_potencia_cuadrado_%zu", label);
    op_emit(context, "shri r2, #1");
    /* El ultimo cuadrado no se usa y podria desbordar innecesariamente. */
    op_emit(context, "jz r2, piper_potencia_fin_%zu", label);
    op_checked_multiply(context, 1, 1, operation->type.piper);
    op_emit(context, "j piper_potencia_repetir_%zu", label);
    op_label(context, "piper_potencia_fin_%zu", label);
    op_move(context, 1, 3);
}

void op_general_arithmetic(CodeOPgenContext *context, const RegallocMachineInstruction *operation)
{
    unsigned destination_register = op_register(context, operation->def), right_register = op_register(context, operation->use1);
    for (unsigned r = 1; r <= 4; r++) op_store(context, PIPER_TYPE_B32, r, -32 + (int)r * 4, 7);
    op_store(context, PIPER_TYPE_B32, destination_register, -8, 7);
    op_store(context, PIPER_TYPE_B32, right_register, -4, 7);
    op_load(context, PIPER_TYPE_B32, 1, -8, 7);
    op_load(context, PIPER_TYPE_B32, 2, -4, 7);
    if (operation->op == REGALLOC_M_POW) power(context, operation);
    else divide(context, operation);
    op_store(context, PIPER_TYPE_B32, 1, -8, 7);
    for (unsigned r = 1; r <= 4; r++) op_load(context, PIPER_TYPE_B32, r, -32 + (int)r * 4, 7);
    op_load(context, PIPER_TYPE_B32, destination_register, -8, 7);
}
