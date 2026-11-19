#include "regalloc_machine.h"
#include <assert.h>
#include <stdio.h>

/* Variacion en instrucciones de maquina del ejemplo a-b, a-c, sin b-c de
 * Cornell CS4120, Lecture 29: b muere al crear c; a vive durante ambos. */
static void test_cornell_live_ranges(void)
{
    RegallocMachineInstruction instructions[] = {
        {.op = REGALLOC_M_IMM, .def = 0, .use1 = IR_NO_ID}, /* a */
        {.op = REGALLOC_M_MOVE, .def = 1, .use1 = 0},      /* b = a */
        {.op = REGALLOC_M_MOVE, .def = 2, .use1 = 1},      /* c = b */
        {.op = REGALLOC_M_ADD, .def = 2, .use1 = 0},       /* c += a */
        {.op = REGALLOC_M_TO_RETURN, .def = IR_NO_ID, .use1 = 2,
         .fixed_writes = REGALLOC_PHYS_R1},
        {.op = REGALLOC_M_RETURN, .def = IR_NO_ID, .use1 = IR_NO_ID,
         .fixed_reads = REGALLOC_PHYS_R1}
    };
    RegallocMachineBlock block = {.id = 23, .instructions = instructions,
                                  .instruction_count = 6};
    RegallocMachineRoutine routine = {.temp_count = 3, .blocks = &block,
                                      .block_count = 1};
    RegallocMachineProgram machine = {.routines = &routine, .routine_count = 1};
    LivenessResult *live = NULL;
    LivenessDiagnostic live_error;
    assert(liveness_analyze_machine(&machine, &live, &live_error) == LIVENESS_OK);
    const LivenessSet *after_b = liveness_instruction_set(
        live, 0, 0, 1, LIVENESS_AFTER);
    const LivenessSet *after_c = liveness_instruction_set(
        live, 0, 0, 2, LIVENESS_AFTER);
    assert(liveness_set_contains(after_b, 0) &&
           liveness_set_contains(after_b, 1) &&
           !liveness_set_contains(after_b, 2));
    assert(liveness_set_contains(after_c, 0) &&
           !liveness_set_contains(after_c, 1) &&
           liveness_set_contains(after_c, 2));

    RegallocMachineGraph *graph = NULL;
    RegallocDiagnostic error;
    assert(regalloc_build_machine_graph(&machine, live, 0, &graph, &error) ==
           REGALLOC_OK);
    assert(graph->word_count == 1);
    assert(graph->neighbors[0] == ((1u << 1) | (1u << 2)));
    assert(graph->neighbors[1] == (1u << 0));
    assert(graph->neighbors[2] == (1u << 0));
    assert(graph->degree[0] == 2 && graph->degree[1] == 1 &&
           graph->degree[2] == 1);

    RegallocColoring *coloring = NULL;
    assert(regalloc_color_machine_graph(graph, &coloring, &error) == REGALLOC_OK);
    assert(coloring->temp_count == 3 && coloring->uncolored_count == 0);
    assert(coloring->steps[0].temp == 0 &&
           coloring->steps[0].active_degree == 2 &&
           !coloring->steps[0].potential_spill);
    assert(coloring->steps[1].temp == 1 &&
           coloring->steps[1].active_degree == 0);
    assert(coloring->steps[2].temp == 2 &&
           coloring->steps[2].active_degree == 0);
    assert(coloring->colors[1] == coloring->colors[2]);
    assert(coloring->colors[0] != coloring->colors[1]);
    regalloc_coloring_free(coloring);
    regalloc_machine_graph_free(graph);
    liveness_result_free(live);
}

/* Construccion independiente de grafos pequenos para probar la pila: no usa
 * liveness ni el constructor de interferencias. Cada fila cabe en uint64_t. */
static void add_test_edge(uint64_t *rows, size_t *degree, size_t a, size_t b)
{
    rows[a] |= UINT64_C(1) << b;
    rows[b] |= UINT64_C(1) << a;
    degree[a]++;
    degree[b]++;
}

static RegallocMachineGraph test_graph(size_t count, uint64_t *rows,
                                       size_t *degree, unsigned *forbidden)
{
    return (RegallocMachineGraph){
        .temp_count = count, .word_count = 1, .neighbors = rows,
        .degree = degree, .forbidden_colors = forbidden
    };
}

