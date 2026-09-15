#include "native.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "fiber.h"
#include "port/recomp_snes.h"

static void push8_raw(Snes *s, uint8_t v)
{
    snes_bus_write(&s->bus, s->cpu.s, v);
    s->cpu.s--;
}

static void push16_raw(Snes *s, uint16_t v)
{
    push8_raw(s, v >> 8);
    push8_raw(s, v & 0xFF);
}

#define MAX_ACTIVE 32
#define MAX_DETAILS 16

/* ---- jobs: native routines running on their own stacks ------------------------ */

typedef enum { JOB_RUNNING, JOB_CALLING, JOB_SYNCING, JOB_DONE } JobState;

/* A native routine in progress. It runs on a fiber so that calling code that
 * isn't ported (m_jsr/m_jsl) can suspend it: the call is set up on the machine,
 * control goes back to the system loop, the interpreter runs the callee with
 * time passing as usual, and the routine resumes once the callee returns. */
typedef struct Job {
    Fiber *fiber;
    const NativeEntry *entry;
    Snes *m;               /* machine the routine runs on */
    JobState state;
    uint32_t call_target;  /* JOB_CALLING: the call it made */
    uint32_t call_ret;     /* where the callee returns to, and S after it has */
    uint16_t call_s_after;
    uint16_t call_dont_care;
    struct Job *next_free;
} Job;

static Job *current_job;
static Job *free_jobs;

/* An idle or generated entry running on the machine outside a job. */
static const NativeEntry *running_direct;

const NativeEntry *native_running_entry(void) { return current_job ? current_job->entry : running_direct; }

static void job_body(void *arg)
{
    Job *j = arg;
    for (;;) {
        j->entry->fn(j->m);
        j->state = JOB_DONE;
        fiber_switch(fiber_main());
    }
}

static Job *job_get(void)
{
    Job *j = free_jobs;
    if (j) {
        free_jobs = j->next_free;
        return j;
    }
    j = calloc(1, sizeof *j);
    j->fiber = fiber_create(job_body, j);
    return j;
}

static void job_put(Job *j) { j->next_free = free_jobs, free_jobs = j; }

/* A suspended job whose continuation will never run: its stack can't be
 * unwound, so retire the fiber. */
static void job_abandon(Job *j)
{
    fiber_destroy(j->fiber);
    free(j);
}

/* Run a job until it finishes or makes a call-out. */
static void job_run(Job *j)
{
    Job *outer = current_job;
    current_job = j;
    j->state = JOB_RUNNING;
    fiber_switch(j->fiber);
    current_job = outer;
}

/* ---- runtime ------------------------------------------------------------------ */

typedef enum {
    PH_SEGMENT,   /* native code ran up to a call or return; the original is catching up */
    PH_CALLOUT,   /* both are inside a callee the native routine called */
    PH_SYNC,      /* ON: suspended in native_sync until its time has passed */
} Phase;

/* A registered routine that has been entered and not yet left. */
typedef struct Active {
    const NativeEntry *entry;
    Job *job;              /* NULL once the native routine has returned */
    Phase phase;
    Snes *clone;           /* SHADOW: the machine the native routine runs on */
    uint16_t entry_s;
    uint16_t ret_s;        /* after the native routine returned: S and PC the original must match */
    uint32_t ret_pc;
    uint64_t segment_start;
    uint32_t segment_frame;      /* frame counter when the segment started */
    uint32_t segment_bus_clocks;
    unsigned long segment_port_clears[2];   /* apu_port_clears when the segment started */
    bool segment_interrupted, interrupted, mismatched;
} Active;

/* SHADOW mode record of the original's execution, for replaying generated
 * blocks against it (see "generated blocks in shadow mode" below). */
typedef struct Access {
    uint32_t addr;
    uint32_t clocks;       /* bus clocks the access took (a $420B write includes its DMA) */
    uint8_t value;
    bool write;
} Access;

enum { B_INTERRUPT = 1, B_WAITING = 2, B_STOPPED = 4 };

/* The machine at the start of a step (an instruction boundary). */
typedef struct Boundary {
    uint64_t master;
    uint32_t acc;          /* accesses recorded before this step */
    uint32_t frame;
    uint32_t pc24;
    uint16_t a, x, y, s, d;
    uint8_t db, p, flags;
    bool e;
} Boundary;

typedef struct Replay {
    NativeRuntime *rt;
    uint32_t pos, end;     /* next access to consume, and where the record stops */
    uint64_t clocks;
    bool dead;             /* stopped consuming: failed or ran out of record */
    bool bad;
    bool exhausted;
    bool exhausted_at_fetch;   /* ran out at the first byte of an instruction */
    uint32_t exhaust_addr;
    Cpu exhaust_cpu;
    uint64_t exhaust_clocks;
    char why[200];
} Replay;

struct NativeRuntime {
    const NativeEntry *table;
    int count;
    NativeMode mode;
    NativeStats *stats;
    uint8_t filter[0x2000];      /* bit per 16-bit address: some entry might be here */
    Active active[MAX_ACTIVE];
    int nactive;
    uint32_t debt;               /* ON mode: master clocks native code still owes */
    char details[MAX_DETAILS][400];
    int ndetails;

    int generated;               /* generated entries in the table */
    Snes *attached;
    CpuBus attached_bus;         /* the machine's CPU bus hooks before recording */
    Access *acc;                 /* SHADOW with generated entries: the record */
    uint32_t nacc, cap_acc;
    Boundary *bnd;
    uint32_t nbnd, cap_bnd;
    Snes *replay_machine;        /* its Cpu runs replayed blocks */
    Replay replay;
    unsigned long gen_unfinished;
};

static const NativeRuntime *active_runtime;   /* for synchronous native_call */

uint64_t recomp_executed;
uint32_t recomp_yield_clocks;
static uint32_t yield_clocks_on = SNES_CLOCKS_PER_LINE;

