#include "fallbacks.h"

#include <stdlib.h>
#include <string.h>

#include "misses.h"

/* COVERED (SHADOW mode): the original running what a port stands for; not counted. */
enum { REFUSED, HANDBACK, SKIPPED, UNPORTED, KINDS, COVERED = KINDS };
static const char *const kind_names[KINDS] = { "refused", "handback", "skipped", "unported" };

/* A routine filed by its bank alone (no call on record). */
#define BANK_ONLY 0x1000000u
#define NO_PORT 0xFFFFFFFFu
/* An extent whose entry S is further above S than this is on another stack. */
#define STACK_SPAN 0x300
#define MAX_FRAMES 512
#define STATES_KEPT 4
/* The instructions of one pass of a wait loop kept to be taken back (SHADOW). */
#define PASS_MAX 64
/* Passes an interrupt cut, kept aside while their handlers run (SHADOW). */
#define PASSES_ASIDE 8
/* The most an instruction pulls (RTI: P, PC and the bank). */
#define MOST_A_STEP_PULLS 4

/* An extent: why the code in it runs, the routine it is filed under, the port
 * it is charged to (entry address, or NO_PORT), the S it ends above, the S it
 * began at (a hand port entered there or above ends it: the original's code
 * went on to a port without returning, a main loop's next pass), and whether
 * an interrupt began it (an RTI ends it, wherever the stack is then). A held
 * extent is an interrupt entry's comparison (NativeEntry.interrupt), whose
 * handler may move the stack: neither S nor a port entered ends it, only the
 * comparison's end or an RTI. */
typedef struct Frame {
    uint8_t kind;
    bool interrupt, held;
    uint32_t routine, port;
    uint16_t floor, start;
    bool call_out;     /* HANDBACK: a port's call-out, the callee it waits for */
    bool away;         /* ...moved onto a stack of its own (NATIVE_STACK_SWITCH_DISTANCE), until S is back */
    uint64_t hidden;   /* COVERED, a comparison's own (named): what it and its callees have not counted */
    const char *name;  /* COVERED: its port's name where the frame is the comparison's own; NULL for a callee's */
} Frame;

/* Counted by (a, b, c, d): instructions by (pc, kind, routine, port), and
 * events by (port, EV_*, where, 0). */
typedef struct Count {
    uint32_t a, b, c, d;
    uint64_t n;
    uint8_t len;
    bool used;
} Count;

enum { EV_CALLOUT = 0x100, EV_HANDBACK, EV_SKIP };

typedef struct Refusal {
    uint32_t addr;
    const char *name;
    const char *why;
    uint64_t count;
    int nstates;
    char states[STATES_KEPT][256];
} Refusal;

typedef struct Pass {
    bool open;
    uint32_t head;
    uint16_t s;
    int n;
    uint32_t keys[PASS_MAX][4];
    uint64_t hidden;   /* of it, run under a comparison (a port waiting through the loop) */
} Pass;

struct Fallbacks {
    Snes *s;
    const NativeRuntime *rt;    /* the runtime observed */
    Frame frames[MAX_FRAMES];
    int depth;                  /* frames[0] is the base */
    Frame eff;                  /* what the latest instruction was counted as */
    bool pending;               /* the latest instruction was a call... */
    uint16_t pending_s;         /* ...and its callee starts with this S */
    bool pending_rti;           /* the latest instruction was an RTI */
    uint16_t last_s;            /* S at the latest instruction */
    bool after_a_call;          /* the latest instruction was a call, whose callee starts with S... */
    uint16_t callee_s;          /* ...this (unlike `pending`, the hand-offs leave it alone) */
    bool shadow;                /* the runtime observed is in SHADOW mode */
    /* SHADOW: the pass of a wait loop an idle entry stands for, as the
     * original runs it: counted as it runs, and taken back when the loop comes
     * round to its head again (the idle port would have run it); the pass
     * that leaves the loop stays counted. A pass an interrupt cuts is kept
     * aside while the handler runs, which has passes of its own, and goes on
     * where the handler returns to it. */
    Pass pass;
    struct {
        uint32_t serial;   /* the order it was kept aside in; 0 for a free place */
        Pass pass;
    } aside[PASSES_ASIDE];
    uint32_t serials;
    /* The interrupt taken latest, while it hasn't returned: the S it was
     * taken at and the extents under it, the interrupted code's. A handler
     * that moves to a stack of its own (the menus' vblank handler) leaves
     * them alone: S far above where it was taken is the handler's, not the
     * interrupted code's having returned (handler_away). */
    struct {
        bool on;
        uint16_t s;
        int depth;
    } interrupt;
    Count *slots;
    size_t cap, used;
    struct { uint32_t addr; const char *name; } *ports;   /* the events' ports' names */
    int nports, cap_ports;
    Refusal *refusals;
    int nrefusals, cap_refusals;
    uint64_t by_kind[KINDS];
};

