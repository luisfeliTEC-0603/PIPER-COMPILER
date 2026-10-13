#include "codegen_internal.h"

#include <stdio.h>

/* ----------------------------------------------------------------
 * Llamadas con identificador de continuación
 * ---------------------------------------------------------------- */

/* El registro r7 permanece en el frame del llamador mientras se evalúan los
 * argumentos. Los registros se guardan debajo de ese frame; una llamada
 * anidada usa una ranura más profunda. Al saltar, r7 baja hasta la ranura
 * actual y el llamado construye su propio frame debajo de ella.
 *
 * Ranura de 32 bytes: id (0), r1..r6 (4..24), retorno (28).
 */
int codegen_call(CodegenContext *ctx, const AstNode *call, int dest_reg)
{
    const AstNodeList *args;
    Symbol *function;
    int id, reserved;
    char resume_label[40];

    if (ctx == NULL || call == NULL || call->kind != AST_CALL_EXPRESSION)
        return 0;
    if (dest_reg < -1 || dest_reg > REG_TMP1 || dest_reg == REG_ZERO) {
        codegen_context_error(ctx, "registro de destino invalido en llamada");
        return 0;
    }
    function = lookup_symbol_or_error(ctx,
        call->data.call_expression.routine_name, SYMBOL_FUNC);
    if (function == NULL) return 0;
    args = call->data.call_expression.arguments;
    if (function->param_count > MAX_ARGUMENTOS ||
        (args != NULL ? (int)args->count : 0) != function->param_count) {
        codegen_context_error(ctx,
            "llamada a '%s': solo se soportan hasta %d argumentos",
            function->name, MAX_ARGUMENTOS);
        return 0;
    }
    if (ctx->call_site_count >= 32767 ||
        ctx->pending_call_bytes >
            SYMBOL_STACK_CALL_RESERVE - CALL_RECORD_SIZE) {
        codegen_context_error(ctx, "limite de llamadas anidadas excedido");
        return 0;
    }

    id = ++ctx->call_site_count;
    ctx->pending_call_bytes += CALL_RECORD_SIZE;
    reserved = ctx->pending_call_bytes;
    snprintf(resume_label, sizeof(resume_label), "retorno_%d", id);

    for (int reg = REG_RET; reg <= REG_TMP1; reg++)
        emit_instruction(ctx, "stwi r%d, %d(r%d)", reg,
                         -reserved + reg * SYMBOL_WORD_SIZE, REG_FP);
    if (!load_immediate(ctx, REG_RET, id)) {
        ctx->pending_call_bytes -= CALL_RECORD_SIZE;
        return 0;
    }
    emit_instruction(ctx, "stwi r%d, %d(r%d)", REG_RET, -reserved, REG_FP);

    if (args != NULL) {
        for (size_t i = 0; i < args->count; i++) {
            const AstNode *arg = args->items[i];
            Symbol *param = function->params != NULL ? function->params[i] : NULL;
            int arg_reg = REG_ARG0 + (int)i;

            if (param == NULL || arg == NULL) {
                codegen_context_error(ctx, "firma incompleta de '%s'", function->name);
                ctx->pending_call_bytes -= CALL_RECORD_SIZE;
                return 0;
            }
            if (param->is_array) {
                Symbol *array;
                if (arg->kind != AST_IDENTIFIER_EXPRESSION) {
                    codegen_context_error(ctx,
                        "argumento de arreglo no soportado en '%s'", function->name);
                    ctx->pending_call_bytes -= CALL_RECORD_SIZE;
                    return 0;
                }
                array = lookup_symbol_or_error(ctx, arg->data.identifier.name, -1);
                if (array == NULL || !load_array_base(ctx, array, arg_reg)) {
                    ctx->pending_call_bytes -= CALL_RECORD_SIZE;
                    return 0;
                }
            } else if (codegen_expression(ctx, arg, arg_reg) == TYPE_VOID) {
                ctx->pending_call_bytes -= CALL_RECORD_SIZE;
                return 0;
            }
        }
    }

    emit_instruction(ctx, "diffi r%d, #%d", REG_FP, reserved);
    emit_instruction(ctx, "j %s", function->name);
    emit_label(ctx, resume_label);
    emit_instruction(ctx, "stwi r%d, 28(r%d)", REG_RET, REG_FP);
    for (int reg = REG_RET; reg <= REG_TMP1; reg++)
        emit_instruction(ctx, "lowi r%d, %d(r%d)", reg,
                         reg * SYMBOL_WORD_SIZE, REG_FP);
    if (dest_reg > 0)
        emit_instruction(ctx, "lowi r%d, 28(r%d)", dest_reg, REG_FP);
    emit_instruction(ctx, "sumi r%d, #%d", REG_FP, reserved);
    ctx->pending_call_bytes -= CALL_RECORD_SIZE;
    return 1;
}

/* ----------------------------------------------------------------
 * Despacho de retornos
 * ---------------------------------------------------------------- */

void codegen_emit_return_dispatch(CodegenContext *ctx)
{
    if (ctx == NULL) return;
    emit_blank(ctx);
    emit_label(ctx, "retornos");
    emit_instruction(ctx, "jz r%d, fin_de_main", REG_TMP1);
    for (int id = 1; id <= ctx->call_site_count; id++) {
        emit_instruction(ctx, "mov r%d, r%d", REG_TMP0, REG_TMP1);
        emit_instruction(ctx, "diffi r%d, #%d", REG_TMP0, id);
        emit_instruction(ctx, "jz r%d, retorno_%d", REG_TMP0, id);
    }
    emit_label(ctx, "error_retorno");
    emit_instruction(ctx, "j error_retorno");
    emit_label(ctx, "fin_de_main");
    emit_instruction(ctx, "sumi r%d, #%d", REG_FP, CALL_RECORD_SIZE);
    emit_label(ctx, "fin_programa");
    emit_instruction(ctx, "j fin_programa");
    emit_label(ctx, "error_pila");
    emit_instruction(ctx, "j error_pila");
}
