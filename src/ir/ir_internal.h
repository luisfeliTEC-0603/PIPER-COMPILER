#ifndef PIPER_IR_INTERNAL_H
#define PIPER_IR_INTERNAL_H

/** @file ir_internal.h
 * Estado compartido del generador. I1 implementa su ciclo de vida y almacenamiento;
 * I3–I5 recorren escalares, rutinas, llamadas, retornos y control. I6 completa
 * efectos y revisa estructura antes de entregar el modelo al llamador.
 * I7 agrega direcciones/accesos de arreglos e intrínsecas, sin otra interfaz.
 * Un primer error queda en error.status: las funciones siguientes no emiten más.
 * Así el resultado de una expresión no necesita otro parámetro de salida.
 * Mantener IR_OK con operando NONE para none; un fallo cambia error.status.
 */
#include "ir.h"

typedef struct {
    const AstNode *root;       /**< Entrada prestada, solo durante generación. */
    IrProgram *program;        /**< Propio hasta transferirlo al llamador. */
    IrId current_routine;      /**< Índice de rutina activa, no puntero movible. */
    IrId current_block;        /**< ID activo; IR_NO_ID si el camino terminó. */
    IrId *break_targets;       /**< Pila propia, vacía al comenzar cada rutina. */
    size_t break_count, break_capacity;
    IrDiagnostic error;       /**< IR_OK mientras el recorrido sea válido. */
} IrContext;

/* ir.c: inicializa un contexto nuevo (sin recursos previos) y su programa vacío.
 * No analiza root ni construye funciones. Ante fallo se puede llamar dispose. */
IrStatus ir_context_init(IrContext *ctx, const AstNode *root,
                         const SymbolTable *symbols);

/* Libera programa y pila propios; acepta NULL y llamadas repetidas.
 * Para publicar un programa, retirar antes ctx->program poniéndolo en NULL.
 * Conserva el diagnóstico para que el orquestador pueda copiarlo al salir. */
void ir_context_dispose(IrContext *ctx);

/* Reserva una rutina vacía sin seleccionarla. symbol es prestado; NULL crea
 * el único arranque sintético. Retorna índice o IR_NO_ID con error guardado. */
IrId ir_new_routine(IrContext *ctx, const Symbol *symbol);

/* ir_generate.c: registra firmas y traduce rutinas/arranque desde ctx->root. */
void ir_build_program(IrContext *ctx);

/* ir_expr.c: expression es una expresión aprobada. Devuelve constante/temporal
 * estable (NONE para none o fallo), emitido en current_block cuando corresponda.
 * ctx->error.status distingue un none válido de un error. Los tipos se obtienen
 * del AST/símbolos; no se estrechan operaciones por el tipo de su consumidor. */
IrOperand ir_expr(IrContext *ctx, const AstNode *expression);

/* ir_expr.c: target es un destino aprobado, no una lectura de su contenido.
 * Devuelve símbolo escalar o dirección de arreglo/elemento; índices comprobados
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

/* ir.c: única entrada de emisión, también para terminadores. Conserva atributos
 * recibidos y completa efectos del opcode/globales. Cerrar un bloque
 * deja current_block=IR_NO_ID. instruction se pasa por valor, pero
 * su lista dinámica se transfiere incluso al fallar: el llamador no la libera.
 * Si ya hubo error, no emite y descarta solo esa lista recibida. */
void ir_emit(IrContext *ctx, IrInstruction instruction);

/* ir.c: crea un temporal del tipo indicado y emite su definición. Compartido
 * por expresiones y control; consume argumentos de CALL también al fallar. */
IrOperand ir_emit_value(IrContext *ctx, IrInstruction instruction, IrType type);

/* ir.c: conserva el primer fallo y su origen. No libera el programa parcial;
 * esa limpieza pertenece a ir_generate. No imprime ni consulta stdout/stderr. */
void ir_fail(IrContext *ctx, IrStatus status, SourceLocation location,
             const char *message);

#endif
