/** API del emisor: rechazar entregas invalidas antes de escribir, conservar
 * asignacion/simbolos, ASM determinista y propagacion de errores de stream. */
#include "codeOPgen.h"
#include "parser.h"
#include "semantic.h"
#include "assembler/asm_program_parser.h"
#include "scheduler/scheduler.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

void yyrestart(FILE *input_file);
int yylex_destroy(void);

static void check_reordering(const char *assembly, size_t expected, size_t second_issued)
{
    porygon_machine_program_t program = {0};
    porygon_schedule_t schedule = {0};
    assert(porygon_parse_program(assembly, &program).status == PORYGON_PROGRAM_OK);
    assert(porygon_schedule_program(&program, &schedule).status == PORYGON_SCHEDULE_OK);
    assert(schedule.bundles[0].slots[1].kind == PORYGON_SLOT_INSTRUCTION);
    assert(schedule.bundles[0].slots[1].instruction_index == second_issued);
    assert(schedule.reordered_instructions == expected);
    porygon_schedule_free(&schedule);
    porygon_machine_program_free(&program);
}

int main(void)
{
    check_reordering("mov r1, r2\nmov r3, r4\n", 0, 1);
    check_reordering("mov r1, r2\nsum r1, r3\nmov r4, r5\n", 1, 2);
    FILE *source = tmpfile();
    assert(source != NULL && fputs("routine B32 main[] { output 7. }", source) >= 0);
    rewind(source);
    SymbolTable *symbols = symbol_table_create();
    AstNode *ast = NULL;
    yyrestart(source);
    assert(yyparse(&ast, symbols) == 0);
    yylex_destroy();
    assert(fclose(source) == 0 && analyze_semantics_with_trace(ast, symbols, 0));
    IrProgram *ir = NULL;
    IrDiagnostic ir_error;
    assert(ir_generate(ast, symbols, &ir, &ir_error) == IR_OK);
    CfgProgram *cfg = NULL;
    CfgDiagnostic cfg_error;
    assert(cfg_build(ir, &cfg, &cfg_error) == CFG_OK);
    RegallocAnalysis *analysis = NULL;
    RegallocAllocation *allocation = NULL;
    RegallocDiagnostic allocation_error;
    assert(regalloc_prepare(ir, cfg, &analysis, &allocation_error) == REGALLOC_OK);
    assert(regalloc_allocate(analysis, &allocation, &allocation_error) == REGALLOC_OK);
    const RegallocAssignedProgram *assigned = regalloc_allocation_view(allocation);
    CodeOPgenDiagnostic error;
    char *first = NULL, *second = NULL;
    size_t first_size = 0, second_size = 0;
    FILE *out = open_memstream(&first, &first_size);
    assert(out != NULL && codeOPgen_generate(assigned, out, NULL, &error) == CODEOPGEN_OK);
    assert(fclose(out) == 0 && first_size > 0);
    out = open_memstream(&second, &second_size);
    assert(out != NULL && codeOPgen_generate(assigned, out, NULL, NULL) == CODEOPGEN_OK);
    assert(fclose(out) == 0 && strcmp(first, second) == 0);
    free(second);
    assert(regalloc_assigned_program_validate(assigned, &allocation_error) == REGALLOC_OK);
    porygon_machine_program_t machine = {0};
    porygon_schedule_t schedule = {0};
    assert(porygon_parse_program(first, &machine).status == PORYGON_PROGRAM_OK);
    assert(porygon_schedule_program(&machine, &schedule).status == PORYGON_SCHEDULE_OK);
    assert(porygon_verify_schedule(&machine, &schedule).status == PORYGON_SCHEDULE_OK);
    porygon_schedule_free(&schedule);
    porygon_machine_program_free(&machine);
    free(first);

    /* Corromper una COPIA del mapa prestado: no modificar al asignador. */
    uint8_t invalid_color[] = {0};
    RegallocAssignedRoutine invalid_routine = {.register_for_temp = invalid_color};
    RegallocAssignedProgram invalid = *assigned;
    invalid.routines = &invalid_routine;
    out = tmpfile();
    assert(out != NULL);
    assert(codeOPgen_generate(&invalid, out, NULL, &error) == CODEOPGEN_INVALID_MACHINE);
    assert(ftell(out) == 0 && error.message[0] != '\0');
    assert(codeOPgen_generate(NULL, out, NULL, &error) == CODEOPGEN_INVALID_INPUT);
    assert(ftell(out) == 0 && fclose(out) == 0);

    /* /dev/full es una comprobacion de IO en Linux, opcional en otros hosts. */
    out = fopen("/dev/full", "w");
    if (out != NULL) {
        assert(setvbuf(out, NULL, _IONBF, 0) == 0);
        assert(codeOPgen_generate(assigned, out, NULL, &error) == CODEOPGEN_IO_ERROR);
        fclose(out);
    }
    regalloc_allocation_free(allocation);
    regalloc_analysis_free(analysis);
    cfg_program_free(cfg);
    ir_program_free(ir);
    ast_free(ast);
    symbol_table_destroy(symbols);
    puts("codeOPgen: contrato, determinismo, ensamblado y errores IO OK");
    return 0;
}
