#include "codegen_internal.h"

#include <stdio.h>
#include <string.h>

/* ----------------------------------------------------------------
 * Almacenamiento en un lvalue escalar
 * ---------------------------------------------------------------- */

static void store_to_symbol(CodegenContext *ctx, Symbol *sym, int src_reg)
{
    Scope *owner = find_owner_scope(ctx, sym);

    if (owner == NULL) {
        codegen_context_error(ctx,
            "no se pudo determinar el ambito de '%s'", sym->name);
        return;
    }

    if (owner->is_global) {
        emit_instruction(ctx, "stwi r%d, %d(r%d)",
                         src_reg, sym->address_or_offset, REG_ZERO);
    } else {
        emit_instruction(ctx, "stwi r%d, %d(r%d)",
                         src_reg, sym->address_or_offset, REG_FP);
    }
}

/*
 * Carga en dest_reg el valor de un símbolo escalar.
 * Devuelve 1 si pudo, 0 si hubo error.
 */

static int load_symbol(CodegenContext *ctx, Symbol *sym, int dest_reg)
{
    Scope *owner = find_owner_scope(ctx, sym);

    if (owner == NULL) {
        codegen_context_error(ctx,
            "no se pudo determinar el ambito de '%s'", sym->name);
        return 0;
    }
    if (owner->is_global) {
        emit_instruction(ctx, "lowi r%d, %d(r%d)",
                         dest_reg, sym->address_or_offset, REG_ZERO);
    } else {
        emit_instruction(ctx, "lowi r%d, %d(r%d)",
                         dest_reg, sym->address_or_offset, REG_FP);
    }
    return 1;
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
        if (sym->type == TYPE_ARRAY || sym->is_array) {
            codegen_context_error(ctx,
                "no se puede asignar a '%s' sin indexacion", sym->name);
            return;
        }
    } else if (target->kind == AST_INDEX_EXPRESSION) {
        codegen_context_error(ctx,
            "asignacion a indexacion aun no implementada (punto 5)");
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
        emit_comment(ctx, "declaracion de arreglo '%s' (pendiente punto 5)",
                     name);
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
        char else_label[32];
        char fin_label[32];
        int else_n = new_label(ctx);
        int fin_n = new_label(ctx);
        snprintf(else_label, sizeof(else_label), "else_%d", else_n);
        snprintf(fin_label, sizeof(fin_label), "fin_%d", fin_n);

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
        char fin_label[32];
        int fin_n = new_label(ctx);
        snprintf(fin_label, sizeof(fin_label), "fin_%d", fin_n);

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
    char inicio_label[32];
    char fin_label[32];
    int temp;

    if (condition == NULL || body == NULL) {
        codegen_context_error(ctx, "alwhen con condicion o cuerpo nulos");
        return;
    }

    snprintf(inicio_label, sizeof(inicio_label), "inicio_%d", inicio_n);
    snprintf(fin_label, sizeof(fin_label), "fin_%d", fin_n);

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
    char inicio_label[32], fin_label[32];
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
    snprintf(inicio_label, sizeof(inicio_label), "inicio_%d", inicio_n);
    snprintf(fin_label, sizeof(fin_label), "fin_%d", fin_n);

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
         * Usamos ltn(end, control) + xori #1 → end < control → control > end.
         * Pero queremos control >= end, que es equivalente a
         * !(control < end).
         *
         * Estrategia: calcular control < end en temp2, luego invertir.
         * ltn rTemp2, rTemp → temp2 = (control < end) ? 1 : 0
         * xori rTemp2, #1   → temp2 = !(control < end) = (control >= end)
         * jnz rTemp2, fin   → si control >= end, salir
         */
        emit_instruction(ctx, "ltn r%d, r%d", temp2, temp);
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
 * Stubs
 * ---------------------------------------------------------------- */

static void codegen_break(CodegenContext *ctx, const AstNode *stmt)
{
    (void)stmt;
    emit_comment(ctx, "break no implementado aun (punto 9)");
}

static void codegen_output(CodegenContext *ctx, const AstNode *stmt)
{
    (void)stmt;
    emit_comment(ctx, "output no implementado aun (punto 9)");
}

static void codegen_call_statement(CodegenContext *ctx, const AstNode *stmt)
{
    (void)stmt;
    emit_comment(ctx, "llamada como sentencia no implementada aun (punto 6)");
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
        codegen_statement_list(ctx, stmt->data.block.statements);
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