static void test_optimistic_candidate_and_real_failure(void)
{
    uint64_t rows[8] = {0};
    size_t degree[8] = {0};
    unsigned forbidden[8] = {0};
    for (size_t a = 0; a < 4; a++)
        for (size_t b = 4; b < 8; b++) add_test_edge(rows, degree, a, b);
    RegallocMachineGraph graph = test_graph(8, rows, degree, forbidden);
    RegallocColoring *coloring = NULL;
    RegallocDiagnostic error;
    assert(regalloc_color_machine_graph(&graph, &coloring, &error) == REGALLOC_OK);
    assert(coloring->steps[0].temp == 0 &&
           coloring->steps[0].active_degree == 4 &&
           coloring->steps[0].potential_spill);
    assert(coloring->uncolored_count == 0); /* K4,4 es bipartito. */
    for (size_t a = 0; a < 4; a++)
        for (size_t b = 4; b < 8; b++)
            assert(coloring->colors[a] != coloring->colors[b]);
    regalloc_coloring_free(coloring);

    uint64_t clique_rows[5] = {0};
    size_t clique_degree[5] = {0};
    unsigned clique_forbidden[5] = {0};
    for (size_t a = 0; a < 5; a++)
        for (size_t b = a + 1; b < 5; b++)
            add_test_edge(clique_rows, clique_degree, a, b);
    graph = test_graph(5, clique_rows, clique_degree, clique_forbidden);
    coloring = NULL;
    assert(regalloc_color_machine_graph(&graph, &coloring, &error) == REGALLOC_OK);
    assert(coloring->steps[0].temp == 0 &&
           coloring->steps[0].potential_spill);
    assert(coloring->uncolored_count == 1 && coloring->colors[0] == 0);
    regalloc_coloring_free(coloring);
}

static void test_forbidden_colors_change_simplification(void)
{
    uint64_t rows[3] = {0};
    size_t degree[3] = {0};
    unsigned forbidden[3] = {
        REGALLOC_PHYS_R1 | REGALLOC_PHYS_R2, 0, 0
    };
    add_test_edge(rows, degree, 0, 1);
    add_test_edge(rows, degree, 0, 2);
    add_test_edge(rows, degree, 1, 2);
    RegallocMachineGraph graph = test_graph(3, rows, degree, forbidden);
    RegallocColoring *coloring = NULL;
    RegallocDiagnostic error;
    assert(regalloc_color_machine_graph(&graph, &coloring, &error) == REGALLOC_OK);
    assert(coloring->steps[0].temp == 1); /* t0: grado 2, solo dos colores. */
    assert(coloring->steps[1].temp == 0 &&
           coloring->steps[1].active_degree == 1 &&
           coloring->steps[1].eligible_colors == 2);
    assert(coloring->uncolored_count == 0);
    assert(coloring->colors[0] == 3 || coloring->colors[0] == 4);
    regalloc_coloring_free(coloring);
}

/* Presentacion del proyecto, diapositivas 21-22: K=3, p-q, p-r, q-r, q-s,
 * r-s. r4 se prohibe para reproducir el ejercicio con el pool fisico K=4. */
static void test_presentation_exercise_one(void)
{
    uint64_t rows[4] = {0};
    size_t degree[4] = {0};
    unsigned forbidden[4] = {
        REGALLOC_PHYS_R4, REGALLOC_PHYS_R4,
        REGALLOC_PHYS_R4, REGALLOC_PHYS_R4
    };
    add_test_edge(rows, degree, 0, 1); /* p-q */
    add_test_edge(rows, degree, 0, 2); /* p-r */
    add_test_edge(rows, degree, 1, 2); /* q-r */
    add_test_edge(rows, degree, 1, 3); /* q-s */
    add_test_edge(rows, degree, 2, 3); /* r-s */
    RegallocMachineGraph graph = test_graph(4, rows, degree, forbidden);
    RegallocColoring *coloring = NULL;
    RegallocDiagnostic error;
    assert(regalloc_color_machine_graph(&graph, &coloring, &error) == REGALLOC_OK);
    assert(coloring->uncolored_count == 0);
    assert(coloring->steps[0].temp == 0 && coloring->steps[0].active_degree == 2);
    /* La diapositiva desempata con s; el algoritmo desempata por ID y
     * produce p,q,r,s. Ambos ordenes son simplificaciones correctas. */
    assert(coloring->steps[1].temp == 1 && coloring->steps[1].active_degree == 2);
    assert(coloring->steps[2].temp == 2 && coloring->steps[2].active_degree == 1);
    assert(coloring->steps[3].temp == 3 && coloring->steps[3].active_degree == 0);
    for (size_t i = 0; i < 4; i++)
        assert(!coloring->steps[i].potential_spill &&
               coloring->steps[i].eligible_colors == 3);
    assert(coloring->colors[3] == 1); /* s = R1 */
    assert(coloring->colors[2] == 2); /* r = R2 */
    assert(coloring->colors[1] == 3); /* q = R3 */
    assert(coloring->colors[0] == 1); /* p y s pueden compartir R1. */
    regalloc_coloring_free(coloring);
}

/* Presentacion, diapositivas 10-13: e tiene grado 2 y el resto forma K4.
 * La seleccion de victima por costo aun falta; el desempate actual elige a. */
