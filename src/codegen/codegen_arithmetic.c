#include "codegen_internal.h"

/* Algoritmos en línea: r1=dividendo/base, r2=divisor/exponente,
 * r3=residuo/acumulador, r4/r5=auxiliares, r6=contador/auxiliar.
 * El punto de entrada conserva registros vivos y libera temporales al evaluar
 * operandos; no modifica el frame pointer ni necesita CALL/RET en la ISA. */

static int unsigned_type(PiperType type)
{
    return type == PIPER_TYPE_UB8 || type == PIPER_TYPE_UB16 ||
           type == PIPER_TYPE_UB32;
}

/* El semántico deja sin ancho los árboles de literales. Recuperar su valor
 * distingue 4294967295 de -1 y permite resultados constantes uB32 sin
 * convertir por error el bit 31 en un signo. Solo admite árboles sin efectos. */
static int constant_integer(const AstNode *node, long long *value)
{
    long long left, right;
    unsigned long long magnitude;
    int negative;
    if (node->kind == AST_INTEGER_LITERAL) {
        if (node->data.integer_literal.value > 4294967295ULL) return 0;
        *value = (long long)node->data.integer_literal.value;
        return 1;
    }
    if (node->kind == AST_UNARY_EXPRESSION &&
        node->data.unary_expression.operator == AST_OP_NEGATE &&
        constant_integer(node->data.unary_expression.operand, value)) {
        *value = -*value;
        return 1;
    }
    if (node->kind != AST_BINARY_EXPRESSION ||
        !constant_integer(node->data.binary_expression.left, &left) ||
        !constant_integer(node->data.binary_expression.right, &right)) return 0;
    switch (node->data.binary_expression.operator) {
    case AST_OP_ADD: *value = left + right; break;
    case AST_OP_SUBTRACT: *value = left - right; break;
    case AST_OP_DIVIDE:
        if (right == 0) return 0;
        *value = left / right;
        break;
    case AST_OP_REMAINDER:
        if (right == 0) return 0;
        *value = left % right;
        break;
    case AST_OP_MULTIPLY:
    case AST_OP_POWER: {
        unsigned long long base = (unsigned long long)(left < 0 ? -left : left);
        if (node->data.binary_expression.operator == AST_OP_MULTIPLY) {
            magnitude = base * (unsigned long long)(right < 0 ? -right : right);
            negative = (left < 0) != (right < 0);
        } else {
            if (right < 0) return 0;
            negative = left < 0 && (right & 1);
            magnitude = 1;
            while (right) {
                if (right & 1) magnitude *= base;
                if (magnitude > 4294967295ULL) return 0;
                right >>= 1;
                if (right) {
                    base *= base;
                    if (base > 4294967295ULL) return 0;
                }
            }
        }
        if (magnitude > 4294967295ULL) return 0;
        *value = negative ? -(long long)magnitude : (long long)magnitude;
        break;
    }
    default: return 0;
    }
    return *value >= -2147483648LL && *value <= 4294967295LL;
}

static int unsigned_operand(const AstNode *node)
{
    long long value;
    if (node->has_piper_type) return unsigned_type(node->piper_type);
    return constant_integer(node, &value) && value > 2147483647LL;
}

int codegen_expression_is_unsigned(const AstNode *expr)
{
    return unsigned_operand(expr);
}

static PiperType result_type(const AstNode *expr)
{
    if (expr->has_piper_type) return expr->piper_type;
    return unsigned_operand(expr)
        ? PIPER_TYPE_UB32 : PIPER_TYPE_B32;
}

static int integer_bits(PiperType type)
{
    if (type == PIPER_TYPE_B8 || type == PIPER_TYPE_UB8) return 8;
    if (type == PIPER_TYPE_B16 || type == PIPER_TYPE_UB16) return 16;
    return 32;
}

/* r1 contiene una magnitud y r4 su máscara de signo (0 o -1).
 * Comparar antes de negar también detecta INT_MIN / -1. ltn/gtn en RTL
 * comparan sin signo, por lo que las magnitudes conservan los 32 bits. */
static void check_magnitude(CodegenContext *ctx, PiperType type)
{
    int bits = integer_bits(type);
    if (unsigned_type(type)) {
        emit_instruction(ctx, "jnz r4, error_aritmetico");
        if (bits == 32) return;
        load_immediate(ctx, 5, (1LL << bits) - 1);
    } else {
        load_immediate(ctx, 5, (1LL << (bits - 1)) - 1);
        emit_instruction(ctx, "diff r5, r4");
    }
    emit_instruction(ctx, "mov r6, r1");
    emit_instruction(ctx, "gtn r6, r5");
    emit_instruction(ctx, "jnz r6, error_aritmetico");
}

