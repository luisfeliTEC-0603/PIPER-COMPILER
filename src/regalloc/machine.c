/** @file machine.c
 * Bajada de las familias IR actuales a instrucciones virtuales de maquina.
 *
 * Las operaciones binarias de PORYGON modifican su primer registro. Por eso
 * primero copiamos/materializamos el operando izquierdo en el resultado y
 * despues emitimos la operacion que lee y redefine ese resultado. ADD/SUB
 * usan el inmediato de 16 bits con signo cuando el operando derecho cabe.
 * Los operandos constantes y simbolos que necesitan registro generan IDs
 * nuevos a partir de ir_temp_count. Un IMM grande se construye solo en su
 * destino y r0; los accesos locales leen r7 y el store global usa r5 reservado.
 * Las llamadas escalares preparan todos los argumentos antes de escribir
 * registros fisicos; sus sitios de retorno reciben IDs globales. El control
 * conserva los IDs de bloque.
 *
 * Esta fase no emite ASM. Rechaza opcodes y efectos cuya expansion fisica aun
 * no esta modelada; publicar un resultado incompleto seria peligroso.
 */
#include "regalloc_machine.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    RegallocMachineRoutine *routine;
    RegallocMachineBlock *block;
    size_t capacity;
    size_t origin;
    size_t routine_index;
    IrId block_id;
    const SymbolTable *symbols;
    size_t *call_site_count;
    int call_reserve_size;
    RegallocDiagnostic *diagnostic;
} LowerContext;

static RegallocStatus machine_report(RegallocDiagnostic *diagnostic,
                                     RegallocStatus status, size_t routine,
                                     IrId block, const char *message)
{
    if (diagnostic != NULL) {
        *diagnostic = (RegallocDiagnostic){
            .status = status, .routine = routine, .block = block
        };
        snprintf(diagnostic->message, sizeof(diagnostic->message), "%s", message);
    }
    return status;
}

void regalloc_machine_program_free(RegallocMachineProgram *program)
{
    if (program == NULL) return;
    for (size_t r = 0; r < program->routine_count; r++) {
        RegallocMachineRoutine *routine = &program->routines[r];
        for (size_t b = 0; b < routine->block_count; b++)
            free(routine->blocks[b].instructions);
        free(routine->blocks);
    }
    free(program->routines);
    free(program);
}

/** La capacidad base es cuatro veces las instrucciones IR del bloque: COPY
 * ocupa como maximo 2, NE 4, ADD de direccion 3, BRANCH 2 y RETURN 3.
 * Por cada CALL/INTRINSIC se suman
 * dos posiciones por argumento para materializacion y preparacion. Se comprueba
 * antes de reservar; no se deja una instruccion a medias si se agota. */
static RegallocStatus emit(LowerContext *ctx, RegallocMachineInstruction in)
{
    if (ctx->block->instruction_count >= ctx->capacity)
        return machine_report(ctx->diagnostic, REGALLOC_INVALID_IR,
                              ctx->routine_index, ctx->block_id,
                              "La bajada escalar excedio su capacidad prevista.");
    in.origin_instruction = ctx->origin;
    ctx->block->instructions[ctx->block->instruction_count++] = in;
    return REGALLOC_OK;
}

static RegallocStatus new_temp(LowerContext *ctx, IrId *out)
{
    if (ctx->routine->temp_count == SIZE_MAX)
        return machine_report(ctx->diagnostic, REGALLOC_OUT_OF_MEMORY,
                              ctx->routine_index, ctx->block_id,
                              "Se agotaron los IDs de temporales virtuales.");
    *out = ctx->routine->temp_count++;
    return REGALLOC_OK;
}

static bool scalar(IrOperand value)
{
    if (value.kind == IR_OPERAND_NONE || value.type.address ||
        value.type.piper == PIPER_TYPE_NONE) return false;
    return value.kind != IR_OPERAND_SYMBOL ||
           (value.value.symbol != NULL && !value.value.symbol->is_array);
}

/* La ISA ordena unsigned; el tipo resuelto de cada operando determina si la
 * expansion debe transformar su signo o comparar un signed con un unsigned. */
static bool unsigned_type(PiperType type)
{
    return type == PIPER_TYPE_UB8 || type == PIPER_TYPE_UB16 ||
           type == PIPER_TYPE_UB32;
}

static bool integer_type(PiperType type)
{
    return type >= PIPER_TYPE_B8 && type <= PIPER_TYPE_UB32;
}

/* La identidad del simbolo, no su offset, determina si su direccion es global.
 * La tabla validada conserva ese enlace durante toda la bajada. */
static bool global_symbol(const LowerContext *ctx, const Symbol *symbol)
{
    if (ctx->symbols->global_scope == NULL) return false;
    for (const Symbol *it = ctx->symbols->global_scope->symbols_head;
         it != NULL; it = it->next)
        if (it == symbol) return true;
    return false;
}

/* El inmediato contiene el offset local o la direccion fisica global.
 * El emisor usa el propio destino como direccion temporal al cargar globales;
 * al guardar globales usa r5, que no pertenece al pool K=4. */
static void symbol_access(const LowerContext *ctx,
                          RegallocMachineInstruction *in,
                          const Symbol *symbol, bool store)
{
    bool global = global_symbol(ctx, symbol);
    in->symbol = symbol;
    in->immediate = global
        ? SYMBOL_DATA_BASE_ADDRESS + symbol->address_or_offset
        : symbol->address_or_offset;
    in->fixed_reads = global ? REGALLOC_PHYS_R0 : REGALLOC_PHYS_R7;
    if (global && store) {
        in->fixed_reads |= REGALLOC_PHYS_R5;
        in->fixed_writes = REGALLOC_PHYS_R5;
    }
}