void native_set_recomp_yield(uint32_t clocks) { yield_clocks_on = clocks ? clocks : 1; }
uint64_t native_generated_instructions(void) { return recomp_executed; }

/* By address; hand-written entries before generated ones at the same address. */
static int entry_cmp(const void *a, const void *b)
{
    const NativeEntry *p = a, *q = b;
    if (p->addr != q->addr)
        return p->addr < q->addr ? -1 : 1;
    return (int)p->generated - (int)q->generated;
}

NativeRuntime *native_create(const NativeEntry *table, int count, NativeMode mode)
{
    NativeRuntime *rt = calloc(1, sizeof *rt);
    NativeEntry *sorted = malloc(sizeof *sorted * (size_t)(count ? count : 1));
    memcpy(sorted, table, sizeof *sorted * (size_t)count);
    qsort(sorted, (size_t)count, sizeof *sorted, entry_cmp);
    rt->table = sorted;
    rt->count = count;
    rt->mode = mode;
    rt->stats = calloc((size_t)(count ? count : 1), sizeof *rt->stats);
    for (int i = 0; i < count; i++) {
        uint16_t a = sorted[i].addr & 0xFFFF;
        rt->filter[a >> 3] |= (uint8_t)(1 << (a & 7));
        rt->generated += sorted[i].generated;
    }
    recomp_yield_clocks = mode == NATIVE_ON ? yield_clocks_on : 0;
    active_runtime = rt;
    return rt;
}

static void active_pop(NativeRuntime *rt)
{
    Active *a = &rt->active[--rt->nactive];
    if (a->job) {
        job_abandon(a->job);
        a->job = NULL;
    }
    free(a->clone);
    a->clone = NULL;
}

void native_destroy(NativeRuntime *rt)
{
    if (!rt)
        return;
    if (active_runtime == rt)
        active_runtime = NULL;
    while (rt->nactive > 0)
        active_pop(rt);
    if (rt->attached && rt->acc)
        rt->attached->cpu.bus = rt->attached_bus;
    free(rt->acc);
    free(rt->bnd);
    free(rt->replay_machine);
    free((void *)rt->table);
    free(rt->stats);
    free(rt);
}

int native_suspended(const NativeRuntime *rt) { return rt->nactive; }

/* The first entry at pc (hand-written ones come first), or -1. */
static int find_entry(const NativeRuntime *rt, uint32_t pc)
{
    uint16_t a = pc & 0xFFFF;
    if (!(rt->filter[a >> 3] & (1 << (a & 7))))
        return -1;
    int lo = 0, hi = rt->count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (rt->table[mid].addr < pc)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < rt->count && rt->table[lo].addr == pc ? lo : -1;
}

/* Entry preconditions: register widths/modes on P, native mode, and the guard.
 * A generated block also needs the CPU running (not halted by WAI or STP). */
static bool entry_applies(const NativeEntry *e, Snes *s)
{
    if ((s->cpu.p & e->p_mask) != e->p_value || s->cpu.e)
        return false;
    if (e->generated)
        return !s->cpu.waiting && !s->cpu.stopped;
    return !e->guard || e->guard(s);
}

/* ---- comparison --------------------------------------------------------------- */

static void detail(NativeRuntime *rt, const char *prefix, const NativeEntry *e, const char *what)
{
    if (rt->ndetails >= MAX_DETAILS)
        return;
    snprintf(rt->details[rt->ndetails++], sizeof rt->details[0], "%s %06X %s: %s", prefix, e->addr, e->name, what);
}

static bool diff_bytes(char *out, size_t n, const char *name, const uint8_t *a, const uint8_t *b, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        if (a[i] != b[i]) {
            snprintf(out, n, "%s[%04zX] original %02X native %02X", name, i, a[i], b[i]);
            return true;
        }
    }
    return false;
}

#define CHECK_FIELD(label, x, y)                                                                         \
    if ((x) != (y)) {                                                                                    \
        snprintf(out, n, "%s original %lX native %lX", label, (unsigned long)(x), (unsigned long)(y));   \
        return true;                                                                                     \
    }

/* Returns true and describes the first difference if the machines differ in
 * anything CPU-side code can observe or change. */
