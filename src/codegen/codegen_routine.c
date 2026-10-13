#include "codegen_internal.h"

#include <string.h>

/*
 * Emite el prólogo de una rutina.
 *
 * Convención acordada:
 *   - r7 apunta al tope del frame de la rutina.
 *   - Los offsets de locales/params son positivos desde r7.
 *   - El llamador salva r1..r4 si los necesita tras la llamada.
 *   - El llamado salva r5..r7 si los modifica (r7 se restaura aquí).
 *   - r6 se usa como dirección de retorno en llamadas manuales.
 *
 * Por ahora, como las rutinas pueden tener frame 0, el prólogo solo
 * ajusta r7 si total_stack_frame_size > 0.
 */

void codegen_routine_prologue(CodegenContext *ctx, const AstNode *routine)
{
    int frame_size;

    if (ctx == NULL || routine == NULL) {
        return;
    }

    /* El tamaño del frame lo calculó la Persona 1 al analizar la rutina. */
    frame_size = ctx->current_function != NULL
        ? ctx->current_function->total_stack_frame_size
        : 0;

    emit_comment(ctx, "prologo de %s (frame %d bytes)",
                 routine->data.routine_declaration.name, frame_size);

    if (frame_size > 0) {
        if (frame_size <= 32767) {
            emit_instruction(ctx, "diffi r%d, #%d", REG_FP, frame_size);
        } else {
            /* Frame grande: construir el valor en un temporal y restar. */
            int tmp = alloc_temp(ctx);
            if (tmp < 0) {
                return;
            }
            load_immediate(ctx, tmp, frame_size);
            emit_instruction(ctx, "diff r%d, r%d", REG_FP, tmp);
            free_temp(ctx, tmp);
        }
    } else {
        emit_comment(ctx, "(frame vacio, sin ajuste de r7)");
    }
}

/*
 * Emite el epílogo de una rutina.
 *
 * Se llega aquí por caída natural o por un salto desde `output`.
 * El valor de retorno, si existe, ya está en r1.
 * Se restaura r7 y se retorna con jr r6 (mecanismo manual sin CALL/RET).
 */

void codegen_routine_epilogue(CodegenContext *ctx, const AstNode *routine)
{
    int frame_size;

    if (ctx == NULL || routine == NULL) {
        return;
    }

    frame_size = ctx->current_function != NULL
        ? ctx->current_function->total_stack_frame_size
        : 0;

    emit_comment(ctx, "epilogo de %s",
                 routine->data.routine_declaration.name);

    if (frame_size > 0) {
        if (frame_size <= 32767) {
            emit_instruction(ctx, "sumi r%d, #%d", REG_FP, frame_size);
        } else {
            int tmp = alloc_temp(ctx);
            if (tmp < 0) {
                return;
            }
            load_immediate(ctx, tmp, frame_size);
            emit_instruction(ctx, "sum r%d, r%d", REG_FP, tmp);
            free_temp(ctx, tmp);
        }
    }

    /* Retorno: la dirección de retorno debe estar en r6.
     * El llamador es responsable de haberla colocado ahí. */
    emit_instruction(ctx, "jr r%d", REG_TMP1);
}

/*
 * Emite una rutina completa: etiqueta, prólogo, cuerpo, epílogo.
 */

void codegen_routine(CodegenContext *ctx, const AstNode *routine)
{
    const char *name;
    char epilogue_name[SYMBOL_NAME_MAX + 16];
    Symbol *func;
    Scope *saved_scope;
    Symbol *saved_function;
    int saved_frame_offset;

    if (ctx == NULL || routine == NULL) {
        return;
    }
    if (routine->kind != AST_ROUTINE_DECLARATION) {
        codegen_context_error(ctx, "codegen_routine: nodo no es rutina");
        return;
    }

    name = routine->data.routine_declaration.name;
    if (name == NULL) {
        codegen_context_error(ctx, "codegen_routine: rutina sin nombre");
        return;
    }

    func = symbol_table_lookup((SymbolTable *)ctx->table, name);
    if (func == NULL || func->symbol_class != SYMBOL_FUNC) {
        codegen_context_error(ctx,
            "codegen_routine: '%s' no es una funcion registrada", name);
        return;
    }

    /* Validar también arreglos que aparecen solo en la firma. */
    if (routine->data.routine_declaration.parameters != NULL) {
        AstNodeList *params = routine->data.routine_declaration.parameters;
        for (size_t i = 0; i < params->count; i++) {
            AstNode *param = params->items[i];
            if (param != NULL && param->kind == AST_PARAMETER &&
                param->data.parameter.dimensions != NULL &&
                param->data.parameter.dimensions->count > 0)
                codegen_array_declaration(ctx, param);
        }
        if (codegen_has_errors(ctx)) return;
    }

    saved_scope = ctx->current_scope;
    saved_function = ctx->current_function;
    saved_frame_offset = ctx->frame_offset;

    ctx->current_function = func;
    ctx->current_scope = func->function_scope;
    ctx->frame_offset = func->total_stack_frame_size;

    /* Etiqueta de entrada. */
    emit_blank(ctx);
    emit_label(ctx, name);

    /* Prólogo. */
    codegen_routine_prologue(ctx, routine);

    /* Cuerpo. */
    if (routine->data.routine_declaration.body != NULL) {
        AstNode *body = routine->data.routine_declaration.body;
        if (body->kind == AST_BLOCK) {
            codegen_statement_list(ctx, body->data.block.statements);
        } else {
            codegen_statement(ctx, body);
        }
    }

    /* Etiqueta de epílogo: se usa si hay `output` en el cuerpo. */
    snprintf(epilogue_name, sizeof(epilogue_name),
             "epilogo_%s", name);
    emit_label(ctx, epilogue_name);

    /* Epílogo. */
    codegen_routine_epilogue(ctx, routine);

    ctx->current_scope = saved_scope;
    ctx->current_function = saved_function;
    ctx->frame_offset = saved_frame_offset;
}