static Fallbacks *current;
static void (*next_insn_hook)(Snes *s, uint32_t pc24);   /* whatever held the hook before */

static void on_generated(Snes *s, uint32_t pc24)
{
    /* not shadow mode's replays of generated blocks, which run on a machine of their own */
    if (current && s == current->s)
        fallbacks_note(current, pc24, s->cpu.p, s->cpu.e);
    if (next_insn_hook)
        next_insn_hook(s, pc24);
}

Fallbacks *fallbacks_create(Snes *s)
{
    Fallbacks *f = calloc(1, sizeof *f);
    if (!f)
        return NULL;
    f->s = s;
    f->cap = 1u << 16;
    f->slots = calloc(f->cap, sizeof *f->slots);
    if (!f->slots) {
        free(f);
        return NULL;
    }
    fallbacks_reset(f);
    current = f;
    if (generated_insn_hook != on_generated) {
        next_insn_hook = generated_insn_hook;
        generated_insn_hook = on_generated;
    }
    s->interpreter = fallbacks_interpreter;
    return f;
}

void fallbacks_destroy(Fallbacks *f)
{
    if (!f)
        return;
    if (current == f) {
        current = NULL;
        if (generated_insn_hook == on_generated)
            generated_insn_hook = next_insn_hook;
    }
    free(f->slots);
    free(f->refusals);
    free(f->ports);
    free(f);
}

void fallbacks_reset(Fallbacks *f)
{
    f->depth = 1;
    f->frames[0] = (Frame){ .kind = UNPORTED, .routine = BANK_ONLY, .port = NO_PORT, .floor = 0xFFFF };
    f->eff = f->frames[0];
    f->pending = false;
    f->pending_rti = false;
    f->pass.open = false;
    f->interrupt.on = false;
    memset(f->aside, 0, sizeof f->aside);
}

static size_t hash(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    uint64_t h = a * 0x9E3779B97F4A7C15ull;
    h ^= (h >> 29) ^ b * 0xBF58476D1CE4E5B9ull;
    h ^= (h >> 31) ^ c * 0x94D049BB133111EBull;
    h ^= (h >> 27) ^ d * 0x9E3779B97F4A7C15ull;
    return (size_t)(h ^ (h >> 32));
}

static Count *slot(Count *slots, size_t cap, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    size_t mask = cap - 1;
    for (size_t i = hash(a, b, c, d) & mask;; i = (i + 1) & mask) {
        Count *k = &slots[i];
        if (!k->used || (k->a == a && k->b == b && k->c == c && k->d == d))
            return k;
    }
}

static void grow(Fallbacks *f)
{
    size_t cap = f->cap * 2;
    Count *slots = calloc(cap, sizeof *slots);
    if (!slots)
        return;   /* keep counting in the full table: it only gets slower */
    for (size_t i = 0; i < f->cap; i++)
        if (f->slots[i].used)
            *slot(slots, cap, f->slots[i].a, f->slots[i].b, f->slots[i].c, f->slots[i].d) = f->slots[i];
    free(f->slots);
    f->slots = slots;
    f->cap = cap;
}

