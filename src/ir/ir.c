/** @file ir.c
 * @brief I1: memoria del modelo, IDs de bloques y almacenamiento de instrucciones.
 * No traduce AST, calcula efectos ni reubica variables de Piper.
 */
#include "ir_internal.h"

#include <stdlib.h>

/* Amplía un arreglo C: si cabían 4 elementos y se necesita otro, reserva 8.
 * No cambia la cantidad de elementos usados ni los datos de Piper. realloc
 * puede mover este arreglo, pero conserva su contenido. Si falla, retorna NULL
 * sin cambiar capacity ni liberar el puntero anterior que conserva el llamador. */
static void *grow_array(void *items, size_t *capacity, size_t item_size)
{
    size_t limit = SIZE_MAX / item_size;
    size_t next;
    void *resized;

    if (*capacity >= limit) return NULL;
    if (*capacity == 0) next = 4;
    else if (*capacity > limit / 2) next = limit;
    else next = *capacity * 2;
    if (next > limit) next = limit;
    resized = realloc(items, next * item_size);
    if (resized != NULL) *capacity = next;
    return resized;
}

/* call f(x,y) posee una lista de operandos [x,y] reservada aparte. Se libera
 * esa lista, no las variables originales, los temporales ni arg1/arg2 de una
 * suma. Solo CALL/INTRINSIC poseen esa memoria; un salto usa extra para IDs. */
static void free_call_arguments(IrInstruction *instruction)
{
    if (instruction->op == IR_OP_CALL || instruction->op == IR_OP_INTRINSIC)
        free(instruction->extra.call.arguments);
}

/** Crea programa y pila vacíos para un contexto nuevo. La tabla y el AST
 * son préstamos; los IDs sin seleccionar no pueden confundirse con cero. */
IrStatus ir_context_init(IrContext *ctx, const AstNode *root,
                         const SymbolTable *symbols)
{
    SourceLocation location;

    if (ctx == NULL) return IR_INVALID_INPUT;
    location = root != NULL ? root->location : (SourceLocation){0};
    *ctx = (IrContext){
        .root = root,
        .current_routine = IR_NO_ID,
        .current_block = IR_NO_ID,
        .error = {.routine = IR_NO_ID, .block = IR_NO_ID, .instruction = IR_NO_ID}
    };
    if (root == NULL || root->kind != AST_PROGRAM || symbols == NULL) {
        ir_fail(ctx, IR_INVALID_INPUT, location, "AST o tabla IR inválidos.");
        return ctx->error.status;
    }
    ctx->program = calloc(1, sizeof(*ctx->program));
    if (ctx->program != NULL) {
        ctx->program->symbols = symbols;
        ctx->program->startup = IR_NO_ID;
        ctx->break_targets = grow_array(NULL, &ctx->break_capacity, sizeof(*ctx->break_targets));
    }
    if (ctx->program == NULL || ctx->break_targets == NULL) {
        ir_fail(ctx, IR_OUT_OF_MEMORY, location, "No se pudo crear el contexto IR.");
        ir_context_dispose(ctx);
    }
    return ctx->error.status;
}

/** También sirve tras una reserva fallida. Separar el programa poniendo
 * ctx->program=NULL antes de llamar aquí transfiere su propiedad al llamador. */
void ir_context_dispose(IrContext *ctx)
{
    if (ctx == NULL) return;
    ir_program_free(ctx->program);
    free(ctx->break_targets);
    ctx->root = NULL;
    ctx->program = NULL;
    ctx->break_targets = NULL;
    ctx->break_count = ctx->break_capacity = 0;
    ctx->current_routine = ctx->current_block = IR_NO_ID;
}

