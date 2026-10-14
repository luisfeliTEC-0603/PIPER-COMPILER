/**
 * @file binary_format.h
 * @brief Contenedor ejecutable binario de PORYGON.
 * @details Define el encabezado versión 1, su serialización little-endian y
 * la conversión validada de la sección de texto a formato $readmemh.
 */

#ifndef PORYGON_BINARY_FORMAT_H
#define PORYGON_BINARY_FORMAT_H

#include "scheduler/scheduler.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define PORYGON_BINARY_MAGIC UINT32_C(0x504F5259)
#define PORYGON_BINARY_VERSION UINT32_C(1)
#define PORYGON_BINARY_HEADER_SIZE UINT32_C(32)
#define PORYGON_BINARY_ENTRY_PC UINT32_C(0)
#define PORYGON_BINARY_DATA_BASE UINT32_C(0x00010000)
#define PORYGON_BINARY_MAX_TEXT_SIZE UINT32_C(65536)

typedef enum {
    PORYGON_BINARY_OK,
    PORYGON_BINARY_INVALID_ARGUMENT,
    PORYGON_BINARY_INVALID_SCHEDULE,
    PORYGON_BINARY_INVALID_HEADER,
    PORYGON_BINARY_TRUNCATED,
    PORYGON_BINARY_TRAILING_DATA,
    PORYGON_BINARY_OUT_OF_MEMORY,
    PORYGON_BINARY_IO_ERROR
} porygon_binary_status_t;

typedef struct {
    uint32_t entry_pc;
    uint32_t data_base;
    uint32_t data_init_size;
    uint32_t bss_size;
    uint64_t *bundles;
    size_t bundle_count;
} porygon_binary_image_t;

/**
 * @brief Escribe un calendario verificado como ejecutable PORYGON versión 1.
 * @param file Archivo binario abierto para escritura.
 * @param program Programa máquina que dio origen al calendario.
 * @param schedule Calendario VLIW final.
 * @return Estado de la serialización.
 */
porygon_binary_status_t porygon_binary_write(
    FILE *file, const porygon_machine_program_t *program,
    const porygon_schedule_t *schedule);

/**
 * @brief Lee y valida por completo un ejecutable PORYGON.
 * @param file Archivo binario abierto para lectura.
 * @param image Imagen inicialmente vacía que recibe los bundles.
 * @return Estado del parseo; en error image permanece vacío.
 */
porygon_binary_status_t porygon_binary_read(
    FILE *file, porygon_binary_image_t *image);

/**
 * @brief Emite la sección de texto de una imagen binaria como $readmemh.
 * @param file Archivo de texto abierto para escritura.
 * @param image Imagen binaria validada.
 * @return Estado de la escritura.
 */
porygon_binary_status_t porygon_binary_write_mem(
    FILE *file, const porygon_binary_image_t *image);

/**
 * @brief Libera los bundles de una imagen y la deja en cero.
 * @param image Imagen que deja de utilizarse; puede ser NULL.
 */
void porygon_binary_image_free(porygon_binary_image_t *image);

/**
 * @brief Devuelve una descripción estable de un estado binario.
 * @param status Estado que se describirá.
 * @return Cadena estática que no debe liberarse.
 */
const char *porygon_binary_status_message(porygon_binary_status_t status);

#endif
