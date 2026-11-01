#include "codegen_internal.h"

/* Agrega un desplazamiento no negativo sin exceder el inmediato de 16 bits. */
static void add_offset(CodegenContext *ctx, int reg, int offset)
{
    while (offset > 0) {
        int chunk = offset > 32767 ? 32767 : offset;
        emit_instruction(ctx, "sumi r%d, #%d", reg, chunk);
        offset -= chunk;
    }
}

/* Calcula la dirección base de un arreglo global, local o recibido como parámetro. */
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
        if (owner->is_global) {
            return load_immediate(ctx, dest_reg, CODEGEN_DATA_BASE + sym->address_or_offset);
        }
        emit_instruction(ctx, "mov r%d, r%d", dest_reg, REG_FP);
        add_offset(ctx, dest_reg, sym->address_or_offset);
    }
    return 1;
}

/* Comprueba que la declaración use un tipo y una cantidad de dimensiones soportados. */
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
    if (dimensions == NULL || dimensions->count < 1 || dimensions->count > 2 ||
        type == PIPER_TYPE_BOOL) {
        codegen_context_error(ctx, "'%s' requiere arreglo/matriz de enteros de hasta dos dimensiones",
                              name);
        return;
    }
    /* El espacio del arreglo o de su puntero ya pertenece a la tabla de símbolos. */
}

/* Convierte uno o dos índices en la dirección del elemento y valida sus límites. */
int codegen_array_index_address(CodegenContext *ctx,
                                const AstNode *index_expr, int dest_reg)
{
    const AstNode *base;
    const AstNodeList *indices;
    Symbol *sym;
    unsigned int saved_temps, save_mask;
    int reserved, ok = 0;

    if (ctx == NULL || index_expr == NULL ||
        index_expr->kind != AST_INDEX_EXPRESSION) {
        codegen_context_error(ctx, "indexacion invalida");
        return 0;
    }
    base = index_expr->data.index_expression.base;
    indices = index_expr->data.index_expression.indices;
    if (base == NULL || base->kind != AST_IDENTIFIER_EXPRESSION ||
        base->array_rank < 1 || base->array_rank > 2 || indices == NULL ||
        indices->count != (size_t)base->array_rank) {
        codegen_context_error(ctx, "indexacion requiere uno o dos indices completos");
        return 0;
    }
    sym = lookup_symbol_or_error(ctx, base->data.identifier.name, -1);
    if (sym == NULL || !sym->is_array || sym->declared_type == PIPER_TYPE_BOOL) {
        codegen_context_error(ctx, "arreglo/matriz '%s' debe ser de enteros",
                              base->data.identifier.name);
        return 0;
    }
    if (ctx->pending_call_bytes > ctx->call_reserve_size - CALL_RECORD_SIZE) {
        codegen_context_error(ctx, "reserva de pila para indexacion excedida");
        return 0;
    }
    /* Los registros vivos se guardan bajo el frame mientras se calculan los índices. */
    saved_temps = ctx->temp_in_use;
    save_mask = (0x1eu | (saved_temps << REG_TMP0)) & ~(1u << dest_reg);
    ctx->pending_call_bytes += CALL_RECORD_SIZE;
    reserved = ctx->pending_call_bytes;
    for (int reg = REG_RET; reg <= REG_TMP1; reg++) {
        if (save_mask & (1u << reg))
            emit_instruction(ctx, "stwi r%d, %d(r7)", reg, -reserved + reg * 4);
    }
    reset_temps(ctx);

    /* Convertir fila y columna en un índice lineal medido en elementos. */
    if (codegen_expression(ctx, indices->items[0], REG_RET) == TYPE_VOID) goto done;
    load_immediate(ctx, REG_ARG2, sym->array_rows);
    emit_instruction(ctx, "mov r4, r1");
    /* Una comparación sin signo rechaza también los índices negativos. */
    emit_instruction(ctx, "ltn r4, r3");
    emit_instruction(ctx, "jz r4, " CODEGEN_INTERNAL_LABEL("error_indice"));
    if (indices->count == 2) {
        if (codegen_expression(ctx, indices->items[1], REG_ARG1) == TYPE_VOID) goto done;
        load_immediate(ctx, REG_ARG2, sym->array_cols);
        emit_instruction(ctx, "mov r4, r2");
        emit_instruction(ctx, "ltn r4, r3");
        emit_instruction(ctx, "jz r4, " CODEGEN_INTERNAL_LABEL("error_indice"));
        /* Orden por filas: base + (fila * columnas + columna) * ancho. */
        emit_instruction(ctx, "mul r1, r3");
        emit_instruction(ctx, "sum r1, r2");
    }
    if (piper_type_storage_size(sym->declared_type) == 2)
        emit_instruction(ctx, "shli r1, #1");
    else if (piper_type_storage_size(sym->declared_type) == 4)
        emit_instruction(ctx, "shli r1, #2");
    if (!load_array_base(ctx, sym, REG_ARG1)) goto done;
    emit_instruction(ctx, "sum r1, r2");
    if (dest_reg != REG_RET) emit_instruction(ctx, "mov r%d, r1", dest_reg);

    /* La dirección queda en dest_reg; los demás registros recuperan su valor. */
    for (int reg = REG_RET; reg <= REG_TMP1; reg++) {
        if (save_mask & (1u << reg))
            emit_instruction(ctx, "lowi r%d, %d(r7)", reg, -reserved + reg * 4);
    }
    ok = 1;
done:
    ctx->temp_in_use = saved_temps;
    ctx->pending_call_bytes -= CALL_RECORD_SIZE;
    return ok;
}
