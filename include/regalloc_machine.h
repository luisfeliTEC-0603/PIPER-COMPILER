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
 * grande solo usa el destino y r0. CALL escalar declara argumentos, efectos
 * y salvado del llamador. LT/GT conservan el signo de ambos operandos para
 * que su expansion sobre la comparacion unsigned de la ISA use solo r5.
 * La aritmetica comprobada usa r5/r6 reservados y conserva MAY_TRAP.
 * ADDR/CHECK_BOUNDS/LOAD/STORE cubren accesos indexados; faltan intrinsecas:
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
#define REGALLOC_REGISTER_ARGS 4u
#define REGALLOC_MAX_CALL_ARGS 32u

typedef enum {
    REGALLOC_M_IMM,          /**< def = constante de 32 bits; solo usa def y r0. */
    REGALLOC_M_LOAD_SYMBOL,  /**< def = lectura tipada; immediate = direccion/offset. */
    REGALLOC_M_STORE_SYMBOL, /**< symbol = use1; immediate = direccion/offset. */
    REGALLOC_M_MOVE,         /**< def = use1. */
    REGALLOC_M_CHECKED_COPY, /**< Comprueba rango de use1 antes de definir def. */
    REGALLOC_M_CHECKED_NEG,  /**< def = -use1 si cabe en type.piper. */
    REGALLOC_M_ADD, REGALLOC_M_SUB, REGALLOC_M_MUL,
    REGALLOC_M_CHECKED_ADD, REGALLOC_M_CHECKED_SUB,
    REGALLOC_M_CHECKED_MUL, /**< Operaciones tipadas con error por overflow. */
    REGALLOC_M_DIV, REGALLOC_M_MOD, REGALLOC_M_POW,
    /**< Algoritmos comprobados con registro de trabajo bajo r7. */
    REGALLOC_M_ADDI, REGALLOC_M_SUBI, /**< def op= immediate; inmediato signed 16. */
    REGALLOC_M_AND, REGALLOC_M_OR, REGALLOC_M_XOR,
    REGALLOC_M_EQ,          /**< eor: igualdad de patrones de 32 bits. */
    REGALLOC_M_LT, REGALLOC_M_GT, /**< Orden tipado: ltn/gtn y r5 si hay signo. */
    REGALLOC_M_BOOL_NOT,    /**< xori def, #1; def contiene bool 0/1. */
    REGALLOC_M_ADDR,        /**< def = base de arreglo global/local/parametro. */
    REGALLOC_M_CHECK_BOUNDS,/**< use1 < immediate unsigned o error_indice. */
    REGALLOC_M_LOAD_INDIRECT, /**< def = memoria tipada en use1 + 0. */
    REGALLOC_M_STORE_INDIRECT,/**< memoria tipada en use1 + 0 = use2. */
    REGALLOC_M_SPILL_LOAD,  /**< def = palabra de slot en el frame. */
    REGALLOC_M_SPILL_STORE, /**< palabra de slot = use1. */
    REGALLOC_M_CALL_SAVE,   /**< Guarda r1..r6 antes de colocar argumentos. */
    REGALLOC_M_CALL_ARG,    /**< use1 = argumento; indice en call_arg_index. */
    REGALLOC_M_CALL_VOID,   /**< Invoca y restaura sin resultado virtual. */
    REGALLOC_M_CALL_RESULT, /**< Invoca y define def con el retorno. */
    REGALLOC_M_JUMP,         /**< Salta a targets[0]. */
    REGALLOC_M_BRANCH,       /**< Prueba use1 y salta a targets[0/1]. */
    REGALLOC_M_TO_RETURN,    /**< r1 = use1. */
    REGALLOC_M_RETURN        /**< Retorno; lee r1 si hay valor. */
} RegallocMachineOp;

/** IR_NO_ID significa que no hay def/use1. use2 solo se interpreta para
 * STORE_INDIRECT: la direccion y el valor han de estar vivos simultaneamente.
 * En una operacion de dos direcciones
 * def es tambien uso: el valor anterior de def participa en el calculo.
 * fixed_reads/writes son mascaras de registros r0..r7. slot solo se interpreta
 * en SPILL_LOAD/STORE: es un ID local a la rutina, no un offset ni un Symbol.
 * flags conserva efectos del IR; los spills llevan READ/WRITE_MEMORY.
 * LOAD/STORE_SYMBOL locales leen r7; los globales construyen la direccion
 * fisica desde r0. El store global usa y escribe r5, reservado fuera de K=4.
 * IMM, incluso grande, solo necesita su def y r0 para construirse.
 * LT/GT leen left_unsigned/right_unsigned: la ISA ordena como unsigned;
 * si algun operando tiene signo, r5 es auxiliar fijo fuera de K=4.
 * CHECKED_ADD/SUB/MUL leen y escriben def, leen use1, y usan r5/r6 fijos
 * para detectar desborde segun type.piper antes de publicar un valor valido.
 * DIV/MOD/POW conservan r1..r4 internamente en 32 bytes bajo r7; sus
 * left_unsigned/right_unsigned indican la interpretacion de los operandos.
 * ADDR entrega la base de un arreglo: global = direccion fisica, local =
 * r7 + immediate, parametro = puntero cargado desde immediate(r7).
 * CHECK_BOUNDS compara use1 con immediate como unsigned y salta a error_indice
 * si falla; r5/r6 son auxiliares fijos. LOAD_INDIRECT y STORE_INDIRECT
 * acceden al tipo de elemento en 0(use1); STORE tambien lee use2.
 * CHECKED_COPY/NEG leen use1 sin modificarlo; source_type conserva su signo,
 * type es el destino. Comprueban rango con r5/r6 antes de definir def.
 * CALL_SAVE/ARG/CALL comparten call_site_id y call_arg_count. ARG lleva
 * call_arg_index; CALL lleva symbol=callee. El emisor guarda r1..r6 antes de
 * tocar r1..r4 y restaura despues del retorno; CALL_RESULT publica def al
 * final. Los argumentos i>=4 se guardan despues del call record de 32 bytes.
 * Ningun CALL_ARG crea un temporal nuevo entre la primera escritura fisica
 * de argumento y el salto. */
