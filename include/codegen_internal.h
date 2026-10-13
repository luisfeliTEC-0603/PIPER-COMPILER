#ifndef PIPER_CODEGEN_INTERNAL_H
#define PIPER_CODEGEN_INTERNAL_H

#include "codegen.h"
#include "ast.h"
#include "symbol_table.h"
#include <stdio.h>

/* ---- Convención de registros ---- */
#define REG_ZERO     0   /* r0: constante 0 */
#define REG_RET      1   /* r1: valor de retorno */
#define REG_ARG0     1   /* r1: primer argumento (coincide con retorno) */
#define REG_ARG1     2   /* r2 */
#define REG_ARG2     3   /* r3 */
#define REG_ARG3     4   /* r4 */
#define REG_TMP0     5   /* r5: temporal */
#define REG_TMP1     6   /* r6: temporal */
#define REG_FP       7   /* r7: frame pointer */

#define MAX_TEMPORALES 2
#define MAX_ARGUMENTOS 4

/* ---- Pila de etiquetas para break ---- */
typedef struct LabelStack {
    int *items;
    size_t count;
    size_t capacity;
} LabelStack;

/* ---- Contexto de generación ---- */
typedef struct {
    /* Entrada */
    const AstNode *root;
    const SymbolTable *table;
    FILE *output;
    CodegenOptions options;

    /* Estado mutable */
    int label_counter;          /* Para generar nombres únicos */
    int error_count;            /* Errores acumulados */
    Symbol *current_function;   /* SYMBOL_FUNC de la rutina actual */
    Scope *current_scope;       /* Ámbito activo durante generación */
    LabelStack *break_stack;    /* Etiquetas de fin de ciclo */
    int frame_offset;           /* Bytes reservados en el frame actual */

    /* Buffer de errores */
    char last_error[256];
} CodegenContext;

/* ---- Funciones internas compartidas ---- */

/* codegen_context.c */
CodegenContext *codegen_context_create(const AstNode *root,
                                       const SymbolTable *table,
                                       FILE *output,
                                       const CodegenOptions *options);
void codegen_context_destroy(CodegenContext *ctx);
void codegen_context_error(CodegenContext *ctx, const char *fmt, ...);
int codegen_has_errors(const CodegenContext *ctx);

/* codegen_emit.c */
void emit_label(CodegenContext *ctx, const char *name);
void emit_instruction(CodegenContext *ctx, const char *fmt, ...);
void emit_comment(CodegenContext *ctx, const char *fmt, ...);
void emit_blank(CodegenContext *ctx);

/* codegen_labels.c */
int new_label(CodegenContext *ctx);
void label_stack_push(CodegenContext *ctx, int label);
int label_stack_pop(CodegenContext *ctx);
int label_stack_top(const CodegenContext *ctx);

/* codegen_regs.c */
int alloc_temp(CodegenContext *ctx);
void free_temp(CodegenContext *ctx, int reg);
int load_immediate(CodegenContext *ctx, int dest_reg, long long value);

/* codegen_expr.c */
int codegen_expression(CodegenContext *ctx, const AstNode *expr, int dest_reg);

/* codegen_stmt.c */
void codegen_statement(CodegenContext *ctx, const AstNode *stmt);
void codegen_statement_list(CodegenContext *ctx, const AstNodeList *list);

/* codegen_routine.c */
void codegen_routine(CodegenContext *ctx, const AstNode *routine);
void codegen_routine_prologue(CodegenContext *ctx, const AstNode *routine);
void codegen_routine_epilogue(CodegenContext *ctx, const AstNode *routine);

/* codegen_array.c */
void codegen_array_declaration(CodegenContext *ctx, const AstNode *decl);
void codegen_array_index_address(CodegenContext *ctx,
                                 const AstNode *index_expr,
                                 int base_reg, int dest_reg);

/* codegen_call.c */
void codegen_call(CodegenContext *ctx, const AstNode *call, int dest_reg);

/* codegen_crypto.c */
int codegen_is_intrinsic(const char *name);
void codegen_intrinsic(CodegenContext *ctx, const AstNode *call, int dest_reg);

#endif
