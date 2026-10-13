#include "codegen_internal.h"

#include <stdlib.h>

/*
 * Genera un identificador de etiqueta único.
 * El contador es parte del contexto para que dos rutinas nunca compartan
 * el mismo número, incluso si se generan etiquetas en distinto orden.
 */

int new_label(CodegenContext *ctx)
{
    if (ctx == NULL) {
        return -1;
    }
    return ctx->label_counter++;
}

/* ----------------------------------------------------------------
 * Pila de etiquetas para break
 * ---------------------------------------------------------------- */

static int label_stack_grow(LabelStack *stack)
{
    size_t new_capacity;
    int *new_items;

    if (stack->capacity > (size_t)-1 / 2 / sizeof(*stack->items)) {
        return 0;
    }
    new_capacity = stack->capacity * 2;
    new_items = realloc(stack->items, new_capacity * sizeof(*new_items));
    if (new_items == NULL) {
        return 0;
    }
    stack->items = new_items;
    stack->capacity = new_capacity;
    return 1;
}

void label_stack_push(CodegenContext *ctx, int label)
{
    LabelStack *stack;

    if (ctx == NULL || ctx->break_stack == NULL) {
        return;
    }
    stack = ctx->break_stack;
    if (stack->count == stack->capacity) {
        if (!label_stack_grow(stack)) {
            codegen_context_error(ctx, "no hay memoria para la pila de break");
            return;
        }
    }
    stack->items[stack->count++] = label;
}

int label_stack_pop(CodegenContext *ctx)
{
    if (ctx == NULL || ctx->break_stack == NULL
        || ctx->break_stack->count == 0) {
        return -1;
    }
    return ctx->break_stack->items[--ctx->break_stack->count];
}

int label_stack_top(const CodegenContext *ctx)
{
    if (ctx == NULL || ctx->break_stack == NULL
        || ctx->break_stack->count == 0) {
        return -1;
    }
    return ctx->break_stack->items[ctx->break_stack->count - 1];
}

int label_stack_is_empty(const CodegenContext *ctx)
{
    return ctx == NULL || ctx->break_stack == NULL
        || ctx->break_stack->count == 0;
}
