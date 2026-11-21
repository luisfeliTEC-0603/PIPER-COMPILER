/** @file ir_unroll.c
 * @brief Desenrollado de ciclos (loop unrolling) sobre el IR de Piper.
 * 
 * @details Detecta loops contados con begin/end/step constantes a partir de la
 * estructura del IR (no del AST): header canónico COPY/LT/BRANCH, un init que
 * asigna el control y salta al header, un advance que incrementa el control y
 * vuelve al header, y una región alcanzable desde el body sin entrar al header
 * ni al exit, sin CALL/INTRINSIC/RETURN ni ciclos internos, que no escribe ni
 * toma la dirección del control. Con el control fijo por el cuerpo, N es exacto
 * y el epílogo no necesita guarda. Con factor F y N = F*full + tail, el header
 * cuenta grupos completos y un epílogo de tail copias ejecuta el resto. Cada
 * copia conserva su advance, así el control termina en begin + N*step. El
 * resultado es un IrProgram nuevo y validado.
 */

#include "ir_unroll.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NPOS SIZE_MAX

/** @brief Resultado interno del análisis/aplicación de un loop. */

typedef enum { LOOP_NONE, LOOP_REJECT, LOOP_OK, LOOP_OOM } LoopResult;

/** @brief Estado de un loop candidato durante el análisis y la transformación.
 * @details region/ridx/isdef/tidx son propias y se liberan con info_free.
 * begin/step/iterations solo son válidos si el análisis retornó LOOP_OK.
 */

typedef struct {
    size_t header, adv, body_pos; /**< Posiciones del header, advance y body. */
    IrId header_id, exit_id;      /**< IDs del header y del exit. */
    const Symbol *control;        /**< Símbolo del contador del loop. */
    size_t *region;               /**< Posiciones de bloques de la región. */
    size_t region_count;          /**< Número de bloques en la región. */
    size_t *ridx;                 /**< Posición -> índice en región, o NPOS. */
    unsigned char *isdef;         /**< temporales definidos en la región. */
    size_t *tidx;                 /**< temporal -> índice denso en la región. */
    size_t temp_defs;             /**< Cantidad de temporales definidos. */
    int64_t begin, step;          /**< Begin y step constantes del loop. */
    uint64_t iterations;          /**< N = ceil((end - begin) / step). */
} LoopInfo;

/** @brief Libera los arreglos de L y lo deja a cero. */

static void info_free(LoopInfo *L)
{
    free(L->region); free(L->ridx); free(L->isdef); free(L->tidx);
    *L = (LoopInfo){0};
}

/** @brief true si o es el temporal t. */

static bool temp_is(IrOperand o, size_t t) { return o.kind == IR_OPERAND_TEMP && o.value.temp == t; }

/** @brief true si o es el símbolo s. */

static bool sym_is(IrOperand o, const Symbol *s) { return o.kind == IR_OPERAND_SYMBOL && o.value.symbol == s; }

/** @brief Posición del bloque con ID id en r, o NPOS si no existe. */

static size_t find_block(const IrRoutine *r, IrId id)
{
    for (size_t i = 0; i < r->block_count; i++)
        if (r->blocks[i].id == id) return i;
    return NPOS;
}

/** @brief Última instrucción (terminador) del bloque b. */

static const IrInstruction *last_of(const IrBlock *b) { return &b->instructions[b->instruction_count - 1]; }

/** @brief Total de instrucciones de todas las rutinas de p. */

static size_t count_instructions(const IrProgram *p)
{
    size_t n = 0;
    for (size_t r = 0; r < p->routine_count; r++)
        for (size_t b = 0; b < p->routines[r].block_count; b++)
            n += p->routines[r].blocks[b].instruction_count;
    return n;
}

/** @brief Total de bloques de todas las rutinas de p. */

static size_t count_blocks(const IrProgram *p)
{
    size_t n = 0;
    for (size_t r = 0; r < p->routine_count; r++) n += p->routines[r].block_count;
    return n;
}

/* ---------- copia profunda: solo CALL/INTRINSIC poseen memoria propia ---------- */

/** @brief Copia profunda de src.
 * 
 * @param src IR validado, prestado.
 * 
 * 
 * @return IrProgram nuevo, o NULL si falla una reserva.
 * 
 * @details Solo CALL/INTRINSIC poseen extra.call.arguments (memoria propia).
 * El resto de la instrucción se copia por valor. La lista de argumentos se
 * duplica para que ir_program_free no libere dos veces el mismo puntero.
 */

