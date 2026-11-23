#define _XOPEN_SOURCE 700
/**
 * @file module_loader.c
 * @brief Carga de módulos para `bring ... from`. Ver module_loader.h.
 */
#include "module_loader.h"

#include "diagnostic.h"
#include "parser.h"
#include "symbol_table.h"

#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

void yyrestart(FILE *input_file);
int yylex_destroy(void);

/* Origen de cada rutina de nivel superior: permite distinguir un duplicado
 * legítimo (mismo módulo alcanzado por dos caminos) de un conflicto real. */
typedef struct { const AstNode *routine; const char *origin; } Origin;

typedef struct {
    const char *const *dirs;
    size_t dir_count;
    char **paths;  size_t path_count, path_cap;      /* rutas canónicas internadas */
    Origin *origins; size_t origin_count, origin_cap;
    ModuleStatus status;
} Loader;

/* Cadena de módulos en compilación, para detectar importaciones circulares. */
typedef struct Frame { const char *path; const struct Frame *parent; } Frame;

/* Mapa persistente rutina importada -> archivo. Es dueño de las rutas. */
typedef struct { const AstNode *node; const char *path; } Source;
struct SourceMap { Source *entries; size_t count; char **paths; size_t path_count; };

const char *source_map_file(const SourceMap *map, const AstNode *top_level)
{
    if (map == NULL) return NULL;
    for (size_t i = 0; i < map->count; i++)
        if (map->entries[i].node == top_level) return map->entries[i].path;
    return NULL;
}

void source_map_free(SourceMap *map)
{
    if (map == NULL) return;
    for (size_t i = 0; i < map->path_count; i++) free(map->paths[i]);
    free(map->paths);
    free(map->entries);
    free(map);
}

/* ---------------------------------------------------------------- utilidades */

static void raise_status(Loader *l, ModuleStatus status)
{
    if (status > l->status) l->status = status;
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void report(const char *file, SourceLocation location, DiagnosticCode code,
                   const char *format, ...)
{
    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof message, format, args);
    va_end(args);
    diagnostics_record(DIAGNOSTIC_SEMANTIC, code, location, "%s", message);
    fprintf(stderr, "%s:%d:%d: error: %s\n", base_name(file),
            location.first_line, location.first_column, message);
}

/* Devuelve la misma dirección para la misma ruta: se compara por puntero. */
static const char *intern(Loader *l, const char *path)
{
    for (size_t i = 0; i < l->path_count; i++)
        if (strcmp(l->paths[i], path) == 0) return l->paths[i];
    if (l->path_count == l->path_cap) {
        size_t cap = l->path_cap ? l->path_cap * 2 : 8;
        char **items = realloc(l->paths, cap * sizeof *items);
        if (!items) return NULL;
        l->paths = items;
        l->path_cap = cap;
    }
    char *copy = strdup(path);
    if (!copy) return NULL;
    return l->paths[l->path_count++] = copy;
}

static void loader_free(Loader *l)
{
    for (size_t i = 0; i < l->path_count; i++) free(l->paths[i]);
    free(l->paths);
    free(l->origins);
}

/* ------------------------------------------------------- registro de origen */

static int origin_set(Loader *l, const AstNode *routine, const char *origin)
{
    if (l->origin_count == l->origin_cap) {
        size_t cap = l->origin_cap ? l->origin_cap * 2 : 16;
        Origin *items = realloc(l->origins, cap * sizeof *items);
        if (!items) return 0;
        l->origins = items;
        l->origin_cap = cap;
    }
    l->origins[l->origin_count++] = (Origin){routine, origin};
    return 1;
}

static const char *origin_get(const Loader *l, const AstNode *routine)
{
    for (size_t i = 0; i < l->origin_count; i++)
        if (l->origins[i].routine == routine) return l->origins[i].origin;
    return NULL;
}

static void origin_forget(Loader *l, const AstNode *routine)
{
    for (size_t i = 0; i < l->origin_count; i++)
        if (l->origins[i].routine == routine) {
            l->origins[i] = l->origins[--l->origin_count];
            return;
        }
}

/* ------------------------------------------------------------ rutinas en AST */

static int is_routine(const AstNode *node)
{
    return node != NULL && node->kind == AST_ROUTINE_DECLARATION;
}

static const char *routine_name(const AstNode *node)
{
    return node->data.routine_declaration.name;
}

static size_t find_routine(const AstNodeList *list, const char *name)
{
    for (size_t i = 0; i < list->count; i++)
        if (is_routine(list->items[i]) && strcmp(routine_name(list->items[i]), name) == 0)
            return i;
    return SIZE_MAX;
}

