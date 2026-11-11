/** @file ir_stmt.c
 * @brief Estructura de sentencias y flujo de control. Solo declaración.
 */
#include "ir_internal.h"

/** Entrada: sentencia o bloque AST. Salidas: efectos/bloques o error en contexto.
 * Asignación: ir_target -> ir_expr -> COPY/STORE mediante ir_emit.
 * Literales/copia de arreglo se escriben por filas, sin alterar efectos o alias.
 * when: ir_expr -> BRANCH -> ramas -> unión si alguna continúa.
 * alwhen: condición -> cuerpo -> vuelta; loop agrega inicio y avance explícitos.
 * break usa el último destino apilado; output emite RETURN.
 * Usa ctx->block=IR_NO_ID para un camino terminado y omite su resto.
 * El manejo de bloques léxicos, pila de break y ciclos quedará en helpers static;
 * no duplica el camino de emisión para cada tipo de terminador. */
void ir_stmt(IrContext *ctx, const AstNode *node);
