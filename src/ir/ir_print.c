/** @file ir_print.c
 * @brief Estructura de impresión del modelo como TAC. Solo declaración.
 */
#include "ir.h"

/** Entrada: stream prestado e IR validada. Salida: texto determinista y estado.
 * Lee firmas/declaraciones de la tabla prestada; nombres homónimos se califican
 * solo si hace falta. Declara variables, temporales y constantes tipadas en
 * cabeceras; las instrucciones quedan sencillas, sin repetir tipos innecesarios.
 * Usa =, +, -, *, /, %, ^, ==, != y operadores similares donde corresponda.
 * La última instrucción se imprime como jump, if ... jump ... else ... o return.
 * Cada flag activo se muestra en un comentario según el contrato; sin comentario
 * significa flags=0. No oculta tipos/efectos de la IR ni imprime punteros/capacidades.
 * No modifica IR, no cierra output y no publica archivos: lo hará el orquestador.
 * Las funciones de formato específicas serán static dentro de este módulo. */
IrStatus ir_write(FILE *output, const IrProgram *program);
