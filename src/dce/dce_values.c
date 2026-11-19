#include "dce_values.h"
#include <stdlib.h>

/* La primera funcion encontrada al subir por los ambitos es la propietaria.
 * Esto distingue nombres sombreados y evita incluir locales de rutinas anidadas.
 */
static bool scope_belongs_to_routine(const IrProgram *ir, const Scope *scope,
                                      size_t routine_index)
{
    for (const Scope *ancestor = scope; ancestor != NULL; ancestor = ancestor->parent) {
        for (size_t r = 0; r < ir->routine_count; r++) {
            const Symbol *function = ir->routines[r].symbol;
            if (function != NULL && function->function_scope == ancestor)
                return r == routine_index;
        }
    }
    return false;
}

static bool is_scalar_variable(const Symbol *symbol)
{
    return (symbol->symbol_class == SYMBOL_VAR || symbol->symbol_class == SYMBOL_PARAM) &&
           !symbol->is_array && symbol->declared_type != PIPER_TYPE_NONE;
}

/* Orden por identidad, nunca por nombre. uintptr_t evita comparar directamente
 * punteros a objetos distintos; el orden solo se utiliza dentro de este mapa.
 */
static int compare_symbols(const void *left, const void *right)
{
    uintptr_t a = (uintptr_t)((const DceLocalValue *)left)->symbol;
    uintptr_t b = (uintptr_t)((const DceLocalValue *)right)->symbol;
    return (a > b) - (a < b);
}

static size_t find_local(const DceValueMap *values, const Symbol *symbol)
{
    uintptr_t identity = (uintptr_t)symbol;
    size_t first = 0, end = values->local_count;
    while (first < end) {
        size_t middle = first + (end - first) / 2;
        uintptr_t candidate = (uintptr_t)values->locals[middle].symbol;
        if (identity == candidate)
            return middle;
        if (identity < candidate)
            end = middle;
        else
            first = middle + 1;
    }
    return IR_NO_ID;
}

static void exclude_exposed_operand(DceValueMap *values, IrOperand operand,
                                     bool exposed_context)
{
    if (operand.kind != IR_OPERAND_SYMBOL || (!exposed_context && !operand.type.address))
        return;
    size_t local = find_local(values, operand.value.symbol);
    if (local != IR_NO_ID)
        values->locals[local].exposed = true;
}

static void exclude_exposed_locals(DceValueMap *values, const IrProgram *ir,
                                    size_t routine_index)
{
    for (size_t r = 0; r < ir->routine_count; r++) {
        const IrRoutine *routine = &ir->routines[r];
        bool other_routine = r != routine_index;
        for (size_t b = 0; b < routine->block_count; b++) {
            const IrBlock *block = &routine->blocks[b];
            for (size_t i = 0; i < block->instruction_count; i++) {
                const IrInstruction *instruction = &block->instructions[i];
                exclude_exposed_operand(values, instruction->arg1,
                                         other_routine || instruction->op == IR_OP_ADDR);
                exclude_exposed_operand(values, instruction->arg2, other_routine);
                exclude_exposed_operand(values, instruction->result, other_routine);
                if (instruction->op == IR_OP_CALL || instruction->op == IR_OP_INTRINSIC) {
                    for (size_t a = 0; a < instruction->extra.call.argument_count; a++)
                        exclude_exposed_operand(values, instruction->extra.call.arguments[a],
                                                 other_routine);
                }
            }
        }
    }

    size_t kept_count = 0;
    for (size_t local = 0; local < values->local_count; local++) {
        if (!values->locals[local].exposed)
            values->locals[kept_count++] = values->locals[local];
    }
    values->local_count = kept_count;
}

bool dce_values_build(const IrProgram *ir, size_t routine_index, DceValueMap *out)
{
    DceValueMap values = {.temp_count = ir->routines[routine_index].temp_count};
    if (ir->routines[routine_index].symbol == NULL) {
        *out = values;       /* El arranque sintetico no posee variables locales. */
        return true;
    }

    for (const Scope *scope = ir->symbols->all_scopes; scope != NULL; scope = scope->next_all) {
        if (!scope_belongs_to_routine(ir, scope, routine_index))
            continue;
        for (const Symbol *symbol = scope->symbols_head; symbol != NULL; symbol = symbol->next) {
            if (!is_scalar_variable(symbol))
                continue;
            if (values.local_count == SIZE_MAX - values.temp_count)
                return false;
            values.local_count++;
        }
    }
    if (values.local_count == 0) {
        *out = values;
        return true;
    }

    values.locals = calloc(values.local_count, sizeof(*values.locals));
    if (values.locals == NULL)
        return false;
    size_t next_local = 0;
    for (const Scope *scope = ir->symbols->all_scopes; scope != NULL; scope = scope->next_all) {
        if (!scope_belongs_to_routine(ir, scope, routine_index))
            continue;
        for (const Symbol *symbol = scope->symbols_head; symbol != NULL; symbol = symbol->next) {
            if (is_scalar_variable(symbol))
                values.locals[next_local++].symbol = symbol;
        }
    }
    qsort(values.locals, values.local_count, sizeof(*values.locals), compare_symbols);
    exclude_exposed_locals(&values, ir, routine_index);
    *out = values;
    return true;
}

size_t dce_value_id(const DceValueMap *values, IrOperand operand)
{
    if (operand.kind == IR_OPERAND_TEMP)
        return operand.value.temp;
    if (operand.kind != IR_OPERAND_SYMBOL || operand.type.address)
        return IR_NO_ID;
    size_t local = find_local(values, operand.value.symbol);
    return local == IR_NO_ID ? IR_NO_ID : values->temp_count + local;
}

void dce_values_free(DceValueMap *values)
{
    free(values->locals);
    *values = (DceValueMap){0};
}
