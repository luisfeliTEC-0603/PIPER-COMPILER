/**
 * @file module_loader.h
 * @brief Resolución de `bring <rutina> from "<archivo>.rwt".` entre archivos.
 *
 * Se ejecuta entre el parser y el análisis semántico. Cada importación se
 * reemplaza, dentro del AST del programa, por la rutina pedida y las rutinas
 * de su mismo módulo que esta invoca (cierre transitivo). Así la semántica y
 * la generación de IR reciben un único programa, sin cambios en ellas.
 */
#ifndef MODULE_LOADER_H
#define MODULE_LOADER_H

#include <stddef.h>

#include "ast.h"

typedef enum {
    MODULE_OK = 0,     /* Todas las importaciones se resolvieron. */
    MODULE_ERRORS,     /* Hubo errores recuperables: seguir para reportar más. */
    MODULE_FATAL       /* Ciclo, conflicto de nombres o memoria: detener. */
} ModuleStatus;

/** Archivo de origen de cada rutina importada. Una rutina que no figura en el
 * mapa pertenece al archivo principal. Sobrevive al loader; se libera aparte. */
typedef struct SourceMap SourceMap;

/** Ruta canónica del archivo que define `top_level`; NULL = archivo principal. */
const char *source_map_file(const SourceMap *map, const AstNode *top_level);
void source_map_free(SourceMap *map);

/**
 * Reemplaza las importaciones de `program` por las rutinas importadas.
 * @param source_path   archivo del programa; las rutas relativas parten de su carpeta.
 * @param include_dirs  carpetas adicionales de búsqueda (opción -I), puede ser NULL.
 * @param sources       si no es NULL, recibe el mapa de orígenes (NULL si hay MODULE_FATAL).
 *                      Debe liberarse con source_map_free DESPUÉS de usar el AST/reportes.
 * Los errores se imprimen en stderr y se registran como diagnósticos.
 */
ModuleStatus module_resolve_imports(AstNode *program, const char *source_path,
                                    const char *const *include_dirs,
                                    size_t include_count, SourceMap **sources);

#endif
