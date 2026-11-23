/** @file ir_expr.c
 * I3/I4/I7: valores, llamadas, direcciones, índices e intrínsecas.
 */
#include "ir_internal.h"
#include <stdlib.h>
#include <string.h>

/* Solo consulta árboles de literales para decidir B32/uB32 cuando semántica
 * no anotó un tipo exacto, igual que el backend actual. No sustituye el árbol
 * por su valor: ir_expr emitirá todas las operaciones, incluso las constantes. */
static bool constant_integer(const AstNode *node, int64_t *value)
{
    int64_t left, right;
    if (node->kind == AST_INTEGER_LITERAL) {
        if (node->data.integer_literal.value > UINT32_MAX) return false;
        *value = (int64_t)node->data.integer_literal.value;
        return true;
    }
    if (node->kind == AST_UNARY_EXPRESSION &&
        node->data.unary_expression.operator == AST_OP_NEGATE &&
        constant_integer(node->data.unary_expression.operand, value)) {
        *value = -*value;
        return *value >= INT32_MIN && *value <= UINT32_MAX;
    }
    if (node->kind != AST_BINARY_EXPRESSION ||
        !constant_integer(node->data.binary_expression.left, &left) ||
        !constant_integer(node->data.binary_expression.right, &right)) return false;
    switch (node->data.binary_expression.operator) {
    case AST_OP_ADD: *value = left + right; break;
    case AST_OP_SUBTRACT: *value = left - right; break;
    case AST_OP_DIVIDE:
        if (right == 0) return false;
        *value = left / right;
        break;
    case AST_OP_REMAINDER:
        if (right == 0) return false;
        *value = left % right;
        break;
    case AST_OP_MULTIPLY:
    case AST_OP_POWER: {
        /* Magnitudes de hasta 32 bits: su producto cabe en uint64_t. */
        uint64_t base = (uint64_t)(left < 0 ? -left : left);
        uint64_t magnitude;
        bool negative;
        if (node->data.binary_expression.operator == AST_OP_MULTIPLY) {
            magnitude = base * (uint64_t)(right < 0 ? -right : right);
            negative = (left < 0) != (right < 0);
        } else {
            if (right < 0) return false;
            magnitude = 1;
            negative = left < 0 && (right & 1);
            while (right) {
                if (right & 1) magnitude *= base;
                if (magnitude > UINT32_MAX) return false;
                right >>= 1;
                if (right) {
                    base *= base;
                    if (base > UINT32_MAX) return false;
                }
            }
        }
        if (magnitude > UINT32_MAX) return false;
        *value = negative ? -(int64_t)magnitude : (int64_t)magnitude;
        break;
    }
    default: return false;
    }
    return *value >= INT32_MIN && *value <= UINT32_MAX;
}

/* Prioridad a semántica; TYPE_INT por sí solo no especifica ancho ni signo. */
static IrType expression_type(const AstNode *node)
{
    if (node->has_piper_type) return (IrType){.piper = node->piper_type};
    int64_t value;
    PiperType type = PIPER_TYPE_B32;
    if (constant_integer(node, &value) && value > INT32_MAX) type = PIPER_TYPE_UB32;
    return (IrType){.piper = type};
}

/* Una sola correspondencia entre operadores AST y opcodes TAC. */
static IrOpcode operator_opcode(AstOperator op)
{
    switch (op) {
    case AST_OP_ADD: return IR_OP_ADD;
    case AST_OP_SUBTRACT: return IR_OP_SUB;
    case AST_OP_MULTIPLY: return IR_OP_MUL;
    case AST_OP_DIVIDE: return IR_OP_DIV;
    case AST_OP_REMAINDER: return IR_OP_MOD;
    case AST_OP_POWER: return IR_OP_POW;
    case AST_OP_EQUAL: return IR_OP_EQ;
    case AST_OP_NOT_EQUAL: return IR_OP_NE;
    case AST_OP_LESS: return IR_OP_LT;
    case AST_OP_LESS_EQUAL: return IR_OP_LE;
    case AST_OP_GREATER: return IR_OP_GT;
    case AST_OP_GREATER_EQUAL: return IR_OP_GE;
    case AST_OP_LOGICAL_AND: return IR_OP_AND;
    case AST_OP_LOGICAL_OR: return IR_OP_OR;
    case AST_OP_LOGICAL_XOR: return IR_OP_XOR;
    case AST_OP_LOGICAL_NOT: return IR_OP_NOT;
    case AST_OP_NEGATE: return IR_OP_NEG;
    case AST_OP_INCREMENT: return IR_OP_ADD;
    case AST_OP_DECREMENT: return IR_OP_SUB;
    }
    return IR_OP_INVALID;
}

