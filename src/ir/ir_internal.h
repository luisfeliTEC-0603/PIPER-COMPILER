#ifndef PIPER_IR_INTERNAL_H
#define PIPER_IR_INTERNAL_H

/** @file ir_internal.h
 * Estado compartido y siete auxiliares del futuro generador. Solo declaraciones.
 * Un primer error queda en error.status: las funciones siguientes no emiten más.
 * Así el resultado de una expresión no necesita otro parámetro de salida.
 */
#include "ir.h"

typedef struct {
    const AstNode *root;       /**< Entrada prestada, solo durante generación. */
    IrProgram *program;        /**< Propiedad provisional del orquestador. */
    IrId routine;              /**< Índice de rutina activa, no puntero movible. */
    IrId block;                /**< ID activo; IR_NO_ID si el camino terminó. */
    IrId *break_targets;       /**< Pila propia, vacía al comenzar cada rutina. */
    size_t break_count, break_capacity;
    IrDiagnostic error;       /**< IR_OK mientras el recorrido sea válido. */
} IrContext;

/* ir_generate.c: registra firmas y traduce rutinas/arranque desde ctx->root. */
void ir_build_program(IrContext *ctx);

/* ir_expr.c: devuelve un valor estable (NONE para none o fallo).
 * ctx->error.status distingue un none válido de un error. Los tipos se obtienen
 * del AST/símbolos; no se estrechan operaciones por el tipo de su consumidor. */
IrOperand ir_expr(IrContext *ctx, const AstNode *node);

/* ir_expr.c: resuelve destino sin cargar contenido. Devuelve símbolo escalar
 * o temporal de dirección con tipo de elemento; índices comprobados una vez. */
IrOperand ir_target(IrContext *ctx, const AstNode *node);

/* ir_stmt.c: emite sentencias/listas, controla ramas y ciclos.
 * Actualiza ctx->block; no devuelve un valor de expresión. */
void ir_stmt(IrContext *ctx, const AstNode *node);

/* ir.c: crea un bloque abierto de la rutina activa sin seleccionarlo.
 * Devuelve ID o IR_NO_ID con error registrado. Seleccionar con ctx->block=id. */
IrId ir_new_block(IrContext *ctx);

/* ir.c: única entrada de emisión, también para terminadores. Calcula efectos;
 * cerrar bloque deja ctx->block=IR_NO_ID. Consume argumentos propios de la
 * instrucción incluso al fallar: el llamador no debe liberarlos de nuevo. */
void ir_emit(IrContext *ctx, IrInstruction instruction);

/* ir.c: conserva el primer fallo y su origen. No libera el programa parcial;
 * esa limpieza pertenece a ir_generate. No imprime ni consulta stdout/stderr. */
void ir_fail(IrContext *ctx, IrStatus status, SourceLocation location,
             const char *message);

#endif
