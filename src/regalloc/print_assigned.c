/** @file print_assigned.c
 * Vista de estudio del resultado que regalloc entrega a codeOPgen.
 * Consulta la asignacion terminada; no vuelve a colorear ni inserta spills.
 */
#include "regalloc_machine.h"
#include <inttypes.h>

static const char *operation_name(RegallocMachineOp operation)
{
    /* Indices designados: agregar un opcode no desplaza los nombres restantes. */
#define NAME(name) [REGALLOC_M_##name] = #name
    static const char *const names[] = {
        NAME(IMM), NAME(LOAD_SYMBOL), NAME(STORE_SYMBOL), NAME(MOVE),
        NAME(CHECKED_COPY), NAME(CHECKED_NEG), NAME(ADD), NAME(SUB), NAME(MUL),
        NAME(CHECKED_ADD), NAME(CHECKED_SUB), NAME(CHECKED_MUL), NAME(DIV),
        NAME(MOD), NAME(POW), NAME(ADDI), NAME(SUBI), NAME(AND), NAME(OR),
        NAME(XOR), NAME(EQ), NAME(LT), NAME(GT), NAME(BOOL_NOT), NAME(ADDR),
        NAME(CHECK_BOUNDS), NAME(LOAD_INDIRECT), NAME(STORE_INDIRECT),
        NAME(SPILL_LOAD), NAME(SPILL_STORE), NAME(CALL_SAVE), NAME(CALL_ARG),
        NAME(CALL_VOID), NAME(CALL_RESULT), NAME(CRYPTO_WORD), NAME(CRYPTO_AUTH),
        NAME(CRYPTO_WRITE), NAME(CRYPTO_FEISTEL), NAME(CRYPTO_REVOKE),
        NAME(JUMP), NAME(BRANCH), NAME(TO_RETURN), NAME(RETURN)
    };
#undef NAME
    size_t index = (size_t)operation;
    return index < sizeof(names) / sizeof(*names) && names[index] ? names[index] : "UNKNOWN";
}

static void write_operand(FILE *output, const char *role, IrId temp,
                          const RegallocAssignedRoutine *routine)
{
    if (temp != IR_NO_ID)
        fprintf(output, " %s=t%zu(r%u)", role, temp, routine->register_for_temp[temp]);
}

RegallocStatus regalloc_allocation_write_assigned(FILE *output,
                                                 const RegallocAllocation *allocation)
{
    if (!output || !allocation) return REGALLOC_INVALID_INPUT;
    if (regalloc_allocation_write_report(output, allocation) != REGALLOC_OK) return REGALLOC_IO_ERROR;
    const RegallocAssignedProgram *assigned = regalloc_allocation_view(allocation);
    fputs("\nMaquina asignada: entrada de codeOPgen, despues de insertar spills.\n"
          "def=destino, use1/use2=operandos; tN(rM)=temporal N en registro M.\n", output);
    for (size_t r = 0; r < assigned->routine_count; r++) {
        const RegallocMachineRoutine *machine = &assigned->machine->routines[r];
        const IrRoutine *source = &assigned->machine->source->routines[machine->ir_routine];
        const RegallocAssignedRoutine *routine = &assigned->routines[r];
        fprintf(output, "\nrutina %zu: %s, pila=%zu bytes\n", r,
                source->symbol ? source->symbol->name : "inicio global", routine->frame_bytes);
        for (size_t b = 0; b < machine->block_count; b++) {
            const RegallocMachineBlock *block = &machine->blocks[b];
            fprintf(output, "B%zu:\n", block->id);
            for (size_t i = 0; i < block->instruction_count; i++) {
                const RegallocMachineInstruction *operation = &block->instructions[i];
                fprintf(output, "  %zu: %s", i, operation_name(operation->op));
                write_operand(output, "def", operation->def, routine);
                write_operand(output, "use1", operation->use1, routine);
                if (operation->op == REGALLOC_M_STORE_INDIRECT)
                    write_operand(output, "use2", operation->use2, routine);
                if (operation->symbol) fprintf(output, " simbolo=%s", operation->symbol->name);
                if (operation->op == REGALLOC_M_SPILL_LOAD || operation->op == REGALLOC_M_SPILL_STORE)
                    fprintf(output, " slot=%zu offset=%zu(r7)", operation->slot,
                            regalloc_assigned_spill_offset(assigned, r, operation->slot));
                if (operation->op == REGALLOC_M_JUMP || operation->op == REGALLOC_M_BRANCH)
                    fprintf(output, " destino=B%zu", operation->targets[0]);
                if (operation->op == REGALLOC_M_BRANCH) fprintf(output, " alterno=B%zu", operation->targets[1]);
                if (operation->op == REGALLOC_M_CALL_ARG)
                    fprintf(output, " argumento=%zu", operation->call_arg_index);
                fprintf(output, " inmediato=%" PRId64 " inmediato2=%" PRId64
                        " ; origen IR=%zu fuente=%d:%d\n", operation->immediate,
                        operation->immediate2, operation->origin_instruction,
                        operation->location.first_line, operation->location.first_column);
            }
        }
    }
    return ferror(output) ? REGALLOC_IO_ERROR : REGALLOC_OK;
}
