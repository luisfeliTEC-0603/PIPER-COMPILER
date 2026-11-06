/**
 * @file clone_detector.c
 * @brief Detección de clones Tipo I y Tipo II sobre el AST de Piper.
 *
 * Definiciones usadas:
 *  - Tipo I  (CLONE_MODE_EXACT): fragmentos idénticos token a token
 *    (se ignoran solo espacios y comentarios, que el AST ya no contiene).
 *  - Tipo II (CLONE_MODE_RENAMED): fragmentos con la MISMA estructura, donde
 *    pueden cambiar los identificadores (variables, parámetros, variable de
 *    control de loop, nombres de rutinas), los literales y los tipos
 *    declarados, siempre que el renombrado sea CONSISTENTE y biyectivo
 *    (`x + x` NO es clon de `a + b`).
 *
 * Los modos son EXCLUYENTES: el modo renamed solo reporta grupos que
 * contienen al menos una diferencia real; si todas las ubicaciones del grupo
 * son idénticas entre sí, es un Tipo I y se reporta únicamente en modo exact.
 *
 * Estrategia:
 *  1. Recorrer el AST y, para cada nodo "frontera" (sentencia, bloque, rutina)
 *     con al menos min_nodes nodos, calcular el hash de su subárbol de forma
 *     INDEPENDIENTE. El hash es una serialización en pre-orden (sensible al
 *     orden de los hijos) y el renombrado de Tipo II se numera por primera
 *     aparición DENTRO de cada fragmento, no del programa completo.
 *  2. Agrupar fragmentos por hash; los grupos con >= 2 ubicaciones son clones.
 *  3. Consolidar para no reportar el mismo clon en varias granularidades.
 */

#include "clone_detector.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define INITIAL_CAPACITY 64

/* Si vale 1, el modo renamed también ignora los tipos de datos declarados
 * (definición clásica de Tipo II: identificadores, literales y tipos).
 * Poner 0 para exigir que los tipos coincidan. */
#define TYPE2_IGNORES_DATA_TYPES 1

/* ============================================================
 * 1. Hashing (FNV-1a de 64 bits)
 * ============================================================ */

static uint64_t fnv1a_init(void) { return 1469598103934665603ULL; }

static uint64_t fnv1a_byte(uint64_t hash, unsigned char byte)
{
    hash ^= byte;
    hash *= 1099511628211ULL;
    return hash;
}

static uint64_t fnv1a_u64(uint64_t hash, uint64_t value)
{
    for (int i = 0; i < 8; ++i)
        hash = fnv1a_byte(hash, (unsigned char)(value >> (i * 8)));
    return hash;
}

/* Marcadores para que distintas "formas" de dato no colisionen entre sí. */
#define MARK_NULL_CHILD 0xFFFFFFFF00000001ULL
#define MARK_NAME_IDX   0xFFFFFFFF00000002ULL

/* ============================================================
 * 2. Tablas de renombrado (por fragmento)
 * ============================================================ */

/* Asigna a cada nombre distinto un índice según su primera aparición dentro
 * del fragmento. Dos fragmentos son Tipo II si y solo si existe un
 * renombrado biyectivo entre ellos, que es exactamente lo que produce esta
 * numeración. Variables y rutinas viven en espacios de nombres distintos. */
typedef struct {
    const char **items;
    size_t count;
    size_t capacity;
} NameTable;

typedef struct {
    NameTable vars;
    NameTable routines;
} RenameState;

static int name_table_intern(NameTable *table, const char *name)
{
    if (!name) name = "";
    for (size_t i = 0; i < table->count; ++i)
        if (strcmp(table->items[i], name) == 0) return (int)i;
    if (table->count == table->capacity) {
        size_t next = table->capacity == 0 ? 16 : table->capacity * 2;
        const char **resized = realloc(table->items, next * sizeof(*resized));
        if (!resized) return -1;
        table->items = resized;
        table->capacity = next;
    }
    table->items[table->count] = name;
    return (int)table->count++;
}

static void rename_state_reset(RenameState *state)
{
    state->vars.count = 0;
    state->routines.count = 0;
}

static void rename_state_free(RenameState *state)
{
    free(state->vars.items);
    free(state->routines.items);
}

