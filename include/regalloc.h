#ifndef PIPER_REGALLOC_H
#define PIPER_REGALLOC_H

/** @file regalloc.h
 * Primera etapa del futuro backend de registros.
 *
 * regalloc_prepare recibe la IR de una compilacion y el CFG construido sobre
 * esa misma IR. Crea un objeto propio con una entrada por rutina y por bloque.
 * El objeto permite localizar los bloques sin confundir un ID IR (por ejemplo,
 * B9) con la posicion de su nodo en el CFG (por ejemplo, nodes[1]).
 *
 * regalloc_compute_liveness añade los conjuntos vivos de temporales virtuales
 * sobre estos bloques. No selecciona registros ni inserta accesos por spill.
 */
#include "cfg.h"

/** Resultado del analisis/bajada. UNSUPPORTED significa que una operacion
 * valida aun no tiene traduccion; no equivale a IR/CFG invalido ni a OOM. */
typedef enum {
    REGALLOC_OK = 0,
    REGALLOC_INVALID_INPUT,
    REGALLOC_INVALID_IR,
    REGALLOC_INVALID_CFG,
    REGALLOC_UNSUPPORTED,
    REGALLOC_OUT_OF_MEMORY
} RegallocStatus;

/** Primer error localizado. routine es un indice en IrProgram.routines;
 * block es un ID IR. Los campos sin ubicacion valen SIZE_MAX/IR_NO_ID. */
typedef struct {
    RegallocStatus status;
    size_t routine;
    IrId block;
    char message[256];
} RegallocDiagnostic;

/** Conjunto de IDs de temporal. words es una vista dentro de la reserva de su
 * RegallocRoutine; el llamador puede consultarla, pero no modificarla/liberarla.
 * Si temp_count == 0, words es NULL y word_count vale cero. */
typedef struct {
    uint64_t *words;
    size_t word_count;
} RegallocSet;

/** Valores vivos inmediatamente antes y despues de una instruccion IR. */
typedef struct {
    RegallocSet before, after;
} RegallocInstructionLive;

/** Correspondencia de un bloque: id identifica el bloque dentro de la IR y
 * cfg_node indexa el nodo que lo representa dentro de CfgRoutine.nodes.
 * No usar id como indice de arreglo: los IDs pueden ser dispersos o cambiar
 * de orden fisico sin perder su identidad. USE/DEF/IN/OUT se llenan al ejecutar
 * regalloc_compute_liveness; instruction_start indexa la lista plana de la
 * rutina y conserva el orden original de IrBlock.instructions. */
typedef struct {
    IrId id;
    size_t cfg_node;
    RegallocSet use, def, live_in, live_out;
    size_t instruction_start, instruction_count;
} RegallocBlock;

/** Datos propios de una rutina. temp_count es la cantidad de IDs virtuales
 * emitidos en IR, no la cantidad de registros disponibles en PORYGON.
 * blocks, instructions y set_storage son reservas propias; los RegallocSet
 * apuntan a segmentos disjuntos de set_storage. */
typedef struct {
    size_t ir_routine;       /**< Indice en IrProgram.routines. */
    size_t temp_count;       /**< IDs de temporal validos: 0..temp_count-1. */
    RegallocBlock *blocks;  /**< Mismo orden fisico que IrRoutine.blocks. */
    size_t block_count;
    RegallocInstructionLive *instructions;
    size_t instruction_count;
    size_t word_count;      /**< Palabras de 64 bits por conjunto. */
    uint64_t *set_storage; /**< Reserva unica de bits, propia de esta rutina. */
} RegallocRoutine;

/** Posee routines y sus arreglos blocks. Los punteros ir/cfg son prestamos:
 * liberar primero este analisis, despues CFG, IR y tabla de simbolos.
 * liveness_ready solo es true si todas las rutinas se calcularon con exito. */
typedef struct {
    const IrProgram *ir;
    const CfgProgram *cfg;
    RegallocRoutine *routines;
    size_t routine_count;
    bool liveness_ready;
} RegallocAnalysis;

/** Grafo preliminar de interferencia entre temporales de IR de UNA rutina.
 * neighbors contiene temp_count filas de word_count palabras; la fila t tiene
 * un bit por vecino de t. degree cuenta los bits de esa fila. Es propiedad del
 * llamador y no depende de la vida del analisis una vez construido.
 *
 * Este grafo sirve para estudiar el solapamiento del TAC. El grafo que se
 * coloreara debera construirse despues de bajar a instrucciones de maquina:
 * faltan registros implicitos, llamadas y temporales creados por el backend. */
typedef struct {
    size_t ir_routine;
    size_t temp_count, word_count;
    uint64_t *neighbors;
    size_t *degree;
} RegallocIrGraph;

/** Prepara las identidades que usaran las siguientes fases.
 * @param ir Programa IR completo; se consulta sin modificarlo.
 * @param cfg Grafo construido sobre exactamente ir; se consulta sin modificarlo.
 * @param out Puntero a NULL que recibe el objeto solo en REGALLOC_OK.
 * @param diagnostic Receptor opcional del estado y primer error.
 * @return REGALLOC_OK o la causa del fallo; ante fallo *out sigue siendo NULL.
 *
 * Primero comprueba la identidad cfg->ir == ir y valida ambos modelos. Luego
 * reserva todos los mapas. No deja al llamador una construccion parcial.
 */
RegallocStatus regalloc_prepare(const IrProgram *ir, const CfgProgram *cfg,
                               RegallocAnalysis **out,
                               RegallocDiagnostic *diagnostic);

/** Calcula USE/DEF/IN/OUT por bloque y before/after por instruccion.
 * @param analysis Salida de regalloc_prepare con IR/CFG aun vivos e intactos.
 * @param diagnostic Receptor opcional del primer fallo.
 * @return REGALLOC_OK al completar todas las rutinas. Ante fallo, descarta los
 * conjuntos parciales y conserva los mapas preparados para poder reintentar.
 * No invocar dos veces sobre un analisis ya completo.
 */
RegallocStatus regalloc_compute_liveness(RegallocAnalysis *analysis,
                                        RegallocDiagnostic *diagnostic);

/** Consulta segura de un ID virtual; false si set es NULL o el ID excede el
 * tamaño representado. Solo interpretar conjuntos tras liveness_ready=true. */
bool regalloc_set_contains(const RegallocSet *set, IrId temp);

/** Construye una aproximacion conservadora de interferencias desde R1.
 * Requiere liveness_ready; routine es indice de IrProgram.routines y *out debe
 * ser NULL. En exito el llamador libera el grafo con regalloc_ir_graph_free.
 * Una arista une valores vivos al entrar a un bloque o un resultado definido
 * con los valores que siguen vivos inmediatamente despues de la instruccion.
 * No se crean aristas reflexivas ni se colorea aqui. */
RegallocStatus regalloc_build_ir_graph(const RegallocAnalysis *analysis,
                                      size_t routine, RegallocIrGraph **out,
                                      RegallocDiagnostic *diagnostic);

/** false si el grafo es NULL o los IDs estan fuera de rango. */
bool regalloc_ir_graph_has_edge(const RegallocIrGraph *graph, IrId a, IrId b);
void regalloc_ir_graph_free(RegallocIrGraph *graph);

/** Libera los mapas propios y los conjuntos vivos, incluso si una reserva
 * anterior quedo a medias.
 * Acepta NULL; no libera la IR, el CFG ni los simbolos prestados. */
void regalloc_analysis_free(RegallocAnalysis *analysis);

#endif
