#ifndef PORYGON_ASSEMBLER_MACHINE_PROGRAM_H
#define PORYGON_ASSEMBLER_MACHINE_PROGRAM_H

#include "mnemonics.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Instrucción ya reconocida: format y funct identifican la operación; la
   variante immediate indica si necesita el slot literal siguiente. La unión
   conserva los operandos según el formato, aún sin convertirlos a bits. */
typedef struct {
    porygon_format_t format;
    unsigned int funct;
    bool immediate;
    union {
        struct {
            uint8_t rs1;
            uint8_t rs2;
            int64_t literal;
        } math;
        struct {
            uint8_t rd;
            uint8_t rs;
            int64_t offset;
        } lost;
        struct {
            uint8_t rd;
            int64_t offset;
        } jump;
        struct {
            uint8_t rs1;
            uint8_t rs2;
            uint8_t item_index; /* Ronda, subllave o palabra, según funct. */
            uint8_t key_index;
        } crypto;
    } operands;
} porygon_instruction_t;

/* El índice de una etiqueta permanece estable durante este MachineProgram.
   El scheduler asignará después a cada etiqueta un índice final de bundle. */
typedef size_t porygon_label_id_t;
#define PORYGON_NO_LABEL SIZE_MAX
#define PORYGON_NO_BLOCK SIZE_MAX

/* Recurso funcional requerido. MATH_SPECIAL agrupa mul, mulh y desplazamientos;
   las capacidades y latencias concretas se fijarán con el scheduler/RTL. */
typedef enum {
    PORYGON_RESOURCE_NONE,
    PORYGON_RESOURCE_MATH,
    PORYGON_RESOURCE_MATH_SPECIAL,
    PORYGON_RESOURCE_LOST,
    PORYGON_RESOURCE_JUMP,
    PORYGON_RESOURCE_CRYPTO
} porygon_resource_t;

/* READ y WRITE indican que debe respetarse el orden de accesos de memoria
   potencialmente relacionados, aunque su dirección exacta dependa de rbase. */
typedef enum {
    PORYGON_MEMORY_NONE,
    PORYGON_MEMORY_READ,
    PORYGON_MEMORY_WRITE
} porygon_memory_effect_t;

/* Estado arquitectónico fuera del banco de registros. Las máscaras permiten
   modelar dependencias sin fingir que AUTH o las banderas son registros GPR. */
#define PORYGON_STATE_FLAGS  UINT32_C(0x01)
#define PORYGON_STATE_CRYPTO UINT32_C(0x02)

/* Entrada que recibe el scheduler para una operación. uses/defs son máscaras
   de ocho bits; por ejemplo, leer r1 y r3 da 0b00001010. state_uses/defs
   aplican la misma idea a banderas y estado CRYPTO. */
typedef struct {
    porygon_instruction_t instruction;
    porygon_label_id_t target_label; /* No se convierte todavía en offset. */
    uint8_t uses;                    /* Bit n: lee rn. */
    uint8_t defs;                    /* Bit n: escribe rn; r0 nunca se marca. */
    uint32_t state_uses;
    uint32_t state_defs;
    porygon_memory_effect_t memory_effect;
    porygon_resource_t resource;
    uint8_t slot_count;             /* 2 si usa un literal en el slot siguiente. */
    bool ends_block;                /* Salto: no mover código a través de él. */
    size_t line;                    /* Ubicación ASM, ambas coordenadas base 1. */
    size_t column;
} porygon_machine_instruction_t;

/* Una etiqueta se asocia a la próxima instrucción, no a un número de bundle.
   Varias etiquetas pueden compartir instruction_index y block_index. */
typedef struct {
    char *name;                     /* Propiedad de MachineProgram. */
    size_t instruction_index;       /* Primera instrucción del bloque destino. */
    size_t block_index;             /* Se llena al construir los bloques. */
    size_t line;
    size_t column;
} porygon_machine_label_t;

/* Bloque básico: rango contiguo en instructions. Las dos aristas describen
   el camino secuencial y el destino de un salto directo, respectivamente. */
typedef struct {
    size_t first_instruction;
    size_t instruction_count;
    size_t fallthrough_block;       /* SIZE_MAX si no hay camino secuencial. */
    size_t branch_target_block;     /* SIZE_MAX si no hay destino directo. */
} porygon_machine_block_t;

/* Arreglos propiedad del programa. Después de parsear, sus índices permiten
   recorrer instrucciones, resolver nombres y consultar bloques sin texto ASM. */
typedef struct {
    porygon_machine_instruction_t *instructions;
    size_t instruction_count;
    porygon_machine_label_t *labels;
    size_t label_count;
    porygon_machine_block_t *blocks;
    size_t block_count;
} porygon_machine_program_t;

/* Calcula datos para dependencias y recursos a partir de campos ya validados.
   No decide ciclos, posiciones de slot ni latencias del pipeline. */
void porygon_describe_instruction(porygon_machine_instruction_t *instruction);

/* Libera arreglos y nombres, y deja la estructura en cero. El llamador debe
   invocarla después de un parseo exitoso; también acepta un programa vacío. */
void porygon_machine_program_free(porygon_machine_program_t *program);

#endif