/** Produce un registro virtual con el valor solicitado. Si preferred es un
 * temporal, se define alli; si no, se reutiliza un operando TEMP o se crea un
 * auxiliar nuevo. IMM y LOAD_SYMBOL declaran ya sus registros fisicos fijos;
 * la secuencia ASM tipada sigue a cargo del emisor. */
static RegallocStatus materialize(LowerContext *ctx, IrOperand value,
                                 IrId preferred, IrId *out,
                                 SourceLocation location)
{
    if (!scalar(value))
        return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                              ctx->routine_index, ctx->block_id,
                              "R2 solo materializa operandos escalares.");
    if (value.kind == IR_OPERAND_TEMP && preferred == IR_NO_ID) {
        *out = value.value.temp;
        return REGALLOC_OK;
    }
    IrId target = preferred;
    RegallocStatus status = REGALLOC_OK;
    if (target == IR_NO_ID) status = new_temp(ctx, &target);
    if (status != REGALLOC_OK) return status;

    RegallocMachineInstruction in = {
        .def = target, .use1 = IR_NO_ID, .type = value.type,
        .location = location
    };
    switch (value.kind) {
    case IR_OPERAND_TEMP:
        if (target == value.value.temp) { *out = target; return REGALLOC_OK; }
        in.op = REGALLOC_M_MOVE;
        in.use1 = value.value.temp;
        break;
    case IR_OPERAND_CONSTANT:
        in.op = REGALLOC_M_IMM;
        in.immediate = value.value.constant;
        in.fixed_reads = REGALLOC_PHYS_R0;
        break;
    case IR_OPERAND_SYMBOL:
        in.op = REGALLOC_M_LOAD_SYMBOL;
        in.flags = IR_READ_MEMORY;
        symbol_access(ctx, &in, value.value.symbol, false);
        break;
    default:
        return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                              ctx->routine_index, ctx->block_id,
                              "Operando escalar no reconocido.");
    }
    status = emit(ctx, in);
    if (status == REGALLOC_OK) *out = target;
    return status;
}

static RegallocStatus lower_copy(LowerContext *ctx, const IrInstruction *source)
{
    bool checked = (source->flags & IR_CHECKED) != 0;
    unsigned allowed_flags = IR_READ_MEMORY | IR_WRITE_MEMORY |
        (checked ? IR_CHECKED | IR_MAY_TRAP : 0);
    if ((source->flags & ~allowed_flags) != 0 ||
        (source->result.kind != IR_OPERAND_TEMP &&
         source->result.kind != IR_OPERAND_SYMBOL) ||
        !scalar(source->result))
        return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                              ctx->routine_index, ctx->block_id,
                              "COPY con efecto especial o destino no escalar.");
    if (checked) {
        if (!integer_type(source->arg1.type.piper) ||
            !integer_type(source->result.type.piper))
            return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                                  ctx->routine_index, ctx->block_id,
                                  "COPY comprobada requiere enteros escalares.");
        IrId input, target;
        RegallocStatus status = materialize(ctx, source->arg1, IR_NO_ID,
                                            &input, source->location);
        if (status != REGALLOC_OK) return status;
        if (source->result.kind == IR_OPERAND_TEMP)
            target = source->result.value.temp;
        else {
            status = new_temp(ctx, &target);
            if (status != REGALLOC_OK) return status;
        }
        /* El chequeo precede al STORE_SYMBOL: un fallo no modifica la
         * variable destino. r5/r6 estan reservados fuera de K=4. */
        status = emit(ctx, (RegallocMachineInstruction){
            .op = REGALLOC_M_CHECKED_COPY, .def = target, .use1 = input,
            .type = source->result.type, .source_type = source->arg1.type,
            .fixed_reads = REGALLOC_PHYS_R0 | REGALLOC_PHYS_R5 |
                           REGALLOC_PHYS_R6,
            .fixed_writes = REGALLOC_PHYS_R5 | REGALLOC_PHYS_R6,
            .flags = IR_CHECKED | IR_MAY_TRAP,
            .location = source->location
        });
        if (status != REGALLOC_OK || source->result.kind == IR_OPERAND_TEMP)
            return status;
        RegallocMachineInstruction store = {
            .op = REGALLOC_M_STORE_SYMBOL, .def = IR_NO_ID, .use1 = target,
            .type = source->result.type, .flags = IR_WRITE_MEMORY,
            .location = source->location
        };
        symbol_access(ctx, &store, source->result.value.symbol, true);
        return emit(ctx, store);
    }
    IrId target = source->result.kind == IR_OPERAND_TEMP
        ? source->result.value.temp : IR_NO_ID;
    IrId value;
    RegallocStatus status = materialize(ctx, source->arg1, target, &value,
                                        source->location);
    if (status != REGALLOC_OK || source->result.kind == IR_OPERAND_TEMP)
        return status;
    if (source->result.kind != IR_OPERAND_SYMBOL)
        return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                              ctx->routine_index, ctx->block_id,
                              "COPY solo escribe temporales o simbolos.");
    RegallocMachineInstruction in = {
        .op = REGALLOC_M_STORE_SYMBOL, .def = IR_NO_ID, .use1 = value,
        .type = source->result.type,
        .flags = IR_WRITE_MEMORY, .location = source->location
    };
    symbol_access(ctx, &in, source->result.value.symbol, true);
    return emit(ctx, in);
}