static IrProgram *copy_program(const IrProgram *src)
{
    IrProgram *dst = calloc(1, sizeof(*dst));
    if (!dst) return NULL;
    dst->symbols = src->symbols;
    dst->startup = src->startup;
    dst->routines = calloc(src->routine_count, sizeof(*dst->routines));
    if (!dst->routines) { free(dst); return NULL; }
    dst->routine_capacity = src->routine_count;
    for (size_t r = 0; r < src->routine_count; r++) {
        const IrRoutine *sr = &src->routines[r];
        IrRoutine *dr = &dst->routines[r];
        *dr = *sr;
        dr->blocks = NULL; dr->block_count = dr->block_capacity = 0;
        dst->routine_count = r + 1;
        dr->blocks = calloc(sr->block_count, sizeof(*dr->blocks));
        if (!dr->blocks) goto fail;
        dr->block_capacity = sr->block_count;
        for (size_t b = 0; b < sr->block_count; b++) {
            const IrBlock *sb = &sr->blocks[b];
            IrBlock *db = &dr->blocks[b];
            *db = (IrBlock){.id = sb->id};
            dr->block_count = b + 1;
            db->instructions = calloc(sb->instruction_count, sizeof(*db->instructions));
            if (!db->instructions) goto fail;
            db->instruction_capacity = sb->instruction_count;
            for (size_t i = 0; i < sb->instruction_count; i++) {
                IrInstruction in = sb->instructions[i];
                if ((in.op == IR_OP_CALL || in.op == IR_OP_INTRINSIC)) {
                    size_t n = in.extra.call.argument_count;
                    in.extra.call.arguments = NULL;
                    if (n) {
                        if (n > SIZE_MAX / sizeof(IrOperand)) goto fail;
                        in.extra.call.arguments = malloc(n * sizeof(IrOperand));
                        if (!in.extra.call.arguments) goto fail;
                        memcpy(in.extra.call.arguments, sb->instructions[i].extra.call.arguments,
                               n * sizeof(IrOperand));
                    }
                }
                db->instructions[i] = in;
                db->instruction_count = i + 1;
            }
        }
    }
    return dst;
fail:
    ir_program_free(dst);
    return NULL;
}

/* ---------- análisis ---------- */

/** @brief Analiza el bloque h como posible header de un loop contado.
 * 
 * @param r Rutina a analizar (prestada).
 * @param h Posición del bloque candidato a header.
 * @param o Opciones (para max_iterations).
 * @param L Salida; se libera con info_free si no retorna LOOP_OK.
 *
 * @return LOOP_NONE si no tiene forma de header; LOOP_REJECT si tiene forma
 *         pero no es transformable; LOOP_OK si es transformable (L queda
 *         lleno y el llamador debe liberarlo con info_free); LOOP_OOM si
 *         falla una reserva.
 *
 * @details Rechaza N no constante, exit==header (sin salida por condición),
 * init==exit (reinicia el loop), CALL/INTRINSIC/RETURN en la región, ciclos
 * internos, escritura o dirección del control fuera del advance, y temporales
 * de la región usados fuera de ella (el renombrado por copia los invalidaría).
 */

