#include "codegen_internal.h"

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
 *
 * El parser ASM acepta inmediatos de 16 bits con signo (-32768..32767).
 * Esta función usa sumi/diffi, cuya ALU extiende el inmediato con signo;
 * no depende de la extensión de andi/ori/xori en el RTL.
 *
 * Estrategia:
 *   - 0:                    mov dest, r0
 *   - 1..32767:             mov dest, r0 ; sumi dest, #N
 *   - -32767..-1:           mov dest, r0 ; diffi dest, #|N|
 *   - -32768:              mov dest, r0 ; sumi dest, #-32768
 *   - resto de 32 bits:     construir dos mitades con shli y sumi
 *
 * Los valores negativos pueden llegar después de resolver una expresión
 * unaria o una constante.
 */
int load_immediate(CodegenContext *ctx, int dest_reg, long long value)
{
    if (ctx == NULL) {
        return 0;
    }

    if (value == 0) {
        emit_instruction(ctx, "mov r%d, r0", dest_reg);
        return 1;
    }

    if (value > 0 && value <= 32767) {
        emit_instruction(ctx, "mov r%d, r0", dest_reg);
        emit_instruction(ctx, "sumi r%d, #%lld", dest_reg, value);
        return 1;
    }

    if (value == -32768) {
        emit_instruction(ctx, "mov r%d, r0", dest_reg);
        emit_instruction(ctx, "sumi r%d, #-32768", dest_reg);
        return 1;
    }

    if (value < 0 && value >= -32767) {
        emit_instruction(ctx, "mov r%d, r0", dest_reg);
        emit_instruction(ctx, "diffi r%d, #%lld", dest_reg, -value);
        return 1;
    }

    if (value >= -2147483648LL && value <= 4294967295LL) {
        unsigned long long bits = (unsigned long long)value & 0xffffffffULL;
        long long low = (long long)(bits & 65535ULL);
        long long high;
        if (low >= 32768) low -= 65536;
        high = ((long long)bits - low) / 65536;
        if (high >= 32768) high -= 65536;
        emit_instruction(ctx, "mov r%d, r0", dest_reg);
        /* La mitad alta se ajusta porque la mitad baja se suma con signo. */
        if (high != 0) {
            emit_instruction(ctx, "sumi r%d, #%lld", dest_reg, high);
            emit_instruction(ctx, "shli r%d, #16", dest_reg);
        }
        if (low != 0) emit_instruction(ctx, "sumi r%d, #%lld", dest_reg, low);
        return 1;
    }
    codegen_context_error(ctx, "literal %lld fuera del rango de 32 bits", value);
    return 0;
}
