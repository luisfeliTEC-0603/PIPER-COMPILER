#include "symbol_table.h"

#include <stdlib.h>
#include <string.h>

static int align_word(int value)
{
    return (value + SYMBOL_WORD_SIZE - 1) & ~(SYMBOL_WORD_SIZE - 1);
}

static int symbol_size(DataType type, int rows, int cols)
{
    long long elements = 1;

    if (type == TYPE_ARRAY) {
        elements = (long long)(rows > 0 ? rows : 1)
                 * (long long)(cols > 0 ? cols : 1);
    }

    if (elements > SYMBOL_MEMORY_SIZE / SYMBOL_WORD_SIZE) {
        return -1;
    }
    return (int)(elements * SYMBOL_WORD_SIZE);
}

static Scope *scope_create(int scope_id, int is_global, Scope *parent)
{
    Scope *scope = calloc(1, sizeof(*scope));
    if (scope == NULL) {
        return NULL;
    }
    scope->scope_id = scope_id;
    scope->is_global = is_global;
    scope->parent = parent;
    return scope;
}

static void symbols_destroy(Symbol *symbol)
{
    while (symbol != NULL) {
        Symbol *next = symbol->next;
        free(symbol->params);
        free(symbol);
        symbol = next;
    }
}

static Symbol *lookup_scope(const Scope *scope, const char *name)
{
    Symbol *symbol;

    for (symbol = scope->symbols_head; symbol != NULL; symbol = symbol->next) {
        if (strcmp(symbol->name, name) == 0) {
            return symbol;
        }
    }
    return NULL;
}

SymbolTable *symbol_table_create(void)
{
    SymbolTable *table = calloc(1, sizeof(*table));
    if (table == NULL) {
        return NULL;
    }

    table->global_scope = scope_create(0, 1, NULL);
    if (table->global_scope == NULL) {
        free(table);
        return NULL;
    }
    table->current_scope = table->global_scope;
    table->all_scopes = table->global_scope;
    table->next_global_address = SYMBOL_GLOBAL_BASE;
    table->scope_counter = 1;
    return table;
}

void symbol_table_destroy(SymbolTable *table)
{
    Scope *scope;

    if (table == NULL) {
        return;
    }
    scope = table->all_scopes;
    while (scope != NULL) {
        Scope *next = scope->next_all;
        symbols_destroy(scope->symbols_head);
        free(scope);
        scope = next;
    }
    free(table);
}

int symbol_table_open_scope(SymbolTable *table)
{
    Scope *scope;

    if (table == NULL || table->current_scope == NULL) {
        return 0;
    }
    scope = scope_create(table->scope_counter, 0, table->current_scope);
    if (scope == NULL) {
        return 0;
    }
    table->scope_counter++;
    scope->next_all = table->all_scopes;
    table->all_scopes = scope;
    table->current_scope = scope;
    return 1;
}

void symbol_table_close_scope(SymbolTable *table)
{
    Scope *scope;

    if (table == NULL || table->current_scope == table->global_scope) {
        return;
    }
    scope = table->current_scope;
    table->current_scope = scope->parent;
}

Symbol *symbol_table_lookup_current_scope(SymbolTable *table, const char *name)
{
    if (table == NULL || table->current_scope == NULL || name == NULL) {
        return NULL;
    }
    return lookup_scope(table->current_scope, name);
}

Symbol *symbol_table_lookup(SymbolTable *table, const char *name)
{
    Scope *scope;

    if (table == NULL || name == NULL) {
        return NULL;
    }
    /* Visibilidad léxica: comenzar en el ámbito actual y subir por todos sus
     * padres. El primer símbolo encontrado permite sombrear declaraciones.
     * all_scopes conserva los ámbitos para liberar/imprimir, no para buscar:
     * recorrerlo haría visibles variables locales de funciones hermanas. */
    for (scope = table->current_scope; scope != NULL; scope = scope->parent) {
        Symbol *symbol = lookup_scope(scope, name);
        if (symbol != NULL) {
            return symbol;
        }
    }
    return NULL;
}