/* ============================================================
 * 3. Acceso genérico a los hijos de un nodo
 * ============================================================ */

/* Los hijos se exponen en un orden fijo por tipo de nodo. Un hijo ausente
 * (p. ej. el fallback opcional) se devuelve como NULL: ocupa su posición. */
static size_t child_count(const AstNode *n)
{
    switch (n->kind) {
    case AST_PROGRAM:          return n->data.program.elements->count;
    case AST_ROUTINE_DECLARATION:
        return n->data.routine_declaration.parameters->count + 1;
    case AST_BLOCK:            return n->data.block.statements->count;
    case AST_ASSIGNMENT:       return 2;
    case AST_WHEN_STATEMENT:   return 3;
    case AST_ALWHEN_STATEMENT: return 2;
    case AST_LOOP_STATEMENT:   return 4;
    case AST_OUTPUT_STATEMENT: return 1;
    case AST_CALL_EXPRESSION:  return n->data.call_expression.arguments->count;
    case AST_INDEX_EXPRESSION: return 1 + n->data.index_expression.indices->count;
    case AST_UNARY_EXPRESSION: return 1;
    case AST_BINARY_EXPRESSION:return 2;
    case AST_ARRAY_LITERAL:    return n->data.array_literal.elements->count;
    case AST_VARIABLE_DECLARATION:
        return n->data.variable_declaration.dimensions->count;
    case AST_PARAMETER:        return n->data.parameter.dimensions->count;
    default:                   return 0;
    }
}

static const AstNode *child_at(const AstNode *n, size_t i)
{
    switch (n->kind) {
    case AST_PROGRAM:          return n->data.program.elements->items[i];
    case AST_ROUTINE_DECLARATION: {
        size_t params = n->data.routine_declaration.parameters->count;
        return i < params ? n->data.routine_declaration.parameters->items[i]
                          : n->data.routine_declaration.body;
    }
    case AST_BLOCK:            return n->data.block.statements->items[i];
    case AST_ASSIGNMENT:
        return i == 0 ? n->data.assignment.target : n->data.assignment.value;
    case AST_WHEN_STATEMENT:
        return i == 0 ? n->data.when_statement.condition
             : i == 1 ? n->data.when_statement.then_block
                      : n->data.when_statement.fallback_block;
    case AST_ALWHEN_STATEMENT:
        return i == 0 ? n->data.alwhen_statement.condition
                      : n->data.alwhen_statement.body;
    case AST_LOOP_STATEMENT:
        return i == 0 ? n->data.loop_statement.begin
             : i == 1 ? n->data.loop_statement.end
             : i == 2 ? n->data.loop_statement.step
                      : n->data.loop_statement.body;
    case AST_OUTPUT_STATEMENT: return n->data.output_statement.value;
    case AST_CALL_EXPRESSION:  return n->data.call_expression.arguments->items[i];
    case AST_INDEX_EXPRESSION:
        return i == 0 ? n->data.index_expression.base
                      : n->data.index_expression.indices->items[i - 1];
    case AST_UNARY_EXPRESSION: return n->data.unary_expression.operand;
    case AST_BINARY_EXPRESSION:
        return i == 0 ? n->data.binary_expression.left
                      : n->data.binary_expression.right;
    case AST_ARRAY_LITERAL:    return n->data.array_literal.elements->items[i];
    case AST_VARIABLE_DECLARATION:
        return n->data.variable_declaration.dimensions->items[i];
    case AST_PARAMETER:        return n->data.parameter.dimensions->items[i];
    default:                   return NULL;
    }
}

/* Cuenta los nodos de un subárbol (para filtrar por min_nodes). */
static int count_nodes(const AstNode *node)
{
    if (!node) return 0;
    int total = 1;
    size_t n = child_count(node);
    for (size_t i = 0; i < n; ++i) total += count_nodes(child_at(node, i));
    return total;
}

/* ============================================================
 * 4. Hash de un subárbol
 * ============================================================ */

typedef struct {
    CloneMode mode;
    RenameState *names;
    uint64_t hash;
} HashCtx;

static void feed_u64(HashCtx *c, uint64_t v) { c->hash = fnv1a_u64(c->hash, v); }

