/** @file ir_validate.c
 * @brief Validación de la traducción; solo declaración, no repite semántica.
 */
#include "ir.h"

/** Entrada: programa completo prestado. Salida: estado y diagnóstico opcional.
 * Comprobará referencias a la tabla final, firma de llamadas y tipos de operandos,
 * incluidos ausencia de resultado en none y resultados booleanos de comparación.
 * Verificará ID/definición única de temporales, disponibilidad según el recorrido
 * estructurado, destinos locales y único salto/condición/retorno al final de cada
 * bloque. Los conteos determinan qué elementos están registrados, no capacity.
 * No ejecuta el fuente ni modifica el modelo; no requiere CFG ni variables vivas.
 * El programa solo se publica si esta función aprueba sus invariantes. */
IrStatus ir_validate(const IrProgram *program, IrDiagnostic *diagnostic);
