#ifndef PIPER_CFG_H
#define PIPER_CFG_H

/** @file cfg.h
 * Grafo de flujo normal sobre bloques IR existentes. No copia instrucciones.
 * C1–C5 implementan almacenamiento, conexiones, alcance, revisión y exportación DOT.
 * Mantener vivos IR y su tabla final hasta liberar todos sus grafos.
 */
#include "ir.h"

/* Índice ausente de nodo/arista/rutina CFG. Un ID de bloque ausente usa IR_NO_ID.
 * Aunque ambos valen SIZE_MAX, nombran cosas distintas: índice CFG frente a ID IR. */
#define CFG_NO_INDEX SIZE_MAX

typedef enum {
    CFG_OK = 0, CFG_INVALID_INPUT, CFG_INVALID_GRAPH,
    CFG_OUT_OF_MEMORY, CFG_IO_ERROR
} CfgStatus;

typedef struct {
    CfgStatus status;
    SourceLocation location;
    size_t routine;       /**< Posición de rutina IR, no nombre ni dirección. */
    IrId block;           /**< ID del bloque IR, si el fallo corresponde a uno. */
    size_t instruction;   /**< Posición de instrucción, si aplica. */
    size_t edge;          /**< Posición de arista CFG, si aplica. */
    char message[256];
} CfgDiagnostic;

typedef enum { CFG_NODE_ENTRY, CFG_NODE_BLOCK, CFG_NODE_EXIT } CfgNodeKind;
typedef enum {
    CFG_EDGE_ENTRY, CFG_EDGE_JUMP, CFG_EDGE_TRUE, CFG_EDGE_FALSE, CFG_EDGE_RETURN
} CfgEdgeKind;

/** Nodo sin copia del bloque. Los vecinos son índices en CfgRoutine.nodes,
 * sin duplicados. ENTRY/EXIT no representan un bloque: block=IR_NO_ID.
 * reachable empieza en false; cfg_build lo calcula desde ENTRY antes de entregar. */
typedef struct {
    CfgNodeKind kind;
    IrId block;
    bool reachable;
    size_t *successors;
    size_t successor_count;
    size_t *predecessors;
    size_t predecessor_count;
} CfgNode;

/** Una conexión etiquetada. TRUE/FALSE pueden compartir destino, pero siguen
 * siendo dos aristas. source/target son índices CFG, nunca IDs de bloque IR. */
typedef struct {
    size_t source, target;
    CfgEdgeKind kind;
} CfgEdge;

/** Posee sus nodos/aristas y las listas de vecinos de cada nodo.
 * Los tamaños de la IR ya se conocen: reservar arreglos del tamaño necesario,
 * sin campos de capacidad ni funciones de crecimiento anticipadas. */
typedef struct {
    size_t routine;       /**< Posición de la rutina correspondiente en IR. */
    CfgNode *nodes;
    size_t node_count;
    CfgEdge *edges;
    size_t edge_count;
    size_t entry, exit;   /**< Índices de los nodos sintéticos. */
} CfgRoutine;

/** Posee los grafos, en el mismo orden que las rutinas del IR prestado.
 * No libera IR, símbolos ni AST. Cambiar bloques/terminadores requiere reconstruir. */
typedef struct {
    const IrProgram *ir;
    CfgRoutine *routines;
    size_t routine_count;
} CfgProgram;

/* IR validada -> grafo conectado, revisado y con alcanzabilidad calculada.
 * out debe apuntar a NULL; publicar solo en éxito y descartar parciales al fallar.
 * No leer .ir, ejecutar frontend ni imprimir diagnósticos. */
CfgStatus cfg_build(const IrProgram *ir, CfgProgram **out, CfgDiagnostic *diagnostic);

/* C3: revisar estructura sin repetir semántica, resolver destinos ni recalcular
 * alcance. No modifica el grafo ni valida tipos Piper. */
CfgStatus cfg_validate(const CfgProgram *cfg, CfgDiagnostic *diagnostic);

/* C4: CFG completo de cfg_build -> DOT determinista en un stream prestado.
 * No cerrarlo ni publicar archivos aquí; el llamador comprueba fclose. */
CfgStatus cfg_write_dot(FILE *output, const CfgProgram *cfg);

/* Liberar también construcciones parciales; acepta NULL y no toca IR prestada.
 * Tras liberar, el llamador no vuelve a utilizar el puntero. Implementada en C1. */
void cfg_program_free(CfgProgram *cfg);

#endif