static RegallocStatus lower_neg(LowerContext *ctx,
                                const IrInstruction *source)
{
    if ((source->flags & ~(IR_READ_MEMORY | IR_CHECKED | IR_MAY_TRAP)) != 0 ||
        (source->flags & IR_CHECKED) == 0 ||
        source->result.kind != IR_OPERAND_TEMP ||
        source->arg2.kind != IR_OPERAND_NONE ||
        !scalar(source->arg1) || !scalar(source->result) ||
        !integer_type(source->arg1.type.piper) ||
        !integer_type(source->result.type.piper))
        return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                              ctx->routine_index, ctx->block_id,
                              "NEG requiere entero escalar y chequeo de rango.");
    IrId input;
    RegallocStatus status = materialize(ctx, source->arg1, IR_NO_ID,
                                        &input, source->location);
    if (status != REGALLOC_OK) return status;
    return emit(ctx, (RegallocMachineInstruction){
        .op = REGALLOC_M_CHECKED_NEG,
        .def = source->result.value.temp, .use1 = input,
        .type = source->result.type, .source_type = source->arg1.type,
        .fixed_reads = REGALLOC_PHYS_R0 | REGALLOC_PHYS_R5 |
                       REGALLOC_PHYS_R6,
        .fixed_writes = REGALLOC_PHYS_R5 | REGALLOC_PHYS_R6,
        .flags = IR_CHECKED | IR_MAY_TRAP,
        .location = source->location
    });
}

static RegallocStatus lower_binary(LowerContext *ctx, const IrInstruction *source,
                                   RegallocMachineOp op)
{
    bool address_add = op == REGALLOC_M_ADD && source->result.type.address;
    bool general = op == REGALLOC_M_DIV || op == REGALLOC_M_MOD ||
                   op == REGALLOC_M_POW;
    bool checked = op == REGALLOC_M_CHECKED_ADD ||
                   op == REGALLOC_M_CHECKED_SUB ||
                   op == REGALLOC_M_CHECKED_MUL || general;
    unsigned allowed_flags = checked
        ? IR_READ_MEMORY | IR_CHECKED | IR_MAY_TRAP : IR_READ_MEMORY;
    if ((source->flags & ~allowed_flags) != 0 ||
        (checked && (source->flags & IR_CHECKED) == 0) ||
        source->result.kind != IR_OPERAND_TEMP ||
        !(address_add
            ? source->arg1.kind == IR_OPERAND_TEMP &&
              source->arg1.type.address &&
              source->arg1.type.piper == source->result.type.piper
            : scalar(source->result) && scalar(source->arg1)) ||
        !scalar(source->arg2))
        return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                              ctx->routine_index, ctx->block_id,
                              "Binaria con flags o tipo no soportado en R2.");
    if (general && ctx->call_reserve_size < SYMBOL_CALL_RECORD_SIZE)
        return machine_report(ctx->diagnostic, REGALLOC_INVALID_INPUT,
                              ctx->routine_index, ctx->block_id,
                              "DIV/MOD/POW requieren 32 bytes de trabajo bajo r7.");
    IrId target = source->result.value.temp;
    /* La IR generada usa un resultado fresco. Rechazar una reasignacion aqui
     * evita destruir un operando antes de consumirlo en la forma de 2 registros. */
    if ((source->arg1.kind == IR_OPERAND_TEMP &&
         source->arg1.value.temp == target) ||
        (source->arg2.kind == IR_OPERAND_TEMP &&
         source->arg2.value.temp == target))
        return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                              ctx->routine_index, ctx->block_id,
                              "La binaria requiere un temporal de resultado fresco.");
    /* sumi/diffi extienden el campo inmediato con signo. La forma de registro
     * conserva los literales fuera de ese rango y las otras operaciones. */
    bool immediate = (op == REGALLOC_M_ADD || op == REGALLOC_M_SUB) &&
        source->arg2.kind == IR_OPERAND_CONSTANT &&
        source->arg2.value.constant >= -32768 &&
        source->arg2.value.constant <= 32767;
    IrId left, right = IR_NO_ID;
    RegallocStatus status = REGALLOC_OK;
    if (!immediate)
        status = materialize(ctx, source->arg2, IR_NO_ID, &right,
                             source->location);
    if (status != REGALLOC_OK) return status;
    if (address_add) {
        left = target;
        status = emit(ctx, (RegallocMachineInstruction){
            .op = REGALLOC_M_MOVE, .def = target,
            .use1 = source->arg1.value.temp, .type = source->result.type,
            .location = source->location
        });
    } else
        status = materialize(ctx, source->arg1, target, &left,
                             source->location);
    if (status != REGALLOC_OK) return status;
    bool ordered = op == REGALLOC_M_LT || op == REGALLOC_M_GT;
    bool left_unsigned = unsigned_type(source->arg1.type.piper);
    bool right_unsigned = unsigned_type(source->arg2.type.piper);
    /* ltn/gtn ordenan unsigned. El emisor necesitara r5 para el bit de signo
     * o la mascara 0x80000000; r5 ya esta fuera del pool de colores. */
    /* CHECKED conserva la trampa y usa solo r5/r6: los colores r1..r4
     * representan exclusivamente los operandos/resultado explicitos. */
    bool needs_scratch = checked ||
                         (ordered && !(left_unsigned && right_unsigned));
    return emit(ctx, (RegallocMachineInstruction){
        .op = immediate ? (op == REGALLOC_M_ADD ? REGALLOC_M_ADDI : REGALLOC_M_SUBI) : op,
        .def = left, .use1 = right,
        .immediate = immediate ? source->arg2.value.constant : 0,
        .type = source->result.type,
        .left_unsigned = (ordered || general) && left_unsigned,
        .right_unsigned = (ordered || general) && right_unsigned,
        .fixed_reads = general ? REGALLOC_PHYS_R0 | REGALLOC_PHYS_R7 |
                                 REGALLOC_CANDIDATE_MASK
            : checked ? REGALLOC_PHYS_R5 | REGALLOC_PHYS_R6
            : needs_scratch ? REGALLOC_PHYS_R0 | REGALLOC_PHYS_R5 : 0,
        .fixed_writes = checked ? REGALLOC_PHYS_R5 | REGALLOC_PHYS_R6
            : needs_scratch ? REGALLOC_PHYS_R5 : 0,
        .flags = general ? IR_CHECKED | IR_MAY_TRAP |
                           IR_READ_MEMORY | IR_WRITE_MEMORY
            : checked ? IR_CHECKED | IR_MAY_TRAP : 0,
        .location = source->location
    });
}

