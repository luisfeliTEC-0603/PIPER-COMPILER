#include "codegen_internal.h"

#include <stdio.h>
#include <string.h>

/* ----------------------------------------------------------------
 * Almacenamiento en un lvalue escalar
 * ---------------------------------------------------------------- */

static void store_to_symbol(CodegenContext *ctx, Symbol *sym, int src_reg)
{
    store_symbol_value(ctx, sym, src_reg);
}

/* ----------------------------------------------------------------
 * Lectura de un símbolo escalar
 * ---------------------------------------------------------------- */

static int load_symbol(CodegenContext *ctx, Symbol *sym, int dest_reg)
{
    return load_symbol_value(ctx, sym, dest_reg);
}

/* ----------------------------------------------------------------
 * Asignaciones a arreglos
 * ---------------------------------------------------------------- */

static void codegen_index_assignment(CodegenContext *ctx,
                                     const AstNode *target,
                                     const AstNode *value)
{
    int address_reg, value_reg, reserved;

    if (ctx->pending_call_bytes > ctx->call_reserve_size - CALL_RECORD_SIZE) {
        codegen_context_error(ctx, "reserva para store indexado excedida");
        return;
    }
    ctx->pending_call_bytes += CALL_RECORD_SIZE;
    reserved = ctx->pending_call_bytes;
    address_reg = alloc_temp(ctx);
    if (address_reg < 0) goto done;
    if (!codegen_array_index_address(ctx, target, address_reg)) {
        free_temp(ctx, address_reg);
        goto done;
    }
    /* El valor puede requerir ambos temporales o llamar rutinas. Conservar
     * la dirección ya calculada en una ranura que esas evaluaciones no pisan. */
    emit_instruction(ctx, "stwi r%d, %d(r7)", address_reg, -reserved + 28);
    free_temp(ctx, address_reg);
    value_reg = alloc_temp(ctx);
    if (value_reg < 0) goto done;
    if (codegen_expression(ctx, value, value_reg) != TYPE_VOID) {
        address_reg = alloc_temp(ctx);
        if (address_reg >= 0) {
            emit_instruction(ctx, "lowi r%d, %d(r7)", address_reg, -reserved + 28);
            emit_instruction(ctx, "stwi r%d, 0(r%d)", value_reg, address_reg);
            free_temp(ctx, address_reg);
        }
    }
    free_temp(ctx, value_reg);
done:
    ctx->pending_call_bytes -= CALL_RECORD_SIZE;
}

static void codegen_array_literal_assignment(CodegenContext *ctx, Symbol *sym,
                                             const AstNode *target,
                                             const AstNode *value)
{
    const AstNodeList *items = value->data.array_literal.elements;
    int value_reg;

    if (target->array_rank < 1 || target->array_rank > 2 ||
        sym->declared_type == PIPER_TYPE_BOOL || items == NULL ||
        items->count != (size_t)sym->array_rows) {
        codegen_context_error(ctx, "inicializacion de arreglo/matriz '%s' invalida",
                              sym->name);
        return;
    }
    value_reg = alloc_temp(ctx);
    if (value_reg < 0) return;
    size_t count = (size_t)sym->array_rows * sym->array_cols;
    for (size_t i = 0; i < count; i++) {
        int base_reg;
        size_t offset = i * SYMBOL_WORD_SIZE;
        const AstNode *element;
        if (target->array_rank == 1) {
            element = items->items[i];
        } else {
            const AstNode *row = items->items[i / (size_t)sym->array_cols];
            if (row == NULL || row->kind != AST_ARRAY_LITERAL ||
                row->data.array_literal.elements == NULL ||
                row->data.array_literal.elements->count != (size_t)sym->array_cols) {
                codegen_context_error(ctx, "fila de matriz '%s' invalida", sym->name);
                break;
            }
            element = row->data.array_literal.elements->items[i % (size_t)sym->array_cols];
        }

        if (codegen_expression(ctx, element, value_reg) == TYPE_VOID)
            break;
        base_reg = alloc_temp(ctx);
        if (base_reg < 0) break;
        if (!load_array_base(ctx, sym, base_reg)) {
            free_temp(ctx, base_reg);
            break;
        }
        while (offset > 0) {
            int chunk = offset > 32767 ? 32767 : (int)offset;
            emit_instruction(ctx, "sumi r%d, #%d", base_reg, chunk);
            offset -= (size_t)chunk;
        }
        emit_instruction(ctx, "stwi r%d, 0(r%d)", value_reg, base_reg);
        free_temp(ctx, base_reg);
    }
    free_temp(ctx, value_reg);
}

