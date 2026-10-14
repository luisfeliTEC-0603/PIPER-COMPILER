#include "codegen_internal.h"

/*
 * Evalúa una expresión y deja el resultado en dest_reg.
 *
 * Retorna el tipo inferido de la expresión (TYPE_INT, TYPE_BOOL, etc.),
 * o TYPE_VOID si hubo error.
 *
 * Convención:
 *   - El llamador reserva dest_reg.
 *   - La función puede usar temporales adicionales (r5/r6) y los libera.
 *   - Si la expresión es un lvalue, se carga su valor, no su dirección.
 */

/* ----------------------------------------------------------------
 * Literales e identificadores
 * ---------------------------------------------------------------- */

static int codegen_integer_literal(CodegenContext *ctx,
                                   const AstNode *expr, int dest_reg)
{
    long long value = (long long)expr->data.integer_literal.value;
    if (!load_immediate(ctx, dest_reg, value)) {
        return TYPE_VOID;
    }
    return TYPE_INT;
}

static int codegen_boolean_literal(CodegenContext *ctx,
                                   const AstNode *expr, int dest_reg)
{
    long long value = expr->data.boolean_literal.value ? 1 : 0;
    if (!load_immediate(ctx, dest_reg, value)) {
        return TYPE_VOID;
    }
    return TYPE_BOOL;
}

static int codegen_identifier_load(CodegenContext *ctx,
                                   const AstNode *expr, int dest_reg)
{
    const char *name = expr->data.identifier.name;
    Symbol *sym = lookup_symbol_or_error(ctx, name, -1);

    if (sym == NULL) {
        return TYPE_VOID;
    }
    if (sym->symbol_class == SYMBOL_FUNC) {
        codegen_context_error(ctx,
            "'%s' es una funcion; se requiere una llamada", name);
        return TYPE_VOID;
    }
    if (sym->is_array || sym->type == TYPE_ARRAY) {
        codegen_context_error(ctx,
            "'%s' es un arreglo; se requiere indexacion", name);
        return TYPE_VOID;
    }
    if (sym->address_or_offset < 0) {
        codegen_context_error(ctx,
            "'%s' no tiene direccion asignada", name);
        return TYPE_VOID;
    }

    if (!load_symbol_value(ctx, sym, dest_reg)) return TYPE_VOID;
    return sym->type;
}

/* ----------------------------------------------------------------
 * Operadores unarios
 * ---------------------------------------------------------------- */

static int codegen_unary_negate(CodegenContext *ctx,
                                const AstNode *expr, int dest_reg)
{
    const AstNode *operand = expr->data.unary_expression.operand;

    /* Optimización: literal entero. */
    if (operand != NULL && operand->kind == AST_INTEGER_LITERAL) {
        long long v = (long long)operand->data.integer_literal.value;
        if (v <= 2147483648LL) {
            if (!load_immediate(ctx, dest_reg, -v)) {
                return TYPE_VOID;
            }
            return TYPE_INT;
        }
    }

    if (codegen_expression(ctx, operand, dest_reg) == TYPE_VOID) {
        return TYPE_VOID;
    }
    /* En MATH, diff rA, rB calcula rA-rB. El complemento a dos da -rA. */
    emit_instruction(ctx, "not r%d", dest_reg);
    emit_instruction(ctx, "sumi r%d, #1", dest_reg);
    return TYPE_INT;
}

static int codegen_unary_logical_not(CodegenContext *ctx,
                                     const AstNode *expr, int dest_reg)
{
    const AstNode *operand = expr->data.unary_expression.operand;
    if (codegen_expression(ctx, operand, dest_reg) == TYPE_VOID) {
        return TYPE_VOID;
    }
    /* Asumimos que la expresión ya produce 0 o 1.
     * xori #1 invierte: 0->1, 1->0. */
    emit_instruction(ctx, "xori r%d, #1", dest_reg);
    return TYPE_BOOL;
}