/* eor compara los bits sin distinguir signo. NE invierte su bool 0/1. */
static RegallocStatus lower_not_equal(LowerContext *ctx,
                                      const IrInstruction *source)
{
    RegallocStatus status = lower_binary(ctx, source, REGALLOC_M_EQ);
    if (status != REGALLOC_OK) return status;
    return emit(ctx, (RegallocMachineInstruction){
        .op = REGALLOC_M_BOOL_NOT, .def = source->result.value.temp,
        .use1 = IR_NO_ID, .type = source->result.type,
        .location = source->location
    });
}

/* <= y >= invierten la comparacion estricta opuesta; BOOL_NOT usa el propio
 * destino y no introduce otro temporal ni registro fisico oculto. */
static RegallocStatus lower_order(LowerContext *ctx,
                                  const IrInstruction *source,
                                  RegallocMachineOp strict, bool invert)
{
    RegallocStatus status = lower_binary(ctx, source, strict);
    if (status != REGALLOC_OK || !invert) return status;
    return emit(ctx, (RegallocMachineInstruction){
        .op = REGALLOC_M_BOOL_NOT, .def = source->result.value.temp,
        .use1 = IR_NO_ID, .type = source->result.type,
        .location = source->location
    });
}

static RegallocStatus lower_not(LowerContext *ctx,
                                const IrInstruction *source)
{
    if ((source->flags & ~IR_READ_MEMORY) != 0 ||
        source->result.kind != IR_OPERAND_TEMP ||
        source->result.type.piper != PIPER_TYPE_BOOL ||
        source->arg1.type.piper != PIPER_TYPE_BOOL ||
        source->arg2.kind != IR_OPERAND_NONE)
        return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                              ctx->routine_index, ctx->block_id,
                              "NOT requiere un bool escalar sin efectos.");
    IrId target = source->result.value.temp;
    IrId value;
    RegallocStatus status = materialize(ctx, source->arg1, target, &value,
                                        source->location);
    if (status != REGALLOC_OK) return status;
    return emit(ctx, (RegallocMachineInstruction){
        .op = REGALLOC_M_BOOL_NOT, .def = value, .use1 = IR_NO_ID,
        .type = source->result.type, .location = source->location
    });
}

/* ADDR conserva la identidad del arreglo para que el emisor distinga una
 * base global/local de un parametro que ya contiene un puntero. En los dos
 * primeros casos construye una direccion; en el tercero carga una palabra. */
static RegallocStatus lower_address(LowerContext *ctx,
                                    const IrInstruction *source)
{
    if (source->arg1.kind != IR_OPERAND_SYMBOL ||
        source->arg1.value.symbol == NULL ||
        !source->arg1.value.symbol->is_array ||
        source->result.kind != IR_OPERAND_TEMP ||
        !source->result.type.address || source->flags != 0)
        return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                              ctx->routine_index, ctx->block_id,
                              "ADDR requiere un arreglo y resultado direccion.");
    const Symbol *symbol = source->arg1.value.symbol;
    RegallocMachineInstruction in = {
        .op = REGALLOC_M_ADDR, .def = source->result.value.temp,
        .use1 = IR_NO_ID, .type = source->result.type,
        .location = source->location
    };
    symbol_access(ctx, &in, symbol, false);
    if (symbol->symbol_class == SYMBOL_PARAM) in.flags = IR_READ_MEMORY;
    return emit(ctx, in);
}

/* La comparacion es unsigned: un indice negativo tambien falla. r5/r6
 * materializan el limite y la comparacion sin ocupar un color adicional. */
static RegallocStatus lower_bounds(LowerContext *ctx,
                                   const IrInstruction *source)
{
    if (source->result.kind != IR_OPERAND_NONE ||
        !scalar(source->arg1) ||
        source->arg2.kind != IR_OPERAND_CONSTANT ||
        source->arg2.value.constant <= 0 ||
        (source->flags & ~IR_MAY_TRAP) != 0)
        return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                              ctx->routine_index, ctx->block_id,
                              "CHECK_BOUNDS requiere indice escalar y limite constante.");
    IrId index;
    RegallocStatus status = materialize(ctx, source->arg1, IR_NO_ID,
                                        &index, source->location);
    if (status != REGALLOC_OK) return status;
    return emit(ctx, (RegallocMachineInstruction){
        .op = REGALLOC_M_CHECK_BOUNDS, .def = IR_NO_ID, .use1 = index,
        .immediate = source->arg2.value.constant,
        .fixed_reads = REGALLOC_PHYS_R0,
        .fixed_writes = REGALLOC_PHYS_R5 | REGALLOC_PHYS_R6,
        .flags = IR_MAY_TRAP, .type = source->arg1.type,
        .location = source->location
    });
}

