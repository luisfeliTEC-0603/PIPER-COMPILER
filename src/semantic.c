#include "semantic.h"
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* Relaciona cada símbolo de arreglo con su declaración original. La tabla
 * conserva filas/columnas; el AST aporta tipo de elemento y número de índices.
 * La identidad del símbolo distingue nombres iguales en ámbitos diferentes.
 * Estos enlaces solo viven durante el análisis y no poseen sus dos punteros. */
typedef struct ArrayDeclaration {
    Symbol *symbol;
    AstNode *node;
    struct ArrayDeclaration *next;
} ArrayDeclaration;

typedef struct {
    unsigned long long magnitude;
    int negative;
} KnownValue;

typedef struct KnownInteger {
    Symbol *symbol;
    KnownValue value;
    struct KnownInteger *next;
} KnownInteger;

typedef struct InitializedSymbol {
    Symbol *symbol;
    struct InitializedSymbol *next;
} InitializedSymbol;

typedef struct RoutineUsage {
    Symbol *symbol;
    AstNode *declaration;
    long long peak_bytes;
    int state;
    struct RoutineUsage *next;
} RoutineUsage;

typedef struct CallUsage {
    RoutineUsage *caller;
    Symbol *callee;
    struct CallUsage *next;
} CallUsage;

/* Estado compartido por el recorrido recursivo. function identifica el retorno
 * esperado y loops cuenta los ciclos que contienen la sentencia actual. */
typedef struct {
    SymbolTable *table;
    ArrayDeclaration *arrays;
    KnownInteger *known;
    InitializedSymbol *initialized;
    AstNode *assignment_target;
    Symbol *function;
    RoutineUsage *routines, *current_routine;
    CallUsage *calls;
    int loops, errors;
    int trace_enabled;
    long long pending_codegen_bytes;
} Visitor;

/* Acumula el diagnóstico y permite seguir revisando el resto del árbol. */
static void error(Visitor *v, AstNode *n, const char *format, ...)
{
    va_list args;
    v->errors++;
    fprintf(stderr, "[Error Semántico] Línea %d: ", n ? n->location.first_line : 0);
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
}

/* Las aprobaciones van a stdout; los errores continúan en stderr. Vaciar
 * stdout conserva el orden cuando ambas salidas comparten la consola. */
static void success(Visitor *v, AstNode *n, const char *format, ...)
{
    va_list args;
    if (!v->trace_enabled) return;
    fprintf(stdout, "[Éxito Semántico] Línea %d [ámbito %d]: ",
            n ? n->location.first_line : 0, v->table->current_scope->scope_id);
    va_start(args, format);
    vfprintf(stdout, format, args);
    va_end(args);
    fputc('\n', stdout);
    fflush(stdout);
}

static const char *type_name(DataType type)
{
    switch (type) {
    case TYPE_INT: return "entero";
    case TYPE_BOOL: return "booleano";
    case TYPE_ARRAY: return "arreglo/matriz";
    case TYPE_VOID: return "none";
    }
    return "desconocido";
}

/* Mostrar el ámbito dueño del símbolo explica de dónde vino una búsqueda,
 * especialmente cuando hay bloques anidados o nombres sombreados. */
static Scope *symbol_scope(Visitor *v, Symbol *symbol)
{
    for (Scope *scope = v->table->current_scope; scope; scope = scope->parent)
        for (Symbol *s = scope->symbols_head; s; s = s->next)
            if (s == symbol) return scope;
    return NULL;
}

static void close_scope(Visitor *v, AstNode *n, int block)
{
    Scope *scope = v->table->current_scope;
    int closed = scope->scope_id;
    if (block && scope->parent && scope->max_offset > scope->parent->max_offset)
        scope->parent->max_offset = scope->max_offset;
    symbol_table_close_scope(v->table);
    success(v, n, "Ámbito %d cerrado; se continúa en el ámbito %d.",
            closed, v->table->current_scope->scope_id);
}

static DataType visit(Visitor *v, AstNode *n);
/* El orden importa: una declaración se vuelve visible cuando se visita. */
static void list(Visitor *v, AstNodeList *nodes)
{
    for (size_t i = 0; i < nodes->count; i++) visit(v, nodes->items[i]);
}
/* La tabla agrupa los anchos y signos de enteros de Piper como TYPE_INT. */
static DataType scalar(PiperType type)
{
    return type == PIPER_TYPE_BOOL ? TYPE_BOOL :
           type == PIPER_TYPE_NONE ? TYPE_VOID : TYPE_INT;
}

static int integer_type(PiperType type)
{
    return type >= PIPER_TYPE_B8 && type <= PIPER_TYPE_UB32;
}

static int integer_bits(PiperType type)
{
    if (type == PIPER_TYPE_B8 || type == PIPER_TYPE_UB8) return 8;
    if (type == PIPER_TYPE_B16 || type == PIPER_TYPE_UB16) return 16;
    return 32;
}

static int unsigned_type(PiperType type)
{
    return type == PIPER_TYPE_UB8 || type == PIPER_TYPE_UB16 || type == PIPER_TYPE_UB32;
}

static int fits_type(PiperType type, KnownValue value)
{
    if (!integer_type(type)) return 0;
    int bits = integer_bits(type);
    unsigned long long maximum = (1ULL << (bits - (unsigned_type(type) ? 0 : 1))) - 1;
    if (unsigned_type(type)) return !value.negative && value.magnitude <= maximum;
    return value.negative ? value.magnitude <= maximum + 1 : value.magnitude <= maximum;
}

static int fits_any_integer(KnownValue value)
{
    return value.negative ? fits_type(PIPER_TYPE_B32, value) :
           fits_type(PIPER_TYPE_UB32, value);
}

/* Un tipo fuente puede convertirse sin pérdida si todo su rango cabe en el destino. */
static int safe_widening(PiperType destination, PiperType source)
{
    if (!integer_type(destination) || !integer_type(source)) return 0;
    if (destination == source) return 1;
    int destination_bits = integer_bits(destination);
    int source_bits = integer_bits(source);
    if (unsigned_type(destination))
        return unsigned_type(source) && source_bits <= destination_bits;
    return unsigned_type(source) ? source_bits < destination_bits : source_bits <= destination_bits;
}
/* La API de ámbitos no devuelve estado; comprobar el cambio detecta OOM. */
static int open_scope(Visitor *v, AstNode *n, int block)
{
    Scope *before = v->table->current_scope;
    symbol_table_open_scope(v->table);
    if (before == v->table->current_scope) {
        error(v, n, "No se pudo abrir el ámbito.");
        return 0;
    }
    n->semantic_scope = v->table->current_scope;
    /* Un bloque comparte el marco de su rutina. Los bloques hermanos pueden
     * reutilizar posiciones, pues nunca están activos simultáneamente. */
    if (block) {
        v->table->current_scope->current_offset = before->current_offset;
        v->table->current_scope->max_offset = before->current_offset;
    }
    success(v, n, "Ámbito %d abierto, con padre en el ámbito %d.",
            v->table->current_scope->scope_id, before->scope_id);
    return 1;
}
/* Busca en el ámbito actual y sus padres léxicos. Los ámbitos de otras
 * funciones, incluido el de quien llama, no forman parte de esta cadena. */
