/** @file ir_print.c
 * @brief Vista legible fiel del modelo publicado; solo declaración.
 */
#include "ir.h"

/** Entradas: output abierto/escribible y program validado con tabla aún viva.
 * Salidas: cabeceras, TAC y comentarios en el stream, más estado de entrada/E/S.
 * Recorre el modelo, no el AST. Imprime firma, variables y dimensiones desde los
 * símbolos; temporales y constantes con tipos desde sus operandos, sin mantener
 * otra tabla persistente ni imprimir capacidades/punteros del proceso compilador.
 *
 * Usa =, +, -, *, /, %, ^, comparaciones, jump, if ... jump ... else ... y return.
 * Las instrucciones no repiten tipos salvo ambigüedad. Cada flag activo aparece
 * según el contrato; IR_CHECKED muestra «comprueba rango del resultado B32» si
 * result.type es B32. Sin comentario significa flags=0, no información oculta.
 *
 * No modifica, cierra ni publica archivos. El llamador comprueba fclose y realiza
 * publicación atómica. Los formateadores específicos futuros serán static aquí. */
IrStatus ir_write(FILE *output, const IrProgram *program);
