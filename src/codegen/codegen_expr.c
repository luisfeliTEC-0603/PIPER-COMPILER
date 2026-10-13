#include "codegen_internal.h"

/*
 * Evalúa una expresión y deja el resultado en dest_reg.
 *
 * Retorna el tipo inferido de la expresión (TYPE_INT, TYPE_BOOL, etc.),
 * o TYPE_VOID si hubo error.
 */

static int codegen_integer_literal(CodegenContext *ctx,
                                   const AstNode *expr, int dest_reg)
{
    long long value = (long long)expr->data.integer_literal.value;
    if (!load_immediate(ctx, dest_reg, value)) {
        return TYPE_VOID;
    }
    return TYPE_INT;
}

static int codegen_boolean_literal(CodegenContext *ctx,
                                   const AstNode *expr, int dest_reg)
{
    long long value = expr->data.boolean_literal.value ? 1 : 0;
    if (!load_immediate(ctx, dest_reg, value)) {
        return TYPE_VOID;
    }
    return TYPE_BOOL;
}

static int codegen_identifier_load(CodegenContext *ctx,
                                   const AstNode *expr, int dest_reg)
{
    const char *name = expr->data.identifier.name;
    Symbol *sym = lookup_symbol_or_error(ctx, name, -1);
    Scope *owner;

    if (sym == NULL) {
        return TYPE_VOID;
    }

    if (sym->symbol_class == SYMBOL_FUNC) {
        codegen_context_error(ctx,
            "'%s' es una funcion; se requiere una llamada", name);
        return TYPE_VOID;
    }

    if (sym->is_array || sym->type == TYPE_ARRAY) {
        codegen_context_error(ctx,
            "'%s' es un arreglo; se requiere indexacion", name);
        return TYPE_VOID;
    }

    if (sym->address_or_offset < 0) {
        codegen_context_error(ctx,
            "'%s' no tiene direccion asignada", name);
        return TYPE_VOID;
    }

    owner = find_owner_scope(ctx, sym);
    if (owner == NULL) {
        codegen_context_error(ctx,
            "no se pudo determinar el ambito de '%s'", name);
        return TYPE_VOID;
    }

    if (owner->is_global) {
        emit_instruction(ctx, "lowi r%d, %d(r%d)",
                         dest_reg, sym->address_or_offset, REG_ZERO);
    } else {
        emit_instruction(ctx, "lowi r%d, %d(r%d)",
                         dest_reg, sym->address_or_offset, REG_FP);
    }

    return sym->type;
}

/*
 * Negación unaria: evalúa el operando y calcula 0 - valor.
 * Para literales enteros negativos, el parser produce
 * AST_UNARY_EXPRESSION(NEGATE, AST_INTEGER_LITERAL(N)).
 * En ese caso se puede cargar directamente -N si cabe en el rango.
 */
static int codegen_unary_negate(CodegenContext *ctx,
                                const AstNode *expr, int dest_reg)
{
    const AstNode *operand = expr->data.unary_expression.operand;

    /* Optimización: si el operando es un literal entero, cargar el
     * negativo directamente cuando cabe en 16 bits con signo. */
    if (operand != NULL && operand->kind == AST_INTEGER_LITERAL) {
        long long v = (long long)operand->data.integer_literal.value;
        if (v <= 32768) {   /* -32768 es representable */
            if (!load_immediate(ctx, dest_reg, -v)) {
                return TYPE_VOID;
            }
            return TYPE_INT;
        }
        /* Si v > 32768, no cabe; caer al camino general. */
    }

    if (codegen_expression(ctx, operand, dest_reg) == TYPE_VOID) {
        return TYPE_VOID;
    }
    emit_instruction(ctx, "diff r%d, r%d", dest_reg, REG_ZERO);
    return TYPE_INT;
}

int codegen_expression(CodegenContext *ctx, const AstNode *expr, int dest_reg)
{
    if (ctx == NULL || expr == NULL) {
        return TYPE_VOID;
    }

    switch (expr->kind) {
    case AST_INTEGER_LITERAL:
        return codegen_integer_literal(ctx, expr, dest_reg);
    case AST_BOOLEAN_LITERAL:
        return codegen_boolean_literal(ctx, expr, dest_reg);
    case AST_IDENTIFIER_EXPRESSION:
        return codegen_identifier_load(ctx, expr, dest_reg);
    case AST_UNARY_EXPRESSION:
        if (expr->data.unary_expression.operator == AST_OP_NEGATE) {
            return codegen_unary_negate(ctx, expr, dest_reg);
        }
        codegen_context_error(ctx,
            "operador unario aun no implementado (op=%d)",
            expr->data.unary_expression.operator);
        return TYPE_VOID;
    default:
        codegen_context_error(ctx,
            "codegen_expression: nodo no implementado aun (kind=%d)",
            expr->kind);
        return TYPE_VOID;
    }
}
