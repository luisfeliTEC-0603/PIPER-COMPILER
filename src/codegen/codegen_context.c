#include "codegen_internal.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

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
    ctx->call_site_count = 0;
    ctx->pending_call_bytes = 0;
    ctx->call_reserve_size = table->global_call_reserve_size;
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

void codegen_context_error(CodegenContext *ctx, const char *fmt, ...)
{
    va_list args;

    if (ctx == NULL) {
        return;
    }

    ctx->error_count++;

    /* Conservar el último mensaje disponible para diagnóstico. */
    va_start(args, fmt);
    vsnprintf(ctx->last_error, sizeof(ctx->last_error), fmt, args);
    va_end(args);

    fprintf(stderr, "[codegen] error: %s\n", ctx->last_error);
}

int codegen_has_errors(const CodegenContext *ctx)
{
    return ctx == NULL || ctx->error_count > 0;
}

/* Busca un nombre desde el ámbito actual hacia sus padres y valida su clase. */
Symbol *lookup_symbol_or_error(CodegenContext *ctx,
                               const char *name,
                               int expected_class)
{
    Scope *scope;
    Symbol *sym = NULL;

    if (ctx == NULL || name == NULL) {
        return NULL;
    }

    /* La búsqueda respeta el anidamiento léxico de la tabla de símbolos. */
    for (scope = ctx->current_scope; scope != NULL; scope = scope->parent) {
        for (Symbol *it = scope->symbols_head; it != NULL; it = it->next) {
            if (strcmp(it->name, name) == 0) {
                sym = it;
                goto found;
            }
        }
    }

found:
    if (sym == NULL) {
        codegen_context_error(ctx,
            "identificador '%s' no encontrado en la tabla de simbolos", name);
        return NULL;
    }

    if (expected_class >= 0 && (int)sym->symbol_class != expected_class) {
        codegen_context_error(ctx,
            "'%s' no es de la clase esperada (%d)", name, expected_class);
        return NULL;
    }

    return sym;
}

/* Determina el ámbito que posee un símbolo visible desde la posición actual. */
Scope *find_owner_scope(CodegenContext *ctx, Symbol *sym)
{
    Scope *scope;

    if (ctx == NULL || sym == NULL) {
        return NULL;
    }
    for (scope = ctx->current_scope; scope != NULL; scope = scope->parent) {
        for (Symbol *it = scope->symbols_head; it != NULL; it = it->next) {
            if (it == sym) {
                return scope;
            }
        }
    }
    return NULL;
}

/* La tabla conserva offsets lógicos. Solo codegen traduce globales a
 * direcciones físicas; locales y parámetros siguen siendo relativos a r7. */
void emit_typed_load(CodegenContext *ctx, PiperType type, int dest_reg,
                     int offset, int base_reg)
{
    const char *mnemonic;
    switch (type) {
    case PIPER_TYPE_B8: mnemonic = "lobi"; break;
    case PIPER_TYPE_UB8: mnemonic = "lobui"; break;
    case PIPER_TYPE_B16: mnemonic = "lohi"; break;
    case PIPER_TYPE_UB16: mnemonic = "lohui"; break;
    case PIPER_TYPE_BOOL:
    case PIPER_TYPE_B32:
    case PIPER_TYPE_UB32: mnemonic = "lowi"; break;
    default:
        codegen_context_error(ctx, "tipo sin instruccion de lectura");
        return;
    }
    emit_instruction(ctx, "%s r%d, %d(r%d)", mnemonic, dest_reg, offset, base_reg);
}

void emit_typed_store(CodegenContext *ctx, PiperType type, int src_reg,
                      int offset, int base_reg)
{
    const char *mnemonic;
    switch (type) {
    case PIPER_TYPE_B8:
    case PIPER_TYPE_UB8: mnemonic = "stbi"; break;
    case PIPER_TYPE_B16:
    case PIPER_TYPE_UB16: mnemonic = "sthi"; break;
    case PIPER_TYPE_BOOL:
    case PIPER_TYPE_B32:
    case PIPER_TYPE_UB32: mnemonic = "stwi"; break;
    default:
        codegen_context_error(ctx, "tipo sin instruccion de escritura");
        return;
    }
    emit_instruction(ctx, "%s r%d, %d(r%d)", mnemonic, src_reg, offset, base_reg);
}

int load_symbol_value(CodegenContext *ctx, Symbol *sym, int dest_reg)
{
    Scope *owner = find_owner_scope(ctx, sym);
    if (owner == NULL || sym->address_or_offset < 0) {
        codegen_context_error(ctx, "simbolo sin direccion de lectura valida");
        return 0;
    }
    if (owner->is_global) {
        if (!load_immediate(ctx, dest_reg, CODEGEN_DATA_BASE + sym->address_or_offset))
            return 0;
        emit_typed_load(ctx, sym->declared_type, dest_reg, 0, dest_reg);
    } else {
        emit_typed_load(ctx, sym->declared_type, dest_reg,
                        sym->address_or_offset, REG_FP);
    }
    return 1;
}

/* Escribe un escalar local directamente o calcula primero su dirección global. */
void store_symbol_value(CodegenContext *ctx, Symbol *sym, int src_reg)
{
    Scope *owner = find_owner_scope(ctx, sym);
    if (owner == NULL || sym->address_or_offset < 0) {
        codegen_context_error(ctx, "simbolo sin direccion de escritura valida");
        return;
    }
    if (!owner->is_global) {
        emit_typed_store(ctx, sym->declared_type, src_reg,
                         sym->address_or_offset, REG_FP);
        return;
    }
    if (ctx->pending_call_bytes > ctx->call_reserve_size - CALL_RECORD_SIZE) {
        codegen_context_error(ctx, "reserva para escritura global excedida");
        return;
    }
    int scratch = src_reg == REG_ARG0 ? REG_ARG1 : REG_ARG0;
    ctx->pending_call_bytes += CALL_RECORD_SIZE;
    int saved_offset = -ctx->pending_call_bytes + 28;
    /* Usar un registro de argumentos evita consumir uno de los dos temporales. */
    emit_instruction(ctx, "stwi r%d, %d(r7)", scratch, saved_offset);
    load_immediate(ctx, scratch, CODEGEN_DATA_BASE + sym->address_or_offset);
    emit_typed_store(ctx, sym->declared_type, src_reg, 0, scratch);
    emit_instruction(ctx, "lowi r%d, %d(r7)", scratch, saved_offset);
    ctx->pending_call_bytes -= CALL_RECORD_SIZE;
}
