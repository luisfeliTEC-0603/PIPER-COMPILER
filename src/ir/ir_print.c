/** @file ir_print.c
 * I8: vista de las mismas instrucciones, tipos y efectos del modelo en memoria.
 * El llamador abre/cierra el stream y publica el archivo al terminar con éxito.
 */
#include "ir.h"
#include <ctype.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static void write_type(FILE *out, IrType type)
{
    fprintf(out, "%s%s", type.address ? "&" : "", piper_type_name(type.piper));
}

/* Calificar solo nombres repetidos o que se confundan con temporales t0, t1... */
static void write_symbol(FILE *out, const SymbolTable *table, const Symbol *symbol)
{
    int matches = 0, owner = -1;
    bool temporary = symbol->name[0] == 't' && symbol->name[1] != '\0';
    for (const char *p = symbol->name + 1; temporary && *p; p++)
        if (!isdigit((unsigned char)*p)) temporary = false;
    for (const Scope *scope = table->all_scopes; scope; scope = scope->next_all)
        for (const Symbol *s = scope->symbols_head; s; s = s->next) {
            if (!strcmp(s->name, symbol->name)) matches++;
            if (s == symbol) owner = scope->scope_id;
        }
    fputs(symbol->name, out);
    if (matches > 1 || temporary) fprintf(out, "@%d", owner);
}

static void write_declaration(FILE *out, const SymbolTable *table, const Symbol *symbol)
{
    write_symbol(out, table, symbol);
    fputc(':', out);
    write_type(out, (IrType){.piper = symbol->declared_type,
        .address = symbol->is_array && symbol->symbol_class == SYMBOL_PARAM});
    /* Se imprime la geometría almacenada: los vectores tienen una columna. */
    if (symbol->is_array) fprintf(out, "[%dx%d]", symbol->array_rows, symbol->array_cols);
}

static bool visible_scope(const Scope *scope, const IrRoutine *routine)
{
    if (scope->is_global) return true;
    if (routine->symbol == NULL) return false;
    for (const Scope *p = scope; p; p = p->parent)
        if (p == routine->symbol->function_scope) return true;
    return false;
}

/* Índice local de impresión, liberado al salir. Invertir las listas enlazadas
 * del semántico recupera orden de creación sin modificar la tabla prestada. */
static IrStatus write_variables(FILE *out, const IrProgram *program, const IrRoutine *routine)
{
    size_t count = 0;
    for (const Scope *s = program->symbols->all_scopes; s; s = s->next_all)
        if (visible_scope(s, routine))
            for (const Symbol *v = s->symbols_head; v; v = v->next)
                if (v->symbol_class == SYMBOL_VAR) count++;
    if (count > SIZE_MAX / sizeof(Symbol *)) return IR_OUT_OF_MEMORY;
    const Symbol **variables = count ? malloc(count * sizeof(*variables)) : NULL;
    if (count && variables == NULL) return IR_OUT_OF_MEMORY;
    size_t i = count;
    for (const Scope *s = program->symbols->all_scopes; s; s = s->next_all)
        if (visible_scope(s, routine))
            for (const Symbol *v = s->symbols_head; v; v = v->next)
                if (v->symbol_class == SYMBOL_VAR) variables[--i] = v;
    fputs("    variables:", out);
    for (i = 0; i < count; i++) {
        bool global = false;
        for (const Symbol *v = program->symbols->global_scope->symbols_head; v; v = v->next)
            if (v == variables[i]) global = true;
        fprintf(out, "%s%s ", i ? ", " : " ", global ? "global" : "local");
        write_declaration(out, program->symbols, variables[i]);
    }
    fputc('\n', out);
    free(variables);
    return IR_OK;
}

/* Reunir temporales y pares valor/tipo constantes, sin tabla persistente extra.
 * El IR llega validado; también se rechaza incoherencia de un temporal impreso. */
static IrStatus collect_value(IrOperand value, IrOperand **values, size_t *count, size_t *capacity)
{
    if (value.kind != IR_OPERAND_TEMP && value.kind != IR_OPERAND_CONSTANT) return IR_OK;
    for (size_t i = 0; i < *count; i++) {
        IrOperand previous = (*values)[i];
        if (previous.kind != value.kind) continue;
        bool same_type = previous.type.piper == value.type.piper && previous.type.address == value.type.address;
        if (value.kind == IR_OPERAND_TEMP && previous.value.temp == value.value.temp)
            return same_type ? IR_OK : IR_INVALID_PROGRAM;
        if (value.kind == IR_OPERAND_CONSTANT && same_type && previous.value.constant == value.value.constant)
            return IR_OK;
    }
    if (*count == *capacity) {
        if (*capacity > SIZE_MAX / sizeof(IrOperand) / 2) return IR_OUT_OF_MEMORY;
        size_t next = *capacity ? *capacity * 2 : 8;
        IrOperand *resized = realloc(*values, next * sizeof(*resized));
        if (resized == NULL) return IR_OUT_OF_MEMORY;
        *values = resized;
        *capacity = next;
    }
    (*values)[(*count)++] = value;
    return IR_OK;
}

