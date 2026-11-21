/** @file regalloc_pipeline.c
 * Recorre Piper -> IR/CFG -> asignacion y ejecuta el subconjunto virtual
 * necesario para comprobar que los spills conservan seis argumentos.
 * No interpreta ASM ni sustituye las pruebas de ejecucion del emisor.
 */
#include "parser.h"
#include "semantic.h"
#include "ir.h"
#include "cfg.h"
#include "regalloc.h"
#include "regalloc_machine.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void yyrestart(FILE *input_file);
int yylex_destroy(void);

static unsigned color(const RegallocAssignedRoutine *routine, IrId temp)
{
    unsigned result = routine->register_for_temp[temp];
    assert(result >= 1 && result <= REGALLOC_COLOR_COUNT);
    return result;
}

/* CALL_ARG cambia r1..r4 como haria su expansion. CALL_RESULT modela el
 * retorno de `primero`, que devuelve el primer argumento, y restaura los
 * colores del llamador. Solo se admiten los opcodes de estos dos programas. */
static uint32_t run_main(const RegallocAssignedProgram *assigned, size_t r,
                         bool with_call)
{
    const RegallocMachineRoutine *machine = &assigned->machine->routines[r];
    const RegallocAssignedRoutine *routine = &assigned->routines[r];
    assert(machine->block_count == 1);
    uint32_t registers[8] = {0}, saved[8] = {0}, arguments[6] = {0};
    uint32_t *slots = calloc(machine->spill_slot_count == 0 ? 1 :
                             machine->spill_slot_count, sizeof(*slots));
    assert(slots != NULL);
    size_t stores = 0, loads = 0, calls = 0, call_args = 0;
    bool returned = false;
    uint32_t result = 0;
    const RegallocMachineBlock *block = &machine->blocks[0];
    for (size_t i = 0; i < block->instruction_count; i++) {
        const RegallocMachineInstruction *in = &block->instructions[i];
        switch (in->op) {
        case REGALLOC_M_IMM:
            registers[color(routine, in->def)] = (uint32_t)in->immediate;
            break;
        case REGALLOC_M_MOVE:
            registers[color(routine, in->def)] =
                registers[color(routine, in->use1)];
            break;
        case REGALLOC_M_SPILL_STORE:
            assert(in->slot < machine->spill_slot_count);
            assert(in->flags == IR_WRITE_MEMORY &&
                   in->fixed_reads == REGALLOC_PHYS_R7);
            assert(regalloc_assigned_spill_offset(assigned, r, in->slot) ==
                   machine->base_frame_bytes + 4 * in->slot);
            slots[in->slot] = registers[color(routine, in->use1)];
            stores++;
            break;
        case REGALLOC_M_SPILL_LOAD:
            assert(in->slot < machine->spill_slot_count);
            assert(in->flags == IR_READ_MEMORY &&
                   in->fixed_reads == REGALLOC_PHYS_R7);
            registers[color(routine, in->def)] = slots[in->slot];
            loads++;
            break;
        case REGALLOC_M_CALL_SAVE:
            memcpy(saved, registers, sizeof(saved));
            break;
        case REGALLOC_M_CALL_ARG: {
            size_t index = in->call_arg_index;
            assert(index < 6);
            arguments[index] = registers[color(routine, in->use1)];
            if (index < REGALLOC_REGISTER_ARGS)
                registers[index + 1] = arguments[index];
            call_args++;
            break;
        }
        case REGALLOC_M_CALL_RESULT: {
            const uint32_t expected[] = {11, 22, 33, 44, 55, 66};
            assert(in->call_arg_count == 6 && call_args == 6);
            /* Verificar los argumentos fisicos al saltar, no solo la copia
             * observada en CALL_ARG: una recarga posterior podria pisarlos. */
            for (size_t a = 0; a < REGALLOC_REGISTER_ARGS; a++)
                assert(registers[a + 1] == expected[a]);
            for (size_t a = 0; a < 6; a++)
                assert(arguments[a] == expected[a]);
            memcpy(registers, saved, sizeof(registers));
            registers[color(routine, in->def)] = arguments[0];
            calls++;
            break;
        }
        case REGALLOC_M_TO_RETURN:
            registers[1] = registers[color(routine, in->use1)];
            break;
        case REGALLOC_M_RETURN:
            result = registers[1];
            returned = true;
            break;
        default:
            assert(!"opcode fuera del subconjunto interpretado");
        }
    }
    assert(returned);
    assert(calls == (size_t)with_call);
    assert((stores != 0) == with_call && (loads != 0) == with_call);
    free(slots);
    return result;
}