typedef struct {
    RegallocMachineOp op;
    IrId def, use1, use2;
    int64_t immediate;
    const Symbol *symbol;
    size_t slot;
    size_t call_site_id, call_arg_index, call_arg_count;
    IrId targets[2];
    IrType type;
    IrType source_type; /**< CHECKED_COPY/NEG: tipo de use1 antes del chequeo. */
    bool left_unsigned, right_unsigned; /**< LT/GT y DIV/MOD/POW: signos. */
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
    size_t call_site_count; /**< IDs 1..N, unicos entre todas las rutinas. */
} RegallocMachineProgram;

/** Baja COPY escalar con/sin chequeo, NEG comprobada,
 * ADD/SUB/MUL con/sin chequeo, DIV/MOD/POW comprobadas, AND/OR/XOR,
 * EQ/NE/LT/LE/GT/GE/NOT, ADD de direcciones,
 * ADDR/CHECK_BOUNDS/LOAD/STORE de arreglos,
 * CALL con argumentos escalares o punteros de arreglo, JUMP, BRANCH y RETURN.
 * Los simbolos escalares incluyen direccion,
 * memoria y registros fijos: r7 local; r0 y r5 en store global. IMM usa r0.
 * Rechaza otras operaciones con REGALLOC_UNSUPPORTED;
 * limpia el resultado parcial y deja *out=NULL. Requiere un analisis preparado
 * sobre IR/CFG aun vivos; analysis->liveness puede ser NULL.
 *
 * El modelo no se integra aun con el CLI. regalloc_allocate consume esta
 * bajada y entrega colores/spills para el subconjunto admitido. */
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
 * R2b no modele las operaciones restantes ni exista emision ASM, no sirve
 * como base para generar el ejecutable final. */
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

/** Resultado de colorear un grafo. steps es la pila de simplificacion
 * (el ultimo elemento se colorea primero). colors[t] vale 1..4 o 0 si no pudo
 * colorearse; un 0 no inserta todavia cargas/escrituras de spill. Incluye los
 * IDs sin uso que figuran en temp_count, para conservar un indice por temporal.
 * regalloc_allocate reescribe esos ceros antes de entregar la vista al emisor. */
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
/** Como el coloreo exploratorio, pero al atascarse retira el menor
 * spill_costs[t]/max(1, grado_activo). Costos NULL usa la politica anterior.
 * El candidato sigue siendo optimista: solo colors[t]==0 exige reescritura. */
RegallocStatus regalloc_color_machine_graph_costed(
    const RegallocMachineGraph *graph, const uint64_t *spill_costs,
    RegallocColoring **out, RegallocDiagnostic *diagnostic);
void regalloc_coloring_free(RegallocColoring *coloring);

/** Vista final para el emisor ASM. No posee machine, routines ni sus arreglos:
 * el asignador conserva esos datos vivos durante la emision. Para cada rutina,
 * register_for_temp tiene temp_count entradas indexadas por ID virtual.
 * 0 significa temporal sin uso; 1..4 son los registros fisicos r1..r4.
 * Todo def/use1/use2 aplicable en las instrucciones finales tiene registro.
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

/** Resultado propietario de la ruta IR -> maquina virtual -> asignacion.
 * Contiene machine, colores y la vista validada para el emisor. IR, CFG y
 * simbolos siguen prestados desde analysis y deben permanecer vivos. */
typedef struct RegallocAllocation RegallocAllocation;

/** Baja el subconjunto IR admitido, repite liveness/grafo/coloreo y reescribe
 * los valores sin color con SPILL_LOAD/STORE hasta entregar todos los operandos
 * en r1..r4. Ante fallo libera el trabajo parcial y deja *out == NULL. */
RegallocStatus regalloc_allocate(const RegallocAnalysis *analysis,
                                 RegallocAllocation **out,
                                 RegallocDiagnostic *diagnostic);
const RegallocAssignedProgram *regalloc_allocation_view(
    const RegallocAllocation *allocation);
void regalloc_allocation_free(RegallocAllocation *allocation);

#endif
