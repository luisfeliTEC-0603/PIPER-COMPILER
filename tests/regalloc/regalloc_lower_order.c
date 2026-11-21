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

static bool unsigned_type(PiperType type)
{
    return type == PIPER_TYPE_UB8 || type == PIPER_TYPE_UB16 ||
           type == PIPER_TYPE_UB32;
}

static int64_t left_value(PiperType type)
{
    if (type == PIPER_TYPE_UB8) return UINT8_MAX;
    if (type == PIPER_TYPE_UB16) return UINT16_MAX;
    if (type == PIPER_TYPE_UB32) return UINT32_MAX;
    return -1;
}

/* Cada variante recorre bajada, liveness, grafo, coloreo y validacion. El
 * resultado aun es virtual: el ASM tipado se comprobara al existir el emisor. */
static void check_order(IrOpcode op, PiperType left_type, PiperType right_type,
                        RegallocMachineOp strict, bool invert)
{
    IrInstruction instructions[] = {
        {.op = IR_OP_COPY, .result = temp(0, left_type),
         .arg1 = constant(left_value(left_type), left_type)},
        {.op = IR_OP_COPY, .result = temp(1, right_type),
         .arg1 = constant(1, right_type)},
        {.op = op, .result = temp(2, PIPER_TYPE_BOOL),
         .arg1 = temp(0, left_type), .arg2 = temp(1, right_type)},
        {.op = IR_OP_COPY, .result = temp(3, left_type),
         .arg1 = temp(0, left_type)},
        {.op = IR_OP_RETURN, .arg1 = temp(2, PIPER_TYPE_BOOL)}
    };
    IrBlock block = {.id = 9, .instructions = instructions,
                     .instruction_count = 5};
    IrRoutine routine = {.blocks = &block, .block_count = 1,
                         .temp_count = 4};
    SymbolTable symbols = {0};
    IrProgram ir = {.symbols = &symbols, .routines = &routine,
                    .routine_count = 1};
    CfgProgram cfg = {.ir = &ir};
    RegallocBlock mapped_block = {.id = 9, .cfg_node = 1};
    RegallocRoutine mapped = {.blocks = &mapped_block, .block_count = 1};
    RegallocAnalysis analysis = {.ir = &ir, .cfg = &cfg,
        .routines = &mapped, .routine_count = 1};
    RegallocDiagnostic error;
    RegallocMachineProgram *machine = NULL;
    assert(regalloc_lower_scalar(&analysis, &machine, &error) == REGALLOC_OK);

    const RegallocMachineBlock *lowered = &machine->routines[0].blocks[0];
    const RegallocMachineInstruction *m = lowered->instructions;
    assert(lowered->instruction_count == (invert ? 8u : 7u));
    assert(m[2].op == REGALLOC_M_MOVE && m[2].def == 2 && m[2].use1 == 0);
    assert(m[3].op == strict && m[3].def == 2 && m[3].use1 == 1);
    bool left_unsigned = unsigned_type(left_type);
    bool right_unsigned = unsigned_type(right_type);
    assert(m[3].left_unsigned == left_unsigned);
    assert(m[3].right_unsigned == right_unsigned);
    unsigned scratch = left_unsigned && right_unsigned ? 0 : REGALLOC_PHYS_R5;
    assert(m[3].fixed_writes == scratch);
    assert(m[3].fixed_reads == (scratch ? REGALLOC_PHYS_R0 | REGALLOC_PHYS_R5 : 0));
    if (invert)
        assert(m[4].op == REGALLOC_M_BOOL_NOT && m[4].def == 2);

    LivenessResult *live = NULL;
    LivenessDiagnostic live_error;
    assert(liveness_analyze_machine(machine, &live, &live_error) == LIVENESS_OK);
    RegallocMachineGraph *graph = NULL;
    assert(regalloc_build_machine_graph(machine, live, 0, &graph, &error) ==
           REGALLOC_OK);
    assert((graph->forbidden_colors[0] & REGALLOC_PHYS_R5) == scratch);
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
}

int main(void)
{
    PiperType types[] = {PIPER_TYPE_B8, PIPER_TYPE_UB8,
                         PIPER_TYPE_B16, PIPER_TYPE_UB16,
                         PIPER_TYPE_B32, PIPER_TYPE_UB32};
    for (size_t left = 0; left < 6; left++)
        for (size_t right = 0; right < 6; right++) {
            check_order(IR_OP_LT, types[left], types[right], REGALLOC_M_LT, false);
            check_order(IR_OP_LE, types[left], types[right], REGALLOC_M_GT, true);
            check_order(IR_OP_GT, types[left], types[right], REGALLOC_M_GT, false);
            check_order(IR_OP_GE, types[left], types[right], REGALLOC_M_LT, true);
        }
    puts("comparaciones ordenadas R2b: ok");
    return 0;
}
