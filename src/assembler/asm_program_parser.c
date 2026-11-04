#include "asm_program_parser.h"

#include "encoder.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Un salto puede mencionar una etiqueta declarada más adelante. Mientras se
   lee el archivo se guarda su nombre; al final se convierte en LabelId. */
typedef struct {
    size_t instruction_index;
    char *name;
    size_t line;
    size_t column;
} pending_target_t;

/* Estado privado de construcción. Las capacidades pertenecen solo al parser:
   MachineProgram publica cantidad y punteros, sin exponer espacio sobrante. */
typedef struct {
    porygon_machine_program_t program;
    size_t instruction_capacity;
    size_t label_capacity;
    pending_target_t *targets;
    size_t target_count;
    size_t target_capacity;
} program_builder_t;

/* Todos los errores del lector usan línea/columna del ASM original. detail
   conserva el error del parser de una instrucción cuando corresponda. */
static porygon_program_parse_result_t result(porygon_program_status_t status,
                                              porygon_parse_status_t detail,
                                              size_t line, size_t column)
{
    porygon_program_parse_result_t value = {status, detail, line, column};
    return value;
}

/* Amplía un arreglo por duplicación para evitar un realloc por instrucción.
   Comprueba la multiplicación antes de reservar para impedir desbordamientos. */
static bool grow(void **items, size_t *capacity, size_t required,
                 size_t item_size)
{
    size_t new_capacity;
    void *new_items;

    if (required <= *capacity) {
        return true;
    }
    new_capacity = *capacity == 0 ? 8 : *capacity;
    while (new_capacity < required) {
        if (new_capacity > SIZE_MAX / 2) {
            new_capacity = required;
            break;
        }
        new_capacity *= 2;
    }
    if (new_capacity > SIZE_MAX / item_size) {
        return false;
    }
    new_items = realloc(*items, new_capacity * item_size);
    if (new_items == NULL) {
        return false;
    }
    *items = new_items;
    *capacity = new_capacity;
    return true;
}

/* Los nombres y líneas se copian: el archivo fuente puede liberarse después
   del parseo sin invalidar ninguna parte del MachineProgram. */
static char *copy_span(const char *start, size_t length)
{
    char *copy;

    if (length == SIZE_MAX) {
        return NULL;
    }
    copy = malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, start, length);
        copy[length] = '\0';
    }
    return copy;
}

static void skip_space(const char **cursor)
{
    while (isspace((unsigned char)**cursor)) {
        ++*cursor;
    }
}

static bool identifier_start(char character)
{
    return isalpha((unsigned char)character) || character == '_';
}

static bool identifier_part(char character)
{
    return isalnum((unsigned char)character) || character == '_';
}

/* Identificadores de etiquetas: letra o '_' inicial, luego letras, dígitos o
   '_'. Un resultado igual a start significa que no había identificador. */
static const char *identifier_end(const char *start)
{
    const char *end = start;

    if (!identifier_start(*end)) {
        return start;
    }
    do {
        ++end;
    } while (identifier_part(*end));
    return end;
}

/* Las etiquetas son sensibles a mayúsculas: Loop y loop son distintas. */
static bool same_name(const char *name, const char *start, size_t length)
{
    return strlen(name) == length && strncmp(name, start, length) == 0;
}

static porygon_program_parse_result_t append_label(program_builder_t *builder,
                                                   const char *name,
                                                   size_t name_length,
                                                   size_t line, size_t column)
{
    porygon_machine_label_t *label;
    size_t index;

    for (index = 0; index < builder->program.label_count; ++index) {
        if (same_name(builder->program.labels[index].name, name, name_length)) {
            return result(PORYGON_PROGRAM_DUPLICATE_LABEL, PORYGON_PARSE_OK,
                          line, column);
        }
    }
    if (!grow((void **)&builder->program.labels, &builder->label_capacity,
              builder->program.label_count + 1, sizeof(*builder->program.labels))) {
        return result(PORYGON_PROGRAM_OUT_OF_MEMORY, PORYGON_PARSE_OK,
                      line, column);
    }
    label = &builder->program.labels[builder->program.label_count];
    label->name = copy_span(name, name_length);
    if (label->name == NULL) {
        return result(PORYGON_PROGRAM_OUT_OF_MEMORY, PORYGON_PARSE_OK,
                      line, column);
    }
    /* La etiqueta apunta a la próxima instrucción lógica, aún sin posición
       de bundle. Varias etiquetas consecutivas comparten este índice. */
    label->instruction_index = builder->program.instruction_count;
    label->block_index = PORYGON_NO_BLOCK;
    label->line = line;
    label->column = column;
    ++builder->program.label_count;
    return result(PORYGON_PROGRAM_OK, PORYGON_PARSE_OK, line, column);
}

