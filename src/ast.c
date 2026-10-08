#include "ast.h"

/*
 * Implementación del AST de Piper.
 *
 * Este archivo queda deliberadamente como skeleton. El responsable del AST
 * implementará aquí las estructuras opacas declaradas en ast.h y garantizará
 * que cada constructor respete el contrato de propiedad documentado.
 */

/* TODO(ast-core): definir struct AstNodeList. */

/*
 * TODO(ast-core): definir struct AstNode con:
 *
 * - AstKind kind;
 * - SourceLocation location;
 * - una union con los datos específicos de cada variante.
 *
 * No es necesario guardar palabras reservadas ni puntuación como nodos.
 */

/* TODO(ast-core): implementar source_location_make(). */

/* TODO(ast-core): implementar creación, append y liberación de listas. */

/* TODO(ast-expressions): implementar literales, nombres y operaciones. */

/* TODO(ast-statements): implementar declaraciones, asignaciones y bloques. */

/* TODO(ast-control): implementar when, alwhen, loop y break. */

/* TODO(ast-routines): implementar parámetros, rutinas, llamadas y output. */

/* TODO(ast-composites): implementar arreglos, índices e importaciones. */

/* TODO(ast-print): implementar impresión indentada para todas las variantes. */

/*
 * TODO(ast-memory): implementar ast_free() de forma recursiva y revisar el
 * camino de error de cada constructor con sanitizadores cuando exista la
 * implementación.
 */
