/** @file ir_stmt.c
 * I3: bloques léxicos, declaraciones sin inicialización y asignaciones escalares.
 * I4 añade llamadas y retornos. I5 añade control e I7 accesos y copias de arreglos.
 */
#include "ir_internal.h"
#include <stdlib.h>

/* Solo añadir el salto si el camino sigue abierto: return/break ya lo cerraron. */
static void jump_to(IrContext *ctx, IrId target, SourceLocation location)
{
    if (ctx->error.status != IR_OK || ctx->current_block == IR_NO_ID) return;
    IrInstruction jump = {.op = IR_OP_JUMP, .location = location};
    jump.extra.targets[0] = target;
    ir_emit(ctx, jump);
}

/* Una condición entera significa distinto de cero; bool ya está normalizado.
 * La expresión se evalúa aquí, dentro del bloque de condición de cada vuelta. */
static IrOperand condition_value(IrContext *ctx, const AstNode *node)
{
    IrOperand value = ir_expr(ctx, node);
    if (ctx->error.status != IR_OK) return (IrOperand){0};
    if (value.kind == IR_OPERAND_NONE || value.type.address) {
        ir_fail(ctx, IR_INVALID_INPUT, node->location, "La condición IR requiere un valor escalar.");
        return (IrOperand){0};
    }
    if (value.type.piper == PIPER_TYPE_BOOL) return value;
    IrOperand zero = {.kind = IR_OPERAND_CONSTANT, .type = value.type};
    return ir_emit_value(ctx, (IrInstruction){.op = IR_OP_NE, .arg1 = value,
        .arg2 = zero, .location = node->location}, (IrType){.piper = PIPER_TYPE_BOOL});
}

/* La pila guarda salidas de ciclos, nunca salidas de when. realloc solo mueve
 * memoria de pipoc; los destinos son IDs y siguen siendo válidos al crecer. */
static void push_break(IrContext *ctx, IrId target, SourceLocation location)
{
    if (ctx->break_count == ctx->break_capacity) {
        if (ctx->break_capacity > SIZE_MAX / sizeof(IrId) / 2) {
            ir_fail(ctx, IR_OUT_OF_MEMORY, location, "Demasiados ciclos anidados en IR.");
            return;
        }
        size_t capacity = ctx->break_capacity ? ctx->break_capacity * 2 : 4;
        IrId *items = realloc(ctx->break_targets, capacity * sizeof(*items));
        if (items == NULL) {
            ir_fail(ctx, IR_OUT_OF_MEMORY, location, "No se pudo ampliar la pila de salidas IR.");
            return;
        }
        ctx->break_targets = items;
        ctx->break_capacity = capacity;
    }
    ctx->break_targets[ctx->break_count++] = target;
}

/* El literal true siempre ejecuta su cuerpo, como reconoce semántica al revisar
 * retornos. Las demás condiciones conservan ambas ramas y su continuación. */
static void when_statement(IrContext *ctx, const AstNode *node)
{
    const AstNode *condition = node->data.when_statement.condition;
    if (condition->kind == AST_BOOLEAN_LITERAL && condition->data.boolean_literal.value) {
        ir_stmt(ctx, node->data.when_statement.then_block);
        return;
    }
    IrOperand value = condition_value(ctx, condition);
    if (ctx->error.status != IR_OK) return;
    IrId yes = ir_new_block(ctx), no = ir_new_block(ctx);
    if (ctx->error.status != IR_OK) return;
    IrInstruction branch = {.op = IR_OP_BRANCH, .arg1 = value, .location = node->location};
    branch.extra.targets[0] = yes;
    branch.extra.targets[1] = no;
    ir_emit(ctx, branch);
    ctx->current_block = yes;
    ir_stmt(ctx, node->data.when_statement.then_block);
    if (ctx->error.status != IR_OK) return;
    IrId yes_end = ctx->current_block;
    if (node->data.when_statement.fallback_block == NULL) {
        jump_to(ctx, no, node->location);
        ctx->current_block = no;
        return;
    }
    ctx->current_block = no;
    ir_stmt(ctx, node->data.when_statement.fallback_block);
    if (ctx->error.status != IR_OK) return;
    IrId no_end = ctx->current_block;
    if (yes_end == IR_NO_ID && no_end == IR_NO_ID) return;
    IrId join = ir_new_block(ctx);
    if (ctx->error.status != IR_OK) return;
    ctx->current_block = yes_end;
    jump_to(ctx, join, node->location);
    ctx->current_block = no_end;
    jump_to(ctx, join, node->location);
    ctx->current_block = join;
}