static bool append_instruction(program_builder_t *builder,
                               const porygon_instruction_t *parsed,
                               size_t line, size_t column)
{
    porygon_machine_instruction_t *instruction;

    if (!grow((void **)&builder->program.instructions,
              &builder->instruction_capacity,
              builder->program.instruction_count + 1,
              sizeof(*builder->program.instructions))) {
        return false;
    }
    instruction = &builder->program.instructions[builder->program.instruction_count];
    *instruction = (porygon_machine_instruction_t){0};
    instruction->instruction = *parsed;
    /* Una instrucción normal no tiene destino simbólico. Si es un salto,
       resolve_targets() reemplazará este valor después de leer las etiquetas. */
    instruction->target_label = PORYGON_NO_LABEL;
    instruction->line = line;
    instruction->column = column;
    /* El scheduler recibe máscaras y recursos calculados una sola vez; no
       necesita volver a interpretar el mnemónico textual. */
    porygon_describe_instruction(instruction);
    ++builder->program.instruction_count;
    return true;
}

/* Guarda una referencia pendiente junto con su lugar exacto de origen para
   poder señalar una etiqueta ausente al terminar la primera pasada. */
static bool append_target(program_builder_t *builder, size_t instruction_index,
                          const char *name, size_t length,
                          size_t line, size_t column)
{
    pending_target_t *target;

    if (!grow((void **)&builder->targets, &builder->target_capacity,
              builder->target_count + 1, sizeof(*builder->targets))) {
        return false;
    }
    target = &builder->targets[builder->target_count];
    target->name = copy_span(name, length);
    if (target->name == NULL) {
        return false;
    }
    target->instruction_index = instruction_index;
    target->line = line;
    target->column = column;
    ++builder->target_count;
    return true;
}

/* Lee j etiqueta, jz rn, etiqueta o jnz rn, etiqueta. También acepta los
   mnemónicos ji/jzi/jnzi, pero el destino sigue siendo simbólico aquí. */
static porygon_program_parse_result_t parse_symbolic_jump(
    program_builder_t *builder, const char *line_start, const char *cursor,
    const porygon_mnemonic_t *mnemonic, size_t line_number,
    size_t instruction_column)
{
    porygon_instruction_t parsed = {0};
    const char *target_start;
    const char *target_end;
    const char *register_start;
    unsigned int register_number = 0;
    size_t target_length;

    parsed.format = PORYGON_FORMAT_JUMP;
    parsed.funct = mnemonic->funct;
    /* Usar siempre la forma larga fija el tamaño antes del scheduler: el
       salto ocupará un slot y su offset otro, dentro del mismo bundle. */
    parsed.immediate = true;
    skip_space(&cursor);
    if (parsed.funct != PORYGON_JUMP_J) {
        /* Los saltos condicionales consultan cualquier registro r0..r7;
           el ejemplo r3 no representa un registro obligatorio. */
        register_start = cursor;
        if (*cursor != 'r' || !isdigit((unsigned char)cursor[1])) {
            return result(PORYGON_PROGRAM_INVALID_INSTRUCTION,
                          PORYGON_PARSE_EXPECTED_REGISTER, line_number,
                          (size_t)(cursor - line_start) + 1);
        }
        ++cursor;
        do {
            register_number = register_number * 10u +
                              (unsigned int)(*cursor - '0');
            ++cursor;
            if (register_number > 7) {
                return result(PORYGON_PROGRAM_INVALID_INSTRUCTION,
                              PORYGON_PARSE_OUT_OF_RANGE, line_number,
                              (size_t)(register_start - line_start) + 1);
            }
        } while (isdigit((unsigned char)*cursor));
        parsed.operands.jump.rd = (uint8_t)register_number;
        skip_space(&cursor);
        if (*cursor != ',') {
            return result(PORYGON_PROGRAM_INVALID_INSTRUCTION,
                          PORYGON_PARSE_EXPECTED_SEPARATOR, line_number,
                          (size_t)(cursor - line_start) + 1);
        }
        ++cursor;
        skip_space(&cursor);
    }
    /* Un número aquí sería un offset prematuro: el scheduler todavía puede
       insertar bundles, por eso exigimos el nombre de una etiqueta. */
    target_start = cursor;
    target_end = identifier_end(cursor);
    if (target_end == target_start) {
        return result(PORYGON_PROGRAM_EXPECTED_LABEL, PORYGON_PARSE_OK,
                      line_number, (size_t)(cursor - line_start) + 1);
    }
    target_length = (size_t)(target_end - target_start);
    cursor = target_end;
    skip_space(&cursor);
    if (*cursor != '\0') {
        return result(PORYGON_PROGRAM_INVALID_INSTRUCTION,
                      PORYGON_PARSE_TRAILING_TEXT, line_number,
                      (size_t)(cursor - line_start) + 1);
    }
    if (!append_instruction(builder, &parsed, line_number, instruction_column)) {
        return result(PORYGON_PROGRAM_OUT_OF_MEMORY, PORYGON_PARSE_OK,
                      line_number, instruction_column);
    }
    if (!append_target(builder, builder->program.instruction_count - 1,
                       target_start, target_length, line_number,
                       (size_t)(target_start - line_start) + 1)) {
        return result(PORYGON_PROGRAM_OUT_OF_MEMORY, PORYGON_PARSE_OK,
                      line_number, (size_t)(target_start - line_start) + 1);
    }
    return result(PORYGON_PROGRAM_OK, PORYGON_PARSE_OK,
                  line_number, instruction_column);
}