static void emit_divmod(CodegenContext *ctx, const AstNode *expr, int reserved)
{
    int id = new_label(ctx);
    int remainder = expr->data.binary_expression.operator == AST_OP_REMAINDER;
    int left_unsigned = unsigned_operand(expr->data.binary_expression.left);
    int right_unsigned = unsigned_operand(expr->data.binary_expression.right);
    int needs_sign = !left_unsigned || (!remainder && !right_unsigned);
    char loop[40], subtract[40], next[40];
    snprintf(loop, sizeof(loop), "division_%d", id);
    snprintf(subtract, sizeof(subtract), "division_restar_%d", id);
    snprintf(next, sizeof(next), "division_siguiente_%d", id);

    emit_instruction(ctx, "jz r2, error_division_cero");
    if (left_unsigned && needs_sign) emit_instruction(ctx, "mov r4, r0");
    if (!left_unsigned) {
        emit_instruction(ctx, "mov r4, r1");
        emit_instruction(ctx, "sari r4, #31");
        emit_instruction(ctx, "xor r1, r4");
        emit_instruction(ctx, "diff r1, r4");
    }
    if (!right_unsigned) {
        emit_instruction(ctx, "mov r5, r2");
        emit_instruction(ctx, "sari r5, #31");
        emit_instruction(ctx, "xor r2, r5");
        emit_instruction(ctx, "diff r2, r5");
    }
    if (!remainder && !right_unsigned) emit_instruction(ctx, "xor r4, r5");
    if (needs_sign)
        emit_instruction(ctx, "stwi r4, %d(r7)", -reserved + 28);
    emit_instruction(ctx, "mov r3, r0");
    load_immediate(ctx, 6, 32);

    /* División restauradora: r1 pasa de dividendo a cociente, r3 es el
     * residuo. El bit extra de r3 obliga a restar incluso si el shift desborda. */
    emit_label(ctx, loop);
    emit_instruction(ctx, "mov r5, r3");
    emit_instruction(ctx, "shri r5, #31");
    emit_instruction(ctx, "mov r4, r1");
    emit_instruction(ctx, "shri r4, #31");
    emit_instruction(ctx, "shli r1, #1");
    emit_instruction(ctx, "shli r3, #1");
    emit_instruction(ctx, "or r3, r4");
    emit_instruction(ctx, "jnz r5, %s", subtract);
    emit_instruction(ctx, "mov r4, r3");
    emit_instruction(ctx, "ltn r4, r2");
    emit_instruction(ctx, "jnz r4, %s", next);
    emit_label(ctx, subtract);
    emit_instruction(ctx, "diff r3, r2");
    emit_instruction(ctx, "sumi r1, #1");
    emit_label(ctx, next);
    emit_instruction(ctx, "diffi r6, #1");
    emit_instruction(ctx, "jnz r6, %s", loop);

    if (remainder) emit_instruction(ctx, "mov r1, r3");
    if (needs_sign) emit_instruction(ctx, "lowi r4, %d(r7)", -reserved + 28);
    /* Un residuo cero no conserva signo negativo. */
    if (needs_sign) {
        char sign[40];
        snprintf(sign, sizeof(sign), "division_signo_%d", id);
        emit_instruction(ctx, "jnz r1, %s", sign);
        emit_instruction(ctx, "mov r4, r0");
        emit_label(ctx, sign);
    }
    if (needs_sign || result_type(expr) != PIPER_TYPE_UB32) {
        if (!needs_sign) emit_instruction(ctx, "mov r4, r0");
        check_magnitude(ctx, result_type(expr));
    }
    if (needs_sign) {
        emit_instruction(ctx, "xor r1, r4");
        emit_instruction(ctx, "diff r1, r4");
    }
}

/* Camino corto para divisores literales positivos que son potencias de dos.
 * Si no queda un temporal, se usa el algoritmo general sin evaluar dos veces. */
