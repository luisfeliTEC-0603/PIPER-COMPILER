/** @file ir_generate.c
 * I4: organizar rutinas y arranque. No integrar con main hasta completar IR.
 */
#include "ir_internal.h"
#include <string.h>

/* La publicación pública espera a ir_validate (I6). No devolver como válida
 * una construcción parcial ni implementar aquí una validación provisional. */
IrStatus ir_generate(const AstNode *root, const SymbolTable *symbols,
                     IrProgram **out, IrDiagnostic *diagnostic);

/* Seleccionar una rutina nueva y crear su entrada. Temporales y bloques son
 * propios de cada rutina; no se mezclan con los de la rutina anterior. */
static void begin_routine(IrContext *ctx, IrId id)
{
    ctx->current_routine = id;
    ctx->break_count = 0;
    ctx->current_block = ir_new_block(ctx);
    if (ctx->error.status == IR_OK)
        ctx->program->routines[id].entry = ctx->current_block;
}

/** Construye dentro del contexto; el llamador conserva su propiedad y descarta
 * todo al fallar. No ejecuta semántica ni publica un programa sin validar.
 * Primero registra firmas, luego cuerpos. Las llamadas conservan el Symbol de
 * I2: registrar una función posterior no permite nuevos usos por nombre. */
void ir_build_program(IrContext *ctx)
{
    if (ctx == NULL || ctx->error.status != IR_OK) return;
    if (ctx->root == NULL || ctx->root->kind != AST_PROGRAM || ctx->program == NULL ||
        ctx->program->routine_count != 0) {
        ir_fail(ctx, IR_INVALID_INPUT, (SourceLocation){0}, "Se requiere un contexto IR nuevo con AST de programa.");
        return;
    }
    const AstNodeList *elements = ctx->root->data.program.elements;
    const Symbol *main_symbol = NULL;
    bool needs_startup = false;
    for (size_t i = 0; i < elements->count; i++) {
        const AstNode *node = elements->items[i];
        if (node->kind == AST_ROUTINE_DECLARATION) {
            const Symbol *symbol = node->resolved_symbol;
            if (symbol == NULL || symbol->symbol_class != SYMBOL_FUNC) {
                ir_fail(ctx, IR_INVALID_INPUT, node->location, "Falta el símbolo final de una rutina.");
                return;
            }
            if (ir_new_routine(ctx, symbol) == IR_NO_ID) return;
            if (strcmp(symbol->name, "main") == 0) main_symbol = symbol;
        } else if (node->kind != AST_VARIABLE_DECLARATION) {
            needs_startup = true;
        }
    }
    if (main_symbol == NULL || main_symbol->param_count != 0) {
        ir_fail(ctx, IR_INVALID_INPUT, ctx->root->location, "Se requiere main global sin parámetros.");
        return;
    }
    IrId routine = 0;
    for (size_t i = 0; i < elements->count; i++) {
        const AstNode *node = elements->items[i];
        if (node->kind != AST_ROUTINE_DECLARATION) continue;
        begin_routine(ctx, routine++);
        ir_stmt(ctx, node->data.routine_declaration.body);
        if (ctx->error.status != IR_OK) return;
        if (ctx->current_block != IR_NO_ID) {
            if (node->resolved_symbol->declared_type != PIPER_TYPE_NONE) {
                ir_fail(ctx, IR_INVALID_PROGRAM, node->location, "La rutina con valor termina sin return.");
                return;
            }
            ir_emit(ctx, (IrInstruction){.op = IR_OP_RETURN, .location = node->location});
        }
        if (ctx->error.status != IR_OK) return;
    }
    if (needs_startup) {
        IrId startup = ir_new_routine(ctx, NULL);
        if (startup == IR_NO_ID) return;
        begin_routine(ctx, startup);
        /* Las declaraciones ya están en la tabla; ejecutar solo las sentencias
         * globales y en el orden original, antes de entrar a main. */
        for (size_t i = 0; i < elements->count; i++) {
            const AstNode *node = elements->items[i];
            if (node->kind != AST_ROUTINE_DECLARATION && node->kind != AST_VARIABLE_DECLARATION)
                ir_stmt(ctx, node);
            if (ctx->error.status != IR_OK) return;
        }
        IrInstruction call = {.op = IR_OP_CALL, .location = ctx->root->location,
            .flags = IR_READ_MEMORY | IR_WRITE_MEMORY | IR_MAY_TRAP};
        call.extra.call.callee = main_symbol;
        /* El resultado de main existe aunque el arranque no lo utilice. */
        if (main_symbol->declared_type != PIPER_TYPE_NONE) {
            IrRoutine *entry = &ctx->program->routines[startup];
            call.result = (IrOperand){.kind = IR_OPERAND_TEMP,
                .type = {.piper = main_symbol->declared_type},
                .value.temp = entry->temp_count++};
        }
        ir_emit(ctx, call);
        ir_emit(ctx, (IrInstruction){.op = IR_OP_RETURN, .location = ctx->root->location});
        if (ctx->error.status != IR_OK) return;
    }
    ctx->current_routine = ctx->current_block = IR_NO_ID;
    ctx->break_count = 0;
}
