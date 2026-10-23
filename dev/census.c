#include "census.h"

#include <stdlib.h>
#include <string.h>

enum { VECTOR, GENERATED, INTERP, CALL, STOP, YIELD, RETURN, WAIT, KINDS, NONE = KINDS };
static const char *const kind_names[KINDS] = { "vector", "generated", "interp", "call", "stop", "yield", "return", "wait" };

/* The cartridge's vectors the machine enters the game through: native mode's
 * NMI and IRQ, and emulation mode's reset (power-on). */
static const uint32_t vector_cells[] = { 0x00FFEA, 0x00FFEE, 0x00FFFC };
enum { VECTORS = sizeof vector_cells / sizeof vector_cells[0] };

/* Counted by (entry, kind, caller): the machine's entries. kind NONE: the
 * entry's work run as C by the port `by`. */
typedef struct Slot {
    const NativeEntry *entry;
    const NativeEntry *by;   /* the caller, where it is a port */
    uint32_t caller;         /* the caller, where it is an address */
    uint8_t kind;
    uint64_t n;
    bool used;
} Slot;

struct Census {
    Snes *s;
    uint32_t vectors[VECTORS];
    /* What happened since the latest entry ran: its kind, the port that made
     * the hand-off (or ran latest), and the latest original instruction's
     * address. */
    uint8_t last;
    const NativeEntry *last_port;
    uint32_t last_pc;
    Slot *slots;
    size_t cap, used;
};

static Census *current;
static void (*next_insn_hook)(Snes *s, uint32_t pc24);

static size_t slot_hash(const NativeEntry *e, const NativeEntry *by, uint32_t caller, uint8_t kind)
{
    uintptr_t h = (uintptr_t)e * 31u ^ (uintptr_t)by * 17u ^ caller * 2654435761u ^ kind;
    return (size_t)(h ^ h >> 15);
}

static bool grow(Census *c)
{
    size_t cap = c->cap * 2;
    Slot *slots = calloc(cap, sizeof *slots);
    if (!slots)
        return false;
    for (size_t i = 0; i < c->cap; i++) {
        if (!c->slots[i].used)
            continue;
        const Slot *o = &c->slots[i];
        size_t j = slot_hash(o->entry, o->by, o->caller, o->kind) & (cap - 1);
        while (slots[j].used)
            j = (j + 1) & (cap - 1);
        slots[j] = *o;
    }
    free(c->slots);
    c->slots = slots;
    c->cap = cap;
    return true;
}

static void count(Census *c, const NativeEntry *e, const NativeEntry *by, uint32_t caller, uint8_t kind)
{
    if (c->used * 2 >= c->cap && !grow(c))
        return;
    size_t i = slot_hash(e, by, caller, kind) & (c->cap - 1);
    while (c->slots[i].used) {
        Slot *s = &c->slots[i];
        if (s->entry == e && s->by == by && s->caller == caller && s->kind == kind) {
            s->n++;
            return;
        }
        i = (i + 1) & (c->cap - 1);
    }
    c->slots[i] = (Slot){ .entry = e, .by = by, .caller = caller, .kind = kind, .n = 1, .used = true };
    c->used++;
}

static bool is_vector(const Census *c, uint32_t addr)
{
    for (int i = 0; i < VECTORS; i++)
        if (c->vectors[i] == addr)
            return true;
    return false;
}

static void on_entered(void *ctx, Snes *s, const NativeEntry *e)
{
    Census *c = ctx;
    (void)s;
    uint8_t kind = c->last;
    if (kind != CALL && kind != STOP && kind != YIELD && is_vector(c, e->addr))
        kind = VECTOR;
    if (kind == NONE)
        kind = VECTOR;   /* nothing ran before it: the power-on */
    if (e->idle)
        kind = WAIT;     /* a pass of a wait loop the machine still makes */
    bool by_port = kind == CALL || kind == STOP || kind == YIELD || kind == RETURN || kind == WAIT;
    count(c, e, by_port ? c->last_port : NULL, by_port ? 0 : kind == VECTOR ? e->addr : c->last_pc, kind);
    /* until something else happens, what runs next is where this one went back to */
    c->last = RETURN;
    c->last_port = e;
}

static void on_as_c(void *ctx, const NativeEntry *by, const NativeEntry *routine)
{
    count(ctx, routine, by, 0, NONE);
}

static void on_called_out(void *ctx, Snes *s, const NativeEntry *e, uint32_t target)
{
    Census *c = ctx;
    (void)s, (void)target;
    c->last = CALL;
    c->last_port = e;
}

static void on_stopped(void *ctx, Snes *s, const NativeEntry *e, uint16_t entry_s)
{
    Census *c = ctx;
    (void)s, (void)entry_s;
    c->last = STOP;
    c->last_port = e;
}

