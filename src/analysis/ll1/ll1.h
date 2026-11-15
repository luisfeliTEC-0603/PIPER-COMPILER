#ifndef PIPER_LL1_H
#define PIPER_LL1_H

/**
 * @file ll1.h
 * @brief Tabla predictiva y consultas de autocompletado LL(1) de Piper.
 */

#include <stdio.h>

/** Resultado observable por la CLI de pipoc. */
typedef enum {
    LL1_ANALYSIS_ACCEPTED,
    LL1_ANALYSIS_REJECTED,
    LL1_ANALYSIS_IO_ERROR,
    LL1_ANALYSIS_INTERNAL_ERROR
} Ll1AnalysisResult;

/**
 * @brief Imprime las celdas definidas de la tabla predictiva.
 * @param output Flujo donde se imprime la tabla.
 */
void ll1_print_predictive_table(FILE *output);

/**
 * @brief Imprime los terminales válidos al final de un prefijo Piper.
 * @param source_path Ruta temporal o persistente que contiene el prefijo.
 * @param output Un terminal por línea, sin tabla ni traza adicional.
 * @return Estado del análisis del prefijo o del acceso al archivo.
 */
Ll1AnalysisResult ll1_expected_file(const char *source_path, FILE *output);

#endif