static LoopResult analyze(const IrRoutine *r, size_t h, const IrUnrollOptions *o,
                          LoopInfo *L, IrUnrollReason *reason)
{
    LoopResult res = LOOP_REJECT;
    size_t n = r->block_count;
    size_t *stack = NULL, *indeg = NULL, *queue = NULL;
    const IrBlock *T = &r->blocks[h];
    *L = (LoopInfo){0};
    *reason = IR_UNROLL_STRUCTURE;

    /* Forma canónica que produce ir_stmt.c para `loop`:
     *   [..., COPY cur = control ; LT c = cur, END ; BRANCH c body, exit]
     * El LT debe comparar contra una constante; si no, N no es calculable. */
    if (T->instruction_count < 3) return LOOP_NONE;
    const IrInstruction *tail3 = &T->instructions[T->instruction_count - 3];
    const IrInstruction *cp = &tail3[0], *lt = &tail3[1], *br = &tail3[2];
    if (cp->op != IR_OP_COPY || cp->arg1.kind != IR_OPERAND_SYMBOL || cp->result.kind != IR_OPERAND_TEMP ||
        lt->op != IR_OP_LT || !temp_is(lt->arg1, cp->result.value.temp) || lt->result.kind != IR_OPERAND_TEMP ||
        br->op != IR_OP_BRANCH || !temp_is(br->arg1, lt->result.value.temp))
        return LOOP_NONE;

    L->header = h;
    L->header_id = T->id;
    L->control = cp->arg1.value.symbol;
    IrId body_id = br->extra.targets[0];
    L->exit_id = br->extra.targets[1];

    if (lt->arg2.kind != IR_OPERAND_CONSTANT) {
        *reason = IR_UNROLL_NON_CONSTANT;
        return LOOP_REJECT;
    }
    if (L->exit_id == L->header_id) return LOOP_REJECT;               /* sin salida por condición */
    if (body_id == L->header_id || body_id == L->exit_id || r->entry == L->header_id) return LOOP_REJECT;
    L->body_pos = find_block(r, body_id);
    if (L->body_pos == NPOS) return LOOP_REJECT;

    /* Un loop tiene exactamente dos predecesores del header: el init (fuera
     * del loop) y el advance (dentro). Más aristas indican un IR malformado
     * o un header compartido; menos, un loop sin back-edge. */
    size_t edges = 0, pa = NPOS, pb = NPOS;
    for (size_t b = 0; b < n; b++) {
        const IrInstruction *e = last_of(&r->blocks[b]);
        size_t paths = e->op == IR_OP_BRANCH ? 2 : e->op == IR_OP_JUMP ? 1 : 0;
        for (size_t p = 0; p < paths; p++)
            if (e->extra.targets[p] == L->header_id) {
                if (edges == 0) pa = b; else if (edges == 1) pb = b;
                edges++;
            }
    }
    if (edges != 2 || pa == pb) return LOOP_REJECT;

    L->region = malloc(n * sizeof(size_t));
    L->ridx = malloc(n * sizeof(size_t));
    stack = malloc(n * sizeof(size_t));
    indeg = calloc(n, sizeof(size_t));
    queue = malloc(n * sizeof(size_t));
    if (!L->region || !L->ridx || !stack || !indeg || !queue) { res = LOOP_OOM; goto done; }
    for (size_t i = 0; i < n; i++) L->ridx[i] = NPOS;

    /* Región = alcanzable desde body sin entrar al header ni al exit.
     * Es el conjunto de bloques que se clonan. Se rechaza si contiene
     * CALL/INTRINSIC/RETURN: el unroll multiplicaría sus efectos. */
    size_t sp = 0;
    L->ridx[L->body_pos] = L->region_count; L->region[L->region_count++] = L->body_pos;
    stack[sp++] = L->body_pos;
    while (sp) {
        const IrBlock *b = &r->blocks[stack[--sp]];
        if (b->id == r->entry) goto done;
        for (size_t i = 0; i < b->instruction_count; i++) {
            IrOpcode op = b->instructions[i].op;
            if (op == IR_OP_CALL || op == IR_OP_INTRINSIC || op == IR_OP_RETURN) {
                *reason = IR_UNROLL_EFFECTS;
                goto done;
            }
        }
        const IrInstruction *e = last_of(b);
        size_t paths = e->op == IR_OP_BRANCH ? 2 : 1;
        for (size_t p = 0; p < paths; p++) {
            IrId t = e->extra.targets[p];
            if (t == L->header_id || t == L->exit_id) continue;
            size_t q = find_block(r, t);
            if (q == NPOS) goto done;
            if (L->ridx[q] == NPOS) {
                L->ridx[q] = L->region_count; L->region[L->region_count++] = q;
                stack[sp++] = q;
            }
        }
    }

    /* Exactamente un predecesor en la región (avance) y otro fuera (init). */
    bool ina = L->ridx[pa] != NPOS, inb = L->ridx[pb] != NPOS;
    if (ina == inb) goto done;
    L->adv = ina ? pa : pb;
    size_t init = ina ? pb : pa;

    /* Un init que es a la vez la salida reinicia el loop: N no describe su fin. */
    if (r->blocks[init].id == L->exit_id) goto done;

    const IrBlock *ib = &r->blocks[init];
    const IrInstruction *ie = last_of(ib);
    if (ie->op != IR_OP_JUMP || ib->instruction_count < 2) goto done;
    const IrInstruction *bi = &ib->instructions[ib->instruction_count - 2];
    if (bi->op != IR_OP_COPY || !sym_is(bi->result, L->control)) goto done;
    if (bi->arg1.kind != IR_OPERAND_CONSTANT) {
        *reason = IR_UNROLL_NON_CONSTANT;
        goto done;
    }
    L->begin = bi->arg1.value.constant;

    const IrBlock *ab = &r->blocks[L->adv];
    if (ab->instruction_count != 4) goto done;
    const IrInstruction *a0 = &ab->instructions[0], *a1 = &ab->instructions[1],
                        *a2 = &ab->instructions[2], *a3 = &ab->instructions[3];
    if (a1->arg2.kind != IR_OPERAND_CONSTANT) {
        *reason = IR_UNROLL_NON_CONSTANT;
        goto done;
    }
    if (a0->op != IR_OP_COPY || !sym_is(a0->arg1, L->control) || a0->result.kind != IR_OPERAND_TEMP ||
        a1->op != IR_OP_ADD || (a1->flags & IR_CHECKED) || !temp_is(a1->arg1, a0->result.value.temp) ||
        a1->arg2.kind != IR_OPERAND_CONSTANT || a1->arg2.value.constant <= 0 ||
        a1->result.kind != IR_OPERAND_TEMP ||
        a2->op != IR_OP_COPY || !temp_is(a2->arg1, a1->result.value.temp) || !sym_is(a2->result, L->control) ||
        a3->op != IR_OP_JUMP)
        goto done;
    L->step = a1->arg2.value.constant;

    /* Sin ciclos internos (solo se desenrolla el loop más interno). Kahn. */
    for (size_t j = 0; j < L->region_count; j++) {
        const IrInstruction *e = last_of(&r->blocks[L->region[j]]);
        size_t paths = e->op == IR_OP_BRANCH ? 2 : 1;
        for (size_t p = 0; p < paths; p++) {
            size_t q = find_block(r, e->extra.targets[p]);
            if (q != NPOS && L->ridx[q] != NPOS) indeg[L->ridx[q]]++;
        }
    }
    size_t qh = 0, qt = 0, removed = 0;
    for (size_t j = 0; j < L->region_count; j++) if (indeg[j] == 0) queue[qt++] = j;
    while (qh < qt) {
        size_t j = queue[qh++]; removed++;
        const IrInstruction *e = last_of(&r->blocks[L->region[j]]);
        size_t paths = e->op == IR_OP_BRANCH ? 2 : 1;
        for (size_t p = 0; p < paths; p++) {
            size_t q = find_block(r, e->extra.targets[p]);
            if (q != NPOS && L->ridx[q] != NPOS && --indeg[L->ridx[q]] == 0) queue[qt++] = L->ridx[q];
        }
    }
    if (removed != L->region_count) {
        *reason = IR_UNROLL_INTERNAL_CYCLE;
        goto done;
    }

    /* Si el cuerpo escribe control o toma su dirección, N deja de ser exacto
     * (el cuerpo podría alterar el contador) y el epílogo necesitaría guarda.
     * Se rechaza para mantener N exacto y el epílogo sin guarda. */
    for (size_t j = 0; j < L->region_count; j++) {
        if (L->region[j] == L->adv) continue;
        const IrBlock *b = &r->blocks[L->region[j]];
        for (size_t i = 0; i < b->instruction_count; i++) {
            const IrInstruction *in = &b->instructions[i];
            if (sym_is(in->result, L->control) || (in->op == IR_OP_ADDR && sym_is(in->arg1, L->control))) {
                *reason = IR_UNROLL_CONTROL_MODIFIED;
                goto done;
            }
        }
    }

    /* N = ceil((end - begin) / step). La suma diff + (step - 1) evita división
     * con signo y se hace en uint64_t. Se acota por max_iterations (0 = sin
     * techo) para no explotar el IR con loops enormes. */
    int64_t end = lt->arg2.value.constant;
    if (L->begin >= end) {
        *reason = IR_UNROLL_FACTOR_LIMIT;
        goto done;
    }
    uint64_t diff = (uint64_t)end - (uint64_t)L->begin, step = (uint64_t)L->step;
    if (diff > UINT64_MAX - (step - 1)) {
        *reason = IR_UNROLL_SIZE_LIMIT;
        goto done;
    }
    L->iterations = (diff + step - 1) / step;
    if (o->max_iterations && L->iterations > o->max_iterations) {
        *reason = IR_UNROLL_ITERATION_LIMIT;
        goto done;
    }

    /* Temporales de la región: renombrables, sin usos fuera de ella. */
    size_t tc = r->temp_count;
    L->isdef = calloc(tc ? tc : 1, 1);
    L->tidx = calloc(tc ? tc : 1, sizeof(size_t));
    if (!L->isdef || !L->tidx) { res = LOOP_OOM; goto done; }
    for (size_t j = 0; j < L->region_count; j++) {
        const IrBlock *b = &r->blocks[L->region[j]];
        for (size_t i = 0; i < b->instruction_count; i++)
            if (b->instructions[i].result.kind == IR_OPERAND_TEMP) {
                size_t t = b->instructions[i].result.value.temp;
                if (t >= tc) goto done;
                L->isdef[t] = 1; L->tidx[t] = L->temp_defs++;
            }
    }
    /* Un temporal definido en la región no puede usarse fuera de ella:
     * al renombrarlo por copia, los usos externos quedarían apuntando a un
     * ID que ya no existe. Esto garantiza que el renombrado es seguro. */
    for (size_t b = 0; b < n; b++) {
        if (L->ridx[b] != NPOS) continue;
        const IrBlock *blk = &r->blocks[b];
        for (size_t i = 0; i < blk->instruction_count; i++) {
            const IrInstruction *in = &blk->instructions[i];
            IrOperand ops[2] = {in->arg1, in->arg2};
            for (int k = 0; k < 2; k++)
                if (ops[k].kind == IR_OPERAND_TEMP && ops[k].value.temp < tc && L->isdef[ops[k].value.temp]) {
                    *reason = IR_UNROLL_EXTERNAL_TEMP;
                    goto done;
                }
        }
    }
    res = LOOP_OK;
done:
    free(stack); free(indeg); free(queue);
    if (res != LOOP_OK) info_free(L);
    return res;
}

