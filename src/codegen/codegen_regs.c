#include "codegen_internal.h"

/* ----------------------------------------------------------------
 * Administración de registros temporales
 * ---------------------------------------------------------------- */

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
 * La ISA de PORYGON usa inmediatos de 16 bits CON SIGNO en todas las
 * instrucciones que los aceptan (`sumi`, `diffi`, `andi`, `ori`, etc.).
 * Por lo tanto, el rango representable directamente es -32768..32767.
 *
 * Estrategia:
 *   - 0:                    mov dest, r0
 *   - 1..32767:             mov dest, r0 ; sumi dest, #N
 *   - -32768..-1:           mov dest, r0 ; diffi dest, #|N|
 *   - fuera de rango:       error (se construirá con shli+ori en punto 3)
 *
 * Nota: el parser representa números negativos como AST_UNARY_EXPRESSION
 * (NEGATE) sobre un AST_INTEGER_LITERAL positivo. Por eso `value` llega
 * aquí normalmente como positivo; los negativos aparecen cuando el
 * llamador ya resolvió el signo (por ejemplo, en un contexto constante).
 * De todas formas, esta función maneja ambos signos por completitud.
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

    if (value < 0 && value >= -32768) {
        emit_instruction(ctx, "mov r%d, r0", dest_reg);
        emit_instruction(ctx, "diffi r%d, #%lld", dest_reg, -value);
        return 1;
    }

    codegen_context_error(ctx,
        "load_immediate: valor %lld fuera del rango -32768..32767 "
        "(se construira con shli+ori en punto 3)",
        value);
    return 0;
}
