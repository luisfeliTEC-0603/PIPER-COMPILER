/** @file ir_stmt.c
 * I3: bloques léxicos, declaraciones sin inicialización y asignaciones escalares.
 * Retornos, llamadas y control se incorporarán en I4/I5; arreglos en I7.
 */
#include "ir_internal.h"

/** Añade las instrucciones al bloque activo, sin modificar AST ni símbolos.
 * No inventa inicializaciones. Un camino cerrado no recibe más instrucciones. */
void ir_stmt(IrContext *ctx, const AstNode *statement)
{
    if (ctx == NULL || ctx->error.status != IR_OK || ctx->current_block == IR_NO_ID)
        return;
    if (statement == NULL) {
        ir_fail(ctx, IR_INVALID_INPUT, (SourceLocation){0}, "Falta la sentencia IR.");
        return;
    }
    switch (statement->kind) {
    case AST_BLOCK: {
        const AstNodeList *list = statement->data.block.statements;
        for (size_t i = 0; i < list->count && ctx->error.status == IR_OK &&
             ctx->current_block != IR_NO_ID; i++)
            ir_stmt(ctx, list->items[i]);
        return;
    }
    case AST_VARIABLE_DECLARATION:
        return; /* El almacenamiento ya está descrito en la tabla semántica. */
    case AST_ASSIGNMENT: {
        /* El destino se resuelve antes del origen; I7 añadirá sus índices. */
        IrOperand target = ir_target(ctx, statement->data.assignment.target);
        if (ctx->error.status != IR_OK) return;
        IrOperand value = ir_expr(ctx, statement->data.assignment.value);
        if (ctx->error.status != IR_OK) return;
        if (value.kind == IR_OPERAND_NONE) {
            ir_fail(ctx, IR_INVALID_INPUT, statement->location, "No se puede asignar un valor none.");
            return;
        }
        /* Un literal adopta el tipo de su uso aprobado. Un temporal conserva
         * el tipo del cálculo: asignar a B8 no convierte su suma previa a B8. */
        if (value.kind == IR_OPERAND_CONSTANT) value.type = target.type;
        ir_emit(ctx, (IrInstruction){.op = IR_OP_COPY, .arg1 = value,
            .result = target, .location = statement->location,
            .flags = value.type.piper != target.type.piper ? IR_CHECKED | IR_MAY_TRAP : 0});
        return;
    }
    case AST_INTEGER_LITERAL: case AST_BOOLEAN_LITERAL: case AST_NONE_LITERAL:
    case AST_IDENTIFIER_EXPRESSION: case AST_UNARY_EXPRESSION: case AST_BINARY_EXPRESSION:
        (void)ir_expr(ctx, statement); /* Expresión usada como sentencia, incluido ++/--. */
        return;
    default:
        ir_fail(ctx, IR_UNSUPPORTED, statement->location, "Sentencia pendiente de una etapa posterior a I3.");
        return;
    }
}
