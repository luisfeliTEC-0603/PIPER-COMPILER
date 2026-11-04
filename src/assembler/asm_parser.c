#include "asm_parser.h"

#include "encoder.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *start;
    const char *cursor;
} parser_t;

/* start fija el inicio de la línea; cursor avanza sin copiar el texto de entrada.
   Su diferencia permite señalar la columna exacta de un error (base 1). */
static porygon_parse_result_t result_at(porygon_parse_status_t status,
                                         const parser_t *parser,
                                         const char *position)
{
    porygon_parse_result_t result = {status, (size_t)(position - parser->start) + 1};
    return result;
}

static porygon_parse_result_t ok(const parser_t *parser)
{
    return result_at(PORYGON_PARSE_OK, parser, parser->cursor);
}

static void skip_space(parser_t *parser)
{
    /* Se admite espacio entre operandos y separadores; el llamador decide
       si después debe venir un registro, un número o un carácter concreto. */
    while (isspace((unsigned char)*parser->cursor)) {
        ++parser->cursor;
    }
}

static porygon_parse_result_t expect_char(parser_t *parser, char expected)
{
    /* Centraliza la lectura de comas, # y paréntesis para que todos los
       formatos señalen la misma clase de error cuando falta un separador. */
    skip_space(parser);
    if (*parser->cursor != expected) {
        return result_at(PORYGON_PARSE_EXPECTED_SEPARATOR, parser,
                         parser->cursor);
    }
    ++parser->cursor;
    return ok(parser);
}

static porygon_parse_result_t parse_register(parser_t *parser, uint8_t *out)
{
    const char *start;
    unsigned int value = 0;

    skip_space(parser);
    start = parser->cursor;
    if (*parser->cursor != 'r' ||
        !isdigit((unsigned char)parser->cursor[1])) {
        return result_at(PORYGON_PARSE_EXPECTED_REGISTER, parser, start);
    }

    /* La ISA dispone de r0..r7. Rechazar en cuanto el valor supera 7 evita
       acumular una secuencia decimal arbitrariamente larga en value. */
    ++parser->cursor;
    do {
        value = value * 10u + (unsigned int)(*parser->cursor - '0');
        ++parser->cursor;
        if (value > 7u) {
            return result_at(PORYGON_PARSE_OUT_OF_RANGE, parser, start);
        }
    } while (isdigit((unsigned char)*parser->cursor));

    *out = (uint8_t)value;
    return ok(parser);
}

static porygon_parse_result_t parse_number(parser_t *parser, int64_t *out)
{
    const char *start;
    const char *digits;
    char *end;
    long long value;
    int base;

    skip_space(parser);
    start = parser->cursor;
    digits = start;
    if (*digits == '+' || *digits == '-') {
        ++digits;
    }
    /* Sin prefijo 0x usamos decimal; un cero inicial no cambia la base. */
    base = (digits[0] == '0' &&
            (digits[1] == 'x' || digits[1] == 'X')) ? 16 : 10;
    if (base == 16 && !isxdigit((unsigned char)digits[2])) {
        return result_at(PORYGON_PARSE_EXPECTED_NUMBER, parser, start);
    }

    /* strtoll deja en end el primer carácter que no pertenece al número.
       Así el parser de cada formato puede exigir su siguiente separador y,
       al final, comprobar que no sobró texto. */
    errno = 0;
    value = strtoll(start, &end, base);
    if (end == start || end == digits) {
        return result_at(PORYGON_PARSE_EXPECTED_NUMBER, parser, start);
    }
    if (errno == ERANGE) {
        return result_at(PORYGON_PARSE_OUT_OF_RANGE, parser, start);
    }

    parser->cursor = end;
    *out = (int64_t)value;
    return ok(parser);
}

