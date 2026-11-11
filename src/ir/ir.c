/** @file ir.c
 * @brief Almacenamiento del modelo y única entrada de emisión.
 * Solo contratos y declaraciones: I1 implementará reservas/liberación; I6 añadirá
 * la descripción de efectos. Estos recursos son del proceso pipoc, no de PORYGON.
 */
#include "ir_internal.h"

/** Entrada: contexto con current_routine válido y sin error previo.
 * Salida: ID estable de un bloque nuevo o IR_NO_ID con el primer fallo guardado.
 * Agrega almacenamiento a blocks, con instruction_count=0, sin seleccionar el
 * bloque. El llamador selecciona después current_block; no se emite ningún salto.
 * No conserva punteros a elementos que puedan moverse cuando crezca el arreglo. */
IrId ir_new_block(IrContext *ctx);

/** Entrada: contexto, bloque abierto e instruction pasada por valor.
 * Salida: instrucción registrada o error en ctx->error. Calcula sus efectos desde
 * opcode, atributos y símbolos; incrementa instruction_count solo tras registrarla.
 * JUMP/BRANCH/RETURN cierran el camino dejando current_block=IR_NO_ID.
 * Adquiere los argumentos de CALL/INTRINSIC incluso si no puede emitir; deberá
 * liberarlos en ese caso. Ningún otro opcode interpreta o libera extra.call.
 * No reordena ni optimiza; un IR_CHECKED se refiere al tipo de result. */
void ir_emit(IrContext *ctx, IrInstruction instruction);

/** Entrada: código de fallo, origen y mensaje prestado.
 * Salida: primer diagnóstico copiado en contexto; no sustituye uno anterior.
 * No imprime ni libera la construcción. Los recorridos consultan error.status
 * antes de continuar; ir_generate hará la limpieza al recibir ese fallo. */
void ir_fail(IrContext *ctx, IrStatus status, SourceLocation location,
             const char *message);

/** Entrada: programa completo/parcial propio, o NULL.
 * Salida: memoria propia liberada una vez. Recorre routine_count, block_count e
 * instruction_count, y libera argumentos solo de CALL/INTRINSIC antes de sus
 * instrucciones/bloques/rutinas. No libera los símbolos ni el AST prestados.
 * La pila de break es del contexto: ir_generate la libera por separado. */
void ir_program_free(IrProgram *program);
