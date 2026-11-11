/** @file ir_expr.c
 * @brief Evaluación de expresiones y resolución de destinos; solo declaraciones.
 * I3 cubrirá escalares; I4 llamadas/retornos; I7 índices e intrínsecas.
 */
#include "ir_internal.h"

/** Entradas: contexto y expression aprobada, sin mover ni modificar sus nodos.
 * Salida: constante o temporal tipado; NONE para none. Si falla también devuelve
 * NONE, con ctx->error.status distinto de IR_OK para distinguir ambos casos.
 * Captura a con t=a antes de evaluar el siguiente efecto; luego emite la operación
 * padre. Un literal puede devolverse sin emitir instrucción. Las anotaciones y
 * símbolos determinan tipos: no elegir simplemente el operando de mayor tamaño.
 * Hijos y argumentos se evalúan una vez, de izquierda a derecha; and/or no
 * introducen cortocircuito. ++/-- prefijos devuelven el valor nuevo comprobado.
 * Call e índices se descomponen aquí con helpers static: memoria, firmas y efectos
 * se conservan, pero no se asignan registros/pila ni se calculan variables vivas. */
IrOperand ir_expr(IrContext *ctx, const AstNode *expression);

/** Entradas: contexto y target aprobado para escritura o acceso indexado.
 * Salida: operando de símbolo escalar, o dirección temporal con tipo de elemento.
 * No carga el dato ni escribe el destino. Para índices, evalúa/comprueba cada uno
 * una vez y calcula base+desplazamiento con dimensiones de Symbol y ancho de
 * piper_type_storage_size. La asignación evalúa luego su origen y reutiliza esa
 * dirección; una expresión indexada la usa para emitir LOAD.
 * Fallos se registran en contexto; el valor devuelto entonces no debe utilizarse. */
IrOperand ir_target(IrContext *ctx, const AstNode *target);
