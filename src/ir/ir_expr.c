/** @file ir_expr.c
 * I3: literales, lecturas y operaciones escalares. Llamadas e índices pendientes.
 */
#include "ir_internal.h"

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

/* Reserva un ID virtual y emite su definición. No reserva registros ni pila.
 * Los atributos aritméticos se solicitan aquí; I6 completará efectos y validación. */
static IrOperand emit_value(IrContext *ctx, IrInstruction instruction, IrType type)
{
    if (ctx->error.status != IR_OK) return (IrOperand){0};
    if (ctx->program == NULL || ctx->current_routine >= ctx->program->routine_count) {
        ir_fail(ctx, IR_INVALID_INPUT, instruction.location, "No hay rutina IR activa.");
        return (IrOperand){0};
    }
    IrRoutine *routine = &ctx->program->routines[ctx->current_routine];
    if (routine->temp_count == IR_NO_ID) {
        ir_fail(ctx, IR_OUT_OF_MEMORY, instruction.location, "Se agotaron los temporales IR.");
        return (IrOperand){0};
    }
    instruction.result = (IrOperand){
        .kind = IR_OPERAND_TEMP, .type = type, .value.temp = routine->temp_count
    };
    ir_emit(ctx, instruction);
    if (ctx->error.status != IR_OK) return (IrOperand){0};
    routine->temp_count++;
    return instruction.result;
}

/** Resuelve el destino sin leerlo. Usa el enlace de I2, nunca busca por nombre.
 * Índices y arreglos completos se incorporarán en I7. */
IrOperand ir_target(IrContext *ctx, const AstNode *target)
{
    if (ctx == NULL || ctx->error.status != IR_OK) return (IrOperand){0};
    if (target == NULL) {
        ir_fail(ctx, IR_INVALID_INPUT, (SourceLocation){0}, "Falta el destino IR.");
        return (IrOperand){0};
    }
    if (target->kind != AST_IDENTIFIER_EXPRESSION) {
        ir_fail(ctx, IR_UNSUPPORTED, target->location, "I3 solo admite destinos escalares identificadores.");
        return (IrOperand){0};
    }
    const Symbol *symbol = target->resolved_symbol;
    if (symbol == NULL || symbol->symbol_class == SYMBOL_FUNC) {
        ir_fail(ctx, IR_INVALID_INPUT, target->location, "El destino no tiene un símbolo de variable resuelto.");
        return (IrOperand){0};
    }
    if (symbol->is_array) {
        ir_fail(ctx, IR_UNSUPPORTED, target->location, "Los accesos a arreglos se incorporarán en I7.");
        return (IrOperand){0};
    }
    return (IrOperand){.kind = IR_OPERAND_SYMBOL,
        .type = {.piper = symbol->declared_type}, .value.symbol = symbol};
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
    case AST_IDENTIFIER_EXPRESSION:
        instruction.op = IR_OP_COPY;
        instruction.arg1 = ir_target(ctx, expression);
        return emit_value(ctx, instruction, instruction.arg1.type);
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
        IrOperand result = emit_value(ctx, instruction, type);
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
        return emit_value(ctx, instruction, type);
    default:
        ir_fail(ctx, IR_UNSUPPORTED, expression->location, "Expresión pendiente de una etapa posterior a I3.");
        return (IrOperand){0};
    }
}
