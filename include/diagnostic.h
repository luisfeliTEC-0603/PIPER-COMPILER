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
#include <stdio.h>

typedef enum {
    DIAGNOSTIC_LEXICAL,
    DIAGNOSTIC_SYNTAX,
    DIAGNOSTIC_SEMANTIC
} DiagnosticPhase;

typedef enum {
    DIAGNOSTIC_CODE_GENERIC = 0,
    DIAGNOSTIC_CODE_MISSING_DOT,
    DIAGNOSTIC_CODE_MISSING_CLOSE_BRACE,
    DIAGNOSTIC_CODE_MISSING_CLOSE_PAREN,
    DIAGNOSTIC_CODE_MISSING_CLOSE_BRACKET,
    DIAGNOSTIC_CODE_UNCLOSED_BLOCK_COMMENT,
    DIAGNOSTIC_CODE_MISSING_MAIN,
    DIAGNOSTIC_CODE_MISSING_OUTPUT,
    DIAGNOSTIC_CODE_MAIN_HAS_PARAMS,
    DIAGNOSTIC_CODE_UNINITIALIZED_LOCAL,
    DIAGNOSTIC_CODE_TYPE_MISMATCH,
    DIAGNOSTIC_CODE_UNKNOWN_CHARACTER,
    DIAGNOSTIC_CODE_INTEGER_OUT_OF_RANGE,
    DIAGNOSTIC_CODE_RESERVED_INTRINSIC,
    DIAGNOSTIC_CODE_DUPLICATE_DECLARATION,
    DIAGNOSTIC_CODE_OUT_OF_RANGE_INDEX,
    DIAGNOSTIC_CODE_BREAK_OUTSIDE_LOOP,
    DIAGNOSTIC_CODE_DIVISION_BY_ZERO,
    DIAGNOSTIC_CODE_IMPORT_UNRESOLVED,
    DIAGNOSTIC_CODE_BLOCK_SCOPE_WITHOUT_ROUTINE,
    DIAGNOSTIC_CODE_UNDECLARED_IDENTIFIER,
    DIAGNOSTIC_CODE_MISSING_OPEN_BRACE,
    DIAGNOSTIC_CODE_MISSING_OPEN_BRACKET,
} DiagnosticCode;

const char *diagnostic_code_name(DiagnosticCode code);

/** Activa una colección nueva; source permanece válido hasta diagnostics_end. */
void diagnostics_begin(const char *source);
/** Libera los mensajes y desactiva la colección. */
void diagnostics_end(void);
/** Registra un mensaje; si no hay una colección activa no hace nada. */
void diagnostics_record(DiagnosticPhase phase, DiagnosticCode code,
                        SourceLocation location, const char *format, ...);
/** Variante para el emisor semántico, que ya recibe argumentos variádicos. */
void diagnostics_recordv(DiagnosticPhase phase, DiagnosticCode code,
                         SourceLocation location, const char *format, va_list arguments);
/** Escribe el protocolo JSON versión 1; devuelve cero ante error de memoria/E/S. */
int diagnostics_write_json(FILE *output, int valid);

/** Cantidad de diagnósticos registrados desde diagnostics_begin. */
size_t diagnostics_count(void);

#endif
