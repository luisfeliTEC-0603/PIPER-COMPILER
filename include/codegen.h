#ifndef PIPER_CODEGEN_H
#define PIPER_CODEGEN_H

#include "ast.h"
#include "symbol_table.h"
#include <stdio.h>

/*
 * Resultado de la generación de código.
 * Cada valor distinto de CODEGEN_OK indica una clase de fallo.
 */

typedef enum {
    CODEGEN_OK = 0,
    CODEGEN_ERROR_SIN_MEMORIA,
    CODEGEN_ERROR_TABLA_INVALIDA,
    CODEGEN_ERROR_AST_INVALIDO,
    CODEGEN_ERROR_IO,
    CODEGEN_ERROR_INTERNO
} CodegenResult;

/*
 * Opciones de generación. Se pasan como puntero; NULL usa valores por defecto.
 */

typedef struct {
    int emitir_comentarios;   /* 1 = incluir ; comentarios en el ASM */
    int optimizar_temporales; /* Reservado para futuro */
} CodegenOptions;

/*
 * Punto de entrada único del generador de código.
 *
 * - root:     AST ya validado semánticamente.
 * - table:    tabla de símbolos ya poblada (con offsets/direcciones).
 * - output:   archivo destino del ASM (abierto por el llamador).
 * - options:  puede ser NULL para valores por defecto.
 *
 * Retorna CODEGEN_OK si todo salió bien.
 */

CodegenResult codegen_generate(const AstNode *root,
                               const SymbolTable *table,
                               FILE *output,
                               const CodegenOptions *options);

#endif
