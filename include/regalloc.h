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
 * regalloc_compute_liveness conserva la entrada historica, pero delega el
 * calculo al modulo liveness compartido con DCE. No selecciona registros.
 */
#include "cfg.h"
#include "liveness/liveness.h"

/** Resultado del analisis/bajada. UNSUPPORTED significa que una operacion
 * valida aun no tiene traduccion; no equivale a IR/CFG invalido ni a OOM. */
typedef enum {
    REGALLOC_OK = 0,
    REGALLOC_INVALID_INPUT,
    REGALLOC_INVALID_IR,
    REGALLOC_INVALID_CFG,
    REGALLOC_UNSUPPORTED,
    REGALLOC_OUT_OF_MEMORY,
    REGALLOC_IO_ERROR
} RegallocStatus;

/** Primer error localizado. routine es un indice en IrProgram.routines;
 * block es un ID IR. Los campos sin ubicacion valen SIZE_MAX/IR_NO_ID. */
typedef struct {
    RegallocStatus status;
    size_t routine;
    IrId block;
    char message[256];
} RegallocDiagnostic;

/** Correspondencia de un bloque: id identifica el bloque dentro de la IR y
 * cfg_node indexa el nodo que lo representa dentro de CfgRoutine.nodes.
 * No usar id como indice de arreglo: los IDs pueden ser dispersos o cambiar
 * de orden fisico sin perder su identidad. Los conjuntos vivos estan en
 * RegallocAnalysis.liveness, indexados por rutina y posicion fisica de bloque. */
typedef struct {
    IrId id;
    size_t cfg_node;
} RegallocBlock;

/** Datos propios de una rutina. temp_count es la cantidad de IDs virtuales
 * emitidos en IR, no la cantidad de registros disponibles en PORYGON.
 * blocks es la unica reserva propia de esta estructura. */
typedef struct {
    size_t ir_routine;       /**< Indice en IrProgram.routines. */
    size_t temp_count;       /**< IDs de temporal validos: 0..temp_count-1. */
    RegallocBlock *blocks;  /**< Mismo orden fisico que IrRoutine.blocks. */
    size_t block_count;
} RegallocRoutine;

/** Posee routines y sus arreglos blocks. Los punteros ir/cfg son prestamos:
 * liberar primero este analisis, despues CFG, IR y tabla de simbolos.
 * liveness es propio y NULL hasta completar el analisis compartido. */
typedef struct {
    const IrProgram *ir;
    const CfgProgram *cfg;
    RegallocRoutine *routines;
    size_t routine_count;
    LivenessResult *liveness;
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

/** Delega USE/DEF/IN/OUT y before/after al liveness compartido.
 * @param analysis Salida de regalloc_prepare con IR/CFG aun vivos e intactos.
 * @param diagnostic Receptor opcional del primer fallo.
 * @return REGALLOC_OK al completar todas las rutinas. Ante fallo, descarta los
 * resultado parcial y conserva los mapas preparados para poder reintentar.
 * No invocar dos veces sobre un analisis ya completo.
 */
RegallocStatus regalloc_compute_liveness(RegallocAnalysis *analysis,
                                        RegallocDiagnostic *diagnostic);

/** Construye una aproximacion conservadora de interferencias desde R1.
 * Requiere analysis->liveness; routine es indice de IrProgram.routines y *out debe
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

/** Exporta todos los grafos IR preliminares en un unico archivo DOT no dirigido.
 * Cada rutina tiene un cluster propio, con nodos tN y aristas simetricas
 * escritas una sola vez. Requiere analysis->liveness y no cierra output.
 * El llamador debe comprobar fclose antes de publicar el archivo. */
RegallocStatus regalloc_write_ir_graph_dot(FILE *output,
                                          const RegallocAnalysis *analysis,
                                          RegallocDiagnostic *diagnostic);

/** Libera los mapas propios y los conjuntos vivos, incluso si una reserva
 * anterior quedo a medias.
 * Acepta NULL; no libera la IR, el CFG ni los simbolos prestados. */
void regalloc_analysis_free(RegallocAnalysis *analysis);

#endif