static void codegen_array_copy(CodegenContext *ctx, Symbol *dest,
                                const AstNode *value)
{
    Symbol *source = lookup_symbol_or_error(ctx, value->data.identifier.name, -1);
    char loop[CODEGEN_LABEL_SIZE];
    if (source == NULL || !source->is_array ||
        source->array_rows != dest->array_rows || source->array_cols != dest->array_cols ||
        source->declared_type != dest->declared_type || dest->declared_type == PIPER_TYPE_BOOL) {
        codegen_context_error(ctx, "copia de arreglo/matriz incompatible");
        return;
    }
    /* Las asignaciones son sentencias: no hay argumentos vivos en r1..r4.
     * Referencias iguales permiten autocopia; no hay vistas parciales en Piper. */
    if (!load_array_base(ctx, dest, REG_RET) ||
        !load_array_base(ctx, source, REG_ARG1)) return;
    load_immediate(ctx, REG_ARG2, (long long)dest->array_rows * dest->array_cols);
    snprintf(loop, sizeof(loop), CODEGEN_INTERNAL_LABEL("copiar_matriz_%d"), new_label(ctx));
    emit_label(ctx, loop);
    emit_instruction(ctx, "lowi r4, 0(r2)");
    emit_instruction(ctx, "stwi r4, 0(r1)");
    emit_instruction(ctx, "sumi r1, #4");
    emit_instruction(ctx, "sumi r2, #4");
    emit_instruction(ctx, "diffi r3, #1");
    emit_instruction(ctx, "jnz r3, %s", loop);
}

/* ----------------------------------------------------------------
 * AST_ASSIGNMENT
 * ---------------------------------------------------------------- */

static void codegen_assignment(CodegenContext *ctx, const AstNode *stmt)
{
    const AstNode *target = stmt->data.assignment.target;
    const AstNode *value = stmt->data.assignment.value;
    Symbol *sym;
    int temp;

    if (target == NULL || value == NULL) {
        codegen_context_error(ctx, "asignacion con target o value nulos");
        return;
    }

    if (target->kind == AST_IDENTIFIER_EXPRESSION) {
        sym = lookup_symbol_or_error(ctx, target->data.identifier.name, -1);
        if (sym == NULL) return;
        if (sym->symbol_class == SYMBOL_FUNC) {
            codegen_context_error(ctx,
                "no se puede asignar a la funcion '%s'", sym->name);
            return;
        }
        if (sym->is_array) {
            if (value->kind == AST_ARRAY_LITERAL)
                codegen_array_literal_assignment(ctx, sym, target, value);
            else if (value->kind == AST_IDENTIFIER_EXPRESSION)
                codegen_array_copy(ctx, sym, value);
            else
                codegen_context_error(ctx,
                    "asignacion de arreglo '%s' no soportada en punto 5",
                    sym->name);
            return;
        }
    } else if (target->kind == AST_INDEX_EXPRESSION) {
        codegen_index_assignment(ctx, target, value);
        return;
    } else {
        codegen_context_error(ctx,
            "destino de asignacion no soportado (kind=%d)", target->kind);
        return;
    }

    temp = alloc_temp(ctx);
    if (temp < 0) return;

    if (codegen_expression(ctx, value, temp) == TYPE_VOID) {
        free_temp(ctx, temp);
        return;
    }

    store_to_symbol(ctx, sym, temp);
    free_temp(ctx, temp);
    emit_comment(ctx, "%s = <expresion>", sym->name);
}

/* ----------------------------------------------------------------
 * AST_VARIABLE_DECLARATION
 * ---------------------------------------------------------------- */

static void codegen_variable_declaration(CodegenContext *ctx,
                                         const AstNode *stmt)
{
    const char *name = stmt->data.variable_declaration.name;
    AstNodeList *dims = stmt->data.variable_declaration.dimensions;

    if (name == NULL) {
        codegen_context_error(ctx, "declaracion sin nombre");
        return;
    }
    if (dims != NULL && dims->count > 0) {
        codegen_array_declaration(ctx, stmt);
        return;
    }
    emit_comment(ctx, "declaracion escalar '%s' (sin codigo)", name);
}

/* ----------------------------------------------------------------
 * WHEN / FALLBACK
 * ---------------------------------------------------------------- */

