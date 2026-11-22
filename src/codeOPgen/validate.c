/** @file validate.c
 * Comprobaciones del emisor, ademas de registros asignados y sus conflictos:
 * firma de parametros y secuencia SAVE/ARG/CALL completa por bloque. Ningun
 * ID de retorno se puede duplicar u omitir. Se revisa antes de escribir ASM.
 */
#include "internal.h"
#include <stdlib.h>

void op_validate(CodeOPgenContext *context)
{
    size_t sites = context->assigned->machine->call_site_count;
    if (sites > 32767) {
        op_fail(context, CODEOPGEN_INVALID_MACHINE, "Demasiadas continuaciones de llamada.");
        return;
    }
    bool *seen = calloc(sites + 1, sizeof(*seen));
    if (seen == NULL) {
        op_fail(context, CODEOPGEN_OUT_OF_MEMORY, "Sin memoria para revisar continuaciones.");
        return;
    }
    size_t count = 0;
    for (size_t routine_index = 0; routine_index < context->assigned->routine_count; routine_index++) {
        context->routine_index = routine_index;
        const RegallocMachineRoutine *routine = &context->assigned->machine->routines[routine_index];
        const Symbol *function = op_source_routine(context)->symbol;
        if (function != NULL) {
            if (function->param_count < 0 || (unsigned)function->param_count > REGALLOC_MAX_CALL_ARGS ||
                (function->param_count != 0 && function->params == NULL)) {
                op_fail(context, CODEOPGEN_INVALID_MACHINE, "Firma de parametros incompleta.");
                break;
            }
            for (int instruction_index = 0; instruction_index < function->param_count; instruction_index++)
                if (function->params[instruction_index] == NULL || function->params[instruction_index]->address_or_offset < 0)
                    op_fail(context, CODEOPGEN_INVALID_MACHINE, "Parametro sin ranura local.");
        }
        for (size_t block_index = 0; block_index < routine->block_count; block_index++) {
            const RegallocMachineBlock *block = &routine->blocks[block_index];
            context->block_id = block->id;
            size_t active = 0, arguments = 0;
            uint32_t placed = 0;
            for (size_t instruction_index = 0; instruction_index < block->instruction_count; instruction_index++) {
                const RegallocMachineInstruction *operation = &block->instructions[instruction_index];
                context->instruction_index = instruction_index;
                context->location = operation->location;
                if ((operation->op == REGALLOC_M_ADDR || operation->op == REGALLOC_M_LOAD_SYMBOL ||
                     operation->op == REGALLOC_M_STORE_SYMBOL) && operation->symbol == NULL)
                    op_fail(context, CODEOPGEN_INVALID_MACHINE, "Acceso sin simbolo resuelto.");
                switch (operation->op) {
                case REGALLOC_M_CALL_SAVE:
                    if (active != 0 || operation->call_site_id == 0 || operation->call_site_id > sites ||
                        operation->call_arg_count > REGALLOC_MAX_CALL_ARGS)
                        op_fail(context, CODEOPGEN_INVALID_MACHINE, "Inicio de llamada invalido.");
                    active = operation->call_site_id;
                    arguments = operation->call_arg_count;
                    placed = 0;
                    break;
                case REGALLOC_M_CALL_ARG:
                    if (active == 0 || operation->call_site_id != active ||
                        operation->call_arg_count != arguments || operation->call_arg_index >= arguments ||
                        operation->call_arg_index >= 32 || (placed & (UINT32_C(1) << operation->call_arg_index)))
                        op_fail(context, CODEOPGEN_INVALID_MACHINE, "Argumento duplicado o ajeno a la llamada.");
                    else placed |= UINT32_C(1) << operation->call_arg_index;
                    break;
                case REGALLOC_M_CALL_RESULT: case REGALLOC_M_CALL_VOID: {
                    uint32_t expected = arguments == 32 ? UINT32_MAX : (UINT32_C(1) << arguments) - 1;
                    if (active == 0 || active > sites || operation->call_site_id != active ||
                        operation->call_arg_count != arguments || placed != expected ||
                        operation->symbol == NULL || operation->symbol->param_count != (int)arguments ||
                        op_find_routine(context, operation->symbol) == SIZE_MAX) {
                        op_fail(context, CODEOPGEN_INVALID_MACHINE, "Llamada sin argumentos o destino completos.");
                    } else {
                        if (seen[active]) op_fail(context, CODEOPGEN_INVALID_MACHINE, "Continuacion duplicada.");
                        seen[active] = true;
                        count++;
                    }
                    active = 0;
                    break;
                }
                default: break;
                }
                if (context->error.status != CODEOPGEN_OK) goto done;
            }
            if (active != 0) op_fail(context, CODEOPGEN_INVALID_MACHINE, "Llamada sin cerrar en el bloque.");
        }
    }
    if (count != sites) op_fail(context, CODEOPGEN_INVALID_MACHINE, "Faltan continuaciones declaradas.");
done:
    free(seen);
}
