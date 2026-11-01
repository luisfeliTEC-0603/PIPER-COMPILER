#ifndef PORYGON_SCHEDULER_H
#define PORYGON_SCHEDULER_H

#include "porygon_assembler/machine_program.h"
#include <stdio.h>

enum {
    PORYGON_BUNDLE_SLOTS = 4,
    PORYGON_BRANCH_NOP_BUNDLES = 2,
    PORYGON_AUTH_TIMEOUT_CYCLES = 16,
    PORYGON_MAX_BUNDLES = 8192
};

typedef enum {
    PORYGON_SLOT_NOP,
    PORYGON_SLOT_INSTRUCTION,
    PORYGON_SLOT_LITERAL
} porygon_slot_kind_t;

/**
 * Posición física del bundle: operación, literal o NOP de relleno.
 * instruction_index enlaza la operación con MachineProgram; en un literal
 * identifica a su instrucción dueña, y en relleno no se consulta.
 * pipe solo selecciona ALU para MATH; word contiene los 16 bits finales.
 */
typedef struct {
    porygon_slot_kind_t kind;
    size_t instruction_index;
    unsigned int pipe;
    uint16_t word;
} porygon_scheduled_slot_t;

/**
 * Cuatro slots físicos, no cuatro unidades fijas.
 * Una instrucción inmediata ocupa dos posiciones adyacentes de este arreglo.
 */
typedef struct {
    porygon_scheduled_slot_t slots[PORYGON_BUNDLE_SLOTS];
} porygon_bundle_t;

/**
 * Calendario propietario de bundles y posiciones finales de etiquetas.
 * label_bundles conserva los mismos índices de etiquetas que MachineProgram.
 * El programa original debe seguir vivo para verificar o imprimir sus instrucciones.
 */
typedef struct {
    porygon_bundle_t *bundles;
    size_t bundle_count;
    size_t *label_bundles;
    size_t label_count;
} porygon_schedule_t;

typedef enum {
    PORYGON_SCHEDULE_OK,
    PORYGON_SCHEDULE_INVALID_PROGRAM,
    PORYGON_SCHEDULE_INVALID_INSTRUCTION,
    PORYGON_SCHEDULE_INVALID_ADDRESS,
    PORYGON_SCHEDULE_TOO_LARGE,
    PORYGON_SCHEDULE_OUT_OF_MEMORY,
    PORYGON_SCHEDULE_INVALID_BUNDLE,
    PORYGON_SCHEDULE_DEPENDENCY,
    PORYGON_SCHEDULE_RESOURCE,
    PORYGON_SCHEDULE_CONTROL,
    PORYGON_SCHEDULE_IO_ERROR
} porygon_schedule_status_t;

/**
 * Error atribuible al ASM original y, cuando existe, a su posición calendarizada.
 * SIZE_MAX indica ubicación no aplicable; message es texto estático y no se libera.
 */
typedef struct {
    porygon_schedule_status_t status;
    size_t instruction_index;
    size_t bundle;
    size_t slot;
    const char *message;
} porygon_schedule_result_t;


/**
 * Calendariza sin modificar el programa original. out debe estar vacío.
 * Si falla, out queda intacto; si funciona, se libera con porygon_schedule_free.
 */
porygon_schedule_result_t porygon_schedule_program(
    const porygon_machine_program_t *program, porygon_schedule_t *out);

/** Comprueba codificación, preservación, dependencias, recursos y control sin replanificar. */
porygon_schedule_result_t porygon_verify_schedule(
    const porygon_machine_program_t *program, const porygon_schedule_t *schedule);


/** Escribe LABEL/BUNDLE/SLOT/END_BUNDLE después de verificar la salida. */
porygon_schedule_result_t porygon_write_bundles(
    FILE *file, const porygon_machine_program_t *program,
    const porygon_schedule_t *schedule);

/** Escribe un hexadecimal de 64 bits por línea después de verificar la salida. */
porygon_schedule_result_t porygon_write_mem(
    FILE *file, const porygon_machine_program_t *program,
    const porygon_schedule_t *schedule);

/** Libera los arreglos y deja el calendario vacío. */
void porygon_schedule_free(porygon_schedule_t *schedule);

#endif
