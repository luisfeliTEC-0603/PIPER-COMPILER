#include "internal.h"

#include <stdlib.h>

/** Construye un diagnóstico sin memoria dinámica. */
porygon_schedule_result_t schedule_result(porygon_schedule_status_t status,
    size_t instruction, size_t bundle, size_t slot, const char *message)
{
    return (porygon_schedule_result_t){status, instruction, bundle, slot, message};
}

/**
 * Copia una instrucción y reconstruye uses/defs, recurso y tamaño desde sus operandos.
 * Así la planificación no hereda máscaras antiguas si otro productor cambió la instrucción.
 */
porygon_machine_instruction_t schedule_describe(const porygon_machine_instruction_t *instruction)
{
    porygon_machine_instruction_t described = *instruction;
    porygon_describe_instruction(&described);
    return described;
}

/**
 * Devuelve la distancia hasta WB: cuatro para MUL/MULH y dos para resultados rápidos.
 * No indica que toda instrucción escriba: el llamador consulta defs o el tipo de
 * productor antes de usarla. Un consumidor RAW necesita además el ciclo siguiente.
 */
size_t schedule_wb_latency(const porygon_instruction_t *instruction)
{
    if (instruction->format == PORYGON_FORMAT_MATH &&
        (instruction->funct == PORYGON_MATH_MUL || instruction->funct == PORYGON_MATH_MULH)) {
        return 4;
    }
    return 2;
}

/**
 * Traduce operandos reconocidos a un slot de instrucción y un posible literal.
 * No coloca slots ni resuelve etiquetas; el literal de un salto simbólico es provisional.
 * word y literal solo deben consumirse cuando se devuelve ENCODE_OK.
 */
porygon_encode_result_t schedule_encode(const porygon_instruction_t *in,
    unsigned int pipe, uint16_t *word, uint16_t *literal)
{
    *literal = 0;
    if (porygon_mnemonic_name(in->format, in->funct, in->immediate) == NULL ||
        (in->format != PORYGON_FORMAT_MATH && pipe != 0)) {
        return PORYGON_ENCODE_INVALID_FORM;
    }
    /* Los rangos se comprueban antes de convertir a 16 bits: truncar primero
       ocultaría un operando inválido detrás de una codificación aparentemente válida. */
    switch (in->format) {
    case PORYGON_FORMAT_NOP:
        *word = 0;
        return PORYGON_ENCODE_OK;
    case PORYGON_FORMAT_MATH:
        if (in->operands.math.literal < -32768 || in->operands.math.literal > 65535 ||
            (!in->immediate && in->operands.math.literal != 0)) {
            return PORYGON_ENCODE_OUT_OF_RANGE;
        }
        *literal = (uint16_t)in->operands.math.literal;
        return porygon_encode_math_pipe((porygon_math_op_t)in->funct,
            in->operands.math.rs1, in->operands.math.rs2, in->immediate, pipe, word);
    case PORYGON_FORMAT_LOST:
        if (in->operands.lost.offset < (in->immediate ? -32768 : -4) ||
            in->operands.lost.offset > (in->immediate ? 32767 : 3)) {
            return PORYGON_ENCODE_OUT_OF_RANGE;
        }
        *literal = (uint16_t)in->operands.lost.offset;
        /* En la forma larga el desplazamiento vive en el literal, por lo que
           el campo corto reservado dentro de la instrucción debe quedar en cero. */
        return porygon_encode_lost((porygon_lost_op_t)in->funct,
            in->operands.lost.rd, in->operands.lost.rs,
            in->immediate ? 0 : (int)in->operands.lost.offset, in->immediate, word);
    case PORYGON_FORMAT_JUMP:
        if (in->operands.jump.offset < (in->immediate ? -32768 : -128) ||
            in->operands.jump.offset > (in->immediate ? 32767 : 127)) {
            return PORYGON_ENCODE_OUT_OF_RANGE;
        }
        *literal = (uint16_t)in->operands.jump.offset;
        return porygon_encode_jump((porygon_jump_op_t)in->funct,
            in->operands.jump.rd,
            in->immediate ? 0 : ((int)in->operands.jump.offset & 255), in->immediate, word);
    case PORYGON_FORMAT_CRYPTO:
        return porygon_encode_crypto((porygon_crypto_op_t)in->funct,
            in->operands.crypto.rs1, in->operands.crypto.rs2,
            in->operands.crypto.item_index, in->operands.crypto.key_index, word);
    }
    return PORYGON_ENCODE_INVALID_OPERATION;
}

/**
 * Sigue valores conocidos en el orden original para comprobar direcciones locales.
 * known[r] distingue un cero demostrado de un registro cuyo valor se desconoce.
 * Esta evaluación no elimina ni reemplaza instrucciones del programa.
 */
