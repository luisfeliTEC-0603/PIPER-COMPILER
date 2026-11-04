#include "machine_program.h"

#include "encoder.h"

#include <stdlib.h>

/** Convierte rn en el bit n; por ejemplo, r3 se representa como 00001000. */
static uint8_t register_bit(uint8_t number)
{
    return (uint8_t)(UINT8_C(1) << number);
}

/** Agrega un destino real; escribir r0 no produce un valor que deba esperarse. */
static void define_register(porygon_machine_instruction_t *out, uint8_t number)
{
    /* r0 es constante: una escritura hacia él no crea dependencia RAW/WAW. */
    if (number != 0) {
        out->defs |= register_bit(number);
    }
}

/**
 * Deriva lecturas, escrituras, efectos y slots desde operandos ya validados.
 * Las máscaras describen dependencias potenciales; no contienen ciclos ni ALU elegida.
 */
void porygon_describe_instruction(porygon_machine_instruction_t *out)
{
    const porygon_instruction_t *in;

    if (out == NULL) {
        return;
    }
    /* Se recalculan todos los metadatos para que una llamada repetida no
       conserve dependencias de la operación anterior. */
    in = &out->instruction;
    out->uses = 0;
    out->defs = 0;
    out->state_uses = 0;
    out->state_defs = 0;
    out->memory_effect = PORYGON_MEMORY_NONE;
    out->resource = PORYGON_RESOURCE_NONE;
    /* El literal comparte el bundle con la instrucción y ocupa justo el
       slot siguiente; el scheduler necesita reservar ambas posiciones. */
    out->slot_count = in->immediate ? 2 : 1;
    out->ends_block = false;

    switch (in->format) {
    case PORYGON_FORMAT_NOP:
        break;
    case PORYGON_FORMAT_MATH:
        /* MOV solo lee rs2. Las otras formas operan sobre el valor previo
           de rs1; con inmediato o en forma unaria no leen rs2. */
        out->resource = ((in->funct >= PORYGON_MATH_SHL &&
                          in->funct <= PORYGON_MATH_MUL) ||
                         in->funct == PORYGON_MATH_MULH)
                            ? PORYGON_RESOURCE_MATH_SPECIAL
                            : PORYGON_RESOURCE_MATH;
        if (in->funct != PORYGON_MATH_MOV) {
            out->uses |= register_bit(in->operands.math.rs1);
        }
        if (!in->immediate && in->funct != PORYGON_MATH_NOT &&
            in->funct != PORYGON_MATH_NEZ) {
            out->uses |= register_bit(in->operands.math.rs2);
        }
        define_register(out, in->operands.math.rs1);
        break;
    case PORYGON_FORMAT_LOST:
        out->resource = PORYGON_RESOURCE_LOST;
        /* La base se lee siempre. En STORE, rd aporta el dato; en LOAD, rd
           recibe el dato. El offset no crea una dependencia de registro. */
        out->uses = register_bit(in->operands.lost.rs);
        if (in->funct >= PORYGON_LOST_STW) {
            out->uses |= register_bit(in->operands.lost.rd);
            out->memory_effect = PORYGON_MEMORY_WRITE;
        } else {
            define_register(out, in->operands.lost.rd);
            out->memory_effect = PORYGON_MEMORY_READ;
        }
        break;
    case PORYGON_FORMAT_JUMP:
        out->resource = PORYGON_RESOURCE_JUMP;
        /* Todos los saltos cierran el bloque actual. La arista condicional
           hacia el bloque siguiente se crea en build_blocks(). */
        out->ends_block = true;
        if (in->funct != PORYGON_JUMP_J) {
            out->uses = register_bit(in->operands.jump.rd);
        }
        break;
    case PORYGON_FORMAT_CRYPTO:
        out->resource = PORYGON_RESOURCE_CRYPTO;
        /* AUTH, PWD, RVK, LOPW, WRITE y FEISTEL comparten estado de bóveda,
           credenciales o sesión. Serializarlas preserva su orden observable. */
        out->state_uses = PORYGON_STATE_CRYPTO;
        out->state_defs = PORYGON_STATE_CRYPTO;
        if (in->funct == PORYGON_CRYPTO_WRITE ||
            in->funct == PORYGON_CRYPTO_FEISTEL ||
            in->funct == PORYGON_CRYPTO_LOPW) {
            out->uses = register_bit(in->operands.crypto.rs1);
        }
        /* FEISTEL lee L y R, y escribe los dos resultados sobre los mismos
           registros. Las otras operaciones CRYPTO no escriben GPR. */
        if (in->funct == PORYGON_CRYPTO_FEISTEL) {
            out->uses |= register_bit(in->operands.crypto.rs2);
            define_register(out, in->operands.crypto.rs1);
            define_register(out, in->operands.crypto.rs2);
        }
        break;
    }
}

/** Libera los arreglos y los nombres de etiquetas que pertenecen al programa. */
void porygon_machine_program_free(porygon_machine_program_t *program)
{
    size_t index;

    if (program == NULL) {
        return;
    }
    for (index = 0; index < program->label_count; ++index) {
        free(program->labels[index].name);
    }
    free(program->instructions);
    free(program->labels);
    free(program->blocks);
    *program = (porygon_machine_program_t){0};
}
