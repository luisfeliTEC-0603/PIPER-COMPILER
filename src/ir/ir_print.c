/**
 * @file ir_print.c
 * @brief Estructura de exportación del modelo IR como TAC legible.
 *
 * Solo declaración. El modelo en memoria será la entrada de los consumidores;
 * el archivo es una vista para inspección, no un protocolo a volver a analizar.
 */
#include "ir_internal.h"

/**
 * @brief Escribirá rutinas, datos, bloques e instrucciones en orden determinista.
 * @param output Stream prestado y abierto por el orquestador.
 * @param program IR validada, prestada e inmutable.
 * @return IR_OK, IR_INVALID_INPUT o IR_IO_ERROR; no altera el programa.
 *
 * Mantener nombres originales, distinguir homónimos por ámbito cuando haga
 * falta y evitar colisiones visuales entre nombres de usuario y temporales.
 * Mostrar tipos, destinos y atributos de comprobación/efectos relevantes.
 * No imprimir registros físicos ni offsets de spill. El caller administra
 * archivo temporal, cierre, publicación atómica y limpieza al fallar.
 */
IrStatus ir_write(FILE *output, const IrProgram *program);