static void add(Fallbacks *f, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint8_t len)
{
    if (f->used * 2 >= f->cap)
        grow(f);
    if (f->used + 1 >= f->cap)
        return;
    Count *k = slot(f->slots, f->cap, a, b, c, d);
    if (!k->used) {
        *k = (Count){ .a = a, .b = b, .c = c, .d = d, .used = true };
        f->used++;
    }
    k->n++;
    k->len = len;
}

/* SHADOW: an instruction a comparison covers, counted on the comparison it is
 * under (the nearest covered frame that is a comparison's own, past the
 * callees' frames above it), so what a comparison that never finishes hides
 * is all on its line. */
static void count_hidden(Fallbacks *f)
{
    for (int i = f->depth - 1; i > 0; i--)
        if (f->frames[i].kind == COVERED && f->frames[i].name) {
            f->frames[i].hidden++;
            return;
        }
}

/* ... and taken back off it, a waiting pass (on_idle). */
static void take_back_hidden(Fallbacks *f, uint64_t n)
{
    for (int i = f->depth - 1; i > 0; i--)
        if (f->frames[i].kind == COVERED && f->frames[i].name) {
            f->frames[i].hidden -= n < f->frames[i].hidden ? n : f->frames[i].hidden;
            return;
        }
}

/* The extents an interrupt's handler can't end: while it runs on a stack of
 * its own, those under the interrupt (the frames below the depth returned),
 * which go on when it returns; else none but the base. */
static int handler_away(const Fallbacks *f, uint16_t s)
{
    return f->interrupt.on && s > f->interrupt.s + STACK_SPAN ? f->interrupt.depth : 1;
}

/* Back above where the interrupt was taken, on its stack: it has returned. */
static void interrupt_returned_when_back(Fallbacks *f, uint16_t s)
{
    if (f->interrupt.on && s > f->interrupt.s && s <= f->interrupt.s + STACK_SPAN)
        f->interrupt.on = false;
}

/* Extents the machine has left: S rose above where they began. Where it rose
 * further than an instruction pulls, the stack was switched (the menus' tasks
 * each run on a stack of their own), and a comparison's own stretch goes on
 * across it, held, until the runtime says it is over (on_finished): a port
 * that switches tasks stands for the original's switch and the going on
 * into the next task, which runs on the other stack. */
static void leave_finished(Fallbacks *f, uint16_t s)
{
    bool switched = s > f->last_s + MOST_A_STEP_PULLS;
    interrupt_returned_when_back(f, s);
    int kept = handler_away(f, s);
    if (f->frames[f->depth - 1].away && s <= f->frames[f->depth - 1].floor)
        f->frames[f->depth - 1].away = false;   /* back on its stack */
    while (f->depth > kept && !f->frames[f->depth - 1].held && !f->frames[f->depth - 1].away &&
           s > f->frames[f->depth - 1].floor) {
        Frame *top = &f->frames[f->depth - 1];
        if (switched && top->kind == COVERED && top->name) {
            top->held = true;
            return;
        }
        /* a port's call-out moving onto a stack of its own goes on there,
         * and ends when it comes back and returns, as the runtime waits for
         * it (a task switch elsewhere ends the extent it leaves) */
        if (switched && top->call_out && f->last_s <= top->floor &&
            (uint32_t)s > (uint32_t)top->floor + NATIVE_STACK_SWITCH_DISTANCE) {
            top->away = true;
            return;
        }
        f->depth--;
    }
}

static void enter(Fallbacks *f, Frame frame)
{
    if (!frame.start)
        frame.start = frame.floor;
    if (f->depth < MAX_FRAMES)
        f->frames[f->depth++] = frame;
}

/* SHADOW: the original has finished what port `e` stood for, or reached where
 * it stopped: the innermost stretch covered by it ends, and what began inside it. */