static porygon_parse_result_t parse_math(parser_t *parser,
                                          porygon_instruction_t *instruction)
{
    porygon_parse_result_t result;
    uint8_t rs1;
    uint8_t rs2 = 0;
    int64_t literal = 0;
    const char *literal_start;
    /* NOT y NEZ solo leen un registro. Las demás operaciones MATH reciben
       un segundo registro o un literal, según el bit imm del mnemónico. */
    bool unary = instruction->funct == PORYGON_MATH_NOT ||
                 instruction->funct == PORYGON_MATH_NEZ;

    result = parse_register(parser, &rs1);
    if (result.status != PORYGON_PARSE_OK) {
        return result;
    }

    if (!unary) {
        result = expect_char(parser, ',');
        if (result.status != PORYGON_PARSE_OK) {
            return result;
        }
        if (instruction->immediate) {
            result = expect_char(parser, '#');
            if (result.status != PORYGON_PARSE_OK) {
                return result;
            }
            skip_space(parser);
            literal_start = parser->cursor;
            result = parse_number(parser, &literal);
            if (result.status != PORYGON_PARSE_OK) {
                return result;
            }
            /* Se guarda el valor completo: la colocación del slot literal
               corresponde a una etapa posterior. Aceptamos un entero con
               signo o un patrón de 16 bits escrito como valor positivo. */
            if (literal < -32768 || literal > 65535) {
                return result_at(PORYGON_PARSE_OUT_OF_RANGE, parser,
                                 literal_start);
            }
        } else {
            result = parse_register(parser, &rs2);
            if (result.status != PORYGON_PARSE_OK) {
                return result;
            }
        }
    }

    instruction->operands.math.rs1 = rs1;
    instruction->operands.math.rs2 = rs2;
    instruction->operands.math.literal = literal;
    return ok(parser);
}

static porygon_parse_result_t parse_lost(parser_t *parser,
                                          porygon_instruction_t *instruction)
{
    porygon_parse_result_t result;
    uint8_t rd;
    uint8_t rs;
    int64_t offset;
    const char *offset_start;

    /* Sintaxis común de carga y almacenamiento: operando, offset(base).
       El mnemónico determina el papel real del primer registro y si el
       desplazamiento usa el campo corto o un slot literal posterior. */
    result = parse_register(parser, &rd);
    if (result.status != PORYGON_PARSE_OK) {
        return result;
    }
    result = expect_char(parser, ',');
    if (result.status != PORYGON_PARSE_OK) {
        return result;
    }
    skip_space(parser);
    offset_start = parser->cursor;
    result = parse_number(parser, &offset);
    if (result.status != PORYGON_PARSE_OK) {
        return result;
    }
    if (instruction->immediate) {
        /* LOST especifica un desplazamiento largo con signo de 16 bits. */
        if (offset < -32768 || offset > 32767) {
            return result_at(PORYGON_PARSE_OUT_OF_RANGE, parser,
                             offset_start);
        }
    } else if (offset < -4 || offset > 3) {
        /* El campo corto off tiene tres bits en complemento a dos. */
        return result_at(PORYGON_PARSE_OUT_OF_RANGE, parser,
                         offset_start);
    }
    result = expect_char(parser, '(');
    if (result.status != PORYGON_PARSE_OK) {
        return result;
    }
    result = parse_register(parser, &rs);
    if (result.status != PORYGON_PARSE_OK) {
        return result;
    }
    result = expect_char(parser, ')');
    if (result.status != PORYGON_PARSE_OK) {
        return result;
    }

    instruction->operands.lost.rd = rd;
    instruction->operands.lost.rs = rs;
    instruction->operands.lost.offset = offset;
    return ok(parser);
}