static Symbol *lookup(Visitor *v, AstNode *n, const char *name)
{
    Symbol *s = symbol_table_lookup(v->table, name);
    if (!s) error(v, n, "El identificador '%s' no ha sido declarado en este ámbito.", name);
    else {
        Scope *owner = symbol_scope(v, s);
        /* Cada rutina tiene su propio r7. Sin un enlace al marco exterior,
         * una rutina anidada no puede acceder a locales de otra rutina. */
        if (s->symbol_class != SYMBOL_FUNC && !owner->is_global &&
            v->function && v->function->function_scope) {
            int same_frame = 0;
            for (Scope *scope = v->table->current_scope; scope; scope = scope->parent) {
                if (scope == owner) { same_frame = 1; break; }
                if (scope == v->function->function_scope) break;
            }
            if (!same_frame) {
                error(v, n, "'%s' pertenece al marco local de otra rutina; no se puede acceder desde '%s'.",
                      name, v->function->name);
                return NULL;
            }
        }
        success(v, n, "Búsqueda de '%s': %s encontrad%s en el ámbito %d (%s), declarado en la línea %d; tipo %s.",
                name, s->symbol_class == SYMBOL_FUNC ? "función" :
                      s->symbol_class == SYMBOL_PARAM ? "parámetro" : "variable",
                s->symbol_class == SYMBOL_PARAM ? "o" : "a", owner->scope_id, owner == v->table->current_scope ? "actual" :
                owner->is_global ? "global" : "padre", s->line_number, type_name(s->type));
    }
    return s;
}

static void free_known(KnownInteger *values)
{
    while (values) {
        KnownInteger *next = values->next;
        free(values);
        values = next;
    }
}

static void clear_known(Visitor *v)
{
    free_known(v->known);
    v->known = NULL;
}

static int is_initialized(InitializedSymbol *values, Symbol *symbol)
{
    for (; values; values = values->next)
        if (values->symbol == symbol) return 1;
    return 0;
}

static void free_initialized(InitializedSymbol *values)
{
    while (values) {
        InitializedSymbol *next = values->next;
        free(values);
        values = next;
    }
}

static void mark_initialized(Visitor *v, AstNode *n, Symbol *symbol)
{
    if (!symbol || is_initialized(v->initialized, symbol)) return;
    InitializedSymbol *item = malloc(sizeof(*item));
    if (!item) { error(v, n, "No se pudo conservar el estado de inicialización."); return; }
    item->symbol = symbol;
    item->next = v->initialized;
    v->initialized = item;
}

static InitializedSymbol *copy_initialized(Visitor *v, AstNode *n,
                                            InitializedSymbol *values)
{
    InitializedSymbol *copy = NULL;
    for (; values; values = values->next) {
        InitializedSymbol *item = malloc(sizeof(*item));
        if (!item) {
            free_initialized(copy);
            error(v, n, "No se pudo conservar el estado de inicialización.");
            return NULL;
        }
        item->symbol = values->symbol;
        item->next = copy;
        copy = item;
    }
    return copy;
}

static void intersect_initialized(InitializedSymbol **left, InitializedSymbol *right)
{
    while (*left) {
        if (is_initialized(right, (*left)->symbol)) left = &(*left)->next;
        else {
            InitializedSymbol *removed = *left;
            *left = removed->next;
            free(removed);
        }
    }
}

static void forget_scope_initializations(Visitor *v, Scope *scope)
{
    for (Symbol *symbol = scope->symbols_head; symbol; symbol = symbol->next) {
        InitializedSymbol **item = &v->initialized;
        while (*item && (*item)->symbol != symbol) item = &(*item)->next;
        if (*item) {
            InitializedSymbol *removed = *item;
            *item = removed->next;
            free(removed);
        }
    }
}

static KnownInteger *copy_known(Visitor *v, AstNode *n, KnownInteger *values)
{
    KnownInteger *copy = NULL;
    for (KnownInteger *item = values; item; item = item->next) {
        KnownInteger *next = malloc(sizeof(*next));
        if (!next) {
            free_known(copy);
            error(v, n, "No se pudo conservar el estado de los índices.");
            return NULL;
        }
        *next = *item;
        next->next = copy;
        copy = next;
    }
    return copy;
}

static int add_values(KnownValue left, KnownValue right, KnownValue *result)
{
    if (left.negative == right.negative) {
        if (left.magnitude > ULLONG_MAX - right.magnitude) return 0;
        result->magnitude = left.magnitude + right.magnitude;
        result->negative = left.negative;
    } else if (left.magnitude >= right.magnitude) {
        result->magnitude = left.magnitude - right.magnitude;
        result->negative = left.negative;
    } else {
        result->magnitude = right.magnitude - left.magnitude;
        result->negative = right.negative;
    }
    if (!result->magnitude) result->negative = 0;
    return 1;
}

static int multiply_values(KnownValue left, KnownValue right, KnownValue *result)
{
    if (right.magnitude && left.magnitude > ULLONG_MAX / right.magnitude) return 0;
    result->magnitude = left.magnitude * right.magnitude;
    result->negative = result->magnitude && (left.negative != right.negative);
    return 1;
}

static int known_integer(Visitor *v, AstNode *n, KnownValue *value)
{
    if (!n) return 0;
    if (n->kind == AST_INTEGER_LITERAL) {
        value->magnitude = n->data.integer_literal.value;
        value->negative = 0;
        return 1;
    }
    if (n->kind == AST_IDENTIFIER_EXPRESSION) {
        Symbol *symbol = symbol_table_lookup(v->table, n->data.identifier.name);
        for (KnownInteger *item = v->known; item; item = item->next)
            if (item->symbol == symbol) { *value = item->value; return 1; }
    }
    if (n->kind == AST_UNARY_EXPRESSION && n->data.unary_expression.operator == AST_OP_NEGATE &&
        known_integer(v, n->data.unary_expression.operand, value)) {
        if (value->magnitude) value->negative = !value->negative;
        return 1;
    }
    if (n->kind == AST_BINARY_EXPRESSION) {
        KnownValue left, right;
        AstOperator op = n->data.binary_expression.operator;
        if (!known_integer(v, n->data.binary_expression.left, &left) ||
            !known_integer(v, n->data.binary_expression.right, &right)) return 0;
        if (op == AST_OP_ADD) return add_values(left, right, value);
        if (op == AST_OP_SUBTRACT) {
            if (right.magnitude) right.negative = !right.negative;
            return add_values(left, right, value);
        }
        if (op == AST_OP_MULTIPLY) return multiply_values(left, right, value);
        if (op == AST_OP_DIVIDE || op == AST_OP_REMAINDER) {
            if (!right.magnitude) return 0;
            value->magnitude = op == AST_OP_DIVIDE ? left.magnitude / right.magnitude :
                               left.magnitude % right.magnitude;
            value->negative = value->magnitude && left.negative != (op == AST_OP_DIVIDE && right.negative);
            return 1;
        }
        if (op == AST_OP_POWER && !right.negative) {
            KnownValue product = {1, 0};
            while (right.magnitude) {
                if (right.magnitude & 1ULL && !multiply_values(product, left, &product)) return 0;
                right.magnitude >>= 1;
                if (right.magnitude && !multiply_values(left, left, &left)) return 0;
            }
            *value = product;
            return 1;
        }
    }
    return 0;
}