static int power_two_divmod(CodegenContext *ctx, const AstNode *expr, int dest)
{
    const AstNode *left = expr->data.binary_expression.left;
    const AstNode *right = expr->data.binary_expression.right;
    unsigned long long divisor;
    unsigned shift = 0;
    int is_unsigned = unsigned_operand(left);
    int remainder = expr->data.binary_expression.operator == AST_OP_REMAINDER;
    int temp;
    if (right->kind != AST_INTEGER_LITERAL) return 0;
    divisor = right->data.integer_literal.value;
    if (divisor == 0 || divisor > (1ULL << 31) ||
        (divisor & (divisor - 1)) != 0) return 0;
    while ((1ULL << shift) != divisor) shift++;
    if (!is_unsigned && shift == 31) return 0;
    if (!is_unsigned && shift != 0 && ctx->temp_in_use == 3) return 0;
    if (codegen_expression(ctx, left, dest) == TYPE_VOID) return -1;
    if (shift == 0) {
        if (remainder) emit_instruction(ctx, "mov r%d, r0", dest);
        return 1;
    }
    temp = -1;
    if (!is_unsigned) {
        temp = alloc_temp(ctx);
        if (temp < 0) return -1;
        emit_instruction(ctx, "mov r%d, r%d", temp, dest);
        emit_instruction(ctx, "sari r%d, #31", temp);
        if (remainder) {
            emit_instruction(ctx, "xor r%d, r%d", dest, temp);
            emit_instruction(ctx, "diff r%d, r%d", dest, temp);
        } else {
            emit_instruction(ctx, "shri r%d, #%u", temp, 32 - shift);
            emit_instruction(ctx, "sum r%d, r%d", dest, temp);
        }
    }
    if (remainder) {
        if (shift <= 15)
            emit_instruction(ctx, "andi r%d, #%llu", dest, divisor - 1);
        else {
            emit_instruction(ctx, "shli r%d, #%u", dest, 32 - shift);
            emit_instruction(ctx, "shri r%d, #%u", dest, 32 - shift);
        }
        if (!is_unsigned) {
            emit_instruction(ctx, "xor r%d, r%d", dest, temp);
            emit_instruction(ctx, "diff r%d, r%d", dest, temp);
        }
    } else {
        emit_instruction(ctx, "%s r%d, #%u", is_unsigned ? "shri" : "sari",
                         dest, shift);
    }
    if (temp >= 0) free_temp(ctx, temp);
    return 1;
}

/* Multiplica dest por factor, conservando factor. mulh devuelve la mitad
 * alta firmada; para uB32 se corrige sumando los operandos con bit 31 activo.
 * r4/r5/r6 son auxiliares; r2 conserva el exponente. */
static void checked_multiply(CodegenContext *ctx, int dest, int factor,
                             PiperType type)
{
    int bits = integer_bits(type);
    emit_instruction(ctx, "mov r5, r%d", dest);
    emit_instruction(ctx, "mulh r5, r%d", factor);
    if (unsigned_type(type)) {
        emit_instruction(ctx, "mov r4, r%d", dest);
        emit_instruction(ctx, "sari r4, #31");
        emit_instruction(ctx, "and r4, r%d", factor);
        emit_instruction(ctx, "sum r5, r4");
        emit_instruction(ctx, "mov r4, r%d", factor);
        emit_instruction(ctx, "sari r4, #31");
        emit_instruction(ctx, "and r4, r%d", dest);
        emit_instruction(ctx, "sum r5, r4");
    }
    emit_instruction(ctx, "mul r%d, r%d", dest, factor);
    if (!unsigned_type(type)) {
        emit_instruction(ctx, "mov r4, r%d", dest);
        emit_instruction(ctx, "sari r4, #31");
        emit_instruction(ctx, "xor r5, r4");
    }
    emit_instruction(ctx, "jnz r5, error_aritmetico");
    if (bits < 32) {
        emit_instruction(ctx, "mov r4, r%d", dest);
        if (unsigned_type(type)) {
            emit_instruction(ctx, "shri r4, #%d", bits);
            emit_instruction(ctx, "jnz r4, error_aritmetico");
        } else {
            emit_instruction(ctx, "shli r4, #%d", 32 - bits);
            emit_instruction(ctx, "sari r4, #%d", 32 - bits);
            emit_instruction(ctx, "eor r4, r%d", dest);
            emit_instruction(ctx, "jz r4, error_aritmetico");
        }
    }
}

