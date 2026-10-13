#include "codegen_internal.h"

/*
 * WIP
 */

void codegen_statement(CodegenContext *ctx, const AstNode *stmt)
{
    if (ctx == NULL || stmt == NULL) {
        return;
    }
    emit_comment(ctx, "sentencia no implementada aun (kind=%d)", stmt->kind);
}

void codegen_statement_list(CodegenContext *ctx, const AstNodeList *list)
{
    if (ctx == NULL || list == NULL) {
        return;
    }
    for (size_t i = 0; i < list->count; i++) {
        codegen_statement(ctx, list->items[i]);
    }
}