static void test_presentation_stuck_graph(void)
{
    uint64_t rows[5] = {0};
    size_t degree[5] = {0};
    unsigned forbidden[5] = {
        REGALLOC_PHYS_R4, REGALLOC_PHYS_R4, REGALLOC_PHYS_R4,
        REGALLOC_PHYS_R4, REGALLOC_PHYS_R4
    };
    for (size_t a = 0; a < 4; a++)
        for (size_t b = a + 1; b < 4; b++)
            add_test_edge(rows, degree, a, b);
    add_test_edge(rows, degree, 0, 4);
    add_test_edge(rows, degree, 1, 4);
    RegallocMachineGraph graph = test_graph(5, rows, degree, forbidden);
    RegallocColoring *coloring = NULL;
    RegallocDiagnostic error;
    assert(regalloc_color_machine_graph(&graph, &coloring, &error) == REGALLOC_OK);
    assert(coloring->steps[0].temp == 4 &&
           coloring->steps[0].active_degree == 2 &&
           !coloring->steps[0].potential_spill);
    assert(coloring->steps[1].temp == 0 &&
           coloring->steps[1].active_degree == 3 &&
           coloring->steps[1].potential_spill);
    assert(coloring->uncolored_count == 1 && coloring->colors[0] == 0);
    assert(coloring->colors[4] != 0);
    regalloc_coloring_free(coloring);
}

static void reference_live_pairs(unsigned live, uint64_t *rows)
{
    for (size_t a = 0; a < 3; a++)
        for (size_t b = a + 1; b < 3; b++)
            if ((live & (1u << a)) != 0 && (live & (1u << b)) != 0) {
                rows[a] |= UINT64_C(1) << b;
                rows[b] |= UINT64_C(1) << a;
            }
}

/* Oraculo pequeno independiente del solver: transferencia hacia atras sobre
 * tres bits. Recorre 243 combinaciones de dos ALU y un valor de retorno. */
static void test_machine_graph_against_reference(void)
{
    for (unsigned pattern = 0; pattern < 243; pattern++) {
        unsigned choice[5];
        unsigned remaining = pattern;
        for (size_t i = 0; i < 5; i++) {
            choice[i] = remaining % 3;
            remaining /= 3;
        }
        RegallocMachineInstruction instructions[] = {
            {.op = REGALLOC_M_IMM, .def = 0, .use1 = IR_NO_ID},
            {.op = REGALLOC_M_IMM, .def = 1, .use1 = IR_NO_ID},
            {.op = REGALLOC_M_IMM, .def = 2, .use1 = IR_NO_ID},
            {.op = REGALLOC_M_ADD, .def = choice[0], .use1 = choice[1]},
            {.op = REGALLOC_M_TO_RETURN, .def = IR_NO_ID, .use1 = choice[4],
             .fixed_writes = REGALLOC_PHYS_R1},
            {.op = REGALLOC_M_ADD, .def = choice[2], .use1 = choice[3]},
            {.op = REGALLOC_M_TO_RETURN, .def = IR_NO_ID, .use1 = choice[4],
             .fixed_writes = REGALLOC_PHYS_R1},
            {.op = REGALLOC_M_RETURN, .def = IR_NO_ID, .use1 = IR_NO_ID,
             .fixed_reads = REGALLOC_PHYS_R1}
        };
        RegallocMachineBlock block = {.id = 11, .instructions = instructions,
                                      .instruction_count = 8};
        RegallocMachineRoutine routine = {.temp_count = 3, .blocks = &block,
                                          .block_count = 1};
        RegallocMachineProgram machine = {
            .routines = &routine, .routine_count = 1
        };
        LivenessResult *live_result = NULL;
        LivenessDiagnostic live_error;
        assert(liveness_analyze_machine(&machine, &live_result, &live_error) ==
               LIVENESS_OK);
        RegallocMachineGraph *graph = NULL;
        RegallocDiagnostic error;
        assert(regalloc_build_machine_graph(&machine, live_result, 0, &graph,
                                            &error) == REGALLOC_OK);

        unsigned live = 0;
        uint64_t expected_rows[3] = {0};
        unsigned expected_forbidden[3] = {0};
        for (size_t i = 8; i != 0; i--) {
            const RegallocMachineInstruction *in = &instructions[i - 1];
            unsigned after = live;
            unsigned def = 0, use = 0;
            if (in->op == REGALLOC_M_IMM) def = 1u << in->def;
            if (in->op == REGALLOC_M_ADD) {
                def = 1u << in->def;
                use = (1u << in->def) | (1u << in->use1);
            }
            if (in->op == REGALLOC_M_TO_RETURN) use = 1u << in->use1;
            unsigned before = (after & ~def) | use;
            reference_live_pairs(before, expected_rows);
            reference_live_pairs(after, expected_rows);
            if (in->fixed_writes != 0)
                for (size_t t = 0; t < 3; t++)
                    if ((before & after & (1u << t)) != 0)
                        expected_forbidden[t] |= in->fixed_writes;
            live = before;
        }
        for (size_t t = 0; t < 3; t++) {
            assert(graph->neighbors[t] == expected_rows[t]);
            assert(graph->forbidden_colors[t] == expected_forbidden[t]);
            size_t expected_degree = 0;
            for (size_t other = 0; other < 3; other++)
                if ((expected_rows[t] & (UINT64_C(1) << other)) != 0)
                    expected_degree++;
            assert(graph->degree[t] == expected_degree);
        }
        regalloc_machine_graph_free(graph);
        liveness_result_free(live_result);
    }
}