static void leave_covered(Fallbacks *f, const NativeEntry *e)
{
    for (int i = f->depth - 1; i > 0; i--) {
        if (f->frames[i].kind == COVERED && f->frames[i].port == e->addr) {
            f->depth = i;
            if (f->interrupt.depth > i)
                f->interrupt.depth = i;
            return;
        }
    }
}

/* SHADOW: an interrupt cuts the pass of a wait loop: it is kept aside, and
 * the handler starts with none. A place whose interrupt never came back (its
 * handler went on to another stack) is the oldest, and is taken first. */
static void put_the_pass_aside(Fallbacks *f)
{
    if (f->pass.open) {
        int at = 0;
        for (int i = 1; i < PASSES_ASIDE; i++)
            if (f->aside[i].serial < f->aside[at].serial)
                at = i;
        f->aside[at].serial = ++f->serials;
        f->aside[at].pass = f->pass;
    }
    f->pass.open = false;
}

/* SHADOW: an interrupt returned to S `sp`: the latest pass it cut there goes
 * on. (The extents can't say which interrupt it was: a handler's port entered
 * at the interrupt's own S ends its extent.) */
static void take_the_pass_back(Fallbacks *f, uint16_t sp)
{
    int at = -1;
    for (int i = 0; i < PASSES_ASIDE; i++)
        if (f->aside[i].serial && f->aside[i].pass.s == sp && (at < 0 || f->aside[i].serial > f->aside[at].serial))
            at = i;
    f->pass.open = false;
    if (at >= 0) {
        f->pass = f->aside[at].pass;
        f->aside[at].serial = 0;
    }
}

/* An interrupt returned: its extent ends, or the held one of the handler's
 * comparison, and every one begun inside it. */
static void leave_interrupt(Fallbacks *f)
{
    for (int i = f->depth - 1; i > 0; i--) {
        if (f->frames[i].interrupt || f->frames[i].held) {
            f->depth = i;
            return;
        }
    }
}

/* The latest instruction was an RTI: the interrupt's extent ends, and the
 * pass it cut goes on. Taken at the instruction after it, or at a wait
 * loop's head where it returned to one, which is told first. */
static void after_an_rti(Fallbacks *f, uint16_t sp)
{
    if (!f->pending_rti)
        return;
    leave_interrupt(f);
    take_the_pass_back(f, sp);
    f->pending_rti = false;
}

void fallbacks_note(Fallbacks *f, uint32_t pc, uint8_t p, bool e)
{
    const Snes *s = f->s;
    uint16_t sp = s->cpu.s;
    after_an_rti(f, sp);
    leave_finished(f, sp);
    if (f->pending && sp == f->pending_s)
        enter(f, (Frame){ .kind = f->eff.kind, .routine = pc, .port = f->eff.port, .floor = sp });
    f->pending = false;

    /* A wait loop's pass ends where its routine returns past the loop's level:
     * left open, the next pass through the same head took back whatever ran
     * between (the routine's tail, its caller's next work). */
    if (f->pass.open && sp > f->pass.s)
        f->pass.open = false;

    Frame eff = f->frames[f->depth - 1];
    int away = handler_away(f, sp);
    if (f->depth > 1 && !eff.held && (eff.floor - sp > STACK_SPAN || (away > 1 && f->depth <= away)))
        eff = (Frame){ .kind = UNPORTED, .routine = BANK_ONLY, .port = NO_PORT, .floor = sp };
    f->eff = eff;
    uint32_t routine = eff.routine == BANK_ONLY ? BANK_ONLY | (pc & 0xFF0000) : eff.routine;

    uint8_t op = snes_bus_peek(&s->bus, pc);
    if (eff.kind == COVERED) {
        count_hidden(f);
        if (f->pass.open)
            f->pass.hidden++;
    }
    if (eff.kind != COVERED) {
        bool m8 = e || (p & CPU_FLAG_M), x8 = e || (p & CPU_FLAG_X);
        add(f, pc, eff.kind, routine, eff.port, (uint8_t)misses_instruction_length(op, m8, x8));
        f->by_kind[eff.kind]++;
        if (f->pass.open) {
            if (f->pass.n == PASS_MAX) {
                f->pass.open = false;
            } else {
                uint32_t *k = f->pass.keys[f->pass.n++];
                k[0] = pc, k[1] = eff.kind, k[2] = routine, k[3] = eff.port;
            }
        }
    }

    /* JSR abs, JSR (abs,X), JSL: the next instruction, if it runs with the
     * return address pushed, is the callee's first */
    f->after_a_call = op == 0x20 || op == 0xFC || op == 0x22;
    if (f->after_a_call) {
        f->pending = true;
        f->pending_s = (uint16_t)(sp - (op == 0x22 ? 3 : 2));
        f->callee_s = f->pending_s;
    }
    f->pending_rti = op == 0x40;
    f->last_s = sp;
}