bool native_compare(const Snes *o, const Snes *c, uint16_t dont_care, char *out, size_t n)
{
    uint8_t ignored_flags = (uint8_t)((dont_care & DC_N ? CPU_FLAG_N : 0) | (dont_care & DC_V ? CPU_FLAG_V : 0) |
                                      (dont_care & DC_Z ? CPU_FLAG_Z : 0) | (dont_care & DC_C ? CPU_FLAG_C : 0));
    uint8_t p_mask = (uint8_t)~ignored_flags;
    if (!(dont_care & DC_A))
        CHECK_FIELD("A", o->cpu.a, c->cpu.a)
    if (!(dont_care & DC_X))
        CHECK_FIELD("X", o->cpu.x, c->cpu.x)
    if (!(dont_care & DC_Y))
        CHECK_FIELD("Y", o->cpu.y, c->cpu.y)
    CHECK_FIELD("S", o->cpu.s, c->cpu.s)
    if (!(dont_care & DC_D))
        CHECK_FIELD("D", o->cpu.d, c->cpu.d)
    if (!(dont_care & DC_DB))
        CHECK_FIELD("DB", o->cpu.db, c->cpu.db)
    CHECK_FIELD("PB", o->cpu.pb, c->cpu.pb)
    CHECK_FIELD("PC", o->cpu.pc, c->cpu.pc)
    CHECK_FIELD("P", o->cpu.p & p_mask, c->cpu.p & p_mask)
    CHECK_FIELD("E", o->cpu.e, c->cpu.e)

    if (diff_bytes(out, n, "WRAM", o->bus.wram, c->bus.wram, sizeof o->bus.wram) ||
        diff_bytes(out, n, "SRAM", o->bus.sram, c->bus.sram, sizeof o->bus.sram) ||
        diff_bytes(out, n, "VRAM", o->ppu.vram, c->ppu.vram, sizeof o->ppu.vram) ||
        diff_bytes(out, n, "CGRAM", (const uint8_t *)o->ppu.cgram, (const uint8_t *)c->ppu.cgram,
                   sizeof o->ppu.cgram) ||
        diff_bytes(out, n, "OAM", o->ppu.oam, c->ppu.oam, sizeof o->ppu.oam))
        return true;

    /* PPU registers that enabled HDMA channels write each scanline change with
     * time, not with the code under test, so skip them and the state they feed. */
    static const uint8_t unit[8][4] = {
        { 0, 0, 0, 0 }, { 0, 1, 0, 1 }, { 0, 0, 0, 0 }, { 0, 0, 1, 1 },
        { 0, 1, 2, 3 }, { 0, 1, 0, 1 }, { 0, 0, 0, 0 }, { 0, 0, 1, 1 },
    };
    uint64_t hdma_regs = 0;
    for (int ch = 0; ch < 8; ch++) {
        if (!(o->bus.hdmaen & (1 << ch)))
            continue;
        const uint8_t *r = o->bus.dma[ch].reg;
        for (int i = 0; i < 4; i++) {
            uint8_t reg = (uint8_t)(r[1] + unit[r[0] & 7][i]);
            if (reg < 0x40)
                hdma_regs |= 1ull << reg;
        }
    }
    const Ppu *po = &o->ppu, *pc = &c->ppu;
    for (int reg = 0; reg < 0x40; reg++) {
        if (!(hdma_regs >> reg & 1) && po->reg[reg] != pc->reg[reg]) {
            snprintf(out, n, "PPU $21%02X original %02X native %02X", reg, po->reg[reg], pc->reg[reg]);
            return true;
        }
    }
    #define HDMA_TOUCHES(lo, hi) ((hdma_regs >> (lo)) & ((2ull << ((hi) - (lo))) - 1))
    if (!HDMA_TOUCHES(0x16, 0x19)) {
        CHECK_FIELD("VMADD", po->vram_addr, pc->vram_addr)
        CHECK_FIELD("VRAM prefetch", po->vram_prefetch, pc->vram_prefetch)
    }
    if (!HDMA_TOUCHES(0x21, 0x22)) {
        CHECK_FIELD("CGADD", po->cg_addr, pc->cg_addr)
        CHECK_FIELD("CGRAM flip-flop", po->cg_second, pc->cg_second)
        CHECK_FIELD("CGRAM latch", po->cg_latch, pc->cg_latch)
    }
    if (!HDMA_TOUCHES(0x02, 0x04)) {
        CHECK_FIELD("OAM reload", po->oam_reload, pc->oam_reload)
        CHECK_FIELD("OAMADD", po->oam_addr, pc->oam_addr)
        CHECK_FIELD("OAM latch", po->oam_latch, pc->oam_latch)
    }
    if (!HDMA_TOUCHES(0x0D, 0x14)) {
        CHECK_FIELD("BGnOFS latch", po->bgofs_latch, pc->bgofs_latch)
        CHECK_FIELD("BGnHOFS latch", po->bghofs_latch, pc->bghofs_latch)
    }
    if (!HDMA_TOUCHES(0x0D, 0x0E) && !HDMA_TOUCHES(0x1B, 0x20))
        CHECK_FIELD("Mode 7 latch", po->m7_latch, pc->m7_latch)
    #undef HDMA_TOUCHES

    const SnesBus *bo = &o->bus, *bc = &c->bus;
    CHECK_FIELD("NMITIMEN", bo->nmitimen, bc->nmitimen)
    CHECK_FIELD("WRIO", bo->wrio, bc->wrio)
    CHECK_FIELD("WRMPYA", bo->wrmpya, bc->wrmpya)
    CHECK_FIELD("WRMPYB", bo->wrmpyb, bc->wrmpyb)
    CHECK_FIELD("WRDIV", bo->wrdiv, bc->wrdiv)
    CHECK_FIELD("RDDIV", bo->rddiv, bc->rddiv)
    CHECK_FIELD("RDMPY", bo->rdmpy, bc->rdmpy)
    CHECK_FIELD("HTIME", bo->htime, bc->htime)
    CHECK_FIELD("VTIME", bo->vtime, bc->vtime)
    CHECK_FIELD("HDMAEN", bo->hdmaen, bc->hdmaen)
    CHECK_FIELD("MEMSEL", bo->fastrom, bc->fastrom)
    CHECK_FIELD("MDMAEN", bo->mdmaen, bc->mdmaen)
    CHECK_FIELD("WRAM port address", bo->wram_port_addr, bc->wram_port_addr)
    CHECK_FIELD("joypad latch", bo->latch, bc->latch)
    /* HDMA advances each channel's table address ($43x8-9), line counter ($43xA)
     * and indirect address ($43x5-6) every scanline, whatever the CPU is doing,
     * so those change with time and aren't compared. */
    const uint16_t hdma_timed = (1u << 5) | (1u << 6) | (1u << 8) | (1u << 9) | (1u << 10);
    for (int ch = 0; ch < 8; ch++) {
        for (int r = 0; r < 16; r++) {
            if (hdma_timed & (1u << r))
                continue;
            if (bo->dma[ch].reg[r] != bc->dma[ch].reg[r]) {
                snprintf(out, n, "DMA $43%X%X original %02X native %02X", ch, r, bo->dma[ch].reg[r],
                         bc->dma[ch].reg[r]);
                return true;
            }
        }
    }
    if (diff_bytes(out, n, "APU port (CPU to APU)", o->apu.cpu_in, c->apu.cpu_in, sizeof o->apu.cpu_in))
        return true;
    return false;
}

/* ---- intercept -------------------------------------------------------------- */

