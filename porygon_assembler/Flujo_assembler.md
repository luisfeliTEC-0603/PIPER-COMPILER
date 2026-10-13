# Ensamblador PORYGON

Esta carpeta contiene los módulos C del ensamblador propio. Es la ubicación de
las fuentes de software que antes estaban en `PORYGON/tools/assembler/`.

## Flujo actual

1. `asm_parser.c` y `mnemonics.c` reconocen y validan instrucciones individuales.
2. `asm_program_parser.c` lee ASM lineal, resuelve nombres de etiquetas y crea
   un `porygon_machine_program_t` definido en `machine_program.h`.
3. El futuro scheduler consumirá ese programa, asignará bundles y asociará cada
   etiqueta con su índice final de bundle.
4. `jump_fixup.c` calcula el desplazamiento de un salto largo una vez conocidos
   esos índices. `encoder.c` codifica las instrucciones en slots de 16 bits.

El ASM de entrada tiene una instrucción por línea. Admite etiquetas como
`inicio:` en una línea propia o antes de una instrucción. No admite comentarios,
directivas, bundles explícitos ni offsets numéricos de salto. Los saltos
simbólicos `j`, `jz` y `jnz` reservan dos slots para poder resolverse después
de calendarizar.

La API de entrada está en `asm_program_parser.h`:
`porygon_parse_program(source, &program)` para texto y
`porygon_parse_program_file(path, &program)` para un archivo. Después de usar
el programa, hay que llamar a `porygon_machine_program_free(&program)`.

El `Makefile` principal compila y enlaza estos módulos con `pipoc`. Todavía no
hay una interfaz de línea de comandos que convierta ASM a `.mem`: falta conectar
el generador, el scheduler y el empaquetado final de bundles de 64 bits.
