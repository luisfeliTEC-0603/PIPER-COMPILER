#include "codegen_internal.h"

/*
 * WIP
 */

void codegen_array_declaration(CodegenContext *ctx, const AstNode *decl)
{
    (void)ctx;
    (void)decl;
}

void codegen_array_index_address(CodegenContext *ctx,
                                 const AstNode *index_expr,
                                 int base_reg, int dest_reg)
{
    (void)base_reg;
    (void)dest_reg;
    if (ctx != NULL && index_expr != NULL) {
        emit_comment(ctx, "indexacion no implementada aun");
    }
}
