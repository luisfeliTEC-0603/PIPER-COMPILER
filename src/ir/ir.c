/** @file ir.c
 * @brief Estructura de almacenamiento y emisión común. Solo declaraciones.
 * Aquí se implementarán las reservas necesarias, sin exponer un método por
 * arreglo, contador, tipo o lista. La tabla de símbolos no se duplica.
 */
#include "ir_internal.h"

/** Entrada: rutina activa. Salida: nuevo ID estable, o error en contexto.
 * Añade un bloque sin seleccionarlo; los saltos pueden referir salidas futuras. */
IrId ir_new_block(IrContext *ctx);

/** Entrada: instrucción y bloque activo. Salida: operación agregada o error.
 * Normal/call/control pasan por el mismo punto. Debe calcular flags desde op,
 * atributos y símbolos globales; no reordenar ni eliminar nada.
 * JUMP/BRANCH/RETURN cierran el bloque; ninguna emisión puede seguir allí.
 * Adquiere la lista de argumentos incondicionalmente y la libera si no emite. */
void ir_emit(IrContext *ctx, IrInstruction instruction);

/** Entrada: fallo y ubicación. Salida: primer diagnóstico guardado por valor.
 * Todos los auxiliares deben respetar error.status antes de continuar. */
void ir_fail(IrContext *ctx, IrStatus status, SourceLocation location,
             const char *message);

/** Consumirá bloques, instrucciones, argumentos y rutinas, también parciales.
 * NULL válido; la tabla semántica prestada permanece intacta. Sin cuerpo aún. */
void ir_program_free(IrProgram *program);
