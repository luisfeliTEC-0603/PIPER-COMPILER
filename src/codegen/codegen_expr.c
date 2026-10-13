#include "codegen_internal.h"

/*
 * WIP
 */

int codegen_expression(CodegenContext *ctx, const AstNode *expr, int dest_reg)
{
    (void)dest_reg;
    if (ctx == NULL || expr == NULL) {
        return TYPE_VOID;
    }
    emit_comment(ctx, "expresion no implementada aun (kind=%d)", expr->kind);
    return TYPE_VOID;
}
