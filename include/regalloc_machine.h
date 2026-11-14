#ifndef PIPER_REGALLOC_MACHINE_H
#define PIPER_REGALLOC_MACHINE_H

/** @file regalloc_machine.h
 * Primer modelo de instrucciones virtuales del backend IR (R2).
 *
 * Cada instruccion declara sus temporales leidos/escritos y los registros
 * fisicos que usa de forma fija. Los IDs 0..ir_temp_count-1 conservan los IDs
 * de IR; los auxiliares creados al bajar constantes/simbolos comienzan alli.
 * MOVE y las operaciones de dos direcciones hacen explicita la restriccion
 * de la ISA: las operaciones ALU leen def antes de escribirlo.
 *
 * LOAD_SYMBOL/STORE_SYMBOL siguen siendo operaciones virtuales: su expansion
 * tipada y las restricciones de una direccion global quedan para la emision.
 * No se debe colorear ni emitir ASM directamente desde este subconjunto.
 */
#include "regalloc.h"

/* Contrato de registros PORYGON: r0 es cero y r7 es frame pointer. El pool
 * candidato tiene seis colores, pero una instruccion puede restringirlos.
 * El coloreador futuro debera garantizar al menos tres colores generales y
 * respetar clobbers/salvados antes de usar r1..r6 a traves de llamadas. */
#define REGALLOC_PHYS_R1 (1u << 1)
#define REGALLOC_PHYS_R2 (1u << 2)
#define REGALLOC_PHYS_R3 (1u << 3)
#define REGALLOC_PHYS_R4 (1u << 4)
#define REGALLOC_PHYS_R5 (1u << 5)
#define REGALLOC_PHYS_R6 (1u << 6)
#define REGALLOC_PHYS_R7 (1u << 7)
#define REGALLOC_MIN_COLOR_COUNT 3u
#define REGALLOC_CANDIDATE_COLOR_COUNT 6u
#define REGALLOC_CANDIDATE_MASK (REGALLOC_PHYS_R1 | REGALLOC_PHYS_R2 | \
                                 REGALLOC_PHYS_R3 | REGALLOC_PHYS_R4 | \
                                 REGALLOC_PHYS_R5 | REGALLOC_PHYS_R6)

typedef enum {
    REGALLOC_M_IMM,          /**< def = constante de 32 bits. */
    REGALLOC_M_LOAD_SYMBOL,  /**< def = lectura tipada de symbol. */
    REGALLOC_M_STORE_SYMBOL, /**< symbol = use1, escritura tipada. */
    REGALLOC_M_MOVE,         /**< def = use1. */
    REGALLOC_M_ADD, REGALLOC_M_SUB,
    REGALLOC_M_ADDI, REGALLOC_M_SUBI, /**< def op= immediate; inmediato signed 16. */
    REGALLOC_M_AND, REGALLOC_M_OR, REGALLOC_M_XOR,
    REGALLOC_M_EQ,          /**< eor: igualdad de patrones de 32 bits. */
    REGALLOC_M_BOOL_NOT,    /**< xori def, #1; def contiene bool 0/1. */
    REGALLOC_M_SPILL_LOAD,  /**< def = palabra de slot en el frame. */
    REGALLOC_M_SPILL_STORE, /**< palabra de slot = use1. */
    REGALLOC_M_JUMP,         /**< Salta a targets[0]. */
    REGALLOC_M_BRANCH,       /**< Prueba use1 y salta a targets[0/1]. */
    REGALLOC_M_TO_RETURN,    /**< r1 = use1. */
    REGALLOC_M_RETURN        /**< Retorno; lee r1 si hay valor. */
} RegallocMachineOp;

/** IR_NO_ID significa que no hay def/use1. En una operacion de dos direcciones
 * def es tambien uso: el valor anterior de def participa en el calculo.
 * fixed_reads/writes son mascaras de registros r0..r7. slot solo se interpreta
 * en SPILL_LOAD/STORE: es un ID local a la rutina, no un offset ni un Symbol.
 * flags conserva efectos del IR; los spills llevan READ/WRITE_MEMORY. */
typedef struct {
    RegallocMachineOp op;
    IrId def, use1;
    int64_t immediate;
    const Symbol *symbol;
    size_t slot;
    IrId targets[2];
    IrType type;
    unsigned flags;
    unsigned fixed_reads, fixed_writes;
    SourceLocation location;
    size_t origin_instruction;
} RegallocMachineInstruction;

typedef struct {
    IrId id;
    size_t cfg_node;
    RegallocMachineInstruction *instructions;
    size_t instruction_count;
} RegallocMachineBlock;

typedef struct {
    size_t ir_routine;
    size_t ir_temp_count, temp_count;
    /* El emisor calcula frame_bytes = base_frame_bytes + 4 * spill_slot_count.
     * El offset de slot s desde r7, tras el prologo, es base_frame_bytes + 4*s.
     * El frame y call_reserve_size deben caber juntos en SYMBOL_STACK_SIZE. */
    size_t base_frame_bytes, spill_slot_count;
    RegallocMachineBlock *blocks;
    size_t block_count;
} RegallocMachineRoutine;

/** Posee rutinas, bloques e instrucciones. source es un prestamo a la IR. */
typedef struct RegallocMachineProgram {
    const IrProgram *source;
    RegallocMachineRoutine *routines;
    size_t routine_count;
} RegallocMachineProgram;

/** Baja COPY escalar, ADD/SUB/AND/OR/XOR sin chequeo, EQ/NE/NOT,
 * JUMP, BRANCH y RETURN. Rechaza otras operaciones con REGALLOC_UNSUPPORTED;
 * limpia el resultado parcial y deja *out=NULL. Requiere un analisis preparado
 * sobre IR/CFG aun vivos; analysis->liveness puede ser NULL.
 *
 * El modelo no se integra aun con el CLI. Es la entrada para el liveness de
 * maquina y la expansion de instrucciones en el siguiente tramo de R2. */
RegallocStatus regalloc_lower_scalar(const RegallocAnalysis *analysis,
                                    RegallocMachineProgram **out,
                                    RegallocDiagnostic *diagnostic);

void regalloc_machine_program_free(RegallocMachineProgram *program);

#endif
