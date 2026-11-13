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
 * Esta etapa prepara la estructura. Todavia no calcula variables vivas,
 * selecciona registros fisicos ni inserta cargas/escrituras por spill.
 */
#include "cfg.h"

/** Resultado de preparar el analisis. Los errores de IR/CFG se distinguen de
 * la falta de memoria para que el llamador sepa donde fallo el recorrido. */
typedef enum {
    REGALLOC_OK = 0,
    REGALLOC_INVALID_INPUT,
    REGALLOC_INVALID_IR,
    REGALLOC_INVALID_CFG,
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

/** Correspondencia de un bloque: id identifica el bloque dentro de la IR y
 * cfg_node indexa el nodo que lo representa dentro de CfgRoutine.nodes.
 * No usar id como indice de arreglo: los IDs pueden ser dispersos o cambiar
 * de orden fisico sin perder su identidad. */
typedef struct {
    IrId id;
    size_t cfg_node;
} RegallocBlock;

/** Datos propios de una rutina. temp_count es la cantidad de IDs virtuales
 * emitidos en IR, no la cantidad de registros disponibles en PORYGON. */
typedef struct {
    size_t ir_routine;       /**< Indice en IrProgram.routines. */
    size_t temp_count;       /**< IDs de temporal validos: 0..temp_count-1. */
    RegallocBlock *blocks;  /**< Mismo orden fisico que IrRoutine.blocks. */
    size_t block_count;
} RegallocRoutine;

/** Posee routines y sus arreglos blocks. Los punteros ir/cfg son prestamos:
 * liberar primero este analisis, despues CFG, IR y tabla de simbolos. */
typedef struct {
    const IrProgram *ir;
    const CfgProgram *cfg;
    RegallocRoutine *routines;
    size_t routine_count;
} RegallocAnalysis;

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

/** Libera los mapas propios, incluso si una reserva anterior quedo a medias.
 * Acepta NULL; no libera la IR, el CFG ni los simbolos prestados. */
void regalloc_analysis_free(RegallocAnalysis *analysis);

#endif
