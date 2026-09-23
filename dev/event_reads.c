/* ct_dev --event-reads (dev/event_reads.h). */
#include "event_reads.h"

#include <stdlib.h>
#include <string.h>

enum {
    DISPATCH_TABLE = 0x5D6E,    /* JSR ($5D6E,X) in bank $C0: a command handler */
    SCRATCH_FIRST = 0x01C1,     /* the field page's event cells, $C1-$E4 */
    SCRATCH_LAST = 0x01E4,
    STACK_SIZE = 0x2000,        /* the stack is in low RAM */
    MAX_FRAMES = 256,
    MAX_IRQ = 8,
};

typedef enum { KIND_Y, KIND_V, KIND_SCRATCH, KIND_Y_STACK, KIND_V_STACK, KIND_Y_STORED } Kind;
static const char *const kind_names[] = { "Y", "V", "cell", "Y-pushed", "V-pushed", "Y-stored" };

/* Where a read is, relative to the command whose effect it reads. */
/* In the command that wrote a cell: the routine that wrote it (same), one it
 * called (down: a parameter), or one that called it (up: a result). */
typedef enum { REL_SAME, REL_DOWN, REL_UP, REL_ACROSS, REL_LATER, REL_OUTSIDE, REL_IRQ, REL_SET } Rel;
static const char *const rel_names[] = { "same-routine", "passed-down", "passed-up", "passed-across", "later-command", "outside",
                                         "interrupt", "set-at" };

/* A value a command left: which command, from which dispatch, which run.
 * `valid` is 0 for none; on the stack, PUSHED_V marks a pushed P. */
enum { PUSHED_V = 2, STORED_Y = 3 };
typedef struct {
    uint8_t valid, cmd;
    uint16_t site;
    uint32_t instance;
    int depth;          /* a cell's: the call depth of the routine that wrote it */
    uint32_t routine;   /* and its entry */
} Taint;

typedef struct {
    uint16_t s0;       /* S before the call */
    uint32_t target;
    bool dispatch;
    Taint cmd;         /* for a dispatch: the command run */
} Frame;

typedef struct {
    uint8_t kind, rel, cmd;
    uint32_t cell;
    uint16_t site;
    uint32_t pc, extra, writer;
} Key;

typedef struct {
    Key key;
    uint64_t count;
    bool used;
} Slot;

struct EventReads {
    Snes *s;
    uint32_t pc;
    uint8_t op;
    bool in_irq;
    uint16_t irq_s[MAX_IRQ];
    int irqs;
    uint32_t nmi_at, irq_at;
    Frame frames[MAX_FRAMES];
    int depth;
    uint32_t instances;
    Taint y_lo, y_hi, v;
    Taint mem[SNES_WRAM_SIZE];       /* a pushed Y byte or P's V, or a stored Y byte */
    unsigned stores;                 /* bytes the instruction has stored */
    Taint cell[SCRATCH_LAST - SCRATCH_FIRST + 1];
    Slot *slots;
    size_t cap, used;
    bool overflowed;
};

static void count(EventReads *r, Kind kind, Rel rel, const Taint *t, unsigned cell, uint32_t pc, uint32_t extra)
{
    Key k = { (uint8_t)kind, (uint8_t)rel, t->cmd, cell, t->site, pc, extra, t->routine };
    uint64_t h = ((uint64_t)kind << 56 ^ (uint64_t)rel << 52 ^ (uint64_t)t->cmd << 44 ^ (uint64_t)cell << 40 ^
                  (uint64_t)t->site << 24 ^ pc) * 0x9E3779B97F4A7C15ull ^ (extra ^ (uint64_t)t->routine << 24) * 0xC2B2AE3D27D4EB4Full;
    size_t mask = r->cap - 1;
    for (size_t i = (size_t)(h >> 40) & mask;; i = (i + 1) & mask) {
        Slot *sl = &r->slots[i];
        if (sl->used && !memcmp(&sl->key, &k, sizeof k)) {
            sl->count++;
            return;
        }
        if (!sl->used) {
            if (r->used * 2 >= r->cap) {
                r->overflowed = true;
                return;
            }
            sl->used = true;
            sl->key = k;
            sl->count = 1;
            r->used++;
            return;
        }
    }
}

