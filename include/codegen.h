#ifndef PIPER_CODEGEN_H
#define PIPER_CODEGEN_H

#include "ast.h"
#include "symbol_table.h"
#include <stdio.h>

/* Resultado de la generación de código. */

typedef enum {
    CODEGEN_OK = 0,
    CODEGEN_ERROR_SIN_MEMORIA,
    CODEGEN_ERROR_TABLA_INVALIDA,
    CODEGEN_ERROR_AST_INVALIDO,
    CODEGEN_ERROR_IO,
    CODEGEN_ERROR_INTERNO
} CodegenResult;

/* Si options es NULL se usan los valores por defecto. */

typedef struct {
    int emitir_comentarios;   /* Incluye comentarios en el ASM. */
    int optimizar_temporales; /* Sin uso actualmente. */
} CodegenOptions;

/* Genera ASM a partir de un AST validado y su tabla de símbolos. */

CodegenResult codegen_generate(const AstNode *root,
                               const SymbolTable *table,
                               FILE *output,
                               const CodegenOptions *options);

#endif