static void update_constants(const porygon_instruction_t *in, bool known[8], uint32_t value[8])
{
    porygon_machine_instruction_t described = {.instruction = *in};
    unsigned int r, a, b;
    uint32_t x, y, answer = 0;
    bool valid;
    porygon_describe_instruction(&described);
    /* Un load puede sobrescribir una constante con un dato desconocido. Mantener
       el valor anterior produciría un diagnóstico de dirección falso más adelante. */
    if (in->format != PORYGON_FORMAT_MATH) {
        for (r = 1; r < 8; ++r) {
            if (described.defs & (1u << r)) known[r] = false;
        }
        return;
    }
    a = in->operands.math.rs1;
    b = in->operands.math.rs2;
    x = value[a];
    y = value[b];
    /* MOV no necesita el valor previo de su destino; las operaciones de dos
       direcciones sí. Los unarios y los inmediatos tampoco leen rs2. */
    valid = (in->funct == PORYGON_MATH_MOV || known[a]) &&
        (in->immediate || in->funct == PORYGON_MATH_NOT ||
         in->funct == PORYGON_MATH_NEZ || known[b]);
    if (in->immediate) {
        uint32_t bits = (uint16_t)in->operands.math.literal;
        bool logical = in->funct >= PORYGON_MATH_AND && in->funct <= PORYGON_MATH_XOR;
        /* 0xFFFF significa 65535 en una operación lógica y -1 en una aritmética. */
        y = !logical && (bits & 0x8000u) ? bits | UINT32_C(0xffff0000) : bits;
    }
    if (valid) {
        switch (in->funct) {
        case PORYGON_MATH_MOV: answer = y; break;
        case PORYGON_MATH_SUM: answer = x + y; break;
        case PORYGON_MATH_DIFF: answer = x - y; break;
        case PORYGON_MATH_AND: answer = x & y; break;
        case PORYGON_MATH_OR: answer = x | y; break;
        case PORYGON_MATH_XOR: answer = x ^ y; break;
        case PORYGON_MATH_NOT: answer = ~x; break;
        case PORYGON_MATH_SHL: answer = x << (y & 31); break;
        case PORYGON_MATH_SHR: answer = x >> (y & 31); break;
        case PORYGON_MATH_SAR:
            /* Reconstruir el relleno de signo evita depender del shift derecho
               de enteros negativos de la implementación de C. */
            answer = x >> (y & 31);
            if ((x & UINT32_C(0x80000000)) && (y & 31)) {
                answer |= UINT32_MAX << (32 - (y & 31));
            }
            break;
        case PORYGON_MATH_MUL: answer = x * y; break;
        case PORYGON_MATH_MULH: {
            /* Los operandos se extienden a 64 bits con signo antes del producto;
               el cast final permite extraer el patrón de sus 32 bits superiores. */
            int64_t sx = x >= UINT32_C(0x80000000) ? (int64_t)x - INT64_C(4294967296) : x;
            int64_t sy = y >= UINT32_C(0x80000000) ? (int64_t)y - INT64_C(4294967296) : y;
            answer = (uint32_t)((uint64_t)(sx * sy) >> 32);
            break;
        }
        case PORYGON_MATH_GTN: answer = x > y; break;
        case PORYGON_MATH_LTN: answer = x < y; break;
        case PORYGON_MATH_EOR: answer = x == y; break;
        case PORYGON_MATH_NEZ: answer = x != 0; break;
        default: valid = false; break;
        }
    }
    if (a != 0) {
        known[a] = valid;
        value[a] = answer;
    }
}

/** Rechaza solo direcciones demostrablemente inválidas; las dinámicas son del runtime. */
static bool valid_constant_address(const porygon_instruction_t *in,
                                   const bool known[8], const uint32_t value[8])
{
    if (in->format == PORYGON_FORMAT_LOST && known[in->operands.lost.rs]) {
        uint32_t address = value[in->operands.lost.rs] + (uint32_t)in->operands.lost.offset;
        unsigned int size = 1;
        if (in->funct == PORYGON_LOST_LOW || in->funct == PORYGON_LOST_STW) size = 4;
        if (in->funct == PORYGON_LOST_LOH || in->funct == PORYGON_LOST_LOHU ||
            in->funct == PORYGON_LOST_STH) size = 2;
        /* Debe caber el acceso completo, no solo su primer byte. */
        return address >= 0x10000u && address <= 0x20000u - size && address % size == 0;
    }
    if (in->format == PORYGON_FORMAT_JUMP && in->funct == PORYGON_JUMP_JR &&
        known[in->operands.jump.rd]) {
        uint32_t address = value[in->operands.jump.rd];
        return address < 0x10000u && address % 8 == 0;
    }
    return true;
}