/** Registra almacenamiento, no el cuerpo ni la firma física de una función. */
IrId ir_new_routine(IrContext *ctx, const Symbol *symbol)
{
    IrProgram *program;
    IrId id;
    SourceLocation location;

    if (ctx == NULL || ctx->error.status != IR_OK) return IR_NO_ID;
    location = ctx->root != NULL ? ctx->root->location : (SourceLocation){0};
    program = ctx->program;
    if (program == NULL || (symbol != NULL && symbol->symbol_class != SYMBOL_FUNC)) {
        ir_fail(ctx, IR_INVALID_INPUT, location, "Función IR inválida.");
        return IR_NO_ID;
    }
    if (symbol == NULL && program->startup != IR_NO_ID) {
        ir_fail(ctx, IR_INVALID_PROGRAM, location, "El arranque IR ya existe.");
        return IR_NO_ID;
    }
    if (program->routine_count == program->routine_capacity) {
        IrRoutine *items = grow_array(program->routines, &program->routine_capacity,
                                sizeof(*program->routines));
        if (items == NULL) {
            ir_fail(ctx, IR_OUT_OF_MEMORY, location, "No se pudo reservar la rutina IR.");
            return IR_NO_ID;
        }
        program->routines = items;
    }
    id = program->routine_count;
    program->routines[id] = (IrRoutine){.symbol = symbol, .entry = IR_NO_ID};
    program->routine_count++;
    if (symbol == NULL) program->startup = id;
    return id;
}

/** Reserva un bloque sin seleccionarlo. Max(id)+1 mantiene la identidad incluso
 * si los bloques existentes cambiaron de orden o quedaron huecos entre IDs. */
IrId ir_new_block(IrContext *ctx)
{
    IrRoutine *routine;
    IrId id = 0;
    SourceLocation location;

    if (ctx == NULL || ctx->error.status != IR_OK) return IR_NO_ID;
    location = ctx->root != NULL ? ctx->root->location : (SourceLocation){0};
    if (ctx->program == NULL || ctx->current_routine >= ctx->program->routine_count) {
        ir_fail(ctx, IR_INVALID_INPUT, location, "No hay rutina IR activa.");
        return IR_NO_ID;
    }
    routine = &ctx->program->routines[ctx->current_routine];
    for (size_t i = 0; i < routine->block_count; i++) {
        if (routine->blocks[i].id >= IR_NO_ID - 1) {
            ir_fail(ctx, IR_OUT_OF_MEMORY, location, "Se agotaron los IDs de bloque IR.");
            return IR_NO_ID;
        }
        if (routine->blocks[i].id >= id) id = routine->blocks[i].id + 1;
    }
    if (routine->block_count == routine->block_capacity) {
        IrBlock *items = grow_array(routine->blocks, &routine->block_capacity,
                              sizeof(*routine->blocks));
        if (items == NULL) {
            ir_fail(ctx, IR_OUT_OF_MEMORY, location, "No se pudo reservar el bloque IR.");
            return IR_NO_ID;
        }
        routine->blocks = items;
    }
    routine->blocks[routine->block_count] = (IrBlock){.id = id};
    routine->block_count++;
    return id;
}

/** Único recorrido de emisión: comprobar contexto/bloque, reservar y guardar.
 * En error se libera solo la lista recibida; al registrar, pasa al programa.
 * Los flags se conservan hasta implementar su cálculo en I6. */
void ir_emit(IrContext *ctx, IrInstruction instruction)
{
    IrRoutine *routine;
    IrBlock *block = NULL;

    if (ctx == NULL || ctx->error.status != IR_OK) {
        free_call_arguments(&instruction);
        return;
    }
    if (ctx->program == NULL || ctx->current_routine >= ctx->program->routine_count) {
        SourceLocation location = ctx->root != NULL ? ctx->root->location : (SourceLocation){0};
        ir_fail(ctx, IR_INVALID_INPUT, location, "No hay rutina IR activa.");
        free_call_arguments(&instruction);
        return;
    }
    routine = &ctx->program->routines[ctx->current_routine];
    for (size_t i = 0; i < routine->block_count; i++) {
        if (routine->blocks[i].id == ctx->current_block) {
            block = &routine->blocks[i];
            break;
        }
    }
    if (block == NULL) {
        ir_fail(ctx, IR_INVALID_INPUT, instruction.location, "No hay bloque IR abierto.");
    } else if (block->instruction_count != 0) {
        IrOpcode last = block->instructions[block->instruction_count - 1].op;
        if (last == IR_OP_JUMP || last == IR_OP_BRANCH || last == IR_OP_RETURN)
            ir_fail(ctx, IR_INVALID_PROGRAM, instruction.location, "El bloque IR ya terminó.");
    }
    if (ctx->error.status == IR_OK &&
        (instruction.op < IR_OP_COPY || instruction.op > IR_OP_RETURN ||
         ((instruction.op == IR_OP_CALL || instruction.op == IR_OP_INTRINSIC) &&
          instruction.extra.call.argument_count != 0 && instruction.extra.call.arguments == NULL))) {
        ir_fail(ctx, IR_INVALID_INPUT, instruction.location, "Descriptor de instrucción IR inválido.");
    }
    if (ctx->error.status == IR_OK && block->instruction_count == block->instruction_capacity) {
        IrInstruction *items = grow_array(block->instructions, &block->instruction_capacity,
                                          sizeof(*block->instructions));
        if (items == NULL)
            ir_fail(ctx, IR_OUT_OF_MEMORY, instruction.location, "No se pudo reservar la instrucción IR.");
        else
            block->instructions = items;
    }
    if (ctx->error.status != IR_OK) {
        free_call_arguments(&instruction);
        return;
    }
    block->instructions[block->instruction_count++] = instruction;
    if (instruction.op == IR_OP_JUMP || instruction.op == IR_OP_BRANCH ||
        instruction.op == IR_OP_RETURN)
        ctx->current_block = IR_NO_ID;
}

