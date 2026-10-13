#include "codegen_internal.h"

#include <stdarg.h>

/*
 * Todas las funciones de emisión escriben directamente en ctx->output.
 * El formato sigue la sintaxis ASM lineal de la Persona 2:
 *   - Etiquetas: sin indentación, con ':' al final.
 *   - Instrucciones: 4 espacios de indentación.
 *   - Comentarios: 4 espacios + ';'.
 *   - Líneas en blanco: solo '\n'.
 */

void emit_label(CodegenContext *ctx, const char *name)
{
    if (ctx == NULL || name == NULL) {
        return;
    }
    fprintf(ctx->output, "%s:\n", name);
}

void emit_instruction(CodegenContext *ctx, const char *fmt, ...)
{
    va_list args;

    if (ctx == NULL || fmt == NULL) {
        return;
    }

    fputs("    ", ctx->output);
    va_start(args, fmt);
    vfprintf(ctx->output, fmt, args);
    va_end(args);
    fputc('\n', ctx->output);
}

void emit_comment(CodegenContext *ctx, const char *fmt, ...)
{
    va_list args;

    if (ctx == NULL || fmt == NULL) {
        return;
    }
    if (!ctx->options.emitir_comentarios) {
        return;
    }

    fputs("    ; ", ctx->output);
    va_start(args, fmt);
    vfprintf(ctx->output, fmt, args);
    va_end(args);
    fputc('\n', ctx->output);
}

void emit_blank(CodegenContext *ctx)
{
    if (ctx == NULL) {
        return;
    }
    fputc('\n', ctx->output);
}