static void write_operand(FILE *out, const IrProgram *program, IrOperand operand,
                          const IrOperand *values, size_t count)
{
    switch (operand.kind) {
    case IR_OPERAND_SYMBOL: write_symbol(out, program->symbols, operand.value.symbol); break;
    case IR_OPERAND_TEMP: fprintf(out, "t%zu", operand.value.temp); break;
    case IR_OPERAND_CONSTANT:
        fprintf(out, "%" PRId64, operand.value.constant);
        for (size_t i = 0; i < count; i++)
            if (values[i].kind == IR_OPERAND_CONSTANT && values[i].value.constant == operand.value.constant &&
                values[i].type.piper != operand.type.piper) {
                fputc(':', out); write_type(out, operand.type); break;
            }
        break;
    case IR_OPERAND_NONE: break;
    }
}

static void write_flags(FILE *out, const IrInstruction *in)
{
    const unsigned bits[] = {IR_CHECKED, IR_READ_MEMORY, IR_WRITE_MEMORY, IR_READ_CRYPTO, IR_WRITE_CRYPTO, IR_MAY_TRAP};
    const char *labels[] = {"comprueba rango del resultado ", "lee memoria", "escribe memoria",
        "lee estado criptográfico", "escribe estado criptográfico", "puede fallar"};
    bool first = true;
    for (size_t i = 0; i < sizeof(bits) / sizeof(*bits); i++)
        if (in->flags & bits[i]) {
            fputs(first ? " ; " : ", ", out); fputs(labels[i], out);
            if (bits[i] == IR_CHECKED) write_type(out, in->result.type);
            first = false;
        }
}

static void write_instruction(FILE *out, const IrProgram *program, const IrInstruction *in,
                              const IrOperand *values, size_t count)
{
    static const char *const operators[] = {
        [IR_OP_ADD] = "+", [IR_OP_SUB] = "-", [IR_OP_MUL] = "*", [IR_OP_DIV] = "/",
        [IR_OP_MOD] = "%", [IR_OP_POW] = "^", [IR_OP_NEG] = "-",
        [IR_OP_EQ] = "==", [IR_OP_NE] = "!=", [IR_OP_LT] = "<", [IR_OP_LE] = "<=",
        [IR_OP_GT] = ">", [IR_OP_GE] = ">=", [IR_OP_AND] = "and", [IR_OP_OR] = "or",
        [IR_OP_XOR] = "xor", [IR_OP_NOT] = "not "
    };
    const char *intrinsics[] = {"crypto_auth", "crypto_write_key", "crypto_feistel", "crypto_revoke"};
    fputs("    ", out);
    if (in->result.kind != IR_OPERAND_NONE) {
        write_operand(out, program, in->result, values, count); fputs(" = ", out);
    }
    if (in->op >= IR_OP_ADD && in->op <= IR_OP_NOT) {
        if (in->op == IR_OP_NEG || in->op == IR_OP_NOT) fputs(operators[in->op], out);
        write_operand(out, program, in->arg1, values, count);
        if (in->arg2.kind != IR_OPERAND_NONE) {
            fprintf(out, " %s ", operators[in->op]);
            write_operand(out, program, in->arg2, values, count);
        }
    } else switch (in->op) {
    case IR_OP_COPY: case IR_OP_ADDR: case IR_OP_LOAD:
        if (in->op == IR_OP_ADDR) fputc('&', out);
        if (in->op == IR_OP_LOAD) fputc('*', out);
        write_operand(out, program, in->arg1, values, count);
        break;
    case IR_OP_STORE:
        fputc('*', out); write_operand(out, program, in->arg1, values, count); fputs(" = ", out);
        write_operand(out, program, in->arg2, values, count); break;
    case IR_OP_CHECK_BOUNDS:
        fputs("check 0 <= ", out); write_operand(out, program, in->arg1, values, count);
        fputs(" < ", out); write_operand(out, program, in->arg2, values, count); break;
    case IR_OP_CALL: case IR_OP_INTRINSIC:
        if (in->op == IR_OP_CALL) { fputs("call ", out); write_symbol(out, program->symbols, in->extra.call.callee); }
        else fputs(intrinsics[in->extra.call.intrinsic], out);
        fputc('(', out);
        for (size_t i = 0; i < in->extra.call.argument_count; i++) {
            if (i) fputs(", ", out);
            write_operand(out, program, in->extra.call.arguments[i], values, count);
        }
        fputc(')', out); break;
    case IR_OP_JUMP: fprintf(out, "jump B%zu", in->extra.targets[0]); break;
    case IR_OP_BRANCH:
        fputs("if ", out); write_operand(out, program, in->arg1, values, count);
        fprintf(out, " jump B%zu else B%zu", in->extra.targets[0], in->extra.targets[1]); break;
    case IR_OP_RETURN:
        fputs("return", out);
        if (in->arg1.kind != IR_OPERAND_NONE) { fputc(' ', out); write_operand(out, program, in->arg1, values, count); }
        break;
    default: break; /* El modelo de entrada ya fue validado. */
    }
    write_flags(out, in); fputc('\n', out);
}