static void emit_power(CodegenContext *ctx, const AstNode *expr)
{
    int id = new_label(ctx);
    char loop[40], square[40], end[40];
    PiperType type = result_type(expr);
    snprintf(loop, sizeof(loop), "potencia_%d", id);
    snprintf(square, sizeof(square), "potencia_cuadrado_%d", id);
    snprintf(end, sizeof(end), "potencia_fin_%d", id);
    if (!unsigned_operand(expr->data.binary_expression.right)) {
        emit_instruction(ctx, "mov r4, r2");
        emit_instruction(ctx, "sari r4, #31");
        emit_instruction(ctx, "jnz r4, error_exponente_negativo");
    }
    if (unsigned_type(type) &&
        !unsigned_operand(expr->data.binary_expression.left)) {
        char magnitude[40];
        snprintf(magnitude, sizeof(magnitude), "potencia_magnitud_%d", id);
        emit_instruction(ctx, "mov r4, r1");
        emit_instruction(ctx, "sari r4, #31");
        emit_instruction(ctx, "jz r4, %s", magnitude);
        emit_instruction(ctx, "mov r5, r2");
        emit_instruction(ctx, "andi r5, #1");
        emit_instruction(ctx, "jnz r5, error_aritmetico");
        /* Un árbol de literales puede producir uB32 con base negativa y
         * exponente par, por ejemplo (-3)^20. Multiplicar su magnitud. */
        emit_instruction(ctx, "xor r1, r4");
        emit_instruction(ctx, "diff r1, r4");
        emit_label(ctx, magnitude);
    }
    load_immediate(ctx, 3, 1);
    emit_label(ctx, loop);
    emit_instruction(ctx, "jz r2, %s", end);
    emit_instruction(ctx, "mov r4, r2");
    emit_instruction(ctx, "andi r4, #1");
    emit_instruction(ctx, "jz r4, %s", square);
    checked_multiply(ctx, 3, 1, type);
    emit_label(ctx, square);
    emit_instruction(ctx, "shri r2, #1");
    /* No calcular un cuadrado que ya no se usará: puede desbordar aunque
     * la potencia solicitada sea válida (por ejemplo, (-2)^31). */
    emit_instruction(ctx, "jz r2, %s", end);
    checked_multiply(ctx, 1, 1, type);
    emit_instruction(ctx, "j %s", loop);
    emit_label(ctx, end);
    emit_instruction(ctx, "mov r1, r3");
}

int codegen_general_arithmetic(CodegenContext *ctx, const AstNode *expr,
                               int dest_reg)
{
    const AstNode *left = expr->data.binary_expression.left;
    const AstNode *right = expr->data.binary_expression.right;
    unsigned int saved_temps = ctx->temp_in_use;
    unsigned int save_mask;
    long long constant;
    int reserved;
    int ok = 0;
    if (left == NULL || right == NULL || dest_reg < 1 || dest_reg > REG_TMP1) {
        codegen_context_error(ctx, "operandos o destino aritmetico invalidos");
        return TYPE_VOID;
    }
    /* Solo árboles de literales: no se eliminan llamadas ni efectos de ++. */
    if (constant_integer(expr, &constant))
        return load_immediate(ctx, dest_reg, constant) ? TYPE_INT : TYPE_VOID;
    if (expr->data.binary_expression.operator != AST_OP_POWER) {
        int fast = power_two_divmod(ctx, expr, dest_reg);
        if (fast != 0) return fast > 0 ? TYPE_INT : TYPE_VOID;
    }
    if (ctx->pending_call_bytes > ctx->call_reserve_size - CALL_RECORD_SIZE) {
        codegen_context_error(ctx, "reserva de aritmetica y llamadas agotada");
        return TYPE_VOID;
    }
    ctx->pending_call_bytes += CALL_RECORD_SIZE;
    reserved = ctx->pending_call_bytes;
    /* Misma reserva de 32 bytes que las llamadas, sin mover r7: los
     * operandos siguen accediendo al frame actual. Las llamadas y operaciones
     * anidadas usan una ranura más profunda y no pisan estos registros. */
    /* En la expresión exterior r1..r4 no contienen valores pendientes.
     * Dentro de otra expresión o de argumentos se conservan por precaución.
     * Los temporales solo se guardan si están ocupados; dest será reemplazado. */
    save_mask = (ctx->expression_depth > 1 || reserved > CALL_RECORD_SIZE)
        ? 0x1eu : 0;
    save_mask |= saved_temps << REG_TMP0;
    save_mask &= ~(1u << dest_reg);
    for (int reg = REG_RET; reg <= REG_TMP1; reg++) {
        if (save_mask & (1u << reg))
            emit_instruction(ctx, "stwi r%d, %d(r7)", reg, -reserved + reg * 4);
    }
    reset_temps(ctx);
    if (codegen_expression(ctx, left, 1) == TYPE_VOID) goto done;
    emit_instruction(ctx, "stwi r1, %d(r7)", -reserved + 28);
    if (codegen_expression(ctx, right, 2) == TYPE_VOID) goto done;
    emit_instruction(ctx, "lowi r1, %d(r7)", -reserved + 28);

    if (expr->data.binary_expression.operator == AST_OP_POWER)
        emit_power(ctx, expr);
    else
        emit_divmod(ctx, expr, reserved);
    if (dest_reg != REG_RET) emit_instruction(ctx, "mov r%d, r1", dest_reg);
    for (int reg = REG_RET; reg <= REG_TMP1; reg++) {
        if (save_mask & (1u << reg))
            emit_instruction(ctx, "lowi r%d, %d(r7)", reg, -reserved + reg * 4);
    }
    ok = 1;
done:
    ctx->temp_in_use = saved_temps;
    ctx->pending_call_bytes -= CALL_RECORD_SIZE;
    return ok ? TYPE_INT : TYPE_VOID;
}
