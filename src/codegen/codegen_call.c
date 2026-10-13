#include "codegen_internal.h"

/*
 * WIP
 */

void codegen_call(CodegenContext *ctx, const AstNode *call, int dest_reg)
{
    (void)dest_reg;
    if (ctx != NULL && call != NULL) {
        emit_comment(ctx, "llamada a '%s' no implementada aun",
                     call->data.call_expression.routine_name);
    }
}