static bool interrupt_deliverable(const Snes *s)
{
    return s->cpu.nmi_pending || (s->cpu.irq_line && !(s->cpu.p & CPU_FLAG_I));
}

static NativeStats *stats_of(NativeRuntime *rt, const Active *a) { return &rt->stats[a->entry - rt->table]; }

/* Close a segment of original execution that native code stood in for. */
static void end_segment(NativeRuntime *rt, Active *a, Snes *s)
{
    NativeStats *st = stats_of(rt, a);
    st->original_clocks += s->master - a->segment_start;
    st->native_bus_clocks += a->segment_bus_clocks;
}

static void compare(NativeRuntime *rt, Active *a, Snes *s, uint16_t dont_care, const char *where)
{
    if (a->segment_interrupted) {
        a->interrupted = true;
        return;
    }
    /* The SPC700 clears the CPU-to-APU ports ($F1 bits 4-5) when its own code
     * decides to, whatever the CPU is doing: a pair it cleared while the
     * original ran isn't compared. */
    for (int pair = 0; pair < 2; pair++)
        if (apu_port_clears[pair] != a->segment_port_clears[pair])
            memcpy(a->clone->apu.cpu_in + 2 * pair, s->apu.cpu_in + 2 * pair, 2);
    /* Vblank reloads the OAM address (outside forced blank), so it moves if a
     * vblank passed while the original ran with NMI disabled. */
    if (s->frame != a->segment_frame)
        a->clone->ppu.oam_addr = s->ppu.oam_addr;
    char why[200], msg[256];
    if (native_compare(s, a->clone, dont_care, why, sizeof why)) {
        a->mismatched = true;
        snprintf(msg, sizeof msg, "%s: %s", where, why);
        detail(rt, "mismatch", a->entry, msg);
    }
}

static void finish(NativeRuntime *rt)
{
    Active *a = &rt->active[rt->nactive - 1];
    NativeStats *st = stats_of(rt, a);
    if (rt->mode == NATIVE_SHADOW) {
        if (a->mismatched)
            st->mismatched++;
        else if (a->interrupted)
            st->interrupted++;
        else
            st->matched++;
    }
    active_pop(rt);
}

/* After running native code for `a`: account for it and record where it stopped. */
static void after_run(NativeRuntime *rt, Active *a, Snes *s)
{
    Job *j = a->job;
    Snes *m = j->m;
    if (rt->mode == NATIVE_ON) {
        rt->debt += (uint32_t)((uint64_t)m->bus.clocks * a->entry->cost_milli / 1000);
        m->bus.clocks = 0;
        if (j->state == JOB_SYNCING) {
            a->phase = PH_SYNC;
            return;
        }
    } else {
        a->segment_bus_clocks = m->bus.clocks;
        a->segment_start = s->master;
        a->segment_frame = s->frame;
        memcpy(a->segment_port_clears, apu_port_clears, sizeof apu_port_clears);
        a->segment_interrupted = false;
    }
    if (j->state == JOB_DONE) {
        job_put(j);
        a->job = NULL;
        a->ret_s = m->cpu.s;
        a->ret_pc = cpu_pc24(&m->cpu);
    }
    a->phase = rt->mode == NATIVE_ON ? PH_CALLOUT : PH_SEGMENT;
    if (rt->mode == NATIVE_ON && !a->job)
        finish(rt);
}

/* Check the innermost active routine against where the machine is now.
 * Returns true if it changed (so the next one may need checking). */
static bool check_top(NativeRuntime *rt, Snes *s, uint32_t pc, int32_t *result)
{
    Active *a = &rt->active[rt->nactive - 1];
    NativeStats *st = stats_of(rt, a);

    if (a->phase == PH_SEGMENT) {   /* SHADOW only */
        if (!a->job) {
            if (s->cpu.s == a->ret_s && pc == a->ret_pc) {
                end_segment(rt, a, s);
                compare(rt, a, s, a->entry->dont_care, "at return");
                finish(rt);
                return true;
            }
            /* Above both the stop point and the entry: an early stop can be
             * below the entry (after pushes), where the original starts. */
            if (s->cpu.s > a->ret_s && s->cpu.s > a->entry_s) {
                /* An interrupt handler may switch stacks (the menu code runs
                 * tasks on their own stacks); nothing to compare then. */
                if (a->segment_interrupted)
                    a->interrupted = true;
                else {
                    a->mismatched = true;
                    detail(rt, "mismatch", a->entry, "original left the routine without returning where the native one did");
                }
                finish(rt);
                return true;
            }
            return false;
        }
        const Job *j = a->job;
        if (pc == j->call_target && s->cpu.s == a->clone->cpu.s) {
            char where[48];
            snprintf(where, sizeof where, "at call to %06X", j->call_target);
            end_segment(rt, a, s);
            compare(rt, a, s, j->call_dont_care, where);
            a->phase = PH_CALLOUT;
            return false;
        }
        if (s->cpu.s > a->entry_s + 1 && a->segment_interrupted) {
            a->interrupted = true;
            finish(rt);
            return true;
        }
        if (s->cpu.s > a->entry_s + 1) {
            char msg[96];
            snprintf(msg, sizeof msg, "original returned without the call to %06X the native routine made",
                     j->call_target);
            a->mismatched = true;
            detail(rt, "mismatch", a->entry, msg);
            finish(rt);
            return true;
        }
        return false;
    }

    if (a->phase == PH_SYNC) {   /* ON only; intercept has paid the debt by now */
        s->bus.clocks = 0;
        job_run(a->job);
        after_run(rt, a, s);
        *result = 0;
        return true;
    }

    /* PH_CALLOUT: waiting for the callee to return. */
    const Job *j = a->job;
    if (s->cpu.s == j->call_s_after && pc == j->call_ret) {
        if (interrupt_deliverable(s))
            return false;   /* resume after the interrupt returns here */
        if (rt->mode == NATIVE_SHADOW) {
            Snes *clone = a->clone;
            snes_clone(clone, s);
            clone->bus.clocks = 0;
            a->job->m = clone;
        } else {
            s->bus.clocks = 0;
        }
        job_run(a->job);
        after_run(rt, a, s);
        if (rt->mode == NATIVE_ON)
            *result = 0;   /* the machine moved; look again before interpreting */
        return true;
    }
    if (s->cpu.s > j->call_s_after) {
        /* The callee unwound past its own return (discarding the caller's frame);
         * the rest of the routine never runs, natively or originally. */
        st->abandoned++;
        if (st->abandoned == 1) {
            char msg[128];
            snprintf(msg, sizeof msg, "callee %06X unwound past its return: at %06X with S %04X above %04X",
                     j->call_target, pc, s->cpu.s, j->call_s_after);
            detail(rt, "abandoned", a->entry, msg);
        }
        finish(rt);
        return true;
    }
    return false;
}

