#ifndef PIPER_CLONE_DETECTOR_H
#define PIPER_CLONE_DETECTOR_H

/**
 * @file clone_detector.h
 * @brief Detección de clones de código Tipo I y Tipo II sobre el AST.
 *
 * El detector recorre todos los nodos del AST, extrae fragmentos cuyo nodo
 * raíz es una sentencia o un bloque, calcula un hash tipo Merkle por subárbol
 * en dos modos (exacto y renombrado) y agrupa los fragmentos con el mismo
 * hash. Los grupos con al menos dos ubicaciones se reportan como clones.
 *
 * La integración con el IDE escribe JSON a stdout, con un formato estable
 * pensado para ser consumido por el panel de clones de Python.
 */

#include "ast.h"
#include <stdio.h>
#include <stdint.h>

/** Modo de normalización de identificadores y literales. */
typedef enum {
    /** Tipo I: se conservan identificadores y literales; solo se ignora el
     * formato (espacios, comentarios, saltos de línea), que el AST ya omite. */
    CLONE_MODE_EXACT = 0,
    /** Tipo II: identificadores → $IDn (renombrado consistente por primera
     * aparición) y literales enteros → $INT. Estructura y operadores se
     * conservan. Dos fragmentos son clones si comparten hash en este modo. */
    CLONE_MODE_RENAMED = 1
} CloneMode;

/** Una ubicación concreta de un fragmento en el código fuente. */
typedef struct {
    int start_line;
    int start_column;
    int end_line;
    int end_column;
    int node_count;   /* número de nodos del subárbol (para filtrar ruido) */
} CloneLocation;

/** Grupo de fragmentos idénticos. */
typedef struct CloneGroup {
    CloneMode mode;                 /* I o II */
    unsigned long long hash;        /* huella del subárbol normalizado */
    size_t location_count;          /* >= 2 para ser reportado */
    CloneLocation *locations;       /* arreglo de location_count elementos */
    struct CloneGroup *next;
} CloneGroup;

/** Resultado completo de una detección. */
typedef struct {
    CloneGroup *groups;             /* lista enlazada de grupos */
    size_t group_count;
    size_t fragment_count;          /* total de fragmentos analizados */
} CloneReport;

/**
 * Ejecuta la detección sobre el AST de un programa.
 *
 * @param root            Raíz del AST (AST_PROGRAM).
 * @param min_nodes       Tamaño mínimo de subárbol, en nodos, para ser
 *                        considerado candidato. Filtrar fragmentos pequeños
 *                        evita ruido. Recomendado: 8.
 * @param file_path       Ruta del archivo de origen, para reportar ubicaciones.
 * @param mode            CLONE_MODE_EXACT o CLONE_MODE_RENAMED.
 *
 * @return Puntero a un CloneReport que el llamador debe liberar con
 *         clone_report_free. Devuelve NULL si no hay memoria.
 */
CloneReport *clone_detect(const AstNode *root, int min_nodes,
                          const char *file_path, CloneMode mode);

/** Libera un reporte completo. */
void clone_report_free(CloneReport *report);

/** Escribe el reporte como JSON a stdout (o a cualquier FILE). */
void clone_report_write_json(FILE *output, const CloneReport *report,
                             const char *file_path, int min_nodes,
                             CloneMode mode);

#endif /* PIPER_CLONE_DETECTOR_H */