static const AstNode *find_duplicate_routine(const AstNodeList *list)
{
    for (size_t i = 0; i < list->count; i++) {
        const AstNode *routine = list->items[i];
        if (is_routine(routine) && find_routine(list, routine_name(routine)) != i)
            return routine;
    }
    return NULL;
}

static int register_routines(Loader *l, const AstNode *program, const char *path)
{
    const AstNodeList *list = program->data.program.elements;
    for (size_t i = 0; i < list->count; i++)
        if (is_routine(list->items[i]) && !origin_set(l, list->items[i], path)) return 0;
    return 1;
}

/* Libera un módulo ya consumido; las rutinas movidas ya no están en su lista. */
static void release_module(Loader *l, AstNode *module)
{
    const AstNodeList *list = module->data.program.elements;
    for (size_t i = 0; i < list->count; i++)
        if (is_routine(list->items[i])) origin_forget(l, list->items[i]);
    ast_free(module);
}

/* ------------------------------------------------------- cierre de llamadas */

typedef struct {
    const AstNodeList *routines;
    unsigned char *needed;
    const char *source_file;
} Closure;

static void mark_node(Closure *c, AstNode *node);

static void mark_list(Closure *c, const AstNodeList *list)
{
    if (list == NULL) return;
    for (size_t i = 0; i < list->count; i++) mark_node(c, list->items[i]);
}

static void mark_routine(Closure *c, size_t index)
{
    if (c->needed[index]) return;
    c->needed[index] = 1;
    mark_node(c, c->routines->items[index]->data.routine_declaration.body);
}

/* Recorre los mismos hijos que ast_free; solo las llamadas generan trabajo. */
static void mark_node(Closure *c, AstNode *n)
{
    if (n == NULL) return;
    switch (n->kind) {
    case AST_ROUTINE_DECLARATION: mark_node(c, n->data.routine_declaration.body); break;
    case AST_BLOCK:               mark_list(c, n->data.block.statements); break;
    case AST_OUTPUT_STATEMENT:    mark_node(c, n->data.output_statement.value); break;
    case AST_ARRAY_LITERAL:       mark_list(c, n->data.array_literal.elements); break;
    case AST_UNARY_EXPRESSION:    mark_node(c, n->data.unary_expression.operand); break;
    case AST_VARIABLE_DECLARATION: mark_list(c, n->data.variable_declaration.dimensions); break;
    case AST_INDEX_EXPRESSION:
        mark_node(c, n->data.index_expression.base);
        mark_list(c, n->data.index_expression.indices);
        break;
    case AST_ASSIGNMENT:
        mark_node(c, n->data.assignment.target);
        mark_node(c, n->data.assignment.value);
        break;
    case AST_BINARY_EXPRESSION:
        mark_node(c, n->data.binary_expression.left);
        mark_node(c, n->data.binary_expression.right);
        break;
    case AST_WHEN_STATEMENT:
        mark_node(c, n->data.when_statement.condition);
        mark_node(c, n->data.when_statement.then_block);
        mark_node(c, n->data.when_statement.fallback_block);
        break;
    case AST_ALWHEN_STATEMENT:
        mark_node(c, n->data.alwhen_statement.condition);
        mark_node(c, n->data.alwhen_statement.body);
        break;
    case AST_LOOP_STATEMENT:
        mark_node(c, n->data.loop_statement.begin);
        mark_node(c, n->data.loop_statement.end);
        mark_node(c, n->data.loop_statement.step);
        mark_node(c, n->data.loop_statement.body);
        break;
    case AST_CALL_EXPRESSION: {
        size_t index = find_routine(c->routines, n->data.call_expression.routine_name);
        /* Una llamada importada ya se comprobó en su archivo de origen. */
        if (n->source_file == c->source_file)
            n->data.call_expression.module_target_missing = index == SIZE_MAX;
        if (index != SIZE_MAX && !n->data.call_expression.module_target_missing)
            mark_routine(c, index);
        mark_list(c, n->data.call_expression.arguments);
        break;
    }
    default: break;
    }
}

/* ------------------------------------------------------ archivos y análisis */

/* Busca primero junto al archivo importador y luego en cada -I. */
static char *locate(const Loader *l, const char *self, const char *literal)
{
    char candidate[PATH_MAX];
    struct stat info;
    const char *slash = strrchr(self, '/');
    int n = slash ? snprintf(candidate, sizeof candidate, "%.*s/%s", (int)(slash - self), self, literal)
                  : snprintf(candidate, sizeof candidate, "%s", literal);
    for (size_t i = 0; ; i++) {
        if (n > 0 && (size_t)n < sizeof candidate) {
            char *resolved = realpath(candidate, NULL);
            if (resolved && stat(resolved, &info) == 0 && S_ISREG(info.st_mode)) return resolved;
            free(resolved);
        }
        if (i >= l->dir_count) return NULL;
        n = snprintf(candidate, sizeof candidate, "%s/%s", l->dirs[i], literal);
    }
}

