#include "regalloc_machine.h"
#include <assert.h>
#include <stdio.h>

static IrOperand temp(size_t id, PiperType type)
{
    return (IrOperand){.kind = IR_OPERAND_TEMP, .type = {.piper = type},
                       .value.temp = id};
}

static IrOperand constant(int64_t value, PiperType type)
{
    return (IrOperand){.kind = IR_OPERAND_CONSTANT, .type = {.piper = type},
                       .value.constant = value};
}

static RegallocAnalysis analysis_for(IrProgram *ir, CfgProgram *cfg,
                                     RegallocRoutine *mapped)
{
    *cfg = (CfgProgram){.ir = ir};
    return (RegallocAnalysis){.ir = ir, .cfg = cfg, .routines = mapped,
                              .routine_count = ir->routine_count};
}

/* La fuente sigue viva durante el chequeo y despues: r5/r6 se registran en
 * el grafo. El resultado conserva por separado los tipos fuente y destino. */
static void check_temp(IrOpcode op, PiperType source_type, PiperType result_type)
{
    RegallocMachineOp expected = op == IR_OP_NEG
        ? REGALLOC_M_CHECKED_NEG : REGALLOC_M_CHECKED_COPY;
    IrInstruction instructions[] = {
        {.op = IR_OP_COPY, .result = temp(0, source_type),
         .arg1 = constant(2, source_type)},
        {.op = op, .result = temp(1, result_type),
         .arg1 = temp(0, source_type),
         .flags = IR_CHECKED | IR_MAY_TRAP},
        {.op = IR_OP_COPY, .result = temp(2, source_type),
         .arg1 = temp(0, source_type)},
        {.op = IR_OP_RETURN, .arg1 = temp(1, result_type)}
    };
    IrBlock block = {.id = 4, .instructions = instructions,
                     .instruction_count = 4};
    IrRoutine routine = {.blocks = &block, .block_count = 1, .temp_count = 3};
    SymbolTable symbols = {0};
    IrProgram ir = {.symbols = &symbols, .routines = &routine,
                    .routine_count = 1};
    CfgProgram cfg;
    RegallocBlock mapped_block = {.id = 4, .cfg_node = 1};
    RegallocRoutine mapped = {.blocks = &mapped_block, .block_count = 1};
    RegallocAnalysis analysis = analysis_for(&ir, &cfg, &mapped);
    RegallocDiagnostic error;
    RegallocMachineProgram *machine = NULL;
    assert(regalloc_lower_scalar(&analysis, &machine, &error) == REGALLOC_OK);
    const RegallocMachineBlock *lowered = &machine->routines[0].blocks[0];
    const RegallocMachineInstruction *m = lowered->instructions;
    assert(lowered->instruction_count == 5);
    assert(m[1].op == expected && m[1].def == 1 && m[1].use1 == 0);
    assert(m[1].source_type.piper == source_type &&
           m[1].type.piper == result_type);
    assert(m[1].fixed_reads == (REGALLOC_PHYS_R0 | REGALLOC_PHYS_R5 |
                                REGALLOC_PHYS_R6));
    assert(m[1].fixed_writes == (REGALLOC_PHYS_R5 | REGALLOC_PHYS_R6));
    assert(m[1].flags == (IR_CHECKED | IR_MAY_TRAP));

    LivenessResult *live = NULL;
    LivenessDiagnostic live_error;
    assert(liveness_analyze_machine(machine, &live, &live_error) == LIVENESS_OK);
    RegallocMachineGraph *graph = NULL;
    assert(regalloc_build_machine_graph(machine, live, 0, &graph, &error) ==
           REGALLOC_OK);
    assert((graph->forbidden_colors[0] & (REGALLOC_PHYS_R5 |
            REGALLOC_PHYS_R6)) == (REGALLOC_PHYS_R5 | REGALLOC_PHYS_R6));
    RegallocColoring *colors = NULL;
    assert(regalloc_color_machine_graph(graph, &colors, &error) == REGALLOC_OK);
    assert(colors->uncolored_count == 0);
    RegallocAssignedRoutine assigned_routine = {.register_for_temp = colors->colors};
    RegallocAssignedProgram assigned = {.machine = machine,
        .routines = &assigned_routine, .routine_count = 1};
    assert(regalloc_assigned_program_validate(&assigned, &error) == REGALLOC_OK);
    regalloc_coloring_free(colors);
    regalloc_machine_graph_free(graph);
    liveness_result_free(live);
    regalloc_machine_program_free(machine);

    instructions[1].flags = IR_CHECKED;
    machine = NULL;
    assert(regalloc_lower_scalar(&analysis, &machine, &error) == REGALLOC_OK);
    assert((machine->routines[0].blocks[0].instructions[1].flags &
            IR_MAY_TRAP) != 0);
    regalloc_machine_program_free(machine);
    if (op == IR_OP_NEG) {
        instructions[1].flags = 0;
        machine = NULL;
        assert(regalloc_lower_scalar(&analysis, &machine, &error) ==
               REGALLOC_UNSUPPORTED);
        assert(machine == NULL);
    }
}