/* Evaluar cada argumento una vez y conservar su valor antes del siguiente.
 * La firma es prestada; solo la lista de operandos pertenece a la llamada. */
static IrOperand call_expression(IrContext *ctx, const AstNode *expression)
{
    const Symbol *callee = expression->resolved_symbol;
    const AstNodeList *args = expression->data.call_expression.arguments;
    IrInstruction call = {.op = IR_OP_CALL, .location = expression->location};
    size_t expected;
    if (callee == NULL) {
        /* Intrínsecas: operación reservada, sin cuerpo ni Symbol de función. */
        const char *name = expression->data.call_expression.routine_name;
        call.op = IR_OP_INTRINSIC;
        if (!strcmp(name, "crypto_auth")) {
            call.extra.call.intrinsic = IR_INTRINSIC_AUTH; expected = 4;
        } else if (!strcmp(name, "crypto_write_key")) {
            call.extra.call.intrinsic = IR_INTRINSIC_WRITE_KEY; expected = 3;
        } else if (!strcmp(name, "crypto_feistel")) {
            call.extra.call.intrinsic = IR_INTRINSIC_FEISTEL; expected = 3;
        } else if (!strcmp(name, "crypto_revoke")) {
            call.extra.call.intrinsic = IR_INTRINSIC_REVOKE; expected = 0;
        } else {
            ir_fail(ctx, IR_INVALID_INPUT, expression->location, "Llamada sin símbolo ni intrínseca reconocida.");
            return (IrOperand){0};
        }
    } else {
        if (callee->symbol_class != SYMBOL_FUNC || callee->param_count < 0) {
            ir_fail(ctx, IR_INVALID_INPUT, expression->location, "La llamada no tiene una firma resuelta válida.");
            return (IrOperand){0};
        }
        expected = (size_t)callee->param_count;
        call.extra.call.callee = callee;
    }
    if (args == NULL || args->count != expected) {
        ir_fail(ctx, IR_INVALID_INPUT, expression->location, "Lista de argumentos IR incompleta.");
        return (IrOperand){0};
    }
    call.extra.call.argument_count = args->count;
    if (args->count > SIZE_MAX / sizeof(IrOperand)) {
        ir_fail(ctx, IR_OUT_OF_MEMORY, expression->location, "Demasiados argumentos IR.");
        return (IrOperand){0};
    }
    if (args->count) {
        call.extra.call.arguments = malloc(args->count * sizeof(IrOperand));
        if (call.extra.call.arguments == NULL) {
            ir_fail(ctx, IR_OUT_OF_MEMORY, expression->location, "No se pudieron reservar los argumentos IR.");
            return (IrOperand){0};
        }
    }
    for (size_t i = 0; i < args->count; i++) {
        const Symbol *parameter = callee ? callee->params[i] : NULL;
        bool array_argument = parameter ? parameter->is_array :
            call.extra.call.intrinsic == IR_INTRINSIC_FEISTEL && i == 0;
        if (array_argument && args->items[i]->kind != AST_IDENTIFIER_EXPRESSION) {
            ir_fail(ctx, IR_UNSUPPORTED, args->items[i]->location,
                "Un argumento de arreglo requiere una variable, no un literal ni una vista parcial.");
        } else {
            /* Un identificador de arreglo devuelve su base tipada, no una copia. */
            IrOperand value = ir_expr(ctx, args->items[i]);
            if (ctx->error.status == IR_OK && value.kind == IR_OPERAND_NONE)
                ir_fail(ctx, IR_INVALID_INPUT, args->items[i]->location, "Un argumento no puede ser none.");
            if (parameter && !parameter->is_array && value.kind == IR_OPERAND_CONSTANT)
                value.type.piper = parameter->declared_type;
            call.extra.call.arguments[i] = value;
        }
        if (ctx->error.status != IR_OK) {
            free(call.extra.call.arguments); /* Todavía no se transfirió a ir_emit. */
            return (IrOperand){0};
        }
    }
    if (callee == NULL || callee->declared_type == PIPER_TYPE_NONE) {
        ir_emit(ctx, call); /* Sin temporal: NONE con IR_OK es una llamada válida. */
        return (IrOperand){0};
    }
    return ir_emit_value(ctx, call, (IrType){.piper = callee->declared_type});
}

/** Símbolo escalar o dirección de arreglo/elemento. ADDR sobre parámetro de
 * arreglo significa la referencia recibida, nunca la dirección de su ranura.
 * Cada índice se evalúa y comprueba antes del siguiente, sin volver a leerlo. */