static int codegen_unary_increment(CodegenContext *ctx,
                                   const AstNode *expr, int dest_reg,
                                   int is_increment)
{
    const AstNode *operand = expr->data.unary_expression.operand;

    if (operand == NULL || operand->kind != AST_IDENTIFIER_EXPRESSION) {
        codegen_context_error(ctx,
            "%s requiere un lvalue (identificador)",
            is_increment ? "++" : "--");
        return TYPE_VOID;
    }

    const char *name = operand->data.identifier.name;
    Symbol *sym = lookup_symbol_or_error(ctx, name, -1);

    if (sym == NULL || sym->symbol_class == SYMBOL_FUNC
        || sym->is_array || sym->type == TYPE_ARRAY) {
        codegen_context_error(ctx,
            "%s requiere una variable escalar", is_increment ? "++" : "--");
        return TYPE_VOID;
    }

    /* Cargar valor actual en dest_reg. */
    if (!load_symbol_value(ctx, sym, dest_reg)) return TYPE_VOID;

    /* Rechazar el extremo del tipo antes de cambiar o almacenar el valor. */
    if (!codegen_checked_step(ctx, sym->declared_type, dest_reg, is_increment))
        return TYPE_VOID;
    if (is_increment) {
        emit_instruction(ctx, "sumi r%d, #1", dest_reg);
    } else {
        emit_instruction(ctx, "diffi r%d, #1", dest_reg);
    }

    /* Almacenar de vuelta. */
    store_symbol_value(ctx, sym, dest_reg);
    return sym->type;
}

static int codegen_unary_expression(CodegenContext *ctx,
                                    const AstNode *expr, int dest_reg)
{
    AstOperator op = expr->data.unary_expression.operator;

    switch (op) {
    case AST_OP_NEGATE:
        return codegen_unary_negate(ctx, expr, dest_reg);
    case AST_OP_LOGICAL_NOT:
        return codegen_unary_logical_not(ctx, expr, dest_reg);
    case AST_OP_INCREMENT:
        return codegen_unary_increment(ctx, expr, dest_reg, 1);
    case AST_OP_DECREMENT:
        return codegen_unary_increment(ctx, expr, dest_reg, 0);
    default:
        codegen_context_error(ctx,
            "operador unario no implementado (op=%d)", op);
        return TYPE_VOID;
    }
}

/* ----------------------------------------------------------------
 * Operadores binarios
 * ---------------------------------------------------------------- */

/*
 * Emite la secuencia para una operación binaria.
 * `instruction` es el mnemónico PORYGON: sum, diff, mul, and, or, xor.
 * `invert` indica si después hay que invertir el resultado con xori #1.
 *   - Se usa para <=, >=, != (que se calculan como la negación de >, <, =).
 */
/* Ya se evaluó el lado izquierdo en dest. Si r5/r6 están ocupados, guardarlo
 * junto con los registros vivos y evaluar el lado derecho con temporales libres.
 * Permite acum + A|i&k| * B|k&j| sin volver a evaluar el lado izquierdo. */
static int emit_binary_operation(CodegenContext *ctx, const AstNode *expr,
                                 int left, int right, const char *instruction)
{
    AstOperator op = expr->data.binary_expression.operator;
    if (op == AST_OP_ADD || op == AST_OP_SUBTRACT || op == AST_OP_MULTIPLY)
        return codegen_checked_binary(ctx, expr, left, right);
    if (op < AST_OP_LESS || op > AST_OP_GREATER_EQUAL) {
        emit_instruction(ctx, "%s r%d, r%d", instruction, left, right);
        return 1;
    }
    int left_unsigned = codegen_expression_is_unsigned(expr->data.binary_expression.left);
    int right_unsigned = codegen_expression_is_unsigned(expr->data.binary_expression.right);
    if (left_unsigned && right_unsigned) {
        emit_instruction(ctx, "%s r%d, r%d", instruction, left, right);
        return 1;
    }
    if (ctx->pending_call_bytes > ctx->call_reserve_size - CALL_RECORD_SIZE) {
        codegen_context_error(ctx, "reserva para mascara de comparacion excedida");
        return 0;
    }
    int scratch = REG_ARG0;
    while (scratch == left || scratch == right) scratch++;
    ctx->pending_call_bytes += CALL_RECORD_SIZE;
    int offset = -ctx->pending_call_bytes + 28;
    /* Un registro de argumentos puede estar vivo: conservarlo sin requerir
     * un tercer temporal físico. Los operandos ya se evaluaron una sola vez. */
    emit_instruction(ctx, "stwi r%d, %d(r7)", scratch, offset);
    emit_typed_order(ctx, left, right, scratch, left_unsigned, right_unsigned,
                     op == AST_OP_GREATER || op == AST_OP_LESS_EQUAL);
    emit_instruction(ctx, "lowi r%d, %d(r7)", scratch, offset);
    ctx->pending_call_bytes -= CALL_RECORD_SIZE;
    return 1;
}

