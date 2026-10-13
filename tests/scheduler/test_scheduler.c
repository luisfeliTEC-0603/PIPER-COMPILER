#include "porygon_assembler/asm_program_parser.h"
#include "porygon_assembler/encoder.h"
#include "scheduler/scheduler.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/** Programa y calendario de una prueba, con propiedad local. */
typedef struct {
    porygon_machine_program_t program;
    porygon_schedule_t schedule;
} fixture_t;

/** Construye la entrada mediante el mismo parser que usa el compilador. */
static fixture_t schedule_text(const char *source)
{
    fixture_t f = {0};
    porygon_schedule_result_t result;
    assert(porygon_parse_program(source, &f.program).status == PORYGON_PROGRAM_OK);
    result = porygon_schedule_program(&f.program, &f.schedule);
    if (result.status != PORYGON_SCHEDULE_OK) {
        fprintf(stderr, "Fallo: %s (instrucción %zu)\n", result.message, result.instruction_index);
        abort();
    }
    assert(porygon_verify_schedule(&f.program, &f.schedule).status == PORYGON_SCHEDULE_OK);
    return f;
}

/** Libera ambos niveles del programa. */
static void destroy(fixture_t *f)
{
    porygon_schedule_free(&f->schedule);
    porygon_machine_program_free(&f->program);
}

/** Busca el ciclo real de una instrucción de la entrada. */
static size_t cycle_of(const fixture_t *f, size_t instruction)
{
    size_t b, s;
    for (b = 0; b < f->schedule.bundle_count; ++b) {
        for (s = 0; s < 4; ++s) {
            const porygon_scheduled_slot_t *slot = &f->schedule.bundles[b].slots[s];
            if (slot->kind == PORYGON_SLOT_INSTRUCTION && slot->instruction_index == instruction) return b;
        }
    }
    abort();
}

/** Devuelve un slot para inyectar errores en el verificador. */
static porygon_scheduled_slot_t *slot_of(fixture_t *f, size_t instruction)
{
    size_t b = cycle_of(f, instruction), s;
    for (s = 0; s < 4; ++s) {
        porygon_scheduled_slot_t *slot = &f->schedule.bundles[b].slots[s];
        if (slot->kind == PORYGON_SLOT_INSTRUCTION && slot->instruction_index == instruction) return slot;
    }
    abort();
}

/** RAW respeta +3 y +5, mientras instrucciones independientes ocupan ambas ALUs. */
static void test_dependencies(void)
{
    fixture_t f = schedule_text("sumi r1, #1\nsumi r2, #2\nsum r1, r2\n");
    assert(f.schedule.bundle_count == 4);
    assert(cycle_of(&f, 0) == 0 && cycle_of(&f, 1) == 0 && cycle_of(&f, 2) == 3);
    assert(slot_of(&f, 0)->pipe == 0 && slot_of(&f, 1)->pipe == 1);
    assert(f.program.instructions[0].state_defs == 0);
    destroy(&f);
    f = schedule_text("mul r1, r2\nsum r3, r1\n");
    assert(cycle_of(&f, 1) == 5);
    destroy(&f);
    f = schedule_text("mov r3, r1\nmov r1, r2\n");
    assert(cycle_of(&f, 0) == cycle_of(&f, 1));
    destroy(&f);
    f = schedule_text("mul r1, r2\nmov r1, r3\n");
    assert(cycle_of(&f, 1) == 3);
    destroy(&f);
    f = schedule_text("mov r0, r1\nsum r2, r0\n");
    assert(cycle_of(&f, 0) == cycle_of(&f, 1));
    destroy(&f);
}

/** MUL y shifts comparten emisión y canal, aunque sus destinos sean diferentes. */
static void test_alu1(void)
{
    fixture_t f = schedule_text("mul r1, r7\nshl r2, r7\nshl r3, r7\n");
    assert(cycle_of(&f, 0) == 0 && cycle_of(&f, 1) == 1 && cycle_of(&f, 2) == 3);
    destroy(&f);
    f = schedule_text("mul r1, r7\nmul r2, r7\nmul r3, r7\n");
    assert(cycle_of(&f, 0) == 0 && cycle_of(&f, 1) == 1 && cycle_of(&f, 2) == 2);
    destroy(&f);
}

/** LSU conserva alias potenciales y CRYPTO conserva orden sin exigir AUTH estático. */
static void test_memory_crypto(void)
{
    fixture_t f = schedule_text("low r1, 0(r7)\nstw r2, 0(r7)\nsum r3, r1\n");
    assert(cycle_of(&f, 1) == 1 && cycle_of(&f, 2) == 3);
    destroy(&f);
    f = schedule_text("low r1, 0(r7)\nstw r1, 0(r7)\n");
    assert(cycle_of(&f, 1) == 3);
    destroy(&f);
    f = schedule_text("write r1, 0, 0\nfeistel r2, r3, 0, 0\nfeistel r2, r3, 1, 0\nrvk\n");
    assert(cycle_of(&f, 1) == 1 && cycle_of(&f, 2) == 4 && cycle_of(&f, 3) == 5);
    destroy(&f);
    f = schedule_text("sum r1, r7\nshl r2, r7\nlow r3, 0(r7)\nfeistel r4, r5, 0, 0\n");
    assert(f.schedule.bundle_count == 1);
    destroy(&f);
    f = schedule_text("feistel r0, r0, 0, 0\n");
    destroy(&f);
}

