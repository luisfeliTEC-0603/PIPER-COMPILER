#include "regalloc_machine.h"
#include <assert.h>
#include <stdio.h>

static IrOperand temp(size_t id)
{
    return (IrOperand){.kind = IR_OPERAND_TEMP,
        .type = {.piper = PIPER_TYPE_UB32}, .value.temp = id};
}

static IrOperand constant(int64_t value)
{
    return (IrOperand){.kind = IR_OPERAND_CONSTANT,
        .type = {.piper = PIPER_TYPE_UB32}, .value.constant = value};
}

static IrOperand symbol(const Symbol *value)
{
    return (IrOperand){.kind = IR_OPERAND_SYMBOL,
        .type = {.piper = PIPER_TYPE_UB32}, .value.symbol = value};
}

/* La tabla de simbolos y los flags corresponden a un IR ya preparado. El
 * programa de prueba conserva vivos todos los prestamos hasta liberar machine.
 */
static void test_symbol_access_and_large_immediate(void)
{
    Symbol global = {.symbol_class = SYMBOL_VAR, .declared_type = PIPER_TYPE_UB32,
                     .address_or_offset = 12};
    Symbol local = {.symbol_class = SYMBOL_VAR, .declared_type = PIPER_TYPE_UB32,
                    .address_or_offset = 24};
    Symbol function = {.symbol_class = SYMBOL_FUNC,
                       .total_stack_frame_size = 32};
    Scope global_scope = {.is_global = 1, .symbols_head = &global};
    SymbolTable symbols = {.global_scope = &global_scope};
    IrInstruction instructions[] = {
        {.op = IR_OP_COPY, .result = temp(0), .arg1 = constant(70000)},
        {.op = IR_OP_COPY, .result = symbol(&global), .arg1 = temp(0),
         .flags = IR_WRITE_MEMORY},
        {.op = IR_OP_COPY, .result = temp(1), .arg1 = symbol(&global),
         .flags = IR_READ_MEMORY},
        {.op = IR_OP_COPY, .result = symbol(&local), .arg1 = temp(1)},
        {.op = IR_OP_COPY, .result = temp(2), .arg1 = symbol(&local)},
        {.op = IR_OP_ADD, .result = temp(3), .arg1 = temp(2),
         .arg2 = constant(70000)},
        {.op = IR_OP_ADD, .result = temp(4), .arg1 = symbol(&global),
         .arg2 = symbol(&local), .flags = IR_READ_MEMORY},
        {.op = IR_OP_ADD, .result = temp(5), .arg1 = temp(0),
         .arg2 = temp(4)},
        {.op = IR_OP_RETURN, .arg1 = temp(5)}
    };
    IrBlock block = {.id = 19, .instructions = instructions,
                     .instruction_count = 9};
    IrRoutine routine = {.symbol = &function, .blocks = &block, .block_count = 1,
                         .temp_count = 6};
    IrProgram ir = {.symbols = &symbols, .routines = &routine,
                    .routine_count = 1};
    CfgProgram cfg = {.ir = &ir};
    RegallocBlock mapped_block = {.id = 19, .cfg_node = 1};
    RegallocRoutine mapped_routine = {.blocks = &mapped_block,
                                      .block_count = 1};
    RegallocAnalysis analysis = {.ir = &ir, .cfg = &cfg,
        .routines = &mapped_routine, .routine_count = 1};
    RegallocMachineProgram *machine = NULL;
    RegallocDiagnostic error;
    assert(regalloc_lower_scalar(&analysis, &machine, &error) == REGALLOC_OK);
    assert(machine->routines[0].temp_count == 8); /* constante y símbolo local. */
    RegallocMachineBlock *lowered = &machine->routines[0].blocks[0];
    assert(lowered->id == 19 && lowered->instruction_count == 15);
    const RegallocMachineInstruction *m = lowered->instructions;
    assert(m[0].op == REGALLOC_M_IMM && m[0].immediate == 70000 &&
           m[0].fixed_reads == REGALLOC_PHYS_R0);
    assert(m[1].op == REGALLOC_M_STORE_SYMBOL &&
           m[1].immediate == SYMBOL_DATA_BASE_ADDRESS + 12 &&
           m[1].fixed_reads == (REGALLOC_PHYS_R0 | REGALLOC_PHYS_R5) &&
           m[1].fixed_writes == REGALLOC_PHYS_R5 &&
           m[1].flags == IR_WRITE_MEMORY);
    assert(m[2].op == REGALLOC_M_LOAD_SYMBOL &&
           m[2].immediate == SYMBOL_DATA_BASE_ADDRESS + 12 &&
           m[2].fixed_reads == REGALLOC_PHYS_R0 &&
           m[2].flags == IR_READ_MEMORY);
    assert(m[3].op == REGALLOC_M_STORE_SYMBOL && m[3].immediate == 24 &&
           m[3].fixed_reads == REGALLOC_PHYS_R7 && m[3].fixed_writes == 0);
    assert(m[4].op == REGALLOC_M_LOAD_SYMBOL && m[4].immediate == 24 &&
           m[4].fixed_reads == REGALLOC_PHYS_R7);
    assert(m[5].op == REGALLOC_M_IMM && m[5].immediate == 70000 &&
           m[5].fixed_reads == REGALLOC_PHYS_R0);
    assert(m[8].op == REGALLOC_M_LOAD_SYMBOL && m[8].immediate == 24 &&
           m[8].fixed_reads == REGALLOC_PHYS_R7);
    assert(m[9].op == REGALLOC_M_LOAD_SYMBOL &&
           m[9].immediate == SYMBOL_DATA_BASE_ADDRESS + 12 &&
           m[9].fixed_reads == REGALLOC_PHYS_R0);

    LivenessResult *live = NULL;
    LivenessDiagnostic live_error;
    assert(liveness_analyze_machine(machine, &live, &live_error) == LIVENESS_OK);
    RegallocMachineGraph *graph = NULL;
    assert(regalloc_build_machine_graph(machine, live, 0, &graph, &error) ==
           REGALLOC_OK);
    /* t0 sobrevive al store global; el grafo registra el clobber de r5. */
    assert((graph->forbidden_colors[0] & REGALLOC_PHYS_R5) != 0);
    RegallocColoring *coloring = NULL;
    assert(regalloc_color_machine_graph(graph, &coloring, &error) == REGALLOC_OK);
    assert(coloring->uncolored_count == 0);
    RegallocAssignedRoutine assigned_routine = {
        .register_for_temp = coloring->colors, .frame_bytes = 32
    };
    RegallocAssignedProgram assigned = {.machine = machine,
        .routines = &assigned_routine, .routine_count = 1};
    assert(regalloc_assigned_program_validate(&assigned, &error) == REGALLOC_OK);

    regalloc_coloring_free(coloring);
    regalloc_machine_graph_free(graph);
    liveness_result_free(live);
    regalloc_machine_program_free(machine);
}

int main(void)
{
    test_symbol_access_and_large_immediate();
    puts("bajada de simbolos R2b: ok");
    return 0;
}
