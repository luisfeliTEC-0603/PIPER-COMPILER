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

void emit_typed_order(CodegenContext *ctx, int left, int right, int scratch,
                      int left_unsigned, int right_unsigned, int greater)
{
    const char *instruction = greater ? "gtn" : "ltn";
    if (left_unsigned && right_unsigned) {
        emit_instruction(ctx, "%s r%d, r%d", instruction, left, right);
    } else if (!left_unsigned && !right_unsigned) {
        /* XOR 0x80000000 traslada el orden signed al orden unsigned.
         * Construir la máscara desde cero, sin depender del contenido previo. */
        load_immediate(ctx, scratch, 2147483648LL);
        emit_instruction(ctx, "xor r%d, r%d", left, scratch);
        emit_instruction(ctx, "xor r%d, r%d", right, scratch);
        emit_instruction(ctx, "%s r%d, r%d", instruction, left, right);
        emit_instruction(ctx, "xor r%d, r%d", right, scratch);
    } else {
        /* Un signed negativo es menor que cualquier unsigned. Si es positivo,
         * ambos valores se pueden comparar directamente como unsigned. */
        int id = new_label(ctx);
        char negative[CODEGEN_LABEL_SIZE], end[CODEGEN_LABEL_SIZE];
        snprintf(negative, sizeof(negative), CODEGEN_INTERNAL_LABEL("comparar_negativo_%d"), id);
        snprintf(end, sizeof(end), CODEGEN_INTERNAL_LABEL("comparar_fin_%d"), id);
        emit_instruction(ctx, "mov r%d, r%d", scratch, left_unsigned ? right : left);
        emit_instruction(ctx, "shri r%d, #31", scratch);
        emit_instruction(ctx, "jnz r%d, %s", scratch, negative);
        emit_instruction(ctx, "%s r%d, r%d", instruction, left, right);
        emit_instruction(ctx, "j %s", end);
        emit_label(ctx, negative);
        load_immediate(ctx, left, greater ? left_unsigned : right_unsigned);
        emit_label(ctx, end);
    }
}