void fallbacks_on_insn(void *ctx, uint32_t pc, uint8_t p, bool e)
{
    fallbacks_note(ctx, pc, p, e);
}

CpuStep fallbacks_interpreter(Cpu *cpu)
{
    CpuStep kind = cpu_step(cpu);
    Fallbacks *f = current;
    if (f && kind == CPU_STEP_INTERRUPT && cpu == &f->s->cpu) {
        leave_finished(f, cpu->s);
        if (!f->interrupt.on || handler_away(f, cpu->s) == 1) {
            f->interrupt.on = true;
            f->interrupt.s = cpu->s;
            f->interrupt.depth = f->depth;
        }
        put_the_pass_aside(f);
        enter(f, (Frame){ .kind = UNPORTED, .interrupt = true, .routine = cpu_pc24(cpu), .port = NO_PORT,
                          .floor = cpu->s });
        f->pending = false;
        f->pending_rti = false;
    }
    return kind;
}

/* ---- the runtime's hand-offs ------------------------------------------------------ */

static Refusal *refusal_of(Fallbacks *f, const NativeEntry *e, const char *why)
{
    for (int i = 0; i < f->nrefusals; i++)
        if (f->refusals[i].addr == e->addr && !strcmp(f->refusals[i].why, why) &&
            !strcmp(f->refusals[i].name, e->name))
            return &f->refusals[i];
    if (f->nrefusals == f->cap_refusals) {
        int cap = f->cap_refusals ? f->cap_refusals * 2 : 16;
        Refusal *r = realloc(f->refusals, sizeof *r * (size_t)cap);
        if (!r)
            return NULL;
        f->refusals = r;
        f->cap_refusals = cap;
    }
    Refusal *r = &f->refusals[f->nrefusals++];
    *r = (Refusal){ .addr = e->addr, .name = e->name, .why = why };
    return r;
}

static void on_refused(void *ctx, Snes *s, const NativeEntry *e, const char *why)
{
    Fallbacks *f = ctx;
    uint16_t sp = s->cpu.s;
    uint32_t pc = cpu_pc24(&s->cpu);
    f->pass.open = false;
    leave_finished(f, sp);
    const Frame *top = &f->frames[f->depth - 1];
    /* asked again at the same call (a generated block that stopped before its
     * first instruction): it is still the one refusal */
    if (top->kind == REFUSED && top->port == e->addr && top->routine == pc && top->floor == sp)
        return;
    enter(f, (Frame){ .kind = REFUSED, .routine = pc, .port = e->addr, .floor = sp });
    f->pending = false;
    Refusal *r = refusal_of(f, e, why);
    if (!r)
        return;
    r->count++;
    if (r->nstates < STATES_KEPT) {
        /* the registers, and for code in RAM whether it is the code the
         * generated blocks were made from (a guard that checks for its own
         * code refuses another routine's) */
        const Cpu *c = &s->cpu;
        char code[160];
        misses_describe(s, code, sizeof code);
        const char *what = strchr(code, ',');
        snprintf(r->states[r->nstates++], sizeof r->states[0],
                 "frame %u A=%04X X=%04X Y=%04X S=%04X D=%04X DB=%02X P=%02X E=%d,%s", (unsigned)s->frame, c->a, c->x,
                 c->y, c->s, c->d, c->db, c->p, c->e, what ? what + 1 : "");
    }
}

