/** @file emit.c
 * Escritura y primitivas comunes. No se oculta ningun registro de trabajo:
 * immediate solo modifica destination_register, y move puede omitirse si ambos son iguales.
 */
#include "internal.h"
#include <stdarg.h>

void op_fail(CodeOPgenContext *context, CodeOPgenResult status, const char *message)
{
    if (context->error.status != CODEOPGEN_OK) return;
    context->error = (CodeOPgenDiagnostic){.status = status, .routine = context->routine_index,
        .block = context->block_id, .instruction = context->instruction_index, .location = context->location};
    snprintf(context->error.message, sizeof(context->error.message), "%s", message);
}

static void write_line(CodeOPgenContext *context, const char *prefix, const char *format,
                        va_list arguments, const char *suffix)
{
    if (context->error.status != CODEOPGEN_OK) return;
    if (fputs(prefix, context->output) < 0 || vfprintf(context->output, format, arguments) < 0 ||
        fputs(suffix, context->output) < 0)
        op_fail(context, CODEOPGEN_IO_ERROR, "No se pudo escribir el ASM.");
}

void op_emit(CodeOPgenContext *context, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    write_line(context, "    ", format, arguments, "\n");
    va_end(arguments);
}

void op_label(CodeOPgenContext *context, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    write_line(context, "", format, arguments, ":\n");
    va_end(arguments);
}

void op_comment(CodeOPgenContext *context, const RegallocMachineInstruction *operation)
{
    if (context->comments)
        op_emit(context, "; rutina %zu B%zu instruccion %zu, op=%d, fuente %d:%d",
                context->routine_index, context->block_id, context->instruction_index, operation->op,
                operation->location.first_line, operation->location.first_column);
}

unsigned op_register(const CodeOPgenContext *context, IrId temporary_id)
{
    return context->assigned->routines[context->routine_index].register_for_temp[temporary_id];
}

void op_move(CodeOPgenContext *context, unsigned destination_register, unsigned source_register)
{
    if (destination_register != source_register) op_emit(context, "mov r%u, r%u", destination_register, source_register);
}

void op_immediate(CodeOPgenContext *context, unsigned destination_register, int64_t value)
{
    if (value < INT32_MIN || value > UINT32_MAX) {
        op_fail(context, CODEOPGEN_INVALID_MACHINE, "Inmediato fuera de 32 bits.");
        return;
    }
    /* sumi extiende con signo: dos mitades ajustadas representan tambien
     * uB32, sin registros auxiliares ni depender del signo de andi/ori. */
    uint32_t bits = (uint32_t)value;
    int64_t low = bits & 65535u;
    if (low >= 32768) low -= 65536;
    int64_t high = ((int64_t)bits - low) / 65536;
    if (high >= 32768) high -= 65536;
    op_move(context, destination_register, 0);
    if (high != 0) {
        op_emit(context, "sumi r%u, #%" PRId64, destination_register, high);
        op_emit(context, "shli r%u, #16", destination_register);
    }
    if (low != 0) op_emit(context, "sumi r%u, #%" PRId64, destination_register, low);
}

bool op_unsigned(PiperType type)
{
    return type == PIPER_TYPE_UB8 || type == PIPER_TYPE_UB16 || type == PIPER_TYPE_UB32;
}

unsigned op_bits(PiperType type)
{
    if (type == PIPER_TYPE_B8 || type == PIPER_TYPE_UB8) return 8;
    if (type == PIPER_TYPE_B16 || type == PIPER_TYPE_UB16) return 16;
    return 32;
}

const IrRoutine *op_source_routine(const CodeOPgenContext *context)
{
    const RegallocMachineRoutine *routine = &context->assigned->machine->routines[context->routine_index];
    return &context->assigned->machine->source->routines[routine->ir_routine];
}

size_t op_function_stack_bytes(const CodeOPgenContext *context)
{
    return context->assigned->routines[context->routine_index].frame_bytes;
}

size_t op_find_routine(const CodeOPgenContext *context, const Symbol *function)
{
    for (size_t routine_index = 0; routine_index < context->assigned->routine_count; routine_index++) {
        size_t source_routine_index = context->assigned->machine->routines[routine_index].ir_routine;
        if (context->assigned->machine->source->routines[source_routine_index].symbol == function) return routine_index;
    }
    return SIZE_MAX;
}
