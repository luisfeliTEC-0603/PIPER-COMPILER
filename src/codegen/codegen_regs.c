#include "codegen_internal.h"

/*
 * Administración de registros temporales.
 *
 * Por ahora solo r5 y r6 son temporales. Si una expresión necesita más,
 * se hará spill a memoria (pendiente para puntos posteriores).
 *
 * El bitmask temp_in_use usa los bits 0 y 1 para r5 y r6 respectivamente.
 */

#define TEMP_BIT(reg) (1u << ((reg) - REG_TMP0))

int alloc_temp(CodegenContext *ctx)
{
    if (ctx == NULL) {
        return -1;
    }
    if ((ctx->temp_in_use & TEMP_BIT(REG_TMP0)) == 0) {
        ctx->temp_in_use |= TEMP_BIT(REG_TMP0);
        return REG_TMP0;
    }
    if ((ctx->temp_in_use & TEMP_BIT(REG_TMP1)) == 0) {
        ctx->temp_in_use |= TEMP_BIT(REG_TMP1);
        return REG_TMP1;
    }
    codegen_context_error(ctx, "no hay registros temporales disponibles");
    return -1;
}

void free_temp(CodegenContext *ctx, int reg)
{
    if (ctx == NULL) {
        return;
    }
    if (reg == REG_TMP0 || reg == REG_TMP1) {
        ctx->temp_in_use &= ~TEMP_BIT(reg);
    }
}

void reset_temps(CodegenContext *ctx)
{
    if (ctx != NULL) {
        ctx->temp_in_use = 0;
    }
}

/*
 * Carga un valor inmediato en dest_reg.
 * Por ahora solo maneja valores que caben en 16 bits:
 *   mov dest_reg, r0    ; dest = 0
 *   sumi dest_reg, #N   ; dest = N
 *
 * Para valores mayores, se construye con shli + ori (pendiente).
 */

int load_immediate(CodegenContext *ctx, int dest_reg, long long value)
{
    if (ctx == NULL) {
        return 0;
    }

    if (value >= 0 && value <= 65535) {
        emit_instruction(ctx, "mov r%d, r0", dest_reg);
        if (value != 0) {
            emit_instruction(ctx, "sumi r%d, #%lld", dest_reg, value);
        }
        return 1;
    }

    if (value >= -32768 && value < 0) {
        emit_instruction(ctx, "mov r%d, r0", dest_reg);
        emit_instruction(ctx, "diffi r%d, #%lld", dest_reg, -value);
        return 1;
    }

    codegen_context_error(ctx,
        "load_immediate: valor %lld fuera de rango (pendiente de implementar)",
        value);
    return 0;
}
