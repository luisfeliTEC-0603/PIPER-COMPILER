#include "codegen_internal.h"

/*
 * Punto de entrada del generador de código.
 *
 * Recorre el AST_PROGRAM y emite cada rutina. Las declaraciones globales
 * ya tienen espacio en la tabla; las formas de arreglo se validan aqui.
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
                if (elem->data.variable_declaration.dimensions != NULL &&
                    elem->data.variable_declaration.dimensions->count > 0)
                    codegen_array_declaration(ctx, elem);
                else
                    emit_comment(ctx, "global '%s' (espacio reservado)",
                                 elem->data.variable_declaration.name);
                break;
            case AST_IMPORT_DECLARATION:
                emit_comment(ctx, "import '%s' (ignorado en codegen)",
                             elem->data.import_declaration.symbol);
                break;
            default:
                codegen_context_error(ctx,
                    "elemento de programa no manejado (kind=%d)", elem->kind);
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
