#ifndef PIPER_IR_INTERNAL_H
#define PIPER_IR_INTERNAL_H

/**
 * @file ir_internal.h
 * @brief Estado y entradas/salidas privadas del futuro generador de IR.
 *
 * Solo declaraciones. Ningún método de este encabezado está implementado.
 * Orden previsto: crear programa -> preparar contexto -> registrar declaraciones
 * -> traducir rutinas/arranque -> finalizar bloques -> validar -> publicar.
 * Un fallo detiene el recorrido: el contexto puede contener una construcción
 * parcial que deberá descartarse, nunca presentarse como IR válida.
 */
#include "ir.h"

/** Mapa por declaración final, no por nombre ni offset de memoria. */
typedef struct {
    const Symbol *declaration; /**< Préstamo a la tabla semántica final. */
    IrSymbolId symbol;
} IrSymbolBinding;

/** Permite resolver CALL a un ID sin depender del orden del arreglo de rutinas. */
typedef struct {
    const Symbol *declaration;
    IrRoutineId routine;
} IrRoutineBinding;

typedef enum { IR_LVALUE_SYMBOL, IR_LVALUE_ADDRESS } IrLValueKind;

/**
 * Destino de una escritura. value contiene símbolo escalar o dirección temporal;
 * stored_type indica el tipo del dato escrito, no el tipo de la dirección.
 * Resolverlo no lee el contenido del destino. Los índices se evalúan una vez.
 */
typedef struct {
    IrLValueKind kind;
    IrOperand value;
    IrType stored_type;
} IrLValue;

/**
 * Estado de una generación: los punteros a AST/tabla/ámbitos son préstamos.
 * program pertenece al orquestador hasta que ir_generate lo publique.
 * Este contexto posee únicamente mapas y pila de destinos; no posee program.
 * current_routine/current_block son IDs: no guardar punteros a arreglos movibles.
 * current_block=IR_NO_ID indica que el camino actual ya no continúa.
 */
typedef struct {
    const AstNode *root;
    const SymbolTable *symbols;
    const Scope *current_scope;
    IrProgram *program;
    IrRoutineId current_routine;
    IrBlockId current_block;
    IrSymbolBinding *symbol_bindings;
    size_t symbol_binding_count;
    size_t symbol_binding_capacity;
    IrRoutineBinding *routine_bindings;
    size_t routine_binding_count;
    size_t routine_binding_capacity;
    IrBlockId *break_targets;
    size_t break_target_count;
    size_t break_target_capacity;
    size_t next_symbol_id;
    size_t next_routine_id;
    size_t next_temp_id;        /**< Reiniciado por rutina. */
    size_t next_block_id;       /**< Reiniciado por rutina. */
    size_t next_instruction_id; /**< Incluye terminadores; por rutina. */
    IrDiagnostic diagnostic;   /**< Primer error por valor. */
} IrContext;

/* ir.c: propiedad y registro de estructuras. Las salidas solo son válidas en
 * IR_OK. Todos los parámetros de entrada se prestan salvo indicación contraria. */
IrStatus ir_program_create(const SymbolTable *symbols, IrProgram **out);
IrStatus ir_context_init(IrContext *ctx, const AstNode *root,
                         const SymbolTable *symbols, IrProgram *program);
void ir_context_dispose(IrContext *ctx);
IrStatus ir_add_symbol(IrContext *ctx, const Symbol *declaration,
                       const Scope *owner, IrSymbolId *out);
IrStatus ir_add_routine(IrContext *ctx, const Symbol *declaration,
                        const char *name, bool is_startup, IrRoutineId *out);
IrStatus ir_new_temp(IrContext *ctx, IrType type, SourceLocation location,
                     IrOperand *out);

/* ir_generate.c: etapas del recorrido. Preparar las firmas antes de emitir
 * cuerpos no amplía la visibilidad del lenguaje: se usan los enlaces semánticos. */
IrStatus ir_generate_program(IrContext *ctx, const AstNode *root);
IrStatus ir_generate_routine(IrContext *ctx, const AstNode *routine,
                             IrRoutineId id);

/* ir_expr.c: valor estable + estado separado. expected_type.kind puede ser
 * IR_TYPE_UNSPECIFIED; no permite estrechar cálculos ya tipados. Para none,
 * out_value.kind=IR_OPERAND_NONE con IR_OK, nunca con un temporal ficticio. */
IrStatus ir_generate_expr(IrContext *ctx, const AstNode *node,
                          IrType expected_type, IrOperand *out_value);

/* ir_stmt.c: efectos y flujo. La ausencia de continuación se comunica mediante
 * ctx->current_block=IR_NO_ID, no mediante un error ni una bandera de liveness. */
IrStatus ir_generate_stmt(IrContext *ctx, const AstNode *node);
IrStatus ir_generate_lvalue(IrContext *ctx, const AstNode *node, IrLValue *out);
IrStatus ir_generate_block(IrContext *ctx, const AstNode *block);

/* ir_blocks.c: IDs de bloques y transferencias, sin construir el CFG.
 * emit/terminate adquieren listas de instruction SOLO en éxito. Tras éxito,
 * el llamador no vuelve a liberarlas. En error siguen perteneciendo al llamador.
 * La instrucción recibida cede su almacenamiento variable, no su struct. */
IrStatus ir_new_block(IrContext *ctx, IrBlockId *out);
IrStatus ir_select_block(IrContext *ctx, IrBlockId block);
IrStatus ir_emit_instruction(IrContext *ctx, IrInstruction *instruction,
                             IrInstructionId *out);
IrStatus ir_terminate_block(IrContext *ctx, IrInstruction *terminator,
                            IrInstructionId *out);
IrStatus ir_finalize_blocks(IrContext *ctx);
IrStatus ir_push_break_target(IrContext *ctx, IrBlockId target);
IrStatus ir_pop_break_target(IrContext *ctx, IrBlockId *out);

/* ir_memory.c: índices -> dirección tipada -> acceso. El resultado address
 * conserva la evaluación ya emitida; no debe regenerarse al almacenar. */
IrStatus ir_generate_index_address(IrContext *ctx, const AstNode *index,
                                   IrOperand *out_address);
IrStatus ir_generate_array_assignment(IrContext *ctx, const AstNode *target,
                                      const AstNode *value);

/* ir_call.c: argumentos evaluados en origen, resultado virtual opcional.
 * Firmas y enlaces vienen del semántico; registros de llamada quedan al backend. */
IrStatus ir_generate_call(IrContext *ctx, const AstNode *call,
                          IrOperand *out_value);
IrStatus ir_generate_intrinsic(IrContext *ctx, const AstNode *call,
                               IrOperand *out_value);

#endif
