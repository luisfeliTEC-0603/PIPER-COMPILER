#include "regalloc_machine.h"
#include <assert.h>
#include <stdio.h>

static IrOperand temp(size_t id)
{
    return (IrOperand){.kind = IR_OPERAND_TEMP,
        .type = {.piper = PIPER_TYPE_B32}, .value.temp = id};
}

static IrOperand constant(int64_t value)
{
    return (IrOperand){.kind = IR_OPERAND_CONSTANT,
        .type = {.piper = PIPER_TYPE_B32}, .value.constant = value};
}

static RegallocAnalysis analysis_for(IrProgram *ir, CfgProgram *cfg,
                                     RegallocRoutine *mapped)
{
    *cfg = (CfgProgram){.ir = ir};
    return (RegallocAnalysis){.ir = ir, .cfg = cfg, .routines = mapped,
                              .routine_count = ir->routine_count};
}

static void test_call_result_and_live_across(void)
{
    Symbol callee = {.symbol_class = SYMBOL_FUNC, .param_count = 2,
                     .declared_type = PIPER_TYPE_B32};
    Symbol caller = {.symbol_class = SYMBOL_FUNC, .call_reserve_size = 64};
    IrOperand args[] = {temp(0), temp(1)};
    IrInstruction instructions[] = {
        {.op = IR_OP_COPY, .result = temp(0), .arg1 = constant(11)},
        {.op = IR_OP_COPY, .result = temp(1), .arg1 = constant(22)},
        {.op = IR_OP_CALL, .result = temp(2),
         .flags = IR_READ_MEMORY | IR_WRITE_MEMORY | IR_MAY_TRAP,
         .extra.call = {.callee = &callee, .arguments = args,
                        .argument_count = 2}},
        {.op = IR_OP_ADD, .result = temp(3), .arg1 = temp(0),
         .arg2 = temp(2)},
        {.op = IR_OP_RETURN, .arg1 = temp(3)}
    };
    IrBlock block = {.id = 8, .instructions = instructions,
                     .instruction_count = 5};
    IrRoutine routine = {.symbol = &caller, .blocks = &block,
                         .block_count = 1, .temp_count = 4};
    SymbolTable symbols = {0};
    IrProgram ir = {.symbols = &symbols, .routines = &routine,
                    .routine_count = 1};
    CfgProgram cfg;
    RegallocBlock mapped_block = {.id = 8, .cfg_node = 1};
    RegallocRoutine mapped = {.blocks = &mapped_block, .block_count = 1};
    RegallocAnalysis analysis = analysis_for(&ir, &cfg, &mapped);
    RegallocMachineProgram *machine = NULL;
    RegallocDiagnostic error;
    assert(regalloc_lower_scalar(&analysis, &machine, &error) == REGALLOC_OK);
    assert(machine->call_site_count == 1);
    const RegallocMachineRoutine *lowered = &machine->routines[0];
    assert(lowered->blocks[0].instruction_count == 10);
    const RegallocMachineInstruction *m = lowered->blocks[0].instructions;
    assert(m[2].op == REGALLOC_M_CALL_SAVE && m[2].call_site_id == 1 &&
           m[2].fixed_writes == 0 && m[2].flags == IR_WRITE_MEMORY);
    assert(m[3].op == REGALLOC_M_CALL_ARG && m[3].use1 == 0 &&
           m[3].call_arg_index == 0 && m[3].fixed_writes == REGALLOC_PHYS_R1);
    assert(m[4].op == REGALLOC_M_CALL_ARG && m[4].use1 == 1 &&
           m[4].call_arg_index == 1 && m[4].fixed_writes == REGALLOC_PHYS_R2);
    assert(m[5].op == REGALLOC_M_CALL_RESULT && m[5].def == 2 &&
           m[5].symbol == &callee && m[5].call_site_id == 1 &&
           m[5].call_arg_count == 2 &&
           (m[5].flags & IR_MAY_TRAP) != 0);

    LivenessResult *live = NULL;
    LivenessDiagnostic live_error;
    assert(liveness_analyze_machine(machine, &live, &live_error) == LIVENESS_OK);
    RegallocMachineGraph *graph = NULL;
    assert(regalloc_build_machine_graph(machine, live, 0, &graph, &error) ==
           REGALLOC_OK);
    assert((graph->forbidden_colors[0] & REGALLOC_PHYS_R1) != 0);
    RegallocColoring *coloring = NULL;
    assert(regalloc_color_machine_graph(graph, &coloring, &error) == REGALLOC_OK);
    assert(coloring->uncolored_count == 0);
    RegallocAssignedRoutine assigned_routine = {
        .register_for_temp = coloring->colors
    };
    RegallocAssignedProgram assigned = {.machine = machine,
        .routines = &assigned_routine, .routine_count = 1};
    assert(regalloc_assigned_program_validate(&assigned, &error) == REGALLOC_OK);
    regalloc_coloring_free(coloring);
    regalloc_machine_graph_free(graph);
    liveness_result_free(live);
    regalloc_machine_program_free(machine);
}