static void codegen_when(CodegenContext *ctx, const AstNode *stmt)
{
    const AstNode *condition = stmt->data.when_statement.condition;
    const AstNode *then_block = stmt->data.when_statement.then_block;
    const AstNode *fallback_block = stmt->data.when_statement.fallback_block;
    int temp;
    int has_fallback = (fallback_block != NULL);

    if (condition == NULL || then_block == NULL) {
        codegen_context_error(ctx, "when con condicion o bloque nulos");
        return;
    }

    /* Reservar temporal para la condición. */
    temp = alloc_temp(ctx);
    if (temp < 0) return;

    /* Evaluar la condición. */
    if (codegen_expression(ctx, condition, temp) == TYPE_VOID) {
        free_temp(ctx, temp);
        return;
    }

    /* Etiqueta de salida. */
    if (has_fallback) {
        char else_label[CODEGEN_LABEL_SIZE];
        char fin_label[CODEGEN_LABEL_SIZE];
        int else_n = new_label(ctx);
        int fin_n = new_label(ctx);
        snprintf(else_label, sizeof(else_label), CODEGEN_INTERNAL_LABEL("else_%d"), else_n);
        snprintf(fin_label, sizeof(fin_label), CODEGEN_INTERNAL_LABEL("fin_%d"), fin_n);

        /* Si condición falsa, saltar al fallback. */
        emit_instruction(ctx, "jz r%d, %s", temp, else_label);
        free_temp(ctx, temp);

        /* Cuerpo del then. */
        emit_comment(ctx, "then de when");
        codegen_statement(ctx, then_block);

        /* Saltar al final. */
        emit_instruction(ctx, "j %s", fin_label);

        /* Cuerpo del fallback. */
        emit_label(ctx, else_label);
        emit_comment(ctx, "fallback de when");
        codegen_statement(ctx, fallback_block);

        emit_label(ctx, fin_label);
    } else {
        char fin_label[CODEGEN_LABEL_SIZE];
        int fin_n = new_label(ctx);
        snprintf(fin_label, sizeof(fin_label), CODEGEN_INTERNAL_LABEL("fin_%d"), fin_n);

        emit_instruction(ctx, "jz r%d, %s", temp, fin_label);
        free_temp(ctx, temp);

        emit_comment(ctx, "then de when");
        codegen_statement(ctx, then_block);

        emit_label(ctx, fin_label);
    }
}

/* ----------------------------------------------------------------
 * ALWHEN
 * ---------------------------------------------------------------- */

static void codegen_alwhen(CodegenContext *ctx, const AstNode *stmt)
{
    const AstNode *condition = stmt->data.alwhen_statement.condition;
    const AstNode *body = stmt->data.alwhen_statement.body;
    int inicio_n = new_label(ctx);
    int fin_n = new_label(ctx);
    char inicio_label[CODEGEN_LABEL_SIZE];
    char fin_label[CODEGEN_LABEL_SIZE];
    int temp;

    if (condition == NULL || body == NULL) {
        codegen_context_error(ctx, "alwhen con condicion o cuerpo nulos");
        return;
    }

    snprintf(inicio_label, sizeof(inicio_label), CODEGEN_INTERNAL_LABEL("inicio_%d"), inicio_n);
    snprintf(fin_label, sizeof(fin_label), CODEGEN_INTERNAL_LABEL("fin_%d"), fin_n);

    /* Empujar etiqueta de fin para break. */
    label_stack_push(ctx, fin_n);

    emit_label(ctx, inicio_label);

    /* Evaluar condición. */
    temp = alloc_temp(ctx);
    if (temp < 0) {
        label_stack_pop(ctx);
        return;
    }
    if (codegen_expression(ctx, condition, temp) == TYPE_VOID) {
        free_temp(ctx, temp);
        label_stack_pop(ctx);
        return;
    }
    emit_instruction(ctx, "jz r%d, %s", temp, fin_label);
    free_temp(ctx, temp);

    /* Cuerpo. */
    emit_comment(ctx, "cuerpo de alwhen");
    codegen_statement(ctx, body);

    /* Saltar al inicio. */
    emit_instruction(ctx, "j %s", inicio_label);

    emit_label(ctx, fin_label);
    label_stack_pop(ctx);
}

/* ----------------------------------------------------------------
 * LOOP
 * ---------------------------------------------------------------- */

