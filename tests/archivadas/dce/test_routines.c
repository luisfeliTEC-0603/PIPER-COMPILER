#include "dce/dce.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static Symbol function(const char *name)
{
    Symbol symbol = {.symbol_class = SYMBOL_FUNC, .declared_type = PIPER_TYPE_NONE};
    snprintf(symbol.name, sizeof(symbol.name), "%s", name);
    return symbol;
}

static IrProgram *program(Symbol *symbols, size_t count, size_t startup,
                           const SymbolTable *table)
{
    IrProgram *ir = calloc(1, sizeof(*ir));
    assert(ir != NULL);
    *ir = (IrProgram){.symbols = table, .routine_count = count,
        .routine_capacity = count, .startup = startup};
    ir->routines = calloc(count, sizeof(*ir->routines));
    assert(ir->routines != NULL);
    for (size_t r = 0; r < count; r++) {
        IrRoutine *routine = &ir->routines[r];
        *routine = (IrRoutine){.symbol = r == startup ? NULL : &symbols[r],
            .block_count = 1, .block_capacity = 1, .entry = 10 + r};
        routine->blocks = calloc(1, sizeof(*routine->blocks));
        assert(routine->blocks != NULL);
        IrBlock *block = routine->blocks;
        *block = (IrBlock){.id = routine->entry, .instruction_count = 1,
            .instruction_capacity = 3};
        block->instructions = calloc(3, sizeof(*block->instructions));
        assert(block->instructions != NULL);
        block->instructions[0].op = IR_OP_RETURN;
    }
    return ir;
}

static void call(IrProgram *ir, size_t from, const Symbol *callee)
{
    IrBlock *block = ir->routines[from].blocks;
    assert(block->instruction_count < block->instruction_capacity);
    size_t position = block->instruction_count - 1;
    IrOperand *arguments = calloc(1, sizeof(*arguments));
    assert(arguments != NULL);
    *arguments = (IrOperand){.kind = IR_OPERAND_CONSTANT,
        .type.piper = PIPER_TYPE_B32, .value.constant = 7};
    block->instructions[position] = (IrInstruction){.op = IR_OP_CALL,
        .flags = IR_WRITE_MEMORY | IR_MAY_TRAP,
        .extra.call = {.callee = callee, .arguments = arguments, .argument_count = 1}};
    block->instructions[block->instruction_count++] = (IrInstruction){.op = IR_OP_RETURN};
}

static void verify_graph_and_idempotence(IrProgram *ir)
{
    IrDiagnostic diagnostic;
    assert(ir_validate(ir, &diagnostic) == IR_OK);
    CfgProgram *cfg = NULL;
    CfgDiagnostic graph_error;
    assert(cfg_build(ir, &cfg, &graph_error) == CFG_OK);
    assert(cfg_validate(cfg, &graph_error) == CFG_OK);
    cfg_program_free(cfg);
    DceRoutineReport second;
    assert(dce_remove_unreachable_routines(ir, &second) == DCE_OK);
    assert(second.removed_routines == 0 && second.removed_instructions == 0);
}

static void chains_and_recursion(const SymbolTable *table)
{
    Symbol symbols[] = {function("dead_a"), function("leaf"), function("main"),
        function("chain"), function("dead_b"), function("recursive")};
    IrProgram *ir = program(symbols, 6, IR_NO_ID, table);
    call(ir, 0, &symbols[4]);
    call(ir, 4, &symbols[0]);       /* Ciclo sin camino desde main. */
    call(ir, 2, &symbols[3]);
    call(ir, 3, &symbols[1]);
    call(ir, 3, &symbols[5]);
    call(ir, 5, &symbols[5]);      /* Recursion alcanzable: conservar. */
    IrInstruction *main_body = ir->routines[2].blocks[0].instructions;
    IrOperand *arguments = main_body[0].extra.call.arguments;
    /* Incluso sin flags, la llamada se conserva: no se infieren efectos. */
    main_body[0].flags = 0;
    DceRoutineReport removed;
    assert(dce_remove_unreachable_routines(ir, &removed) == DCE_OK);
    assert(removed.removed_routines == 2 && removed.removed_instructions == 4);
    assert(ir->routine_count == 4 && ir->startup == IR_NO_ID);
    const size_t kept[] = {1, 2, 3, 5};
    for (size_t r = 0; r < 4; r++)
        assert(ir->routines[r].symbol == &symbols[kept[r]]);
    assert(ir->routines[1].blocks[0].instructions == main_body);
    assert(main_body[0].extra.call.arguments == arguments && main_body[0].flags == 0);
    verify_graph_and_idempotence(ir);
    ir_program_free(ir);
}

static void startup_root(const SymbolTable *table)
{
    Symbol symbols[] = {function("dead"), function("main"), function("initialize"), {0}};
    IrProgram *ir = program(symbols, 4, 3, table);
    call(ir, 3, &symbols[2]);       /* Solo startup llama al inicializador. */
    call(ir, 3, &symbols[1]);
    DceRoutineReport removed;
    assert(dce_remove_unreachable_routines(ir, &removed) == DCE_OK);
    assert(removed.removed_routines == 1 && removed.removed_instructions == 1);
    assert(ir->routine_count == 3 && ir->startup == 2);
    assert(ir->routines[0].symbol == &symbols[1]);
    assert(ir->routines[1].symbol == &symbols[2]);
    assert(ir->routines[2].symbol == NULL);
    verify_graph_and_idempotence(ir);
    ir_program_free(ir);
}

static void invalid_call_or_entry(const SymbolTable *table)
{
    Symbol symbols[] = {function("unused"), function("main")};
    Symbol external = function("missing_body");
    IrProgram *ir = program(symbols, 2, IR_NO_ID, table);
    call(ir, 1, &external);
    IrBlock *unused_blocks = ir->routines[0].blocks;
    DceRoutineReport removed;
    assert(dce_remove_unreachable_routines(ir, &removed) == DCE_ANALYSIS_ERROR);
    assert(removed.removed_routines == 0 && removed.removed_instructions == 0);
    assert(ir->routine_count == 2 && ir->routines[0].blocks == unused_blocks);
    strcpy(symbols[1].name, "other");
    assert(dce_remove_unreachable_routines(ir, &removed) == DCE_ANALYSIS_ERROR);
    assert(removed.removed_routines == 0 && ir->routine_count == 2);
    ir_program_free(ir);
}

int main(void)
{
    SymbolTable table = {0};
    chains_and_recursion(&table);
    startup_root(&table);
    invalid_call_or_entry(&table);
    puts("DCE routines: chains, recursion, startup, ownership, idempotence and errors OK");
    return 0;
}