/* Estructura compartida: entrada -> condición -> cuerpo o salida.
 * loop añade inicio y avance; alwhen vuelve directamente a su condición. */
static void loop_statement(IrContext *ctx, const AstNode *node)
{
    bool counted = node->kind == AST_LOOP_STATEMENT;
    const AstNode *body = counted ? node->data.loop_statement.body : node->data.alwhen_statement.body;
    IrOperand control = {0}, step = {0};
    if (counted) {
        const Symbol *symbol = node->resolved_symbol;
        const AstNode *step_node = node->data.loop_statement.step;
        if (symbol == NULL || symbol->symbol_class == SYMBOL_FUNC ||
            symbol->is_array || symbol->type != TYPE_INT) {
            ir_fail(ctx, IR_INVALID_INPUT, node->location, "Falta el control entero resuelto del loop.");
            return;
        }
        if (step_node == NULL || step_node->kind != AST_INTEGER_LITERAL ||
            step_node->data.integer_literal.value == 0 ||
            step_node->data.integer_literal.value > UINT32_MAX) {
            ir_fail(ctx, IR_UNSUPPORTED, node->location, "loop requiere un paso literal entero positivo.");
            return;
        }
        control = (IrOperand){.kind = IR_OPERAND_SYMBOL,
            .type = {.piper = symbol->declared_type}, .value.symbol = symbol};
        step = ir_expr(ctx, step_node); /* Literal: no emite operaciones. */
        IrOperand begin = ir_expr(ctx, node->data.loop_statement.begin);
        /* El backend actual almacena inicio sin añadir comprobación de rango. */
        ir_emit(ctx, (IrInstruction){.op = IR_OP_COPY, .arg1 = begin,
            .result = control, .location = node->location});
    }
    if (ctx->error.status != IR_OK) return;
    IrId test = ir_new_block(ctx), inside = ir_new_block(ctx), exit = ir_new_block(ctx);
    if (ctx->error.status != IR_OK) return;
    jump_to(ctx, test, node->location);
    ctx->current_block = test;
    IrOperand value;
    if (counted) {
        /* Como en el backend: evaluar fin antes de leer el control; fin puede
         * llamar una función que lo modifique. Se repite en cada vuelta. */
        IrOperand end = ir_expr(ctx, node->data.loop_statement.end);
        IrOperand current = ir_emit_value(ctx, (IrInstruction){.op = IR_OP_COPY,
            .arg1 = control, .location = node->location}, control.type);
        value = ir_emit_value(ctx, (IrInstruction){.op = IR_OP_LT, .arg1 = current,
            .arg2 = end, .location = node->location}, (IrType){.piper = PIPER_TYPE_BOOL});
    } else {
        value = condition_value(ctx, node->data.alwhen_statement.condition);
    }
    if (ctx->error.status != IR_OK) return;
    IrInstruction branch = {.op = IR_OP_BRANCH, .arg1 = value, .location = node->location};
    branch.extra.targets[0] = inside;
    branch.extra.targets[1] = exit;
    ir_emit(ctx, branch);
    push_break(ctx, exit, node->location);
    if (ctx->error.status != IR_OK) return;
    ctx->current_block = inside;
    ir_stmt(ctx, body);
    ctx->break_count--; /* Restaurar la salida exterior incluso si el cuerpo falló. */
    if (ctx->error.status != IR_OK) return;
    if (counted && ctx->current_block != IR_NO_ID) {
        /* No crear avance si todos los caminos del cuerpo retornan o rompen. */
        IrId advance = ir_new_block(ctx);
        if (ctx->error.status != IR_OK) return;
        jump_to(ctx, advance, node->location);
        ctx->current_block = advance;
        IrOperand current = ir_emit_value(ctx, (IrInstruction){.op = IR_OP_COPY,
            .arg1 = control, .location = node->location}, control.type);
        IrOperand next = ir_emit_value(ctx, (IrInstruction){.op = IR_OP_ADD,
            .arg1 = current, .arg2 = step, .location = node->location}, control.type);
        /* Avance sin CHECKED: conservar la actualización actual del loop.
         * No usar la traducción de ++, que sí exige comprobación de rango. */
        ir_emit(ctx, (IrInstruction){.op = IR_OP_COPY, .arg1 = next,
            .result = control, .location = node->location});
    }
    jump_to(ctx, test, node->location);
    if (ctx->error.status == IR_OK) ctx->current_block = exit;
}


