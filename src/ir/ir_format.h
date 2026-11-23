#ifndef PIPER_IR_FORMAT_H
#define PIPER_IR_FORMAT_H

/* Formato interno compartido por .ir y etiquetas DOT; no es otra IR ni API
 * pública. Una vista presta rutina/programa y posee solo el índice values.
 * El llamador libera values al terminar, también si falla la escritura. */
#include "ir.h"

typedef struct {
    const IrProgram *program;
    const IrRoutine *routine;
    IrOperand *values;
    size_t value_count;
} IrPrintView;

/* Inicializar una vista nueva desde el modelo validado; sin leer archivos.
 * Si falla, libera sus reservas y deja values=NULL. */
IrStatus ir_print_prepare(IrPrintView *view, const IrProgram *program, size_t routine);
IrStatus ir_print_header(FILE *output, const IrPrintView *view);
IrStatus ir_print_block(FILE *output, const IrPrintView *view, const IrBlock *block);

#endif