static void feed_string(HashCtx *c, const char *s)
{
    if (!s) s = "";
    for (; *s; ++s) c->hash = fnv1a_byte(c->hash, (unsigned char)*s);
    c->hash = fnv1a_byte(c->hash, 0); /* terminador: "ab"+"c" != "a"+"bc" */
}

/* Un nombre: literal en modo exact; índice de primera aparición en renamed. */
static void feed_name(HashCtx *c, NameTable *table, const char *name)
{
    if (c->mode == CLONE_MODE_RENAMED) {
        feed_u64(c, MARK_NAME_IDX);
        feed_u64(c, (uint64_t)(int64_t)name_table_intern(table, name));
    } else {
        feed_string(c, name);
    }
}

static void feed_type(HashCtx *c, int type)
{
#if TYPE2_IGNORES_DATA_TYPES
    if (c->mode == CLONE_MODE_RENAMED) return;
#endif
    feed_u64(c, (uint64_t)(int64_t)type);
}

static void hash_node(HashCtx *c, const AstNode *node)
{
    if (!node) { feed_u64(c, MARK_NULL_CHILD); return; }

    feed_u64(c, (uint64_t)node->kind);

    switch (node->kind) {
    case AST_IDENTIFIER_EXPRESSION:
        feed_name(c, &c->names->vars, node->data.identifier.name);
        break;
    case AST_INTEGER_LITERAL:
        if (c->mode == CLONE_MODE_EXACT)
            feed_u64(c, node->data.integer_literal.value);
        break;
    case AST_BOOLEAN_LITERAL:
        if (c->mode == CLONE_MODE_EXACT)
            feed_u64(c, node->data.boolean_literal.value ? 1 : 0);
        break;
    case AST_UNARY_EXPRESSION:
        feed_u64(c, (uint64_t)(int64_t)node->data.unary_expression.operator);
        break;
    case AST_BINARY_EXPRESSION:
        feed_u64(c, (uint64_t)(int64_t)node->data.binary_expression.operator);
        break;
    case AST_CALL_EXPRESSION:
        feed_name(c, &c->names->routines, node->data.call_expression.routine_name);
        break;
    case AST_LOOP_STATEMENT:
        feed_name(c, &c->names->vars, node->data.loop_statement.control_name);
        break;
    case AST_VARIABLE_DECLARATION:
        feed_type(c, (int)node->data.variable_declaration.type);
        feed_name(c, &c->names->vars, node->data.variable_declaration.name);
        break;
    case AST_PARAMETER:
        feed_type(c, (int)node->data.parameter.type);
        feed_name(c, &c->names->vars, node->data.parameter.name);
        break;
    case AST_ROUTINE_DECLARATION:
        feed_type(c, (int)node->data.routine_declaration.return_type);
        feed_name(c, &c->names->routines, node->data.routine_declaration.name);
        break;
    case AST_IMPORT_DECLARATION:
        feed_name(c, &c->names->routines, node->data.import_declaration.symbol);
        if (c->mode == CLONE_MODE_EXACT)
            feed_string(c, node->data.import_declaration.file_path);
        break;
    default:
        break;
    }

    /* La aridad evita ambigüedades y los hijos van EN ORDEN: a - b != b - a,
     * y [0,0] != [0,0,0,0]. */
    size_t n = child_count(node);
    feed_u64(c, (uint64_t)n);
    for (size_t i = 0; i < n; ++i) hash_node(c, child_at(node, i));
}

/* Hash independiente de un fragmento: el renombrado empieza desde cero. */
static uint64_t hash_fragment(const AstNode *node, CloneMode mode,
                              RenameState *names)
{
    rename_state_reset(names);
    HashCtx ctx = { mode, names, fnv1a_init() };
    feed_u64(&ctx, (uint64_t)mode);
    hash_node(&ctx, node);
    return ctx.hash;
}

/* ============================================================
 * 5. Recolección de fragmentos
 * ============================================================ */

typedef struct {
    const AstNode *node;
    uint64_t hash;        /* hash según el modo solicitado */
    uint64_t exact_hash;  /* hash exacto (para distinguir Tipo I de Tipo II) */
    int node_count;
} Fragment;

