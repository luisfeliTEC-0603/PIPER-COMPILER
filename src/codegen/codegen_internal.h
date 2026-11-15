#ifndef PIPER_CODEGEN_INTERNAL_H
#define PIPER_CODEGEN_INTERNAL_H

#include "codegen.h"
#include "ast.h"
#include "symbol_table.h"
#include <stdio.h>
#include <stddef.h>

/* Convención de registros de la ISA. */
#define REG_ZERO     0   /* r0: constante 0, no escribible */
#define REG_RET      1   /* r1: valor de retorno */
#define REG_ARG0     1   /* r1: primer argumento (coincide con retorno) */
#define REG_ARG1     2   /* r2: segundo argumento */
#define REG_ARG2     3   /* r3: tercer argumento */
#define REG_ARG3     4   /* r4: cuarto argumento */
#define REG_TMP0     5   /* r5: temporal */
#define REG_TMP1     6   /* r6: temporal / id al despachar retornos */
#define REG_FP       7   /* r7: frame pointer */

#define MAX_TEMPORALES   2

/* Convención de llamadas.
 * - Los primeros MAX_REGISTER_ARGS argumentos viajan en r1..r4.
 * - Los restantes se depositan en el registro de llamada, encima de él,
 *   en orden de aparición (arg4 en +CALL_RECORD_SIZE, arg5 en +CALL_RECORD_SIZE+4, ...).
 * - El llamado los lee desde su r7 con offset frame_size + CALL_RECORD_SIZE + 4·(i-4). */
#define MAX_REGISTER_ARGS 4
#define MAX_TOTAL_ARGS    32

/* Desplazamiento, respecto al r7 del llamado tras ajustar el frame,
 * del i-ésimo argumento que viaja en pila (i >= MAX_REGISTER_ARGS). */
#define CODEGEN_STACK_ARG_OFFSET(frame_size, i) \
    ((frame_size) + CALL_RECORD_SIZE + ((i) - MAX_REGISTER_ARGS) * SYMBOL_WORD_SIZE)

/* Tamaño total del registro de llamada (registro + argumentos en pila). */
#define CODEGEN_CALL_RECORD_SIZE(nargs) \
    (CALL_RECORD_SIZE + \
     ((nargs) > MAX_REGISTER_ARGS ? ((nargs) - MAX_REGISTER_ARGS) * SYMBOL_WORD_SIZE : 0))

#define CALL_RECORD_SIZE SYMBOL_CALL_RECORD_SIZE  /* id, r1..r6 y valor de retorno */
/* Espacios disjuntos: incluso los identificadores que ya contienen estos
 * prefijos reciben el prefijo de rutina. No se reservan nombres en Piper. */
#define CODEGEN_INTERNAL_LABEL(name) "_IR_" name
#define CODEGEN_ROUTINE_LABEL(name) "_R_" name
#define CODEGEN_LABEL_SIZE (SYMBOL_NAME_MAX + 64)
#define CODEGEN_DATA_BASE 0x10000
#define CODEGEN_STACK_TOP (CODEGEN_DATA_BASE + SYMBOL_MEMORY_SIZE)
#define CODEGEN_STACK_BOTTOM (CODEGEN_STACK_TOP - SYMBOL_STACK_SIZE)

/* Pila de destinos para sentencias break anidadas. */
typedef struct LabelStack {
    int *items;
    size_t count;
    size_t capacity;
} LabelStack;

/* Estado compartido durante la generación de un programa. */
typedef struct {
    /* Entrada inmutable. */
    const AstNode *root;
    const SymbolTable *table;
    FILE *output;
    CodegenOptions options;

    /* Ubicación y control de flujo de la emisión actual. */
    int label_counter;
    int error_count;            /* Errores acumulados. */
    Symbol *current_function;
    Scope *current_scope;
    LabelStack *break_stack;
    int frame_offset;           /* Tamaño del frame actual. */

    /* Bits 0 y 1 representan r5 y r6. */
    unsigned int temp_in_use;
    int expression_depth;       /* Indica si hay expresiones externas vivas. */

    /* Estado de llamadas y espacio temporal bajo el frame actual. */
    int call_site_count;        /* Continuaciones emitidas. */
    int pending_call_bytes;     /* Reserva temporal bajo el frame. */
    int call_reserve_size;      /* Límite calculado por el semántico. */

    /* Último diagnóstico producido. */
    char last_error[256];
} CodegenContext;

