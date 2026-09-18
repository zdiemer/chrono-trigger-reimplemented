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
#define RESUME_SLOTS 4096

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
    int64_t spent;         /* m_spend since the job last resumed (the timing spike) */
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
    j->spent = 0;
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
    Snes *clone;           /* SHADOW: the machine the native routine runs on (clones[level]) */
    uint64_t since;        /* SHADOW: the write epoch the clone was last refreshed at */
    uint16_t entry_s;
    uint16_t ret_s;        /* after the native routine returned: S and PC the original must match */
    uint32_t ret_pc;
    uint64_t segment_start;
    uint32_t segment_frame;      /* frame counter when the segment started */
    uint32_t segment_bus_clocks;
    int64_t segment_spent;       /* SHADOW: the port's m_spend for the segment */
    unsigned long segment_port_clears[2];   /* apu_port_clears when the segment started */
    uint64_t segment_horizon, segment_interrupt_horizon;   /* SHADOW: at the segment's start (timing spike) */
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
    uint64_t master;       /* the CPU's own time: master clocks minus its pauses (refresh, HDMA) */
    uint32_t acc;          /* accesses recorded before this step */
    uint32_t frame;
    int32_t gen;           /* the generated entry that applies here, decided while recording
                              where guards or emulation mode matter; GEN_LATER otherwise */
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
    /* SHADOW: the machine copies native routines run on, one per nesting level,
     * kept between calls so a refresh copies only what changed
     * (snes_clone_since), and the epoch each was last refreshed at (0: never). */
    Snes *clones[MAX_ACTIVE];
    uint64_t clone_since[MAX_ACTIVE];
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

    /* ON mode: where generated blocks stopped before an instruction, so that
     * code resumes there in the same block (direct-mapped by address and widths). */
    struct { uint32_t key; NativeFn fn; bool (*guard)(Snes *s); } resume[RESUME_SLOTS];
    unsigned long gen_stops, gen_resumes;
    /* ON mode: every instruction of every block (native_set_index), for
     * continuing in generated code where no entry or stop applies. */
    const RecompIndexEntry *index;
    int index_count;
    uint8_t index_filter[0x2000];   /* bit per 16-bit address: some index entry might be here */
    unsigned long gen_indexed;
    /* SHADOW: bit per 16-bit address with a generated entry that has a guard or
     * needs emulation mode, decided while recording (Boundary.gen). */
    uint8_t record_filter[0x2000];

    /* The timing spike (native_set_port_timing, native_set_timing_report). */
    PortTiming port_timing;
    bool port_horizon_interrupts;
    bool timing_report;
    unsigned long self_timed_io;              /* SELF: time-dependent I/O while a port runs */
    void (*prev_observer)(void *ctx, const SnesBus *bus, uint16_t addr, uint8_t value, unsigned flags);
    void *prev_observer_ctx;
    uint64_t step_master, prev_step_master;   /* SHADOW: master clock at the latest two steps */
    Snes *timing_copy;                        /* the machine as it was before a port ran, to put it back */
    uint64_t timing_copy_since;
    Snes *timing_scratch;                     /* ORACLE: the machine the generated code is timed on */
    uint64_t timing_scratch_since;
};

void native_set_port_timing(NativeRuntime *rt, PortTiming timing) { rt->port_timing = timing; }
void native_set_timing_report(NativeRuntime *rt, bool on) { rt->timing_report = on; }
void native_set_port_horizon(NativeRuntime *rt, bool interrupts_only) { rt->port_horizon_interrupts = interrupts_only; }

/* I/O accesses rc_io_sync saw that depend on or change the time (the timing
 * spike's oracle counts them while it times a call). */
static unsigned long rc_timed_io;

#define GEN_LATER (-2)
#define CLOCKS_PER_IO_CYCLE_NATIVE 6

static const NativeRuntime *active_runtime;   /* for synchronous native_call */

uint64_t recomp_executed;
RcTiming rc_timing = { .limit = UINT32_MAX };

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
        if (sorted[i].generated && (sorted[i].guard || sorted[i].emulation))
            rt->record_filter[a >> 3] |= (uint8_t)(1 << (a & 7));
    }
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
    a->clone = NULL;
}

