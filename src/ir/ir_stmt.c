/** @file ir_stmt.c
 * @brief Traducción de sentencias y caminos; solo declaración.
 */
#include "ir_internal.h"

/** Entradas: contexto y statement, que puede ser sentencia o bloque léxico AST.
 * Salidas: efectos/bloques en el programa y current_block actualizado; no devuelve
 * un valor de expresión. Error en contexto aborta el recorrido.
 *
 * Reglas que guiarán el cuerpo:
 * - Declarar no inicializa ni vuelve a asignar memoria a las variables.
 * - Asignar: ir_target -> ir_expr -> ir_emit con COPY/STORE.
 * - when: condición una vez, destinos verdadero/falso y unión si se continúa.
 * - alwhen: condición reevaluada -> cuerpo o salida; vuelta a condición.
 * - loop: inicio una vez, fin por vuelta, cuerpo y avance; paso literal positivo.
 * - break: salto a última salida apilada; output: RETURN del valor/none.
 * - Cerrar un camino pone current_block=IR_NO_ID; no emitir luego otro salto.
 * - Inicialización/copia de arreglos mantiene orden por filas y posibles alias.
 *
 * Pila de ciclos y helpers de control quedan aquí como detalles static futuros;
 * no hace análisis de liveness, optimizaciones ni CFG. */
void ir_stmt(IrContext *ctx, const AstNode *statement);