static int codegen_binary_spilled(CodegenContext *ctx, const AstNode *expr,
                                  int dest, const char *instruction, int invert)
{
    unsigned int saved = ctx->temp_in_use;
    unsigned int mask = (0x1eu | (saved << REG_TMP0)) & ~(1u << dest);
    int reserved, type = TYPE_VOID;
    if (ctx->pending_call_bytes > ctx->call_reserve_size - CALL_RECORD_SIZE) {
        codegen_context_error(ctx, "reserva de pila para expresion excedida");
        return TYPE_VOID;
    }
    ctx->pending_call_bytes += CALL_RECORD_SIZE;
    reserved = ctx->pending_call_bytes;
    emit_instruction(ctx, "stwi r%d, %d(r7)", dest, -reserved + 28);
    for (int reg = REG_RET; reg <= REG_TMP1; reg++)
        if (mask & (1u << reg))
            emit_instruction(ctx, "stwi r%d, %d(r7)", reg, -reserved + reg * 4);
    reset_temps(ctx);
    if (codegen_expression(ctx, expr->data.binary_expression.right, REG_RET) == TYPE_VOID)
        goto done;
    emit_instruction(ctx, "lowi r2, %d(r7)", -reserved + 28);
    if (!emit_binary_operation(ctx, expr, 2, 1, instruction)) goto done;
    if (invert) emit_instruction(ctx, "xori r2, #1");
    if (dest != REG_ARG1) emit_instruction(ctx, "mov r%d, r2", dest);
    for (int reg = REG_RET; reg <= REG_TMP1; reg++)
        if (mask & (1u << reg))
            emit_instruction(ctx, "lowi r%d, %d(r7)", reg, -reserved + reg * 4);
    type = expr->inferred_type;
done:
    ctx->temp_in_use = saved;
    ctx->pending_call_bytes -= CALL_RECORD_SIZE;
    return type;
}

static int codegen_binary_emit(CodegenContext *ctx,
                               const AstNode *expr, int dest_reg,
                               const char *instruction,
                               int invert)
{
    const AstNode *left = expr->data.binary_expression.left;
    const AstNode *right = expr->data.binary_expression.right;
    int temp;
    int left_type, right_type;

    /* 1. Evaluar left en dest_reg. */
    left_type = codegen_expression(ctx, left, dest_reg);
    if (left_type == TYPE_VOID) {
        return TYPE_VOID;
    }

    /* 2. Reservar temporal. */
    if (ctx->temp_in_use == 3)
        return codegen_binary_spilled(ctx, expr, dest_reg, instruction, invert);
    temp = alloc_temp(ctx);
    if (temp < 0) {
        return TYPE_VOID;
    }

    /* 3. Evaluar right en temp. */
    right_type = codegen_expression(ctx, right, temp);
    if (right_type == TYPE_VOID) {
        free_temp(ctx, temp);
        return TYPE_VOID;
    }

    /* 4. Emitir la operación. */
    if (instruction != NULL) {
        if (!emit_binary_operation(ctx, expr, dest_reg, temp, instruction)) {
            free_temp(ctx, temp);
            return TYPE_VOID;
        }
    }

    /* 5. Invertir si es necesario. */
    if (invert) {
        emit_instruction(ctx, "xori r%d, #1", dest_reg);
    }

    /* 6. Liberar temporal. */
    free_temp(ctx, temp);

    /* 7. Retornar el tipo. */
    return expr->inferred_type != TYPE_VOID
        ? expr->inferred_type
        : (left_type == TYPE_BOOL && right_type == TYPE_BOOL
           ? TYPE_BOOL : TYPE_INT);
}

