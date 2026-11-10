/**
 * @file ir_call.c
 * @brief Estructura de traducción de llamadas e intrínsecas criptográficas.
 *
 * Solo firmas. Las convenciones r1..r7 y la reserva del frame son decisiones
 * posteriores del backend; aquí solo hay firma, valores, destino y efectos.
 */
#include "ir_internal.h"

/**
 * @brief Traducirá una llamada a una rutina resuelta del mismo archivo.
 * @param ctx Contexto con la rutina destino registrada por ID.
 * @param call AST_CALL_EXPRESSION validado con enlace semántico final.
 * @param out_value Temporal para retorno con valor o NONE para retorno none.
 * @return Estado; los efectos quedan como instrucción CALL en el bloque actual.
 *
 * Evalúa argumentos de izquierda a derecha y captura cada valor antes de pasar
 * al siguiente; arreglos viajan por dirección. La lista de operandos se vuelve
 * propiedad de CALL al emitir con éxito. Se conserva la llamada aunque nadie
 * consuma su resultado. Recursión no implica ejecutar la rutina al compilar.
 */
IrStatus ir_generate_call(IrContext *ctx, const AstNode *call,
                          IrOperand *out_value);

/**
 * @brief Representará auth/write_key/feistel/revoke sin expandir ASM.
 * @param ctx Contexto para evaluar argumentos y emitir INTRINSIC.
 * @param call Llamada intrínseca que el semántico aprobó, prestada.
 * @param out_value NONE para las intrínsecas actuales, sin resultado escalar.
 * @return Estado; preservará estado criptográfico y memoria observables.
 * @note Feistel lee y escribe el bloque recibido por referencia. Índices de
 * llave/ronda conservan su condición de constantes ya validadas. No tratar
 * estas operaciones como llamadas puras ni eliminarlas por no devolver valor.
 */
IrStatus ir_generate_intrinsic(IrContext *ctx, const AstNode *call,
                               IrOperand *out_value);
