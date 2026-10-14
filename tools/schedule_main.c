#include "porygon_assembler/asm_program_parser.h"
#include "scheduler/scheduler.h"

#include <errno.h>
#include <string.h>

/** Imprime la ubicación ASM y la posición calendarizada cuando están disponibles. */
static void print_error(const char *path, const porygon_machine_program_t *program,
                        porygon_schedule_result_t error)
{
    fprintf(stderr, "%s", path);
    if (error.instruction_index < program->instruction_count) {
        const porygon_machine_instruction_t *mi = &program->instructions[error.instruction_index];
        fprintf(stderr, ":%zu:%zu", mi->line, mi->column);
    }
    fprintf(stderr, ": %s", error.message);
    if (error.bundle != SIZE_MAX) fprintf(stderr, " (bundle %zu)", error.bundle);
    if (error.slot != SIZE_MAX) fprintf(stderr, " (slot %zu)", error.slot);
    fputc('\n', stderr);
}

/** Escribe una salida ya verificada y comprueba también errores al cerrar el archivo. */
static int write_file(const char *path, const porygon_machine_program_t *program,
                      const porygon_schedule_t *schedule, bool mem)
{
    FILE *file = fopen(path, "w");
    porygon_schedule_result_t result;
    int closed;
    if (file == NULL) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return 1;
    }
    result = mem ? porygon_write_mem(file, program, schedule)
                 : porygon_write_bundles(file, program, schedule);
    /* Un fprintf puede quedar en el buffer: algunos errores aparecen recién al
       cerrarlo, incluso si el escritor del calendario devolvió éxito. */
    closed = fclose(file);
    if (result.status != PORYGON_SCHEDULE_OK) {
        print_error(path, program, result);
        return 1;
    }
    if (closed != 0) {
        fprintf(stderr, "%s: error al cerrar la salida\n", path);
        return 1;
    }
    return 0;
}

/**
 * Une parser, scheduler y escritores para usar ASM sin pasar por el frontend de Piper.
 * Las dos estructuras viven hasta terminar todas las salidas, porque los slots
 * conservan índices hacia las instrucciones originales.
 */
int main(int argc, char **argv)
{
    porygon_machine_program_t program = {0};
    porygon_schedule_t schedule = {0};
    porygon_program_parse_result_t parsed;
    porygon_schedule_result_t result;
    int status = 1;
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts("Uso: porygon-schedule entrada.asm salida.bundles [salida.mem]");
        return 0;
    }
    if (argc != 3 && argc != 4) {
        fprintf(stderr, "Uso: %s entrada.asm salida.bundles [salida.mem]\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[1], argv[2]) == 0 || (argc == 4 &&
        (strcmp(argv[1], argv[3]) == 0 || strcmp(argv[2], argv[3]) == 0))) {
        fputs("Las rutas de entrada y salida deben ser diferentes\n", stderr);
        return 2;
    }
    parsed = porygon_parse_program_file(argv[1], &program);
    if (parsed.status != PORYGON_PROGRAM_OK) {
        fprintf(stderr, "%s:%zu:%zu: error de parseo PORYGON (programa=%d, instrucción=%d)\n",
                argv[1], parsed.line, parsed.column, (int)parsed.status, (int)parsed.instruction_status);
        return 1;
    }
    result = porygon_schedule_program(&program, &schedule);
    if (result.status != PORYGON_SCHEDULE_OK) {
        print_error(argv[1], &program, result);
        goto done;
    }
    if (write_file(argv[2], &program, &schedule, false)) goto done;
    if (argc == 4 && write_file(argv[3], &program, &schedule, true)) goto done;
    printf("%zu instrucciones, %zu bundles verificados\n", program.instruction_count, schedule.bundle_count);
    status = 0;
done:
    porygon_schedule_free(&schedule);
    porygon_machine_program_free(&program);
    return status;
}
