#include "codegen_internal.h"

#include <stdio.h>

/* Prepara argumentos, transfiere el control y restaura el estado al regresar.
 * El registro r7 permanece en el frame del llamador mientras se evalúan los
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
    int id, reserved, record_size, nargs;
    char resume_label[CODEGEN_LABEL_SIZE];

    if (ctx == NULL || call == NULL || call->kind != AST_CALL_EXPRESSION)
        return 0;
    if (dest_reg < -1 || dest_reg > REG_TMP1 || dest_reg == REG_ZERO) {
        codegen_context_error(ctx, "registro de destino invalido en llamada");
        return 0;
    }

    /* Resolver la firma determina cuántos argumentos y bytes necesita la llamada. */
    function = lookup_symbol_or_error(ctx,
        call->data.call_expression.routine_name, SYMBOL_FUNC);
    if (function == NULL) return 0;
    args = call->data.call_expression.arguments;
    nargs = args != NULL ? (int)args->count : 0;
    if (nargs != function->param_count) {
        codegen_context_error(ctx,
            "llamada a '%s': se esperaban %d argumentos, se recibieron %d",
            function->name, function->param_count, nargs);
        return 0;
    }
    if (nargs > MAX_TOTAL_ARGS) {
        codegen_context_error(ctx,
            "llamada a '%s': maximo %d argumentos",
            function->name, MAX_TOTAL_ARGS);
        return 0;
    }
    record_size = CODEGEN_CALL_RECORD_SIZE(nargs);

    if (ctx->call_site_count >= 32767 ||
        ctx->pending_call_bytes > ctx->call_reserve_size - record_size) {
        codegen_context_error(ctx, "limite de llamadas anidadas excedido");
        return 0;
    }

    /* Reservar el call record y conservar los registros del llamador. */
    id = ++ctx->call_site_count;
    ctx->pending_call_bytes += record_size;
    reserved = ctx->pending_call_bytes;
    snprintf(resume_label, sizeof(resume_label),
             CODEGEN_INTERNAL_LABEL("retorno_%d"), id);

    for (int reg = REG_RET; reg <= REG_TMP1; reg++)
        emit_instruction(ctx, "stwi r%d, %d(r%d)", reg,
                         -reserved + reg * SYMBOL_WORD_SIZE, REG_FP);
    if (!load_immediate(ctx, REG_RET, id)) {
        ctx->pending_call_bytes -= record_size;
        return 0;
    }
    emit_instruction(ctx, "stwi r%d, %d(r%d)", REG_RET, -reserved, REG_FP);

    /* Cada argumento se evalúa en su registro o en su ranura del stack. */
    if (args != NULL) {
        for (size_t i = 0; i < args->count; i++) {
            const AstNode *arg = args->items[i];
            Symbol *param = function->params != NULL ? function->params[i] : NULL;
            int slot = (int)i;

            if (param == NULL || arg == NULL) {
                codegen_context_error(ctx, "firma incompleta de '%s'", function->name);
                ctx->pending_call_bytes -= record_size;
                return 0;
            }

            /* Los argumentos posteriores a r4 se guardan después del call record. */
            int stack_off = -record_size + CALL_RECORD_SIZE
                          + (slot - MAX_REGISTER_ARGS) * SYMBOL_WORD_SIZE;

            if (slot < MAX_REGISTER_ARGS) {
                int dst = REG_ARG0 + slot;
                if (param->is_array) {
                    Symbol *array;
                    if (arg->kind != AST_IDENTIFIER_EXPRESSION) {
                        codegen_context_error(ctx,
                            "argumento de arreglo no soportado en '%s'", function->name);
                        ctx->pending_call_bytes -= record_size;
                        return 0;
                    }
                    array = lookup_symbol_or_error(ctx, arg->data.identifier.name, -1);
                    if (array == NULL || !load_array_base(ctx, array, dst)) {
                        ctx->pending_call_bytes -= record_size;
                        return 0;
                    }
                } else if (codegen_expression(ctx, arg, dst) == TYPE_VOID) {
                    ctx->pending_call_bytes -= record_size;
                    return 0;
                }
            } else {
                /* El temporal conserva el argumento hasta escribirlo en el stack. */
                int temp = alloc_temp(ctx);
                if (temp < 0) {
                    ctx->pending_call_bytes -= record_size;
                    return 0;
                }
                if (param->is_array) {
                    Symbol *array;
                    if (arg->kind != AST_IDENTIFIER_EXPRESSION) {
                        codegen_context_error(ctx,
                            "argumento de arreglo no soportado en '%s'", function->name);
                        free_temp(ctx, temp);
                        ctx->pending_call_bytes -= record_size;
                        return 0;
                    }
                    array = lookup_symbol_or_error(ctx, arg->data.identifier.name, -1);
                    if (array == NULL || !load_array_base(ctx, array, temp)) {
                        free_temp(ctx, temp);
                        ctx->pending_call_bytes -= record_size;
                        return 0;
                    }
                } else if (codegen_expression(ctx, arg, temp) == TYPE_VOID) {
                    free_temp(ctx, temp);
                    ctx->pending_call_bytes -= record_size;
                    return 0;
                }
                emit_instruction(ctx, "stwi r%d, %d(r%d)", temp, stack_off, REG_FP);
                free_temp(ctx, temp);
            }
        }
    }

    /* Entrar a la rutina y, al regresar, restaurar el estado del llamador. */
    emit_instruction(ctx, "diffi r%d, #%d", REG_FP, reserved);
    emit_instruction(ctx, "j " CODEGEN_ROUTINE_LABEL("%s"), function->name);
    emit_label(ctx, resume_label);
    emit_instruction(ctx, "stwi r%d, 28(r%d)", REG_RET, REG_FP);
    for (int reg = REG_RET; reg <= REG_TMP1; reg++)
        emit_instruction(ctx, "lowi r%d, %d(r%d)", reg,
                         reg * SYMBOL_WORD_SIZE, REG_FP);
    if (dest_reg > 0)
        emit_instruction(ctx, "lowi r%d, 28(r%d)", dest_reg, REG_FP);
    emit_instruction(ctx, "sumi r%d, #%d", REG_FP, reserved);
    ctx->pending_call_bytes -= record_size;
    return 1;
}