static porygon_parse_result_t parse_jump(parser_t *parser,
                                          porygon_instruction_t *instruction)
{
    porygon_parse_result_t result;
    uint8_t rd = 0;
    int64_t offset = 0;
    const char *offset_start;

    /* JR toma un registro; J toma un desplazamiento; los saltos
       condicionales toman ambos, separados por coma. */
    if (instruction->funct == PORYGON_JUMP_JR) {
        result = parse_register(parser, &rd);
        if (result.status != PORYGON_PARSE_OK) {
            return result;
        }
    } else {
        if (instruction->funct != PORYGON_JUMP_J) {
            result = parse_register(parser, &rd);
            if (result.status != PORYGON_PARSE_OK) {
                return result;
            }
            result = expect_char(parser, ',');
            if (result.status != PORYGON_PARSE_OK) {
                return result;
            }
        }
        /* Se conserva el número escrito: este parser no conoce la posición
           final del bundle y todavía no lo convierte a bits de JUMP. */
        skip_space(parser);
        offset_start = parser->cursor;
        result = parse_number(parser, &offset);
        if (result.status != PORYGON_PARSE_OK) {
            return result;
        }
        /* El campo corto tiene ocho bits con signo; ji/jzi/jnzi consumen un
           literal de dieciséis bits también con signo. El valor se verifica
           aquí para que el encoder no descubra un salto imposible más tarde. */
        if ((instruction->immediate && (offset < -32768 || offset > 32767)) ||
            (!instruction->immediate && (offset < -128 || offset > 127))) {
            return result_at(PORYGON_PARSE_OUT_OF_RANGE, parser, offset_start);
        }
    }

    instruction->operands.jump.rd = rd;
    instruction->operands.jump.offset = offset;
    return ok(parser);
}

static porygon_parse_result_t parse_crypto_index(parser_t *parser, uint8_t *out)
{
    porygon_parse_result_t result;
    int64_t value;
    const char *start;

    skip_space(parser);
    start = parser->cursor;
    result = parse_number(parser, &value);
    if (result.status != PORYGON_PARSE_OK) {
        return result;
    }
    /* La ISA reserva dos bits para cada índice. El ASM usa directamente
       valores 0..3, sin restar uno al número escrito por el programador. */
    if (value < 0 || value > 3) {
        return result_at(PORYGON_PARSE_OUT_OF_RANGE, parser, start);
    }

    *out = (uint8_t)value;
    return ok(parser);
}

static porygon_parse_result_t parse_crypto(parser_t *parser,
                                            porygon_instruction_t *instruction)
{
    porygon_parse_result_t result;
    uint8_t rs1 = 0;
    uint8_t rs2 = 0;
    uint8_t item_index = 0;
    uint8_t key_index = 0;

    /* AUTH, PWD y RVK no leen operandos. El control final de texto sobrante
       rechazará cualquier registro o índice que se les agregue por error. */
    switch (instruction->funct) {
    case PORYGON_CRYPTO_AUTH:
    case PORYGON_CRYPTO_PWD:
    case PORYGON_CRYPTO_RVK:
        return ok(parser);
    case PORYGON_CRYPTO_WRITE:
    case PORYGON_CRYPTO_FEISTEL:
    case PORYGON_CRYPTO_LOPW:
        break;
    default:
        return result_at(PORYGON_PARSE_UNSUPPORTED_FORMAT, parser,
                         parser->cursor);
    }

    result = parse_register(parser, &rs1);
    if (result.status != PORYGON_PARSE_OK) {
        return result;
    }
    if (instruction->funct == PORYGON_CRYPTO_FEISTEL) {
        /* FEISTEL es la única forma que también utiliza rs2. */
        result = expect_char(parser, ',');
        if (result.status != PORYGON_PARSE_OK) {
            return result;
        }
        result = parse_register(parser, &rs2);
        if (result.status != PORYGON_PARSE_OK) {
            return result;
        }
    }

    result = expect_char(parser, ',');
    if (result.status != PORYGON_PARSE_OK) {
        return result;
    }
    result = parse_crypto_index(parser, &item_index);
    if (result.status != PORYGON_PARSE_OK) {
        return result;
    }

    if (instruction->funct != PORYGON_CRYPTO_LOPW) {
        /* En WRITE el índice anterior es la subllave; en FEISTEL, la ronda.
           Ambos terminan con el índice de llave. LOPW solo usa la palabra. */
        result = expect_char(parser, ',');
        if (result.status != PORYGON_PARSE_OK) {
            return result;
        }
        result = parse_crypto_index(parser, &key_index);
        if (result.status != PORYGON_PARSE_OK) {
            return result;
        }
    }

    instruction->operands.crypto.rs1 = rs1;
    instruction->operands.crypto.rs2 = rs2;
    instruction->operands.crypto.item_index = item_index;
    instruction->operands.crypto.key_index = key_index;
    return ok(parser);
}