Symbol *symbol_table_insert(SymbolTable *table, const char *name,
                            SymbolClass symbol_class, DataType type,
                            int rows, int cols, int line)
{
    Symbol *symbol;
    int size;

    if (table == NULL || table->current_scope == NULL || name == NULL
        || symbol_table_lookup_current_scope(table, name) != NULL) {
        return NULL;
    }

    symbol = calloc(1, sizeof(*symbol));
    if (symbol == NULL) {
        return NULL;
    }
    strncpy(symbol->name, name, SYMBOL_NAME_MAX - 1);
    symbol->symbol_class = symbol_class;
    symbol->type = type;
    symbol->return_type = type;
    symbol->array_rows = rows;
    symbol->array_cols = cols;
    symbol->is_array = type == TYPE_ARRAY;
    symbol->line_number = line;

    if (symbol_class == SYMBOL_FUNC) {
        symbol->size_bytes = 0;
        symbol->address_or_offset = -1;
    } else {
        size = symbol_size(type, rows, cols);
        if (size < 0) {
            free(symbol);
            return NULL;
        }
        /* La matriz vive en el ámbito del llamador. El parámetro solo guarda
         * su dirección base; conservamos filas y columnas para describirla. */
        if (symbol_class == SYMBOL_PARAM && type == TYPE_ARRAY) {
            size = SYMBOL_WORD_SIZE;
        }
        symbol->size_bytes = size;
        if (table->current_scope->is_global) {
            table->next_global_address = align_word(table->next_global_address);
            if (table->next_global_address > SYMBOL_MEMORY_SIZE - size) {
                free(symbol);
                return NULL;
            }
            symbol->address_or_offset = table->next_global_address;
            table->next_global_address += size;
        } else {
            table->current_scope->current_offset =
                align_word(table->current_scope->current_offset);
            symbol->address_or_offset = table->current_scope->current_offset;
            table->current_scope->current_offset += size;
        }
    }

    symbol->next = table->current_scope->symbols_head;
    table->current_scope->symbols_head = symbol;
    return symbol;
}

int symbol_table_set_function_frame(SymbolTable *table, Symbol *function)
{
    if (table == NULL || table->current_scope == NULL || function == NULL) {
        return 0;
    }
    function->total_stack_frame_size =
        align_word(table->current_scope->current_offset);
    return 1;
}

Scope *symbol_table_current_scope(SymbolTable *table)
{
    return table == NULL ? NULL : table->current_scope;
}

static const char *symbol_class_name(SymbolClass symbol_class)
{
    switch (symbol_class) {
    case SYMBOL_VAR:
        return "var";
    case SYMBOL_PARAM:
        return "param";
    case SYMBOL_FUNC:
        return "func";
    }
    return "unknown";
}

static const char *data_type_name(DataType type)
{
    switch (type) {
    case TYPE_INT:
        return "int";
    case TYPE_BOOL:
        return "bool";
    case TYPE_ARRAY:
        return "array";
    case TYPE_VOID:
        return "void";
    }
    return "unknown";
}

static void print_memory_address(FILE *output, const Scope *scope,
                                 const Symbol *symbol)
{
    if (symbol->symbol_class == SYMBOL_FUNC) {
        fprintf(output, "etiqueta %-20s", symbol->name);
    } else if (scope->is_global) {
        fprintf(output, "0x%04X                 ",
                (unsigned int)symbol->address_or_offset);
    } else {
        fprintf(output, "r7 + 0x%04X             ",
                (unsigned int)symbol->address_or_offset);
    }
}

void symbol_table_print_scope(FILE *output, const Scope *scope)
{
    const Symbol *symbol;
    char type_label[40];

    if (output == NULL || scope == NULL) {
        return;
    }
    fprintf(output, "\nScope %d%s\n", scope->scope_id,
            scope->is_global ? " (global)" : " (local, base r7)");
    fprintf(output, "  %-18s %-10s %-16s %-5s %s\n",
            "nombre", "clase", "tipo", "bytes", "mem_dir");
    fprintf(output, "  %-18s %-10s %-16s %-5s %s\n",
            "------------------", "----------", "----------------", "-----",
            "------------------------");
    for (symbol = scope->symbols_head; symbol != NULL; symbol = symbol->next) {
        if (symbol->is_array) {
            snprintf(type_label, sizeof(type_label), "array[%dx%d]",
                     symbol->array_rows, symbol->array_cols);
        } else {
            snprintf(type_label, sizeof(type_label), "%s",
                     data_type_name(symbol->type));
        }
        fprintf(output, "  %-18s %-10s %-16s %-5d ", symbol->name,
                symbol_class_name(symbol->symbol_class), type_label,
                symbol->size_bytes);
        print_memory_address(output, scope, symbol);
        if (symbol->symbol_class == SYMBOL_PARAM && symbol->is_array) {
            fputs("(ranura de puntero)", output);
        }
        fputc('\n', output);
    }
}

void symbol_table_print(FILE *output, const SymbolTable *table)
{
    const Scope *scope;

    if (output == NULL || table == NULL) {
        return;
    }
    fprintf(output, "\n================================ Tabla de simbolos ================================\n");
    for (scope = table->all_scopes; scope != NULL; scope = scope->next_all) {
        symbol_table_print_scope(output, scope);
    }
        fprintf(output, "\nSiguiente direccion global disponible: 0x%04X\n",
            (unsigned int)table->next_global_address);
        fprintf(output, "===================================================================================\n");
}
