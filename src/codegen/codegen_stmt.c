#include "codegen_internal.h"

#include <string.h>

/* ----------------------------------------------------------------
 * Almacenamiento en un lvalue escalar
 * ---------------------------------------------------------------- */

/*
 * Emite el store de `src_reg` en el símbolo `sym`.
 * Decide entre global y local/param según el scope dueño.
 */
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

/* ----------------------------------------------------------------
 * AST_ASSIGNMENT
 *
 * Formas soportadas en el punto 2:
 *   - target es AST_IDENTIFIER_EXPRESSION (variable escalar)
 *   - value es cualquier expresión evaluable
 *
 * Formas pendientes:
 *   - target es AST_INDEX_EXPRESSION (se maneja en punto 5)
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

    /* Caso 1: destino es un identificador simple. */
    if (target->kind == AST_IDENTIFIER_EXPRESSION) {
        sym = lookup_symbol_or_error(ctx, target->data.identifier.name, -1);
        if (sym == NULL) {
            return;
        }
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
    }
    /* Caso 2: destino es una indexación (pendiente punto 5). */
    else if (target->kind == AST_INDEX_EXPRESSION) {
        codegen_context_error(ctx,
            "asignacion a indexacion aun no implementada (punto 5)");
        return;
    }
    else {
        codegen_context_error(ctx,
            "destino de asignacion no soportado (kind=%d)", target->kind);
        return;
    }

    /* Evaluar el valor en un temporal. */
    temp = alloc_temp(ctx);
    if (temp < 0) {
        return;
    }

    if (codegen_expression(ctx, value, temp) == TYPE_VOID) {
        free_temp(ctx, temp);
        return;
    }

    /* Almacenar el resultado en el símbolo. */
    store_to_symbol(ctx, sym, temp);
    free_temp(ctx, temp);

    emit_comment(ctx, "%s = <expresion>", sym->name);
}

/* ----------------------------------------------------------------
 * AST_VARIABLE_DECLARATION
 *
 * En el punto 2, las variables escalares ya tienen su espacio reservado
 * por el prólogo (a través de total_stack_frame_size). No hay que
 * emitir código para declararlas.
 *
 * Los inicializadores se manejan como AST_ASSIGNMENT separados en el AST.
 * Los arreglos con inicializador se manejan en el punto 5.
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
        /* Es arreglo: pendiente punto 5. */
        emit_comment(ctx, "declaracion de arreglo '%s' (pendiente punto 5)",
                     name);
        return;
    }

    /* Variable escalar: no hay código que emitir. */
    emit_comment(ctx, "declaracion escalar '%s' (sin codigo)", name);
}

/* ----------------------------------------------------------------
 * Stubs para los casos que se implementan en puntos posteriores
 * ---------------------------------------------------------------- */

static void codegen_when(CodegenContext *ctx, const AstNode *stmt)
{
    (void)stmt;
    emit_comment(ctx, "when no implementado aun (punto 4)");
}

static void codegen_alwhen(CodegenContext *ctx, const AstNode *stmt)
{
    (void)stmt;
    emit_comment(ctx, "alwhen no implementado aun (punto 4)");
}

static void codegen_loop(CodegenContext *ctx, const AstNode *stmt)
{
    (void)stmt;
    emit_comment(ctx, "loop no implementado aun (punto 4)");
}

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