/** Exportar el modelo validado, sin modificarlo ni cerrar el stream prestado. */
IrStatus ir_write(FILE *output, const IrProgram *program)
{
    if (output == NULL || program == NULL || program->symbols == NULL ||
        program->symbols->global_scope == NULL) return IR_INVALID_INPUT;
    for (size_t r = 0; r < program->routine_count; r++) {
        const IrRoutine *routine = &program->routines[r];
        IrOperand *values = NULL; size_t count = 0, capacity = 0;
        IrStatus status = IR_OK;
        for (size_t b = 0; b < routine->block_count && status == IR_OK; b++)
            for (size_t i = 0; i < routine->blocks[b].instruction_count && status == IR_OK; i++) {
                const IrInstruction *in = &routine->blocks[b].instructions[i];
                IrOperand operands[] = {in->arg1, in->arg2, in->result};
                for (size_t j = 0; j < 3 && status == IR_OK; j++)
                    status = collect_value(operands[j], &values, &count, &capacity);
                if (in->op == IR_OP_CALL || in->op == IR_OP_INTRINSIC)
                    for (size_t j = 0; j < in->extra.call.argument_count && status == IR_OK; j++)
                        status = collect_value(in->extra.call.arguments[j], &values, &count, &capacity);
            }
        if (status != IR_OK) { free(values); return status; }
        if (r) fputc('\n', output);
        fputs("function ", output);
        if (routine->symbol) write_symbol(output, program->symbols, routine->symbol);
        else fputs("<startup>", output);
        fputc('(', output);
        if (routine->symbol)
            for (int i = 0; i < routine->symbol->param_count; i++) {
                if (i) fputs(", ", output);
                write_declaration(output, program->symbols, routine->symbol->params[i]);
            }
        fprintf(output, ") -> %s\n", piper_type_name(routine->symbol ? routine->symbol->declared_type : PIPER_TYPE_NONE));
        if (routine->blocks[0].id != routine->entry)
            fprintf(output, "    entrada: B%zu\n", routine->entry);
        status = write_variables(output, program, routine);
        if (status != IR_OK) { free(values); return status; }
        fputs("    temporales:", output);
        bool first = true;
        for (IrId id = 0; id < routine->temp_count; id++)
            for (size_t i = 0; i < count; i++)
                if (values[i].kind == IR_OPERAND_TEMP && values[i].value.temp == id) {
                    fprintf(output, "%st%zu:", first ? " " : ", ", id);
                    write_type(output, values[i].type); first = false;
                }
        fputs("\n    constantes:", output); first = true;
        for (size_t i = 0; i < count; i++)
            if (values[i].kind == IR_OPERAND_CONSTANT) {
                fprintf(output, "%s%" PRId64 ":", first ? " " : ", ", values[i].value.constant);
                write_type(output, values[i].type); first = false;
            }
        fputc('\n', output);
        for (size_t b = 0; b < routine->block_count; b++) {
            fprintf(output, "B%zu:\n", routine->blocks[b].id);
            for (size_t i = 0; i < routine->blocks[b].instruction_count; i++)
                write_instruction(output, program, &routine->blocks[b].instructions[i], values, count);
        }
        free(values);
        if (ferror(output)) return IR_IO_ERROR;
    }
    return ferror(output) ? IR_IO_ERROR : IR_OK;
}