/* ---- generated blocks in shadow mode ---------------------------------------------
 *
 * Cloning the machine for every block, as for hand ports, would cost a few
 * hundred kilobytes of copying and comparing per block entry. Instead the
 * interpreter's CPU bus accesses are recorded (address, value, direction, bus
 * time), with the registers and master clock at every instruction boundary.
 * Every so often the record is replayed: at each boundary where a generated
 * block applies (right widths, native mode, not an interrupt entry), the block
 * runs on a scratch CPU from the boundary's registers, with its bus accesses
 * served from the record. Each access must be the one the original made, in the
 * same order (instruction fetches included), with the same value written; where
 * the block stops, the original must be at an instruction boundary with the same
 * registers, and the same time must have passed. Everything a block can observe
 * comes through its accesses, so this checks every block the machine reaches,
 * with nothing sampled and no machine copies. An interrupt before the block
 * stops ends the check at the interrupt's boundary, where the registers and
 * time so far are compared (counted as interrupted). Blocks then chain: the next
 * check starts where this block stopped, as ON mode's dispatch would. */

#define RECORD_BOUNDARIES (1u << 17)
#define RECORD_ACCESSES (1u << 21)

static uint8_t record_read(void *ctx, uint32_t addr)
{
    SnesBus *bus = ctx;
    uint32_t before = bus->clocks;
    uint8_t v = snes_bus_read(ctx, addr);
    NativeRuntime *rt = (NativeRuntime *)active_runtime;
    if (rt && rt->attached && bus == &rt->attached->bus && rt->nacc < rt->cap_acc)
        rt->acc[rt->nacc++] = (Access){ addr, bus->clocks - before, v, false };
    return v;
}

static void record_write(void *ctx, uint32_t addr, uint8_t v)
{
    SnesBus *bus = ctx;
    uint32_t before = bus->clocks;
    snes_bus_write(ctx, addr, v);
    NativeRuntime *rt = (NativeRuntime *)active_runtime;
    if (rt && rt->attached && bus == &rt->attached->bus && rt->nacc < rt->cap_acc)
        rt->acc[rt->nacc++] = (Access){ addr, bus->clocks - before, v, true };
}

static void replay_fail(Replay *r, const char *why)
{
    if (!r->dead) {
        r->bad = true;
        snprintf(r->why, sizeof r->why, "%s", why);
    }
    r->dead = true;
}

/* The next recorded access, or NULL (and the replay stops consuming) if the
 * record ends here. */
static const Access *replay_next(Replay *r, const Cpu *c, uint32_t addr, bool fetch_start)
{
    if (r->dead)
        return NULL;
    if (r->pos >= r->end) {
        r->dead = true;
        r->exhausted = true;
        r->exhausted_at_fetch = fetch_start;
        r->exhaust_addr = addr;
        if (c)
            r->exhaust_cpu = *c;
        r->exhaust_clocks = r->clocks;
        return NULL;
    }
    return &r->rt->acc[r->pos];
}

static void describe_access(char *out, size_t n, const Access *a)
{
    if (a->write)
        snprintf(out, n, "wrote %02X to %06X", a->value, a->addr);
    else
        snprintf(out, n, "read %06X", a->addr);
}

static uint8_t replay_read(void *ctx, uint32_t addr)
{
    Replay *r = ctx;
    const Access *a = replay_next(r, NULL, addr, false);
    if (!a)
        return 0;
    if (a->write || a->addr != addr) {
        char orig[48], why[160];
        describe_access(orig, sizeof orig, a);
        snprintf(why, sizeof why, "block read %06X where the original %s", addr, orig);
        replay_fail(r, why);
        return 0;
    }
    r->pos++;
    r->clocks += a->clocks;
    return a->value;
}

static void replay_write(void *ctx, uint32_t addr, uint8_t v)
{
    Replay *r = ctx;
    const Access *a = replay_next(r, NULL, addr, false);
    if (!a)
        return;
    if (!a->write || a->addr != addr || a->value != v) {
        char orig[48], why[160];
        describe_access(orig, sizeof orig, a);
        snprintf(why, sizeof why, "block wrote %02X to %06X where the original %s", v, addr, orig);
        replay_fail(r, why);
        return;
    }
    r->pos++;
    r->clocks += a->clocks;
}

void native_replay_fetch(Cpu *c, uint32_t addr, unsigned n, uint8_t last)
{
    Replay *r = c->bus.ctx;
    for (unsigned i = 0; i < n; i++) {
        uint32_t at = (addr & 0xFF0000) | (uint16_t)(addr + i);
        const Access *a = replay_next(r, c, at, i == 0);
        if (!a)
            return;
        if (a->write || a->addr != at || (i == n - 1 && a->value != last)) {
            char orig[48], why[160];
            describe_access(orig, sizeof orig, a);
            snprintf(why, sizeof why, "block fetched %06X where the original %s", at, orig);
            replay_fail(r, why);
            return;
        }
        r->pos++;
        r->clocks += a->clocks;
    }
}

