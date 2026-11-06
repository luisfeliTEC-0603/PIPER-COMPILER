#ifndef PIPER_DIAGNOSTIC_H
#define PIPER_DIAGNOSTIC_H

/**
 * @file diagnostic.h
 * @brief Registro estructurado de errores del frontend para clientes como el IDE.
 *
 * No decide qué es un error: lexer, Bison y semántica conservan esa lógica.
 * El registro se activa solo para una revisión solicitada por la CLI. Cada
 * proceso de pipoc analiza un archivo; sus diagnósticos no son estado del IDE.
 * Las ubicaciones son desde uno y en bytes UTF-8, con extremo final exclusivo,
 * igual que yylloc actualizado por el lexer y SourceLocation del AST.
 * Un rango vacío puede señalar la inserción de un terminador ausente.
 */
#include "ast.h"
#include <stdarg.h>

typedef enum {
    DIAGNOSTIC_LEXICAL,
    DIAGNOSTIC_SYNTAX,
    DIAGNOSTIC_SEMANTIC
} DiagnosticPhase;

/** Activa una colección nueva; source permanece válido hasta diagnostics_end. */
void diagnostics_begin(const char *source);
/** Libera los mensajes y desactiva la colección. */
void diagnostics_end(void);
/** Registra un mensaje; si no hay una colección activa no hace nada. */
void diagnostics_record(DiagnosticPhase phase, SourceLocation location,
                        const char *format, ...);
/** Variante para el emisor semántico, que ya recibe argumentos variádicos. */
void diagnostics_recordv(DiagnosticPhase phase, SourceLocation location,
                         const char *format, va_list arguments);
/** Escribe el protocolo JSON versión 1; devuelve cero ante error de memoria/E/S. */
int diagnostics_write_json(FILE *output, int valid);

#endif