/** @brief true si F copias caben en max_unrolled_blocks.
 * 
 * @details K = F-1 + N%F bloques nuevos por copia; R bloques por copia.
 */

static bool fits_budget(const IrRoutine *r, const LoopInfo *L, size_t f, const IrUnrollOptions *o)
{
    if (!o->max_unrolled_blocks) return true;
    size_t K = f - 1 + (size_t)(L->iterations % f), add;
    if (K > SIZE_MAX / L->region_count) return false;
    add = K * L->region_count;
    return r->block_count <= o->max_unrolled_blocks && add <= o->max_unrolled_blocks - r->block_count;
}

/** @brief Elige F para un loop según el modo y la heurística.
 * 
 * @details FACTOR: opts->factor. AUTO: 1 si N<2, 2 si 2<=N<=8, 4 si N>8;
 * luego acota por max_factor, por N y por presupuesto (baja hasta que quepa).
 * 
 * @return F >= 1; si F < 2 el llamador rechaza el unroll.
 */

static size_t choose_factor(const IrRoutine *r, const LoopInfo *L, const IrUnrollOptions *o, uint64_t n)
{
    if (o->mode == IR_UNROLL_FACTOR) return o->factor;
    size_t f = n < 2 ? 1 : n <= 8 ? 2 : 4;
    if (o->max_factor && f > o->max_factor) f = o->max_factor;
    if (f > n) f = (size_t)n;
    /* AUTO respeta max_unrolled_blocks: si F no cabe, baja hasta que quepa.
     * Si ni F=2 cabe, devuelve 1 y el llamador rechaza el loop. */
    while (f >= 2 && !fits_budget(r, L, f, o)) f--;
    return f;
}

