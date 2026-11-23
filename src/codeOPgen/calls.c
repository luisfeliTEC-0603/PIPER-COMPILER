/** @file calls.c
 * PORYGON no tiene CALL/RET: cada llamada guarda un numero para saber donde
 * volver. Se reservan 32 bytes: numero en 0, r1..r6 en 4..24 y resultado en
 * 28. Los argumentos quinto y siguientes se guardan desde el byte 32.
 */
#include "internal.h"

static size_t record_bytes(size_t argument_count)
{
    return SYMBOL_CALL_RECORD_SIZE + (argument_count > 4 ? argument_count - 4 : 0) * SYMBOL_WORD_SIZE;
}

/* Preparacion de entrada (prologo): comprobar pila, reservar espacio para
 * parametros/locales/valores derramados y copiar los parametros recibidos. */
void op_prepare_function(CodeOPgenContext *context)
{
    const Symbol *function = op_source_routine(context)->symbol;
    size_t function_stack_bytes = op_function_stack_bytes(context);
    int call_reserve_bytes = function != NULL ? function->call_reserve_size
        : context->assigned->machine->source->symbols->global_call_reserve_size;
    /* El tamano ya incluye los slots de spill agregados por regalloc.
     * Comprobar ademas el espacio de futuras llamadas, incluso en recursion.
     * No tocar r1..r4: todavia contienen los primeros parametros. */
    op_move(context, 5, 7);
    op_emit(context, "diffi r5, #%zu", function_stack_bytes + (size_t)call_reserve_bytes);
    op_immediate(context, 6, OP_STACK_BOTTOM);
    op_emit(context, "ltn r5, r6");
    op_emit(context, "jnz r5, " OP_ERROR("pila"));
    if (function_stack_bytes != 0) op_emit(context, "diffi r7, #%zu", function_stack_bytes);
    if (function == NULL) return;
    for (int parameter_index = 0; parameter_index < function->param_count; parameter_index++) {
        const Symbol *parameter = function->params[parameter_index];
        PiperType type = parameter->is_array ? PIPER_TYPE_B32 : parameter->declared_type;
        unsigned register_number = parameter_index < 4 ? (unsigned)parameter_index + 1 : 5;
        if (parameter_index >= 4)
            op_load(context, PIPER_TYPE_B32, register_number,
                    (int)function_stack_bytes + SYMBOL_CALL_RECORD_SIZE +
                    (parameter_index - 4) * SYMBOL_WORD_SIZE, 7);
        op_store(context, type, register_number, parameter->address_or_offset, 7);
    }
}

/* Salida comun (epilogo): liberar el area de pila de esta funcion y recuperar
 * el numero de llamada. El resultado permanece en r1 al buscar donde volver. */
void op_emit_function_exit(CodeOPgenContext *context)
{
    op_label(context, "piper_salida_funcion_%zu", context->routine_index);
    if (op_function_stack_bytes(context) != 0) op_emit(context, "sumi r7, #%zu", op_function_stack_bytes(context));
    op_load(context, PIPER_TYPE_B32, 6, 0, 7);
    op_emit(context, "j piper_buscar_retorno");
}

void op_call(CodeOPgenContext *context, const RegallocMachineInstruction *operation)
{
    int call_storage_bytes = (int)record_bytes(operation->call_arg_count);
    if (operation->op == REGALLOC_M_CALL_SAVE) {
        for (unsigned register_number = 1; register_number <= 6; register_number++)
            op_store(context, PIPER_TYPE_B32, register_number, -call_storage_bytes + (int)register_number * 4, 7);
    } else if (operation->op == REGALLOC_M_CALL_ARG) {
        unsigned source_register = op_register(context, operation->use1);
        if (operation->call_arg_index < 4) op_move(context, (unsigned)operation->call_arg_index + 1, source_register);
        else op_store(context, PIPER_TYPE_B32, source_register,
                      -call_storage_bytes + SYMBOL_CALL_RECORD_SIZE + (int)(operation->call_arg_index - 4) * 4, 7);
    } else {
        /* Escribir el ID aqui (no en CALL_SAVE) evita destruir un argumento
         * que sigue materializado en r1. r5/r6 estan declarados como fijos. */
        op_immediate(context, 5, (int64_t)operation->call_site_id);
        op_store(context, PIPER_TYPE_B32, 5, -call_storage_bytes, 7);
        op_emit(context, "diffi r7, #%d", call_storage_bytes);
        op_emit(context, "j piper_funcion_%zu", op_find_routine(context, operation->symbol));
        op_label(context, "piper_volver_llamada_%zu", operation->call_site_id);
        op_store(context, PIPER_TYPE_B32, 1, 28, 7);
        for (unsigned register_number = 1; register_number <= 6; register_number++) op_load(context, PIPER_TYPE_B32, register_number, (int)register_number * 4, 7);
        if (operation->op == REGALLOC_M_CALL_RESULT)
            op_load(context, PIPER_TYPE_B32, op_register(context, operation->def), 28, 7);
        op_emit(context, "sumi r7, #%d", call_storage_bytes);
    }
}

void op_finish(CodeOPgenContext *context)
{
    op_label(context, "piper_buscar_retorno");
    op_emit(context, "jz r6, piper_fin_main");
    for (size_t return_id = 1; return_id <= context->assigned->machine->call_site_count; return_id++) {
        op_move(context, 5, 6);
        op_emit(context, "diffi r5, #%zu", return_id);
        op_emit(context, "jz r5, piper_volver_llamada_%zu", return_id);
    }
    op_label(context, OP_ERROR("retorno"));
    op_emit(context, "j " OP_ERROR("retorno"));
    op_label(context, "piper_fin_main");
    op_emit(context, "sumi r7, #%d", SYMBOL_CALL_RECORD_SIZE);
    op_label(context, "piper_fin_programa");
    op_emit(context, "j piper_fin_programa");
    const char *errors[] = {OP_ERROR("pila"), OP_ERROR("division_cero"),
        OP_ERROR("exponente_negativo"), OP_ERROR("aritmetico"), OP_ERROR("indice")};
    for (size_t error_index = 0; error_index < sizeof(errors) / sizeof(*errors); error_index++) {
        op_label(context, "%s", errors[error_index]);
        op_emit(context, "j %s", errors[error_index]);
    }
}
