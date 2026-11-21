/** @file memory.c
 * Direcciones ya resueltas por lowering: locales/spills relativos a r7;
 * globales absolutos. Los anchos de carga incluyen extension de signo.
 */
#include "internal.h"

void op_load(CodeOPgenContext *context, PiperType type, unsigned destination_register, int offset, unsigned base)
{
    const char *name = "lowi";
    if (type == PIPER_TYPE_B8) name = "lobi";
    if (type == PIPER_TYPE_UB8) name = "lobui";
    if (type == PIPER_TYPE_B16) name = "lohi";
    if (type == PIPER_TYPE_UB16) name = "lohui";
    op_emit(context, "%s r%u, %d(r%u)", name, destination_register, offset, base);
}

void op_store(CodeOPgenContext *context, PiperType type, unsigned source_register, int offset, unsigned base)
{
    unsigned bits = op_bits(type);
    const char *name = bits == 8 ? "stbi" : bits == 16 ? "sthi" : "stwi";
    op_emit(context, "%s r%u, %d(r%u)", name, source_register, offset, base);
}

void op_memory(CodeOPgenContext *context, const RegallocMachineInstruction *operation)
{
    unsigned destination_register = operation->def == IR_NO_ID ? 0 : op_register(context, operation->def);
    unsigned source_register = operation->use1 == IR_NO_ID ? 0 : op_register(context, operation->use1);
    bool local = (operation->fixed_reads & REGALLOC_PHYS_R7) != 0;
    switch (operation->op) {
    case REGALLOC_M_LOAD_SYMBOL:
        if (local) op_load(context, operation->type.piper, destination_register, (int)operation->immediate, 7);
        else {
            op_immediate(context, destination_register, operation->immediate);
            op_load(context, operation->type.piper, destination_register, 0, destination_register);
        }
        break;
    case REGALLOC_M_STORE_SYMBOL:
        if (local) op_store(context, operation->type.piper, source_register, (int)operation->immediate, 7);
        else {
            op_immediate(context, 5, operation->immediate);
            op_store(context, operation->type.piper, source_register, 0, 5);
        }
        break;
    case REGALLOC_M_ADDR:
        if (operation->symbol->symbol_class == SYMBOL_PARAM)
            op_load(context, PIPER_TYPE_B32, destination_register, (int)operation->immediate, 7);
        else {
            op_immediate(context, destination_register, operation->immediate);
            if (local) op_emit(context, "sum r%u, r7", destination_register);
        }
        break;
    case REGALLOC_M_LOAD_INDIRECT:
        op_load(context, operation->type.piper, destination_register, 0, source_register);
        break;
    case REGALLOC_M_STORE_INDIRECT:
        op_store(context, operation->type.piper, op_register(context, operation->use2), 0, source_register);
        break;
    case REGALLOC_M_SPILL_LOAD:
    case REGALLOC_M_SPILL_STORE: {
        int offset = (int)regalloc_assigned_spill_offset(context->assigned, context->routine_index, operation->slot);
        if (operation->op == REGALLOC_M_SPILL_LOAD) op_load(context, PIPER_TYPE_B32, destination_register, offset, 7);
        else op_store(context, PIPER_TYPE_B32, source_register, offset, 7);
        break;
    }
    case REGALLOC_M_CHECK_BOUNDS:
        op_move(context, 5, source_register);
        op_immediate(context, 6, operation->immediate);
        op_emit(context, "ltn r5, r6");
        op_emit(context, "jz r5, " OP_ERROR("indice"));
        break;
    default: op_fail(context, CODEOPGEN_INVALID_MACHINE, "Operacion de memoria desconocida.");
    }
}

void op_crypto(CodeOPgenContext *context, const RegallocMachineInstruction *operation)
{
    unsigned source_register = operation->use1 == IR_NO_ID ? 0 : op_register(context, operation->use1);
    switch (operation->op) {
    case REGALLOC_M_CRYPTO_WORD:
        op_move(context, 1, source_register);
        op_emit(context, "lopw r1, %" PRId64, operation->immediate);
        break;
    case REGALLOC_M_CRYPTO_AUTH: op_emit(context, "auth"); break;
    case REGALLOC_M_CRYPTO_REVOKE: op_emit(context, "rvk"); break;
    case REGALLOC_M_CRYPTO_WRITE:
        op_move(context, 1, source_register);
        op_emit(context, "write r1, %" PRId64 ", %" PRId64, operation->immediate, operation->immediate2);
        break;
    case REGALLOC_M_CRYPTO_FEISTEL:
        /* Conservar la direccion ANTES de tocar r1/r2: source_register puede ser uno de ellos. */
        op_move(context, 3, source_register);
        op_load(context, PIPER_TYPE_B32, 1, 0, 3);
        op_load(context, PIPER_TYPE_B32, 2, 4, 3);
        op_emit(context, "feistel r1, r2, %" PRId64 ", %" PRId64, operation->immediate, operation->immediate2);
        op_store(context, PIPER_TYPE_B32, 1, 0, 3);
        op_store(context, PIPER_TYPE_B32, 2, 4, 3);
        break;
    default: op_fail(context, CODEOPGEN_INVALID_MACHINE, "Operacion crypto desconocida.");
    }
}
