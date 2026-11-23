#include "regalloc_machine.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LENGTH(array) (sizeof(array) / sizeof((array)[0]))

/* Los mapas de entrada son coloreos completos. Validarlos a ambos lados
 * permite distinguir un rechazo por interferencia de un empate de costo. */
static void check(RegallocMachineBlock *blocks, size_t block_count,
                  uint8_t *colors, const uint8_t *expected, size_t count,
                  size_t changes, size_t operations, IrId only_color)
{
    RegallocMachineRoutine routine = {.temp_count = count, .blocks = blocks,
                                      .block_count = block_count};
    SymbolTable symbols = {0};
    IrRoutine source = {0};
    IrProgram ir = {.symbols = &symbols, .routines = &source, .routine_count = 1};
    RegallocMachineProgram machine = {.source = &ir, .routines = &routine, .routine_count = 1};
    RegallocAssignedRoutine map = {.register_for_temp = colors};
    RegallocAssignedProgram assigned = {.machine = &machine, .routines = &map, .routine_count = 1};
    RegallocDiagnostic error;
    assert(regalloc_assigned_program_validate(&assigned, &error) == REGALLOC_OK);

    LivenessResult *live = NULL;
    RegallocMachineGraph *graph = NULL;
    assert(liveness_analyze_machine(&machine, &live, NULL) == LIVENESS_OK);
    assert(regalloc_build_machine_graph(&machine, live, 0, &graph, &error) == REGALLOC_OK);
    if (only_color != IR_NO_ID)
        assert(regalloc_available_colors(graph, only_color, colors) == (1u << colors[only_color]));

    RegallocRoutineReport report = {0};
    assert(regalloc_rename_locals(&routine, live, graph, colors, &report));
    assert(memcmp(colors, expected, count) == 0);
    assert(report.rename_count == changes && report.affected_operations == operations);
    assert(regalloc_assigned_program_validate(&assigned, &error) == REGALLOC_OK);
    free((void *)report.renames);
    regalloc_machine_graph_free(graph);
    liveness_result_free(live);
}

static void no_alternative(void)
{
    /* Ningun operando de B27 menciona r2..r4, pero esos tres valores viven
     * a traves de B27. Solo el grafo impide usarlos para quitar la WAW. */
    RegallocMachineInstruction entry[] = {
        {.op = REGALLOC_M_IMM, .def = 2, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_IMM, .def = 3, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_IMM, .def = 4, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_JUMP, .def = IR_NO_ID, .use1 = IR_NO_ID, .targets = {27}}
    };
    RegallocMachineInstruction middle[] = {
        {.op = REGALLOC_M_IMM, .def = 0, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_IMM, .def = 1, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_ADDI, .def = 1, .use1 = IR_NO_ID, .immediate = 1},
        {.op = REGALLOC_M_JUMP, .def = IR_NO_ID, .use1 = IR_NO_ID, .targets = {41}}
    };
    RegallocMachineInstruction last[] = {
        {.op = REGALLOC_M_STORE_INDIRECT, .def = IR_NO_ID, .use1 = 2, .use2 = 3},
        {.op = REGALLOC_M_STORE_SYMBOL, .def = IR_NO_ID, .use1 = 4},
        {.op = REGALLOC_M_RETURN, .def = IR_NO_ID, .use1 = IR_NO_ID}
    };
    RegallocMachineBlock blocks[] = {
        {.id = 7, .instructions = entry, .instruction_count = LENGTH(entry)},
        {.id = 27, .instructions = middle, .instruction_count = LENGTH(middle)},
        {.id = 41, .instructions = last, .instruction_count = LENGTH(last)}
    };
    uint8_t colors[] = {1, 1, 2, 3, 4};
    check(blocks, LENGTH(blocks), colors, (uint8_t[]){1, 1, 2, 3, 4}, LENGTH(colors), 0, 0, 1);
}

