#ifndef PIPER_REGALLOC_MACHINE_H
#define PIPER_REGALLOC_MACHINE_H

/** @file regalloc_machine.h
 * Primer modelo de instrucciones virtuales del backend IR (R2).
 *
 * Cada instruccion declara sus temporales leidos/escritos y los registros
 * fisicos que usa de forma fija. Los IDs 0..ir_temp_count-1 conservan los IDs
 * de IR; los auxiliares creados al bajar constantes/simbolos comienzan alli.
 * MOVE y las operaciones de dos direcciones hacen explicita la restriccion
 * de la ISA: en ADD/SUB/AND/OR/XOR, def se lee antes de escribirse.
 *
 * LOAD_SYMBOL/STORE_SYMBOL siguen siendo operaciones virtuales: su expansion
 * tipada y las restricciones de una direccion global quedan para la emision.
 * No se debe colorear ni emitir ASM directamente desde este subconjunto.
 */
#include "regalloc.h"

#define REGALLOC_PHYS_R1 (1u << 1)

typedef enum {
    REGALLOC_M_IMM,          /**< def = constante de 32 bits. */
    REGALLOC_M_LOAD_SYMBOL,  /**< def = lectura tipada de symbol. */
    REGALLOC_M_STORE_SYMBOL, /**< symbol = use1, escritura tipada. */
    REGALLOC_M_MOVE,         /**< def = use1. */
    REGALLOC_M_ADD, REGALLOC_M_SUB,
    REGALLOC_M_AND, REGALLOC_M_OR, REGALLOC_M_XOR,
    REGALLOC_M_JUMP,         /**< Salta a targets[0]. */
    REGALLOC_M_BRANCH,       /**< Prueba use1 y salta a targets[0/1]. */
    REGALLOC_M_TO_RETURN,    /**< r1 = use1. */
    REGALLOC_M_RETURN        /**< Retorno; lee r1 si hay valor. */
} RegallocMachineOp;

/** IR_NO_ID significa que no hay def/use1. En una operacion de dos direcciones
 * def es tambien uso: el valor anterior de def participa en el calculo.
 * fixed_reads/writes son mascaras de registros r0..r7; por ahora se llenan
 * para el retorno. flags conserva efectos del IR cuando corresponda. */
typedef struct {
    RegallocMachineOp op;
    IrId def, use1;
    int64_t immediate;
    const Symbol *symbol;
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
    RegallocMachineBlock *blocks;
    size_t block_count;
} RegallocMachineRoutine;

/** Posee rutinas, bloques e instrucciones. source es un prestamo a la IR. */
typedef struct {
    const IrProgram *source;
    RegallocMachineRoutine *routines;
    size_t routine_count;
} RegallocMachineProgram;

/** Baja solo COPY escalar, operaciones binarias sin comprobacion de rango,
 * JUMP, BRANCH y RETURN. Rechaza otras operaciones con REGALLOC_UNSUPPORTED;
 * limpia el resultado parcial y deja *out=NULL. Requiere un analisis preparado
 * sobre IR/CFG aun vivos; liveness_ready no es obligatorio.
 *
 * El modelo no se integra aun con el CLI. Es la entrada para el liveness de
 * maquina y la expansion de instrucciones en el siguiente tramo de R2. */
RegallocStatus regalloc_lower_scalar(const RegallocAnalysis *analysis,
                                    RegallocMachineProgram **out,
                                    RegallocDiagnostic *diagnostic);

void regalloc_machine_program_free(RegallocMachineProgram *program);

#endif