/**
 * Comprueba que el programa puede entrar al planificador sin interpretar ASM otra vez.
 * Primero valida instrucciones, después bloques y constantes, y finalmente etiquetas.
 * No presupone que el generador de Piper haya terminado sus validaciones.
 */
porygon_schedule_result_t schedule_validate_program(const porygon_machine_program_t *p)
{
    size_t i, b, next = 0;
    uint16_t word, literal;
    if (p == NULL || p->instruction_count == 0 || p->instructions == NULL ||
        p->block_count == 0 || p->blocks == NULL || (p->label_count && p->labels == NULL)) {
        return schedule_result(PORYGON_SCHEDULE_INVALID_PROGRAM, SIZE_MAX, SIZE_MAX, SIZE_MAX,
                               "MachineProgram vacío o incompleto");
    }
    if (p->instruction_count > PORYGON_MAX_BUNDLES * PORYGON_BUNDLE_SLOTS) {
        return schedule_result(PORYGON_SCHEDULE_TOO_LARGE, SIZE_MAX, SIZE_MAX, SIZE_MAX,
                               "El programa excede la memoria de instrucciones");
    }
    for (i = 0; i < p->instruction_count; ++i) {
        const porygon_machine_instruction_t *mi = &p->instructions[i];
        const porygon_instruction_t *in = &mi->instruction;
        /* Aquí solo interesa que exista una ALU legal; elegir cuál usar en un
           ciclo concreto corresponde más adelante a choose_unit. */
        if (schedule_encode(in, 0, &word, &literal) != PORYGON_ENCODE_OK &&
            schedule_encode(in, 1, &word, &literal) != PORYGON_ENCODE_OK) {
            return schedule_result(PORYGON_SCHEDULE_INVALID_INSTRUCTION, i, SIZE_MAX, SIZE_MAX,
                                   "Operandos, función o destinos FEISTEL inválidos");
        }
        if (in->format == PORYGON_FORMAT_JUMP && in->funct != PORYGON_JUMP_JR) {
            if (!in->immediate || mi->target_label >= p->label_count) {
                return schedule_result(PORYGON_SCHEDULE_INVALID_PROGRAM, i, SIZE_MAX, SIZE_MAX,
                                       "Un salto directo requiere etiqueta y forma larga");
            }
        } else if (mi->target_label != PORYGON_NO_LABEL) {
            return schedule_result(PORYGON_SCHEDULE_INVALID_PROGRAM, i, SIZE_MAX, SIZE_MAX,
                                   "Etiqueta destino en una instrucción que no es salto directo");
        }
    }
    for (b = 0; b < p->block_count; ++b) {
        const porygon_machine_block_t *block = &p->blocks[b];
        /* Un bloque puede tener varias entradas. Solo r0 es conocido sin analizar
           sus predecesores; {true} inicializa los otros siete elementos a false. */
        bool known[8] = {true};
        uint32_t values[8] = {0};
        if (block->first_instruction != next || block->instruction_count == 0 ||
            block->instruction_count > p->instruction_count - next) {
            return schedule_result(PORYGON_SCHEDULE_INVALID_PROGRAM, next, SIZE_MAX, SIZE_MAX,
                                   "Rangos de bloques inconsistentes");
        }
        next += block->instruction_count;
        for (i = block->first_instruction; i < next; ++i) {
            const porygon_instruction_t *in = &p->instructions[i].instruction;
            if (in->format == PORYGON_FORMAT_JUMP && i + 1 != next) {
                return schedule_result(PORYGON_SCHEDULE_INVALID_PROGRAM, i, SIZE_MAX, SIZE_MAX,
                                       "El salto debe terminar su bloque");
            }
            if (!valid_constant_address(in, known, values)) {
                return schedule_result(PORYGON_SCHEDULE_INVALID_ADDRESS, i, SIZE_MAX, SIZE_MAX,
                                       "Dirección constante desalineada o fuera del segmento");
            }
            update_constants(in, known, values);
        }
    }
    if (next != p->instruction_count) {
        return schedule_result(PORYGON_SCHEDULE_INVALID_PROGRAM, next, SIZE_MAX, SIZE_MAX,
                               "Hay instrucciones sin bloque");
    }
    for (i = 0; i < p->label_count; ++i) {
        const porygon_machine_label_t *label = &p->labels[i];
        if (label->name == NULL || label->name[0] == '\0' || label->block_index >= p->block_count ||
            label->instruction_index != p->blocks[label->block_index].first_instruction) {
            return schedule_result(PORYGON_SCHEDULE_INVALID_PROGRAM, SIZE_MAX, SIZE_MAX, SIZE_MAX,
                                   "Etiqueta sin comienzo de bloque válido");
        }
    }
    return schedule_result(PORYGON_SCHEDULE_OK, SIZE_MAX, SIZE_MAX, SIZE_MAX, "OK");
}