static AstNode *parse_module(const char *path, const char *interned_path)
{
    AstNode *ast = NULL;
    FILE *input = fopen(path, "rb");
    SymbolTable *symbols = input ? symbol_table_create() : NULL;
    int status;

    if (!symbols) {
        if (input) fclose(input);
        return NULL;
    }
    yyrestart(input);
    status = yyparse(&ast, symbols);
    (void)yylex_destroy();            /* el lexer queda listo para otro archivo */
    fclose(input);
    symbol_table_destroy(symbols);    /* la semántica reconstruye su propia tabla */
    if (status != 0 || ast == NULL || ast->kind != AST_PROGRAM) {
        ast_free(ast);
        return NULL;
    }
    ast_set_source_file(ast, interned_path);
    return ast;
}

static void report_cycle(const char *self, SourceLocation location,
                         const Frame *frame, const Frame *hit, const char *target)
{
    char chain[512] = "";
    size_t used = 0;
    /* Del módulo que cierra el ciclo hacia el actual: a -> b -> a. */
    const Frame *stack[64];
    size_t depth = 0;
    for (const Frame *f = frame; f && depth < 64; f = f->parent) {
        stack[depth++] = f;
        if (f == hit) break;
    }
    while (depth-- > 0 && used < sizeof chain)
        used += (size_t)snprintf(chain + used, sizeof chain - used, "'%s' -> ",
                                 base_name(stack[depth]->path));
    report(self, location, DIAGNOSTIC_CODE_IMPORT_UNRESOLVED,
           "importación circular detectada: %s'%s'", chain, base_name(target));
}

/* ----------------------------------------------------------- resolución real */

static void resolve(Loader *l, AstNode *program, const char *self, const Frame *parent);

/* Mueve la rutina j del módulo al programa, salvo duplicado o conflicto. */
static void splice_routine(Loader *l, AstNodeList *old, AstNodeList *out,
                           AstNodeList *module, size_t j, const char *path,
                           const char *self, SourceLocation location)
{
    AstNode *routine = module->items[j];
    const char *name = routine_name(routine);
    const char *incoming = origin_get(l, routine);
    AstNodeList *scopes[2] = {out, old};

    for (int s = 0; s < 2; s++) {
        size_t index = find_routine(scopes[s], name);
        if (index == SIZE_MAX) continue;
        const char *other = origin_get(l, scopes[s]->items[index]);
        if (other == incoming) return;
        if (other == self || other == NULL)
            report(self, location, DIAGNOSTIC_CODE_IMPORT_UNRESOLVED,
                   "conflicto de nombres: '%s' ya está declarado en '%s' y fue importado desde '%s'",
                   name, base_name(self), base_name(path));
        else
            report(self, location, DIAGNOSTIC_CODE_IMPORT_UNRESOLVED,
                   "conflicto de nombres: '%s' fue importado tanto desde '%s' como desde '%s'",
                   name, base_name(other), base_name(path));
        raise_status(l, MODULE_FATAL);
        return;
    }

    if (!ast_node_list_append(out, routine)) {
        raise_status(l, MODULE_FATAL);
        return;
    }
    module->items[j] = NULL;
}

