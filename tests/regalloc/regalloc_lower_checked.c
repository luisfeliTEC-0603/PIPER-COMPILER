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

/* El valor de t0 se usa despues de la operacion: el grafo debe registrar
 * las escrituras fijas de r5/r6 cuando el chequeo esta presente. */
static void check_binary(IrOpcode op, RegallocMachineOp plain,
                         RegallocMachineOp checked_op, PiperType type,
                         bool checked)
{
    IrInstruction instructions[] = {
        {.op = IR_OP_COPY, .result = temp(0, type),
         .arg1 = constant(2, type)},
        {.op = IR_OP_COPY, .result = temp(1, type),
         .arg1 = constant(3, type)},
        {.op = op, .result = temp(2, type),
         .arg1 = temp(0, type), .arg2 = temp(1, type),
         .flags = checked ? IR_CHECKED | IR_MAY_TRAP : 0},
        {.op = IR_OP_COPY, .result = temp(3, type), .arg1 = temp(0, type)},
        {.op = IR_OP_RETURN, .arg1 = temp(2, type)}
    };
    IrBlock block = {.id = 6, .instructions = instructions,
                     .instruction_count = 5};
    IrRoutine routine = {.blocks = &block, .block_count = 1, .temp_count = 4};
    SymbolTable symbols = {0};
    IrProgram ir = {.symbols = &symbols, .routines = &routine,
                    .routine_count = 1};
    CfgProgram cfg = {.ir = &ir};
    RegallocBlock mapped_block = {.id = 6, .cfg_node = 1};
    RegallocRoutine mapped = {.blocks = &mapped_block, .block_count = 1};
    RegallocAnalysis analysis = {.ir = &ir, .cfg = &cfg,
        .routines = &mapped, .routine_count = 1};
    RegallocDiagnostic error;
    RegallocMachineProgram *machine = NULL;
    assert(regalloc_lower_scalar(&analysis, &machine, &error) == REGALLOC_OK);
    const RegallocMachineBlock *lowered = &machine->routines[0].blocks[0];
    const RegallocMachineInstruction *m = lowered->instructions;
    assert(lowered->instruction_count == 7);
    assert(m[2].op == REGALLOC_M_MOVE && m[2].def == 2 && m[2].use1 == 0);
    assert(m[3].op == (checked ? checked_op : plain));
    assert(m[3].def == 2 && m[3].use1 == 1 && m[3].type.piper == type);
    unsigned scratch = checked ? REGALLOC_PHYS_R5 | REGALLOC_PHYS_R6 : 0;
    assert(m[3].fixed_reads == scratch && m[3].fixed_writes == scratch);
    assert(m[3].flags == (checked ? IR_CHECKED | IR_MAY_TRAP : 0));

    LivenessResult *live = NULL;
    LivenessDiagnostic live_error;
    assert(liveness_analyze_machine(machine, &live, &live_error) == LIVENESS_OK);
    RegallocMachineGraph *graph = NULL;
    assert(regalloc_build_machine_graph(machine, live, 0, &graph, &error) ==
           REGALLOC_OK);
    assert((graph->forbidden_colors[0] &
            (REGALLOC_PHYS_R5 | REGALLOC_PHYS_R6)) == scratch);
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

    /* MAY_TRAP sin CHECKED no tiene implementacion aritmetica equivalente. */
    instructions[2].flags = IR_MAY_TRAP;
    machine = NULL;
    assert(regalloc_lower_scalar(&analysis, &machine, &error) ==
           REGALLOC_UNSUPPORTED);
    assert(machine == NULL);

    /* CHECKED por si solo obliga a conservar la posibilidad de error. */
    instructions[2].flags = IR_CHECKED;
    assert(regalloc_lower_scalar(&analysis, &machine, &error) == REGALLOC_OK);
    assert(machine->routines[0].blocks[0].instructions[3].op == checked_op);
    assert((machine->routines[0].blocks[0].instructions[3].flags &
            IR_MAY_TRAP) != 0);
    regalloc_machine_program_free(machine);
}

int main(void)
{
    PiperType types[] = {PIPER_TYPE_B8, PIPER_TYPE_UB8,
                         PIPER_TYPE_B16, PIPER_TYPE_UB16,
                         PIPER_TYPE_B32, PIPER_TYPE_UB32};
    for (size_t t = 0; t < 6; t++)
        for (unsigned checked = 0; checked < 2; checked++) {
            check_binary(IR_OP_ADD, REGALLOC_M_ADD, REGALLOC_M_CHECKED_ADD,
                         types[t], checked != 0);
            check_binary(IR_OP_SUB, REGALLOC_M_SUB, REGALLOC_M_CHECKED_SUB,
                         types[t], checked != 0);
            check_binary(IR_OP_MUL, REGALLOC_M_MUL, REGALLOC_M_CHECKED_MUL,
                         types[t], checked != 0);
        }
    puts("aritmetica comprobada R2b: ok");
    return 0;
}
