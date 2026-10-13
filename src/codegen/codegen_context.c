#include "codegen_internal.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ----------------------------------------------------------------
 * Pila de etiquetas
 * ---------------------------------------------------------------- */

static LabelStack *label_stack_create(void)
{
    LabelStack *stack = calloc(1, sizeof(*stack));
    if (stack == NULL) {
        return NULL;
    }
    stack->capacity = 8;
    stack->items = malloc(stack->capacity * sizeof(*stack->items));
    if (stack->items == NULL) {
        free(stack);
        return NULL;
    }
    stack->count = 0;
    return stack;
}

static void label_stack_destroy(LabelStack *stack)
{
    if (stack == NULL) {
        return;
    }
    free(stack->items);
    free(stack);
}

/* ----------------------------------------------------------------
 * Ciclo de vida del contexto
 * ---------------------------------------------------------------- */

CodegenContext *codegen_context_create(const AstNode *root,
                                       const SymbolTable *table,
                                       FILE *output,
                                       const CodegenOptions *options)
{
    CodegenContext *ctx;

    if (root == NULL || table == NULL || output == NULL) {
        return NULL;
    }

    ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return NULL;
    }

    ctx->root = root;
    ctx->table = table;
    ctx->output = output;

    if (options != NULL) {
        ctx->options = *options;
    } else {
        ctx->options.emitir_comentarios = 0;
        ctx->options.optimizar_temporales = 0;
    }

    ctx->label_counter = 0;
    ctx->error_count = 0;
    ctx->current_function = NULL;
    ctx->current_scope = table->global_scope;
    ctx->frame_offset = 0;
    ctx->temp_in_use = 0;
    ctx->last_error[0] = '\0';

    ctx->break_stack = label_stack_create();
    if (ctx->break_stack == NULL) {
        free(ctx);
        return NULL;
    }

    return ctx;
}

void codegen_context_destroy(CodegenContext *ctx)
{
    if (ctx == NULL) {
        return;
    }
    label_stack_destroy(ctx->break_stack);
    free(ctx);
}

/* ----------------------------------------------------------------
 * Acumulación de errores
 * ---------------------------------------------------------------- */

void codegen_context_error(CodegenContext *ctx, const char *fmt, ...)
{
    va_list args;

    if (ctx == NULL) {
        return;
    }

    ctx->error_count++;

    /* Guardar el último error para diagnóstico */
    va_start(args, fmt);
    vsnprintf(ctx->last_error, sizeof(ctx->last_error), fmt, args);
    va_end(args);

    /* También emitir a stderr para visibilidad inmediata */
    fprintf(stderr, "[codegen] error: %s\n", ctx->last_error);
}

int codegen_has_errors(const CodegenContext *ctx)
{
    return ctx == NULL || ctx->error_count > 0;
}