/* LOAD/STORE indirectos operan sobre una direccion virtual previamente
 * calculada; STORE expone ambos usos para liveness y el grafo. */
static RegallocStatus lower_indirect(LowerContext *ctx,
                                     const IrInstruction *source)
{
    if (source->arg1.kind != IR_OPERAND_TEMP ||
        !source->arg1.type.address ||
        (source->flags & ~(IR_READ_MEMORY | IR_WRITE_MEMORY)) != 0)
        return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                              ctx->routine_index, ctx->block_id,
                              "Acceso indirecto requiere direccion virtual.");
    if (source->op == IR_OP_LOAD) {
        if (source->result.kind != IR_OPERAND_TEMP ||
            !scalar(source->result))
            return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                                  ctx->routine_index, ctx->block_id,
                                  "LOAD requiere resultado escalar.");
        return emit(ctx, (RegallocMachineInstruction){
            .op = REGALLOC_M_LOAD_INDIRECT,
            .def = source->result.value.temp,
            .use1 = source->arg1.value.temp,
            .type = source->result.type, .flags = IR_READ_MEMORY,
            .location = source->location
        });
    }
    if (source->result.kind != IR_OPERAND_NONE ||
        !scalar(source->arg2))
        return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                              ctx->routine_index, ctx->block_id,
                              "STORE requiere valor escalar.");
    IrId value;
    RegallocStatus status = materialize(ctx, source->arg2, IR_NO_ID,
                                        &value, source->location);
    if (status != REGALLOC_OK) return status;
    return emit(ctx, (RegallocMachineInstruction){
        .op = REGALLOC_M_STORE_INDIRECT,
        .def = IR_NO_ID, .use1 = source->arg1.value.temp,
        .use2 = value, .type = source->arg2.type,
        .flags = IR_WRITE_MEMORY, .location = source->location
    });
}

/* La IR ya evaluo los argumentos de izquierda a derecha. Materializarlos
 * antes de CALL_SAVE evita crear definiciones entre TO_ARG y CALL: los
 * registros fisicos con argumentos quedan intactos hasta el salto. CALL_SAVE
 * conserva los colores del llamador; CALL los restaura y publica def al final.
 * Los valores que cruzan un TO_ARG reciben su restriccion en el grafo. */
static RegallocStatus lower_call(LowerContext *ctx,
                                 const IrInstruction *source)
{
    const Symbol *callee = source->extra.call.callee;
    size_t count = source->extra.call.argument_count;
    if (callee == NULL || callee->symbol_class != SYMBOL_FUNC ||
        count > REGALLOC_MAX_CALL_ARGS ||
        (count != 0 && source->extra.call.arguments == NULL) ||
        (source->result.kind != IR_OPERAND_NONE &&
         source->result.kind != IR_OPERAND_TEMP))
        return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                              ctx->routine_index, ctx->block_id,
                              "CALL requiere firma y resultado escalar admitidos.");
    if (*ctx->call_site_count >= 32767)
        return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                              ctx->routine_index, ctx->block_id,
                              "Demasiados puntos de retorno para el despachador.");
    size_t record_bytes = SYMBOL_CALL_RECORD_SIZE +
        (count > REGALLOC_REGISTER_ARGS
             ? (count - REGALLOC_REGISTER_ARGS) * SYMBOL_WORD_SIZE : 0);
    if (ctx->call_reserve_size < 0 ||
        record_bytes > (size_t)ctx->call_reserve_size)
        return machine_report(ctx->diagnostic, REGALLOC_INVALID_INPUT,
                              ctx->routine_index, ctx->block_id,
                              "La reserva de llamada no cubre su registro y argumentos.");

    IrId args[REGALLOC_MAX_CALL_ARGS];
    for (size_t i = 0; i < count; i++) {
        IrOperand arg = source->extra.call.arguments[i];
        if (arg.type.address) {
            if (arg.kind != IR_OPERAND_TEMP)
                return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                                      ctx->routine_index, ctx->block_id,
                                      "El argumento arreglo requiere direccion virtual.");
            args[i] = arg.value.temp;
            continue;
        }
        RegallocStatus status = materialize(ctx, arg, IR_NO_ID,
                                            &args[i], source->location);
        if (status != REGALLOC_OK) return status;
    }

    size_t site = ++*ctx->call_site_count;
    RegallocStatus status = emit(ctx, (RegallocMachineInstruction){
        .op = REGALLOC_M_CALL_SAVE, .def = IR_NO_ID, .use1 = IR_NO_ID,
        .call_site_id = site, .call_arg_count = count,
        .fixed_reads = REGALLOC_CANDIDATE_MASK |
                                             REGALLOC_PHYS_R7,
        .flags = IR_WRITE_MEMORY, .location = source->location
    });
    if (status != REGALLOC_OK) return status;
    for (size_t i = 0; i < count; i++) {
        bool register_arg = i < REGALLOC_REGISTER_ARGS;
        status = emit(ctx, (RegallocMachineInstruction){
            .op = REGALLOC_M_CALL_ARG, .def = IR_NO_ID, .use1 = args[i],
            .call_site_id = site, .call_arg_index = i,
            .call_arg_count = count,
            .fixed_reads = register_arg ? 0 : REGALLOC_PHYS_R7,
            .fixed_writes = register_arg ? REGALLOC_PHYS_R1 << i : 0,
            .flags = register_arg ? 0 : IR_WRITE_MEMORY,
            .type = source->extra.call.arguments[i].type,
            .location = source->location
        });
        if (status != REGALLOC_OK) return status;
    }
    unsigned arg_reads = REGALLOC_PHYS_R7;
    for (size_t i = 0; i < count && i < REGALLOC_REGISTER_ARGS; i++)
        arg_reads |= REGALLOC_PHYS_R1 << i;
    bool has_result = source->result.kind == IR_OPERAND_TEMP;
    return emit(ctx, (RegallocMachineInstruction){
        .op = has_result ? REGALLOC_M_CALL_RESULT : REGALLOC_M_CALL_VOID,
        .def = has_result ? source->result.value.temp : IR_NO_ID,
        .use1 = IR_NO_ID, .symbol = callee,
        .call_site_id = site, .call_arg_count = count,
        .fixed_reads = arg_reads, .fixed_writes = REGALLOC_PHYS_R5 |
                                                   REGALLOC_PHYS_R6,
        .flags = source->flags | IR_READ_MEMORY | IR_WRITE_MEMORY | IR_MAY_TRAP,
        .type = source->result.type, .location = source->location
    });
}

