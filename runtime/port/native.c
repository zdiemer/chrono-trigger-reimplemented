#include "native.h"

#include <stdlib.h>
#include <string.h>

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

#define MAX_PENDING 32
#define MAX_DETAILS 16

typedef struct Pending {
    const NativeEntry *entry;
    Snes *clone;           /* machine after the native routine ran */
    uint16_t ret_s;        /* S and PC the original must return with */
    uint32_t ret_pc;
    uint64_t master_at_entry;
    uint32_t native_bus_clocks;
    bool interrupted;
} Pending;

struct NativeRuntime {
    const NativeEntry *table;
    int count;
    NativeMode mode;
    NativeStats *stats;
    uint8_t filter[0x2000];      /* bit per 16-bit address: some entry might be here */
    Pending pending[MAX_PENDING];
    int npending;
    uint32_t debt;               /* ON mode: master clocks a native routine still owes */
    char details[MAX_DETAILS][256];
    int ndetails;
};

static const NativeRuntime *active_runtime;   /* for native_call */

/* ---- registry --------------------------------------------------------------- */

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

void native_destroy(NativeRuntime *rt)
{
    if (!rt)
        return;
    if (active_runtime == rt)
        active_runtime = NULL;
    for (int i = 0; i < rt->npending; i++)
        free(rt->pending[i].clone);
    free((void *)rt->table);
    free(rt->stats);
    free(rt);
}

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

/* ---- comparison --------------------------------------------------------------- */

static void detail(NativeRuntime *rt, const char *fmt_prefix, const NativeEntry *e, const char *what)
{
    if (rt->ndetails >= MAX_DETAILS)
        return;
    snprintf(rt->details[rt->ndetails++], sizeof rt->details[0], "%s %06X %s: %s", fmt_prefix, e->addr, e->name,
             what);
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
static bool machines_differ(const Snes *o, const Snes *c, uint16_t dont_care, char *out, size_t n)
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

static void finish_pending(NativeRuntime *rt, Snes *s, bool returned_normally)
{
    Pending *p = &rt->pending[--rt->npending];
    NativeStats *st = &rt->stats[p->entry - rt->table];
    if (p->interrupted) {
        st->interrupted++;
    } else if (!returned_normally) {
        st->mismatched++;
        detail(rt, "mismatch", p->entry, "original left the routine without returning where the native one did");
    } else {
        char why[200];
        if (machines_differ(s, p->clone, p->entry->dont_care, why, sizeof why)) {
            st->mismatched++;
            detail(rt, "mismatch", p->entry, why);
        } else {
            st->matched++;
            uint64_t original = s->master - p->master_at_entry;
            st->original_clocks += original;
            st->native_bus_clocks += p->native_bus_clocks;
            if (p->native_bus_clocks) {
                double r = (double)original / p->native_bus_clocks;
                if (st->matched == 1 || r < st->min_ratio) st->min_ratio = r;
                if (st->matched == 1 || r > st->max_ratio) st->max_ratio = r;
            }
        }
    }
    free(p->clone);
}

static int32_t intercept(void *ctx, Snes *s)
{
    NativeRuntime *rt = ctx;
    uint32_t pc = cpu_pc24(&s->cpu);

    /* A native routine finishes instantly but the original took time. Pay that
     * time off a scanline at a time so frame boundaries and line events still
     * happen while the routine "runs". */
    if (rt->debt > 0) {
        uint32_t spend = rt->debt < SNES_CLOCKS_PER_LINE ? rt->debt : SNES_CLOCKS_PER_LINE;
        rt->debt -= spend;
        return (int32_t)spend;
    }

    /* Returns from routines under shadow validation. */
    while (rt->npending > 0) {
        Pending *top = &rt->pending[rt->npending - 1];
        if (s->cpu.s == top->ret_s && pc == top->ret_pc)
            finish_pending(rt, s, true);
        else if (s->cpu.s > top->ret_s)
            finish_pending(rt, s, false);
        else
            break;
    }

    if (interrupt_deliverable(s)) {
        for (int i = 0; i < rt->npending; i++)
            rt->pending[i].interrupted = true;
        return -1;
    }

    int idx = find_entry(rt, pc);
    if (idx < 0)
        return -1;
    const NativeEntry *e = &rt->table[idx];
    NativeStats *st = &rt->stats[idx];
    st->calls++;
    if ((s->cpu.p & e->p_mask) != e->p_value || s->cpu.e) {
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
            return (int32_t)(SNES_CLOCKS_PER_LINE - s->hclock);
        return 0;
    }

    if (rt->mode == NATIVE_ON) {
        uint32_t before = s->bus.clocks;
        e->fn(s);
        uint64_t bus = s->bus.clocks - before;
        s->bus.clocks = before;
        rt->debt = (uint32_t)(bus * e->cost_milli / 1000);
        return 0;
    }

    if (rt->npending == MAX_PENDING) {
        st->skipped++;
        return -1;
    }
    Pending *p = &rt->pending[rt->npending++];
    p->entry = e;
    p->clone = malloc(sizeof *p->clone);
    snes_clone(p->clone, s);
    p->clone->bus.clocks = 0;
    e->fn(p->clone);
    p->native_bus_clocks = p->clone->bus.clocks;
    p->ret_s = p->clone->cpu.s;
    p->ret_pc = cpu_pc24(&p->clone->cpu);
    p->master_at_entry = s->master;
    p->interrupted = false;
    return -1;
}

#define CALL_STEP_LIMIT 50000000u

void native_call(Snes *s, uint32_t site, uint32_t target, bool long_call)
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

    bool nmi = s->cpu.nmi_pending, irq = s->cpu.irq_line;
    s->cpu.nmi_pending = false;
    s->cpu.irq_line = false;
    unsigned steps = 0;
    while (!(cpu_pc24(&s->cpu) == ret && s->cpu.s == s_after)) {
        const NativeRuntime *rt = active_runtime;
        int idx = rt ? find_entry(rt, cpu_pc24(&s->cpu)) : -1;
        if (idx >= 0 && (s->cpu.p & rt->table[idx].p_mask) == rt->table[idx].p_value && !s->cpu.e)
            rt->table[idx].fn(s);
        else
            cpu_step(&s->cpu);
        if (++steps == CALL_STEP_LIMIT) {
            fprintf(stderr, "native_call: %06X called from %06X did not return\n", target, site);
            break;
        }
    }
    s->cpu.nmi_pending = nmi;
    s->cpu.irq_line = irq;
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
            if (st->matched)
                fprintf(out, "  avg original clocks %llu",
                        (unsigned long long)(st->original_clocks / st->matched));
            if (st->native_bus_clocks)
                fprintf(out, "  original/native-bus %.3f (%.3f-%.3f)",
                        (double)st->original_clocks / st->native_bus_clocks, st->min_ratio, st->max_ratio);
        }
        if (st->skipped)
            fprintf(out, "  skipped %u", st->skipped);
        fprintf(out, "\n");
    }
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
