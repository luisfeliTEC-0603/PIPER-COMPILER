#include "codegen_internal.h"

/* ----------------------------------------------------------------
 * Desplazamientos de arreglos
 * ---------------------------------------------------------------- */

/* Agrega un desplazamiento no negativo sin exceder el inmediato de 16 bits. */
static void add_offset(CodegenContext *ctx, int reg, int offset)
{
    while (offset > 0) {
        int chunk = offset > 32767 ? 32767 : offset;
        emit_instruction(ctx, "sumi r%d, #%d", reg, chunk);
        offset -= chunk;
    }
}

/* ----------------------------------------------------------------
 * Base del arreglo
 * ---------------------------------------------------------------- */

int load_array_base(CodegenContext *ctx, Symbol *sym, int dest_reg)
{
    Scope *owner;

    if (ctx == NULL || sym == NULL || !sym->is_array ||
        sym->address_or_offset < 0) {
        codegen_context_error(ctx, "base de arreglo invalida");
        return 0;
    }
    owner = find_owner_scope(ctx, sym);
    if (owner == NULL) {
        codegen_context_error(ctx, "no se encontro el ambito de '%s'", sym->name);
        return 0;
    }
    if (sym->symbol_class == SYMBOL_PARAM) {
        emit_instruction(ctx, "lowi r%d, %d(r%d)", dest_reg,
                         sym->address_or_offset, REG_FP);
    } else {
        emit_instruction(ctx, "mov r%d, r%d", dest_reg,
                         owner->is_global ? REG_ZERO : REG_FP);
        add_offset(ctx, dest_reg, sym->address_or_offset);
    }
    return 1;
}

/* ----------------------------------------------------------------
 * Declaración de arreglos
 * ---------------------------------------------------------------- */

void codegen_array_declaration(CodegenContext *ctx, const AstNode *decl)
{
    const AstNodeList *dimensions;
    PiperType type;
    const char *name;

    if (ctx == NULL || decl == NULL)
        return;
    if (decl->kind == AST_VARIABLE_DECLARATION) {
        dimensions = decl->data.variable_declaration.dimensions;
        type = decl->data.variable_declaration.type;
        name = decl->data.variable_declaration.name;
    } else if (decl->kind == AST_PARAMETER) {
        dimensions = decl->data.parameter.dimensions;
        type = decl->data.parameter.type;
        name = decl->data.parameter.name;
    } else {
        return;
    }
    if (dimensions == NULL || dimensions->count != 1 ||
        type == PIPER_TYPE_BOOL) {
        codegen_context_error(ctx, "arreglo de '%s' no soportado en punto 5",
                              name);
        return;
    }
    /* La tabla de simbolos reservo elementos o la ranura del puntero. */
}

/* ----------------------------------------------------------------
 * Dirección de un elemento
 * ---------------------------------------------------------------- */

int codegen_array_index_address(CodegenContext *ctx,
                                const AstNode *index_expr, int dest_reg)
{
    const AstNode *base;
    const AstNodeList *indices;
    Symbol *sym;
    Scope *owner;
    int base_reg;

    if (ctx == NULL || index_expr == NULL ||
        index_expr->kind != AST_INDEX_EXPRESSION) {
        codegen_context_error(ctx, "indexacion invalida");
        return 0;
    }
    base = index_expr->data.index_expression.base;
    indices = index_expr->data.index_expression.indices;
    if (base == NULL || base->kind != AST_IDENTIFIER_EXPRESSION ||
        base->array_rank != 1 || indices == NULL || indices->count != 1) {
        codegen_context_error(ctx, "solo se soporta indexacion de una dimension");
        return 0;
    }
    sym = lookup_symbol_or_error(ctx, base->data.identifier.name, -1);
    if (sym == NULL || !sym->is_array || sym->declared_type == PIPER_TYPE_BOOL) {
        codegen_context_error(ctx, "arreglo '%s' no soportado en punto 5",
                              base->data.identifier.name);
        return 0;
    }
    if (codegen_expression(ctx, indices->items[0], dest_reg) == TYPE_VOID)
        return 0;
    emit_instruction(ctx, "shli r%d, #2", dest_reg);

    owner = find_owner_scope(ctx, sym);
    if (owner == NULL) {
        codegen_context_error(ctx, "no se encontro el ambito de '%s'", sym->name);
        return 0;
    }
    if (sym->symbol_class != SYMBOL_PARAM) {
        if (!owner->is_global)
            emit_instruction(ctx, "sum r%d, r%d", dest_reg, REG_FP);
        add_offset(ctx, dest_reg, sym->address_or_offset);
        return 1;
    }

    base_reg = alloc_temp(ctx);
    if (base_reg < 0) return 0;
    if (!load_array_base(ctx, sym, base_reg)) {
        free_temp(ctx, base_reg);
        return 0;
    }
    emit_instruction(ctx, "sum r%d, r%d", dest_reg, base_reg);
    free_temp(ctx, base_reg);
    return 1;
}