/* SHADOW: bring the active routine's machine copy up to date with s. */
static void clone_refresh(NativeRuntime *rt, Active *a, Snes *s)
{
    int level = (int)(a - rt->active);
    if (!rt->clones[level])
        rt->clones[level] = malloc(sizeof *rt->clones[level]);
    a->clone = rt->clones[level];
    a->since = rt->clone_since[level] = snes_clone_since(a->clone, s, rt->clone_since[level]);
    a->clone->bus.clocks = 0;
    a->job->m = a->clone;
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
    for (int i = 0; i < MAX_ACTIVE; i++)
        free(rt->clones[i]);
    free(rt->acc);
    free(rt->bnd);
    free(rt->replay_machine);
    free(rt->timing_copy);
    free(rt->timing_scratch);
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

/* Entry preconditions: register widths/modes on P, native mode (emulation mode
 * for the generated entries made for it), and the guard. A generated block also
 * needs the CPU running (not halted by WAI or STP). */
static bool entry_applies(const NativeEntry *e, Snes *s)
{
    if ((s->cpu.p & e->p_mask) != e->p_value || s->cpu.e != e->emulation)
        return false;
    if (e->generated && (s->cpu.waiting || s->cpu.stopped))
        return false;
    return !e->guard || e->guard(s);
}

const NativeEntry *native_hand_entry(Snes *s)
{
    const NativeRuntime *rt = active_runtime;
    uint32_t pc = cpu_pc24(&s->cpu);
    int idx = rt ? find_entry(rt, pc) : -1;
    for (; idx >= 0 && idx < rt->count && rt->table[idx].addr == pc && !rt->table[idx].generated; idx++)
        if (entry_applies(&rt->table[idx], s))
            return &rt->table[idx];
    return NULL;
}

/* ---- comparison --------------------------------------------------------------- */

static void detail(NativeRuntime *rt, const char *prefix, const NativeEntry *e, const char *what)
{
    if (rt->ndetails >= MAX_DETAILS)
        return;
    snprintf(rt->details[rt->ndetails++], sizeof rt->details[0], "%s %06X %s: %s", prefix, e->addr, e->name, what);
}

static bool diff_bytes(char *out, size_t n, const char *name, const uint8_t *a, const uint8_t *b, size_t size,
                       size_t base)
{
    /* memcmp first: it is several times faster than a byte loop that stops at
     * the first difference, and this runs on every shadowed call. The loop
     * only runs to describe a difference. */
    if (!memcmp(a, b, size))
        return false;
    for (size_t i = 0; i < size; i++) {
        if (a[i] != b[i]) {
            snprintf(out, n, "%s[%04zX] original %02X native %02X", name, base + i, a[i], b[i]);
            return true;
        }
    }
    return false;
}

/* diff_bytes over the pages of a tracked memory (SNES_TRACK_PAGE_BITS) that
 * either machine wrote since `since`: the others are equal, as they were when
 * the copy was refreshed. */
static bool diff_pages(char *out, size_t n, const char *name, const uint8_t *a, const uint8_t *b, size_t size,
                       const uint64_t *a_epoch, const uint64_t *b_epoch, size_t count, uint64_t since)
{
    size_t page = size / count;
    for (size_t i = 0; i < count; i++)
        if ((a_epoch[i] >= since || b_epoch[i] >= since) &&
            diff_bytes(out, n, name, a + i * page, b + i * page, page, i * page))
            return true;
    return false;
}

#define CHECK_FIELD(label, x, y)                                                                         \
    if ((x) != (y)) {                                                                                    \
        snprintf(out, n, "%s original %lX native %lX", label, (unsigned long)(x), (unsigned long)(y));   \
        return true;                                                                                     \
    }

/* Returns true and describes the first difference if the machines differ in
 * anything CPU-side code can observe or change. Work RAM and VRAM are compared
 * only in the pages written since write epoch `since` (0: all of them). */
static bool compare_machines(const Snes *o, const Snes *c, uint16_t dont_care, uint64_t since, char *out, size_t n)
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

    if (diff_pages(out, n, "WRAM", o->bus.wram, c->bus.wram, sizeof o->bus.wram, o->bus.wram_page_epoch,
                   c->bus.wram_page_epoch, SNES_WRAM_PAGES, since) ||
        diff_bytes(out, n, "SRAM", o->bus.sram, c->bus.sram, sizeof o->bus.sram, 0) ||
        diff_pages(out, n, "VRAM", o->ppu.vram, c->ppu.vram, sizeof o->ppu.vram, o->ppu.vram_page_epoch,
                   c->ppu.vram_page_epoch, sizeof o->ppu.vram_page_epoch / sizeof o->ppu.vram_page_epoch[0],
                   since) ||
        diff_bytes(out, n, "CGRAM", (const uint8_t *)o->ppu.cgram, (const uint8_t *)c->ppu.cgram,
                   sizeof o->ppu.cgram, 0) ||
        diff_bytes(out, n, "OAM", o->ppu.oam, c->ppu.oam, sizeof o->ppu.oam, 0))
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
    if (diff_bytes(out, n, "APU port (CPU to APU)", o->apu.cpu_in, c->apu.cpu_in, sizeof o->apu.cpu_in, 0))
        return true;
    return false;
}

