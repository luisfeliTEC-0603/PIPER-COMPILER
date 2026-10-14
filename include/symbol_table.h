#ifndef PIPER_SYMBOL_TABLE_H
#define PIPER_SYMBOL_TABLE_H

#include <stddef.h>
#include <stdio.h>

#define SYMBOL_NAME_MAX 64
#define SYMBOL_MEMORY_SIZE 65536
#define SYMBOL_GLOBAL_BASE 0
#define SYMBOL_DATA_BASE_ADDRESS 0x10000
#define SYMBOL_WORD_SIZE 4
#define SYMBOL_STACK_SIZE 4096
#define SYMBOL_GLOBAL_MEMORY_SIZE (SYMBOL_MEMORY_SIZE - SYMBOL_STACK_SIZE)
/* Presupuesto por llamada para retorno, registros guardados y temporales. */
#define SYMBOL_STACK_CALL_RESERVE 64

typedef enum {
    SYMBOL_VAR,
    SYMBOL_PARAM,
    SYMBOL_FUNC
} SymbolClass;

typedef enum {
    TYPE_INT, /* categoria para expresiones; consultar declared_type para ancho/signo */
    TYPE_BOOL,
    TYPE_ARRAY,
    TYPE_VOID
} DataType;

typedef enum {
    PIPER_TYPE_BOOL,
    PIPER_TYPE_B8,
    PIPER_TYPE_UB8,
    PIPER_TYPE_B16,
    PIPER_TYPE_UB16,
    PIPER_TYPE_B32,
    PIPER_TYPE_UB32,
    PIPER_TYPE_NONE
} PiperType;

typedef struct Scope Scope;

typedef struct Symbol {
    char name[SYMBOL_NAME_MAX];
    SymbolClass symbol_class;
    DataType type;
    /* Tipo exacto declarado; en matrices corresponde al elemento. */
    PiperType declared_type;
    int is_array;
    int array_rows;
    int array_cols;
    /* Espacio ocupado en el ámbito; un parámetro matriz solo ocupa el puntero. */
    int size_bytes;
    /* Para un parámetro matriz: ranura del puntero, no de los elementos. */
    int address_or_offset;
    DataType return_type;
    int param_count;
    struct Symbol **params;
    int total_stack_frame_size;
    int call_reserve_size; /* Espacio para llamadas, índices y stores pendientes */
    int line_number;
    Scope *function_scope;
    struct Symbol *next;
} Symbol;

struct Scope {
    int scope_id;
    int is_global;
    Symbol *symbols_head;
    Scope *parent;
    int current_offset;
    int max_offset;
    Scope *next_all;
};

typedef struct SymbolTable {
    Scope *current_scope;
    Scope *global_scope;
    Scope *all_scopes;
    int next_global_address;
    int scope_counter;
    int global_call_reserve_size;
} SymbolTable;

SymbolTable *symbol_table_create(void);
void symbol_table_destroy(SymbolTable *table);
/* Devuelve 1 si creó el ámbito; 0 si no pudo hacerlo. */
int symbol_table_open_scope(SymbolTable *table);
void symbol_table_close_scope(SymbolTable *table);

Symbol *symbol_table_insert(SymbolTable *table, const char *name,
                            SymbolClass symbol_class, DataType type, PiperType declared_type,
                            int rows, int cols, int line);

Symbol *symbol_table_lookup_current_scope(SymbolTable *table, const char *name);

Symbol *symbol_table_lookup(SymbolTable *table, const char *name);

int symbol_table_set_function_frame(SymbolTable *table, Symbol *function);

Scope *symbol_table_current_scope(SymbolTable *table);

void symbol_table_print(FILE *output, const SymbolTable *table);

void symbol_table_print_scope(FILE *output, const Scope *scope);

#endif
