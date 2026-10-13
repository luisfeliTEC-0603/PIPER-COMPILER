#include "codegen_internal.h"

#include <string.h>

/*
 * Punto de entrada del generador de código.
 *
 * Recorre la lista de hijos de AST_PROGRAM en varias pasadas:
 *   1. Detectar si hay sentencias globales que necesitan una entrada.
 *   2. Generar esas sentencias en orden, antes del salto a main o al final.
 *   3. Generar las rutinas y procesar declaraciones/imports aún no visitados.
 * codegen_statement y codegen_routine recorren los hijos de cada elemento;
 * codegen_expression recorre sus operandos. El recorrido del AST se conserva.
 */

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

    /* Encabezado del archivo ASM. */
    emit_comment(ctx, "Generado por Piper codegen (Persona 3)");
    emit_comment(ctx, "Convencion: r7=frame pointer, r1=retorno, r5/r6=temp");
    emit_blank(ctx);

    elements = root->data.program.elements;
    /* main se busca en la tabla semántica para comprobar su firma. Este
     * recorrido de símbolos es independiente de las pasadas del AST. */
    for (const Symbol *it = table->global_scope->symbols_head;
         it != NULL; it = it->next) {
        if (strcmp(it->name, "main") == 0 && it->symbol_class == SYMBOL_FUNC &&
            it->param_count == 0) {
            entry = it;
            break;
        }
    }
    /* Primera pasada del AST: solo clasifica, no genera instrucciones.
     * Declaraciones, imports y rutinas no son sentencias globales ejecutables;
     * se excluyen aquí, pero sí se procesan en las siguientes pasadas. */
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
        emit_instruction(ctx, "mov r%d, r%d", REG_FP, REG_ZERO);
        emit_instruction(ctx, "sumi r%d, #2", REG_FP);
        emit_instruction(ctx, "shli r%d, #16", REG_FP);
    }
    /* Segunda pasada del AST, si hay código global: procesa en orden todos
     * los elementos salvo rutinas. Incluye declaraciones e imports mediante
     * codegen_statement; las rutinas se emiten después para que la entrada
     * no caiga accidentalmente dentro de su cuerpo.
     * El semántico no permite locales en bloques fuera de una rutina. */
    if (has_global_statements) {
        for (size_t i = 0; i < elements->count; i++) {
            const AstNode *elem = elements->items[i];
            if (elem != NULL && elem->kind != AST_ROUTINE_DECLARATION)
                codegen_statement(ctx, elem);
        }
    }
    if (entry != NULL) {
        /* El id cero termina el retorno de main en fin_programa. */
        emit_instruction(ctx, "diffi r%d, #%d", REG_FP, CALL_RECORD_SIZE);
        emit_instruction(ctx, "stwi r%d, 0(r%d)", REG_ZERO, REG_FP);
        emit_instruction(ctx, "j main");
    } else if (has_global_statements) {
        emit_instruction(ctx, "j fin_programa");
    }

    /* Tercera pasada del AST: emitir las rutinas. Si no hubo segunda pasada,
     * procesar aquí las declaraciones e imports. No repetir nodos ya tratados. */
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
                /* Ya validada por codegen_statement en la segunda pasada. */
                if (has_global_statements) break;
                if (elem->data.variable_declaration.dimensions != NULL &&
                    elem->data.variable_declaration.dimensions->count > 0)
                    codegen_array_declaration(ctx, elem);
                else
                    emit_comment(ctx, "global '%s' (espacio reservado)",
                                 elem->data.variable_declaration.name);
                break;
            case AST_IMPORT_DECLARATION:
                /* Ya tratado por codegen_statement en la segunda pasada. */
                if (has_global_statements) break;
                emit_comment(ctx, "import '%s' (ignorado en codegen)",
                             elem->data.import_declaration.symbol);
                break;
            default:
                /* Asignaciones, expresiones y control global ya fueron
                 * recorridos y emitidos por codegen_statement. */
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