/* An event, and its port's name (the registry's, which lives as long as the run). */
static void count_event(Fallbacks *f, int tag, const NativeEntry *e, uint32_t where)
{
    add(f, e->addr, (uint32_t)tag, where, 0, 0);
    for (int i = f->nports - 1; i >= 0; i--)
        if (f->ports[i].addr == e->addr)
            return;
    if (f->nports == f->cap_ports) {
        int cap = f->cap_ports ? f->cap_ports * 2 : 64;
        void *grown = realloc(f->ports, sizeof *f->ports * (size_t)cap);
        if (!grown)
            return;
        f->ports = grown;
        f->cap_ports = cap;
    }
    f->ports[f->nports].addr = e->addr;
    f->ports[f->nports++].name = e->name;
}

static void on_skipped(void *ctx, Snes *s, const NativeEntry *e)
{
    Fallbacks *f = ctx;
    uint16_t sp = s->cpu.s;
    f->pass.open = false;
    leave_finished(f, sp);
    enter(f, (Frame){ .kind = SKIPPED, .routine = cpu_pc24(&s->cpu), .port = e->addr, .floor = sp });
    f->pending = false;
    count_event(f, EV_SKIP, e, 0);
}

static void on_called_out(void *ctx, Snes *s, const NativeEntry *e, uint32_t target)
{
    Fallbacks *f = ctx;
    uint16_t sp = s->cpu.s;
    f->pass.open = false;
    leave_finished(f, sp);
    enter(f, (Frame){ .kind = HANDBACK, .call_out = true, .routine = target, .port = e->addr, .floor = sp });
    f->pending = false;
    count_event(f, EV_CALLOUT, e, target);
}

static void on_stopped(void *ctx, Snes *s, const NativeEntry *e, uint16_t entry_s)
{
    Fallbacks *f = ctx;
    uint16_t sp = s->cpu.s;
    uint32_t pc = cpu_pc24(&s->cpu);
    f->pass.open = false;
    leave_covered(f, e);
    leave_finished(f, sp);
    f->pending = false;
    if (sp > entry_s)
        return;   /* it stopped where its routine has returned: nothing of it is left */
    /* filed under the routine the port stands for: the rest of it is what runs */
    enter(f, (Frame){ .kind = HANDBACK, .routine = e->addr, .port = e->addr, .floor = entry_s, .start = sp });
    count_event(f, EV_HANDBACK, e, pc);
}

/* SHADOW: an extent that is a comparison's own, of one the runtime still
 * makes. */
static bool still_compared(const Fallbacks *f, const Frame *frame)
{
    return f->shadow && frame->kind == COVERED && frame->name && f->rt && native_compares(f->rt, frame->port);
}

/* A hand port takes over: extents begun at its level or below it are over
 * (the original's code went on to it without returning). In SHADOW mode what
 * the original runs from here is the port's, until it calls out, stops or
 * finishes. Where `nested` is set an enclosing comparison's own extent stays,
 * if the runtime still makes the comparison: an entry the original reaches
 * inside a port's stretch at the port's own S (a registered address on the
 * routine's path, the field's pass going on from a step) is compared nested
 * in it, and once it is set aside the enclosing comparison goes on covering
 * what the original runs, until the runtime says it is over. */
static void take_over(Fallbacks *f, uint16_t sp, bool nested)
{
    f->pass.open = false;
    leave_finished(f, sp);
    int kept = handler_away(f, sp);
    while (f->depth > kept && !f->frames[f->depth - 1].held && f->frames[f->depth - 1].start <= sp &&
           !(nested && still_compared(f, &f->frames[f->depth - 1])))
        f->depth--;
    f->pending = false;
    f->pending_rti = false;
}