/** Rechaza errores de PORYGON sin publicar salidas parciales. */
static void expect_error(const char *source, porygon_schedule_status_t expected)
{
    fixture_t f = {0};
    assert(porygon_parse_program(source, &f.program).status == PORYGON_PROGRAM_OK);
    assert(porygon_schedule_program(&f.program, &f.schedule).status == expected);
    assert(f.schedule.bundles == NULL && f.schedule.bundle_count == 0);
    destroy(&f);
}

/** Comprueba FEISTEL y constantes de direcciones con semántica de 32 bits. */
static void test_validation(void)
{
    expect_error("feistel r1, r1, 0, 0", PORYGON_SCHEDULE_INVALID_INSTRUCTION);
    expect_error("low r1, 0(r0)", PORYGON_SCHEDULE_INVALID_ADDRESS);
    expect_error("mov r1, r0\nsumi r1, #1\njr r1", PORYGON_SCHEDULE_INVALID_ADDRESS);
    expect_error("mov r1, r0\nori r1, #65535\nshli r1, #1\nlow r2, 0(r1)",
                 PORYGON_SCHEDULE_INVALID_ADDRESS);
    {
        fixture_t f = schedule_text("mov r1, r0\nsumi r1, #1\nshli r1, #16\nlow r2, 0(r1)");
        destroy(&f);
    }
}

/** Los destinos son bundles definitivos y cada salto cubre ambos caminos con dos NOPs. */
static void test_control(void)
{
    fixture_t f = schedule_text("start: mul r1, r2\nj end\nend: sum r3, r1\njz r3, start\n");
    size_t jump = cycle_of(&f, 1), last = cycle_of(&f, 3), b, s;
    assert(jump == 2);
    assert(f.schedule.label_bundles[1] == jump + 3);
    assert(cycle_of(&f, 2) == 5);
    for (b = jump + 1; b <= jump + 2; ++b) {
        for (s = 0; s < 4; ++s) assert(f.schedule.bundles[b].slots[s].word == 0);
    }
    assert(f.schedule.bundles[last].slots[1].word == (uint16_t)(-(int)last));
    assert(f.schedule.bundle_count == last + 3);
    destroy(&f);
    f = schedule_text("mul r1, r2\nnext: sum r3, r1\n");
    assert(f.schedule.label_bundles[0] == 1 && cycle_of(&f, 1) == 5);
    destroy(&f);
    f = schedule_text("mul r1, r2\njr r7\n");
    assert(cycle_of(&f, 1) == 2 && f.schedule.bundle_count == 5);
    destroy(&f);
}

/** Inyecta errores que un verificador limitado a repetir el scheduler no detectaría. */
static void test_verifier(void)
{
    fixture_t f = schedule_text("sumi r1, #0\nsum r2, r1\n");
    porygon_scheduled_slot_t saved = *slot_of(&f, 1);
    *slot_of(&f, 1) = (porygon_scheduled_slot_t){0};
    f.schedule.bundles[1].slots[0] = saved;
    assert(porygon_verify_schedule(&f.program, &f.schedule).status == PORYGON_SCHEDULE_DEPENDENCY);
    destroy(&f);
    f = schedule_text("sumi r1, #0\n");
    f.schedule.bundles[0].slots[1].kind = PORYGON_SLOT_NOP;
    assert(porygon_verify_schedule(&f.program, &f.schedule).status == PORYGON_SCHEDULE_INVALID_BUNDLE);
    destroy(&f);
    f = schedule_text("shl r1, r2\n");
    slot_of(&f, 0)->pipe = 0;
    assert(porygon_verify_schedule(&f.program, &f.schedule).status == PORYGON_SCHEDULE_INVALID_BUNDLE);
    destroy(&f);
    f = schedule_text("mul r1, r7\nshl r2, r7\nshl r3, r7\n");
    saved = *slot_of(&f, 2);
    *slot_of(&f, 2) = (porygon_scheduled_slot_t){0};
    f.schedule.bundles[2].slots[0] = saved;
    assert(porygon_verify_schedule(&f.program, &f.schedule).status == PORYGON_SCHEDULE_RESOURCE);
    destroy(&f);
    f = schedule_text("loop: j loop\n");
    f.schedule.bundles[1].slots[0].word = 1;
    assert(porygon_verify_schedule(&f.program, &f.schedule).status != PORYGON_SCHEDULE_OK);
    destroy(&f);
    f = schedule_text("loop: j loop\n");
    f.schedule.label_bundles[0] = 1;
    assert(porygon_verify_schedule(&f.program, &f.schedule).status == PORYGON_SCHEDULE_INVALID_BUNDLE);
    destroy(&f);
}