IrOperand ir_target(IrContext *ctx, const AstNode *target)
{
    if (ctx == NULL || ctx->error.status != IR_OK) return (IrOperand){0};
    if (target == NULL) {
        ir_fail(ctx, IR_INVALID_INPUT, (SourceLocation){0}, "Falta el destino IR.");
        return (IrOperand){0};
    }
    bool indexed = target->kind == AST_INDEX_EXPRESSION;
    const AstNode *base = indexed ? target->data.index_expression.base : target;
    if (base == NULL || base->kind != AST_IDENTIFIER_EXPRESSION) {
        ir_fail(ctx, IR_UNSUPPORTED, target->location, "El destino requiere un identificador o acceso indexado.");
        return (IrOperand){0};
    }
    const Symbol *symbol = base->resolved_symbol;
    if (symbol == NULL || symbol->symbol_class == SYMBOL_FUNC) {
        ir_fail(ctx, IR_INVALID_INPUT, target->location, "El destino no tiene un símbolo de variable resuelto.");
        return (IrOperand){0};
    }
    IrOperand variable = {.kind = IR_OPERAND_SYMBOL,
        .type = {.piper = symbol->declared_type}, .value.symbol = symbol};
    if (!symbol->is_array) {
        if (!indexed) return variable;
        ir_fail(ctx, IR_INVALID_INPUT, target->location, "Un acceso indexado requiere arreglo.");
        return (IrOperand){0};
    }
    if (symbol->declared_type == PIPER_TYPE_BOOL) {
        ir_fail(ctx, IR_UNSUPPORTED, target->location, "Arreglos bool fuera del soporte actual del backend.");
        return (IrOperand){0};
    }
    IrOperand linear = {0};
    IrType integer = {.piper = PIPER_TYPE_B32};
    if (indexed) {
        const AstNodeList *indices = target->data.index_expression.indices;
        if (indices == NULL || indices->count < 1 || indices->count > 2) {
            ir_fail(ctx, IR_INVALID_INPUT, target->location, "Acceso sin uno o dos índices IR.");
            return (IrOperand){0};
        }
        for (size_t i = 0; i < indices->count; i++) {
            IrOperand index = ir_expr(ctx, indices->items[i]);
            IrOperand length = {.kind = IR_OPERAND_CONSTANT, .type = integer,
                .value.constant = i == 0 ? symbol->array_rows : symbol->array_cols};
            ir_emit(ctx, (IrInstruction){.op = IR_OP_CHECK_BOUNDS, .arg1 = index,
                .arg2 = length, .location = indices->items[i]->location});
            if (ctx->error.status != IR_OK) return (IrOperand){0};
            if (i == 0) linear = index;
            else {
                /* Orden por filas: fila * columnas + columna. Ya hay límites;
                 * estos cálculos de dirección no son aritmética Piper CHECKED. */
                linear = ir_emit_value(ctx, (IrInstruction){.op = IR_OP_MUL,
                    .arg1 = linear, .arg2 = length, .location = target->location}, integer);
                linear = ir_emit_value(ctx, (IrInstruction){.op = IR_OP_ADD,
                    .arg1 = linear, .arg2 = index, .location = target->location}, integer);
            }
        }
    }
    IrType address = {.piper = symbol->declared_type, .address = true};
    IrOperand result = ir_emit_value(ctx, (IrInstruction){.op = IR_OP_ADDR,
        .arg1 = variable, .location = target->location}, address);
    if (!indexed || ctx->error.status != IR_OK) return result;
    IrOperand width = {.kind = IR_OPERAND_CONSTANT, .type = integer,
        .value.constant = piper_type_storage_size(symbol->declared_type)};
    IrOperand offset = ir_emit_value(ctx, (IrInstruction){.op = IR_OP_MUL,
        .arg1 = linear, .arg2 = width, .location = target->location}, integer);
    return ir_emit_value(ctx, (IrInstruction){.op = IR_OP_ADD,
        .arg1 = result, .arg2 = offset, .location = target->location}, address);
}

/** Traduce hijos de izquierda a derecha, sin plegado ni cortocircuito.
 * Cada lectura devuelve un temporal estable. NONE también representa fallo;
 * consultar ctx->error.status para distinguirlo del literal none válido. */