static void record_boundary(NativeRuntime *rt, Snes *s)
{
    const Cpu *c = &s->cpu;
    Boundary *b = &rt->bnd[rt->nbnd++];
    b->master = s->master;
    b->acc = rt->nacc;
    b->frame = s->frame;
    b->pc24 = cpu_pc24(c);
    b->a = c->a, b->x = c->x, b->y = c->y, b->s = c->s, b->d = c->d;
    b->db = c->db, b->p = c->p, b->e = c->e;
    b->flags = (uint8_t)((interrupt_deliverable(s) ? B_INTERRUPT : 0) | (c->waiting ? B_WAITING : 0) |
                         (c->stopped ? B_STOPPED : 0));
}

static int find_generated(const NativeRuntime *rt, const Boundary *b)
{
    int i = find_entry(rt, b->pc24);
    if (i < 0)
        return -1;
    for (; i < rt->count && rt->table[i].addr == b->pc24; i++) {
        const NativeEntry *e = &rt->table[i];
        if (e->generated && (b->p & e->p_mask) == e->p_value)
            return i;
    }
    return -1;
}

/* Registers where a replayed block stopped against a recorded boundary. */
static bool registers_differ(const Cpu *c, uint32_t pc24, const Boundary *b, char *out, size_t n)
{
#define REG(label, x, y)                                                                              \
    if ((x) != (y)) {                                                                                 \
        snprintf(out, n, "%s original %lX block %lX", label, (unsigned long)(y), (unsigned long)(x)); \
        return true;                                                                                  \
    }
    REG("PC", pc24, b->pc24)
    REG("A", c->a, b->a)
    REG("X", c->x, b->x)
    REG("Y", c->y, b->y)
    REG("S", c->s, b->s)
    REG("D", c->d, b->d)
    REG("DB", c->db, b->db)
    REG("P", c->p, b->p)
    REG("E", c->e, b->e)
    REG("WAI", c->waiting, (b->flags & B_WAITING) != 0)
    REG("STP", c->stopped, (b->flags & B_STOPPED) != 0)
#undef REG
    return false;
}

static void generated_mismatch(NativeRuntime *rt, int idx, const Boundary *start, const Boundary *at,
                               const char *why)
{
    rt->stats[idx].mismatched++;
    char msg[300];
    snprintf(msg, sizeof msg, "from frame %u, stopped at %06X: %s", (unsigned)start->frame,
             (unsigned)at->pc24, why);
    detail(rt, "mismatch", &rt->table[idx], msg);
}

/* Replay the record up to the last boundary whose blocks can be decided, and
 * drop what has been checked. `final`: the record ends here for good. */
static void replay_record(NativeRuntime *rt, bool final)
{
    uint32_t nb = rt->nbnd;
    if (nb == 0)
        return;
    /* next_int[i]: the first boundary at or after i where an interrupt entry runs */
    uint32_t *next_int = malloc(sizeof *next_int * nb);
    uint32_t next = nb;
    for (uint32_t i = nb; i-- > 0;) {
        if (rt->bnd[i].flags & B_INTERRUPT)
            next = i;
        next_int[i] = next;
    }
    Snes *m = rt->replay_machine;
    Replay *r = &rt->replay;
    uint32_t i = 0;
    while (i + 1 < nb) {
        const Boundary *b = &rt->bnd[i];
        if ((b->flags & (B_INTERRUPT | B_WAITING | B_STOPPED)) || b->e) {
            i++;
            continue;
        }
        int idx = find_generated(rt, b);
        if (idx < 0) {
            i++;
            continue;
        }
        uint32_t k = next_int[i + 1];
        bool open = k >= nb;
        memset(r, 0, offsetof(Replay, why));
        r->rt = rt;
        r->pos = b->acc;
        /* up to the interrupt, or to the latest boundary: accesses after it
         * belong to a step still in progress */
        r->end = open ? rt->bnd[nb - 1].acc : rt->bnd[k].acc;
        Cpu *c = &m->cpu;
        c->a = b->a, c->x = b->x, c->y = b->y, c->s = b->s, c->d = b->d;
        c->db = b->db, c->p = b->p, c->e = false;
        c->pb = b->pc24 >> 16, c->pc = b->pc24 & 0xFFFF;
        c->io_cycles = 0;
        c->waiting = c->stopped = false;
        c->bus = (CpuBus){ r, replay_read, replay_write };
        rt->table[idx].fn(m);

        if (r->bad) {
            rt->stats[idx].calls++;
            /* where in the original: the step holding the failing access */
            uint32_t lo = i, hi = nb - 1;
            while (lo < hi) {
                uint32_t mid = (lo + hi + 1) / 2;
                if (rt->bnd[mid].acc <= r->pos)
                    lo = mid;
                else
                    hi = mid - 1;
            }
            generated_mismatch(rt, idx, b, &rt->bnd[lo], r->why);
            i++;
            continue;
        }
        if (r->exhausted) {
            if (open) {
                if (!final)
                    break;   /* the original hasn't got that far yet */
                rt->gen_unfinished++;
                i++;
                continue;
            }
            rt->stats[idx].calls++;
            const Boundary *at = &rt->bnd[k];
            char why[160];
            uint64_t spent = r->exhaust_clocks + 6ull * r->exhaust_cpu.io_cycles;
            if (!r->exhausted_at_fetch)
                generated_mismatch(rt, idx, b, at, "the original took an interrupt inside an instruction of the block");
            else if (registers_differ(&r->exhaust_cpu, r->exhaust_addr, at, why, sizeof why))
                generated_mismatch(rt, idx, b, at, why);
            else if (spent != at->master - b->master) {
                snprintf(why, sizeof why, "time original %llu block %llu (to an interrupt)",
                         (unsigned long long)(at->master - b->master), (unsigned long long)spent);
                generated_mismatch(rt, idx, b, at, why);
            } else
                rt->stats[idx].interrupted++;
            i = k;
            continue;
        }
        rt->stats[idx].calls++;
        /* the block stopped: the original's first boundary after the same accesses */
        uint32_t lo = i + 1, hi = nb;
        while (lo < hi) {
            uint32_t mid = (lo + hi) / 2;
            if (rt->bnd[mid].acc < r->pos)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo >= nb || rt->bnd[lo].acc != r->pos) {
            generated_mismatch(rt, idx, b, &rt->bnd[lo >= nb ? nb - 1 : lo],
                               "the block stopped inside an instruction of the original");
            i++;
            continue;
        }
        const Boundary *at = &rt->bnd[lo];
        char why[160];
        uint64_t spent = r->clocks + 6ull * c->io_cycles;
        if (registers_differ(c, cpu_pc24(c), at, why, sizeof why))
            generated_mismatch(rt, idx, b, at, why);
        else if (spent != at->master - b->master) {
            snprintf(why, sizeof why, "time original %llu block %llu", (unsigned long long)(at->master - b->master),
                     (unsigned long long)spent);
            generated_mismatch(rt, idx, b, at, why);
        } else
            rt->stats[idx].matched++;
        i = lo;
    }
    free(next_int);
    if (final)
        i = nb;
    /* keep boundaries from i on */
    uint32_t base = i < nb ? rt->bnd[i].acc : rt->nacc;
    memmove(rt->acc, rt->acc + base, sizeof *rt->acc * (rt->nacc - base));
    rt->nacc -= base;
    memmove(rt->bnd, rt->bnd + i, sizeof *rt->bnd * (nb - i));
    rt->nbnd = nb - i;
    for (uint32_t j = 0; j < rt->nbnd; j++)
        rt->bnd[j].acc -= base;
}