/*
 * Orden: los enteros signed invierten el bit 31 antes de comparar en la ALU
 * unsigned. Igualdad y desigualdad comparan directamente los patrones de bits.
 *   <  -> ltn
 *   >  -> gtn
 *   <= -> gtn + xori #1  (porque a <= b  ==  !(a > b))
 *   >= -> ltn + xori #1  (porque a >= b  ==  !(a < b))
 *   =  -> eor
 *   != -> eor + xori #1  (porque a != b  ==  !(a == b))
 */
static int codegen_binary_comparison(CodegenContext *ctx,
                                     const AstNode *expr, int dest_reg)
{
    AstOperator op = expr->data.binary_expression.operator;

    switch (op) {
    case AST_OP_LESS:
        return codegen_binary_emit(ctx, expr, dest_reg, "ltn", 0);
    case AST_OP_LESS_EQUAL:
        return codegen_binary_emit(ctx, expr, dest_reg, "gtn", 1);
    case AST_OP_GREATER:
        return codegen_binary_emit(ctx, expr, dest_reg, "gtn", 0);
    case AST_OP_GREATER_EQUAL:
        return codegen_binary_emit(ctx, expr, dest_reg, "ltn", 1);
    case AST_OP_EQUAL:
        return codegen_binary_emit(ctx, expr, dest_reg, "eor", 0);
    case AST_OP_NOT_EQUAL:
        return codegen_binary_emit(ctx, expr, dest_reg, "eor", 1);
    default:
        codegen_context_error(ctx,
            "comparacion no implementada (op=%d)", op);
        return TYPE_VOID;
    }
}

/* ----------------------------------------------------------------
 * Aritmética y lógica binaria
 * ---------------------------------------------------------------- */

/* +, -, *, and, or y xor usan instrucciones directas de la ISA. */
static int codegen_binary_arithmetic(CodegenContext *ctx,
                                     const AstNode *expr, int dest_reg)
{
    AstOperator op = expr->data.binary_expression.operator;
    const AstNode *left = expr->data.binary_expression.left;
    const AstNode *right = expr->data.binary_expression.right;
    int temp;
    int left_type, right_type;
    const char *instruction;

    if (op == AST_OP_DIVIDE || op == AST_OP_REMAINDER)
        return codegen_general_arithmetic(ctx, expr, dest_reg);
    if (op == AST_OP_POWER)
        return codegen_general_arithmetic(ctx, expr, dest_reg);

    switch (op) {
    case AST_OP_ADD: instruction = "sum"; break;
    case AST_OP_SUBTRACT: instruction = "diff"; break;
    case AST_OP_MULTIPLY: instruction = "mul"; break;
    case AST_OP_LOGICAL_AND: instruction = "and"; break;
    case AST_OP_LOGICAL_OR: instruction = "or"; break;
    case AST_OP_LOGICAL_XOR: instruction = "xor"; break;
    default:
        codegen_context_error(ctx, "operador binario no implementado (op=%d)", op);
        return TYPE_VOID;
    }

    /* 1. Evaluar left en dest_reg. */
    left_type = codegen_expression(ctx, left, dest_reg);
    if (left_type == TYPE_VOID) {
        return TYPE_VOID;
    }

    /* 2. Reservar temporal. */
    if (ctx->temp_in_use == 3)
        return codegen_binary_spilled(ctx, expr, dest_reg, instruction, 0);
    temp = alloc_temp(ctx);
    if (temp < 0) {
        return TYPE_VOID;
    }

    /* 3. Evaluar right en temp. */
    right_type = codegen_expression(ctx, right, temp);
    if (right_type == TYPE_VOID) {
        free_temp(ctx, temp);
        return TYPE_VOID;
    }

    /* 4. Emitir la operación. */
    if (!emit_binary_operation(ctx, expr, dest_reg, temp, instruction)) {
        free_temp(ctx, temp);
        return TYPE_VOID;
    }
    free_temp(ctx, temp);

    /* Tipo resultante: para lógicos, TYPE_BOOL; para aritmética, TYPE_INT. */
    if (op == AST_OP_LOGICAL_AND || op == AST_OP_LOGICAL_OR
        || op == AST_OP_LOGICAL_XOR) {
        return TYPE_BOOL;
    }
    return TYPE_INT;
}

