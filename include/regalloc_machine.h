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
 * LOAD_SYMBOL/STORE_SYMBOL siguen siendo operaciones virtuales para el emisor;
 * los accesos escalares ya declaran sus direcciones y registros fijos. IMM
 * grande solo usa el destino y r0. Faltan llamadas, arreglos y comprobaciones:
 * no se debe emitir ASM directamente desde este subconjunto.
 */
#include "regalloc.h"

/* Contrato de registros PORYGON: r0 es cero y r7 es frame pointer. Hay seis
 * registros fisicos candidatos, pero esta implementacion colorea con r1..r4.
 * r5/r6 quedan para auxiliares y secuencias fijas del backend. Las llamadas
 * y demas usos fijos tambien restringen los cuatro colores asignables. */
#define REGALLOC_PHYS_R0 (1u << 0)
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
#define REGALLOC_COLOR_COUNT 4u
#define REGALLOC_COLOR_MASK (REGALLOC_PHYS_R1 | REGALLOC_PHYS_R2 | \
                             REGALLOC_PHYS_R3 | REGALLOC_PHYS_R4)

typedef enum {
    REGALLOC_M_IMM,          /**< def = constante de 32 bits; solo usa def y r0. */
    REGALLOC_M_LOAD_SYMBOL,  /**< def = lectura tipada; immediate = direccion/offset. */
    REGALLOC_M_STORE_SYMBOL, /**< symbol = use1; immediate = direccion/offset. */
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
 * flags conserva efectos del IR; los spills llevan READ/WRITE_MEMORY.
 * LOAD/STORE_SYMBOL locales leen r7; los globales construyen la direccion
 * fisica desde r0. El store global usa y escribe r5, reservado fuera de K=4.
 * IMM, incluso grande, solo necesita su def y r0 para construirse. */
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
 * JUMP, BRANCH y RETURN. Los operandos simbolo escalares incluyen direccion,
 * memoria y registros fijos: r7 local; r0 y r5 en store global. IMM usa r0.
 * Rechaza otras operaciones con REGALLOC_UNSUPPORTED;
 * limpia el resultado parcial y deja *out=NULL. Requiere un analisis preparado
 * sobre IR/CFG aun vivos; analysis->liveness puede ser NULL.
 *
 * El modelo no se integra aun con el CLI. Es la entrada para el liveness de
 * maquina y la expansion de instrucciones en el siguiente tramo de R2. */
RegallocStatus regalloc_lower_scalar(const RegallocAnalysis *analysis,
                                    RegallocMachineProgram **out,
                                    RegallocDiagnostic *diagnostic);

void regalloc_machine_program_free(RegallocMachineProgram *program);

/** Grafo de los temporales virtuales de UNA rutina de maquina. neighbors
 * contiene temp_count filas de word_count palabras de 64 bits. degree cuenta
 * vecinos distintos; forbidden_colors marca los registros r1..r6 que una
 * escritura fija impide usar para un valor vivo a traves de ella.
 *
 * Este grafo se recalcula despues de cada cambio de instrucciones. Mientras
 * R2b no expanda todos los pseudos y llamadas, no sirve para emitir ASM. */
typedef struct {
    size_t routine, temp_count, word_count;
    uint64_t *neighbors;
    size_t *degree;
    unsigned *forbidden_colors;
} RegallocMachineGraph;

/** machine y live describen exactamente la misma instantanea; live procede de
 * liveness_analyze_machine(machine). routine es un indice, no un ID de bloque.
 * *out comienza en NULL y recibe un grafo propio solo en REGALLOC_OK.
 * Conecta todos los pares vivos antes o despues de cada instruccion; tambien
 * registra colores bloqueados por escrituras fisicas a traves de una instruccion.
 * No calcula colores, costos ni decide spills. */
RegallocStatus regalloc_build_machine_graph(const RegallocMachineProgram *machine,
                                           const LivenessResult *live,
                                           size_t routine,
                                           RegallocMachineGraph **out,
                                           RegallocDiagnostic *diagnostic);
bool regalloc_machine_graph_has_edge(const RegallocMachineGraph *graph,
                                    IrId a, IrId b);
void regalloc_machine_graph_free(RegallocMachineGraph *graph);

/** Una entrada por temporal, en el orden en que se retira del grafo activo.
 * active_degree se mide justo antes de retirarlo; eligible_colors cuenta los
 * colores del pool que no prohibe su mascara fija. potential_spill solo indica
 * que no habia nodo simplificable en ese paso: no implica spill real. */
typedef struct {
    IrId temp;
    size_t active_degree;
    unsigned eligible_colors;
    bool potential_spill;
} RegallocSimplifyStep;

/** Resultado exploratorio del grafo actual. steps es la pila de simplificacion
 * (el ultimo elemento se colorea primero). colors[t] vale 1..4 o 0 si no pudo
 * colorearse; un 0 no inserta todavia cargas/escrituras de spill. Incluye los
 * IDs sin uso que figuran en temp_count, para conservar un indice por temporal.
 * No entregar al emisor hasta completar R2b y reescribir spills reales. */
typedef struct {
    RegallocSimplifyStep *steps;
    uint8_t *colors;
    size_t temp_count, uncolored_count;
} RegallocColoring;

/** Simplifica con K=4, elige provisionalmente el nodo de mayor grado activo
 * (desempate por ID menor) si no queda uno de grado < colores elegibles, y
 * reconstruye la pila eligiendo el registro fisico libre de menor numero.
 * graph procede de regalloc_build_machine_graph; *out comienza en NULL. */
RegallocStatus regalloc_color_machine_graph(const RegallocMachineGraph *graph,
                                            RegallocColoring **out,
                                            RegallocDiagnostic *diagnostic);
void regalloc_coloring_free(RegallocColoring *coloring);

/** Vista final para el emisor ASM. No posee machine, routines ni sus arreglos:
 * el asignador conserva esos datos vivos durante la emision. Para cada rutina,
 * register_for_temp tiene temp_count entradas indexadas por ID virtual.
 * 0 significa temporal sin uso; 1..4 son los registros fisicos r1..r4.
 * Todo def/use1 presente en las instrucciones finales debe tener registro.
 * Los valores derramados aparecen mediante SPILL_LOAD/STORE y sus copias
 * recargadas tambien tienen un registro; no se codifica memoria como color. */
typedef struct {
    const uint8_t *register_for_temp;
    size_t frame_bytes;
} RegallocAssignedRoutine;

typedef struct {
    const RegallocMachineProgram *machine;
    const RegallocAssignedRoutine *routines;
    size_t routine_count;
} RegallocAssignedProgram;

/** Comprueba una entrega completa antes de llamar al emisor: frame y reserva
 * de llamadas dentro de la pila, colores de todos los operandos, interferencias
 * y colores prohibidos por escrituras fijas. Requiere machine final e inmutable;
 * cada arreglo register_for_temp tiene machine->routines[r].temp_count entradas.
 * No valida aun restricciones de operaciones que R2b no ha bajado. */
RegallocStatus regalloc_assigned_program_validate(
    const RegallocAssignedProgram *assigned, RegallocDiagnostic *diagnostic);

/** Offset de una ranura desde r7 tras el prologo. Precondiciones: assigned fue
 * validado, routine < routine_count y slot < spill_slot_count de esa rutina. */
size_t regalloc_assigned_spill_offset(const RegallocAssignedProgram *assigned,
                                     size_t routine, size_t slot);

#endif