static bool crypto_index(IrOperand value)
{
    return value.kind == IR_OPERAND_CONSTANT && !value.type.address &&
           value.value.constant >= 0 && value.value.constant <= 3;
}

/* Los registros implicitos de cada instruccion crypto se declaran antes del
 * grafo. Un valor vivo a traves de una escritura fija recibe otro color o
 * spill; no se presupone que r1..r3 esten vacios entre sentencias IR. */
static RegallocStatus lower_intrinsic(LowerContext *ctx,
                                      const IrInstruction *source)
{
    size_t count = source->extra.call.argument_count;
    const IrOperand *args = source->extra.call.arguments;
    if (source->result.kind != IR_OPERAND_NONE ||
        (count != 0 && args == NULL))
        return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                              ctx->routine_index, ctx->block_id,
                              "INTRINSIC requiere argumentos y no devuelve temporal.");
    switch (source->extra.call.intrinsic) {
    case IR_INTRINSIC_AUTH: {
        if (count != 4)
            return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                                  ctx->routine_index, ctx->block_id,
                                  "AUTH requiere cuatro valores.");
        IrId words[4];
        for (size_t i = 0; i < 4; i++) {
            RegallocStatus status = materialize(ctx, args[i], IR_NO_ID,
                                                &words[i], source->location);
            if (status != REGALLOC_OK) return status;
        }
        for (size_t i = 0; i < 4; i++) {
            RegallocStatus status = emit(ctx, (RegallocMachineInstruction){
                .op = REGALLOC_M_CRYPTO_WORD, .def = IR_NO_ID,
                .use1 = words[i], .immediate = (int64_t)i,
                .fixed_reads = REGALLOC_PHYS_R1,
                .fixed_writes = REGALLOC_PHYS_R1,
                .flags = IR_WRITE_CRYPTO, .location = source->location
            });
            if (status != REGALLOC_OK) return status;
        }
        return emit(ctx, (RegallocMachineInstruction){
            .op = REGALLOC_M_CRYPTO_AUTH, .def = IR_NO_ID,
            .use1 = IR_NO_ID,
            .flags = IR_READ_CRYPTO | IR_WRITE_CRYPTO | IR_MAY_TRAP,
            .location = source->location
        });
    }
    case IR_INTRINSIC_WRITE_KEY: {
        if (count != 3 || !crypto_index(args[1]) ||
            !crypto_index(args[2]))
            return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                                  ctx->routine_index, ctx->block_id,
                                  "WRITE_KEY requiere valor y dos indices de 0 a 3.");
        IrId value;
        RegallocStatus status = materialize(ctx, args[0], IR_NO_ID,
                                            &value, source->location);
        if (status != REGALLOC_OK) return status;
        return emit(ctx, (RegallocMachineInstruction){
            .op = REGALLOC_M_CRYPTO_WRITE, .def = IR_NO_ID,
            .use1 = value, .immediate = args[1].value.constant,
            .immediate2 = args[2].value.constant,
            .fixed_reads = REGALLOC_PHYS_R1,
            .fixed_writes = REGALLOC_PHYS_R1,
            .flags = IR_READ_CRYPTO | IR_WRITE_CRYPTO | IR_MAY_TRAP,
            .location = source->location
        });
    }
    case IR_INTRINSIC_FEISTEL:
        if (count != 3 || args[0].kind != IR_OPERAND_TEMP ||
            !args[0].type.address ||
            args[0].type.piper != PIPER_TYPE_B32 ||
            !crypto_index(args[1]) || !crypto_index(args[2]))
            return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                                  ctx->routine_index, ctx->block_id,
                                  "FEISTEL requiere direccion B32[2] y dos indices.");
        return emit(ctx, (RegallocMachineInstruction){
            .op = REGALLOC_M_CRYPTO_FEISTEL, .def = IR_NO_ID,
            .use1 = args[0].value.temp,
            .immediate = args[1].value.constant,
            .immediate2 = args[2].value.constant,
            .fixed_reads = REGALLOC_PHYS_R1 | REGALLOC_PHYS_R2 |
                           REGALLOC_PHYS_R3,
            .fixed_writes = REGALLOC_PHYS_R1 | REGALLOC_PHYS_R2 |
                            REGALLOC_PHYS_R3,
            .flags = IR_READ_MEMORY | IR_WRITE_MEMORY |
                     IR_READ_CRYPTO | IR_WRITE_CRYPTO | IR_MAY_TRAP,
            .type = args[0].type, .location = source->location
        });
    case IR_INTRINSIC_REVOKE:
        if (count != 0)
            return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                                  ctx->routine_index, ctx->block_id,
                                  "REVOKE no acepta argumentos.");
        return emit(ctx, (RegallocMachineInstruction){
            .op = REGALLOC_M_CRYPTO_REVOKE, .def = IR_NO_ID,
            .use1 = IR_NO_ID, .flags = IR_WRITE_CRYPTO,
            .location = source->location
        });
    }
    return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                          ctx->routine_index, ctx->block_id,
                          "Intrinseca desconocida.");
}

