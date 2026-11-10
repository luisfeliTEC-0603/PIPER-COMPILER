/**
 * @file ir_expr.c
 * @brief Estructura de evaluación de expresiones y creación de valores TAC.
 *
 * Solo declaración. Flujo previsto: literal/identificador o hijos recursivos
 * -> operandos estables -> una operación TAC -> valor de salida.
 * Llamadas e indexación se delegarán a ir_call.c e ir_memory.c.
 */
#include "ir_internal.h"

/**
 * @brief Traducirá una expresión y devolverá el valor ya evaluado.
 * @param ctx Rutina y bloque activos; recibe las instrucciones emitidas.
 * @param node Expresión AST validada, prestada e inmutable.
 * @param expected_type Contexto de destino/argumento/retorno; UNSPECIFIED si no
 * existe. No estrecha resultados intermedios ya tipados por el semántico.
 * @param out_value Constante/temporal en éxito; NONE para una expresión none.
 * @return Estado separado del valor: NONE con IR_OK no significa fallo.
 *
 * Reglas que deberá cumplir el cuerpo:
 * - Capturar cada lectura escalar en t=COPY símbolo antes del siguiente efecto.
 * - Evaluar izquierda y luego derecha; and/or no introducen cortocircuito.
 * - Conservar ancho, signo y comprobaciones dinámicas del backend actual.
 * - ++/-- prefijos devuelven el valor nuevo y escriben el destino una sola vez.
 * - No asignar registros, reservar spills, plegar constantes ni calcular vivos.
 */
IrStatus ir_generate_expr(IrContext *ctx, const AstNode *node,
                          IrType expected_type, IrOperand *out_value);