/* Elemento de una copia/inicialización: índice plano constante y ancho exacto.
 * No lleva bounds check: el recorrido usa las dimensiones ya aprobadas. */
static IrOperand element_address(IrContext *ctx, IrOperand base, size_t index, SourceLocation location)
{
    if (index == 0) return base;
    IrOperand offset = {.kind = IR_OPERAND_CONSTANT, .type = {.piper = PIPER_TYPE_B32},
        .value.constant = (int64_t)(index * (size_t)piper_type_storage_size(base.type.piper))};
    return ir_emit_value(ctx, (IrInstruction){.op = IR_OP_ADD,
        .arg1 = base, .arg2 = offset, .location = location}, base.type);
}

/* STORE no tiene resultado donde anotar CHECKED: si hay conversión de ancho,
 * hacerla primero en una copia tipada, sin estrechar el cálculo de origen. */
static void store_element(IrContext *ctx, IrOperand address, IrOperand value, SourceLocation location)
{
    if (ctx->error.status != IR_OK) return;
    if (value.kind == IR_OPERAND_NONE || value.type.address) {
        ir_fail(ctx, IR_INVALID_INPUT, location, "Un elemento requiere un valor escalar.");
        return;
    }
    IrType type = {.piper = address.type.piper};
    if (value.kind == IR_OPERAND_CONSTANT) value.type = type;
    else if (value.type.piper != type.piper)
        value = ir_emit_value(ctx, (IrInstruction){.op = IR_OP_COPY, .arg1 = value,
            .flags = IR_CHECKED, .location = location}, type);
    ir_emit(ctx, (IrInstruction){.op = IR_OP_STORE, .arg1 = address,
        .arg2 = value, .location = location});
}

/* Literal o copia completa en orden por filas. Leer/evaluar un elemento y
 * escribirlo antes del siguiente conserva efectos y alias, incluso autocopia.
 * No crear buffers, memcpy ni una copia masiva opaca para el optimizador. */
static void array_assignment(IrContext *ctx, const AstNode *statement)
{
    const AstNode *target = statement->data.assignment.target;
    const AstNode *source = statement->data.assignment.value;
    const Symbol *symbol = target->resolved_symbol;
    IrOperand destination = ir_target(ctx, target), origin = {0};
    bool literal = source->kind == AST_ARRAY_LITERAL;
    if (!literal) {
        if (source->kind != AST_IDENTIFIER_EXPRESSION) {
            ir_fail(ctx, IR_UNSUPPORTED, source->location, "Copia de arreglo requiere una variable o literal.");
            return;
        }
        origin = ir_expr(ctx, source); /* Base del arreglo, incluidos parámetros. */
    }
    if (ctx->error.status != IR_OK) return;
    size_t count = (size_t)symbol->array_rows * (size_t)symbol->array_cols;
    for (size_t i = 0; i < count && ctx->error.status == IR_OK; i++) {
        IrOperand value;
        SourceLocation location = statement->location;
        if (literal) {
            const AstNodeList *items = source->data.array_literal.elements;
            const AstNode *element;
            if (target->array_rank == 2) {
                const AstNode *row = items->items[i / (size_t)symbol->array_cols];
                element = row->data.array_literal.elements->items[i % (size_t)symbol->array_cols];
            } else element = items->items[i];
            location = element->location;
            value = ir_expr(ctx, element);
        } else {
            IrOperand address = element_address(ctx, origin, i, location);
            value = ir_emit_value(ctx, (IrInstruction){.op = IR_OP_LOAD,
                .arg1 = address, .location = location}, (IrType){.piper = symbol->declared_type});
        }
        IrOperand address = element_address(ctx, destination, i, location);
        store_element(ctx, address, value, location);
    }
}

/** Añade las instrucciones al bloque activo, sin modificar AST ni símbolos.
 * No inventa inicializaciones. Un camino cerrado no recibe más instrucciones. */
