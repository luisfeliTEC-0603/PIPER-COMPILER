#ifndef PIPER_IR_INTERNAL_H
#define PIPER_IR_INTERNAL_H

/** @file ir_internal.h
 * Estado compartido y siete auxiliares del futuro generador. Solo declaraciones.
 * Un primer error queda en error.status: las funciones siguientes no emiten más.
 * Así el resultado de una expresión no necesita otro parámetro de salida.
 * Mantener IR_OK con operando NONE para none; un fallo cambia error.status.
 */
#include "ir.h"

typedef struct {
    const AstNode *root;       /**< Entrada prestada, solo durante generación. */
    IrProgram *program;        /**< Propiedad provisional del orquestador. */
    IrId current_routine;      /**< Índice de rutina activa, no puntero movible. */
    IrId current_block;        /**< ID activo; IR_NO_ID si el camino terminó. */
    IrId *break_targets;       /**< Pila propia, vacía al comenzar cada rutina. */
    size_t break_count, break_capacity;
    IrDiagnostic error;       /**< IR_OK mientras el recorrido sea válido. */
} IrContext;

/* ir_generate.c: registra firmas y traduce rutinas/arranque desde ctx->root. */
void ir_build_program(IrContext *ctx);

/* ir_expr.c: expression es una expresión aprobada. Devuelve constante/temporal
 * estable (NONE para none o fallo), emitido en current_block cuando corresponda.
 * ctx->error.status distingue un none válido de un error. Los tipos se obtienen
 * del AST/símbolos; no se estrechan operaciones por el tipo de su consumidor. */
IrOperand ir_expr(IrContext *ctx, const AstNode *expression);

/* ir_expr.c: target es un destino aprobado, no una lectura de su contenido.
 * Devuelve símbolo escalar o dirección tipada; índices evaluados/comprobados
 * antes de evaluar el origen de la asignación. No reubica variables. */
IrOperand ir_target(IrContext *ctx, const AstNode *target);

/* ir_stmt.c: statement es una sentencia o bloque léxico del AST.
 * Emite efectos y flujo; actualiza current_block, no devuelve valor de expresión.
 * Una rutina anidada no se trata como una sentencia ejecutable del cuerpo. */
void ir_stmt(IrContext *ctx, const AstNode *statement);

/* ir.c: crea un bloque abierto de la rutina activa sin seleccionarlo.
 * Devuelve ID o IR_NO_ID con error registrado. Seleccionar con current_block=id.
 * Su almacenamiento es del compilador; no asigna direcciones de datos Piper. */
IrId ir_new_block(IrContext *ctx);

/* ir.c: única entrada de emisión, también para terminadores. Calcula efectos;
 * cerrar bloque deja current_block=IR_NO_ID. instruction se pasa por valor, pero
 * su lista dinámica se transfiere incluso al fallar: el llamador no la libera.
 * Si ya hubo error, no emite y descarta solo esa lista recibida. */
void ir_emit(IrContext *ctx, IrInstruction instruction);

/* ir.c: conserva el primer fallo y su origen. No libera el programa parcial;
 * esa limpieza pertenece a ir_generate. No imprime ni consulta stdout/stderr. */
void ir_fail(IrContext *ctx, IrStatus status, SourceLocation location,
             const char *message);

#endif