static RegallocStatus lower_control(LowerContext *ctx,
                                    const IrInstruction *source)
{
    if ((source->flags & ~IR_READ_MEMORY) != 0)
        return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                              ctx->routine_index, ctx->block_id,
                              "Control con efecto especial no soportado en R2.");
    if (source->op == IR_OP_JUMP)
        return emit(ctx, (RegallocMachineInstruction){
            .op = REGALLOC_M_JUMP, .def = IR_NO_ID, .use1 = IR_NO_ID,
            .targets = {source->extra.targets[0], IR_NO_ID},
            .location = source->location
        });
    if (source->op == IR_OP_BRANCH) {
        IrId condition;
        RegallocStatus status = materialize(ctx, source->arg1, IR_NO_ID,
                                            &condition, source->location);
        if (status != REGALLOC_OK) return status;
        return emit(ctx, (RegallocMachineInstruction){
            .op = REGALLOC_M_BRANCH, .def = IR_NO_ID, .use1 = condition,
            .targets = {source->extra.targets[0], source->extra.targets[1]},
            .type = source->arg1.type, .location = source->location
        });
    }
    unsigned reads = 0;
    if (source->arg1.kind != IR_OPERAND_NONE) {
        IrId value;
        RegallocStatus status = materialize(ctx, source->arg1, IR_NO_ID,
                                            &value, source->location);
        if (status != REGALLOC_OK) return status;
        status = emit(ctx, (RegallocMachineInstruction){
            .op = REGALLOC_M_TO_RETURN, .def = IR_NO_ID, .use1 = value,
            .fixed_writes = REGALLOC_PHYS_R1, .type = source->arg1.type,
            .location = source->location
        });
        if (status != REGALLOC_OK) return status;
        reads = REGALLOC_PHYS_R1;
    }
    return emit(ctx, (RegallocMachineInstruction){
        .op = REGALLOC_M_RETURN, .def = IR_NO_ID, .use1 = IR_NO_ID,
        .fixed_reads = reads, .location = source->location
    });
}

static RegallocStatus lower_instruction(LowerContext *ctx,
                                        const IrInstruction *source)
{
    switch (source->op) {
    case IR_OP_COPY: return lower_copy(ctx, source);
    case IR_OP_ADD: return lower_binary(ctx, source,
        (source->flags & IR_CHECKED) ? REGALLOC_M_CHECKED_ADD : REGALLOC_M_ADD);
    case IR_OP_SUB: return lower_binary(ctx, source,
        (source->flags & IR_CHECKED) ? REGALLOC_M_CHECKED_SUB : REGALLOC_M_SUB);
    case IR_OP_MUL: return lower_binary(ctx, source,
        (source->flags & IR_CHECKED) ? REGALLOC_M_CHECKED_MUL : REGALLOC_M_MUL);
    case IR_OP_DIV: return lower_binary(ctx, source, REGALLOC_M_DIV);
    case IR_OP_MOD: return lower_binary(ctx, source, REGALLOC_M_MOD);
    case IR_OP_POW: return lower_binary(ctx, source, REGALLOC_M_POW);
    case IR_OP_AND: return lower_binary(ctx, source, REGALLOC_M_AND);
    case IR_OP_OR:  return lower_binary(ctx, source, REGALLOC_M_OR);
    case IR_OP_XOR: return lower_binary(ctx, source, REGALLOC_M_XOR);
    case IR_OP_EQ:  return lower_binary(ctx, source, REGALLOC_M_EQ);
    case IR_OP_NE:  return lower_not_equal(ctx, source);
    case IR_OP_LT:  return lower_order(ctx, source, REGALLOC_M_LT, false);
    case IR_OP_LE:  return lower_order(ctx, source, REGALLOC_M_GT, true);
    case IR_OP_GT:  return lower_order(ctx, source, REGALLOC_M_GT, false);
    case IR_OP_GE:  return lower_order(ctx, source, REGALLOC_M_LT, true);
    case IR_OP_NOT: return lower_not(ctx, source);
    case IR_OP_NEG: return lower_neg(ctx, source);
    case IR_OP_ADDR: return lower_address(ctx, source);
    case IR_OP_CHECK_BOUNDS: return lower_bounds(ctx, source);
    case IR_OP_LOAD: case IR_OP_STORE:
        return lower_indirect(ctx, source);
    case IR_OP_CALL: return lower_call(ctx, source);
    case IR_OP_INTRINSIC: return lower_intrinsic(ctx, source);
    case IR_OP_JUMP: case IR_OP_BRANCH: case IR_OP_RETURN:
        return lower_control(ctx, source);
    default:
        return machine_report(ctx->diagnostic, REGALLOC_UNSUPPORTED,
                              ctx->routine_index, ctx->block_id,
                              "Opcode IR aun sin bajada escalar en R2.");
    }
}