/* ---------- transformación ---------- */

/** @brief Plan de clonación para un loop aceptado. */

typedef struct {
    const LoopInfo *L;      /**< LoopInfo prestado durante el apply. */
    size_t F, K, R, tail;   /**< Factor, clones, tamaño de región y N%F. */
    size_t base_temp;       /**< Primer ID temporal de los clones. */
    IrId first_new;         /**< Primer ID de bloque de los clones. */
} Plan;

/** @brief ID del bloque j-ésimo de la copia k. */

static IrId clone_id(const Plan *p, size_t k, size_t j) { return p->first_new + k * p->R + j; }

/** @brief ID del bloque body de la copia k. */

static IrId entry_of(const Plan *p, size_t k) { return clone_id(p, k, p->L->ridx[p->L->body_pos]); }

/** @brief Renombra o si es un temporal definido en la región.
 * @details Solo TEMP; SYMBOL y CONSTANT se devuelven intactos.
 */

static IrOperand remap(const Plan *p, IrOperand o, size_t k)
{
    if (o.kind == IR_OPERAND_TEMP && p->L->isdef[o.value.temp])
        o.value.temp = p->base_temp + k * p->L->temp_defs + p->L->tidx[o.value.temp];
    return o;
}

/** @brief Re-cablea un destino t desde la copia k.
 * @details header: k==F-2 -> header; k==K-1 -> exit; si no -> entry_of(k+1).
 * exit: mismo exit. Otro: clone_id(k, ridx[t]).
 */

static IrId map_target(const Plan *p, const IrRoutine *r, IrId t, size_t k)
{
    if (t == p->L->header_id) {
        if (k == p->F - 2) return p->L->header_id;      /* fin del grupo */
        if (k == p->K - 1) return p->L->exit_id;        /* fin del epílogo */
        return entry_of(p, k + 1);
    }
    if (t == p->L->exit_id) return t;
    size_t q = find_block(r, t);
    return clone_id(p, k, p->L->ridx[q]);
}

/** @brief a*b con detección de desbordamiento en size_t. */

static bool mul_ok(size_t a, size_t b, size_t *out)
{
    if (a != 0 && b > SIZE_MAX / a) return false;
    *out = a * b;
    return true;
}

/** @brief Aplica el unroll de un loop aceptado.
 * 
 * @details Clona K=F-1+tail copias de la región, renombra temporales,
 * re-cablea saltos, reescribe el LT del header (solo si tail>0) y el BRANCH
 * falso (solo si tail>0), y re-cablea el advance original al clon 0.
 * 
 * @return LOOP_OK si aplicó; LOOP_REJECT por presupuesto/tamaño (sin tocar r);
 *         LOOP_OOM si falla una reserva (r puede quedar parcial).
 */