IrOperand ir_expr(IrContext *ctx, const AstNode *expression)
{
    if (ctx == NULL || ctx->error.status != IR_OK) return (IrOperand){0};
    if (expression == NULL) {
        ir_fail(ctx, IR_INVALID_INPUT, (SourceLocation){0}, "Falta la expresión IR.");
        return (IrOperand){0};
    }
    IrInstruction instruction = {.location = expression->location};
    IrType type = expression_type(expression);
    switch (expression->kind) {
    case AST_CALL_EXPRESSION:
        return call_expression(ctx, expression);
    case AST_NONE_LITERAL:
        return (IrOperand){0};
    case AST_INTEGER_LITERAL:
        if (expression->data.integer_literal.value > UINT32_MAX) {
            ir_fail(ctx, IR_INVALID_INPUT, expression->location, "Literal fuera del dominio entero de Piper.");
            return (IrOperand){0};
        }
        return (IrOperand){.kind = IR_OPERAND_CONSTANT, .type = type,
            .value.constant = (int64_t)expression->data.integer_literal.value};
    case AST_BOOLEAN_LITERAL:
        return (IrOperand){.kind = IR_OPERAND_CONSTANT,
            .type = {.piper = PIPER_TYPE_BOOL},
            .value.constant = expression->data.boolean_literal.value};
    case AST_IDENTIFIER_EXPRESSION: case AST_INDEX_EXPRESSION: {
        IrOperand target = ir_target(ctx, expression);
        if (expression->kind == AST_IDENTIFIER_EXPRESSION && target.type.address) return target;
        instruction.op = target.type.address ? IR_OP_LOAD : IR_OP_COPY;
        instruction.arg1 = target;
        return ir_emit_value(ctx, instruction, (IrType){.piper = target.type.piper});
    }
    case AST_UNARY_EXPRESSION: {
        AstOperator op = expression->data.unary_expression.operator;
        const AstNode *operand = expression->data.unary_expression.operand;
        IrOperand target = {0};
        bool update = op == AST_OP_INCREMENT || op == AST_OP_DECREMENT;
        if (!update && op != AST_OP_NEGATE && op != AST_OP_LOGICAL_NOT) {
            ir_fail(ctx, IR_UNSUPPORTED, expression->location, "Operador unario IR desconocido.");
            return (IrOperand){0};
        }
        if (update) {
            if (operand->kind != AST_IDENTIFIER_EXPRESSION || operand->inferred_type == TYPE_ARRAY) {
                ir_fail(ctx, IR_UNSUPPORTED, expression->location, "++/-- solo admiten identificadores escalares.");
                return (IrOperand){0};
            }
            target = ir_target(ctx, operand);
            if (ctx->error.status != IR_OK) return (IrOperand){0};
        }
        instruction.op = operator_opcode(op);
        instruction.arg1 = ir_expr(ctx, operand);
        if (update) {
            type = target.type;
            instruction.arg2 = (IrOperand){.kind = IR_OPERAND_CONSTANT,
                .type = type, .value.constant = 1};
        }
        if (op != AST_OP_LOGICAL_NOT) instruction.flags = IR_CHECKED | IR_MAY_TRAP;
        IrOperand result = ir_emit_value(ctx, instruction, type);
        if (update) {
            /* Primero comprobar el resultado, después escribir y devolverlo. */
            ir_emit(ctx, (IrInstruction){.op = IR_OP_COPY, .arg1 = result,
                .result = target, .location = expression->location});
        }
        return ctx->error.status == IR_OK ? result : (IrOperand){0};
    }
    case AST_BINARY_EXPRESSION:
        instruction.op = operator_opcode(expression->data.binary_expression.operator);
        if (instruction.op == IR_OP_INVALID || instruction.op == IR_OP_NEG ||
            instruction.op == IR_OP_NOT ||
            expression->data.binary_expression.operator == AST_OP_INCREMENT ||
            expression->data.binary_expression.operator == AST_OP_DECREMENT) {
            ir_fail(ctx, IR_UNSUPPORTED, expression->location, "Operador binario IR desconocido.");
            return (IrOperand){0};
        }
        instruction.arg1 = ir_expr(ctx, expression->data.binary_expression.left);
        if (ctx->error.status != IR_OK) return (IrOperand){0};
        instruction.arg2 = ir_expr(ctx, expression->data.binary_expression.right);
        if (instruction.op >= IR_OP_ADD && instruction.op <= IR_OP_POW)
            instruction.flags = IR_CHECKED | IR_MAY_TRAP;
        return ir_emit_value(ctx, instruction, type);
    default:
        ir_fail(ctx, IR_UNSUPPORTED, expression->location, "Expresión sin traducción IR soportada en este contexto.");
        return (IrOperand){0};
    }
}