RegallocStatus regalloc_lower_scalar(const RegallocAnalysis *analysis,
                                    RegallocMachineProgram **out,
                                    RegallocDiagnostic *diagnostic)
{
    if (analysis == NULL || analysis->ir == NULL || analysis->cfg == NULL ||
        analysis->routines == NULL || out == NULL || *out != NULL)
        return machine_report(diagnostic, REGALLOC_INVALID_INPUT, SIZE_MAX,
                              IR_NO_ID, "Se requiere un analisis preparado y salida NULL.");
    const IrProgram *source = analysis->ir;
    if (analysis->routine_count > SIZE_MAX / sizeof(RegallocMachineRoutine))
        return machine_report(diagnostic, REGALLOC_OUT_OF_MEMORY, SIZE_MAX,
                              IR_NO_ID, "Demasiadas rutinas de maquina.");
    RegallocMachineProgram *program = calloc(1, sizeof(*program));
    if (program == NULL)
        return machine_report(diagnostic, REGALLOC_OUT_OF_MEMORY, SIZE_MAX,
                              IR_NO_ID, "No se pudo reservar el programa de maquina.");
    program->source = source;
    program->routines = calloc(analysis->routine_count,
                               sizeof(*program->routines));
    if (program->routines == NULL) {
        regalloc_machine_program_free(program);
        return machine_report(diagnostic, REGALLOC_OUT_OF_MEMORY, SIZE_MAX,
                              IR_NO_ID, "No se pudieron reservar las rutinas de maquina.");
    }
    program->routine_count = analysis->routine_count;

    for (size_t r = 0; r < program->routine_count; r++) {
        const IrRoutine *ir_routine = &source->routines[r];
        RegallocMachineRoutine *target = &program->routines[r];
        target->ir_routine = r;
        target->ir_temp_count = target->temp_count = ir_routine->temp_count;
        target->base_frame_bytes = ir_routine->symbol != NULL
            ? (size_t)ir_routine->symbol->total_stack_frame_size : 0;
        if (ir_routine->block_count > SIZE_MAX / sizeof(*target->blocks)) {
            regalloc_machine_program_free(program);
            return machine_report(diagnostic, REGALLOC_OUT_OF_MEMORY, r,
                                  IR_NO_ID, "Demasiados bloques de maquina.");
        }
        target->blocks = calloc(ir_routine->block_count,
                                sizeof(*target->blocks));
        if (target->blocks == NULL) {
            regalloc_machine_program_free(program);
            return machine_report(diagnostic, REGALLOC_OUT_OF_MEMORY, r,
                                  IR_NO_ID, "No se pudieron reservar los bloques de maquina.");
        }
        target->block_count = ir_routine->block_count;
        for (size_t b = 0; b < target->block_count; b++) {
            const IrBlock *input = &ir_routine->blocks[b];
            RegallocMachineBlock *output = &target->blocks[b];
            output->id = analysis->routines[r].blocks[b].id;
            output->cfg_node = analysis->routines[r].blocks[b].cfg_node;
            if (input->instruction_count >
                SIZE_MAX / 4 / sizeof(*output->instructions)) {
                regalloc_machine_program_free(program);
                return machine_report(diagnostic, REGALLOC_OUT_OF_MEMORY, r,
                                      input->id, "Demasiadas instrucciones de maquina.");
            }
            size_t capacity = input->instruction_count * 4;
            for (size_t i = 0; i < input->instruction_count; i++)
                if (input->instructions[i].op == IR_OP_CALL ||
                    input->instructions[i].op == IR_OP_INTRINSIC) {
                    size_t n = input->instructions[i].extra.call.argument_count;
                    if (n > (SIZE_MAX / sizeof(*output->instructions) - capacity) / 2) {
                        regalloc_machine_program_free(program);
                        return machine_report(diagnostic, REGALLOC_OUT_OF_MEMORY,
                                              r, input->id,
                                              "Demasiados argumentos para la bajada de CALL.");
                    }
                    capacity += 2 * n;
                }
            output->instructions = calloc(capacity, sizeof(*output->instructions));
            if (output->instructions == NULL) {
                regalloc_machine_program_free(program);
                return machine_report(diagnostic, REGALLOC_OUT_OF_MEMORY, r,
                                      input->id, "No se pudieron reservar las instrucciones de maquina.");
            }
            LowerContext ctx = {.routine = target, .block = output,
                .capacity = capacity, .routine_index = r, .block_id = input->id,
                .symbols = source->symbols,
                .call_site_count = &program->call_site_count,
                .call_reserve_size = ir_routine->symbol != NULL
                    ? ir_routine->symbol->call_reserve_size
                    : source->symbols->global_call_reserve_size,
                .diagnostic = diagnostic};
            for (size_t i = 0; i < input->instruction_count; i++) {
                ctx.origin = i;
                RegallocStatus status = lower_instruction(&ctx,
                                                          &input->instructions[i]);
                if (status != REGALLOC_OK) {
                    regalloc_machine_program_free(program);
                    return status;
                }
            }
        }
    }
    *out = program;
    return machine_report(diagnostic, REGALLOC_OK, SIZE_MAX, IR_NO_ID, "");
}