static void set_known(Visitor *v, AstNode *n, Symbol *symbol, const KnownValue *value)
{
    if (!symbol) return;
    KnownInteger **item = &v->known;
    while (*item && (*item)->symbol != symbol) item = &(*item)->next;
    if (!value) {
        if (*item) {
            KnownInteger *old = *item;
            *item = old->next;
            free(old);
        }
        return;
    }
    if (!*item) {
        *item = calloc(1, sizeof(**item));
        if (!*item) { error(v, n, "No se pudo conservar el valor del índice."); return; }
        (*item)->symbol = symbol;
    }
    (*item)->value = *value;
}
/* Los arreglos requieren el mismo tipo de elemento, rango y tamaños. */
static int compatible(AstNode *a, AstNode *b)
{
    if (a->inferred_type != b->inferred_type) return 0;
    return a->inferred_type != TYPE_ARRAY ||
        (a->element_type == b->element_type && a->array_rank == b->array_rank &&
         a->array_rows == b->array_rows && a->array_cols == b->array_cols);
}

static int assignable(Visitor *v, AstNode *destination, AstNode *source);

static int literal_elements_fit(Visitor *v, AstNode *literal, PiperType element_type)
{
    AstNodeList *items = literal->data.array_literal.elements;
    for (size_t i = 0; i < items->count; i++) {
        AstNode *item = items->items[i];
        if (item->kind == AST_ARRAY_LITERAL) {
            if (!literal_elements_fit(v, item, element_type)) return 0;
        } else {
            AstNode expected = {0};
            expected.inferred_type = scalar(element_type);
            expected.piper_type = element_type;
            expected.has_piper_type = true;
            if (!assignable(v, &expected, item)) return 0;
        }
    }
    return 1;
}

static int assignable(Visitor *v, AstNode *destination, AstNode *source)
{
    if (!compatible(destination, source)) return 0;
    if (destination->inferred_type == TYPE_ARRAY) {
        if (source->kind == AST_ARRAY_LITERAL)
            return literal_elements_fit(v, source, destination->element_piper_type);
        return destination->has_element_piper_type && source->has_element_piper_type &&
               destination->element_piper_type == source->element_piper_type;
    }
    if (destination->inferred_type != TYPE_INT) return 1;
    KnownValue value;
    if (!destination->has_piper_type) return 0;
    if (known_integer(v, source, &value)) return fits_type(destination->piper_type, value);
    return source->has_piper_type && safe_widening(destination->piper_type, source->piper_type);
}

static int arithmetic_type(Visitor *v, AstNode *left, AstNode *right, PiperType *result)
{
    if (!left->has_piper_type && !right->has_piper_type) return 0;
    static const PiperType candidates[] = {
        PIPER_TYPE_B8, PIPER_TYPE_UB8, PIPER_TYPE_B16,
        PIPER_TYPE_UB16, PIPER_TYPE_B32, PIPER_TYPE_UB32
    };
    KnownValue left_value, right_value;
    int left_known = known_integer(v, left, &left_value);
    int right_known = known_integer(v, right, &right_value);
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        PiperType candidate = candidates[i];
        int left_fits = left->has_piper_type && integer_type(left->piper_type) ?
            safe_widening(candidate, left->piper_type) : left_known && fits_type(candidate, left_value);
        int right_fits = right->has_piper_type && integer_type(right->piper_type) ?
            safe_widening(candidate, right->piper_type) : right_known && fits_type(candidate, right_value);
        if (left_fits && right_fits) { *result = candidate; return 1; }
    }
    return 0;
}
/* Copia la información del símbolo resuelto. Para un arreglo, recuperar la
 * declaración evita perder el tipo de elemento o confundir vector y matriz.
 * También sirve para construir el tipo esperado de un parámetro de función. */
static void decorate_symbol(Visitor *v, AstNode *n, Symbol *s)
{
    n->inferred_type = s->type;
    n->has_piper_type = s->type != TYPE_ARRAY;
    n->has_element_piper_type = s->type == TYPE_ARRAY;
    if (n->has_piper_type) n->piper_type = s->declared_type;
    if (n->has_element_piper_type) n->element_piper_type = s->declared_type;
    n->array_rows = s->array_rows;
    n->array_cols = s->array_cols;
    for (ArrayDeclaration *a = v->arrays; a != NULL; a = a->next) {
        if (a->symbol == s) {
            n->element_type = a->node->element_type;
            n->array_rank = a->node->array_rank;
            break;
        }
    }
}

/* Variables y parámetros siguen las mismas reglas de nombres y dimensiones.
 * Solo se prohíben duplicados en el ámbito actual: sombrear un padre es válido. */
static Symbol *declare(Visitor *v, AstNode *n)
{
    int errors_before = v->errors;
    int parameter = n->kind == AST_PARAMETER;
    const char *name = parameter ? n->data.parameter.name : n->data.variable_declaration.name;
    PiperType type = parameter ? n->data.parameter.type : n->data.variable_declaration.type;
    AstNodeList *dims = parameter ? n->data.parameter.dimensions : n->data.variable_declaration.dimensions;
    int sizes[2] = {1, 1};
    if (dims->count > 2) error(v, n, "El arreglo '%s' admite una o dos dimensiones.", name);
    else if (dims->count) success(v, n, "'%s': cantidad de dimensiones válida (%zu).", name, dims->count);
    for (size_t i = 0; i < dims->count; i++) {
        AstNode *d = dims->items[i];
        visit(v, d);
        if (d->kind != AST_INTEGER_LITERAL || d->data.integer_literal.value == 0 ||
            d->data.integer_literal.value > SYMBOL_MEMORY_SIZE / SYMBOL_WORD_SIZE) {
            error(v, d, "La dimensión de '%s' debe ser un entero constante positivo válido.", name);
        } else {
            if (i < 2) sizes[i] = (int)d->data.integer_literal.value;
            success(v, d, "Dimensión %zu de '%s' aprobada: entero constante positivo %llu.",
                    i + 1, name, d->data.integer_literal.value);
        }
    }
    if (scalar(type) == TYPE_VOID) error(v, n, "'%s' no puede declararse con tipo none.", name);
    if (strlen(name) >= SYMBOL_NAME_MAX) {
        error(v, n, "El identificador '%s' excede la longitud máxima.", name);
        return NULL;
    }
    if (symbol_table_lookup_current_scope(v->table, name)) {
        error(v, n, "Declaración duplicada de '%s' en el mismo ámbito.", name);
        return NULL;
    }
    /* No reservar una forma provisional cuando las dimensiones son inválidas. */
    if (v->errors != errors_before) return NULL;
    success(v, n, "Nombre '%s' aprobado: sin declaración duplicada en el ámbito actual.", name);
    Symbol *s = symbol_table_insert(v->table, name, parameter ? SYMBOL_PARAM : SYMBOL_VAR,
        dims->count ? TYPE_ARRAY : scalar(type), type, sizes[0], sizes[1], n->location.first_line);
    if (!s) error(v, n, "No se pudo registrar '%s' o asignar su memoria.", name);
    else {
        n->inferred_type = s->type;
        n->element_type = scalar(type);
        n->has_piper_type = dims->count == 0;
        n->has_element_piper_type = dims->count != 0;
        if (n->has_piper_type) n->piper_type = type;
        if (n->has_element_piper_type) n->element_piper_type = type;
        n->array_rank = (int)dims->count;
        n->array_rows = sizes[0];
        n->array_cols = sizes[1];
        if (dims->count) {
            ArrayDeclaration *a = malloc(sizeof(*a));
            if (!a) error(v, n, "No se pudo conservar la declaración de '%s'.", name);
            else {
                a->symbol = s;
                a->node = n;
                a->next = v->arrays;
                v->arrays = a;
            }
        }
    }
    if (s && v->errors == errors_before) {
        success(v, n, "Declaración de %s '%s' aprobada: tipo %s (%s); registrado en el ámbito %d.",
                parameter ? "parámetro" : "variable", name, piper_type_name(type),
                type_name(s->type), v->table->current_scope->scope_id);
        if (dims->count)
            success(v, n, "'%s': forma %d x %d, %zu dimensiones, elementos de tipo %s.",
                    name, sizes[0], sizes[1], dims->count, type_name(scalar(type)));
    }
    return s;
}
/* Visita una expresión y exige el tipo del operador o índice que la usa. */
static void require(Visitor *v, AstNode *n, DataType type, const char *context)
{
    int errors_before = v->errors;
    DataType actual = visit(v, n);
    if (actual != type)
        error(v, n, "%s requiere tipo %s.", context, type == TYPE_INT ? "entero" : "booleano");
    else if (v->errors == errors_before)
        success(v, n, "%s: tipo %s aprobado (esperado %s).", context, type_name(actual), type_name(type));
}
/* Piper admite condiciones booleanas o enteras, pero no arreglos ni none. */
static void condition(Visitor *v, AstNode *n)
{
    int errors_before = v->errors;
    DataType t = visit(v, n);
    if (t != TYPE_BOOL && t != TYPE_INT) error(v, n, "La condición debe ser booleana o entera.");
    else if (v->errors == errors_before) success(v, n, "Condición de control aprobada: tipo %s.", type_name(t));
}
/* Solo identificadores y accesos indexados tienen forma de destino.
 * Su existencia y tipo se comprueban al visitar la expresión. */
