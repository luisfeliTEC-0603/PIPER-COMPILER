/** @file ir_expr.c
 * @brief Estructura de evaluación de valores y destinos. Solo declaraciones.
 * Incluye llamadas, intrínsecas y accesos indexados: todos son expresiones o
 * partes de su evaluación, no requieren otros módulos de interfaces públicas.
 */
#include "ir_internal.h"

/** Entrada: expresión validada. Salida: constante/temporal, o NONE para none.
 * Un error también devuelve NONE, pero queda identificado por ctx->error.status.
 * Captura un identificador con t = símbolo (IR_OP_COPY) antes del siguiente efecto.
 * Evalúa hijos/argumentos de izquierda a derecha y una vez; and/or evalúan ambos lados.
 * Conserva comprobaciones de aritmética/índices, signo y ancho exactos.
 * CALL guarda Symbol destino + argumentos; INTRINSIC conserva memoria/cripto.
 * ++/-- prefijos actualizan y devuelven el valor nuevo. Sin registros ni spills.
 * Los auxiliares de aritmética, llamadas e índices serán static dentro del archivo. */
IrOperand ir_expr(IrContext *ctx, const AstNode *node);

/** Entrada: destino aprobado. Salida: símbolo escalar o dirección ya calculada.
 * No lee el contenido. Para índices, evalúa y comprueba antes de calcular base
 * + desplazamiento; obtiene dimensiones de Symbol y ancho de piper_type_storage_size.
 * La asignación evalúa el origen después y reutiliza esta dirección una sola vez.
 * El mismo método sirve al LOAD de una expresión indexada. */
IrOperand ir_target(IrContext *ctx, const AstNode *node);