static void on_entered(void *ctx, Snes *s, const NativeEntry *e)
{
    Fallbacks *f = ctx;
    uint16_t sp = s->cpu.s;
    take_over(f, sp, true);
    if (f->shadow)
        enter(f, (Frame){ .kind = COVERED, .held = e->interrupt, .routine = cpu_pc24(&s->cpu), .port = e->addr,
                          .floor = sp, .name = e->name });
}

/* A port yielded to another task: what the other tasks run is theirs, and
 * in SHADOW mode its stretch so far is over (its comparison isn't: it goes
 * on when the task does). */
static void on_parked(void *ctx, Snes *s, const NativeEntry *e)
{
    Fallbacks *f = ctx;
    (void)s;
    f->pass.open = false;
    f->pending = false;
    if (f->shadow)
        leave_covered(f, e);
}

/* ... and its task goes on in it, or (SHADOW) the comparisons nested in its
 * stretch were set aside at its point: from here the port takes over again,
 * as where it was entered, its routine returning above where it was entered.
 * Where that point is a call it makes (the original at the callee's first
 * instruction), the port stands at the caller's S, where the call was made:
 * the callee, a port entered with its return address pushed, is the port's
 * call-out, not the original going on to another routine. */
static void on_resumed(void *ctx, Snes *s, const NativeEntry *e, uint16_t entry_s)
{
    Fallbacks *f = ctx;
    uint16_t sp = s->cpu.s;
    for (int i = f->depth - 1; i > 0; i--)
        if (f->frames[i].kind == COVERED && f->frames[i].port == e->addr && f->frames[i].name)
            return;   /* its stretch is still there */
    uint16_t level = f->after_a_call && sp == f->callee_s ? f->last_s : sp;
    take_over(f, sp, false);
    if (f->shadow)
        enter(f, (Frame){ .kind = COVERED, .routine = e->addr, .port = e->addr, .floor = entry_s, .start = level,
                          .name = e->name });
}

/* SHADOW: a wait loop's head, where an idle entry applies. A pass that came
 * round to it again, at the same S, is the idle port's: taken back, and so
 * is what of it a comparison hid (a port waiting through the loop): its wait
 * is no work hidden under it. */
static void on_idle(void *ctx, Snes *s, const NativeEntry *e)
{
    Fallbacks *f = ctx;
    (void)e;
    uint32_t pc = cpu_pc24(&s->cpu);
    after_an_rti(f, s->cpu.s);
    if (f->pass.open && f->pass.head == pc && f->pass.s == s->cpu.s) {
        for (int i = 0; i < f->pass.n; i++) {
            const uint32_t *k = f->pass.keys[i];
            Count *c = slot(f->slots, f->cap, k[0], k[1], k[2], k[3]);
            if (c->used && c->n) {
                c->n--;
                f->by_kind[k[1]]--;
            }
        }
        take_back_hidden(f, f->pass.hidden);
    }
    f->pass.open = true;
    f->pass.head = pc;
    f->pass.s = s->cpu.s;
    f->pass.n = 0;
    f->pass.hidden = 0;
}

static void on_finished(void *ctx, const NativeEntry *e)
{
    Fallbacks *f = ctx;
    f->pass.open = false;
    leave_covered(f, e);
}

static void on_returned_from_interrupt(void *ctx, Snes *s, const NativeEntry *e)
{
    Fallbacks *f = ctx;
    (void)e;
    leave_interrupt(f);
    take_the_pass_back(f, s->cpu.s);
}

void fallbacks_attach(Fallbacks *f, NativeRuntime *rt, bool shadow)
{
    f->shadow = shadow;
    f->rt = rt;
    NativeObserver o = { .ctx = f, .refused = on_refused, .skipped = on_skipped, .called_out = on_called_out,
                         .stopped = on_stopped, .entered = on_entered,
                         .returned_from_interrupt = on_returned_from_interrupt, .idle = on_idle,
                         .finished = on_finished, .parked = on_parked, .resumed = on_resumed };
    native_set_observer(rt, &o);
}