static const Frame *command_frame(const EventReads *r)
{
    for (int i = r->depth - 1; i >= 0; i--)
        if (r->frames[i].dispatch)
            return &r->frames[i];
    return NULL;
}

static uint32_t innermost(const EventReads *r) { return r->depth ? r->frames[r->depth - 1].target : 0; }

/* A read of Y or V, outside the command that left it or in another. */
static void read_left(EventReads *r, Kind kind, const Taint *t)
{
    if (!t->valid)
        return;
    const Frame *f = command_frame(r);
    if (f)
        count(r, kind, REL_LATER, t, 0, r->pc, f->cmd.cmd);
    else
        count(r, kind, REL_OUTSIDE, t, 0, r->pc, innermost(r));
}

static void observe(void *ctx, const SnesBus *bus, uint32_t index, uint8_t value, unsigned flags)
{
    (void)value;
    EventReads *r = ctx;
    if (bus != &r->s->bus || (flags & SNES_IO_DMA))
        return;
    bool write = flags & SNES_IO_WRITE;
    if (index >= SCRATCH_FIRST && index <= SCRATCH_LAST) {
        Taint *c = &r->cell[index - SCRATCH_FIRST];
        const Frame *f = r->in_irq ? NULL : command_frame(r);
        if (write) {
            if (f) {
                *c = f->cmd;
                c->depth = r->depth;
                c->routine = innermost(r);
                count(r, KIND_SCRATCH, REL_SET, c, index, r->pc, c->routine);
            } else if (!r->in_irq) {
                memset(c, 0, sizeof *c);
            }
        } else if (c->valid) {
            Rel rel;
            uint32_t extra = 0;
            if (r->in_irq)
                rel = REL_IRQ;
            else if (!f)
                rel = REL_OUTSIDE, extra = innermost(r);
            else if (f->cmd.instance == c->instance)
                rel = r->depth == c->depth ? (innermost(r) == c->routine ? REL_SAME : REL_ACROSS)
                    : r->depth > c->depth ? REL_DOWN : REL_UP,
                extra = innermost(r);
            else
                rel = REL_LATER, extra = f->cmd.cmd;
            count(r, KIND_SCRATCH, rel, c, index, r->pc, extra);
        }
        return;
    }
    if (!r->in_irq) {
        Taint *t = &r->mem[index];
        bool pull = r->op == 0x7A || r->op == 0x28;   /* PLY, PLP: followed as registers */
        bool push = r->op == 0x5A || r->op == 0x08;   /* PHY, PHP: set in note_instruction */
        bool store = r->op == 0x84 || r->op == 0x8C || r->op == 0x94;   /* STY: Y's bytes, low first */
        if (write && store) {
            *t = r->stores++ ? r->y_hi : r->y_lo;
            if (t->valid)
                t->valid = STORED_Y;
        } else if (write) {
            if (!push)
                memset(t, 0, sizeof *t);
        } else if (t->valid && !pull) {
            Kind k = t->valid == PUSHED_V ? KIND_V_STACK : t->valid == STORED_Y ? KIND_Y_STORED : KIND_Y_STACK;
            count(r, k, REL_OUTSIDE, t, index, r->pc, r->op);
        }
    }
}

EventReads *event_reads_create(Snes *s)
{
    EventReads *r = calloc(1, sizeof *r);
    r->s = s;
    r->cap = 1u << 16;
    r->slots = calloc(r->cap, sizeof *r->slots);
    r->nmi_at = snes_bus_peek(&s->bus, 0x00FFEA) | snes_bus_peek(&s->bus, 0x00FFEB) << 8;
    r->irq_at = snes_bus_peek(&s->bus, 0x00FFEE) | snes_bus_peek(&s->bus, 0x00FFEF) << 8;
    snes_bus_wram_observer = observe;
    snes_bus_wram_observer_ctx = r;
    return r;
}

static bool in(uint8_t op, const uint8_t *set, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (set[i] == op)
            return true;
    return false;
}

