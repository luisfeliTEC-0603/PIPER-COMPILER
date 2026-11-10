/**
 * @file ir_blocks.c
 * @brief Estructura de construcción de bloques básicos y terminadores.
 *
 * Solo firmas, sin algoritmo. Orden de uso previsto:
 *   new_block -> select_block -> emit_instruction* -> terminate_block.
 * Repetir para ramas/ciclos y terminar con finalize_blocks por rutina.
 * Las conexiones de predecesores/sucesores corresponderán al módulo CFG futuro.
 */
#include "ir_internal.h"

/**
 * @brief Creará un bloque sin terminador en la rutina activa.
 * @param ctx Contexto con rutina activa; el arreglo de bloques puede moverse.
 * @param out ID estable asignado en éxito.
 * @return Estado; crear no selecciona el bloque ni emite un salto hacia él.
 */
IrStatus ir_new_block(IrContext *ctx, IrBlockId *out);

/**
 * @brief Establecerá el bloque donde continuar emitiendo instrucciones.
 * @param ctx Contexto mutable de la rutina activa.
 * @param block ID existente, de esta rutina y todavía abierto.
 * @return Estado; no establece caída implícita desde el bloque anterior.
 */
IrStatus ir_select_block(IrContext *ctx, IrBlockId block);

/**
 * @brief Registrará una operación no terminal, con ID y efectos coherentes.
 * @param ctx Contexto con bloque activo abierto.
 * @param instruction Descriptor cuya memoria variable se transfiere en éxito.
 * @param out ID de instrucción asignado en éxito.
 * @return Estado; en fallo no adquiere las listas propias del descriptor.
 * @note Rechazará JUMP/BRANCH/RETURN y cualquier emisión tras terminador.
 * El llamador descartará el descriptor tras éxito sin liberar de nuevo sus listas.
 */
IrStatus ir_emit_instruction(IrContext *ctx, IrInstruction *instruction,
                             IrInstructionId *out);

/**
 * @brief Cerrará un bloque con su única transferencia explícita de control.
 * @param ctx Bloque activo abierto; en éxito current_block queda IR_NO_ID.
 * @param terminator Descriptor JUMP, BRANCH o RETURN que se registra al final.
 * @param out ID asignado; comparte contador con instrucciones no terminales.
 * @return Estado; una segunda terminación es error, no reemplazo silencioso.
 * @note Destinos son IDs de bloques. Los bloques pueden crearse por adelantado
 * para permitir saltos de vuelta o hacia una salida todavía no seleccionada.
 */
IrStatus ir_terminate_block(IrContext *ctx, IrInstruction *terminator,
                            IrInstructionId *out);

/**
 * @brief Finalizará la estructura de bloques de la rutina activa.
 * @param ctx Contexto tras emitir todas las sentencias de la rutina.
 * @return Estado; comprobará destinos existentes y terminadores únicos.
 * @note No construirá CFG ni agregará return 0. Solo podrá completar un retorno
 * sin valor compatible con none según el contrato; un camino sin retorno de
 * valor es error. No consulta análisis de variables vivas.
 */
IrStatus ir_finalize_blocks(IrContext *ctx);

/**
 * @brief Apilará la salida del ciclo que comienza a traducirse.
 * @param ctx Contexto de la rutina; posee la pila dinámica de destinos.
 * @param target Bloque de salida válido de la rutina actual.
 * @return Estado; break consultará el último destino, no el primero.
 */
IrStatus ir_push_break_target(IrContext *ctx, IrBlockId target);

/**
 * @brief Retirará la salida al terminar de traducir un ciclo.
 * @param ctx Contexto con pila no vacía.
 * @param out Destino retirado, válido solo en éxito.
 * @return Estado; el llamador debe equilibrar push/pop incluso en ciclos
 * anidados. El aborto completo libera la pila mediante ir_context_dispose.
 */
IrStatus ir_pop_break_target(IrContext *ctx, IrBlockId *out);