/* Los 2^10 grafos simples de cinco nodos: el oraculo recompone grados activos
 * desde filas independientes y comprueba cada retiro, cada color y K5. */
static void test_all_five_node_graphs(void)
{
    for (unsigned mask = 0; mask < (1u << 10); mask++) {
        uint64_t rows[5] = {0};
        size_t degree[5] = {0};
        unsigned forbidden[5] = {0};
        unsigned bit = 0;
        for (size_t a = 0; a < 5; a++)
            for (size_t b = a + 1; b < 5; b++, bit++)
                if ((mask & (1u << bit)) != 0)
                    add_test_edge(rows, degree, a, b);
        RegallocMachineGraph graph = test_graph(5, rows, degree, forbidden);
        RegallocColoring *coloring = NULL;
        RegallocDiagnostic error;
        assert(regalloc_color_machine_graph(&graph, &coloring, &error) ==
               REGALLOC_OK);
        bool active[5] = {true, true, true, true, true};
        for (size_t step = 0; step < 5; step++) {
            size_t chosen = coloring->steps[step].temp;
            bool has_simple = false;
            size_t active_degree = 0;
            for (size_t t = 0; t < 5; t++) {
                if (!active[t]) continue;
                size_t neighbors = 0;
                for (size_t u = 0; u < 5; u++)
                    if (active[u] && (rows[t] & (UINT64_C(1) << u)) != 0)
                        neighbors++;
                if (neighbors < REGALLOC_COLOR_COUNT) has_simple = true;
                if (t == chosen) active_degree = neighbors;
            }
            assert(active[chosen]);
            assert(coloring->steps[step].active_degree == active_degree);
            assert(coloring->steps[step].eligible_colors == REGALLOC_COLOR_COUNT);
            assert(coloring->steps[step].potential_spill == !has_simple);
            if (has_simple) assert(active_degree < REGALLOC_COLOR_COUNT);
            active[chosen] = false;
        }
        assert(coloring->uncolored_count == (mask == (1u << 10) - 1));
        for (size_t a = 0; a < 5; a++) {
            assert(coloring->colors[a] <= REGALLOC_COLOR_COUNT);
            for (size_t b = a + 1; b < 5; b++)
                if ((rows[a] & (UINT64_C(1) << b)) != 0 &&
                    coloring->colors[a] != 0 && coloring->colors[b] != 0)
                    assert(coloring->colors[a] != coloring->colors[b]);
        }
        regalloc_coloring_free(coloring);
    }
}

static void test_empty_and_second_bit_word(void)
{
    RegallocMachineGraph empty = {0};
    RegallocColoring *coloring = NULL;
    RegallocDiagnostic error;
    assert(regalloc_color_machine_graph(&empty, &coloring, &error) == REGALLOC_OK);
    assert(coloring->temp_count == 0 && coloring->steps == NULL &&
           coloring->colors == NULL && coloring->uncolored_count == 0);
    regalloc_coloring_free(coloring);

    uint64_t rows[65 * 2] = {0};
    size_t degree[65] = {0};
    unsigned forbidden[65] = {0};
    rows[0 * 2 + 1] = 1u; /* 0 -- 64, en la segunda palabra. */
    rows[64 * 2] = 1u;
    degree[0] = degree[64] = 1;
    RegallocMachineGraph graph = {
        .temp_count = 65, .word_count = 2, .neighbors = rows,
        .degree = degree, .forbidden_colors = forbidden
    };
    coloring = NULL;
    assert(regalloc_color_machine_graph(&graph, &coloring, &error) == REGALLOC_OK);
    assert(coloring->uncolored_count == 0);
    assert(coloring->colors[0] != coloring->colors[64]);
    regalloc_coloring_free(coloring);
}

int main(void)
{
    test_cornell_live_ranges();
    test_optimistic_candidate_and_real_failure();
    test_forbidden_colors_change_simplification();
    test_presentation_exercise_one();
    test_presentation_stuck_graph();
    test_machine_graph_against_reference();
    test_all_five_node_graphs();
    test_empty_and_second_bit_word();
    puts("grafo y pila de simplificacion: ok");
    return 0;
}