/* Conserva vivas las estructuras prestadas hasta terminar la validacion de
 * la vista asignada y la interpretacion de la rutina principal. */
static void check_source(const char *source, bool with_spill,
                         uint32_t expected_result)
{
    FILE *input = tmpfile();
    assert(input != NULL);
    assert(fputs(source, input) >= 0 && fflush(input) == 0);
    rewind(input);
    SymbolTable *symbols = symbol_table_create();
    assert(symbols != NULL);
    AstNode *ast = NULL;
    yyrestart(input);
    assert(yyparse(&ast, symbols) == 0 && ast != NULL);
    yylex_destroy();
    assert(fclose(input) == 0);
    assert(analyze_semantics_with_trace(ast, symbols, 0));

    IrProgram *ir = NULL;
    IrDiagnostic ir_error;
    assert(ir_generate(ast, symbols, &ir, &ir_error) == IR_OK);
    CfgProgram *cfg = NULL;
    CfgDiagnostic cfg_error;
    assert(cfg_build(ir, &cfg, &cfg_error) == CFG_OK);
    RegallocAnalysis *analysis = NULL;
    RegallocDiagnostic error;
    assert(regalloc_prepare(ir, cfg, &analysis, &error) == REGALLOC_OK);
    RegallocAllocation *allocation = NULL;
    assert(regalloc_allocate(analysis, &allocation, &error) == REGALLOC_OK);
    const RegallocAssignedProgram *assigned =
        regalloc_allocation_view(allocation);
    assert(regalloc_assigned_program_validate(assigned, &error) == REGALLOC_OK);

    size_t main_routine = SIZE_MAX;
    for (size_t r = 0; r < assigned->routine_count; r++) {
        const RegallocMachineRoutine *machine = &assigned->machine->routines[r];
        const Symbol *symbol = ir->routines[machine->ir_routine].symbol;
        if (symbol != NULL && strcmp(symbol->name, "main") == 0)
            main_routine = r;
        assert(assigned->routines[r].frame_bytes ==
               machine->base_frame_bytes + 4 * machine->spill_slot_count);
    }
    assert(main_routine != SIZE_MAX);
    const RegallocMachineRoutine *main_machine =
        &assigned->machine->routines[main_routine];
    assert((main_machine->spill_slot_count != 0) == with_spill);
    assert(run_main(assigned, main_routine, with_spill) == expected_result);
    printf("%s: %zu ranuras, frame %zu bytes, retorno %u\n",
           with_spill ? "con spill" : "sin spill",
           main_machine->spill_slot_count,
           assigned->routines[main_routine].frame_bytes, expected_result);

    regalloc_allocation_free(allocation);
    regalloc_analysis_free(analysis);
    cfg_program_free(cfg);
    ir_program_free(ir);
    ast_free(ast);
    symbol_table_destroy(symbols);
}

int main(void)
{
    check_source("routine B32 main[] { output 7. }\n", false, 7);
    check_source(
        "routine B32 primero[B32 a, B32 b, B32 c, B32 d, B32 e, B32 f] {\n"
        "    output a.\n"
        "}\n"
        "routine B32 main[] {\n"
        "    output primero[11,22,33,44,55,66].\n"
        "}\n", true, 11);
    return 0;
}