void ir_stmt(IrContext *ctx, const AstNode *statement)
{
    if (ctx == NULL || ctx->error.status != IR_OK || ctx->current_block == IR_NO_ID)
        return;
    if (statement == NULL) {
        ir_fail(ctx, IR_INVALID_INPUT, (SourceLocation){0}, "Falta la sentencia IR.");
        return;
    }
    switch (statement->kind) {
    case AST_BLOCK: {
        const AstNodeList *list = statement->data.block.statements;
        for (size_t i = 0; i < list->count && ctx->error.status == IR_OK &&
             ctx->current_block != IR_NO_ID; i++)
            ir_stmt(ctx, list->items[i]);
        return;
    }
    case AST_WHEN_STATEMENT:
        when_statement(ctx, statement);
        return;
    case AST_ALWHEN_STATEMENT: case AST_LOOP_STATEMENT:
        loop_statement(ctx, statement);
        return;
    case AST_BREAK_STATEMENT:
        if (ctx->break_count == 0)
            ir_fail(ctx, IR_INVALID_INPUT, statement->location, "break requiere un ciclo activo.");
        else
            jump_to(ctx, ctx->break_targets[ctx->break_count - 1], statement->location);
        return;
    case AST_VARIABLE_DECLARATION:
        return; /* El almacenamiento ya está descrito en la tabla semántica. */
    case AST_ROUTINE_DECLARATION:
        ir_fail(ctx, IR_UNSUPPORTED, statement->location, "IR no admite rutinas anidadas.");
        return;
    case AST_OUTPUT_STATEMENT: {
        if (ctx->program == NULL || ctx->current_routine >= ctx->program->routine_count) {
            ir_fail(ctx, IR_INVALID_INPUT, statement->location, "No hay rutina IR para output.");
            return;
        }
        const Symbol *function = ctx->program->routines[ctx->current_routine].symbol;
        if (function == NULL) {
            ir_fail(ctx, IR_INVALID_INPUT, statement->location, "output requiere una rutina declarada.");
            return;
        }
        IrOperand value = ir_expr(ctx, statement->data.output_statement.value);
        if (ctx->error.status != IR_OK) return;
        if ((value.kind == IR_OPERAND_NONE) != (function->declared_type == PIPER_TYPE_NONE)) {
            ir_fail(ctx, IR_INVALID_INPUT, statement->location, "El retorno no corresponde a la firma de la rutina.");
            return;
        }
        /* El retorno conserva cálculos previos; la firma indica el tipo de entrega. */
        if (value.kind == IR_OPERAND_CONSTANT) value.type.piper = function->declared_type;
        ir_emit(ctx, (IrInstruction){.op = IR_OP_RETURN, .arg1 = value,
            .location = statement->location});
        return;
    }
    case AST_ASSIGNMENT: {
        const AstNode *node = statement->data.assignment.target;
        if (node->kind == AST_IDENTIFIER_EXPRESSION && node->resolved_symbol && node->resolved_symbol->is_array) {
            array_assignment(ctx, statement);
            return;
        }
        /* Capturar y comprobar todos los índices antes de evaluar el origen. */
        IrOperand target = ir_target(ctx, statement->data.assignment.target);
        if (ctx->error.status != IR_OK) return;
        IrOperand value = ir_expr(ctx, statement->data.assignment.value);
        if (ctx->error.status != IR_OK) return;
        if (value.kind == IR_OPERAND_NONE) {
            ir_fail(ctx, IR_INVALID_INPUT, statement->location, "No se puede asignar un valor none.");
            return;
        }
        if (target.type.address) {
            store_element(ctx, target, value, statement->location);
            return;
        }
        /* Un literal adopta el tipo de su uso aprobado. Un temporal conserva
         * el tipo del cálculo: asignar a B8 no convierte su suma previa a B8. */
        if (value.kind == IR_OPERAND_CONSTANT) value.type = target.type;
        ir_emit(ctx, (IrInstruction){.op = IR_OP_COPY, .arg1 = value,
            .result = target, .location = statement->location,
            .flags = value.type.piper != target.type.piper ? IR_CHECKED | IR_MAY_TRAP : 0});
        return;
    }
    case AST_INTEGER_LITERAL: case AST_BOOLEAN_LITERAL: case AST_NONE_LITERAL:
    case AST_IDENTIFIER_EXPRESSION: case AST_UNARY_EXPRESSION: case AST_BINARY_EXPRESSION:
    case AST_CALL_EXPRESSION: case AST_INDEX_EXPRESSION:
        (void)ir_expr(ctx, statement); /* Expresión usada como sentencia, incluido ++/--. */
        return;
    default:
        ir_fail(ctx, IR_UNSUPPORTED, statement->location, "Sentencia sin traducción IR soportada.");
        return;
    }
}