/* ---- the report --------------------------------------------------------------------- */

static void put_addr(FILE *out, uint32_t a)
{
    if (a & BANK_ONLY)
        fprintf(out, "%02X----", (unsigned)(a >> 16 & 0xFF));
    else
        fprintf(out, "%06X", (unsigned)(a & 0xFFFFFF));
}

static const char *name_of(const Fallbacks *f, uint32_t addr)
{
    for (int i = 0; i < f->nports; i++)
        if (f->ports[i].addr == addr)
            return f->ports[i].name;
    return "?";
}

int fallbacks_write(Fallbacks *f, const char *path)
{
    FILE *out = fopen(path, "w");
    if (!out)
        return -1;
    fprintf(out, "# fallbacks (dev/fallbacks.h; tools/fallbacks.py reads it)\n"
                 "# insn KIND PC LEN COUNT ROUTINE PORT | event KIND PORT WHERE COUNT NAME | "
                 "refusal ENTRY WHY COUNT NAME | state ENTRY WHY ... | open PORT COUNT NAME\n");
    for (size_t i = 0; i < f->cap; i++) {
        const Count *k = &f->slots[i];
        if (!k->used || k->b >= EV_CALLOUT || !k->n)
            continue;
        fprintf(out, "insn %s %06X %u %llu ", kind_names[k->b], (unsigned)k->a, k->len, (unsigned long long)k->n);
        put_addr(out, k->c);
        fputc(' ', out);
        if (k->d == NO_PORT)
            fputc('-', out);
        else
            put_addr(out, k->d);
        fputc('\n', out);
    }
    /* SHADOW: comparisons the run ended inside, and what the original ran
     * under them uncounted since they began: one that never finishes would
     * hide everything after it until its routine returns, so it is shown */
    for (int i = 1; i < f->depth; i++)
        if (f->frames[i].kind == COVERED && f->frames[i].name && f->frames[i].hidden)
            fprintf(out, "open %06X %llu %s\n", (unsigned)f->frames[i].port,
                    (unsigned long long)f->frames[i].hidden, f->frames[i].name);
    for (size_t i = 0; i < f->cap; i++) {
        const Count *k = &f->slots[i];
        if (!k->used || k->b < EV_CALLOUT)
            continue;
        const char *what = k->b == EV_CALLOUT ? "callout" : k->b == EV_HANDBACK ? "stop" : "skip";
        fprintf(out, "event %s %06X %06X %llu %s\n", what, (unsigned)k->a, (unsigned)k->c, (unsigned long long)k->n,
                name_of(f, k->a));
    }
    for (int i = 0; i < f->nrefusals; i++) {
        const Refusal *r = &f->refusals[i];
        fprintf(out, "refusal %06X %s %llu %s\n", (unsigned)r->addr, r->why, (unsigned long long)r->count, r->name);
        for (int k = 0; k < r->nstates; k++)
            fprintf(out, "state %06X %s %s\n", (unsigned)r->addr, r->why, r->states[k]);
    }
    int bad = ferror(out);
    if (fclose(out) != 0)
        bad = 1;
    return bad ? -1 : 0;
}

void fallbacks_summary(const Fallbacks *f, FILE *out)
{
    fprintf(out, "fallbacks:");
    for (int k = 0; k < KINDS; k++)
        fprintf(out, "%s %s %llu", k ? "," : "", kind_names[k], (unsigned long long)f->by_kind[k]);
    uint64_t refusals = 0;
    for (int i = 0; i < f->nrefusals; i++)
        refusals += f->refusals[i].count;
    uint64_t open = 0;
    for (int i = 1; i < f->depth; i++)
        if (f->frames[i].kind == COVERED && f->frames[i].name)
            open += f->frames[i].hidden;
    fprintf(out, " instructions; %llu refusals at %d entries; %llu instructions under comparisons still open\n",
            (unsigned long long)refusals, f->nrefusals, (unsigned long long)open);
}