static void import_one(Loader *l, AstNodeList *old, AstNodeList *out,
                       const AstNode *import, const char *self, const Frame *frame)
{
    const char *symbol = import->data.import_declaration.symbol;
    const char *literal = import->data.import_declaration.file_path;
    SourceLocation location = import->location;
    char *found = locate(l, self, literal);
    const char *path = found ? intern(l, found) : NULL;
    AstNode *module;
    AstNodeList *routines;
    size_t target;
    int exists = found != NULL;

    free(found);
    if (!exists) {
        report(self, location, DIAGNOSTIC_CODE_IMPORT_UNRESOLVED,
               "no se encontró el archivo '%s'", literal);
        raise_status(l, MODULE_ERRORS);
        return;
    }
    if (!path) { raise_status(l, MODULE_FATAL); return; }
    for (const Frame *f = frame; f; f = f->parent)
        if (f->path == path) {
            report_cycle(self, location, frame, f, path);
            raise_status(l, MODULE_FATAL);
            return;
        }
    module = parse_module(path, path);
    if (!module) {
        report(self, location, DIAGNOSTIC_CODE_IMPORT_UNRESOLVED,
               "no se pudo analizar el módulo '%s'", literal);
        raise_status(l, MODULE_ERRORS);
        return;
    }
    const AstNode *duplicate = find_duplicate_routine(module->data.program.elements);
    if (duplicate) {
        report(self, location, DIAGNOSTIC_CODE_DUPLICATE_DECLARATION,
               "declaración duplicada de la rutina '%s' en '%s', línea %d",
               routine_name(duplicate), literal, duplicate->location.first_line);
        raise_status(l, MODULE_FATAL);
        ast_free(module);
        return;
    }
    if (!register_routines(l, module, path)) raise_status(l, MODULE_FATAL);
    else resolve(l, module, path, frame);          /* sus propios bring */
    if (l->status == MODULE_FATAL) { release_module(l, module); return; }

    routines = module->data.program.elements;
    target = find_routine(routines, symbol);
    if (target == SIZE_MAX || strcmp(symbol, "main") == 0) {
        report(self, location, DIAGNOSTIC_CODE_IMPORT_UNRESOLVED,
               "no se encontró el símbolo '%s' en el archivo '%s'", symbol, literal);
        raise_status(l, MODULE_ERRORS);
    } else {
        Closure closure = {routines, calloc(routines->count, 1), path};
        if (!closure.needed) raise_status(l, MODULE_FATAL);
        else {
            mark_routine(&closure, target);
            for (size_t j = 0; j < routines->count && l->status != MODULE_FATAL; j++)
                if (closure.needed[j])
                    splice_routine(l, old, out, routines, j, path, self, location);
            free(closure.needed);
        }
    }
    release_module(l, module);
}

/* Sustituye cada importación del programa por las rutinas que trae. */
static void resolve(Loader *l, AstNode *program, const char *self, const Frame *parent)
{
    Frame frame = {self, parent};
    AstNodeList *old = program->data.program.elements;
    AstNodeList *out = ast_node_list_create();

    if (!out) { raise_status(l, MODULE_FATAL); return; }
    for (size_t i = 0; i < old->count && l->status != MODULE_FATAL; i++) {
        AstNode *node = old->items[i];
        if (node->kind == AST_IMPORT_DECLARATION) {
            import_one(l, old, out, node, self, &frame);
            old->items[i] = NULL;
            ast_free(node);
        } else if (ast_node_list_append(out, node)) {
            old->items[i] = NULL;
        } else {
            raise_status(l, MODULE_FATAL);
        }
    }

    for (size_t k = 0; k < old->count; k++) {
        if (old->items[k] != NULL) {
            ast_free(old->items[k]);
            old->items[k] = NULL;
        }
    }
    free(old->items);
    free(old);
    if (l->status == MODULE_FATAL) {
        program->data.program.elements = ast_node_list_create();
        ast_node_list_free(out);
        return;
    }
    program->data.program.elements = out;
}

/* Conserva el origen de las rutinas importadas y toma las rutas del loader. */
static SourceMap *build_source_map(Loader *l, const AstNode *program, const char *self)
{
    const AstNodeList *list = program->data.program.elements;
    SourceMap *map = calloc(1, sizeof *map);
    if (!map) return NULL;
    map->entries = calloc(list->count ? list->count : 1, sizeof *map->entries);
    if (!map->entries) { free(map); return NULL; }
    for (size_t i = 0; i < list->count; i++) {
        const char *origin = is_routine(list->items[i]) ? origin_get(l, list->items[i]) : NULL;
        if (origin && origin != self)
            map->entries[map->count++] = (Source){list->items[i], origin};
    }
    map->paths = l->paths;               /* las rutas pasan al mapa */
    map->path_count = l->path_count;
    l->paths = NULL;
    l->path_count = 0;
    return map;
}

ModuleStatus module_resolve_imports(AstNode *program, const char *source_path,
                                    const char *const *include_dirs,
                                    size_t include_count, SourceMap **sources)
{
    Loader loader = {.dirs = include_dirs, .dir_count = include_dirs ? include_count : 0};
    char *canonical = realpath(source_path, NULL);
    const char *self = intern(&loader, canonical ? canonical : source_path);

    if (sources) *sources = NULL;
    free(canonical);
    if (program == NULL || program->kind != AST_PROGRAM || !self ||
        !register_routines(&loader, program, self))
        loader.status = MODULE_FATAL;
    else
        resolve(&loader, program, self, NULL);
    if (loader.status != MODULE_FATAL && sources &&
        !(*sources = build_source_map(&loader, program, self)))
        loader.status = MODULE_FATAL;
    loader_free(&loader);
    return loader.status;
}
