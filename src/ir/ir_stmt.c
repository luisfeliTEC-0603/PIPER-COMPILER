/**
 * @file ir_stmt.c
 * @brief Estructura de traducción de sentencias, destinos y control de flujo.
 *
 * Solo firmas. Las sentencias emiten efectos; las expresiones devuelven valores.
 * Los bloques de IR se crean durante este recorrido, sin construir un CFG.
 */
#include "ir_internal.h"

/**
 * @brief Traducirá una sentencia y actualizará el camino de ejecución actual.
 * @param ctx Contexto mutable; recibe instrucciones y bloques.
 * @param node Sentencia AST prestada.
 * @return Estado; current_block=IR_NO_ID indicará un camino ya terminado.
 *
 * Asignación: resolver destino y origen en el orden del contrato.
 * when: condición, dos ramas y unión solo si hay continuación.
 * alwhen: condición reevaluada, cuerpo y salida.
 * loop: inicio una vez, fin por vuelta, avance y salida; paso literal positivo.
 * break/output: terminar el bloque, sin emitir instrucciones posteriores.
 * No conservar nombres como sustituto de la declaración semántica resuelta.
 */
IrStatus ir_generate_stmt(IrContext *ctx, const AstNode *node);

/**
 * @brief Resolverá dónde escribir, sin leer todavía el contenido del destino.
 * @param ctx Contexto mutable para emitir cálculo/comprobación de índices.
 * @param node Identificador o índice que el semántico aprobó como modificable.
 * @param out Símbolo escalar o dirección estable y tipo del dato almacenado.
 * @return Estado; out válido solo en éxito. El caller evalúa luego el origen.
 * @note Una dirección indexada se calcula antes del origen y no se recalcula
 * al emitir STORE, aunque el origen llame rutinas o modifique variables.
 */
IrStatus ir_generate_lvalue(IrContext *ctx, const AstNode *node, IrLValue *out);

/**
 * @brief Recorrerá en orden una lista de sentencias de un bloque léxico.
 * @param ctx Estado mutable; conservará/restaurará el ámbito que corresponda.
 * @param block AST_BLOCK prestado; no es lo mismo que un bloque básico de IR.
 * @return Estado; si el camino termina, omite las sentencias posteriores.
 * @note No hace DCE global. El cuerpo de rutina puede reutilizar el ámbito de
 * la rutina en lugar de exigir block->semantic_scope distinto de NULL.
 */
IrStatus ir_generate_block(IrContext *ctx, const AstNode *block);