static int codegen_binary_expression(CodegenContext *ctx,
                                     const AstNode *expr, int dest_reg)
{
    AstOperator op = expr->data.binary_expression.operator;

    switch (op) {
    case AST_OP_ADD:
    case AST_OP_SUBTRACT:
    case AST_OP_MULTIPLY:
    case AST_OP_DIVIDE:
    case AST_OP_REMAINDER:
    case AST_OP_POWER:
    case AST_OP_LOGICAL_AND:
    case AST_OP_LOGICAL_OR:
    case AST_OP_LOGICAL_XOR:
        return codegen_binary_arithmetic(ctx, expr, dest_reg);
    case AST_OP_LESS:
    case AST_OP_LESS_EQUAL:
    case AST_OP_GREATER:
    case AST_OP_GREATER_EQUAL:
    case AST_OP_EQUAL:
    case AST_OP_NOT_EQUAL:
        return codegen_binary_comparison(ctx, expr, dest_reg);
    default:
        codegen_context_error(ctx,
            "operador binario no soportado (op=%d)", op);
        return TYPE_VOID;
    }
}

/* ----------------------------------------------------------------
 * Punto de entrada
 * ---------------------------------------------------------------- */

static int codegen_expression_impl(CodegenContext *ctx, const AstNode *expr,
                                   int dest_reg)
{
    if (ctx == NULL || expr == NULL) {
        return TYPE_VOID;
    }

    switch (expr->kind) {
    case AST_INTEGER_LITERAL:
        return codegen_integer_literal(ctx, expr, dest_reg);
    case AST_BOOLEAN_LITERAL:
        return codegen_boolean_literal(ctx, expr, dest_reg);
    case AST_IDENTIFIER_EXPRESSION:
        return codegen_identifier_load(ctx, expr, dest_reg);
    case AST_INDEX_EXPRESSION:
        if (!codegen_array_index_address(ctx, expr, dest_reg))
            return TYPE_VOID;
        emit_instruction(ctx, "lowi r%d, 0(r%d)", dest_reg, dest_reg);
        return expr->inferred_type;
    case AST_UNARY_EXPRESSION:
        return codegen_unary_expression(ctx, expr, dest_reg);
    case AST_BINARY_EXPRESSION:
        return codegen_binary_expression(ctx, expr, dest_reg);
    case AST_CALL_EXPRESSION:
        if (expr->inferred_type == TYPE_VOID) {
            codegen_context_error(ctx, "llamada sin valor usada como expresion");
            return TYPE_VOID;
        }
        return codegen_call(ctx, expr, dest_reg)
            ? expr->inferred_type : TYPE_VOID;
    default:
        codegen_context_error(ctx,
            "codegen_expression: nodo no implementado aun (kind=%d)",
            expr->kind);
        return TYPE_VOID;
    }
}

int codegen_expression(CodegenContext *ctx, const AstNode *expr, int dest_reg)
{
    int type;
    if (ctx == NULL || expr == NULL) return TYPE_VOID;
    ctx->expression_depth++;
    type = codegen_expression_impl(ctx, expr, dest_reg);
    ctx->expression_depth--;
    return type;
}