bool native_compare(const Snes *o, const Snes *c, uint16_t dont_care, char *out, size_t n)
{
    return compare_machines(o, c, dont_care, 0, out, n);
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
    uint64_t original = s->master - s->stall_total - a->segment_start;   /* CPU time */
    st->original_clocks += original;
    st->native_bus_clocks += a->segment_bus_clocks;
    /* The timing spike: the estimate against the original, and whether the
     * stretch fit (its last instruction, the step before this one, started
     * before the horizon it had when it began). */
    uint64_t estimate = (uint64_t)a->segment_bus_clocks * a->entry->cost_milli / 1000;
    if (a->entry->exact_time && !a->segment_interrupted) {
        int64_t own = (int64_t)a->segment_bus_clocks + a->entry->time_extra + a->segment_spent;
        if (own != (int64_t)original) {
            if (!st->t_time_mismatched) {
                char msg[160];
                snprintf(msg, sizeof msg, "time original %llu port %lld (bus %u)", (unsigned long long)original,
                         (long long)own, a->segment_bus_clocks);
                detail(rt, "timing", a->entry, msg);
            }
            st->t_time_mismatched++;
        }
    }
    st->t_stretches++;
    st->t_estimate_exact += estimate == original;
    st->t_estimate_error += estimate > original ? estimate - original : original - estimate;
    if (!a->segment_interrupted && st->t_nextra <= 8) {
        uint32_t extra = (uint32_t)(original - a->segment_bus_clocks);
        unsigned k = 0;
        while (k < st->t_nextra && k < 8 && st->t_extra[k] != extra)
            k++;
        if (k == st->t_nextra) {
            if (k < 8)
                st->t_extra[k] = extra;
            st->t_nextra++;
        }
    }
    if (!a->segment_interrupted && rt->prev_step_master <= a->segment_horizon) {
        st->t_fit_horizon++;
        st->t_fit_horizon_clocks += original;
    }
    if (!a->segment_interrupted && rt->prev_step_master <= a->segment_interrupt_horizon) {
        st->t_fit_interrupt++;
        st->t_fit_interrupt_clocks += original;
    }
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
    if (compare_machines(s, a->clone, dont_care, a->since, why, sizeof why)) {
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
        a->segment_spent = j->spent;
        a->segment_start = s->master - s->stall_total;
        a->segment_frame = s->frame;
        a->segment_horizon = snes_horizon(s);
        a->segment_interrupt_horizon = snes_interrupt_horizon(s);
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
            clone_refresh(rt, a, s);
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

void native_replay_fetch_live(Cpu *c, uint32_t addr, unsigned n, unsigned start)
{
    Replay *r = c->bus.ctx;
    for (unsigned i = 0; i < n; i++) {
        uint32_t at = (addr & 0xFF0000) | (uint16_t)(addr + i);
        rc_timing.op[start + i] = 0;
        const Access *a = replay_next(r, c, at, i == 0);
        if (!a)
            return;
        if (a->write || a->addr != at) {
            char orig[48], why[160];
            describe_access(orig, sizeof orig, a);
            snprintf(why, sizeof why, "block fetched %06X where the original %s", at, orig);
            replay_fail(r, why);
            return;
        }
        rc_timing.op[start + i] = a->value;
        r->pos++;
        r->clocks += a->clocks;
    }
}

/* The first generated entry that applies to the machine as it is (widths,
 * emulation mode, guard), or -1. */
static int generated_applying(const NativeRuntime *rt, Snes *s)
{
    uint32_t pc = cpu_pc24(&s->cpu);
    int i = find_entry(rt, pc);
    if (i < 0)
        return -1;
    for (; i < rt->count && rt->table[i].addr == pc; i++) {
        const NativeEntry *e = &rt->table[i];
        if (e->generated && entry_applies(e, s))
            return i;
    }
    return -1;
}

static void record_boundary(NativeRuntime *rt, Snes *s)
{
    const Cpu *c = &s->cpu;
    uint16_t a16 = (uint16_t)c->pc;
    Boundary *b = &rt->bnd[rt->nbnd++];
    /* Guards look at RAM as it is now; decide those entries while recording. */
    b->gen = rt->record_filter[a16 >> 3] & (1 << (a16 & 7)) ? generated_applying(rt, s) : GEN_LATER;
    b->master = s->master - s->stall_total;
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
    if (b->gen != GEN_LATER)
        return b->gen;
    int i = find_entry(rt, b->pc24);
    if (i < 0)
        return -1;
    for (; i < rt->count && rt->table[i].addr == b->pc24; i++) {
        const NativeEntry *e = &rt->table[i];
        if (e->generated && (b->p & e->p_mask) == e->p_value && e->emulation == b->e && !e->guard)
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
        if (b->flags & (B_INTERRUPT | B_WAITING | B_STOPPED)) {
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
        c->db = b->db, c->p = b->p, c->e = b->e;
        c->pb = b->pc24 >> 16, c->pc = b->pc24 & 0xFFFF;
        c->io_cycles = 0;
        c->waiting = c->stopped = false;
        c->bus = (CpuBus){ r, replay_read, replay_write };
        rc_timing = (RcTiming){ .limit = UINT32_MAX };
        rt->table[idx].fn(m);
        rc_timing = (RcTiming){ .limit = UINT32_MAX };

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

/* ---- generated blocks in ON mode ------------------------------------------------
 *
 * A block runs against the system's horizon (snes_horizon): it stops before the
 * first instruction that would start past it, so everything the system does
 * between steps (line events, interrupt recognition, the frame's end) happens
 * between the same instructions as with the interpreter. I/O accesses first let
 * the machine catch up with the start of their instruction (rc_io_sync), which
 * is the time the interpreter's I/O sees. The step's time is what the block
 * spent beyond that, and its last cycle is the block's last instruction's. */

void rc_io_sync(uint32_t addr, bool write)
{
    uint16_t a = (uint16_t)addr;
    /* Registers nothing but CPU writes changes: the WRAM port and the math. */
    if ((a >= 0x2180 && a <= 0x2183) || (a >= 0x4202 && a <= 0x4206) || (a >= 0x4214 && a <= 0x4217))
        return;
    rc_timed_io++;
    uint32_t delta = rc_timing.insn_start - rc_timing.synced;
    if (delta) {
        snes_sync(rc_timing.s, delta);
        rc_timing.synced = rc_timing.insn_start;
    }
    /* NMITIMEN and the timer positions change when interrupts happen (and
     * enabling NMIs with RDNMI set requests one): let the system look again
     * after this instruction. */
    if (write && a >= 0x4200 && a <= 0x420A)
        rc_timing.limit = 0;
}

/* A RAM block wrote at `addr` (rc_snes_write): if that can change the block's
 * own code (its instructions' bytes, or anything through the WRAM port or a DMA
 * transfer), stop before the next instruction so the guard is checked again. */
void rc_code_write(uint32_t addr)
{
    uint8_t bank = (uint8_t)(addr >> 16);
    uint16_t a = (uint16_t)addr;
    uint32_t w;
    if (bank == 0x7E || bank == 0x7F)
        w = addr;
    else if (!(bank & 0x40) && a < 0x2000)
        w = 0x7E0000 | a;
    else {
        if (!(bank & 0x40) && (a == 0x2180 || a == 0x420B))
            rc_timing.limit = 0;
        return;
    }
    for (uint32_t i = 0; i < rc_timing.code_count; i++)
        if (w - rc_timing.code[2 * i] < rc_timing.code[2 * i + 1])
            rc_timing.limit = 0;
}

/* Address and widths, as recomp_index's keys: pc | P's M and X << 24 | E << 30
 * (emulation mode counts as 8-bit). */
static inline uint32_t resume_key(const Snes *s)
{
    if (s->cpu.e)
        return cpu_pc24(&s->cpu) | (uint32_t)(CPU_FLAG_M | CPU_FLAG_X) << 24 | 1u << 30;
    return cpu_pc24(&s->cpu) | (uint32_t)(s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) << 24;
}

static inline unsigned resume_slot(uint32_t key) { return (key * 2654435761u) >> 20; }

/* Stands in for the entry of a block resumed where it stopped, for attribution
 * (native_running_entry). */
static const NativeEntry resumed_block = { .name = "(resumed generated block)", .generated = true };

static int32_t run_block(NativeRuntime *rt, Snes *s, NativeFn fn, const NativeEntry *entry)
{
    uint64_t horizon = snes_horizon(s);
    uint64_t room = horizon > s->master ? horizon - s->master : 0;
    s->bus.clocks = 0;
    s->cpu.io_cycles = 0;
    rc_timing = (RcTiming){ .s = s, .limit = room < UINT32_MAX ? (uint32_t)room : UINT32_MAX - 1 };
    running_direct = entry;
    fn(s);
    running_direct = NULL;
    uint32_t clocks = s->bus.clocks + 6 * s->cpu.io_cycles;
    uint32_t synced = rc_timing.synced;
    NativeFn stopped = rc_timing.stopped;
    bool (*stopped_guard)(Snes *) = rc_timing.stopped_guard;
    rc_timing = (RcTiming){ .limit = UINT32_MAX };
    s->cpu.io_cycles = 0;
    if (clocks == 0)
        return -1;   /* not resumable here after all: let the interpreter run it */
    s->intercept_last_cycle = s->cpu.last_cycle_io ? 6 : s->bus.clocks - s->bus.access_start;
    if (stopped) {
        rt->gen_stops++;
        uint32_t key = resume_key(s);
        unsigned slot = resume_slot(key);
        rt->resume[slot].key = key | 1u << 31;
        rt->resume[slot].fn = stopped;
        rt->resume[slot].guard = stopped_guard;
    }
    return (int32_t)(clocks - synced);
}

static const NativeEntry indexed_block = { .name = "(generated block at an instruction)", .generated = true };

void native_set_index(NativeRuntime *rt, const RecompIndexEntry *index, int count)
{
    rt->index = index;
    rt->index_count = count;
    memset(rt->index_filter, 0, sizeof rt->index_filter);
    for (int i = 0; i < count; i++) {
        uint16_t a = (uint16_t)index[i].key;
        rt->index_filter[a >> 3] |= (uint8_t)(1 << (a & 7));
    }
}

/* ON mode, where no entry applies: continue in generated code if a block stopped
 * here, or else if any block has the instruction (for code reached through a
 * computed jump, or where an interrupt handler took the stop's slot). */
static int32_t continue_generated(NativeRuntime *rt, Snes *s)
{
    if (s->cpu.waiting || s->cpu.stopped)
        return -1;
    uint32_t key = resume_key(s);
    unsigned slot = resume_slot(key);
    if (rt->gen_stops && rt->resume[slot].key == (key | 1u << 31) &&
        (!rt->resume[slot].guard || rt->resume[slot].guard(s))) {
        rt->gen_resumes++;
        return run_block(rt, s, rt->resume[slot].fn, &resumed_block);
    }
    uint16_t a16 = (uint16_t)key;
    if (!(rt->index_filter[a16 >> 3] & (1 << (a16 & 7))))
        return -1;
    int lo = 0, hi = rt->index_count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (rt->index[mid].key < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    for (; lo < rt->index_count && rt->index[lo].key == key; lo++) {
        const RecompIndexEntry *x = &rt->index[lo];
        if (x->guard && !x->guard(s))
            continue;
        rt->gen_indexed++;
        return run_block(rt, s, x->fn, &indexed_block);
    }
    return -1;
}

static void observe_timed_io(void *ctx, const SnesBus *bus, uint16_t a, uint8_t v, unsigned flags)
{
    NativeRuntime *rt = ctx;
    if (!((a >= 0x2180 && a <= 0x2183) || (a >= 0x4202 && a <= 0x4206) || (a >= 0x4214 && a <= 0x4217)))
        rt->self_timed_io++;
    if (rt->prev_observer)
        rt->prev_observer(rt->prev_observer_ctx, bus, a, v, flags);
}

/* Put s back as it was when `copy` was refreshed from it (write epoch `since`),
 * keeping s's hooks. */
static void restore_machine(Snes *s, Snes *copy, uint64_t since)
{
    uint8_t *wram_written = s->bus.wram_written;
    void (*cpu_on_insn)(void *, uint32_t, uint8_t, bool) = s->cpu.on_insn;
    void (*apu_on_insn)(void *, const Apu *) = s->apu.on_insn;
    void (*on_line)(void *, Snes *, uint16_t) = s->on_line;
    void (*on_frame_top)(void *, Snes *) = s->on_frame_top;
    int32_t (*icpt)(void *, Snes *) = s->intercept;
    bool (*on_miss)(void *, Snes *) = s->on_miss;
    snes_clone_since(s, copy, since);
    s->bus.wram_written = wram_written;
    s->cpu.on_insn = cpu_on_insn;
    s->apu.on_insn = apu_on_insn;
    s->on_line = on_line;
    s->on_frame_top = on_frame_top;
    s->intercept = icpt;
    s->on_miss = on_miss;
}

/* Master clocks `cpu_clocks` of a port's time may take from now: exactly that
 * against snes_horizon, which no pause lies before; against the interrupt
 * horizon, a bound with the pauses of the lines it crosses. */
static uint64_t span_of(const NativeRuntime *rt, const Snes *s, uint64_t cpu_clocks)
{
    return rt->port_horizon_interrupts ? snes_span_bound(s, cpu_clocks) : cpu_clocks;
}

/* ---- the timing spike: exact time for a hand port (PORT_TIMING_ORACLE) -------------
 *
 * A prototype, to test whether a port can be exact when given the original's
 * exact time (docs/RECOMP.md, "Can a hand port be exact?"): the port runs on
 * the machine, then the generated code for the call runs on a copy of the
 * machine as it was, to where the port stopped, which gives the exact time,
 * the last cycle and whether it did anything that depends on the time. Only if
 * it did nothing of the kind and all of it fits before the horizon (so nothing
 * the system does falls inside it) is the port's call kept, charged that time;
 * otherwise the machine is put back and the generated block, exact by itself,
 * runs instead. The copy's end state doubles as a check of the port. */

/* The generated code that runs at the machine's PC, or NULL. */
static NativeFn generated_at(const NativeRuntime *rt, Snes *s)
{
    int idx = generated_applying(rt, s);
    if (idx >= 0)
        return rt->table[idx].fn;
    uint32_t key = resume_key(s);
    uint16_t a16 = (uint16_t)key;
    if (!(rt->index_filter[a16 >> 3] & (1 << (a16 & 7))))
        return NULL;
    int lo = 0, hi = rt->index_count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (rt->index[mid].key < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    for (; lo < rt->index_count && rt->index[lo].key == key; lo++)
        if (!rt->index[lo].guard || rt->index[lo].guard(s))
            return rt->index[lo].fn;
    return NULL;
}

typedef struct OracleTime {
    uint64_t clocks;        /* CPU time to the return */
    uint64_t last_start;    /* when its last instruction started */
    uint32_t last_cycle;
    bool ok;                /* timed, with nothing time-dependent inside */
    bool untimed;           /* no generated code for part of it */
} OracleTime;

/* Run generated code on m from its PC until it reaches end_pc with S at end_s
 * (where the port stopped). */
static OracleTime oracle_time(const NativeRuntime *rt, Snes *m, uint32_t end_pc, uint16_t end_s, uint64_t room)
{
    OracleTime o = { .last_cycle = CLOCKS_PER_IO_CYCLE_NATIVE, .ok = true };
    unsigned long io_before = rc_timed_io;
    NativeFn next = NULL;
    for (int n = 0; o.ok; n++) {
        NativeFn fn = next ? next : generated_at(rt, m);
        if (!fn || n > 1000000 || m->cpu.waiting || m->cpu.stopped) {
            o.ok = false;
            o.untimed = true;
            break;
        }
        m->bus.clocks = 0;
        m->cpu.io_cycles = 0;
        rc_timing = (RcTiming){ .s = m, .limit = UINT32_MAX - 1 };
        fn(m);
        uint32_t t = m->bus.clocks + CLOCKS_PER_IO_CYCLE_NATIVE * m->cpu.io_cycles;
        o.last_start = o.clocks + rc_timing.insn_start;
        next = rc_timing.stopped;
        if (rc_timing.limit == 0)
            o.ok = false;   /* it made an interrupt deliverable, or wrote code */
        rc_timing = (RcTiming){ .limit = UINT32_MAX };
        m->cpu.io_cycles = 0;
        if (!t) {
            o.ok = false;
            break;
        }
        o.clocks += t;
        if (o.clocks > room + 0x10000) {
            o.last_start = o.clocks;   /* far past the room: it can't fit, wherever it ends */
            break;
        }
        o.last_cycle = m->cpu.last_cycle_io ? CLOCKS_PER_IO_CYCLE_NATIVE : m->bus.clocks - m->bus.access_start;
        if (cpu_pc24(&m->cpu) == end_pc && m->cpu.s == end_s)
            break;
        if (m->cpu.s > end_s) {
            o.ok = false;   /* the original went elsewhere */
            o.untimed = true;
        }
    }
    if (rc_timed_io != io_before)
        o.ok = false;
    return o;
}

/* ON, PORT_TIMING_ORACLE: run hand entry e exactly if it can be. Returns false
 * (with the machine put back) to leave the call to the generated block. */
static bool oracle_run(NativeRuntime *rt, const NativeEntry *e, NativeStats *st, Snes *s, int32_t *result)
{
    if (!rt->timing_copy)
        rt->timing_copy = malloc(sizeof *rt->timing_copy);
    if (!rt->timing_scratch)
        rt->timing_scratch = malloc(sizeof *rt->timing_scratch);
    uint64_t backup_since = rt->timing_copy_since = snes_clone_since(rt->timing_copy, s, rt->timing_copy_since);
    Snes *m = rt->timing_scratch;
    uint64_t since = rt->timing_scratch_since = snes_clone_since(m, s, rt->timing_scratch_since);
    uint64_t horizon = rt->port_horizon_interrupts ? snes_interrupt_horizon(s) : snes_horizon(s);
    uint64_t room = horizon > s->master ? horizon - s->master : 0;

    Active *a = &rt->active[rt->nactive++];
    memset(a, 0, sizeof *a);
    a->entry = e;
    a->entry_s = s->cpu.s;
    a->job = job_get();
    a->job->entry = e;
    a->job->m = s;
    s->bus.clocks = 0;
    job_run(a->job);
    bool escaped = a->job->state != JOB_DONE;
    if (escaped) {
        /* it called out or synced: the prototype only runs whole calls */
        active_pop(rt);
    } else {
        job_put(a->job);
        a->job = NULL;
        rt->nactive--;
    }
    OracleTime o = { 0 };
    if (!escaped)
        o = oracle_time(rt, m, cpu_pc24(&s->cpu), s->cpu.s, room);
    if (escaped || !o.ok || span_of(rt, s, o.last_start) > room || o.clocks > INT32_MAX) {
        restore_machine(s, rt->timing_copy, backup_since);
        rt->timing_copy_since = rt->timing_scratch_since = 0;
        st->t_fallback++;
        if (escaped)
            st->t_escapes++;
        else if (o.untimed)
            st->t_untimed++;
        else if (!o.ok)
            st->t_timed_io++;
        else
            st->t_no_fit++;
        return false;
    }

    /* The port's registers must be the original's: an interrupt right after
     * the return pushes them. Registers the callers don't read (dont_care) may
     * differ; count those and take the original's. */
    Cpu *c = &s->cpu;
    const Cpu *oc = &m->cpu;
    if (c->a != oc->a || c->x != oc->x || c->y != oc->y || c->p != oc->p || c->d != oc->d || c->db != oc->db) {
        st->t_register_fixups++;
        c->a = oc->a, c->x = oc->x, c->y = oc->y, c->p = oc->p, c->d = oc->d, c->db = oc->db;
    }
    s->bus.open_bus = m->bus.open_bus;
    char why[200];
    if (compare_machines(m, s, 0, since, why, sizeof why)) {
        if (!st->t_state_diffs) {
            char msg[256];
            snprintf(msg, sizeof msg, "port state differs from the timed original: %s", why);
            detail(rt, "timing", e, msg);
        }
        st->t_state_diffs++;
    }
    st->t_exact++;
    s->bus.clocks = 0;
    s->intercept_last_cycle = o.last_cycle;
    *result = (int32_t)o.clocks;
    return true;
}

/* ON, PORT_TIMING_SELF: run exact_time entry e and charge its own time if that
 * is exact; otherwise put the machine back and return false (the generated
 * block runs). */
static bool self_run(NativeRuntime *rt, const NativeEntry *e, NativeStats *st, Snes *s, int32_t *result)
{
    if (!rt->timing_copy)
        rt->timing_copy = malloc(sizeof *rt->timing_copy);
    Snes *m = rt->timing_copy;
    uint64_t since = rt->timing_copy_since = snes_clone_since(m, s, rt->timing_copy_since);
    uint64_t horizon = rt->port_horizon_interrupts ? snes_interrupt_horizon(s) : snes_horizon(s);
    uint64_t room = horizon > s->master ? horizon - s->master : 0;
    uint16_t entry_s = s->cpu.s;

    Active *a = &rt->active[rt->nactive++];
    memset(a, 0, sizeof *a);
    a->entry = e;
    a->entry_s = entry_s;
    a->job = job_get();
    a->job->entry = e;
    a->job->m = s;
    s->bus.clocks = 0;
    rt->self_timed_io = 0;
    rt->prev_observer = snes_bus_io_observer;
    rt->prev_observer_ctx = snes_bus_io_observer_ctx;
    snes_bus_io_observer = observe_timed_io;
    snes_bus_io_observer_ctx = rt;
    job_run(a->job);
    snes_bus_io_observer = rt->prev_observer;
    snes_bus_io_observer_ctx = rt->prev_observer_ctx;
    Job *j = a->job;
    if (j->state != JOB_DONE) {
        /* it called out or synced: the prototype only runs whole calls */
        active_pop(rt);
        restore_machine(s, m, since);
        rt->timing_copy_since = 0;
        st->t_escapes++;
        st->t_fallback++;
        return false;
    }
    int64_t own = (int64_t)s->bus.clocks + e->time_extra + j->spent;
    /* The last cycle: RTS ends with an internal cycle, RTL with its last pull. */
    uint32_t last_cycle = (uint16_t)(s->cpu.s - entry_s) == 3 ? s->bus.clocks - s->bus.access_start
                                                               : CLOCKS_PER_IO_CYCLE_NATIVE;
    job_put(j);
    a->job = NULL;
    rt->nactive--;
    if (rt->self_timed_io || own <= 0 || span_of(rt, s, (uint64_t)own) > room || own > INT32_MAX) {
        restore_machine(s, m, since);
        rt->timing_copy_since = 0;
        st->t_fallback++;
        if (rt->self_timed_io)
            st->t_timed_io++;
        else
            st->t_no_fit++;
        return false;
    }
    st->t_exact++;
    s->bus.clocks = 0;
    s->intercept_last_cycle = last_cycle;
    *result = (int32_t)own;
    return true;
}

static int32_t intercept(void *ctx, Snes *s)
{
    NativeRuntime *rt = ctx;
    uint32_t pc = cpu_pc24(&s->cpu);

    if (rt->acc)
        record_step(rt, s);
    rt->prev_step_master = rt->step_master;
    rt->step_master = s->master;

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
    for (; idx >= 0 && idx < rt->count && rt->table[idx].addr == pc; idx++) {
        const NativeEntry *e = &rt->table[idx];
        NativeStats *st = &rt->stats[idx];

        if (e->generated) {
            /* SHADOW checks generated blocks by replaying the record */
            if (rt->mode != NATIVE_ON || !entry_applies(e, s))
                continue;
            st->calls++;
            return run_block(rt, s, e->fn, e);
        }

        st->calls++;
        if (!entry_applies(e, s)) {
            st->skipped++;
            continue;
        }

        if (rt->mode == NATIVE_ON && rt->port_timing != PORT_TIMING_ESTIMATE && e->idle)
            continue;   /* the timing spike's modes run only whole routines */
        if (rt->mode == NATIVE_ON && rt->port_timing == PORT_TIMING_SELF) {
            int32_t result;
            if (e->exact_time && rt->nactive < MAX_ACTIVE && self_run(rt, e, st, s, &result))
                return result;
            continue;   /* the generated block at the same address runs */
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
        if (rt->mode == NATIVE_ON && rt->port_timing == PORT_TIMING_ORACLE) {
            int32_t result;
            if (oracle_run(rt, e, st, s, &result))
                return result;
            continue;   /* the generated block at the same address runs */
        }
        Active *a = &rt->active[rt->nactive++];
        memset(a, 0, sizeof *a);
        a->entry = e;
        a->entry_s = s->cpu.s;
        a->job = job_get();
        a->job->entry = e;
        if (rt->mode == NATIVE_SHADOW) {
            clone_refresh(rt, a, s);
        } else {
            s->bus.clocks = 0;
            a->job->m = s;
        }
        job_run(a->job);
        after_run(rt, a, s);
        return rt->mode == NATIVE_ON ? 0 : -1;
    }
    return rt->mode == NATIVE_ON && rt->generated ? continue_generated(rt, s) : -1;
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
        if (idx >= 0 && idx < rt->count && rt->table[idx].addr == pc) {
            rt->table[idx].fn(s);
            if (rt->table[idx].generated)
                rc_timing = (RcTiming){ .limit = UINT32_MAX };
        } else if (s->interpreter) {
            s->interpreter(&s->cpu);
        } else {
            /* Without the interpreter (a player built without it runs hand ports
             * only inside the runtime, so this is a host's mistake): give up. */
            native_call_timeouts++;
            fprintf(stderr, "native_call: %06X called from %06X reached %06X, which only the interpreter runs\n",
                    target, site, pc);
            break;
        }
        native_call_steps++;
    }
    s->cpu.nmi_pending = nmi;
    s->cpu.irq_line = irq;
}

void native_spend(Snes *s, int32_t clocks)
{
    Job *j = current_job;
    if (j && j->m == s)
        j->spent += clocks;
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
    for (int i = 0; i < MAX_ACTIVE; i++)
        rt->clone_since[i] = 0;   /* copies of another machine, if any */
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
        if (rt->timing_report && st->t_stretches) {
            fprintf(out, "    timing: stretches %u  estimate exact %u  mean |error| %.1f  of %.1f"
                         "  fit horizon %u (%.1f%% of time)  fit interrupts %u (%.1f%% of time)\n",
                    st->t_stretches, st->t_estimate_exact, (double)st->t_estimate_error / st->t_stretches,
                    (double)st->original_clocks / st->t_stretches, st->t_fit_horizon,
                    st->original_clocks ? 100.0 * st->t_fit_horizon_clocks / st->original_clocks : 0.0,
                    st->t_fit_interrupt,
                    st->original_clocks ? 100.0 * st->t_fit_interrupt_clocks / st->original_clocks : 0.0);
            fprintf(out, "    timing: unseen time values %s%u:", st->t_nextra > 8 ? ">" : "",
                    st->t_nextra > 8 ? 8 : st->t_nextra);
            for (unsigned k = 0; k < st->t_nextra && k < 8; k++)
                fprintf(out, " %u", st->t_extra[k]);
            fprintf(out, "\n");
        }
        if (rt->timing_report && st->t_time_mismatched)
            fprintf(out, "    timing: own time mismatched %u\n", st->t_time_mismatched);
        if (rt->timing_report && (st->t_exact || st->t_fallback || st->t_escapes))
            fprintf(out, "    timing: exact %u  fallback %u (untimed %u, timed I/O %u, no room %u)  register fixups %u"
                         "  state diffs %u  escapes %u\n",
                    st->t_exact, st->t_fallback, st->t_untimed, st->t_timed_io, st->t_no_fit, st->t_register_fixups,
                    st->t_state_diffs, st->t_escapes);
    }
    if (rt->generated) {
        fprintf(out, "  generated blocks: %d entries, %lu reached, calls %lu", rt->generated, gen_reached, gen_calls);
        if (rt->mode == NATIVE_SHADOW)
            fprintf(out, "  matched %lu  mismatched %lu  interrupted %lu  unfinished %lu", gen_matched, gen_mismatched,
                    gen_interrupted, rt->gen_unfinished);
        else
            fprintf(out, "  instructions %llu  stops %lu  resumed %lu  indexed %lu", (unsigned long long)recomp_executed,
                    rt->gen_stops, rt->gen_resumes, rt->gen_indexed);
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
