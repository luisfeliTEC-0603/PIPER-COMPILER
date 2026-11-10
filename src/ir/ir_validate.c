/**
 * @file ir_validate.c
 * @brief Estructura del control de consistencia previo a publicar la IR.
 *
 * Solo declaración. No repite el análisis semántico del lenguaje: verifica que
 * la traducción haya producido estructuras y referencias coherentes.
 */
#include "ir_internal.h"

/**
 * @brief Revisará el modelo completo sin modificarlo ni ejecutarlo.
 * @param program IR completa prestada, todavía propiedad del generador.
 * @param diagnostic Error opcional con rutina, bloque e instrucción.
 * @return IR_OK si cumple los invariantes; error estructural/tipado en otro caso.
 *
 * Lista de comprobaciones para implementar:
 * - IDs únicos en su ámbito, referencias válidas y tipos exactos aplicables.
 * - Firmas, parámetros y retornos compatibles; none sin resultado ficticio.
 * - Un terminador por bloque y destinos dentro de la misma rutina.
 * - Una definición por temporal, sin usar valores exclusivos de otra rama.
 * - Operandos/atributos/efectos correspondientes a cada operación.
 *
 * No precisa CFG, dominadores, conjuntos de vivos ni registros físicos. La
 * disponibilidad de valores seguirá el contexto estructurado de emisión.
 */
IrStatus ir_validate(const IrProgram *program, IrDiagnostic *diagnostic);
