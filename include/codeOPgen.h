#ifndef PIPER_CODEOPGEN_H
#define PIPER_CODEOPGEN_H

/** @file codeOPgen.h
 * Emisor ASM de la maquina final de regalloc. No recorre AST, no asigna
 * registros y no reordena instrucciones. La entrada se presta durante la
 * llamada; main conserva asignacion, IR y simbolos hasta terminar la emision.
 */
#include "regalloc_machine.h"

typedef enum {
    CODEOPGEN_OK = 0, CODEOPGEN_INVALID_INPUT, CODEOPGEN_INVALID_MACHINE,
    CODEOPGEN_OUT_OF_MEMORY, CODEOPGEN_IO_ERROR
} CodeOPgenResult;

typedef struct {
    bool comments; /**< Describe operaciones virtuales en el ASM, si se solicita. */
} CodeOPgenOptions;

typedef struct {
    CodeOPgenResult status;
    size_t routine, instruction;
    IrId block;
    SourceLocation location;
    char message[256];
} CodeOPgenDiagnostic;

/** Revisa los registros asignados y las llamadas antes de escribir. Una operacion virtual
 * puede expandirse a varias instrucciones/etiquetas; el scheduler posterior
 * conserva dependencias e inserta los NOPs. El stream no se cierra. Ante error
 * de escritura puede contener un prefijo: el llamador no debe publicarlo.
 * options/diagnostic son opcionales; por defecto no se escriben comentarios.
 */
CodeOPgenResult codeOPgen_generate(const RegallocAssignedProgram *assigned,
                                  FILE *output, const CodeOPgenOptions *options,
                                  CodeOPgenDiagnostic *diagnostic);
#endif