static LoopResult apply(IrRoutine *r, const LoopInfo *L, size_t F, const IrUnrollOptions *o)
{
    uint64_t N = L->iterations;
    Plan p = {.L = L, .F = F, .R = L->region_count, .tail = (size_t)(N % F)};
    p.K = F - 1 + p.tail;
    size_t new_blocks, new_temps, need;
    if (!mul_ok(p.K, p.R, &new_blocks) || new_blocks > SIZE_MAX - r->block_count) return LOOP_REJECT;
    need = r->block_count + new_blocks;
    if (o->max_unrolled_blocks && need > o->max_unrolled_blocks) return LOOP_REJECT;
    if (!mul_ok(p.K, L->temp_defs, &new_temps) || new_temps > IR_NO_ID - 1 - r->temp_count) return LOOP_REJECT;
    IrId max_id = 0;
    for (size_t i = 0; i < r->block_count; i++) if (r->blocks[i].id >= max_id) max_id = r->blocks[i].id + 1;
    if (new_blocks > IR_NO_ID - 1 - max_id) return LOOP_REJECT;
    p.first_new = max_id;
    p.base_temp = r->temp_count;

    /* Se reserva de una vez el espacio para todos los clones. Si falla,
     * r no se ha tocado (no hay bloques parciales). */
    if (need > r->block_capacity) {
        if (need > SIZE_MAX / sizeof(IrBlock)) return LOOP_REJECT;
        IrBlock *items = realloc(r->blocks, need * sizeof(*items));
        if (!items) return LOOP_OOM;
        r->blocks = items; r->block_capacity = need;
    }

    /* Orden k (copia) externo, j (bloque en la región) interno. Cada clon
     * recibe un ID nuevo (max_id + k*R + j) y una copia profunda de las
     * instrucciones con temporales renombrados y saltos re-cableados. */
    for (size_t k = 0; k < p.K; k++)
        for (size_t j = 0; j < p.R; j++) {
            const IrBlock *src = &r->blocks[L->region[j]];
            IrBlock *dst = &r->blocks[r->block_count];
            *dst = (IrBlock){.id = clone_id(&p, k, j)};
            r->block_count++;
            dst->instructions = calloc(src->instruction_count, sizeof(*dst->instructions));
            if (!dst->instructions) return LOOP_OOM;
            dst->instruction_capacity = src->instruction_count;
            for (size_t i = 0; i < src->instruction_count; i++) {
                IrInstruction in = src->instructions[i];   /* flags y ubicación intactos */
                in.arg1 = remap(&p, in.arg1, k);
                in.arg2 = remap(&p, in.arg2, k);
                in.result = remap(&p, in.result, k);
                if (in.op == IR_OP_JUMP || in.op == IR_OP_BRANCH) {
                    in.extra.targets[0] = map_target(&p, r, in.extra.targets[0], k);
                    if (in.op == IR_OP_BRANCH)
                        in.extra.targets[1] = map_target(&p, r, in.extra.targets[1], k);
                }
                dst->instructions[i] = in;
                dst->instruction_count = i + 1;
            }
        }

    /* Enlaces del original (copia 0) y del header, ya con los clones creados. */
    IrBlock *T = &r->blocks[L->header];
    IrInstruction *lt = &T->instructions[T->instruction_count - 2];
    IrInstruction *br = &T->instructions[T->instruction_count - 1];
    /* Límite del grupo, sin desbordar ni salir del rango de end:
     *  - tail == 0: g*F < N  <=>  begin + g*F*step < end, así que se conserva end.
     *  - tail  > 0: (N-tail)*step <= (N-1)*step < end-begin, luego el límite
     *    begin + (N-tail)*step es estrictamente menor que end y cabe en int64. */
    if (p.tail != 0) {
        uint64_t span = (uint64_t)(N - p.tail) * (uint64_t)L->step;
        lt->arg2.value.constant = (int64_t)((uint64_t)L->begin + span);
    }
    if (p.tail) br->extra.targets[1] = entry_of(&p, F - 1);
    /* El advance original (copia 0) ahora salta al clon 0 del body, no al
     * header. La cadena de copias termina en la última, que vuelve al header
     * (o al exit, si es la última del epílogo). */
    IrBlock *adv = &r->blocks[L->adv];
    adv->instructions[adv->instruction_count - 1].extra.targets[0] = entry_of(&p, 0);
    r->temp_count += new_temps;
    return LOOP_OK;
}

/** @brief Opciones por defecto: AUTO, F=4, max_iterations=4096,
 * max_unrolled_blocks=256, max_factor=4.
 * 
 * @return IrUnrollOptions inicializado.
 */

IrUnrollOptions ir_unroll_default_options(void)
{
    return (IrUnrollOptions){.mode = IR_UNROLL_AUTO, .factor = 4, .max_iterations = 4096,
                             .max_unrolled_blocks = 256, .max_factor = 4};
}

const char *ir_unroll_reason_name(IrUnrollReason reason)
{
    switch (reason) {
    case IR_UNROLL_APPLIED: return "aplicado";
    case IR_UNROLL_NON_CONSTANT: return "inicio, limite o paso no constante";
    case IR_UNROLL_STRUCTURE: return "estructura de control no admitida";
    case IR_UNROLL_EFFECTS: return "llamada, intrinsica o retorno en el cuerpo";
    case IR_UNROLL_INTERNAL_CYCLE: return "ciclo interno en el cuerpo";
    case IR_UNROLL_CONTROL_MODIFIED: return "contador modificado o expuesto por el cuerpo";
    case IR_UNROLL_EXTERNAL_TEMP: return "temporal del cuerpo usado fuera del loop";
    case IR_UNROLL_ITERATION_LIMIT: return "limite de iteraciones excedido";
    case IR_UNROLL_FACTOR_LIMIT: return "factor incompatible con las iteraciones";
    case IR_UNROLL_BLOCK_BUDGET: return "presupuesto de bloques insuficiente";
    case IR_UNROLL_SIZE_LIMIT: return "limite de tamanos o identidades excedido";
    }
    return "motivo desconocido";
}

static void notify_loop(const IrUnrollOptions *options,
                         const IrUnrollLoopReport *report)
{
    if (options->observer != NULL)
        options->observer(report, options->observer_context);
}

/** @brief Escribe un resumen legible de las métricas (para -v).
 * 
 * @param out Stream destino; no se cierra.
 * @param s   Métricas a imprimir.
 */