static int writable(AstNode *n)
{
    return n && (n->kind == AST_IDENTIFIER_EXPRESSION || n->kind == AST_INDEX_EXPRESSION);
}

/* Una rutina devuelve en todos sus caminos si una sentencia alcanzable
 * garantiza output. Los ciclos pueden no ejecutarse; una rutina anidada no
 * devuelve por la rutina que la contiene. */
static int guarantees_output(const AstNode *n);

static int statements_guarantee_output(const AstNodeList *statements)
{
    for (size_t i = 0; i < statements->count; i++)
        if (guarantees_output(statements->items[i])) return 1;
    return 0;
}

static int guarantees_output(const AstNode *n)
{
    if (!n) return 0;
    switch (n->kind) {
    case AST_OUTPUT_STATEMENT:
        return 1;
    case AST_BLOCK:
        return statements_guarantee_output(n->data.block.statements);
    case AST_WHEN_STATEMENT:
        if (n->data.when_statement.condition->kind == AST_BOOLEAN_LITERAL)
            return guarantees_output(n->data.when_statement.condition->data.boolean_literal.value ?
                n->data.when_statement.then_block : n->data.when_statement.fallback_block);
        return n->data.when_statement.fallback_block &&
            guarantees_output(n->data.when_statement.then_block) &&
            guarantees_output(n->data.when_statement.fallback_block);
    default:
        return 0;
    }
}

static RoutineUsage *find_routine(Visitor *v, Symbol *symbol)
{
    for (RoutineUsage *r = v->routines; r; r = r->next)
        if (r->symbol == symbol) return r;
    return NULL;
}

/* En un grafo sin ciclos, calcular el camino de llamadas más costoso.
 * -1 indica que hay recursión y la profundidad depende de la ejecución. */
static long long peak_stack(Visitor *v, RoutineUsage *routine)
{
    long long child_peak = 0;
    int dynamic_depth = 0;
    routine->state = 1;
    for (CallUsage *edge = v->calls; edge; edge = edge->next) {
        if (edge->caller != routine) continue;
        RoutineUsage *callee = find_routine(v, edge->callee);
        if (!callee) continue;
        if (callee->state == 1) {
            dynamic_depth = 1;
            continue;
        }
        long long bytes = callee->state == 2 ? callee->peak_bytes : peak_stack(v, callee);
        if (bytes < 0) {
            dynamic_depth = 1;
            continue;
        }
        if (bytes > child_peak) child_peak = bytes;
    }
    long long known_peak = (long long)routine->symbol->total_stack_frame_size +
                           routine->symbol->call_reserve_size + child_peak;
    if (known_peak > SYMBOL_STACK_SIZE)
        error(v, routine->declaration,
              "La rutina '%s' puede ocupar %lld bytes de pila entre variables, llamadas y reserva; máximo %d.",
              routine->symbol->name, known_peak, SYMBOL_STACK_SIZE);
    routine->peak_bytes = dynamic_depth ? -1 : known_peak;
    routine->state = 2;
    return routine->peak_bytes;
}

static void check_stack(Visitor *v)
{
    for (RoutineUsage *routine = v->routines; routine; routine = routine->next)
        if (routine->state == 0) peak_stack(v, routine);
}

/* Visitor del AST: cada case conoce los hijos de su variante y los visita.
 * Retorna el tipo decorado de la expresión; TYPE_VOID representa none o un
 * nodo sin valor. NULL permite omitir hijos opcionales, como fallback. */
