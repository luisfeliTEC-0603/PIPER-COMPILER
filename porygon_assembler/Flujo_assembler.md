# Ensamblador PORYGON

Esta carpeta contiene los módulos C del ensamblador propio. Es la ubicación de
las fuentes de software que antes estaban en `PORYGON/tools/assembler/`.

## Flujo actual

1. `asm_parser.c` y `mnemonics.c` reconocen y validan instrucciones individuales.
2. `asm_program_parser.c` lee ASM lineal, resuelve nombres de etiquetas y crea
   un `porygon_machine_program_t` definido en `machine_program.h`.
3. `scheduler/` consume ese programa, asigna slots y ALUs, y asocia cada etiqueta
   con su índice final de bundle.
4. `jump_fixup.c` calcula el desplazamiento de un salto largo una vez conocidos
   esos índices. `encoder.c` codifica las instrucciones en slots de 16 bits;
   `porygon_encode_math_pipe` recibe la ALU elegida por el scheduler.
5. Un verificador independiente comprueba el calendario y las codificaciones.
   `scheduler/output.c` escribe el archivo de bundles y, opcionalmente, `.mem`.

El ASM de entrada tiene una instrucción por línea. Admite etiquetas como
`inicio:` en una línea propia o antes de una instrucción. No admite comentarios,
directivas, bundles explícitos ni offsets numéricos de salto. Los saltos
simbólicos `j`, `jz` y `jnz` reservan dos slots para poder resolverse después
de calendarizar.

La API de entrada está en `asm_program_parser.h`:
`porygon_parse_program(source, &program)` para texto y
`porygon_parse_program_file(path, &program)` para un archivo. Después de usar
el programa, hay que llamar a `porygon_machine_program_free(&program)`.

El `Makefile` principal compila y enlaza estos módulos con `pipoc` y con
`bin/porygon-schedule entrada.asm salida.bundles [salida.mem]`. La CLI del
scheduler funciona sin depender del generador de Piper, aún en desarrollo.

El parser lineal no consume el archivo `.bundles`: ese contrato de salida,
incluidos los slots literales y `pipe`, se describe en
[`docs/scheduler.md`](../docs/scheduler.md).
