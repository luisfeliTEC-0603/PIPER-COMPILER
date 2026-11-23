#include "regalloc_machine.h"
#include <assert.h>
#include <stdio.h>

/* t1 y t2 cruzan la escritura de r1; t0 solo alimenta el retorno anterior.
 * Los tres estan vivos antes de esa escritura y deben interferir. */
static void test_live_pairs_and_fixed_write(void)
{
    RegallocMachineInstruction instructions[] = {
        {.op = REGALLOC_M_IMM, .def = 0, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_IMM, .def = 1, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_IMM, .def = 2, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_TO_RETURN, .def = IR_NO_ID, .use1 = 0,
         .fixed_writes = REGALLOC_PHYS_R1},
        {.op = REGALLOC_M_ADD, .def = 1, .use1 = 2},
        {.op = REGALLOC_M_TO_RETURN, .def = IR_NO_ID, .use1 = 1,
         .fixed_writes = REGALLOC_PHYS_R1},
        {.op = REGALLOC_M_RETURN, .def = IR_NO_ID, .use1 = IR_NO_ID,
         .fixed_reads = REGALLOC_PHYS_R1}
    };
    RegallocMachineBlock block = {.id = 7, .instructions = instructions,
                                  .instruction_count = 7};
    RegallocMachineRoutine routine = {.temp_count = 3, .blocks = &block,
                                      .block_count = 1};
    RegallocMachineProgram machine = {.routines = &routine, .routine_count = 1};
    LivenessResult *live = NULL;
    LivenessDiagnostic live_error;
    assert(liveness_analyze_machine(&machine, &live, &live_error) == LIVENESS_OK);

    RegallocMachineGraph *graph = NULL;
    RegallocDiagnostic error;
    assert(regalloc_build_machine_graph(&machine, live, 0, &graph, &error) ==
           REGALLOC_OK);
    assert(graph->temp_count == 3);
    assert(graph->degree[0] == 2 && graph->degree[1] == 2 &&
           graph->degree[2] == 2);
    assert(regalloc_machine_graph_has_edge(graph, 0, 1));
    assert(regalloc_machine_graph_has_edge(graph, 1, 0));
    assert(regalloc_machine_graph_has_edge(graph, 0, 2));
    assert(regalloc_machine_graph_has_edge(graph, 1, 2));
    assert(!regalloc_machine_graph_has_edge(graph, 0, 0));
    assert(graph->forbidden_colors[0] == 0);
    assert(graph->forbidden_colors[1] == REGALLOC_PHYS_R1);
    assert(graph->forbidden_colors[2] == REGALLOC_PHYS_R1);
    regalloc_machine_graph_free(graph);
    liveness_result_free(live);
}

/* El valor del contador y la condicion sobreviven a la vuelta del ciclo. */
static void test_loop_and_zero_temporaries(void)
{
    RegallocMachineInstruction entry[] = {
        {.op = REGALLOC_M_IMM, .def = 0, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_IMM, .def = 1, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_JUMP, .def = IR_NO_ID, .use1 = IR_NO_ID,
         .targets = {11, IR_NO_ID}}
    };
    RegallocMachineInstruction loop[] = {
        {.op = REGALLOC_M_ADDI, .def = 0, .use1 = IR_NO_ID, .immediate = 1},
        {.op = REGALLOC_M_BRANCH, .def = IR_NO_ID, .use1 = 1,
         .targets = {11, 18}}
    };
    RegallocMachineInstruction end[] = {
        {.op = REGALLOC_M_TO_RETURN, .def = IR_NO_ID, .use1 = 0,
         .fixed_writes = REGALLOC_PHYS_R1},
        {.op = REGALLOC_M_RETURN, .def = IR_NO_ID, .use1 = IR_NO_ID,
         .fixed_reads = REGALLOC_PHYS_R1}
    };
    RegallocMachineBlock blocks[] = {
        {.id = 7, .instructions = entry, .instruction_count = 3},
        {.id = 11, .instructions = loop, .instruction_count = 2},
        {.id = 18, .instructions = end, .instruction_count = 2}
    };
    RegallocMachineRoutine routine = {.temp_count = 2, .blocks = blocks,
                                      .block_count = 3};
    RegallocMachineProgram machine = {.routines = &routine, .routine_count = 1};
    LivenessResult *live = NULL;
    LivenessDiagnostic live_error;
    assert(liveness_analyze_machine(&machine, &live, &live_error) == LIVENESS_OK);
    RegallocMachineGraph *graph = NULL;
    RegallocDiagnostic error;
    assert(regalloc_build_machine_graph(&machine, live, 0, &graph, &error) ==
           REGALLOC_OK);
    assert(regalloc_machine_graph_has_edge(graph, 0, 1));
    assert(graph->degree[0] == 1 && graph->degree[1] == 1);
    regalloc_machine_graph_free(graph);
    liveness_result_free(live);

    RegallocMachineInstruction ret = {.op = REGALLOC_M_RETURN,
        .def = IR_NO_ID, .use1 = IR_NO_ID};
    RegallocMachineBlock empty = {.id = 4, .instructions = &ret,
                                  .instruction_count = 1};
    routine = (RegallocMachineRoutine){.blocks = &empty, .block_count = 1};
    live = NULL;
    assert(liveness_analyze_machine(&machine, &live, &live_error) == LIVENESS_OK);
    graph = NULL;
    assert(regalloc_build_machine_graph(&machine, live, 0, &graph, &error) ==
           REGALLOC_OK);
    assert(graph->temp_count == 0 && graph->neighbors == NULL);
    regalloc_machine_graph_free(graph);
    liveness_result_free(live);
}

/* Los IDs de maquina pueden ocupar mas de una palabra de la matriz de bits. */
static void test_high_temporal_id(void)
{
    RegallocMachineInstruction instructions[] = {
        {.op = REGALLOC_M_IMM, .def = 0, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_IMM, .def = 64, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_ADD, .def = 0, .use1 = 64},
        {.op = REGALLOC_M_TO_RETURN, .def = IR_NO_ID, .use1 = 0,
         .fixed_writes = REGALLOC_PHYS_R1},
        {.op = REGALLOC_M_RETURN, .def = IR_NO_ID, .use1 = IR_NO_ID,
         .fixed_reads = REGALLOC_PHYS_R1}
    };
    RegallocMachineBlock block = {.id = 2, .instructions = instructions,
                                  .instruction_count = 5};
    RegallocMachineRoutine routine = {.temp_count = 65, .blocks = &block,
                                      .block_count = 1};
    RegallocMachineProgram machine = {.routines = &routine, .routine_count = 1};
    LivenessResult *live = NULL;
    LivenessDiagnostic live_error;
    assert(liveness_analyze_machine(&machine, &live, &live_error) == LIVENESS_OK);
    RegallocMachineGraph *graph = NULL;
    RegallocDiagnostic error;
    assert(regalloc_build_machine_graph(&machine, live, 0, &graph, &error) ==
           REGALLOC_OK);
    assert(graph->word_count == 2);
    assert(regalloc_machine_graph_has_edge(graph, 0, 64));
    assert(regalloc_machine_graph_has_edge(graph, 64, 0));
    assert(graph->degree[0] == 1 && graph->degree[64] == 1);
    regalloc_machine_graph_free(graph);
    liveness_result_free(live);
}

int main(void)
{
    test_live_pairs_and_fixed_write();
    test_loop_and_zero_temporaries();
    test_high_temporal_id();
    puts("grafo virtual: ok");
    return 0;
}