/* At the start of every step. The record is checked when it fills up, right
 * after a boundary (so every recorded access belongs to a step that ended). */
static void record_step(NativeRuntime *rt, Snes *s)
{
    record_boundary(rt, s);
    if (rt->nbnd < rt->cap_bnd && rt->nacc + 0x10000 < rt->cap_acc)
        return;
    replay_record(rt, false);
    if (rt->nbnd >= rt->cap_bnd || rt->nacc + 0x10000 >= rt->cap_acc) {
        /* a block whose record doesn't fit: give up on it */
        rt->gen_unfinished++;
        rt->nbnd = 0;
        rt->nacc = 0;
        record_boundary(rt, s);
    }
}

static int32_t intercept(void *ctx, Snes *s)
{
    NativeRuntime *rt = ctx;
    uint32_t pc = cpu_pc24(&s->cpu);

    if (rt->acc)
        record_step(rt, s);

    /* Native code finishes instantly but the original took time. Pay that time
     * off a scanline at a time so frame boundaries and line events still happen
     * while the routine "runs". */
    if (rt->debt > 0) {
        uint32_t spend = rt->debt < SNES_CLOCKS_PER_LINE ? rt->debt : SNES_CLOCKS_PER_LINE;
        rt->debt -= spend;
        return (int32_t)spend;
    }

    int32_t result = -1;
    while (rt->nactive > 0 && check_top(rt, s, pc, &result)) {
        if (result >= 0)
            return result;
    }

    if (interrupt_deliverable(s)) {
        for (int i = 0; i < rt->nactive; i++) {
            if (rt->active[i].phase == PH_SEGMENT)
                rt->active[i].segment_interrupted = true;
        }
        return -1;
    }

    int idx = find_entry(rt, pc);
    if (idx < 0)
        return -1;
    for (; idx < rt->count && rt->table[idx].addr == pc; idx++) {
        const NativeEntry *e = &rt->table[idx];
        NativeStats *st = &rt->stats[idx];

        if (e->generated) {
            /* SHADOW checks generated blocks by replaying the record */
            if (rt->mode != NATIVE_ON || !entry_applies(e, s))
                continue;
            st->calls++;
            s->bus.clocks = 0;
            s->cpu.io_cycles = 0;
            running_direct = e;
            e->fn(s);
            running_direct = NULL;
            uint32_t clocks = s->bus.clocks + 6 * s->cpu.io_cycles;
            s->cpu.io_cycles = 0;
            return (int32_t)clocks;
        }

        st->calls++;
        if (!entry_applies(e, s)) {
            st->skipped++;
            continue;
        }

        if (e->idle) {
            if (rt->mode != NATIVE_ON) {
                st->skipped++;
                continue;
            }
            running_direct = e;
            e->fn(s);
            running_direct = NULL;
            if (cpu_pc24(&s->cpu) == pc)
                return (int32_t)(e->idle_clocks ? e->idle_clocks : snes_clocks_to_next_poll_change(s));
            return 0;
        }

        if (rt->nactive == MAX_ACTIVE) {
            st->skipped++;
            continue;
        }
        Active *a = &rt->active[rt->nactive++];
        memset(a, 0, sizeof *a);
        a->entry = e;
        a->entry_s = s->cpu.s;
        a->job = job_get();
        a->job->entry = e;
        if (rt->mode == NATIVE_SHADOW) {
            a->clone = malloc(sizeof *a->clone);
            snes_clone(a->clone, s);
            a->clone->bus.clocks = 0;
            a->job->m = a->clone;
        } else {
            s->bus.clocks = 0;
            a->job->m = s;
        }
        job_run(a->job);
        after_run(rt, a, s);
        return rt->mode == NATIVE_ON ? 0 : -1;
    }
    return -1;
}

unsigned long native_call_step_limit = 50000000ul;
unsigned long native_call_steps;
unsigned long native_call_timeouts;
void (*native_call_timeout_hook)(void);