static void test_stack_arguments_and_array_rejection(void)
{
    Symbol callee = {.symbol_class = SYMBOL_FUNC, .param_count = 6,
                     .declared_type = PIPER_TYPE_NONE};
    Symbol caller = {.symbol_class = SYMBOL_FUNC, .call_reserve_size = 64};
    IrOperand args[] = {constant(1), constant(2), constant(3),
                        constant(4), constant(5), constant(6)};
    IrInstruction instructions[] = {
        {.op = IR_OP_CALL, .result = {.kind = IR_OPERAND_NONE},
         .flags = IR_READ_MEMORY | IR_WRITE_MEMORY,
         .extra.call = {.callee = &callee, .arguments = args,
                        .argument_count = 6}},
        {.op = IR_OP_RETURN}
    };
    IrBlock block = {.id = 2, .instructions = instructions,
                     .instruction_count = 2};
    IrRoutine routine = {.symbol = &caller, .blocks = &block,
                         .block_count = 1};
    SymbolTable symbols = {0};
    IrProgram ir = {.symbols = &symbols, .routines = &routine,
                    .routine_count = 1};
    CfgProgram cfg;
    RegallocBlock mapped_block = {.id = 2, .cfg_node = 1};
    RegallocRoutine mapped = {.blocks = &mapped_block, .block_count = 1};
    RegallocAnalysis analysis = analysis_for(&ir, &cfg, &mapped);
    RegallocMachineProgram *machine = NULL;
    RegallocDiagnostic error;
    assert(regalloc_lower_scalar(&analysis, &machine, &error) == REGALLOC_OK);
    const RegallocMachineRoutine *lowered = &machine->routines[0];
    assert(lowered->temp_count == 6);
    assert(lowered->blocks[0].instruction_count == 15);
    const RegallocMachineInstruction *m = lowered->blocks[0].instructions;
    assert(m[6].op == REGALLOC_M_CALL_SAVE);
    assert(m[7].op == REGALLOC_M_CALL_ARG && m[7].call_arg_index == 4 &&
           m[7].fixed_reads == REGALLOC_PHYS_R7 &&
           m[7].flags == IR_WRITE_MEMORY);
    assert(m[8].op == REGALLOC_M_CALL_ARG && m[8].call_arg_index == 5 &&
           m[8].fixed_reads == REGALLOC_PHYS_R7 &&
           m[8].flags == IR_WRITE_MEMORY);
    assert(m[13].op == REGALLOC_M_CALL_VOID && m[13].call_arg_count == 6);
    LivenessResult *live = NULL;
    LivenessDiagnostic live_error;
    assert(liveness_analyze_machine(machine, &live, &live_error) == LIVENESS_OK);
    RegallocMachineGraph *graph = NULL;
    assert(regalloc_build_machine_graph(machine, live, 0, &graph, &error) ==
           REGALLOC_OK);
    RegallocColoring *coloring = NULL;
    assert(regalloc_color_machine_graph(graph, &coloring, &error) == REGALLOC_OK);
    assert(coloring->uncolored_count >= 2); /* Seis valores simultáneos, K=4. */
    regalloc_coloring_free(coloring);
    regalloc_machine_graph_free(graph);
    liveness_result_free(live);
    regalloc_machine_program_free(machine);

    caller.call_reserve_size = SYMBOL_CALL_RECORD_SIZE;
    machine = NULL;
    assert(regalloc_lower_scalar(&analysis, &machine, &error) ==
           REGALLOC_INVALID_INPUT);
    assert(machine == NULL);
    caller.call_reserve_size = 64;

    Symbol array = {.symbol_class = SYMBOL_VAR, .is_array = 1};
    args[0] = (IrOperand){.kind = IR_OPERAND_SYMBOL,
        .type = {.piper = PIPER_TYPE_B32, .address = true},
        .value.symbol = &array};
    machine = NULL;
    assert(regalloc_lower_scalar(&analysis, &machine, &error) ==
           REGALLOC_UNSUPPORTED);
    assert(machine == NULL);
}

int main(void)
{
    test_call_result_and_live_across();
    test_stack_arguments_and_array_rejection();
    puts("bajada virtual de llamadas: ok");
    return 0;
}