/* Contexto y acceso a símbolos. */
CodegenContext *codegen_context_create(const AstNode *root,
                                       const SymbolTable *table,
                                       FILE *output,
                                       const CodegenOptions *options);
void codegen_context_destroy(CodegenContext *ctx);
void codegen_context_error(CodegenContext *ctx, const char *fmt, ...);
int codegen_has_errors(const CodegenContext *ctx);
Symbol *lookup_symbol_or_error(CodegenContext *ctx,
                               const char *name,
                               int expected_class);
Scope *find_owner_scope(CodegenContext *ctx, Symbol *sym);
int load_symbol_value(CodegenContext *ctx, Symbol *sym, int dest_reg);
void store_symbol_value(CodegenContext *ctx, Symbol *sym, int src_reg);
void emit_typed_load(CodegenContext *ctx, PiperType type, int dest_reg,
                     int offset, int base_reg);
void emit_typed_store(CodegenContext *ctx, PiperType type, int src_reg,
                      int offset, int base_reg);

/* Emisión de ASM. */
void emit_label(CodegenContext *ctx, const char *name);
void emit_instruction(CodegenContext *ctx, const char *fmt, ...);
void emit_comment(CodegenContext *ctx, const char *fmt, ...);
void emit_blank(CodegenContext *ctx);
/* ALU unsigned: ajusta el orden según los tipos. Modifica left y scratch;
 * conserva right. Los tres registros deben ser distintos. */
void emit_typed_order(CodegenContext *ctx, int left, int right, int scratch,
                      int left_unsigned, int right_unsigned, int greater);

/* Etiquetas y destinos de break. */
int new_label(CodegenContext *ctx);
void label_stack_push(CodegenContext *ctx, int label);
int label_stack_pop(CodegenContext *ctx);
int label_stack_top(const CodegenContext *ctx);
int label_stack_is_empty(const CodegenContext *ctx);

/* Registros temporales e inmediatos. */
int alloc_temp(CodegenContext *ctx);
void free_temp(CodegenContext *ctx, int reg);
void reset_temps(CodegenContext *ctx);
int load_immediate(CodegenContext *ctx, int dest_reg, long long value);

/* Expresiones. */
int codegen_expression(CodegenContext *ctx, const AstNode *expr, int dest_reg);
int codegen_checked_binary(CodegenContext *ctx, const AstNode *expr,
                           int left, int right);
int codegen_checked_step(CodegenContext *ctx, PiperType type, int dest,
                         int increment);
int codegen_general_arithmetic(CodegenContext *ctx, const AstNode *expr,
                               int dest_reg);
int codegen_expression_is_unsigned(const AstNode *expr);

/* Sentencias. */
void codegen_statement(CodegenContext *ctx, const AstNode *stmt);
void codegen_statement_list(CodegenContext *ctx, const AstNodeList *list);

/* Rutinas. */
void codegen_routine(CodegenContext *ctx, const AstNode *routine);
void codegen_routine_prologue(CodegenContext *ctx, const AstNode *routine);
void codegen_routine_epilogue(CodegenContext *ctx, const AstNode *routine);

/* Arreglos. */
void codegen_array_declaration(CodegenContext *ctx, const AstNode *decl);
int load_array_base(CodegenContext *ctx, Symbol *sym, int dest_reg);
int codegen_array_index_address(CodegenContext *ctx,
                                const AstNode *index_expr, int dest_reg);

/* Llamadas. */
int codegen_call(CodegenContext *ctx, const AstNode *call, int dest_reg);
void codegen_emit_return_dispatch(CodegenContext *ctx);

/* Intrínsecas criptográficas. */
int codegen_is_intrinsic(const char *name);
void codegen_intrinsic(CodegenContext *ctx, const AstNode *call, int dest_reg);

#endif
