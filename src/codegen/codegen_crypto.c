#include "codegen_internal.h"

/*
 * WIP
 */

#include <string.h>

int codegen_is_intrinsic(const char *name)
{
    if (name == NULL) {
        return 0;
    }
    return strcmp(name, "feistel") == 0
        || strcmp(name, "auth") == 0
        || strcmp(name, "write") == 0
        || strcmp(name, "pwd") == 0
        || strcmp(name, "rvk") == 0
        || strcmp(name, "LOPW") == 0;
}

void codegen_intrinsic(CodegenContext *ctx, const AstNode *call, int dest_reg)
{
    (void)dest_reg;
    if (ctx != NULL && call != NULL) {
        emit_comment(ctx, "intrinseco '%s' no implementado aun",
                     call->data.call_expression.routine_name);
    }
}