typedef struct {
    Fragment *items;
    size_t count;
    size_t capacity;
} FragmentList;

static int fragment_list_push(FragmentList *list, Fragment fragment)
{
    if (list->count == list->capacity) {
        size_t next = list->capacity == 0 ? INITIAL_CAPACITY : list->capacity * 2;
        Fragment *resized = realloc(list->items, next * sizeof(*resized));
        if (!resized) return 0;
        list->items = resized;
        list->capacity = next;
    }
    list->items[list->count++] = fragment;
    return 1;
}

/* Nodos que pueden ser un fragmento reportable. Expresiones sueltas no. */
static int is_fragment_root(AstKind kind)
{
    switch (kind) {
    case AST_ASSIGNMENT:
    case AST_WHEN_STATEMENT:
    case AST_ALWHEN_STATEMENT:
    case AST_LOOP_STATEMENT:
    case AST_OUTPUT_STATEMENT:
    case AST_CALL_EXPRESSION:
    case AST_BREAK_STATEMENT:
    case AST_VARIABLE_DECLARATION:
    case AST_BLOCK:
    case AST_ROUTINE_DECLARATION:
        return 1;
    default:
        return 0;
    }
}

/* Recorre el AST en pre-orden y recolecta fragmentos. Un subárbol con menos
 * de min_nodes nodos no puede contener fragmentos válidos: se poda. */
static int collect(const AstNode *node, CloneMode mode, int min_nodes,
                   RenameState *names, FragmentList *fragments)
{
    if (!node) return 1;

    int count = count_nodes(node);
    if (count < min_nodes) return 1;

    if (is_fragment_root(node->kind)) {
        Fragment f = { node, 0, 0, count };
        f.exact_hash = hash_fragment(node, CLONE_MODE_EXACT, names);
        f.hash = mode == CLONE_MODE_EXACT
                     ? f.exact_hash
                     : hash_fragment(node, CLONE_MODE_RENAMED, names);
        if (!fragment_list_push(fragments, f)) return 0;
    }

    size_t n = child_count(node);
    for (size_t i = 0; i < n; ++i)
        if (!collect(child_at(node, i), mode, min_nodes, names, fragments))
            return 0;
    return 1;
}

/* ============================================================
 * 6. Agrupación
 * ============================================================ */

typedef struct HashBucket {
    uint64_t hash;
    size_t *indices;
    size_t count;
    size_t capacity;
    struct HashBucket *next;
} HashBucket;

static HashBucket *find_or_create_bucket(HashBucket **head, HashBucket **tail,
                                         uint64_t hash)
{
    for (HashBucket *b = *head; b; b = b->next)
        if (b->hash == hash) return b;
    HashBucket *bucket = calloc(1, sizeof(*bucket));
    if (!bucket) return NULL;
    bucket->hash = hash;
    /* Se agrega al final para conservar el orden de aparición en el fuente. */
    if (*tail) (*tail)->next = bucket; else *head = bucket;
    *tail = bucket;
    return bucket;
}

static int bucket_push(HashBucket *bucket, size_t index)
{
    if (bucket->count == bucket->capacity) {
        size_t next = bucket->capacity == 0 ? 4 : bucket->capacity * 2;
        size_t *resized = realloc(bucket->indices, next * sizeof(*resized));
        if (!resized) return 0;
        bucket->indices = resized;
        bucket->capacity = next;
    }
    bucket->indices[bucket->count++] = index;
    return 1;
}

static void buckets_free(HashBucket *buckets)
{
    while (buckets) {
        HashBucket *next = buckets->next;
        free(buckets->indices);
        free(buckets);
        buckets = next;
    }
}

/* ============================================================
 * 7. Consolidación
 * ============================================================ */

static int range_equal(const CloneLocation *a, const CloneLocation *b)
{
    return a->start_line == b->start_line && a->start_column == b->start_column &&
           a->end_line == b->end_line && a->end_column == b->end_column;
}

/* outer contiene a inner (incluye igualdad). */
static int range_contains(const CloneLocation *outer, const CloneLocation *inner)
{
    if (outer->start_line == 0 || inner->start_line == 0) return 0;
    return (outer->start_line < inner->start_line ||
            (outer->start_line == inner->start_line &&
             outer->start_column <= inner->start_column)) &&
           (outer->end_line > inner->end_line ||
            (outer->end_line == inner->end_line &&
             outer->end_column >= inner->end_column));
}