static void fixed_argument(void)
{
    /* t0 cruza la escritura de r1. r3 pertenece a su vecino t1. La WAW
     * posterior con t2 en r2 solo puede eliminarse moviendo t0 a r4. */
    RegallocMachineInstruction ins[] = {
        {.op = REGALLOC_M_IMM, .def = 0, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_IMM, .def = 1, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_CALL_SAVE, .def = IR_NO_ID, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_CALL_ARG, .def = IR_NO_ID, .use1 = 1,
         .call_arg_index = 0, .fixed_writes = REGALLOC_PHYS_R1},
        {.op = REGALLOC_M_CALL_VOID, .def = IR_NO_ID, .use1 = IR_NO_ID, .fixed_reads = REGALLOC_PHYS_R1},
        {.op = REGALLOC_M_STORE_SYMBOL, .def = IR_NO_ID, .use1 = 0},
        {.op = REGALLOC_M_IMM, .def = 2, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_RETURN, .def = IR_NO_ID, .use1 = IR_NO_ID}
    };
    RegallocMachineBlock block = {.id = 5, .instructions = ins, .instruction_count = LENGTH(ins)};
    uint8_t colors[] = {2, 3, 2};
    check(&block, 1, colors, (uint8_t[]){4, 3, 2}, LENGTH(colors), 1, 2, IR_NO_ID);
}

static void count_shared_operations(void)
{
    RegallocMachineInstruction ins[] = {
        {.op = REGALLOC_M_IMM, .def = 0, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_IMM, .def = 1, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_ADD, .def = 0, .use1 = 1},
        {.op = REGALLOC_M_STORE_INDIRECT, .def = IR_NO_ID, .use1 = 0, .use2 = 1},
        {.op = REGALLOC_M_IMM, .def = 2, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_IMM, .def = 3, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_STORE_INDIRECT, .def = IR_NO_ID, .use1 = 2, .use2 = 3},
        {.op = REGALLOC_M_RETURN, .def = IR_NO_ID, .use1 = IR_NO_ID}
    };
    RegallocMachineBlock block = {.id = 9, .instructions = ins, .instruction_count = LENGTH(ins)};
    uint8_t colors[] = {1, 2, 1, 2};
    check(&block, 1, colors, (uint8_t[]){3, 4, 1, 2}, LENGTH(colors), 2, 4, IR_NO_ID);
}

static void keep_elided_moves(void)
{
    RegallocMachineInstruction ins[] = {
        {.op = REGALLOC_M_IMM, .def = 0, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_MOVE, .def = 1, .use1 = 0},
        {.op = REGALLOC_M_STORE_SYMBOL, .def = IR_NO_ID, .use1 = 1},
        {.op = REGALLOC_M_IMM, .def = 2, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_TO_RETURN, .def = IR_NO_ID, .use1 = 2, .fixed_writes = REGALLOC_PHYS_R1},
        {.op = REGALLOC_M_RETURN, .def = IR_NO_ID, .use1 = IR_NO_ID, .fixed_reads = REGALLOC_PHYS_R1}
    };
    RegallocMachineBlock block = {.id = 5, .instructions = ins, .instruction_count = LENGTH(ins)};
    uint8_t colors[] = {1, 1, 1};
    check(&block, 1, colors, (uint8_t[]){1, 1, 1}, LENGTH(colors), 0, 0, IR_NO_ID);
}

static void keep_nonlocal_values(void)
{
    /* t0 se redefine en dos bloques aunque muera en las dos fronteras.
     * t1 se menciona solo en B42, pero su ADDI lee el valor de la vuelta anterior. */
    RegallocMachineInstruction entry[] = {
        {.op = REGALLOC_M_IMM, .def = 0, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_IMM, .def = 2, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_JUMP, .def = IR_NO_ID, .use1 = IR_NO_ID, .targets = {42}}
    };
    RegallocMachineInstruction loop[] = {
        {.op = REGALLOC_M_IMM, .def = 0, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_ADDI, .def = 1, .use1 = IR_NO_ID, .immediate = 1},
        {.op = REGALLOC_M_IMM, .def = 3, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_IMM, .def = 1, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_JUMP, .def = IR_NO_ID, .use1 = IR_NO_ID, .targets = {42}}
    };
    RegallocMachineBlock blocks[] = {
        {.id = 9, .instructions = entry, .instruction_count = LENGTH(entry)},
        {.id = 42, .instructions = loop, .instruction_count = LENGTH(loop)}
    };
    uint8_t colors[] = {1, 2, 1, 2};
    check(blocks, LENGTH(blocks), colors, (uint8_t[]){1, 2, 3, 3},
          LENGTH(colors), 2, 2, IR_NO_ID);
}

int main(void)
{
    no_alternative();
    fixed_argument();
    count_shared_operations();
    keep_elided_moves();
    keep_nonlocal_values();
    puts("PASS rename: color unico, argumento fijo, conteo sin duplicados, mov omitido y fronteras");
    return 0;
}