static void codegen_loop(CodegenContext *ctx, const AstNode *stmt)
{
    const char *control_name = stmt->data.loop_statement.control_name;
    const AstNode *begin = stmt->data.loop_statement.begin;
    const AstNode *end = stmt->data.loop_statement.end;
    const AstNode *step = stmt->data.loop_statement.step;
    const AstNode *body = stmt->data.loop_statement.body;
    Symbol *control_sym;
    int inicio_n, fin_n;
    char inicio_label[CODEGEN_LABEL_SIZE], fin_label[CODEGEN_LABEL_SIZE];
    int temp;

    if (control_name == NULL || begin == NULL || end == NULL
        || step == NULL || body == NULL) {
        codegen_context_error(ctx, "loop con campos nulos");
        return;
    }

    /* Buscar la variable de control. */
    control_sym = lookup_symbol_or_error(ctx, control_name, -1);
    if (control_sym == NULL) return;
    if (control_sym->symbol_class == SYMBOL_FUNC
        || control_sym->is_array
        || control_sym->type != TYPE_INT) {
        codegen_context_error(ctx,
            "el control '%s' del loop debe ser una variable entera",
            control_name);
        return;
    }

    /* Verificar que step sea una constante positiva. */
    if (step->kind != AST_INTEGER_LITERAL
        || (long long)step->data.integer_literal.value <= 0) {
        codegen_context_error(ctx,
            "el paso del loop debe ser una constante entera positiva "
            "(semantica de paso negativo pendiente)");
        return;
    }

    inicio_n = new_label(ctx);
    fin_n = new_label(ctx);
    snprintf(inicio_label, sizeof(inicio_label), CODEGEN_INTERNAL_LABEL("inicio_%d"), inicio_n);
    snprintf(fin_label, sizeof(fin_label), CODEGEN_INTERNAL_LABEL("fin_%d"), fin_n);

    /* control = begin. */
    temp = alloc_temp(ctx);
    if (temp < 0) return;
    if (codegen_expression(ctx, begin, temp) == TYPE_VOID) {
        free_temp(ctx, temp);
        return;
    }
    store_to_symbol(ctx, control_sym, temp);
    free_temp(ctx, temp);
    emit_comment(ctx, "%s = begin", control_name);

    /* Empujar etiqueta de fin para break. */
    label_stack_push(ctx, fin_n);

    emit_label(ctx, inicio_label);

    /* Evaluar end en temp1. */
    temp = alloc_temp(ctx);
    if (temp < 0) {
        label_stack_pop(ctx);
        return;
    }
    if (codegen_expression(ctx, end, temp) == TYPE_VOID) {
        free_temp(ctx, temp);
        label_stack_pop(ctx);
        return;
    }

    /* Cargar control en temp2. */
    {
        int temp2 = alloc_temp(ctx);
        if (temp2 < 0) {
            free_temp(ctx, temp);
            label_stack_pop(ctx);
            return;
        }
        if (!load_symbol(ctx, control_sym, temp2)) {
            free_temp(ctx, temp2);
            free_temp(ctx, temp);
            label_stack_pop(ctx);
            return;
        }

        /* Comparar: si control >= end, salir.
         * Calcular control < end en temp2 y luego invertir.
         * emit_typed_order → temp2 = (control < end) ? 1 : 0,
         * con máscara del bit 31 para operandos signed.
         * xori rTemp2, #1   → temp2 = !(control < end) = (control >= end)
         * jnz rTemp2, fin   → si control >= end, salir
         */
        int control_unsigned = control_sym->declared_type == PIPER_TYPE_UB8 ||
            control_sym->declared_type == PIPER_TYPE_UB16 ||
            control_sym->declared_type == PIPER_TYPE_UB32;
        /* r1..r4 no contienen valores vivos entre sentencias de la rutina. */
        emit_typed_order(ctx, temp2, temp, REG_ARG3, control_unsigned,
                         codegen_expression_is_unsigned(end), 0);
        emit_instruction(ctx, "xori r%d, #1", temp2);
        emit_instruction(ctx, "jnz r%d, %s", temp2, fin_label);
        free_temp(ctx, temp2);
    }
    free_temp(ctx, temp);

    /* Cuerpo. */
    emit_comment(ctx, "cuerpo de loop");
    codegen_statement(ctx, body);

    /* control = control + step. */
    {
        int t = alloc_temp(ctx);
        if (t >= 0) {
            if (load_symbol(ctx, control_sym, t)) {
                long long step_val =
                    (long long)step->data.integer_literal.value;
                if (step_val <= 32767) {
                    emit_instruction(ctx, "sumi r%d, #%lld", t, step_val);
                } else {
                    int t2 = alloc_temp(ctx);
                    if (t2 >= 0) {
                        load_immediate(ctx, t2, step_val);
                        emit_instruction(ctx, "sum r%d, r%d", t, t2);
                        free_temp(ctx, t2);
                    }
                }
                store_to_symbol(ctx, control_sym, t);
            }
            free_temp(ctx, t);
        }
    }

    /* Saltar al inicio. */
    emit_instruction(ctx, "j %s", inicio_label);
    emit_label(ctx, fin_label);
    label_stack_pop(ctx);
}