/* ¿Todas las ubicaciones de `small` están dentro de alguna ubicación de
 * `big`, y esta es estrictamente mayor? Entonces `big` ya explica a `small`:
 * reportar ambos sería el mismo clon a dos granularidades. */
static int group_subsumed_by(const CloneGroup *small, const CloneGroup *big)
{
    for (size_t i = 0; i < small->location_count; ++i) {
        const CloneLocation *s = &small->locations[i];
        int covered = 0;
        for (size_t j = 0; j < big->location_count; ++j) {
            const CloneLocation *b = &big->locations[j];
            if (range_contains(b, s) && b->node_count > s->node_count) {
                covered = 1;
                break;
            }
        }
        if (!covered) return 0;
    }
    return 1;
}

/* ============================================================
 * 8. API pública
 * ============================================================ */

CloneReport *clone_detect(const AstNode *root, int min_nodes,
                          const char *file_path, CloneMode mode)
{
    (void)file_path;
    if (!root || min_nodes < 1) return NULL;

    FragmentList fragments = {0};
    RenameState names = {0};
    if (!collect(root, mode, min_nodes, &names, &fragments)) {
        free(fragments.items);
        rename_state_free(&names);
        return NULL;
    }
    rename_state_free(&names);

    CloneReport *report = calloc(1, sizeof(*report));
    if (!report) {
        free(fragments.items);
        return NULL;
    }
    report->fragment_count = fragments.count;

    /* --- Paso 1: agrupar fragmentos por hash. --- */
    HashBucket *buckets = NULL, *buckets_tail = NULL;
    for (size_t i = 0; i < fragments.count; ++i) {
        HashBucket *bucket = find_or_create_bucket(&buckets, &buckets_tail,
                                                   fragments.items[i].hash);
        if (!bucket || !bucket_push(bucket, i)) {
            clone_report_free(report);
            free(fragments.items);
            buckets_free(buckets);
            return NULL;
        }
    }

    /* --- Paso 2: grupos brutos (>= 2 ubicaciones), en orden de fuente. ---
     * En modo renamed se descartan los grupos cuyas ubicaciones son todas
     * idénticas entre sí: son Tipo I, no Tipo II. */
    CloneGroup *raw_head = NULL, *raw_tail = NULL;
    size_t raw_count = 0;
    for (HashBucket *b = buckets; b; b = b->next) {
        if (b->count < 2) continue;

        if (mode == CLONE_MODE_RENAMED) {
            uint64_t first = fragments.items[b->indices[0]].exact_hash;
            int all_identical = 1;
            for (size_t i = 1; i < b->count; ++i)
                if (fragments.items[b->indices[i]].exact_hash != first) {
                    all_identical = 0;
                    break;
                }
            if (all_identical) continue;
        }

        CloneGroup *group = calloc(1, sizeof(*group));
        if (!group) continue;
        group->mode = mode;
        group->hash = b->hash;
        group->location_count = b->count;
        group->locations = calloc(b->count, sizeof(CloneLocation));
        if (!group->locations) { free(group); continue; }
        for (size_t i = 0; i < b->count; ++i) {
            const Fragment *f = &fragments.items[b->indices[i]];
            group->locations[i] = (CloneLocation){
                f->node->location.first_line,
                f->node->location.first_column,
                f->node->location.last_line,
                f->node->location.last_column,
                f->node_count
            };
        }
        if (raw_tail) raw_tail->next = group; else raw_head = group;
        raw_tail = group;
        raw_count++;
    }
    buckets_free(buckets);
    free(fragments.items);

    /* --- Paso 3: dentro de cada grupo, quedarse con los fragmentos
     * maximales (si uno contiene a otro del mismo grupo, se descarta el
     * interno; con rangos idénticos se conserva solo el primero). --- */
    for (CloneGroup *g = raw_head; g; g = g->next) {
        size_t kept = 0;
        CloneLocation *out = calloc(g->location_count, sizeof(*out));
        if (!out) continue;
        for (size_t i = 0; i < g->location_count; ++i) {
            int inner = 0;
            for (size_t j = 0; j < g->location_count && !inner; ++j) {
                if (i == j) continue;
                if (range_equal(&g->locations[j], &g->locations[i]))
                    inner = j < i;
                else
                    inner = range_contains(&g->locations[j], &g->locations[i]);
            }
            if (!inner) out[kept++] = g->locations[i];
        }
        free(g->locations);
        g->locations = out;
        g->location_count = kept;
    }

    /* --- Paso 4: consolidación entre grupos. Se decide contra una FOTO del
     * estado tras el paso 3, así el resultado no depende del orden. --- */
    unsigned char *drop = calloc(raw_count ? raw_count : 1, 1);
    CloneGroup **arr = calloc(raw_count ? raw_count : 1, sizeof(*arr));
    if (!drop || !arr) {
        free(drop); free(arr);
        report->groups = raw_head;
        report->group_count = raw_count;
        return report;
    }
    size_t n = 0;
    for (CloneGroup *g = raw_head; g; g = g->next) arr[n++] = g;
    for (size_t i = 0; i < n; ++i) {
        if (arr[i]->location_count < 2) { drop[i] = 1; continue; }
        for (size_t j = 0; j < n && !drop[i]; ++j) {
            if (i == j || arr[j]->location_count < 2) continue;
            if (group_subsumed_by(arr[i], arr[j])) drop[i] = 1;
        }
    }

    /* --- Paso 5: transferir al reporte, conservando el orden. --- */
    CloneGroup *tail = NULL;
    for (size_t i = 0; i < n; ++i) {
        CloneGroup *g = arr[i];
        if (drop[i]) {
            free(g->locations);
            free(g);
            continue;
        }
        g->next = NULL;
        if (tail) tail->next = g; else report->groups = g;
        tail = g;
        report->group_count++;
    }
    free(drop);
    free(arr);
    return report;
}

