#include "semantic.h"
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

/* Estado compartido por el recorrido recursivo. function identifica el retorno
 * esperado y loops cuenta los ciclos que contienen la sentencia actual. */
typedef struct {
    SymbolTable *table;
    ArrayDeclaration *arrays;
    Symbol *function;
    int loops, errors;
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

static void close_scope(Visitor *v, AstNode *n)
{
    int closed = v->table->current_scope->scope_id;
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
/* La API de ámbitos no devuelve estado; comprobar el cambio detecta OOM. */
static int open_scope(Visitor *v, AstNode *n)
{
    Scope *before = v->table->current_scope;
    symbol_table_open_scope(v->table);
    if (before == v->table->current_scope) {
        error(v, n, "No se pudo abrir el ámbito.");
        return 0;
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
        success(v, n, "Búsqueda de '%s': %s encontrad%s en el ámbito %d (%s), declarado en la línea %d; tipo %s.",
                name, s->symbol_class == SYMBOL_FUNC ? "función" :
                      s->symbol_class == SYMBOL_PARAM ? "parámetro" : "variable",
                s->symbol_class == SYMBOL_PARAM ? "o" : "a", owner->scope_id, owner == v->table->current_scope ? "actual" :
                owner->is_global ? "global" : "padre", s->line_number, type_name(s->type));
    }
    return s;
}
/* Los arreglos requieren el mismo tipo de elemento, rango y tamaños. */
static int compatible(AstNode *a, AstNode *b)
{
    if (a->inferred_type != b->inferred_type) return 0;
    return a->inferred_type != TYPE_ARRAY ||
        (a->element_type == b->element_type && a->array_rank == b->array_rank &&
         a->array_rows == b->array_rows && a->array_cols == b->array_cols);
}
/* Copia la información del símbolo resuelto. Para un arreglo, recuperar la
 * declaración evita perder el tipo de elemento o confundir vector y matriz.
 * También sirve para construir el tipo esperado de un parámetro de función. */
static void decorate_symbol(Visitor *v, AstNode *n, Symbol *s)
{
    n->inferred_type = s->type;
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
    success(v, n, "Nombre '%s' aprobado: sin declaración duplicada en el ámbito actual.", name);
    Symbol *s = symbol_table_insert(v->table, name, parameter ? SYMBOL_PARAM : SYMBOL_VAR,
        dims->count ? TYPE_ARRAY : scalar(type), sizes[0], sizes[1], n->location.first_line);
    if (!s) error(v, n, "No se pudo registrar '%s' o asignar su memoria.", name);
    else {
        n->inferred_type = s->type;
        n->element_type = scalar(type);
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

/* Visitor del AST: cada case conoce los hijos de su variante y los visita.
 * Retorna el tipo decorado de la expresión; TYPE_VOID representa none o un
 * nodo sin valor. NULL permite omitir hijos opcionales, como fallback. */
static DataType visit(Visitor *v, AstNode *n)
{
    if (!n) return TYPE_VOID;
    /* Solo aprobar el nodo completo si sus hijos y sus reglas no fallaron. */
    int errors_before = v->errors;
    /* Reiniciar la decoración permite analizar el mismo árbol otra vez. */
    n->inferred_type = TYPE_VOID;
    n->array_rank = n->array_rows = n->array_cols = 0;
    n->element_type = TYPE_VOID;
    switch (n->kind) {
    /* Las declaraciones globales se procesan en orden de aparición. */
    case AST_PROGRAM: list(v, n->data.program.elements); break;
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
            symbol_table_insert(v->table, name, SYMBOL_FUNC, scalar(n->data.routine_declaration.return_type), 1, 1, n->location.first_line);
        if (valid_name && !f && !symbol_table_lookup_current_scope(v->table, name)) error(v, n, "No se pudo registrar la función '%s'.", name);
        if (f) success(v, n, "Función '%s' registrada en el ámbito %d, línea %d; retorno %s.",
                       name, v->table->current_scope->scope_id, f->line_number, type_name(f->return_type));
        /* Si falla el registro, conservar el retorno esperado para analizar
         * el cuerpo y reportar otros errores sin reemplazar la firma original. */
        Symbol fallback = {0};
        fallback.return_type = scalar(n->data.routine_declaration.return_type);
        snprintf(fallback.name, sizeof(fallback.name), "%s", name);
        Symbol *old = v->function;
        int old_loops = v->loops;
        if (!open_scope(v, n)) break;
        v->function = f ? f : &fallback;
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
        }
        if (f && f->params && v->errors == signature_errors)
            success(v, n, "Firma de '%s' aprobada: %zu parámetros declarados en el ámbito %d.",
                    name, params->count, v->table->current_scope->scope_id);
        /* Parámetros y declaraciones del cuerpo comparten el ámbito. */
        AstNode *body = n->data.routine_declaration.body;
        if (body->kind == AST_BLOCK) list(v, body->data.block.statements);
        else visit(v, body);
        if (f) symbol_table_set_function_frame(v->table, f);
        if (v->errors == errors_before)
            success(v, n, "Análisis de la función '%s' completado sin errores.", name);
        close_scope(v, n);
        v->function = old;
        v->loops = old_loops;
        break;
    }
    /* Al salir, las declaraciones del bloque dejan de ser visibles. */
    case AST_BLOCK:
        if (open_scope(v, n)) {
            list(v, n->data.block.statements);
            if (v->errors == errors_before) success(v, n, "Bloque analizado sin errores.");
            close_scope(v, n);
        }
        break;
    case AST_INTEGER_LITERAL: n->inferred_type = TYPE_INT; break;
    case AST_BOOLEAN_LITERAL: n->inferred_type = TYPE_BOOL; break;
    case AST_NONE_LITERAL: break;
    case AST_IDENTIFIER_EXPRESSION: {
        Symbol *s = lookup(v, n, n->data.identifier.name);
        if (s && s->symbol_class == SYMBOL_FUNC) error(v, n, "La función '%s' requiere una llamada.", s->name);
        else if (s) decorate_symbol(v, n, s);
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
        n->array_rank = first->array_rank + 1;
        n->array_rows = (int)items->count;
        n->array_cols = first->array_rank ? first->array_rows : 1;
        if (v->errors == errors_before)
            success(v, n, "Arreglo literal aprobado: elementos homogéneos de tipo %s, forma rectangular %d x %d y %d dimensiones.",
                    type_name(n->element_type), n->array_rows, n->array_cols, n->array_rank);
        break;
    }
    /* La base debe ser un arreglo; cada índice entero y la cantidad exacta.
     * Un acceso completo produce el tipo del elemento, no TYPE_ARRAY. */
    case AST_INDEX_EXPRESSION: {
        AstNode *base = n->data.index_expression.base;
        visit(v, base);
        AstNodeList *indices = n->data.index_expression.indices;
        for (size_t i = 0; i < indices->count; i++) require(v, indices->items[i], TYPE_INT, "El índice");
        const char *name = base->kind == AST_IDENTIFIER_EXPRESSION ? base->data.identifier.name : "expresión";
        if (base->inferred_type != TYPE_ARRAY) error(v, n, "'%s' no es un arreglo o matriz.", name);
        else if ((int)indices->count != base->array_rank) error(v, n, "'%s' requiere %d índices; recibió %zu.", name, base->array_rank, indices->count);
        else {
            n->inferred_type = base->element_type;
            success(v, n, "Acceso a '%s': cantidad de índices aprobada (%zu).", name, indices->count);
            if (v->errors == errors_before)
                success(v, n, "Indexación de '%s' aprobada: índices enteros; resultado de tipo %s.", name, type_name(n->inferred_type));
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
            if (!compatible(&expected, args->items[i])) error(v, args->items[i], "Tipo incompatible para el argumento %zu de '%s'.", i + 1, f->name);
            else if (v->errors == argument_errors)
                success(v, args->items[i], "Argumento %zu de '%s' aprobado: compatible con el parámetro '%s' (tipo %s), declarado en la línea %d, ámbito %d.",
                        i + 1, f->name, f->params[i]->name, type_name(expected.inferred_type),
                        f->params[i]->line_number, f->function_scope->scope_id);
        }
        n->inferred_type = f->return_type;
        if (v->errors == errors_before)
            success(v, n, "Llamada a '%s' aprobada; resultado de tipo %s.", f->name, type_name(f->return_type));
        break;
    }
    /* Analizar ambos lados aun si uno falla permite acumular diagnósticos. */
    case AST_ASSIGNMENT: {
        AstNode *a = n->data.assignment.target, *b = n->data.assignment.value;
        visit(v, a);
        int target_valid = v->errors == errors_before;
        visit(v, b);
        if (!writable(a)) error(v, a, "El destino de asignación no es modificable.");
        else if (target_valid && a->inferred_type != TYPE_VOID) success(v, a, "Destino de asignación aprobado: identificador o elemento indexado modificable.");
        if (a->inferred_type == TYPE_VOID || b->inferred_type == TYPE_VOID || !compatible(a, b))
            error(v, n, "Tipos incompatibles en asignación a '%s'.", a->kind == AST_IDENTIFIER_EXPRESSION ? a->data.identifier.name : "elemento indexado");
        else if (v->errors == errors_before)
            success(v, n, "Asignación a '%s' aprobada: origen y destino compatibles, tipo %s.",
                    a->kind == AST_IDENTIFIER_EXPRESSION ? a->data.identifier.name :
                    a->data.index_expression.base->kind == AST_IDENTIFIER_EXPRESSION ?
                    a->data.index_expression.base->data.identifier.name : "elemento indexado", type_name(a->inferred_type));
        break;
    }
    /* not exige bool; negación, incremento y decremento exigen enteros. */
    case AST_UNARY_EXPRESSION: {
        AstOperator op = n->data.unary_expression.operator;
        AstNode *operand = n->data.unary_expression.operand;
        DataType type = op == AST_OP_LOGICAL_NOT ? TYPE_BOOL : TYPE_INT;
        require(v, operand, type, ast_operator_name(op));
        if ((op == AST_OP_INCREMENT || op == AST_OP_DECREMENT) && !writable(operand)) error(v, n, "'%s' requiere un destino modificable.", ast_operator_name(op));
        n->inferred_type = type;
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
        } else {
            int logical = op == AST_OP_LOGICAL_AND || op == AST_OP_LOGICAL_OR || op == AST_OP_LOGICAL_XOR;
            require(v, a, logical ? TYPE_BOOL : TYPE_INT, ast_operator_name(op));
            require(v, b, logical ? TYPE_BOOL : TYPE_INT, ast_operator_name(op));
            n->inferred_type = logical || (op >= AST_OP_LESS && op <= AST_OP_GREATER_EQUAL) ? TYPE_BOOL : TYPE_INT;
        }
        if (v->errors == errors_before)
            success(v, n, "Operador binario '%s' aprobado: operandos %s y %s; resultado %s.",
                    ast_operator_name(op), type_name(a->inferred_type), type_name(b->inferred_type), type_name(n->inferred_type));
        break;
    }
    /* Cada rama abre su propio ámbito al visitar su bloque. */
    case AST_WHEN_STATEMENT:
        condition(v, n->data.when_statement.condition);
        visit(v, n->data.when_statement.then_block);
        visit(v, n->data.when_statement.fallback_block);
        if (v->errors == errors_before) success(v, n, "Sentencia when aprobada: condición y ramas válidas.");
        break;
    /* Contar anidamiento durante el cuerpo habilita break en sus bloques. */
    case AST_ALWHEN_STATEMENT:
        condition(v, n->data.alwhen_statement.condition);
        v->loops++; visit(v, n->data.alwhen_statement.body); v->loops--;
        if (v->errors == errors_before) success(v, n, "Ciclo alwhen aprobado: condición y cuerpo válidos.");
        break;
    /* El control del loop debe estar declarado; sus límites y paso son enteros. */
    case AST_LOOP_STATEMENT: {
        Symbol *control = lookup(v, n, n->data.loop_statement.control_name);
        if (control && (control->symbol_class == SYMBOL_FUNC || control->type != TYPE_INT)) error(v, n, "El control '%s' debe ser una variable entera.", control->name);
        else if (control) success(v, n, "Control del loop '%s' aprobado: variable o parámetro entero.", control->name);
        require(v, n->data.loop_statement.begin, TYPE_INT, "El inicio del loop");
        require(v, n->data.loop_statement.end, TYPE_INT, "El fin del loop");
        require(v, n->data.loop_statement.step, TYPE_INT, "El paso del loop");
        v->loops++; visit(v, n->data.loop_statement.body); v->loops--;
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
        else if (v->errors == errors_before)
            success(v, n, "Retorno output de '%s' aprobado: tipo %s coincide con la firma declarada en la línea %d.",
                    v->function->name, type_name(t), v->function->line_number);
        break;
    }
    }
    if (v->errors == errors_before && n->kind >= AST_CALL_EXPRESSION)
        success(v, n, "Decoración de expresión aprobada: tipo inferido %s.", type_name(n->inferred_type));
    return n->inferred_type;
}

/* Punto de entrada: reconstruir ámbitos desde la raíz evita aceptar usos de
 * declaraciones posteriores que el parser ya haya insertado en su tabla. */
int analyze_semantics(AstNode *root, SymbolTable *symbol_table)
{
    Visitor v = {0};
    if (!root || !symbol_table) {
        error(&v, root, "El AST y la tabla de símbolos son obligatorios.");
        return 0;
    }
    v.table = symbol_table_create();
    if (!v.table) { error(&v, root, "No se pudo crear la tabla semántica."); return 0; }
    success(&v, root, "Inicio del análisis: tabla semántica creada en el ámbito global %d.", v.table->global_scope->scope_id);
    visit(&v, root);
    if (v.errors == 0) success(&v, root, "Programa aprobado: todas las comprobaciones finalizaron sin errores.");
    /* Liberar solo los enlaces temporales: el AST y la tabla conservan sus datos. */
    while (v.arrays != NULL) {
        ArrayDeclaration *next = v.arrays->next;
        free(v.arrays);
        v.arrays = next;
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