static DataType visit(Visitor *v, AstNode *n)
{
    if (!n) return TYPE_VOID;
    /* Solo aprobar el nodo completo si sus hijos y sus reglas no fallaron. */
    int errors_before = v->errors;
    /* Reiniciar la decoración permite analizar el mismo árbol otra vez. */
    /* Los índices y stores conservan valores en ranuras de 32 bytes,
     * igual que llamadas y aritmética general. Contar el máximo simultáneo
     * para que el prólogo compruebe la pila antes de usar esas ranuras. */
    int reserved = 0;
    if (n->kind == AST_CALL_EXPRESSION) {
        int nargs = n->data.call_expression.arguments != NULL
            ? (int)n->data.call_expression.arguments->count : 0;
        reserved = 32 + (nargs > 4 ? (nargs - 4) * 4 : 0);
    } else if (n->kind == AST_INDEX_EXPRESSION || n->kind == AST_ASSIGNMENT ||
               (n->kind == AST_UNARY_EXPRESSION &&
                (n->data.unary_expression.operator == AST_OP_INCREMENT ||
                 n->data.unary_expression.operator == AST_OP_DECREMENT))) {
        reserved = 32;
    }
    if (n->kind == AST_BINARY_EXPRESSION) {
        /* Reserva conservadora: también cubre un spill de una operación
         * simple cuando los dos temporales ya contienen resultados vivos. */
        AstOperator op = n->data.binary_expression.operator;
        /* Un spill puede coexistir con la ranura de comprobación del
         * resultado. Ambos deben quedar dentro de la guarda de pila. */
        reserved = ((op >= AST_OP_LESS && op <= AST_OP_GREATER_EQUAL) ||
                    op == AST_OP_ADD || op == AST_OP_SUBTRACT ||
                    op == AST_OP_MULTIPLY) ? 64 : 32;
    }
    v->pending_codegen_bytes += reserved;
    if (v->pending_codegen_bytes > SYMBOL_STACK_SIZE)
        error(v, n, "La evaluación excede los %d bytes de pila.", SYMBOL_STACK_SIZE);
    else {
        int *peak = v->function ? &v->function->call_reserve_size
                                : &v->table->global_call_reserve_size;
        if (v->pending_codegen_bytes > *peak) *peak = (int)v->pending_codegen_bytes;
    }
    n->semantic_scope = NULL;
    n->inferred_type = TYPE_VOID;
    n->array_rank = n->array_rows = n->array_cols = 0;
    n->element_type = TYPE_VOID;
    n->has_piper_type = false;
    n->has_element_piper_type = false;
    switch (n->kind) {
    /* Las declaraciones globales se procesan en orden de aparición. */
    case AST_PROGRAM: {
        AstNodeList *elements = n->data.program.elements;
        list(v, elements);
        /* La entrada debe estar en el ámbito global: una rutina anidada
         * llamada main no puede recibir el salto del arranque de codegen. */
        AstNode *entry = NULL;
        if (elements) {
            for (size_t i = 0; i < elements->count; i++) {
                AstNode *element = elements->items[i];
                if (element && element->kind == AST_ROUTINE_DECLARATION &&
                    strcmp(element->data.routine_declaration.name, "main") == 0) {
                    entry = element;
                    break;
                }
            }
        }
        if (!entry)
            error(v, n, "El programa debe declarar una rutina global 'main' sin parámetros.");
        else if (entry->data.routine_declaration.parameters &&
                 entry->data.routine_declaration.parameters->count != 0)
            error(v, entry, "La rutina de entrada 'main' no puede recibir parámetros.");
        else if (v->errors == errors_before)
            success(v, entry, "Entrada del programa aprobada: rutina global 'main' sin parámetros.");
        break;
    }
    /* Sin cargar módulos no se puede validar una firma importada. */
    case AST_IMPORT_DECLARATION:
        error(v, n, "No se puede resolver el import '%s': no hay cargador de módulos.", n->data.import_declaration.symbol);
        break;
    case AST_VARIABLE_DECLARATION: case AST_PARAMETER: declare(v, n); break;
    /* Registrar la función antes del cuerpo permite llamadas recursivas. */
    case AST_ROUTINE_DECLARATION: {
        const char *name = n->data.routine_declaration.name;
        if (symbol_table_lookup_current_scope(v->table, name)) {
            error(v, n, "Declaración duplicada de '%s' en el mismo ámbito.", name);
        }
        else success(v, n, "Nombre de función '%s' aprobado: sin duplicados en el ámbito actual.", name);
        int valid_name = strlen(name) < SYMBOL_NAME_MAX;
        if (!valid_name) error(v, n, "El identificador '%s' excede la longitud máxima.", name);
        Symbol *f = !valid_name || symbol_table_lookup_current_scope(v->table, name) ? NULL :
            symbol_table_insert(v->table, name, SYMBOL_FUNC, scalar(n->data.routine_declaration.return_type),
                                n->data.routine_declaration.return_type, 1, 1, n->location.first_line);
        if (valid_name && !f && !symbol_table_lookup_current_scope(v->table, name)) error(v, n, "No se pudo registrar la función '%s'.", name);
        if (f) success(v, n, "Función '%s' registrada en el ámbito %d, línea %d; retorno %s.",
                       name, v->table->current_scope->scope_id, f->line_number, type_name(f->return_type));
        /* Si falla el registro, conservar el retorno esperado para analizar
         * el cuerpo y reportar otros errores sin reemplazar la firma original. */
        Symbol fallback = {0};
        fallback.return_type = scalar(n->data.routine_declaration.return_type);
        fallback.declared_type = n->data.routine_declaration.return_type;
        snprintf(fallback.name, sizeof(fallback.name), "%s", name);
        Symbol *old = v->function;
        RoutineUsage *old_routine = v->current_routine;
        int old_loops = v->loops;
        long long old_pending = v->pending_codegen_bytes;
        if (!open_scope(v, n, 0)) break;
        v->pending_codegen_bytes = 0;
        v->function = f ? f : &fallback;
        v->current_routine = NULL;
        if (f) {
            RoutineUsage *usage = calloc(1, sizeof(*usage));
            if (!usage) error(v, n, "No se pudo registrar el uso de pila de '%s'.", name);
            else {
                usage->symbol = f;
                usage->declaration = n;
                usage->next = v->routines;
                v->routines = usage;
                v->current_routine = usage;
            }
        }
        KnownInteger *outer_known = v->known;
        InitializedSymbol *outer_initialized = v->initialized;
        v->known = NULL;
        v->initialized = NULL;
        /* Un ciclo de un ámbito exterior no autoriza break en esta función. */
        v->loops = 0;
        AstNodeList *params = n->data.routine_declaration.parameters;
        if (f) {
            f->function_scope = v->table->current_scope;
            f->param_count = (int)params->count;
            f->params = calloc(params->count ? params->count : 1, sizeof(*f->params));
            if (!f->params) error(v, n, "No se pudo reservar la firma de '%s'.", name);
        }
        /* Los parámetros son declaraciones locales; params conserva su orden
         * para comparar cada argumento con la posición correspondiente. */
        int signature_errors = v->errors;
        for (size_t i = 0; i < params->count; i++) {
            Symbol *p = declare(v, params->items[i]);
            if (f && f->params) f->params[i] = p;
            if (p && p->type != TYPE_ARRAY) mark_initialized(v, params->items[i], p);
        }
        if (f && f->params && v->errors == signature_errors)
            success(v, n, "Firma de '%s' aprobada: %zu parámetros declarados en el ámbito %d.",
                    name, params->count, v->table->current_scope->scope_id);
        /* Parámetros y declaraciones del cuerpo comparten el ámbito. */
        AstNode *body = n->data.routine_declaration.body;
        if (body->kind == AST_BLOCK) list(v, body->data.block.statements);
        else visit(v, body);
        if (!guarantees_output(body))
            error(v, n, "La rutina '%s' puede terminar sin ejecutar output.", name);
        if (f) symbol_table_set_function_frame(v->table, f);
        if (v->errors == errors_before)
            success(v, n, "Análisis de la función '%s' completado sin errores.", name);
        close_scope(v, n, 0);
        clear_known(v);
        free_initialized(v->initialized);
        v->known = outer_known;
        v->initialized = outer_initialized;
        v->function = old;
        v->current_routine = old_routine;
        v->loops = old_loops;
        v->pending_codegen_bytes = old_pending;
        break;
    }
    /* Al salir, las declaraciones del bloque dejan de ser visibles. */
    case AST_BLOCK:
        if (open_scope(v, n, 1)) {
            list(v, n->data.block.statements);
            if (!v->function && v->table->current_scope->max_offset > 0)
                error(v, n, "Un bloque global no puede reservar variables locales sin un marco de rutina.");
            if (v->errors == errors_before) success(v, n, "Bloque analizado sin errores.");
            forget_scope_initializations(v, v->table->current_scope);
            close_scope(v, n, 1);
        }
        break;
    case AST_INTEGER_LITERAL:
        n->inferred_type = TYPE_INT;
        if (n->data.integer_literal.value > 4294967295ULL)
            error(v, n, "El literal %llu no cabe en ningún tipo entero de Piper.",
                  n->data.integer_literal.value);
        break;
    case AST_BOOLEAN_LITERAL: n->inferred_type = TYPE_BOOL; n->piper_type = PIPER_TYPE_BOOL; n->has_piper_type = true; break;
    case AST_NONE_LITERAL: n->piper_type = PIPER_TYPE_NONE; n->has_piper_type = true; break;
    case AST_IDENTIFIER_EXPRESSION: {
        Symbol *s = lookup(v, n, n->data.identifier.name);
        if (s && s->symbol_class == SYMBOL_FUNC) error(v, n, "La función '%s' requiere una llamada.", s->name);
        else if (s) {
            decorate_symbol(v, n, s);
            Scope *owner = symbol_scope(v, s);
            if (n != v->assignment_target && s->symbol_class == SYMBOL_VAR &&
                s->type != TYPE_ARRAY && owner && !owner->is_global &&
                !is_initialized(v->initialized, s))
                error(v, n, "La variable local '%s' se lee antes de recibir un valor.", s->name);
        }
        break;
    }
    /* Inferir de los elementos y verificar filas homogéneas/rectangulares. */
    case AST_ARRAY_LITERAL: {
        AstNodeList *items = n->data.array_literal.elements;
        list(v, items);
        if (!items->count) { error(v, n, "El arreglo literal no puede estar vacío."); break; }
        AstNode *first = items->items[0];
        for (size_t i = 1; i < items->count; i++)
            if (!compatible(first, items->items[i])) error(v, items->items[i], "El arreglo debe ser rectangular y tener elementos del mismo tipo.");
        if (first->inferred_type == TYPE_VOID || first->array_rank > 1) error(v, n, "Tipo o dimensión inválida en arreglo literal.");
        n->inferred_type = TYPE_ARRAY;
        n->element_type = first->inferred_type == TYPE_ARRAY ? first->element_type : first->inferred_type;
        n->element_piper_type = first->inferred_type == TYPE_ARRAY ?
            first->element_piper_type : first->piper_type;
        n->has_element_piper_type = first->inferred_type == TYPE_ARRAY ?
            first->has_element_piper_type : first->has_piper_type;
        n->array_rank = first->array_rank + 1;
        n->array_rows = (int)items->count;
        n->array_cols = first->array_rank ? first->array_rows : 1;
        if (v->errors == errors_before)
            success(v, n, "Arreglo literal aprobado: elementos homogéneos de tipo %s, forma rectangular %d x %d y %d dimensiones.",
                    type_name(n->element_type), n->array_rows, n->array_cols, n->array_rank);
        break;
    }
    /* La base debe ser un arreglo; cada índice entero, la cantidad exacta y
     * los valores conocidos dentro de las dimensiones declaradas. */
    case AST_INDEX_EXPRESSION: {
        AstNode *base = n->data.index_expression.base;
        visit(v, base);
        AstNodeList *indices = n->data.index_expression.indices;
        for (size_t i = 0; i < indices->count; i++) require(v, indices->items[i], TYPE_INT, "El índice");
        const char *name = base->kind == AST_IDENTIFIER_EXPRESSION ? base->data.identifier.name : "expresión";
        if (base->inferred_type != TYPE_ARRAY) error(v, n, "'%s' no es un arreglo o matriz.", name);
        else if ((int)indices->count != base->array_rank) error(v, n, "'%s' requiere %d índices; recibió %zu.", name, base->array_rank, indices->count);
        else {
            for (size_t i = 0; i < indices->count; i++) {
                AstNode *index = indices->items[i];
                int dimension = i == 0 ? base->array_rows : base->array_cols;
                KnownValue value;
                if (dimension > 0 && known_integer(v, index, &value) &&
                    (value.negative || value.magnitude >= (unsigned long long)dimension))
                    error(v, index, "El índice %zu de '%s' (%s%llu) está fuera de rango [0, %d].",
                          i + 1, name, value.negative ? "-" : "", value.magnitude, dimension - 1);
            }
            n->inferred_type = base->element_type;
            n->piper_type = base->element_piper_type;
            n->has_piper_type = base->has_element_piper_type;
            if (v->errors == errors_before) {
                success(v, n, "Acceso a '%s': cantidad de índices aprobada (%zu).", name, indices->count);
                success(v, n, "Indexación de '%s' aprobada: índices enteros; resultado de tipo %s.", name, type_name(n->inferred_type));
            }
        }
        break;
    }
    /* Los argumentos se resuelven en el ámbito de la llamada. Se compara
     * su tipo con la firma; aquí no se vuelve a recorrer el cuerpo llamado. */
    case AST_CALL_EXPRESSION: {
        Symbol *f = lookup(v, n, n->data.call_expression.routine_name);
        AstNodeList *args = n->data.call_expression.arguments;
        if (!f || f->symbol_class != SYMBOL_FUNC) {
            /* Incluso sin una firma válida, analizar todos los argumentos. */
            list(v, args);
            if (f) error(v, n, "'%s' no es una función.", f->name);
            break;
        }
        success(v, n, "Destino de llamada '%s' aprobado: clase SYMBOL_FUNC, declarado en la línea %d.", f->name, f->line_number);
        if ((int)args->count != f->param_count) error(v, n, "'%s' requiere %d argumentos; recibió %zu.", f->name, f->param_count, args->count);
        else success(v, n, "Llamada a '%s': cantidad de argumentos aprobada (%zu).", f->name, args->count);
        for (size_t i = 0; i < args->count; i++) {
            int argument_errors = v->errors;
            visit(v, args->items[i]);
            AstNode expected = {0};
            if (i >= (size_t)f->param_count || !f->params || !f->params[i]) continue;
            decorate_symbol(v, &expected, f->params[i]);
            if (!assignable(v, &expected, args->items[i]))
                error(v, args->items[i], "Tipo o rango incompatible para el argumento %zu de '%s' (esperado %s).",
                      i + 1, f->name, piper_type_name(f->params[i]->declared_type));
            else if (v->errors == argument_errors)
                success(v, args->items[i], "Argumento %zu de '%s' aprobado: compatible con el parámetro '%s' (tipo %s), declarado en la línea %d, ámbito %d.",
                        i + 1, f->name, f->params[i]->name, type_name(expected.inferred_type),
                        f->params[i]->line_number, f->function_scope->scope_id);
        }
        n->inferred_type = f->return_type;
        n->piper_type = f->declared_type;
        n->has_piper_type = true;
        if (v->current_routine) {
            CallUsage *edge = malloc(sizeof(*edge));
            if (!edge) error(v, n, "No se pudo registrar la llamada para calcular la pila.");
            else {
                edge->caller = v->current_routine;
                edge->callee = f;
                edge->next = v->calls;
                v->calls = edge;
            }
        }
        /* La llamada puede modificar variables visibles fuera de esta rutina. */
        clear_known(v);
        if (v->errors == errors_before)
            success(v, n, "Llamada a '%s' aprobada; resultado de tipo %s.", f->name, type_name(f->return_type));
        break;
    }
    /* Analizar ambos lados aun si uno falla permite acumular diagnósticos. */
    case AST_ASSIGNMENT: {
        AstNode *a = n->data.assignment.target, *b = n->data.assignment.value;
        AstNode *old_target = v->assignment_target;
        v->assignment_target = a;
        visit(v, a);
        v->assignment_target = old_target;
        int target_valid = v->errors == errors_before;
        visit(v, b);
        KnownValue assigned_value;
        int assigned_known = known_integer(v, b, &assigned_value);
        if (!writable(a)) error(v, a, "El destino de asignación no es modificable.");
        else if (target_valid && a->inferred_type != TYPE_VOID) success(v, a, "Destino de asignación aprobado: identificador o elemento indexado modificable.");
        if (a->inferred_type == TYPE_VOID || b->inferred_type == TYPE_VOID || !assignable(v, a, b)) {
            if (a->inferred_type == TYPE_INT && assigned_known && !fits_type(a->piper_type, assigned_value))
                error(v, n, "El valor %s%llu no cabe en %s.", assigned_value.negative ? "-" : "",
                      assigned_value.magnitude, piper_type_name(a->piper_type));
            else if (a->inferred_type == TYPE_ARRAY && b->kind == AST_ARRAY_LITERAL)
                error(v, n, "El arreglo literal tiene un tipo o valor incompatible con %s.",
                      piper_type_name(a->element_piper_type));
            else
                error(v, n, "Tipo o rango incompatible en asignación a '%s'.", a->kind == AST_IDENTIFIER_EXPRESSION ? a->data.identifier.name : "elemento indexado");
        }
        else if (v->errors == errors_before)
            success(v, n, "Asignación a '%s' aprobada: origen y destino compatibles, tipo %s.",
                    a->kind == AST_IDENTIFIER_EXPRESSION ? a->data.identifier.name :
                    a->data.index_expression.base->kind == AST_IDENTIFIER_EXPRESSION ?
                    a->data.index_expression.base->data.identifier.name : "elemento indexado", type_name(a->inferred_type));
        if (a->kind == AST_IDENTIFIER_EXPRESSION) {
            Symbol *symbol = symbol_table_lookup(v->table, a->data.identifier.name);
            if (symbol && symbol->type == TYPE_INT)
                set_known(v, n, symbol, v->errors == errors_before && assigned_known ? &assigned_value : NULL);
            Scope *owner = symbol ? symbol_scope(v, symbol) : NULL;
            if (v->errors == errors_before && symbol && symbol->symbol_class == SYMBOL_VAR &&
                symbol->type != TYPE_ARRAY && owner && !owner->is_global)
                mark_initialized(v, n, symbol);
        }
        break;
    }
    /* not exige bool; negación, incremento y decremento exigen enteros. */
    case AST_UNARY_EXPRESSION: {
        AstOperator op = n->data.unary_expression.operator;
        AstNode *operand = n->data.unary_expression.operand;
        DataType type = op == AST_OP_LOGICAL_NOT ? TYPE_BOOL : TYPE_INT;
        require(v, operand, type, ast_operator_name(op));
        if ((op == AST_OP_INCREMENT || op == AST_OP_DECREMENT) && !writable(operand)) error(v, n, "'%s' requiere un destino modificable.", ast_operator_name(op));
        if ((op == AST_OP_INCREMENT || op == AST_OP_DECREMENT) &&
            operand->has_piper_type && integer_type(operand->piper_type)) {
            KnownValue old_value, new_value, one = {1, op == AST_OP_DECREMENT};
            if (known_integer(v, operand, &old_value) &&
                (!add_values(old_value, one, &new_value) ||
                 !fits_type(operand->piper_type, new_value)))
                error(v, n, "'%s' produce un valor fuera del rango de %s.",
                      ast_operator_name(op), piper_type_name(operand->piper_type));
        }
        if (op == AST_OP_INCREMENT || op == AST_OP_DECREMENT) clear_known(v);
        n->inferred_type = type;
        if (type == TYPE_BOOL) {
            n->piper_type = PIPER_TYPE_BOOL;
            n->has_piper_type = true;
        } else if (operand->has_piper_type &&
                   !(op == AST_OP_NEGATE && unsigned_type(operand->piper_type))) {
            n->piper_type = operand->piper_type;
            n->has_piper_type = true;
        }
        if (op == AST_OP_NEGATE) {
            KnownValue value;
            if (known_integer(v, n, &value) && !fits_any_integer(value))
                error(v, n, "La negación produce un valor fuera de los enteros de Piper.");
        }
        if (v->errors == errors_before)
            success(v, n, "Operador unario '%s' aprobado; resultado %s%s.", ast_operator_name(op), type_name(type),
                    op == AST_OP_INCREMENT || op == AST_OP_DECREMENT ? "; destino modificable" : "");
        break;
    }
    /* Igualdad compara escalares compatibles; aritmética/orden usan enteros
     * y operadores lógicos usan bool. Las comparaciones producen bool. */
    case AST_BINARY_EXPRESSION: {
        AstOperator op = n->data.binary_expression.operator;
        AstNode *a = n->data.binary_expression.left, *b = n->data.binary_expression.right;
        if (op == AST_OP_EQUAL || op == AST_OP_NOT_EQUAL) {
            visit(v, a); visit(v, b);
            if (!compatible(a, b) || (a->inferred_type != TYPE_BOOL && a->inferred_type != TYPE_INT)) error(v, n, "'%s' requiere operandos escalares compatibles.", ast_operator_name(op));
            n->inferred_type = TYPE_BOOL;
            n->piper_type = PIPER_TYPE_BOOL;
            n->has_piper_type = true;
        } else {
            int logical = op == AST_OP_LOGICAL_AND || op == AST_OP_LOGICAL_OR || op == AST_OP_LOGICAL_XOR;
            require(v, a, logical ? TYPE_BOOL : TYPE_INT, ast_operator_name(op));
            require(v, b, logical ? TYPE_BOOL : TYPE_INT, ast_operator_name(op));
            n->inferred_type = logical || (op >= AST_OP_LESS && op <= AST_OP_GREATER_EQUAL) ? TYPE_BOOL : TYPE_INT;
            if (n->inferred_type == TYPE_BOOL) {
                n->piper_type = PIPER_TYPE_BOOL;
                n->has_piper_type = true;
            } else n->has_piper_type = arithmetic_type(v, a, b, &n->piper_type);
        }
        if (n->inferred_type == TYPE_INT) {
            KnownValue value;
            KnownValue left_value, right_value;
            int left_known = known_integer(v, a, &left_value);
            int right_known = known_integer(v, b, &right_value);
            int operands_known = left_known && right_known;
            if ((op == AST_OP_DIVIDE || op == AST_OP_REMAINDER) &&
                right_known && right_value.magnitude == 0)
                error(v, n, "La división entre cero no está permitida.");
            else if (operands_known &&
                     (op == AST_OP_ADD || op == AST_OP_SUBTRACT ||
                      op == AST_OP_MULTIPLY ||
                      (op == AST_OP_POWER && !right_value.negative)) &&
                     !known_integer(v, n, &value))
                error(v, n, "La expresión constante supera el rango de los enteros de Piper.");
            else if (known_integer(v, n, &value) &&
                     (n->has_piper_type ? !fits_type(n->piper_type, value) :
                                          !fits_any_integer(value)))
                error(v, n, "La expresión produce un valor fuera del rango de %s.",
                      n->has_piper_type ? piper_type_name(n->piper_type) : "los enteros de Piper");
        }
        if (v->errors == errors_before)
            success(v, n, "Operador binario '%s' aprobado: operandos %s y %s; resultado %s.",
                    ast_operator_name(op), type_name(a->inferred_type), type_name(b->inferred_type), type_name(n->inferred_type));
        break;
    }
    /* Cada rama abre su propio ámbito al visitar su bloque. */
    case AST_WHEN_STATEMENT: {
        condition(v, n->data.when_statement.condition);
        KnownInteger *before = v->known;
        InitializedSymbol *before_initialized = v->initialized;
        v->known = copy_known(v, n, before);
        v->initialized = copy_initialized(v, n, before_initialized);
        visit(v, n->data.when_statement.then_block);
        InitializedSymbol *then_initialized = v->initialized;
        clear_known(v);
        v->known = copy_known(v, n, before);
        v->initialized = copy_initialized(v, n, before_initialized);
        visit(v, n->data.when_statement.fallback_block);
        InitializedSymbol *else_initialized = v->initialized;
        clear_known(v);
        v->known = before;
        clear_known(v);
        AstNode *condition_node = n->data.when_statement.condition;
        int then_returns = guarantees_output(n->data.when_statement.then_block);
        int else_returns = guarantees_output(n->data.when_statement.fallback_block);
        if (condition_node->kind == AST_BOOLEAN_LITERAL) {
            if (condition_node->data.boolean_literal.value) {
                free_initialized(else_initialized);
                v->initialized = then_initialized;
            } else {
                free_initialized(then_initialized);
                v->initialized = else_initialized;
            }
        } else if (then_returns && !else_returns) {
            free_initialized(then_initialized);
            v->initialized = else_initialized;
        } else if (else_returns && !then_returns) {
            free_initialized(else_initialized);
            v->initialized = then_initialized;
        } else {
            intersect_initialized(&then_initialized, else_initialized);
            free_initialized(else_initialized);
            v->initialized = then_initialized;
        }
        free_initialized(before_initialized);
        if (v->errors == errors_before) success(v, n, "Sentencia when aprobada: condición y ramas válidas.");
        break;
    }
    /* Contar anidamiento durante el cuerpo habilita break en sus bloques. */
    case AST_ALWHEN_STATEMENT: {
        condition(v, n->data.alwhen_statement.condition);
        InitializedSymbol *before_initialized = v->initialized;
        v->initialized = copy_initialized(v, n, before_initialized);
        clear_known(v);
        v->loops++; visit(v, n->data.alwhen_statement.body); v->loops--;
        free_initialized(v->initialized);
        v->initialized = before_initialized;
        clear_known(v);
        if (v->errors == errors_before) success(v, n, "Ciclo alwhen aprobado: condición y cuerpo válidos.");
        break;
    }
    /* El control del loop debe estar declarado; sus límites y paso son enteros. */
    case AST_LOOP_STATEMENT: {
        Symbol *control = lookup(v, n, n->data.loop_statement.control_name);
        if (control && (control->symbol_class == SYMBOL_FUNC || control->type != TYPE_INT)) error(v, n, "El control '%s' debe ser una variable entera.", control->name);
        else if (control) success(v, n, "Control del loop '%s' aprobado: variable o parámetro entero.", control->name);
        require(v, n->data.loop_statement.begin, TYPE_INT, "El inicio del loop");
        require(v, n->data.loop_statement.end, TYPE_INT, "El fin del loop");
        require(v, n->data.loop_statement.step, TYPE_INT, "El paso del loop");
        InitializedSymbol *before_initialized = v->initialized;
        v->initialized = copy_initialized(v, n, before_initialized);
        if (control && control->symbol_class != SYMBOL_FUNC && control->type == TYPE_INT)
            mark_initialized(v, n, control);
        clear_known(v);
        v->loops++; visit(v, n->data.loop_statement.body); v->loops--;
        free_initialized(v->initialized);
        v->initialized = before_initialized;
        clear_known(v);
        if (v->errors == errors_before) success(v, n, "Ciclo loop aprobado: control, límites, paso y cuerpo válidos.");
        break;
    }
    case AST_BREAK_STATEMENT:
        if (!v->loops) error(v, n, "'break' solo es válido dentro de un ciclo.");
        else success(v, n, "'break' aprobado: dentro de un ciclo, profundidad %d.", v->loops);
        break;
    /* output se valida contra la función que contiene esta sentencia.
     * Aquí se verifica cada retorno, sin demostrar cobertura de caminos. */
    case AST_OUTPUT_STATEMENT: {
        DataType t = visit(v, n->data.output_statement.value);
        if (!v->function) error(v, n, "'output' solo es válido dentro de una función.");
        else if (t != v->function->return_type || t == TYPE_ARRAY ||
                 (t == TYPE_VOID && n->data.output_statement.value && n->data.output_statement.value->kind != AST_NONE_LITERAL))
            error(v, n, "Retorno incompatible en '%s'.", v->function->name);
        else if (t == TYPE_INT) {
            AstNode expected = {0};
            expected.inferred_type = TYPE_INT;
            expected.piper_type = v->function->declared_type;
            expected.has_piper_type = true;
            if (!assignable(v, &expected, n->data.output_statement.value))
                error(v, n, "El resultado de '%s' no cabe o no es compatible con %s.",
                      v->function->name, piper_type_name(v->function->declared_type));
        }
        if (v->errors == errors_before)
            success(v, n, "Retorno output de '%s' aprobado: tipo %s coincide con la firma declarada en la línea %d.",
                    v->function->name, type_name(t), v->function->line_number);
        break;
    }
    }
    if (v->errors == errors_before && n->kind >= AST_CALL_EXPRESSION)
        success(v, n, "Decoración de expresión aprobada: tipo inferido %s.", type_name(n->inferred_type));
    v->pending_codegen_bytes -= reserved;
    return n->inferred_type;
}