void native_call_dc(Snes *s, uint32_t site, uint32_t target, bool long_call, uint16_t dont_care)
{
    uint16_t s_after = s->cpu.s;
    uint32_t ret;
    if (long_call) {
        push8_raw(s, site >> 16);
        push16_raw(s, (uint16_t)(site + 3));
        ret = (site & 0xFF0000) | (uint16_t)(site + 4);
    } else {
        push16_raw(s, (uint16_t)(site + 2));
        ret = (site & 0xFF0000) | (uint16_t)(site + 3);
    }
    s->cpu.pb = target >> 16;
    s->cpu.pc = target & 0xFFFF;

    Job *j = current_job;
    if (j && j->m == s) {
        j->state = JOB_CALLING;
        j->call_target = target;
        j->call_ret = ret;
        j->call_s_after = s_after;
        j->call_dont_care = dont_care;
        fiber_switch(fiber_main());
        return;
    }

    /* Not inside the runtime (tests, tools): interpret the callee right here,
     * interrupts held off and no time passing. */
    bool nmi = s->cpu.nmi_pending, irq = s->cpu.irq_line;
    s->cpu.nmi_pending = false;
    s->cpu.irq_line = false;
    while (!(cpu_pc24(&s->cpu) == ret && s->cpu.s == s_after)) {
        if (native_call_steps >= native_call_step_limit) {
            native_call_timeouts++;
            if (native_call_timeout_hook)
                native_call_timeout_hook();
            else if (native_call_timeouts == 1)
                fprintf(stderr, "native_call: %06X called from %06X did not return\n", target, site);
            break;
        }
        const NativeRuntime *rt = active_runtime;
        uint32_t pc = cpu_pc24(&s->cpu);
        int idx = rt ? find_entry(rt, pc) : -1;
        while (idx >= 0 && idx < rt->count && rt->table[idx].addr == pc &&
               (rt->table[idx].idle || !entry_applies(&rt->table[idx], s)))
            idx++;
        if (idx >= 0 && idx < rt->count && rt->table[idx].addr == pc)
            rt->table[idx].fn(s);
        else
            cpu_step(&s->cpu);
        native_call_steps++;
    }
    s->cpu.nmi_pending = nmi;
    s->cpu.irq_line = irq;
}

void native_sync(Snes *s)
{
    Job *j = current_job;
    if (!j || j->m != s || !active_runtime || active_runtime->mode != NATIVE_ON || !s->bus.clocks)
        return;
    j->state = JOB_SYNCING;
    fiber_switch(fiber_main());
}

void native_call(Snes *s, uint32_t site, uint32_t target, bool long_call)
{
    native_call_dc(s, site, target, long_call, 0);
}

void native_attach(NativeRuntime *rt, Snes *snes)
{
    if (rt->mode == NATIVE_OFF)
        return;
    snes->intercept = intercept;
    snes->intercept_ctx = rt;
    rt->attached = snes;
    if (rt->mode == NATIVE_SHADOW && rt->generated && !rt->acc) {
        rt->cap_acc = RECORD_ACCESSES;
        rt->cap_bnd = RECORD_BOUNDARIES;
        rt->acc = malloc(sizeof *rt->acc * rt->cap_acc);
        rt->bnd = malloc(sizeof *rt->bnd * rt->cap_bnd);
        rt->replay_machine = calloc(1, sizeof *rt->replay_machine);
        rt->attached_bus = snes->cpu.bus;
        snes->cpu.bus.read = record_read;
        snes->cpu.bus.write = record_write;
    }
}

/* ---- reporting ---------------------------------------------------------------- */

/* Check what the record holds so far. */
static void replay_pending(const NativeRuntime *crt, bool final)
{
    NativeRuntime *rt = (NativeRuntime *)crt;
    if (rt->acc && rt->nbnd)
        replay_record(rt, final);
}

void native_report(const NativeRuntime *rt, FILE *out)
{
    static const char *modes[] = { "off", "shadow", "on" };
    replay_pending(rt, true);
    fprintf(out, "native routines (%s mode):\n", modes[rt->mode]);
    unsigned long gen_calls = 0, gen_matched = 0, gen_mismatched = 0, gen_interrupted = 0, gen_reached = 0;
    for (int i = 0; i < rt->count; i++) {
        const NativeEntry *e = &rt->table[i];
        const NativeStats *st = &rt->stats[i];
        if (e->generated) {
            gen_calls += st->calls;
            gen_matched += st->matched;
            gen_mismatched += st->mismatched;
            gen_interrupted += st->interrupted;
            gen_reached += st->calls > 0;
            continue;
        }
        fprintf(out, "  %06X %-40s calls %u", e->addr, e->name, st->calls);
        if (rt->mode == NATIVE_SHADOW) {
            fprintf(out, "  matched %u  mismatched %u  interrupted %u", st->matched, st->mismatched, st->interrupted);
            if (st->native_bus_clocks)
                fprintf(out, "  original/native-bus %.3f", (double)st->original_clocks / st->native_bus_clocks);
        }
        if (st->abandoned)
            fprintf(out, "  abandoned %u", st->abandoned);
        if (st->skipped)
            fprintf(out, "  skipped %u", st->skipped);
        fprintf(out, "\n");
    }
    if (rt->generated) {
        fprintf(out, "  generated blocks: %d entries, %lu reached, calls %lu", rt->generated, gen_reached, gen_calls);
        if (rt->mode == NATIVE_SHADOW)
            fprintf(out, "  matched %lu  mismatched %lu  interrupted %lu  unfinished %lu", gen_matched, gen_mismatched,
                    gen_interrupted, rt->gen_unfinished);
        else
            fprintf(out, "  instructions %llu", (unsigned long long)recomp_executed);
        fprintf(out, "\n");
    }
    if (rt->nactive)
        fprintf(out, "  %d routine(s) still in progress\n", rt->nactive);
    for (int i = 0; i < rt->ndetails; i++)
        fprintf(out, "  %s\n", rt->details[i]);
}

int native_failed(const NativeRuntime *rt)
{
    replay_pending(rt, false);
    for (int i = 0; i < rt->count; i++)
        if (rt->stats[i].mismatched)
            return 1;
    return 0;
}
