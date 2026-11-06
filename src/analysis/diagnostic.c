/**
 * @file diagnostic.c
 * @brief Colección y serialización JSON, sin reglas de validación del lenguaje.
 */
#include "diagnostic.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    DiagnosticPhase phase;
    SourceLocation location;
    char *message;
} DiagnosticEntry;

static DiagnosticEntry *entries;
static size_t count, capacity;
static const char *source_path;
static int active, allocation_failed;

void diagnostics_end(void)
{
    for (size_t index = 0; index < count; ++index) free(entries[index].message);
    free(entries);
    entries = NULL;
    count = capacity = 0;
    source_path = NULL;
    active = allocation_failed = 0;
}

void diagnostics_begin(const char *source)
{
    diagnostics_end();
    source_path = source;
    active = 1;
}

void diagnostics_recordv(DiagnosticPhase phase, SourceLocation location,
                         const char *format, va_list arguments)
{
    va_list copy;
    int length;
    char *message;
    if (!active || allocation_failed) return;
    /* Un carácter inválido provoca también el rechazo de Bison en el mismo
     * token. El JSON conserva el error léxico original, sin duplicar la cascada.
     * Los mensajes humanos de ambos analizadores siguen disponibles en stderr. */
    if (phase == DIAGNOSTIC_SYNTAX && count != 0 &&
        entries[count - 1].phase == DIAGNOSTIC_LEXICAL &&
        entries[count - 1].location.first_line == location.first_line &&
        entries[count - 1].location.first_column == location.first_column)
        return;
    va_copy(copy, arguments);
    length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (length < 0) { allocation_failed = 1; return; }
    message = malloc((size_t)length + 1);
    if (message == NULL) { allocation_failed = 1; return; }
    va_copy(copy, arguments);
    (void)vsnprintf(message, (size_t)length + 1, format, copy);
    va_end(copy);
    if (count == capacity) {
        size_t next_capacity = capacity == 0 ? 16 : capacity * 2;
        DiagnosticEntry *resized = realloc(entries, next_capacity * sizeof(*entries));
        if (resized == NULL) {
            free(message);
            allocation_failed = 1;
            return;
        }
        entries = resized;
        capacity = next_capacity;
    }
    entries[count++] = (DiagnosticEntry){phase, location, message};
}

void diagnostics_record(DiagnosticPhase phase, SourceLocation location,
                        const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    diagnostics_recordv(phase, location, format, arguments);
    va_end(arguments);
}

/** Escapa JSON y conserva UTF-8 válido. Un byte inválido del lexer se representa
 * como U+FFFD para que un error de codificación no invalide el propio reporte. */
static void write_string(FILE *output, const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;
    fputc('"', output);
    while (*cursor) {
        unsigned char byte = *cursor;
        if (byte == '"' || byte == '\\') {
            fputc('\\', output);
            fputc(byte, output);
            cursor++;
        } else if (byte < 32) {
            fprintf(output, "\\u%04x", byte);
            cursor++;
        } else if (byte < 128) {
            fputc(byte, output);
            cursor++;
        } else {
            size_t length = byte >= 0xc2 && byte <= 0xdf ? 2 :
                            byte >= 0xe0 && byte <= 0xef ? 3 :
                            byte >= 0xf0 && byte <= 0xf4 ? 4 : 0;
            size_t index = 1;
            while (index < length && cursor[index] >= 0x80 && cursor[index] <= 0xbf) index++;
            int valid = length != 0 && index == length;
            if (valid && length == 3)
                valid = (byte != 0xe0 || cursor[1] >= 0xa0) &&
                        (byte != 0xed || cursor[1] < 0xa0);
            if (valid && length == 4)
                valid = (byte != 0xf0 || cursor[1] >= 0x90) &&
                        (byte != 0xf4 || cursor[1] < 0x90);
            if (valid) {
                (void)fwrite(cursor, 1, length, output);
                cursor += length;
            } else {
                fputs("\\ufffd", output);
                cursor++;
            }
        }
    }
    fputc('"', output);
}

int diagnostics_write_json(FILE *output, int valid)
{
    static const char *const phases[] = {"lexical", "syntax", "semantic"};
    if (allocation_failed) return 0;
    fputs("{\"version\":1,\"column_unit\":\"utf8-byte\",\"source\":", output);
    write_string(output, source_path == NULL ? "" : source_path);
    fprintf(output, ",\"valid\":%s,\"diagnostics\":[", valid ? "true" : "false");
    for (size_t index = 0; index < count; ++index) {
        const DiagnosticEntry *entry = &entries[index];
        SourceLocation span = entry->location;
        if (index) fputc(',', output);
        fprintf(output, "{\"phase\":\"%s\",\"severity\":\"error\",\"message\":", phases[entry->phase]);
        write_string(output, entry->message);
        fprintf(output,
                ",\"start\":{\"line\":%d,\"column\":%d},"
                "\"end\":{\"line\":%d,\"column\":%d}}",
                span.first_line, span.first_column, span.last_line, span.last_column);
    }
    fputs("]}\n", output);
    return !ferror(output);
}