static const uint8_t reads_y[] = {
    0x84, 0x8C, 0x94, 0xC0, 0xC4, 0xCC, 0xC8, 0x88, 0x98, 0xBB, 0x54, 0x44, 0xB6, 0x96, 0xBE,
    0x11, 0x31, 0x51, 0x71, 0x91, 0xB1, 0xD1, 0xF1, 0x17, 0x37, 0x57, 0x77, 0x97, 0xB7, 0xD7, 0xF7,
    0x19, 0x39, 0x59, 0x79, 0x99, 0xB9, 0xD9, 0xF9, 0x13, 0x33, 0x53, 0x73, 0x93, 0xB3, 0xD3, 0xF3,
};
static const uint8_t writes_y[] = { 0xA0, 0xA4, 0xAC, 0xB4, 0xBC, 0xA8, 0x9B, 0xC8, 0x88, 0x54, 0x44 };
static const uint8_t writes_v[] = {
    0x61, 0x63, 0x65, 0x67, 0x69, 0x6D, 0x6F, 0x71, 0x72, 0x73, 0x75, 0x77, 0x79, 0x7D, 0x7F,
    0xE1, 0xE3, 0xE5, 0xE7, 0xE9, 0xED, 0xEF, 0xF1, 0xF2, 0xF3, 0xF5, 0xF7, 0xF9, 0xFD, 0xFF,
    0x24, 0x2C, 0x34, 0x3C, 0xB8,
};

void event_reads_note_instruction(EventReads *r, uint32_t pc)
{
    const Cpu *c = &r->s->cpu;
    const SnesBus *bus = &r->s->bus;
    r->pc = pc;
    r->op = snes_bus_peek(bus, pc);

    /* interrupts: entered at a vector's target, left when S comes back up */
    while (r->irqs && c->s >= r->irq_s[r->irqs - 1])
        r->irqs--;
    if ((pc >> 16) == 0 && ((pc & 0xFFFF) == r->nmi_at || (pc & 0xFFFF) == r->irq_at) && r->irqs < MAX_IRQ)
        r->irq_s[r->irqs++] = (uint16_t)(c->s + 4);
    r->in_irq = r->irqs != 0;
    if (r->in_irq)
        return;

    /* returns: a frame is left when S is back where it was before the call */
    while (r->depth && c->s >= r->frames[r->depth - 1].s0) {
        const Frame *f = &r->frames[--r->depth];
        if (f->dispatch) {
            r->y_lo = r->y_hi = r->v = f->cmd;
            if ((pc & 0xFFFF) != (uint16_t)(f->cmd.site + 3))
                count(r, KIND_Y, REL_SET, &f->cmd, 0, pc, 0);
        }
    }

    uint8_t op = r->op;
    bool x8 = c->e || (c->p & CPU_FLAG_X);
    uint8_t operand = snes_bus_peek(bus, pc + 1);

    if (in(op, reads_y, sizeof reads_y)) {
        read_left(r, KIND_Y, &r->y_lo);
        if (!x8)
            read_left(r, KIND_Y, &r->y_hi);
    }
    if (in(op, writes_y, sizeof writes_y))
        memset(&r->y_lo, 0, sizeof r->y_lo), memset(&r->y_hi, 0, sizeof r->y_hi);
    if (op == 0xE2 && (operand & CPU_FLAG_X))   /* 8-bit index registers clear Y's high byte */
        memset(&r->y_hi, 0, sizeof r->y_hi);
    r->stores = 0;
    if (op == 0x5A) {   /* PHY */
        if (x8) {
            r->mem[c->s % STACK_SIZE] = r->y_lo;
        } else {
            r->mem[c->s % STACK_SIZE] = r->y_hi;
            r->mem[(uint16_t)(c->s - 1) % STACK_SIZE] = r->y_lo;
        }
    } else if (op == 0x7A) {   /* PLY */
        r->y_lo = r->mem[(uint16_t)(c->s + 1) % STACK_SIZE];
        if (x8)
            memset(&r->y_hi, 0, sizeof r->y_hi);
        else
            r->y_hi = r->mem[(uint16_t)(c->s + 2) % STACK_SIZE];
        r->y_lo.valid = r->y_lo.valid == 1;
        r->y_hi.valid = r->y_hi.valid == 1;
    }

    if (op == 0x50 || op == 0x70)
        read_left(r, KIND_V, &r->v);
    if (in(op, writes_v, sizeof writes_v) || ((op == 0xE2 || op == 0xC2) && (operand & CPU_FLAG_V)))
        memset(&r->v, 0, sizeof r->v);
    if (op == 0x08) {   /* PHP */
        r->mem[c->s % STACK_SIZE] = r->v;
        if (r->v.valid)
            r->mem[c->s % STACK_SIZE].valid = PUSHED_V;
    } else if (op == 0x28) {   /* PLP */
        r->v = r->mem[(uint16_t)(c->s + 1) % STACK_SIZE];
        r->v.valid = r->v.valid == PUSHED_V;
    }

    /* calls */
    if ((op == 0x20 || op == 0x22 || op == 0xFC) && r->depth < MAX_FRAMES) {
        Frame *f = &r->frames[r->depth++];
        uint16_t word = (uint16_t)(operand | snes_bus_peek(bus, pc + 2) << 8);
        uint32_t bank = pc & 0xFF0000;
        memset(f, 0, sizeof *f);
        f->s0 = c->s;
        if (op == 0x20) {
            f->target = bank | word;
        } else if (op == 0x22) {
            f->target = word | (uint32_t)snes_bus_peek(bus, pc + 3) << 16;
        } else {
            uint16_t ptr = (uint16_t)(word + c->x);
            f->target = bank | snes_bus_peek(bus, bank | ptr) | snes_bus_peek(bus, bank | (uint16_t)(ptr + 1)) << 8;
            if (bank == 0xC00000 && word == DISPATCH_TABLE) {
                f->dispatch = true;
                f->cmd = (Taint){ 1, (uint8_t)(c->x >> 1), (uint16_t)pc, ++r->instances, 0, 0 };
            }
        }
    }
}

