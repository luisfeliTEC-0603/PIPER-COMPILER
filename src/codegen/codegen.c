#include "codegen_internal.h"

/*
 * Punto de entrada del generador de código.
 *
 * Recorre el AST_PROGRAM y emite cada rutina y declaración global.
 * Por ahora solo procesa rutinas; las declaraciones globales y las
 * importaciones se ignoran con un comentario.
 */

CodegenResult codegen_generate(const AstNode *root,
                               const SymbolTable *table,
                               FILE *output,
                               const CodegenOptions *options)
{
    CodegenContext *ctx;
    CodegenResult result;

    if (root == NULL || table == NULL || output == NULL) {
        return CODEGEN_ERROR_AST_INVALIDO;
    }
    if (root->kind != AST_PROGRAM) {
        return CODEGEN_ERROR_AST_INVALIDO;
    }

    ctx = codegen_context_create(root, table, output, options);
    if (ctx == NULL) {
        return CODEGEN_ERROR_SIN_MEMORIA;
    }

    /* Encabezado del archivo ASM. */
    emit_comment(ctx, "Generado por Piper codegen (Persona 3)");
    emit_comment(ctx, "Convencion: r7=frame pointer, r1=retorno, r5/r6=temp");
    emit_blank(ctx);

    /* Recorrer los elementos del programa. */
    if (root->data.program.elements != NULL) {
        AstNodeList *elements = root->data.program.elements;
        for (size_t i = 0; i < elements->count; i++) {
            AstNode *elem = elements->items[i];
            if (elem == NULL) {
                continue;
            }
            switch (elem->kind) {
            case AST_ROUTINE_DECLARATION:
                codegen_routine(ctx, elem);
                break;
            case AST_VARIABLE_DECLARATION:
                /* Globales: por ahora solo comentario.
                 * En puntos posteriores se emitirá su inicialización. */
                emit_comment(ctx, "global '%s' (pendiente de inicializar)",
                             elem->data.variable_declaration.name);
                break;
            case AST_IMPORT_DECLARATION:
                emit_comment(ctx, "import '%s' (ignorado en codegen)",
                             elem->data.import_declaration.symbol);
                break;
            default:
                emit_comment(ctx, "elemento de programa no manejado (kind=%d)",
                             elem->kind);
                break;
            }
        }
    }

    result = codegen_has_errors(ctx)
        ? CODEGEN_ERROR_INTERNO
        : CODEGEN_OK;

    codegen_context_destroy(ctx);
    return result;
}