/* Cada línea admite etiquetas antes de una instrucción o etiquetas solas.
   Las líneas sin etiquetas se entregan al parser de instrucciones existente. */
static porygon_program_parse_result_t parse_line(program_builder_t *builder,
                                                   const char *line,
                                                   size_t line_number)
{
    const char *cursor = line;
    const char *name_end;
    const char *mnemonic_end;
    const porygon_mnemonic_t *mnemonic;
    porygon_instruction_t parsed;
    porygon_parse_result_t parsed_result;
    porygon_program_parse_result_t label_result;
    char mnemonic_name[32];
    size_t length;
    size_t column;
    size_t index;

    skip_space(&cursor);
    /* Varias etiquetas pueden nombrar el mismo comienzo de bloque. */
    while (identifier_start(*cursor)) {
        name_end = identifier_end(cursor);
        if (*name_end != ':') {
            break;
        }
        label_result = append_label(builder, cursor,
                                    (size_t)(name_end - cursor), line_number,
                                    (size_t)(cursor - line) + 1);
        if (label_result.status != PORYGON_PROGRAM_OK) {
            return label_result;
        }
        cursor = name_end + 1;
        skip_space(&cursor);
    }
    if (*cursor == '\0') {
        return result(PORYGON_PROGRAM_OK, PORYGON_PARSE_OK, line_number, 1);
    }
    column = (size_t)(cursor - line) + 1;

    /* Solo los saltos directos necesitan una ruta nueva. Consultamos la misma
       tabla de mnemónicos que usa asm_parser.c para no duplicar sus opcodes. */
    mnemonic_end = cursor;
    while (isalpha((unsigned char)*mnemonic_end)) {
        ++mnemonic_end;
    }
    length = (size_t)(mnemonic_end - cursor);
    mnemonic = NULL;
    if (length > 0 && length < sizeof(mnemonic_name)) {
        for (index = 0; index < length; ++index) {
            mnemonic_name[index] = (char)tolower((unsigned char)cursor[index]);
        }
        mnemonic_name[length] = '\0';
        mnemonic = porygon_find_mnemonic(mnemonic_name);
    }
    if (mnemonic != NULL && mnemonic->format == PORYGON_FORMAT_JUMP &&
        mnemonic->funct != PORYGON_JUMP_JR) {
        return parse_symbolic_jump(builder, line, mnemonic_end, mnemonic,
                                   line_number, column);
    }
    parsed_result = porygon_parse_instruction(cursor, &parsed);
    if (parsed_result.status != PORYGON_PARSE_OK) {
        /* El parser de línea mide columnas desde cursor; sumamos la parte
           anterior de la línea, que pudo incluir espacios o una etiqueta. */
        return result(PORYGON_PROGRAM_INVALID_INSTRUCTION,
                      parsed_result.status, line_number,
                      column + parsed_result.column - 1);
    }
    if (!append_instruction(builder, &parsed, line_number, column)) {
        return result(PORYGON_PROGRAM_OUT_OF_MEMORY, PORYGON_PARSE_OK,
                      line_number, column);
    }
    return result(PORYGON_PROGRAM_OK, PORYGON_PARSE_OK, line_number, column);
}