porygon_parse_result_t porygon_parse_instruction(const char *line,
                                                  porygon_instruction_t *out)
{
    parser_t parser;
    porygon_instruction_t parsed = {0};
    porygon_parse_result_t result;
    const porygon_mnemonic_t *mnemonic;
    const char *name_start;
    size_t name_length;
    size_t index;
    char name[32];

    if (line == NULL || out == NULL) {
        porygon_parse_result_t invalid = {PORYGON_PARSE_INVALID_ARGUMENT, 0};
        return invalid;
    }

    parser.start = line;
    parser.cursor = line;
    skip_space(&parser);
    if (*parser.cursor == '\0') {
        return result_at(PORYGON_PARSE_EMPTY, &parser, parser.cursor);
    }

    /* El mnemónico se copia a un búfer acotado porque la tabla de la ISA
       busca cadenas terminadas en NUL, no tramos de la línea original. */
    name_start = parser.cursor;
    while (isalpha((unsigned char)*parser.cursor)) {
        ++parser.cursor;
    }
    name_length = (size_t)(parser.cursor - name_start);
    if (name_length == 0 || name_length >= sizeof(name)) {
        return result_at(PORYGON_PARSE_UNKNOWN_MNEMONIC, &parser, name_start);
    }
    memcpy(name, name_start, name_length);
    name[name_length] = '\0';
    /* La tabla conserva nombres en minúsculas. Normalizar aquí permite usar
       también LOPW, como aparece escrito en la ISA, y cualquier otro mnemónico
       en mayúsculas sin duplicar entradas en la tabla. */
    for (index = 0; index < name_length; ++index) {
        name[index] = (char)tolower((unsigned char)name[index]);
    }
    mnemonic = porygon_find_mnemonic(name);
    if (mnemonic == NULL) {
        return result_at(PORYGON_PARSE_UNKNOWN_MNEMONIC, &parser, name_start);
    }

    /* La tabla concentra la relación nombre -> formato/funct/imm. Cada rama
       solo interpreta los operandos que corresponden a ese formato. */
    parsed.format = mnemonic->format;
    parsed.funct = mnemonic->funct;
    parsed.immediate = mnemonic->immediate;
    switch (parsed.format) {
    case PORYGON_FORMAT_NOP:
        result = ok(&parser);
        break;
    case PORYGON_FORMAT_MATH:
        result = parse_math(&parser, &parsed);
        break;
    case PORYGON_FORMAT_LOST:
        result = parse_lost(&parser, &parsed);
        break;
    case PORYGON_FORMAT_JUMP:
        result = parse_jump(&parser, &parsed);
        break;
    case PORYGON_FORMAT_CRYPTO:
        result = parse_crypto(&parser, &parsed);
        break;
    default:
        return result_at(PORYGON_PARSE_UNKNOWN_MNEMONIC, &parser, name_start);
    }
    if (result.status != PORYGON_PARSE_OK) {
        return result;
    }
    skip_space(&parser);
    if (*parser.cursor != '\0') {
        return result_at(PORYGON_PARSE_TRAILING_TEXT, &parser, parser.cursor);
    }

    /* parsed es temporal: si hay error, out conserva su valor anterior.
       Solo se publica la instrucción cuando toda la línea fue validada. */
    *out = parsed;
    return ok(&parser);
}
