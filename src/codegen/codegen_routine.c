#include "codegen_internal.h"

#include <string.h>

_Static_assert(CODEGEN_STACK_BOTTOM == 0x1F000, "revisar mapa fisico de pila");

/*
 * Emite el prólogo de una rutina.
 *
 * Convención acordada:
 *   - r7 apunta al tope del frame de la rutina.
 *   - Los offsets de locales/params son positivos desde r7.
 *   - El llamador conserva r1..r6 en la ranura de llamada.
 *   - Los parámetros llegan por r1..r4 y se copian al frame local.
 *   - La dirección de retorno se representa por un id de continuación.
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

    /* Comprobar antes de escribir el frame. El semántico calcula una reserva
     * de al menos 64 bytes para llamadas, stores, índices y spills pendientes. */
    emit_instruction(ctx, "mov r%d, r%d", REG_TMP0, REG_FP);
    emit_instruction(ctx, "diffi r%d, #%d", REG_TMP0,
                     frame_size + ctx->call_reserve_size);
    load_immediate(ctx, REG_TMP1, CODEGEN_STACK_BOTTOM);
    emit_instruction(ctx, "ltn r%d, r%d", REG_TMP0, REG_TMP1);
    emit_instruction(ctx, "jnz r%d, error_pila", REG_TMP0);

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

    if (ctx->current_function != NULL) {
        Symbol *function = ctx->current_function;
        if (function->param_count > MAX_ARGUMENTOS ||
            (function->param_count > 0 && function->params == NULL)) {
            codegen_context_error(ctx, "firma de '%s' no soportada", function->name);
            return;
        }
        for (int i = 0; i < function->param_count; i++) {
            Symbol *param = function->params[i];
            if (param == NULL || param->address_or_offset < 0) {
                codegen_context_error(ctx, "parametro sin ranura en '%s'", function->name);
                return;
            }
            emit_instruction(ctx, "stwi r%d, %d(r%d)",
                             REG_ARG0 + i, param->address_or_offset, REG_FP);
        }
    }
}

/*
 * Emite el epílogo de una rutina.
 *
 * Se llega aquí por caída natural o por un salto desde `output`.
 * El valor de retorno, si existe, ya está en r1.
 * Se restaura r7, se lee el id de continuación y se salta al despachador.
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

    emit_instruction(ctx, "lowi r%d, 0(r%d)", REG_TMP1, REG_FP);
    emit_instruction(ctx, "j retornos");
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
    int saved_reserve;

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
    if (func->param_count > MAX_ARGUMENTOS) {
        codegen_context_error(ctx,
            "'%s' tiene mas de %d parametros", name, MAX_ARGUMENTOS);
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
    saved_reserve = ctx->call_reserve_size;

    ctx->current_function = func;
    ctx->current_scope = func->function_scope;
    ctx->frame_offset = func->total_stack_frame_size;
    ctx->call_reserve_size = func->call_reserve_size;

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
    ctx->call_reserve_size = saved_reserve;
}