void ir_unroll_stats_write(FILE *out, const IrUnrollStats *s)
{
    fprintf(out, "[unroll] loops: analizados=%zu desenrollados=%zu rechazados=%zu\n"
                 "[unroll] iteraciones: antes=%zu después=%zu\n"
                 "[unroll] instrucciones: antes=%zu después=%zu\n"
                 "[unroll] bloques: antes=%zu después=%zu\n",
            s->loops_analyzed, s->loops_unrolled, s->loops_skipped,
            s->iterations_before, s->iterations_after,
            s->instructions_before, s->instructions_after, s->blocks_before, s->blocks_after);
}

/** @brief Copia msg en diag (si no es NULL) y devuelve status. */

static IrStatus fail(IrDiagnostic *diag, IrStatus status, const char *msg)
{
    if (diag) {
        *diag = (IrDiagnostic){.status = status, .routine = IR_NO_ID, .block = IR_NO_ID, .instruction = IR_NO_ID};
        snprintf(diag->message, sizeof(diag->message), "%s", msg);
    }
    return status;
}

/** @brief Desenrolla los loops contados del IR.
 * 
 * @param ir    IR validado; se presta y no se modifica.
 * @param opts  Opciones; no puede ser NULL.
 * @param out   Salida; debe apuntar a NULL. En éxito recibe un IrProgram
 *              nuevo y validado, propiedad del llamador.
 * @param diag  Diagnóstico opcional; puede ser NULL.
 * @param stats Métricas opcionales; puede ser NULL.
 * @return IR_OK en éxito; IR_INVALID_INPUT si ir/opts/out son inválidos o
 *         *out no es NULL; IR_OUT_OF_MEMORY si falla una reserva; el estado
 *         propagado si ir_validate falla sobre ir o sobre la copia.
 *         
 * @details En fallo *out queda NULL y se libera lo parcial. La entrada nunca
 * se libera ni se modifica. Se analiza cada bloque original como posible
 * header; los clones agregados por un apply anterior no se reexaminan (el
 * unroll es de un solo nivel). La validación final de la copia es la red de
 * seguridad: si falla, se libera y se propaga el diagnóstico.
 */

