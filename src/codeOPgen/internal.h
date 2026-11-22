#ifndef PIPER_CODEOPGEN_INTERNAL_H
#define PIPER_CODEOPGEN_INTERNAL_H

/** Reglas internas: regalloc elige registros r1..r4; r5/r6 son auxiliares.
 * r7 apunta al area de pila de la funcion y r0 contiene cero. Las funciones
 * auxiliares reciben registros fisicos, nunca expresiones del fuente.
 * Entradas, bloques y salidas de funcion tienen etiquetas distintas.
 */
#include "codeOPgen.h"
#include <inttypes.h>

#define OP_ERROR(name) "piper_error_" name
#define OP_STACK_TOP (SYMBOL_DATA_BASE_ADDRESS + SYMBOL_MEMORY_SIZE)
#define OP_STACK_BOTTOM (OP_STACK_TOP - SYMBOL_STACK_SIZE)

typedef struct {
    const RegallocAssignedProgram *assigned;
    FILE *output;
    CodeOPgenDiagnostic error;
    size_t routine_index;     /**< Funcion que estamos escribiendo. */
    size_t instruction_index; /**< Posicion dentro del bloque actual. */
    size_t next_label_id;     /**< Evita repetir etiquetas de calculos internos. */
    IrId block_id;
    SourceLocation location;
    bool comments;
} CodeOPgenContext;

void op_fail(CodeOPgenContext *context, CodeOPgenResult status, const char *message);
void op_emit(CodeOPgenContext *context, const char *format, ...);
void op_label(CodeOPgenContext *context, const char *format, ...);
void op_comment(CodeOPgenContext *context, const RegallocMachineInstruction *operation);
void op_move(CodeOPgenContext *context, unsigned destination_register, unsigned source_register);
void op_immediate(CodeOPgenContext *context, unsigned destination_register, int64_t value);
unsigned op_register(const CodeOPgenContext *context, IrId temporary_id);
bool op_unsigned(PiperType type);
unsigned op_bits(PiperType type);
const IrRoutine *op_source_routine(const CodeOPgenContext *context);
size_t op_function_stack_bytes(const CodeOPgenContext *context);
size_t op_find_routine(const CodeOPgenContext *context, const Symbol *function);

void op_load(CodeOPgenContext *context, PiperType type, unsigned destination_register, int offset, unsigned base);
void op_store(CodeOPgenContext *context, PiperType type, unsigned source_register, int offset, unsigned base);
void op_memory(CodeOPgenContext *context, const RegallocMachineInstruction *operation);
void op_crypto(CodeOPgenContext *context, const RegallocMachineInstruction *operation);
void op_compare(CodeOPgenContext *context, const RegallocMachineInstruction *operation);
void op_checked(CodeOPgenContext *context, const RegallocMachineInstruction *operation);
void op_check_width(CodeOPgenContext *context, unsigned register_number, bool source_unsigned, PiperType type);
void op_checked_multiply(CodeOPgenContext *context, unsigned destination_register, unsigned right_register, PiperType type);
void op_general_arithmetic(CodeOPgenContext *context, const RegallocMachineInstruction *operation);
void op_prepare_function(CodeOPgenContext *context);
void op_emit_function_exit(CodeOPgenContext *context);
void op_call(CodeOPgenContext *context, const RegallocMachineInstruction *operation);
void op_finish(CodeOPgenContext *context);
void op_validate(CodeOPgenContext *context);
#endif
