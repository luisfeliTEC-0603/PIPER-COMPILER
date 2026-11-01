#ifndef PORYGON_SCHEDULER_INTERNAL_H
#define PORYGON_SCHEDULER_INTERNAL_H

#include "scheduler.h"
#include "porygon_assembler/encoder.h"

/** Construye un diagnóstico sin asignar memoria. */
porygon_schedule_result_t schedule_result(porygon_schedule_status_t status,
    size_t instruction, size_t bundle, size_t slot, const char *message);

/** Valida el contrato del parser y los accesos constantes demostrables por bloque. */
porygon_schedule_result_t schedule_validate_program(const porygon_machine_program_t *program);

/** Describe operandos desde la instrucción, sin confiar en máscaras almacenadas. */
porygon_machine_instruction_t schedule_describe(const porygon_machine_instruction_t *instruction);

/** Distancia issue a RF de un resultado; no sustituye consultar si la operación escribe. */
size_t schedule_wb_latency(const porygon_instruction_t *instruction);

/**
 * Codifica una operación y su literal, todavía sin resolver destinos simbólicos.
 * Las salidas se consumen únicamente si el resultado es ENCODE_OK.
 */
porygon_encode_result_t schedule_encode(const porygon_instruction_t *instruction,
    unsigned int pipe, uint16_t *word, uint16_t *literal);

#endif
