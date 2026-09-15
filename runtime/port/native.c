#include "native.h"

#include <stdlib.h>
#include <string.h>

#include "fiber.h"

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

typedef enum { JOB_RUNNING, JOB_CALLING, JOB_DONE } JobState;

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
    uint32_t segment_bus_clocks;
    unsigned long segment_port_clears[2];   /* apu_port_clears when the segment started */
    bool segment_interrupted, interrupted, mismatched;
} Active;

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
};

static const NativeRuntime *active_runtime;   /* for synchronous native_call */

static int entry_cmp(const void *a, const void *b)
{
    uint32_t x = ((const NativeEntry *)a)->addr, y = ((const NativeEntry *)b)->addr;
    return x < y ? -1 : x > y;
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
    free((void *)rt->table);
    free(rt->stats);
    free(rt);
}

int native_suspended(const NativeRuntime *rt) { return rt->nactive; }

static int find_entry(const NativeRuntime *rt, uint32_t pc)
{
    uint16_t a = pc & 0xFFFF;
    if (!(rt->filter[a >> 3] & (1 << (a & 7))))
        return -1;
    int lo = 0, hi = rt->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (rt->table[mid].addr == pc)
            return mid;
        if (rt->table[mid].addr < pc)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

/* Entry preconditions: register widths/modes on P, native mode, and the guard. */
static bool entry_applies(const NativeEntry *e, Snes *s)
{
    if ((s->cpu.p & e->p_mask) != e->p_value || s->cpu.e)
        return false;
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
    } else {
        a->segment_bus_clocks = m->bus.clocks;
        a->segment_start = s->master;
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

static int32_t intercept(void *ctx, Snes *s)
{
    NativeRuntime *rt = ctx;
    uint32_t pc = cpu_pc24(&s->cpu);

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
    const NativeEntry *e = &rt->table[idx];
    NativeStats *st = &rt->stats[idx];
    st->calls++;
    if (!entry_applies(e, s)) {
        st->skipped++;
        return -1;
    }

    if (e->idle) {
        if (rt->mode != NATIVE_ON) {
            st->skipped++;
            return -1;
        }
        e->fn(s);
        if (cpu_pc24(&s->cpu) == pc)
            return (int32_t)(e->idle_clocks ? e->idle_clocks : snes_clocks_to_next_poll_change(s));
        return 0;
    }

    if (rt->nactive == MAX_ACTIVE) {
        st->skipped++;
        return -1;
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
        int idx = rt ? find_entry(rt, cpu_pc24(&s->cpu)) : -1;
        if (idx >= 0 && !rt->table[idx].idle && entry_applies(&rt->table[idx], s))
            rt->table[idx].fn(s);
        else
            cpu_step(&s->cpu);
        native_call_steps++;
    }
    s->cpu.nmi_pending = nmi;
    s->cpu.irq_line = irq;
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
}

/* ---- reporting ---------------------------------------------------------------- */

void native_report(const NativeRuntime *rt, FILE *out)
{
    static const char *modes[] = { "off", "shadow", "on" };
    fprintf(out, "native routines (%s mode):\n", modes[rt->mode]);
    for (int i = 0; i < rt->count; i++) {
        const NativeEntry *e = &rt->table[i];
        const NativeStats *st = &rt->stats[i];
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
    if (rt->nactive)
        fprintf(out, "  %d routine(s) still in progress\n", rt->nactive);
    for (int i = 0; i < rt->ndetails; i++)
        fprintf(out, "  %s\n", rt->details[i]);
}

int native_failed(const NativeRuntime *rt)
{
    for (int i = 0; i < rt->count; i++)
        if (rt->stats[i].mismatched)
            return 1;
    return 0;
}
