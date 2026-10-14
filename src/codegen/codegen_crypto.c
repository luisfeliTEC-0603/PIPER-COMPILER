#include "codegen_internal.h"

#include <string.h>

int codegen_is_intrinsic(const char *name)
{
    if (name == NULL) {
        return 0;
    }
    return strcmp(name, "crypto_auth") == 0
        || strcmp(name, "crypto_write_key") == 0
        || strcmp(name, "crypto_feistel") == 0
        || strcmp(name, "crypto_revoke") == 0;
}

void codegen_intrinsic(CodegenContext *ctx, const AstNode *call, int dest_reg)
{
    (void)dest_reg;
    if (ctx == NULL || call == NULL) return;
    const char *name = call->data.call_expression.routine_name;
    const AstNodeList *args = call->data.call_expression.arguments;
    /* La semántica valida firma e inmediatos; r1..r4 están libres entre sentencias. */
    if (!strcmp(name, "crypto_auth")) {
        for (size_t i = 0; i < 4; i++) {
            if (codegen_expression(ctx, args->items[i], REG_RET) == TYPE_VOID) return;
            emit_instruction(ctx, "lopw r1, %zu", i);
        }
        emit_instruction(ctx, "auth");
    } else if (!strcmp(name, "crypto_revoke")) {
        emit_instruction(ctx, "rvk");
    } else if (!strcmp(name, "crypto_write_key")) {
        if (codegen_expression(ctx, args->items[0], REG_RET) == TYPE_VOID) return;
        emit_instruction(ctx, "write r1, %llu, %llu",
                         args->items[1]->data.integer_literal.value,
                         args->items[2]->data.integer_literal.value);
    } else if (!strcmp(name, "crypto_feistel")) {
        Symbol *block = lookup_symbol_or_error(ctx, args->items[0]->data.identifier.name, -1);
        if (block == NULL || !load_array_base(ctx, block, REG_ARG2)) return;
        emit_instruction(ctx, "lowi r1, 0(r3)");
        emit_instruction(ctx, "lowi r2, 4(r3)");
        emit_instruction(ctx, "feistel r1, r2, %llu, %llu",
                         args->items[1]->data.integer_literal.value,
                         args->items[2]->data.integer_literal.value);
        emit_instruction(ctx, "stwi r1, 0(r3)");
        emit_instruction(ctx, "stwi r2, 4(r3)");
    }
}