void clone_report_free(CloneReport *report)
{
    if (!report) return;
    CloneGroup *g = report->groups;
    while (g) {
        CloneGroup *next = g->next;
        free(g->locations);
        free(g);
        g = next;
    }
    free(report);
}

void clone_report_write_json(FILE *output, const CloneReport *report,
                             const char *file_path, int min_nodes,
                             CloneMode mode)
{
    if (!output || !report) return;

    fputs("{\n  \"version\": 1,\n", output);
    fprintf(output, "  \"mode\": \"%s\",\n",
            mode == CLONE_MODE_EXACT ? "exact" : "renamed");
    fprintf(output, "  \"min_nodes\": %d,\n", min_nodes);
    fputs("  \"source\": \"", output);
    for (const char *c = file_path ? file_path : ""; *c; ++c) {
        unsigned char u = (unsigned char)*c;
        if (u == '"' || u == '\\') {
            fputc('\\', output);
            fputc(u, output);
        } else if (u >= 0x20 && u < 0x7f) {
            fputc(u, output);
        } else {
            fputc('?', output);
        }
    }
    fputs("\",\n", output);
    fprintf(output, "  \"fragments\": %zu,\n", report->fragment_count);
    fputs("  \"groups\": [", output);

    int first = 1;
    for (const CloneGroup *g = report->groups; g; g = g->next) {
        if (!first) fputc(',', output);
        first = 0;
        fputs("\n    {\n", output);
        fprintf(output, "      \"type\": \"%s\",\n",
                g->mode == CLONE_MODE_EXACT ? "I" : "II");
        fprintf(output, "      \"hash\": \"%016llx\",\n",
                (unsigned long long)g->hash);
        fputs("      \"locations\": [", output);
        for (size_t i = 0; i < g->location_count; ++i) {
            if (i) fputc(',', output);
            const CloneLocation *loc = &g->locations[i];
            fprintf(output,
                    "\n        {\"start_line\": %d, \"start_column\": %d, "
                    "\"end_line\": %d, \"end_column\": %d, \"node_count\": %d}",
                    loc->start_line, loc->start_column,
                    loc->end_line, loc->end_column, loc->node_count);
        }
        fputs("\n      ]\n    }", output);
    }
    fputs("\n  ]\n}\n", output);
}
