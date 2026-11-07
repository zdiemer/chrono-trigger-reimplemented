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
/* What a native-mode interrupt pushes (the bank, PC and P). */
#define INTERRUPT_PUSHES 4

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
    /* COVERED: a comparison the runtime gave up when an interrupt's handler
     * moved to a stack of its own (the menus' vblank handler): the rest of the
     * routine is still the port's, run by the original after the handler
     * returns to it, until it returns or its task is switched away from */
    bool abandoned;
    uint16_t resume_s; /* ...the S the handler's return comes back to it with; 0 once it has */
    bool has_stop;     /* ...and the port stopped where its routine goes on as the original's: */
    uint32_t stop_pc;  /* the stretch ends there (at this PC with this S), as at a stop */
    uint16_t stop_s;
    uint64_t hidden;   /* COVERED, a comparison's own (named): what it and its callees have not counted */
    const char *name;  /* COVERED: its port's name where the frame is the comparison's own; NULL for a callee's */
    const NativeEntry *entry;
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
    uint8_t span;      /* its idle entry's leaving pass's bytes, to its return (NativeEntry.idle_return_span) */
    uint32_t body[PASS_MAX];   /* the addresses of the latest pass that went round at this head */
    int nbody;
} Pass;

struct Fallbacks {
    struct {
        uint32_t port;
        const char *name;
        const NativeStats *stats;
        uint64_t hidden;
        unsigned instances, calls, matched, mismatched, interrupted, abandoned;
        bool completed;
    } ending[MAX_FRAMES];
    int nending;
    Snes *s;
    const NativeRuntime *rt;    /* the runtime observed */
    Frame frames[MAX_FRAMES];
    int depth;                  /* frames[0] is the base */
    Frame eff;                  /* what the latest instruction was counted as */
    bool pending;               /* the latest instruction was a call... */
    uint16_t pending_s;         /* ...and its callee starts with this S */
    bool pending_rti;           /* the latest instruction was an RTI */
    uint16_t last_s;            /* S at the latest instruction */
    int handlers;               /* interrupts taken and not yet returned from (RTI) */
    struct {                    /* SHADOW: the comparison the runtime gave up last (on_given_up) */
        const NativeEntry *entry;
        bool returned;
        uint32_t stop_pc;
        uint16_t stop_s;
    } given_up;
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
        uint16_t resume_s; /* the S the interrupt's return comes back to it with */
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
    f->handlers = 0;
    f->given_up.entry = NULL;
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

/* SHADOW: the runtime gave up port `e`'s comparison while an interrupt's
 * handler runs on a stack of its own above the routine's (it can't compare
 * a routine whose stack the handler left). The routine itself isn't over:
 * the handler comes back to it, and what the original runs of it from there
 * is still what the port stands for. Its stretch stays, uncompared, until
 * the routine returns, or the handler comes back somewhere else or the
 * routine's task is switched away from (settle_abandoned). */
static bool abandon_under_the_handler(Fallbacks *f, const NativeEntry *e)
{
    if (f->given_up.entry != e || !f->handlers || handler_away(f, f->s->cpu.s) == 1)
        return false;
    for (int i = f->depth - 1; i > 0; i--) {
        Frame *fr = &f->frames[i];
        if (fr->kind == COVERED && fr->port == e->addr) {
            if (i >= f->interrupt.depth)
                return false;   /* the handler's own */
            fr->abandoned = true;
            fr->name = NULL;
            fr->resume_s = (uint16_t)(f->interrupt.s + INTERRUPT_PUSHES);
            fr->has_stop = !f->given_up.returned;
            fr->stop_pc = f->given_up.stop_pc;
            fr->stop_s = f->given_up.stop_s;
            return true;
        }
    }
    return false;
}

/* The stretches of abandoned comparisons, as the original goes on with S `sp`:
 * after a return from an interrupt, kept where it came back to them (where
 * they were interrupted) and ended where it came back somewhere else (the
 * handler switched tasks); outside a handler, ended where the stack moved
 * further than an instruction does (their task switched away). An interrupt
 * taken while they run marks them to be come back to again. */
static void settle_abandoned(Fallbacks *f, uint16_t sp, bool returned)
{
    bool moved = sp > f->last_s + MOST_A_STEP_PULLS || f->last_s > sp + MOST_A_STEP_PULLS;
    for (int i = 1; i < f->depth; i++) {
        Frame *fr = &f->frames[i];
        if (!fr->abandoned)
            continue;
        if (returned && fr->resume_s == sp) {
            fr->resume_s = 0;
            continue;
        }
        if (returned ? sp <= f->interrupt.s + STACK_SPAN : (!f->handlers && !fr->resume_s && moved)) {
            f->depth = i;
            return;
        }
    }
}

/* The original reached where an abandoned stretch's port stopped: from here
 * its routine is the original's, handed back as at the port's stop
 * (on_stopped). */
static void reach_abandoned_stop(Fallbacks *f, uint32_t pc, uint16_t sp)
{
    for (int i = f->depth - 1; i > 0; i--) {
        Frame fr = f->frames[i];
        if (fr.abandoned && fr.has_stop && fr.stop_pc == pc && fr.stop_s == sp) {
            f->depth = i;
            enter(f, (Frame){ .kind = HANDBACK, .routine = fr.port, .port = fr.port, .floor = fr.floor, .start = sp });
            return;
        }
    }
}

/* An interrupt taken at S `sp` (after its pushes) interrupts the abandoned
 * stretches too: the handler's return comes back to them there. */
static void interrupt_the_abandoned(Fallbacks *f, uint16_t sp)
{
    for (int i = 1; i < f->depth; i++)
        if (f->frames[i].abandoned && !f->frames[i].resume_s)
            f->frames[i].resume_s = (uint16_t)(sp + INTERRUPT_PUSHES);
}

/* SHADOW: the pass of a wait loop that an idle entry stands for was the
 * port's: its counts are taken back, and so is what of it a comparison hid
 * (a port waiting through the loop): its wait is no work hidden under it. */
static void take_the_pass_back_as_the_port_s(Fallbacks *f)
{
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

/* SHADOW: the pass ran nothing but the loop's own instructions, as the
 * latest pass that went round did: where the loop's exit is a hand port's
 * entry, that is the pass that leaves the wait, as the idle entry runs it
 * before it sets PC to the exit. */
static bool ran_only_the_loop(const Pass *p)
{
    if (!p->n || !p->nbody)
        return false;
    for (int i = 0; i < p->n; i++) {
        bool in = false;
        for (int j = 0; j < p->nbody && !in; j++)
            in = p->keys[i][0] == p->body[j];
        if (!in)
            return false;
    }
    return true;
}

/* SHADOW: the pass that left the wait returned having run nothing but its
 * idle entry's own bytes, the loop's and its return's: the port's pass. */
static bool returned_as_the_port(const Pass *p)
{
    if (!p->span || !p->n)
        return false;
    for (int i = 0; i < p->n; i++)
        if (p->keys[i][0] < p->head || p->keys[i][0] >= p->head + p->span)
            return false;
    return true;
}

/* SHADOW: an interrupt cuts the pass of a wait loop: it is kept aside, and
 * the handler starts with none. A place whose interrupt never came back (its
 * handler went on to another stack) is the oldest, and is taken first. */
static void put_the_pass_aside(Fallbacks *f, uint16_t resume_s)
{
    if (f->pass.open) {
        int at = 0;
        for (int i = 1; i < PASSES_ASIDE; i++)
            if (f->aside[i].serial < f->aside[at].serial)
                at = i;
        f->aside[at].serial = ++f->serials;
        f->aside[at].resume_s = resume_s;
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
        if (f->aside[i].serial && f->aside[i].resume_s == sp && (at < 0 || f->aside[i].serial > f->aside[at].serial))
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

/* An interrupt's handler returned, to S `sp`. */
static void returned_from_a_handler(Fallbacks *f, uint16_t sp)
{
    if (f->handlers)
        f->handlers--;
    settle_abandoned(f, sp, true);
    f->last_s = sp;
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
    returned_from_a_handler(f, sp);
}

/* The cartridge's CPU half of its sound driver (bank $C7, and its mirror $47)
 * where the machine's sound processor runs the cartridge's driver: the
 * reference play, which is the cartridge on the emulated console. No port
 * stands for it, by design: wherever the hand ports play from power-on the C
 * driver takes the game's boot and commands as calls, and none of this code
 * runs (docs/REIMPLEMENTATION.md, "The sound driver's teardown"). So it is
 * the reference's own machinery, like the sound processor's program, and not
 * counted; where the C driver plays it is, since nothing should reach it. */
static bool the_cartridges_sound_protocol(const Snes *s, uint32_t pc)
{
    return !s->apu.host_command && ((pc >> 16) & 0x7F) == 0x47;
}

/* The cartridge's decompressor ($C3:0002's jump and $C3:0557-$C3:08B2, and
 * their mirror in $43) in the reference play: no port stands for it, by
 * design, since its subsystem retired the gate. Every port calls the C
 * unpacker (game/data/decompress.c), so where the hand ports play none of
 * this code runs; in the reference, the cartridge on the console, it is the
 * original's own machinery, and not counted (docs/REIMPLEMENTATION.md, "The
 * data subsystem's teardown"). Where the hand ports play it is, since
 * nothing should reach it. */
static bool the_cartridges_decompressor(const Fallbacks *f, uint32_t pc)
{
    uint16_t at = (uint16_t)pc;
    return f->shadow && ((pc >> 16) & 0x7F) == 0x43 &&
           ((at >= 0x0002 && at <= 0x0004) || (at >= 0x0557 && at <= 0x08B2));
}

/* The cartridge's time warp ($FD:DA00-$FD:DD95, its handlers $FD:DE15-$FD:DE97
 * and its routines $FD:EAA6-$FD:F28F, and their mirror in $3D) in the
 * reference play: no port stands for it, by design, since its subsystem
 * retired the gate. In the shipping mode its trip is C from the field's end
 * to the field's start again (game/warp/warp.c), so none of this code runs
 * there; in the reference it is the original's own machinery, and not counted
 * (docs/REIMPLEMENTATION.md, "The time warp's teardown"). Where the hand
 * ports play it is, since nothing should reach it. */
static bool the_cartridges_warp(const Fallbacks *f, uint32_t pc)
{
    uint16_t at = (uint16_t)pc;
    return f->shadow && ((pc >> 16) & 0x7F) == 0x7D &&
           ((at >= 0xDA00 && at <= 0xDD95) || (at >= 0xDE15 && at <= 0xDE97) || (at >= 0xEAA6 && at <= 0xF28F));
}

/* The cartridge's overworld in the reference play, as the warp's: the code
 * its retired entries owned in bank $C2 (and its mirror in $02), less what
 * the menus' code shares (tools/burndown.py's RETIRED_RUNS lists the same
 * runs). In the shipping mode the world map is C from the sprite-object
 * mode's start to the field's (game/overworld/), so none of it runs there
 * (docs/REIMPLEMENTATION.md, "The overworld's teardown"). */
static bool the_cartridges_overworld(const Fallbacks *f, uint32_t pc)
{
    static const struct {
        uint16_t first, last;
    } RUNS[] = {
        { 0x229D, 0x2335 }, { 0x2345, 0x26A7 }, { 0x26D6, 0x271F },
        { 0x274D, 0x2D6F }, { 0x2D91, 0x57AF }, { 0x6263, 0x631E },
    };
    if (!f->shadow)
        return false;
    for (int d = f->depth; d >= 0; d--) {
        /* the instruction itself, then the routines it runs inside: the
         * menus' helpers the overworld's code calls (the wrap onto the map,
         * the palette's send) are the cartridge's overworld's there too */
        uint32_t at24 = d == f->depth ? pc : f->frames[d].routine;
        if (((at24 >> 16) & 0x7F) != 0x42)
            continue;
        uint16_t at = (uint16_t)at24;
        for (unsigned i = 0; i < sizeof RUNS / sizeof RUNS[0]; i++)
            if (at >= RUNS[i].first && at <= RUNS[i].last)
                return true;
    }
    /* The objects' loop's jump through the slot's handler (JMP ($0050) at
     * $C2:0553) where the handler it reaches is one of the overworld's: the
     * menus' ports call every other handler as C, so only the cartridge's
     * overworld is reached through it in the reference play. */
    enum { HANDLER_JUMP = 0x0553, HANDLER_JUMP_END = 0x0555, HANDLER_CELL = 0x0050 };
    uint16_t at = (uint16_t)pc;
    if (((pc >> 16) & 0x7F) == 0x42 && at >= HANDLER_JUMP && at <= HANDLER_JUMP_END) {
        uint16_t handler = (uint16_t)(snes_bus_peek(&f->s->bus, HANDLER_CELL) | snes_bus_peek(&f->s->bus, HANDLER_CELL + 1) << 8);
        for (unsigned i = 0; i < sizeof RUNS / sizeof RUNS[0]; i++)
            if (handler >= RUNS[i].first && handler <= RUNS[i].last)
                return true;
    }
    return false;
}

void fallbacks_note(Fallbacks *f, uint32_t pc, uint8_t p, bool e)
{
    /* Observe completion before the caller starts another invocation of the
     * same routine later in this frame. Frame-boundary sampling misses it. */
    if (f->nending)
        (void)fallbacks_end_pending(f);
    const Snes *s = f->s;
    uint16_t sp = s->cpu.s;
    after_an_rti(f, sp);
    settle_abandoned(f, sp, false);
    reach_abandoned_stop(f, pc, sp);
    leave_finished(f, sp);
    if (f->pending && sp == f->pending_s)
        enter(f, (Frame){ .kind = f->eff.kind, .routine = pc, .port = f->eff.port, .floor = sp });
    f->pending = false;

    /* A wait loop's pass ends where its routine returns past the loop's level:
     * left open, the next pass through the same head took back whatever ran
     * between (the routine's tail, its caller's next work). */
    if (f->pass.open && sp > f->pass.s) {
        if (returned_as_the_port(&f->pass))
            take_the_pass_back_as_the_port_s(f);   /* the wait over, returned from as the port returns */
        f->pass.open = false;
    }

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
    if (eff.kind != COVERED && !the_cartridges_sound_protocol(s, pc) && !the_cartridges_decompressor(f, pc) &&
        !the_cartridges_warp(f, pc) && !the_cartridges_overworld(f, pc)) {
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
        put_the_pass_aside(f, (uint16_t)(cpu->s + INTERRUPT_PUSHES));
        f->handlers++;
        interrupt_the_abandoned(f, cpu->s);
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
    /* the wait over: the loop left to the port at its exit, or returned from
     * as its idle entry returns, to a port */
    if (f->pass.open && (ran_only_the_loop(&f->pass) || (sp > f->pass.s && returned_as_the_port(&f->pass))))
        take_the_pass_back_as_the_port_s(f);
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
                          .floor = sp, .name = e->name, .entry = e });
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
                          .name = e->name, .entry = e });
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
    bool round = f->pass.open && f->pass.head == pc && f->pass.s == s->cpu.s;
    if (round) {
        take_the_pass_back_as_the_port_s(f);
        for (int i = 0; i < f->pass.n; i++)
            f->pass.body[i] = f->pass.keys[i][0];
        f->pass.nbody = f->pass.n;
    } else if (f->pass.head != pc || f->pass.s != s->cpu.s) {
        f->pass.nbody = 0;
    }
    f->pass.open = true;
    f->pass.head = pc;
    f->pass.s = s->cpu.s;
    f->pass.n = 0;
    f->pass.hidden = 0;
    f->pass.span = e->idle_return_span;
}

/* SHADOW: the runtime gives a comparison up under a handler that moved the
 * stack, and says where the port's run ended (abandon_under_the_handler). */
static void on_given_up(void *ctx, const NativeEntry *e, bool returned, uint32_t stop_pc, uint16_t stop_s)
{
    Fallbacks *f = ctx;
    f->given_up.entry = e;
    f->given_up.returned = returned;
    f->given_up.stop_pc = stop_pc;
    f->given_up.stop_s = stop_s;
}

static void on_finished(void *ctx, const NativeEntry *e)
{
    Fallbacks *f = ctx;
    bool abandoned = abandon_under_the_handler(f, e);
    f->given_up.entry = NULL;
    if (abandoned)
        return;
    f->pass.open = false;
    leave_covered(f, e);
}

static void on_returned_from_interrupt(void *ctx, Snes *s, const NativeEntry *e)
{
    Fallbacks *f = ctx;
    (void)e;
    leave_interrupt(f);
    take_the_pass_back(f, s->cpu.s);
    returned_from_a_handler(f, s->cpu.s);
}

void fallbacks_attach(Fallbacks *f, NativeRuntime *rt, bool shadow)
{
    f->shadow = shadow;
    f->rt = rt;
    NativeObserver o = { .ctx = f, .refused = on_refused, .skipped = on_skipped, .called_out = on_called_out,
                         .stopped = on_stopped, .entered = on_entered,
                         .returned_from_interrupt = on_returned_from_interrupt, .idle = on_idle,
                         .finished = on_finished, .given_up = on_given_up, .parked = on_parked, .resumed = on_resumed };
    native_set_observer(rt, &o);
}

/* ---- the report --------------------------------------------------------------------- */

void fallbacks_end_begin(Fallbacks *f)
{
    f->nending = 0;
    if (!f->shadow)
        return;
    for (int i = 1; i < f->depth; i++) {
        const Frame *frame = &f->frames[i];
        if (frame->kind != COVERED || !frame->name || !frame->hidden)
            continue;
        const NativeStats *st = native_stats_for(f->rt, frame->entry);
        int n = 0;
        while (n < f->nending && f->ending[n].port != frame->port)
            n++;
        if (n == f->nending) {
            memset(&f->ending[n], 0, sizeof f->ending[n]);
            f->ending[n].port = frame->port;
            f->ending[n].name = frame->name;
            f->ending[n].stats = st;
            if (st) {
                f->ending[n].calls = st->calls;
                f->ending[n].matched = st->matched;
                f->ending[n].mismatched = st->mismatched;
                f->ending[n].interrupted = st->interrupted;
                f->ending[n].abandoned = st->abandoned;
            }
            f->nending++;
        } else if (f->ending[n].stats != st)
            f->ending[n].stats = NULL;   /* ambiguous registry variants: no completion certificate */
        f->ending[n].instances++;
        f->ending[n].hidden += frame->hidden;
    }
}

bool fallbacks_end_pending(Fallbacks *f)
{
    bool pending = false;
    for (int i = 0; i < f->nending; i++) {
        const NativeStats *st = f->ending[i].stats;
        if (!f->ending[i].completed && st && !native_compares(f->rt, f->ending[i].port) &&
            st->calls == f->ending[i].calls &&
            st->matched == f->ending[i].matched + f->ending[i].instances &&
            st->mismatched == f->ending[i].mismatched &&
            st->interrupted == f->ending[i].interrupted && st->abandoned == f->ending[i].abandoned)
            f->ending[i].completed = true;
        pending |= !f->ending[i].completed;
    }
    return pending;
}

int fallbacks_end_write(const Fallbacks *f, const char *path)
{
    FILE *out = fopen(path, "a");
    if (!out)
        return -1;
    for (int i = 0; i < f->nending; i++)
        if (f->ending[i].completed)
            fprintf(out, "closed %06X %llu %s\n", (unsigned)f->ending[i].port,
                    (unsigned long long)f->ending[i].hidden, f->ending[i].name);
    return fclose(out) == 0 ? 0 : -1;
}

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
