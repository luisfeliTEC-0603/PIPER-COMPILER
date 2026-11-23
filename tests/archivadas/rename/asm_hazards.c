/* Oraculo sobre el ASM emitido: cuenta pares I < J por clase de dependencia
 * en cada bloque fisico usando las mismas mascaras que recibe el scheduler. */
#include "assembler/asm_program_parser.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    porygon_machine_program_t program = {0};
    if (argc != 2) return 2;
    if (porygon_parse_program_file(argv[1], &program).status != PORYGON_PROGRAM_OK) {
        fprintf(stderr, "ASM invalido: %s\n", argv[1]);
        return 1;
    }
    size_t war = 0, waw = 0;
    for (size_t b = 0; b < program.block_count; b++) {
        const porygon_machine_block_t *block = &program.blocks[b];
        size_t end = block->first_instruction + block->instruction_count;
        for (size_t j = block->first_instruction; j < end; j++) {
            for (size_t i = block->first_instruction; i < j; i++) {
                war += (program.instructions[i].uses & program.instructions[j].defs) != 0;
                waw += (program.instructions[i].defs & program.instructions[j].defs) != 0;
            }
        }
    }
    printf("%zu %zu\n", war, waw);
    porygon_machine_program_free(&program);
    return 0;
}
