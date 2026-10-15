#include "codegen_internal.h"

#include <string.h>

/* El programa se emite en tres fases: detectar código global, emitirlo y,
 * finalmente, emitir las rutinas sin alterar el orden de sus nodos. */

CodegenResult codegen_generate(const AstNode *root,
                               const SymbolTable *table,
                               FILE *output,
                               const CodegenOptions *options)
{
    CodegenContext *ctx;
    CodegenResult result;
    const Symbol *entry = NULL;
    int has_global_statements = 0;
    AstNodeList *elements;

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

    emit_comment(ctx, "Generado por Piper");
    emit_comment(ctx, "Registros: r7=frame, r1=retorno, r5/r6=temporales");
    emit_blank(ctx);

    elements = root->data.program.elements;
    /* Solo una función main sin parámetros puede ser punto de entrada. */
    for (const Symbol *it = table->global_scope->symbols_head;
         it != NULL; it = it->next) {
        if (strcmp(it->name, "main") == 0 && it->symbol_class == SYMBOL_FUNC &&
            it->param_count == 0) {
            entry = it;
            break;
        }
    }
    /* Detectar si hace falta una sección de ejecución global. */
    if (elements != NULL) {
        for (size_t i = 0; i < elements->count; i++) {
            const AstNode *elem = elements->items[i];
            if (elem != NULL && elem->kind != AST_ROUTINE_DECLARATION &&
                elem->kind != AST_VARIABLE_DECLARATION &&
                elem->kind != AST_IMPORT_DECLARATION)
                has_global_statements = 1;
        }
    }

    if (entry != NULL || has_global_statements) {
        load_immediate(ctx, REG_FP, CODEGEN_STACK_TOP);
    }
    /* Las rutinas se emiten después para que la entrada no caiga en su cuerpo. */
    if (has_global_statements) {
        for (size_t i = 0; i < elements->count; i++) {
            const AstNode *elem = elements->items[i];
            if (elem != NULL && elem->kind != AST_ROUTINE_DECLARATION)
                codegen_statement(ctx, elem);
        }
    }
    if (entry != NULL) {
        /* La continuación cero marca el retorno de main. */
        emit_instruction(ctx, "diffi r%d, #%d", REG_FP, CALL_RECORD_SIZE);
        emit_instruction(ctx, "stwi r%d, 0(r%d)", REG_ZERO, REG_FP);
        emit_instruction(ctx, "j " CODEGEN_ROUTINE_LABEL("main"));
    } else if (has_global_statements) {
        emit_instruction(ctx, "j " CODEGEN_INTERNAL_LABEL("fin_programa"));
    }

    /* Emitir rutinas y los nodos no procesados por la sección global. */
    if (root->data.program.elements != NULL) {
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
                if (has_global_statements) break;
                if (elem->data.variable_declaration.dimensions != NULL &&
                    elem->data.variable_declaration.dimensions->count > 0)
                    codegen_array_declaration(ctx, elem);
                else
                    emit_comment(ctx, "global '%s' (espacio reservado)",
                                 elem->data.variable_declaration.name);
                break;
            case AST_IMPORT_DECLARATION:
                if (has_global_statements) break;
                emit_comment(ctx, "import '%s' (ignorado en codegen)",
                             elem->data.import_declaration.symbol);
                break;
            default:
                break;
            }
        }
    }

    codegen_emit_return_dispatch(ctx);

    result = codegen_has_errors(ctx)
        ? CODEGEN_ERROR_INTERNO
        : CODEGEN_OK;

    codegen_context_destroy(ctx);
    return result;
}