IrStatus ir_unroll(const IrProgram *ir, const IrUnrollOptions *opts,
                   IrProgram **out, IrDiagnostic *diag, IrUnrollStats *stats)
{
    /* stats es opcional: si el llamador no lo pide, usamos uno local para no
     * ramificar en cada actualización. Se limpia siempre al entrar. */
    IrUnrollStats local = {0};
    if (!stats) stats = &local;
    *stats = (IrUnrollStats){0};

    /* Precondiciones del contrato: IR, opciones y salida válidos, y *out
     * inicialmente NULL para no sobrescribir un programa previo del llamador. */
    if (!ir || !opts || !out || *out != NULL)
        return fail(diag, IR_INVALID_INPUT, "ir_unroll requiere IR, opciones y salida inicialmente NULL.");
    if ((opts->mode != IR_UNROLL_OFF && opts->mode != IR_UNROLL_AUTO &&
         opts->mode != IR_UNROLL_FACTOR) ||
        (opts->mode == IR_UNROLL_FACTOR && opts->factor < 2))
        return fail(diag, IR_INVALID_INPUT, "Modo o factor de unrolling invalido.");

    /* El IR de entrada debe ser estructuralmente válido. No transformamos
     * IR inválido: si falla, propagamos el diagnóstico tal cual. */
    IrDiagnostic vd;
    if (ir_validate(ir, &vd) != IR_OK) {
        if (diag) *diag = vd;
        return vd.status;
    }

    /* Copia profunda: el original queda intacto y la copia es la que se
     * modifica. Si falla la reserva, no hay nada que liberar todavía. */
    IrProgram *copy = copy_program(ir);
    if (!copy) return fail(diag, IR_OUT_OF_MEMORY, "No se pudo copiar el IR para desenrollar.");

    /* Métricas del estado inicial: se miden sobre el original, no sobre la
     * copia, para que "before" refleje exactamente lo que recibió el llamador. */
    stats->instructions_before = count_instructions(ir);
    stats->blocks_before = count_blocks(ir);

    /* En modo OFF no se transforma nada: la copia se devuelve tal cual. El
     * bloque se salta por completo, incluido el recorrido de loops. */
    if (opts->mode != IR_UNROLL_OFF) {
        for (size_t ri = 0; ri < copy->routine_count; ri++) {
            IrRoutine *r = &copy->routines[ri];

            /* Solo se analizan los bloques originales de la rutina. Los clones
             * que agregue un apply anterior NO se reexaminan: el unroll es de
             * un solo nivel. Si no, un loop desenrollado podría desenrollarse
             * otra vez, explotando el tamaño sin control. */
            size_t original = r->block_count;

            for (size_t h = 0; h < original; h++) {
                /* analyze decide si el bloque h es header de un loop contado
                 * transformable. Si lo es, llena L con toda la información
                 * necesaria (región, control, begin, step, N, temporales). */
                LoopInfo L;
                IrUnrollLoopReport report = {
                    .routine = ri, .block = r->blocks[h].id,
                    .location = last_of(&r->blocks[h])->location
                };
                LoopResult res = analyze(r, h, opts, &L, &report.reason);

                /* Sin forma de header: no es un loop, seguir con el siguiente. */
                if (res == LOOP_NONE) continue;

                /* Falla de reserva durante el análisis: no se puede continuar
                 * porque copy puede quedar parcial. Se libera y se aborta. */
                if (res == LOOP_OOM) {
                    ir_program_free(copy);
                    return fail(diag, IR_OUT_OF_MEMORY, "Sin memoria al analizar loops.");
                }

                /* Es candidato (tenía forma de header), independientemente de
                 * si luego se acepta o se rechaza. */
                stats->loops_analyzed++;

                /* Tiene forma pero no es transformable: N variable, CALL/
                 * INTRINSIC/RETURN en la región, ciclos internos, escritura
                 * del control, etc. Se cuenta como rechazado y se sigue. */
                if (res == LOOP_REJECT) {
                    stats->loops_skipped++;
                    notify_loop(opts, &report);
                    continue;
                }

                /* Aceptado por analyze. Elegir F según modo y presupuesto.
                 * AUTO baja F hasta que quepa; FACTOR usa opts->factor. */
                size_t F = choose_factor(r, &L, opts, L.iterations);
                report.factor = F;
                report.iterations = L.iterations;

                /* F < 2: no se puede desenrollar (N < 2 o presupuesto
                 * insuficiente). F > N: no tiene sentido desenrollar más
                 * iteraciones de las que se ejecutan. En ambos casos se
                 * rechaza el loop y se libera L. */
                if (F < 2 || F > L.iterations) {
                    stats->loops_skipped++;
                    report.reason = opts->mode == IR_UNROLL_AUTO && L.iterations >= 2
                        ? IR_UNROLL_BLOCK_BUDGET : IR_UNROLL_FACTOR_LIMIT;
                    if (opts->mode == IR_UNROLL_AUTO && opts->max_factor == 1)
                        report.reason = IR_UNROLL_FACTOR_LIMIT;
                    notify_loop(opts, &report);
                    info_free(&L);
                    continue;
                }
                if (!fits_budget(r, &L, F, opts)) {
                    stats->loops_skipped++;
                    report.reason = IR_UNROLL_BLOCK_BUDGET;
                    notify_loop(opts, &report);
                    info_free(&L);
                    continue;
                }

                /* Guardar N antes de apply, porque info_free limpia L y
                 * necesitamos N para las métricas después. */
                uint64_t n = L.iterations;

                /* apply clona K = F-1 + N%F copias, renombra temporales,
                 * re-cablea saltos, reescribe el LT del header (si tail > 0),
                 * el BRANCH falso al epílogo (si tail > 0) y el advance
                 * original al clon 0. No toca el IR original: solo copy. */
                res = apply(r, &L, F, opts);

                /* L ya no se necesita: apply copió lo que necesitaba. */
                info_free(&L);

                /* Falla de reserva durante la clonación: copy puede quedar
                 * parcial. Se libera y se aborta. */
                if (res == LOOP_OOM) {
                    ir_program_free(copy);
                    return fail(diag, IR_OUT_OF_MEMORY, "Sin memoria al desenrollar.");
                }

                /* apply rechazó por presupuesto/tamaño. No tocó r, así que
                 * copy sigue válido. Se cuenta como rechazado y se sigue. */
                if (res == LOOP_REJECT) {
                    stats->loops_skipped++;
                    report.reason = IR_UNROLL_SIZE_LIMIT;
                    notify_loop(opts, &report);
                    continue;
                }

                /* apply tuvo éxito: el loop se desenrolló. Acumular métricas.
                 * iterations_before suma N (las iteraciones del loop original);
                 * iterations_after suma N/F + N%F (vueltas del header + copias
                 * del epílogo). La diferencia mide el ahorro de vueltas. */
                stats->loops_unrolled++;
                stats->iterations_before += (size_t)n;
                stats->iterations_after += (size_t)(n / F + n % F);
                report.reason = IR_UNROLL_APPLIED;
                notify_loop(opts, &report);
            }
        }
    }

    /* Métricas del estado final: se miden sobre la copia transformada, que es
     * lo que el llamador recibirá si todo sale bien. */
    stats->instructions_after = count_instructions(copy);
    stats->blocks_after = count_blocks(copy);

    /* La transformación puede introducir errores estructurales (IDs duplicados,
     * destinos inexistentes, temporales sin definición). ir_validate es la red
     * de seguridad: si falla, se libera la copia y se propaga el diagnóstico.
     * Nunca se publica un IR inválido. */
    IrDiagnostic od;
    if (ir_validate(copy, &od) != IR_OK) {
        IrStatus st = od.status;
        ir_program_free(copy);
        if (diag) *diag = od;
        return st;
    }

    /* Éxito: transferir la propiedad de la copia al llamador. El original
     * sigue siendo del llamador y no se ha tocado. */
    *out = copy;
    return fail(diag, IR_OK, "");
}
