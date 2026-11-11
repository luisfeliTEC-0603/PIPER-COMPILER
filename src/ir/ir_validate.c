/** @file ir_validate.c
 * @brief Estructura del validador de la traducción. Solo declaración.
 */
#include "ir.h"

/** Entrada: programa completo prestado. Salida: estado y diagnóstico opcional.
 * Revisa tipos/firmas, símbolos finales, temporales definidos y destinos locales.
 * Cada bloque debe tener un único JUMP/BRANCH/RETURN, en su última posición.
 * La lectura/escritura explícita se reconoce por opcode, arg1/arg2/result y los
 * argumentos de llamada; los efectos indirectos permanecen en flags.
 * No modifica el modelo ni calcula CFG, liveness o registros físicos. */
IrStatus ir_validate(const IrProgram *program, IrDiagnostic *diagnostic);