/* Un COPY comprobado hacia memoria debe comprobar antes del store. */
static void check_symbol_destination(void)
{
    Symbol local = {.symbol_class = SYMBOL_VAR, .declared_type = PIPER_TYPE_UB8,
                    .address_or_offset = 16};
    Symbol caller = {.symbol_class = SYMBOL_FUNC, .total_stack_frame_size = 20};
    IrInstruction instructions[] = {
        {.op = IR_OP_COPY, .result = temp(0, PIPER_TYPE_B32),
         .arg1 = constant(100, PIPER_TYPE_B32)},
        {.op = IR_OP_COPY, .result = {.kind = IR_OPERAND_SYMBOL,
             .type = {.piper = PIPER_TYPE_UB8}, .value.symbol = &local},
         .arg1 = temp(0, PIPER_TYPE_B32),
         .flags = IR_CHECKED | IR_MAY_TRAP | IR_WRITE_MEMORY},
        {.op = IR_OP_RETURN}
    };
    IrBlock block = {.id = 8, .instructions = instructions,
                     .instruction_count = 3};
    IrRoutine routine = {.symbol = &caller, .blocks = &block,
                         .block_count = 1, .temp_count = 1};
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
    const RegallocMachineRoutine *lowered = &machine->routines[0];
    const RegallocMachineInstruction *m = lowered->blocks[0].instructions;
    assert(lowered->temp_count == 2 && lowered->blocks[0].instruction_count == 4);
    assert(m[1].op == REGALLOC_M_CHECKED_COPY && m[1].def == 1 && m[1].use1 == 0);
    assert(m[1].flags == (IR_CHECKED | IR_MAY_TRAP));
    assert(m[2].op == REGALLOC_M_STORE_SYMBOL && m[2].use1 == 1 &&
           m[2].symbol == &local && m[2].immediate == 16 &&
           m[2].flags == IR_WRITE_MEMORY && m[2].fixed_reads == REGALLOC_PHYS_R7);
    LivenessResult *live = NULL;
    LivenessDiagnostic live_error;
    assert(liveness_analyze_machine(machine, &live, &live_error) == LIVENESS_OK);
    RegallocMachineGraph *graph = NULL;
    assert(regalloc_build_machine_graph(machine, live, 0, &graph, &error) ==
           REGALLOC_OK);
    RegallocColoring *colors = NULL;
    assert(regalloc_color_machine_graph(graph, &colors, &error) == REGALLOC_OK);
    assert(colors->uncolored_count == 0);
    RegallocAssignedRoutine assigned_routine = {.register_for_temp = colors->colors,
                                                .frame_bytes = 20};
    RegallocAssignedProgram assigned = {.machine = machine,
        .routines = &assigned_routine, .routine_count = 1};
    assert(regalloc_assigned_program_validate(&assigned, &error) == REGALLOC_OK);
    regalloc_coloring_free(colors);
    regalloc_machine_graph_free(graph);
    liveness_result_free(live);
    regalloc_machine_program_free(machine);
}

int main(void)
{
    PiperType types[] = {PIPER_TYPE_B8, PIPER_TYPE_UB8,
                         PIPER_TYPE_B16, PIPER_TYPE_UB16,
                         PIPER_TYPE_B32, PIPER_TYPE_UB32};
    for (size_t source = 0; source < 6; source++) {
        check_temp(IR_OP_NEG, types[source], PIPER_TYPE_B32);
        for (size_t result = 0; result < 6; result++)
            check_temp(IR_OP_COPY, types[source], types[result]);
    }
    check_symbol_destination();
    puts("conversion y negacion comprobadas R2b: ok");
    return 0;
}