/* Segunda pasada: convierte cada nombre de salto en un índice estable de
   program.labels. Así se aceptan tanto saltos hacia atrás como hacia delante. */
static porygon_program_parse_result_t resolve_targets(program_builder_t *builder)
{
    size_t target_index;
    size_t label_index;
    bool found;

    for (target_index = 0; target_index < builder->target_count; ++target_index) {
        pending_target_t *target = &builder->targets[target_index];
        found = false;
        for (label_index = 0; label_index < builder->program.label_count;
             ++label_index) {
            if (strcmp(target->name, builder->program.labels[label_index].name) == 0) {
                builder->program.instructions[target->instruction_index].target_label =
                    label_index;
                found = true;
                break;
            }
        }
        if (!found) {
            return result(PORYGON_PROGRAM_UNKNOWN_LABEL, PORYGON_PARSE_OK,
                          target->line, target->column);
        }
    }
    return result(PORYGON_PROGRAM_OK, PORYGON_PARSE_OK, 0, 0);
}

/* Un bloque empieza al principio del archivo, después de un salto o donde
   aparece una etiqueta. Cada bloque es un rango del arreglo de instrucciones;
   sus índices siguen válidos aunque no se guarden punteros internos. */
static bool build_blocks(porygon_machine_program_t *program)
{
    size_t instruction_index;
    size_t label_index = 0;
    size_t block_start = 0;
    size_t block_index;

    if (program->instruction_count > SIZE_MAX / sizeof(*program->blocks)) {
        return false;
    }
    /* En el caso extremo, cada instrucción inicia un bloque. */
    program->blocks = calloc(program->instruction_count, sizeof(*program->blocks));
    if (program->blocks == NULL) {
        return false;
    }
    for (instruction_index = 0; instruction_index < program->instruction_count;
         ++instruction_index) {
        if (instruction_index != 0 &&
            (program->instructions[instruction_index - 1].ends_block ||
             (label_index < program->label_count &&
              program->labels[label_index].instruction_index == instruction_index))) {
            program->blocks[program->block_count].first_instruction = block_start;
            program->blocks[program->block_count].instruction_count =
                instruction_index - block_start;
            ++program->block_count;
            block_start = instruction_index;
        }
        /* Todas las etiquetas que apuntan a esta instrucción pertenecen al
           mismo bloque recién iniciado. */
        while (label_index < program->label_count &&
               program->labels[label_index].instruction_index == instruction_index) {
            program->labels[label_index].block_index = program->block_count;
            ++label_index;
        }
    }
    program->blocks[program->block_count].first_instruction = block_start;
    program->blocks[program->block_count].instruction_count =
        program->instruction_count - block_start;
    ++program->block_count;

    /* Una vez conocidos todos los bloques se construyen las aristas de flujo.
       JZ/JNZ conservan el camino secuencial; J/JR no lo conservan. */
    for (block_index = 0; block_index < program->block_count; ++block_index) {
        porygon_machine_block_t *block = &program->blocks[block_index];
        const porygon_machine_instruction_t *last =
            &program->instructions[block->first_instruction +
                                   block->instruction_count - 1];
        bool has_next = block_index + 1 < program->block_count;

        block->fallthrough_block = PORYGON_NO_BLOCK;
        block->branch_target_block = PORYGON_NO_BLOCK;
        if (last->target_label != PORYGON_NO_LABEL) {
            block->branch_target_block =
                program->labels[last->target_label].block_index;
        }
        if (has_next && (!last->ends_block ||
                         (last->instruction.format == PORYGON_FORMAT_JUMP &&
                          (last->instruction.funct == PORYGON_JUMP_JZ ||
                           last->instruction.funct == PORYGON_JUMP_JNZ)))) {
            block->fallthrough_block = block_index + 1;
        }
    }
    return true;
}

/* En cualquier error se descartan también los nombres pendientes. El out del
   llamador aún no se ha tocado, por lo que puede volver a intentar el parseo. */
static void free_builder(program_builder_t *builder)
{
    size_t index;

    for (index = 0; index < builder->target_count; ++index) {
        free(builder->targets[index].name);
    }
    free(builder->targets);
    porygon_machine_program_free(&builder->program);
}

