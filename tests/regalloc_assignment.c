#include "regalloc_machine.h"
#include <assert.h>
#include <stdio.h>

/* El emisor recibe colores para cada operando y un frame que incluye el slot. */
static void test_spill_handoff(void)
{
    RegallocMachineInstruction instructions[] = {
        {.op = REGALLOC_M_IMM, .def = 0, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_IMM, .def = 1, .use1 = IR_NO_ID},
        {.op = REGALLOC_M_ADD, .def = 0, .use1 = 1},
        {.op = REGALLOC_M_SPILL_STORE, .def = IR_NO_ID, .use1 = 0,
         .slot = 0, .flags = IR_WRITE_MEMORY, .fixed_reads = REGALLOC_PHYS_R7},
        {.op = REGALLOC_M_SPILL_LOAD, .def = 2, .use1 = IR_NO_ID,
         .slot = 0, .flags = IR_READ_MEMORY, .fixed_reads = REGALLOC_PHYS_R7},
        {.op = REGALLOC_M_TO_RETURN, .def = IR_NO_ID, .use1 = 2,
         .fixed_writes = REGALLOC_PHYS_R1},
        {.op = REGALLOC_M_RETURN, .def = IR_NO_ID, .use1 = IR_NO_ID,
         .fixed_reads = REGALLOC_PHYS_R1}
    };
    RegallocMachineBlock block = {.id = 4, .instructions = instructions,
                                  .instruction_count = 7};
    RegallocMachineRoutine routine = {.ir_routine = 0, .temp_count = 3,
        .base_frame_bytes = 20, .spill_slot_count = 1,
        .blocks = &block, .block_count = 1};
    Symbol function = {.symbol_class = SYMBOL_FUNC,
                       .total_stack_frame_size = 20, .call_reserve_size = 8};
    IrRoutine ir_routine = {.symbol = &function};
    SymbolTable symbols = {0};
    IrProgram ir = {.symbols = &symbols, .routines = &ir_routine,
                    .routine_count = 1};
    RegallocMachineProgram machine = {.source = &ir, .routines = &routine,
                                      .routine_count = 1};
    uint8_t colors[] = {2, 3, 2};
    RegallocAssignedRoutine assigned_routine = {
        .register_for_temp = colors, .frame_bytes = 24
    };
    RegallocAssignedProgram assigned = {.machine = &machine,
        .routines = &assigned_routine, .routine_count = 1};
    RegallocDiagnostic diagnostic;
    assert(regalloc_assigned_program_validate(&assigned, &diagnostic) ==
           REGALLOC_OK);
    assert(regalloc_assigned_spill_offset(&assigned, 0, 0) == 20);

    colors[1] = 2; /* t0 y t1 estan vivos al mismo tiempo. */
    assert(regalloc_assigned_program_validate(&assigned, &diagnostic) ==
           REGALLOC_INVALID_INPUT);
    colors[1] = 3;
    colors[2] = 0; /* La recarga tiene que recibir registro. */
    assert(regalloc_assigned_program_validate(&assigned, &diagnostic) ==
           REGALLOC_INVALID_INPUT);
    colors[2] = 2;
    assigned_routine.frame_bytes = 20;
    assert(regalloc_assigned_program_validate(&assigned, &diagnostic) ==
           REGALLOC_INVALID_INPUT);
    assigned_routine.frame_bytes = 24;
    instructions[3].flags = 0;
    assert(regalloc_assigned_program_validate(&assigned, &diagnostic) ==
           REGALLOC_INVALID_INPUT);
    instructions[3].flags = IR_WRITE_MEMORY;
    function.call_reserve_size = SYMBOL_STACK_SIZE;
    assert(regalloc_assigned_program_validate(&assigned, &diagnostic) ==
           REGALLOC_INVALID_INPUT);
}

/* La escritura de r1 bloquea ese color a los valores que sobreviven. */
static void test_fixed_write_without_spill(void)
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
    RegallocMachineBlock block = {.id = 0, .instructions = instructions,
                                  .instruction_count = 7};
    RegallocMachineRoutine routine = {.temp_count = 3, .blocks = &block,
                                      .block_count = 1};
    Symbol function = {.symbol_class = SYMBOL_FUNC};
    IrRoutine ir_routine = {.symbol = &function};
    SymbolTable symbols = {0};
    IrProgram ir = {.symbols = &symbols, .routines = &ir_routine,
                    .routine_count = 1};
    RegallocMachineProgram machine = {.source = &ir, .routines = &routine,
                                      .routine_count = 1};
    uint8_t colors[] = {3, 1, 2};
    RegallocAssignedRoutine assigned_routine = {.register_for_temp = colors};
    RegallocAssignedProgram assigned = {.machine = &machine,
        .routines = &assigned_routine, .routine_count = 1};
    RegallocDiagnostic diagnostic;
    assert(regalloc_assigned_program_validate(&assigned, &diagnostic) ==
           REGALLOC_INVALID_INPUT);
    colors[1] = 4;
    assert(regalloc_assigned_program_validate(&assigned, &diagnostic) ==
           REGALLOC_OK);
    colors[1] = 5; /* r5 queda reservado para el backend. */
    assert(regalloc_assigned_program_validate(&assigned, &diagnostic) ==
           REGALLOC_INVALID_INPUT);
    colors[1] = 6; /* r6 tambien queda fuera del pool de colores. */
    assert(regalloc_assigned_program_validate(&assigned, &diagnostic) ==
           REGALLOC_INVALID_INPUT);
    assert(routine.spill_slot_count == 0 && assigned_routine.frame_bytes == 0);
}

/* El arranque sin temporales usa la reserva global, no un Symbol de funcion. */
static void test_startup_without_temporaries(void)
{
    RegallocMachineInstruction ret = {.op = REGALLOC_M_RETURN,
        .def = IR_NO_ID, .use1 = IR_NO_ID};
    RegallocMachineBlock block = {.id = 9, .instructions = &ret,
                                  .instruction_count = 1};
    RegallocMachineRoutine routine = {.blocks = &block, .block_count = 1};
    IrRoutine ir_routine = {0};
    SymbolTable symbols = {.global_call_reserve_size = 64};
    IrProgram ir = {.symbols = &symbols, .routines = &ir_routine,
                    .routine_count = 1, .startup = 0};
    RegallocMachineProgram machine = {.source = &ir, .routines = &routine,
                                      .routine_count = 1};
    RegallocAssignedRoutine assigned_routine = {0};
    RegallocAssignedProgram assigned = {.machine = &machine,
        .routines = &assigned_routine, .routine_count = 1};
    RegallocDiagnostic diagnostic;
    assert(regalloc_assigned_program_validate(&assigned, &diagnostic) ==
           REGALLOC_OK);
    symbols.global_call_reserve_size = SYMBOL_STACK_SIZE + 1;
    assert(regalloc_assigned_program_validate(&assigned, &diagnostic) ==
           REGALLOC_INVALID_INPUT);
}

int main(void)
{
    test_spill_handoff();
    test_fixed_write_without_spill();
    test_startup_without_temporaries();
    puts("contrato asignador-ASM: ok");
    return 0;
}