static void on_parked(void *ctx, Snes *s, const NativeEntry *e)
{
    Census *c = ctx;
    (void)s;
    c->last = YIELD;
    c->last_port = e;
}

static void on_returned_from_interrupt(void *ctx, Snes *s, const NativeEntry *e)
{
    Census *c = ctx;
    (void)s;
    c->last = RETURN;
    c->last_port = e;
}

void census_note(Census *c, uint32_t pc)
{
    c->last = INTERP;
    c->last_pc = pc;
}

static void on_generated(Snes *s, uint32_t pc24)
{
    if (current && s == current->s) {
        current->last = GENERATED;
        current->last_pc = pc24;
    }
    if (next_insn_hook)
        next_insn_hook(s, pc24);
}

Census *census_create(Snes *s, NativeRuntime *rt)
{
    Census *c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    c->cap = 1u << 12;
    if (!(c->slots = calloc(c->cap, sizeof *c->slots))) {
        free(c);
        return NULL;
    }
    c->s = s;
    c->last = NONE;
    for (int i = 0; i < VECTORS; i++)
        c->vectors[i] = (uint32_t)(snes_bus_peek(&s->bus, vector_cells[i]) | snes_bus_peek(&s->bus, vector_cells[i] + 1) << 8);
    native_census_hooks = (NativeCensusHooks){ .ctx = c, .entered = on_entered, .as_c = on_as_c };
    native_set_observer(rt, &(NativeObserver){ .ctx = c, .called_out = on_called_out, .stopped = on_stopped,
                                               .parked = on_parked,
                                               .returned_from_interrupt = on_returned_from_interrupt });
    current = c;
    next_insn_hook = generated_insn_hook;
    generated_insn_hook = on_generated;
    return c;
}

static int by_entry(const void *a, const void *b)
{
    const Slot *x = a, *y = b;
    if (x->entry->addr != y->entry->addr)
        return x->entry->addr < y->entry->addr ? -1 : 1;
    int names = strcmp(x->entry->name, y->entry->name);
    if (names)
        return names;
    if (x->kind != y->kind)
        return x->kind < y->kind ? -1 : 1;
    return x->n > y->n ? -1 : x->n < y->n;
}

/* The format, a line per (entry, how, caller):
 *   machine <entry addr> <entry name> <kind> <caller addr> <caller name or -> <count>
 *   c       <entry addr> <entry name> - <caller addr> <caller name> <count>
 * addresses as six hex digits. */
int census_write(const Census *c, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    Slot *rows = malloc((c->used ? c->used : 1) * sizeof *rows);
    if (!rows) {
        fclose(f);
        return -1;
    }
    size_t n = 0;
    for (size_t i = 0; i < c->cap; i++)
        if (c->slots[i].used)
            rows[n++] = c->slots[i];
    qsort(rows, n, sizeof *rows, by_entry);
    fprintf(f, "# census: how each registered entry was entered (dev/census.h)\n");
    for (size_t i = 0; i < n; i++) {
        const Slot *r = &rows[i];
        uint32_t caller = r->by ? r->by->addr : r->caller;
        fprintf(f, "%s %06X %s %s %06X %s %llu\n", r->kind == NONE ? "c" : "machine", (unsigned)r->entry->addr,
                r->entry->name, r->kind == NONE ? "-" : kind_names[r->kind], (unsigned)caller,
                r->by ? r->by->name : "-", (unsigned long long)r->n);
    }
    free(rows);
    return fclose(f) == 0 ? 0 : -1;
}

void census_summary(const Census *c, FILE *out)
{
    uint64_t by_kind[KINDS + 1] = { 0 };
    for (size_t i = 0; i < c->cap; i++)
        if (c->slots[i].used)
            by_kind[c->slots[i].kind] += c->slots[i].n;
    uint64_t machine = 0;
    for (int k = 0; k < KINDS; k++)
        machine += by_kind[k];
    fprintf(out, "census: %llu entries by the machine (", (unsigned long long)machine);
    for (int k = 0; k < KINDS; k++)
        fprintf(out, "%s%s %llu", k ? ", " : "", kind_names[k], (unsigned long long)by_kind[k]);
    fprintf(out, "), %llu runs as C\n", (unsigned long long)by_kind[NONE]);
}

void census_destroy(Census *c)
{
    if (!c)
        return;
    if (current == c) {
        current = NULL;
        if (generated_insn_hook == on_generated)
            generated_insn_hook = next_insn_hook;
        native_census_hooks = (NativeCensusHooks){ 0 };
    }
    free(c->slots);
    free(c);
}