/* Punto de entrada: reconstruir ámbitos desde la raíz evita aceptar usos de
 * declaraciones posteriores que el parser ya haya insertado en su tabla. */
int analyze_semantics_with_trace(AstNode *root, SymbolTable *symbol_table,
                                 int trace_enabled)
{
    Visitor v = {0};
    v.trace_enabled = trace_enabled;
    if (!root || !symbol_table) {
        error(&v, root, "El AST y la tabla de símbolos son obligatorios.");
        return 0;
    }
    v.table = symbol_table_create();
    if (!v.table) { error(&v, root, "No se pudo crear la tabla semántica."); return 0; }
    success(&v, root, "Inicio del análisis: tabla semántica creada en el ámbito global %d.", v.table->global_scope->scope_id);
    visit(&v, root);
    check_stack(&v);
    if (v.errors == 0) success(&v, root, "Programa aprobado: todas las comprobaciones finalizaron sin errores.");
    /* Liberar solo los enlaces temporales: el AST y la tabla conservan sus datos. */
    while (v.arrays != NULL) {
        ArrayDeclaration *next = v.arrays->next;
        free(v.arrays);
        v.arrays = next;
    }
    clear_known(&v);
    free_initialized(v.initialized);
    while (v.calls) {
        CallUsage *next = v.calls->next;
        free(v.calls);
        v.calls = next;
    }
    while (v.routines) {
        RoutineUsage *next = v.routines->next;
        free(v.routines);
        v.routines = next;
    }
    /* Sustituir la tabla del parser conservando la dirección del objeto público.
     * La tabla anterior se destruye y sus punteros a símbolos dejan de ser válidos.
     * La reconstruida queda disponible incluso si se encontraron errores. */
    SymbolTable old = *symbol_table;
    *symbol_table = *v.table;
    *v.table = old;
    symbol_table_destroy(v.table);
    return v.errors == 0;
}

int analyze_semantics(AstNode *root, SymbolTable *symbol_table)
{
    return analyze_semantics_with_trace(root, symbol_table, 0);
}