/** Comprueba el contrato textual y el orden de slots en readmemh. */
static void test_output(void)
{
    fixture_t f = schedule_text("begin: sumi r1, #1\nsumi r2, #2\n");
    FILE *file = tmpfile();
    char text[2048] = {0};
    size_t length;
    assert(file != NULL);
    assert(porygon_write_bundles(file, &f.program, &f.schedule).status == PORYGON_SCHEDULE_OK);
    rewind(file);
    length = fread(text, 1, sizeof(text) - 1, file);
    assert(length > 0);
    assert(strstr(text, "LABEL begin 0\nBUNDLE 0\n") != NULL);
    assert(strstr(text, "SLOT 0 MATH pipe=0 sumi r1, #1") != NULL);
    assert(strstr(text, "SLOT 1 LITERAL 0x0001") != NULL);
    assert(strstr(text, "SLOT 2 MATH pipe=1 sumi r2, #2") != NULL);
    fclose(file);
    file = tmpfile();
    assert(file != NULL);
    assert(porygon_write_mem(file, &f.program, &f.schedule).status == PORYGON_SCHEDULE_OK);
    rewind(file);
    assert(fgets(text, sizeof(text), file) != NULL);
    assert(strcmp(text, "0002402C00012024\n") == 0);
    fclose(file);
    destroy(&f);
}


/** Evalúa las operaciones usadas por la prueba diferencial sin usar metadatos del scheduler. */
static uint32_t evaluate(const porygon_instruction_t *in, const uint32_t regs[8])
{
    uint32_t a = regs[in->operands.math.rs1];
    uint32_t b = in->immediate ? (uint32_t)in->operands.math.literal : regs[in->operands.math.rs2];
    switch (in->funct) {
    case PORYGON_MATH_SUM: return a + b;
    case PORYGON_MATH_MOV: return b;
    case PORYGON_MATH_MUL: return a * b;
    case PORYGON_MATH_SHL: return a << (b & 31);
    default: abort();
    }
}

/** Generador determinista para reproducir dependencias densas y escrituras a r0. */
static unsigned int random_value(unsigned int *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

/** Compara el programa secuencial con emisión paralela y commits diferidos reales. */
static void test_differential(void)
{
    unsigned int seed;
    for (seed = 1; seed <= 100; ++seed) {
        char source[4096];
        size_t length = 0, i, t, s;
        unsigned int state = seed;
        uint32_t expected[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        uint32_t actual[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        uint32_t values[512][8] = {{0}};
        bool pending[512][8] = {{false}};
        fixture_t f;
        for (i = 0; i < 60; ++i) {
            unsigned int op = (random_value(&state) >> 16) % 4;
            unsigned int a = (random_value(&state) >> 16) % 8;
            unsigned int b = (random_value(&state) >> 16) % 8;
            const char *names[] = {"sum", "mov", "mul", "shl"};
            int written = snprintf(source + length, sizeof(source) - length, "%s r%u, r%u\n", names[op], a, b);
            assert(written > 0 && (size_t)written < sizeof(source) - length);
            length += (size_t)written;
        }
        f = schedule_text(source);
        assert(f.schedule.bundle_count + 4 < 512);
        for (i = 0; i < f.program.instruction_count; ++i) {
            const porygon_instruction_t *in = &f.program.instructions[i].instruction;
            uint32_t value = evaluate(in, expected);
            if (in->operands.math.rs1) expected[in->operands.math.rs1] = value;
        }
        for (t = 0; t < f.schedule.bundle_count + 4; ++t) {
            if (t < f.schedule.bundle_count) {
                for (s = 0; s < 4; ++s) {
                    const porygon_scheduled_slot_t *slot = &f.schedule.bundles[t].slots[s];
                    const porygon_instruction_t *in;
                    size_t wb;
                    unsigned int dest;
                    if (slot->kind != PORYGON_SLOT_INSTRUCTION) continue;
                    in = &f.program.instructions[slot->instruction_index].instruction;
                    wb = t + (in->funct == PORYGON_MATH_MUL ? 4 : 2);
                    dest = in->operands.math.rs1;
                    if (dest) {
                        assert(!pending[wb][dest]);
                        values[wb][dest] = evaluate(in, actual);
                        pending[wb][dest] = true;
                    }
                }
            }
            for (i = 1; i < 8; ++i) if (pending[t][i]) actual[i] = values[t][i];
        }
        assert(memcmp(actual, expected, sizeof(actual)) == 0);
        destroy(&f);
    }
}

/** Ejecuta regresiones de semántica, recursos, serialización y corrupción de bundles. */
int main(void)
{
    test_dependencies();
    test_alu1();
    test_memory_crypto();
    test_validation();
    test_control();
    test_verifier();
    test_output();
    test_differential();
    puts("test_scheduler: regresiones y 100 programas diferenciales correctos");
    return 0;
}