/* ----------------------------------------------------------------
 * break y output
 * ---------------------------------------------------------------- */

static void codegen_break(CodegenContext *ctx, const AstNode *stmt)
{
    int fin_n;
    char fin_label[CODEGEN_LABEL_SIZE];

    (void)stmt;
    fin_n = label_stack_top(ctx);
    if (fin_n < 0) {
        codegen_context_error(ctx, "break fuera de un ciclo");
        return;
    }
    snprintf(fin_label, sizeof(fin_label), CODEGEN_INTERNAL_LABEL("fin_%d"), fin_n);
    emit_instruction(ctx, "j %s", fin_label);
}

static void codegen_output(CodegenContext *ctx, const AstNode *stmt)
{
    const AstNode *value = stmt->data.output_statement.value;
    char epilogue_name[CODEGEN_LABEL_SIZE];

    if (ctx->current_function == NULL || value == NULL) {
        codegen_context_error(ctx, "output fuera de una rutina o sin valor");
        return;
    }
    if (value->kind != AST_NONE_LITERAL &&
        codegen_expression(ctx, value, REG_RET) == TYPE_VOID)
        return;

    snprintf(epilogue_name, sizeof(epilogue_name), CODEGEN_INTERNAL_LABEL("epilogo_%s"),
             ctx->current_function->name);
    emit_instruction(ctx, "j %s", epilogue_name);
}

/* ----------------------------------------------------------------
 * Llamadas pendientes
 * ---------------------------------------------------------------- */

static void codegen_call_statement(CodegenContext *ctx, const AstNode *stmt)
{
    (void)codegen_call(ctx, stmt, -1);
}

/* ----------------------------------------------------------------
 * Ámbito de bloques
 * ---------------------------------------------------------------- */

static void codegen_block(CodegenContext *ctx, const AstNode *block)
{
    Scope *saved_scope = ctx->current_scope;
    Scope *block_scope = block->semantic_scope;

    if (block_scope == NULL || block_scope->parent != saved_scope) {
        codegen_context_error(ctx, "ambito semantico del bloque no disponible");
        return;
    }
    ctx->current_scope = block_scope;
    codegen_statement_list(ctx, block->data.block.statements);
    ctx->current_scope = saved_scope;
}

/* ----------------------------------------------------------------
 * Punto de entrada
 * ---------------------------------------------------------------- */

void codegen_statement(CodegenContext *ctx, const AstNode *stmt)
{
    if (ctx == NULL || stmt == NULL) {
        return;
    }

    switch (stmt->kind) {
    case AST_UNARY_EXPRESSION:
        {
            int temp = alloc_temp(ctx);
            if (temp < 0) break;
            codegen_expression(ctx, stmt, temp);
            free_temp(ctx, temp);
        }
        break;
    case AST_BINARY_EXPRESSION: {
        int temp = alloc_temp(ctx);
        if (temp < 0) {
            break;
        }
        (void)codegen_expression(ctx, stmt, temp);
        free_temp(ctx, temp);
        break;
    }
    case AST_ASSIGNMENT:
        codegen_assignment(ctx, stmt);
        break;
    case AST_VARIABLE_DECLARATION:
        codegen_variable_declaration(ctx, stmt);
        break;
    case AST_WHEN_STATEMENT:
        codegen_when(ctx, stmt);
        break;
    case AST_ALWHEN_STATEMENT:
        codegen_alwhen(ctx, stmt);
        break;
    case AST_LOOP_STATEMENT:
        codegen_loop(ctx, stmt);
        break;
    case AST_BREAK_STATEMENT:
        codegen_break(ctx, stmt);
        break;
    case AST_OUTPUT_STATEMENT:
        codegen_output(ctx, stmt);
        break;
    case AST_CALL_EXPRESSION:
        codegen_call_statement(ctx, stmt);
        break;
    case AST_ROUTINE_DECLARATION:
        codegen_routine(ctx, stmt);
        break;
    case AST_BLOCK:
        codegen_block(ctx, stmt);
        break;
    case AST_IMPORT_DECLARATION:
        emit_comment(ctx, "import '%s' ignorado en codegen",
                     stmt->data.import_declaration.symbol);
        break;
    default:
        codegen_context_error(ctx,
            "codegen_statement: nodo no manejado (kind=%d)", stmt->kind);
        break;
    }
}

void codegen_statement_list(CodegenContext *ctx, const AstNodeList *list)
{
    if (ctx == NULL || list == NULL) {
        return;
    }
    for (size_t i = 0; i < list->count; i++) {
        codegen_statement(ctx, list->items[i]);
    }
}