static int by_key(const void *a, const void *b)
{
    const Slot *x = a, *y = b;
    if (x->key.kind != y->key.kind)
        return x->key.kind - y->key.kind;
    if (x->key.cmd != y->key.cmd)
        return x->key.cmd - y->key.cmd;
    if (x->key.cell != y->key.cell)
        return x->key.cell - y->key.cell;
    if (x->key.rel != y->key.rel)
        return x->key.rel - y->key.rel;
    return x->key.pc < y->key.pc ? -1 : x->key.pc > y->key.pc;
}

int event_reads_write(EventReads *r, const char *path, FILE *summary)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    size_t n = 0;
    for (size_t i = 0; i < r->cap; i++)
        if (r->slots[i].used)
            r->slots[n++] = r->slots[i];
    qsort(r->slots, n, sizeof *r->slots, by_key);
    fprintf(f, "# kind command dispatch wram-byte relation reader-pc extra writer count\n"
               "# extra: the routine the read is in, or for later-command the command reading; for a\n"
               "# pushed copy, the opcode reading it. writer: the routine that wrote a cell.\n");
    for (size_t i = 0; i < n; i++) {
        const Key *k = &r->slots[i].key;
        fprintf(f, "%s $%02X $C0:%04X $%05X %s $%02X:%04X $%06X $%06X %llu\n", kind_names[k->kind], k->cmd,
                k->site, (unsigned)k->cell, rel_names[k->rel], (unsigned)(k->pc >> 16), (unsigned)(k->pc & 0xFFFF), (unsigned)k->extra,
                (unsigned)k->writer, (unsigned long long)r->slots[i].count);
    }
    if (fclose(f) != 0)
        return -1;
    fprintf(summary, "event reads: %u commands run, %zu distinct reads%s\n", (unsigned)r->instances, n,
            r->overflowed ? " (table full: some dropped)" : "");
    return 0;
}

void event_reads_destroy(EventReads *r)
{
    if (!r)
        return;
    if (snes_bus_wram_observer_ctx == r) {
        snes_bus_wram_observer = NULL;
        snes_bus_wram_observer_ctx = NULL;
    }
    free(r->slots);
    free(r);
}