/* Emite el despachador que conecta cada id de continuación con su punto de retorno. */
void codegen_emit_return_dispatch(CodegenContext *ctx)
{
    if (ctx == NULL) return;
    emit_blank(ctx);
    emit_label(ctx, CODEGEN_INTERNAL_LABEL("retornos"));
    emit_instruction(ctx, "jz r%d, " CODEGEN_INTERNAL_LABEL("fin_de_main"), REG_TMP1);
    for (int id = 1; id <= ctx->call_site_count; id++) {
        emit_instruction(ctx, "mov r%d, r%d", REG_TMP0, REG_TMP1);
        emit_instruction(ctx, "diffi r%d, #%d", REG_TMP0, id);
        emit_instruction(ctx, "jz r%d, " CODEGEN_INTERNAL_LABEL("retorno_%d"), REG_TMP0, id);
    }

    /* Los errores de ejecución terminan en ciclos de parada identificables. */
    emit_label(ctx, CODEGEN_INTERNAL_LABEL("error_retorno"));
    emit_instruction(ctx, "j " CODEGEN_INTERNAL_LABEL("error_retorno"));
    emit_label(ctx, CODEGEN_INTERNAL_LABEL("fin_de_main"));
    emit_instruction(ctx, "sumi r%d, #%d", REG_FP, CALL_RECORD_SIZE);
    emit_label(ctx, CODEGEN_INTERNAL_LABEL("fin_programa"));
    emit_instruction(ctx, "j " CODEGEN_INTERNAL_LABEL("fin_programa"));
    emit_label(ctx, CODEGEN_INTERNAL_LABEL("error_pila"));
    emit_instruction(ctx, "j " CODEGEN_INTERNAL_LABEL("error_pila"));
    emit_label(ctx, CODEGEN_INTERNAL_LABEL("error_division_cero"));
    emit_instruction(ctx, "j " CODEGEN_INTERNAL_LABEL("error_division_cero"));
    emit_label(ctx, CODEGEN_INTERNAL_LABEL("error_exponente_negativo"));
    emit_instruction(ctx, "j " CODEGEN_INTERNAL_LABEL("error_exponente_negativo"));
    emit_label(ctx, CODEGEN_INTERNAL_LABEL("error_aritmetico"));
    emit_instruction(ctx, "j " CODEGEN_INTERNAL_LABEL("error_aritmetico"));
    emit_label(ctx, CODEGEN_INTERNAL_LABEL("error_indice"));
    emit_instruction(ctx, "j " CODEGEN_INTERNAL_LABEL("error_indice"));
}