/* Reserva un ID virtual y emite su definición. No reserva registros ni pila.
 * Consume argumentos de CALL incluso si falla y conserva los atributos
 * recibidos; I6 completará efectos y validación. */
IrOperand ir_emit_value(IrContext *ctx, IrInstruction instruction, IrType type)
{
    if (ctx->error.status == IR_OK &&
        (ctx->program == NULL || ctx->current_routine >= ctx->program->routine_count))
        ir_fail(ctx, IR_INVALID_INPUT, instruction.location, "No hay rutina IR activa.");
    if (ctx->error.status != IR_OK) {
        ir_emit(ctx, instruction); /* Descarta también los argumentos de CALL. */
        return (IrOperand){0};
    }
    IrRoutine *routine = &ctx->program->routines[ctx->current_routine];
    if (routine->temp_count == IR_NO_ID) {
        ir_fail(ctx, IR_OUT_OF_MEMORY, instruction.location, "Se agotaron los temporales IR.");
        ir_emit(ctx, instruction);
        return (IrOperand){0};
    }
    instruction.result = (IrOperand){
        .kind = IR_OPERAND_TEMP, .type = type, .value.temp = routine->temp_count
    };
    ir_emit(ctx, instruction);
    if (ctx->error.status != IR_OK) return (IrOperand){0};
    routine->temp_count++;
    return instruction.result;
}

/** No sustituye errores anteriores. instruction es posición, no un ID temporal. */
void ir_fail(IrContext *ctx, IrStatus status, SourceLocation location,
             const char *message)
{
    if (ctx == NULL || ctx->error.status != IR_OK || status == IR_OK) return;
    ctx->error = (IrDiagnostic){
        .status = status, .location = location,
        .routine = ctx->current_routine, .block = ctx->current_block,
        .instruction = IR_NO_ID
    };
    if (ctx->program != NULL && ctx->current_routine < ctx->program->routine_count) {
        IrRoutine *routine = &ctx->program->routines[ctx->current_routine];
        for (size_t i = 0; i < routine->block_count; i++) {
            if (routine->blocks[i].id == ctx->current_block) {
                ctx->error.instruction = routine->blocks[i].instruction_count;
                break;
            }
        }
    }
    snprintf(ctx->error.message, sizeof(ctx->error.message), "%s", message != NULL ? message : "Error IR.");
}

/** counts solo incluyen objetos registrados: una reserva sin elementos también
 * se libera. Los punteros prestados de operandos, funciones y tabla no se tocan. */
void ir_program_free(IrProgram *program)
{
    if (program == NULL) return;
    for (size_t r = 0; r < program->routine_count; r++) {
        IrRoutine *routine = &program->routines[r];
        for (size_t b = 0; b < routine->block_count; b++) {
            IrBlock *block = &routine->blocks[b];
            for (size_t i = 0; i < block->instruction_count; i++)
                free_call_arguments(&block->instructions[i]);
            free(block->instructions);
        }
        free(routine->blocks);
    }
    free(program->routines);
    free(program);
}
