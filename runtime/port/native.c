#include "native.h"

#include <stdlib.h>
#include <string.h>

#define MAX_PENDING 32
#define MAX_DETAILS 16

typedef struct Pending {
    const NativeEntry *entry;
    Snes *clone;           /* machine after the native routine ran */
    uint16_t ret_s;        /* S and PC the original must return with */
    uint32_t ret_pc;
    uint64_t master_at_entry;
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
    char details[MAX_DETAILS][256];
    int ndetails;
};

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
    return rt;
}

void native_destroy(NativeRuntime *rt)
{
    if (!rt)
        return;
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
static bool machines_differ(const Snes *o, const Snes *c, char *out, size_t n)
{
    CHECK_FIELD("A", o->cpu.a, c->cpu.a)
    CHECK_FIELD("X", o->cpu.x, c->cpu.x)
    CHECK_FIELD("Y", o->cpu.y, c->cpu.y)
    CHECK_FIELD("S", o->cpu.s, c->cpu.s)
    CHECK_FIELD("D", o->cpu.d, c->cpu.d)
    CHECK_FIELD("DB", o->cpu.db, c->cpu.db)
    CHECK_FIELD("PB", o->cpu.pb, c->cpu.pb)
    CHECK_FIELD("PC", o->cpu.pc, c->cpu.pc)
    CHECK_FIELD("P", o->cpu.p, c->cpu.p)
    CHECK_FIELD("E", o->cpu.e, c->cpu.e)

    if (diff_bytes(out, n, "WRAM", o->bus.wram, c->bus.wram, sizeof o->bus.wram) ||
        diff_bytes(out, n, "SRAM", o->bus.sram, c->bus.sram, sizeof o->bus.sram) ||
        diff_bytes(out, n, "VRAM", o->ppu.vram, c->ppu.vram, sizeof o->ppu.vram) ||
        diff_bytes(out, n, "CGRAM", (const uint8_t *)o->ppu.cgram, (const uint8_t *)c->ppu.cgram,
                   sizeof o->ppu.cgram) ||
        diff_bytes(out, n, "OAM", o->ppu.oam, c->ppu.oam, sizeof o->ppu.oam) ||
        diff_bytes(out, n, "PPU reg", o->ppu.reg, c->ppu.reg, sizeof o->ppu.reg))
        return true;

    const Ppu *po = &o->ppu, *pc = &c->ppu;
    CHECK_FIELD("VMADD", po->vram_addr, pc->vram_addr)
    CHECK_FIELD("VRAM prefetch", po->vram_prefetch, pc->vram_prefetch)
    CHECK_FIELD("CGADD", po->cg_addr, pc->cg_addr)
    CHECK_FIELD("CGRAM flip-flop", po->cg_second, pc->cg_second)
    CHECK_FIELD("CGRAM latch", po->cg_latch, pc->cg_latch)
    CHECK_FIELD("OAM reload", po->oam_reload, pc->oam_reload)
    CHECK_FIELD("OAMADD", po->oam_addr, pc->oam_addr)
    CHECK_FIELD("OAM latch", po->oam_latch, pc->oam_latch)
    CHECK_FIELD("BGnOFS latch", po->bgofs_latch, pc->bgofs_latch)
    CHECK_FIELD("BGnHOFS latch", po->bghofs_latch, pc->bghofs_latch)
    CHECK_FIELD("Mode 7 latch", po->m7_latch, pc->m7_latch)

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
    for (int i = 0; i < 8; i++) {
        if (diff_bytes(out, n, "DMA reg", bo->dma[i].reg, bc->dma[i].reg, sizeof bo->dma[i].reg))
            return true;
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
        if (machines_differ(s, p->clone, why, sizeof why)) {
            st->mismatched++;
            detail(rt, "mismatch", p->entry, why);
        } else {
            st->matched++;
            st->original_clocks += s->master - p->master_at_entry;
        }
    }
    free(p->clone);
}

static int32_t intercept(void *ctx, Snes *s)
{
    NativeRuntime *rt = ctx;
    uint32_t pc = cpu_pc24(&s->cpu);

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

    if (rt->mode == NATIVE_ON) {
        e->fn(s);
        return (int32_t)e->clocks;
    }

    if (rt->npending == MAX_PENDING) {
        st->skipped++;
        return -1;
    }
    Pending *p = &rt->pending[rt->npending++];
    p->entry = e;
    p->clone = malloc(sizeof *p->clone);
    snes_clone(p->clone, s);
    e->fn(p->clone);
    p->ret_s = p->clone->cpu.s;
    p->ret_pc = cpu_pc24(&p->clone->cpu);
    p->master_at_entry = s->master;
    p->interrupted = false;
    return -1;
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