porygon_program_parse_result_t porygon_parse_program(
    const char *source, porygon_machine_program_t *out)
{
    program_builder_t builder = {0};
    porygon_program_parse_result_t parsed;
    const char *line_start;
    const char *line_end;
    char *line;
    size_t line_number = 1;
    size_t index;

    if (source == NULL || out == NULL) {
        return result(PORYGON_PROGRAM_INVALID_ARGUMENT, PORYGON_PARSE_OK, 0, 0);
    }
    /* Primera pasada: separamos líneas conservando su número. La copia deja
       cada línea terminada en NUL para reutilizar el parser ya existente. */
    line_start = source;
    do {
        line_end = line_start;
        while (*line_end != '\0' && *line_end != '\n') {
            ++line_end;
        }
        line = copy_span(line_start, (size_t)(line_end - line_start));
        if (line == NULL) {
            parsed = result(PORYGON_PROGRAM_OUT_OF_MEMORY, PORYGON_PARSE_OK,
                            line_number, 1);
            goto failure;
        }
        parsed = parse_line(&builder, line, line_number);
        free(line);
        if (parsed.status != PORYGON_PROGRAM_OK) {
            goto failure;
        }
        if (*line_end == '\0') {
            break;
        }
        line_start = line_end + 1;
        ++line_number;
    } while (true);

    /* Una etiqueta al final no nombra una instrucción ni un bloque al que el
       scheduler pueda asignar una posición de bundle. Se comprueba antes del
       programa vacío para dar el error preciso incluso si solo hay etiquetas. */
    for (index = 0; index < builder.program.label_count; ++index) {
        if (builder.program.labels[index].instruction_index ==
            builder.program.instruction_count) {
            parsed = result(PORYGON_PROGRAM_LABEL_WITHOUT_INSTRUCTION,
                            PORYGON_PARSE_OK, builder.program.labels[index].line,
                            builder.program.labels[index].column);
            goto failure;
        }
    }
    if (builder.program.instruction_count == 0) {
        parsed = result(PORYGON_PROGRAM_EMPTY, PORYGON_PARSE_OK, 1, 1);
        goto failure;
    }
    parsed = resolve_targets(&builder);
    if (parsed.status != PORYGON_PROGRAM_OK) {
        goto failure;
    }
    if (!build_blocks(&builder.program)) {
        parsed = result(PORYGON_PROGRAM_OUT_OF_MEMORY, PORYGON_PARSE_OK, 0, 0);
        goto failure;
    }
    for (index = 0; index < builder.target_count; ++index) {
        free(builder.targets[index].name);
    }
    free(builder.targets);
    /* La propiedad de los arreglos pasa al llamador solo cuando el programa
       completo, incluidas referencias y aristas, está validado. */
    *out = builder.program;
    return result(PORYGON_PROGRAM_OK, PORYGON_PARSE_OK, 0, 0);

failure:
    free_builder(&builder);
    return parsed;
}

porygon_program_parse_result_t porygon_parse_program_file(
    const char *path, porygon_machine_program_t *out)
{
    FILE *file;
    char *source = NULL;
    size_t capacity = 0;
    size_t length = 0;
    size_t bytes_read;
    char chunk[4096];
    porygon_program_parse_result_t parsed;

    if (path == NULL || out == NULL) {
        return result(PORYGON_PROGRAM_INVALID_ARGUMENT, PORYGON_PARSE_OK, 0, 0);
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        return result(PORYGON_PROGRAM_IO_ERROR, PORYGON_PARSE_OK, 0, 0);
    }
    /* Se lee en fragmentos para aceptar archivos sin un tamaño fijo. Se
       rechaza NUL porque el parser de texto lo interpretaría como fin. */
    do {
        bytes_read = fread(chunk, 1, sizeof(chunk), file);
        if (memchr(chunk, '\0', bytes_read) != NULL) {
            parsed = result(PORYGON_PROGRAM_INVALID_SOURCE, PORYGON_PARSE_OK,
                            0, 0);
            goto done;
        }
        /* Reservamos un byte adicional para el NUL final antes de sumar
           tamaños, evitando desbordamientos en archivos grandes. */
        if (length > SIZE_MAX - bytes_read - 1 ||
            !grow((void **)&source, &capacity, length + bytes_read + 1,
                  sizeof(*source))) {
            parsed = result(PORYGON_PROGRAM_OUT_OF_MEMORY, PORYGON_PARSE_OK,
                            0, 0);
            goto done;
        }
        memcpy(source + length, chunk, bytes_read);
        length += bytes_read;
    } while (bytes_read == sizeof(chunk));
    if (ferror(file)) {
        parsed = result(PORYGON_PROGRAM_IO_ERROR, PORYGON_PARSE_OK, 0, 0);
        goto done;
    }
    source[length] = '\0';
    parsed = porygon_parse_program(source, out);

done:
    free(source);
    (void)fclose(file);
    return parsed;
}
