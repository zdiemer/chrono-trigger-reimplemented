/* The differential fuzzer's core; see fuzz.h, and dev/ct_fuzz.c for what an
 * iteration does. */
#include "fuzz.h"

#include <stdlib.h>
#include <string.h>

#include "port/fiber.h"

/* ---- areas --------------------------------------------------------------------- */

static bool parse_hex(const char **p, uint32_t *v)
{
    char *end;
    unsigned long x = strtoul(*p, &end, 16);
    if (end == *p)
        return false;
    *v = (uint32_t)x;
    *p = end;
    return true;
}

bool fuzz_parse_area(const char *spec, bool keep, FuzzArea *out)
{
    memset(out, 0, sizeof *out);
    out->keep = keep;
    out->bank = -1;
    const char *p = spec;
    uint32_t v;
    if (p[0] && p[1] && p[2] == ':' && p[3] == '[') {   /* BB:[...] */
        if (!parse_hex(&p, &v) || v > 0xFF)
            return false;
        out->bank = (int)v;
        p++;
    }
    if (*p == '[') {
        p++;
        if ((p[0] == 'X' || p[0] == 'Y' || p[0] == 'D') && p[1] == ']') {
            out->base = p[0] == 'X' ? FUZZ_AT_X : p[0] == 'Y' ? FUZZ_AT_Y : FUZZ_AT_D;
            p += 2;
        } else {
            if (!parse_hex(&p, &out->pointer) || *p++ != ']')
                return false;
            out->base = FUZZ_AT_CELL;
        }
        if (*p == '+' || *p == '-') {
            bool minus = *p++ == '-';
            if (!parse_hex(&p, &v))
                return false;
            out->offset = minus ? -(int32_t)v : (int32_t)v;
        }
        if (*p++ != ',' || !parse_hex(&p, &out->count) || out->count == 0)
            return false;
    } else {
        out->base = FUZZ_AT_ADDRESS;
        if (!parse_hex(&p, &out->lo_addr) || *p++ != '-' || !parse_hex(&p, &out->hi_addr))
            return false;
    }
    if (keep)
        return *p == 0;
    uint32_t lo, hi;
    if (*p++ != '=' || !parse_hex(&p, &lo) || *p++ != '-' || !parse_hex(&p, &hi) || *p || lo > 0xFF || hi > 0xFF)
        return false;
    out->lo = (uint8_t)lo;
    out->hi = (uint8_t)hi;
    return true;
}

static uint64_t rng_state;

static uint64_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

/* The work RAM byte a 24-bit bus address reaches, or -1. */
static long wram_index(uint32_t addr)
{
    uint8_t bank = (uint8_t)(addr >> 16);
    uint16_t a = (uint16_t)addr;
    if (bank == 0x7E || bank == 0x7F)
        return (long)(addr - 0x7E0000);
    if (!(bank & 0x40) && a < 0x2000)
        return a;
    return -1;
}

static void set_byte(Snes *m, const Snes *base, const FuzzArea *f, uint32_t addr)
{
    long i = wram_index(addr);
    if (i < 0)
        return;   /* not work RAM: nothing to set */
    if (f->keep) {
        m->bus.wram[i] = base->bus.wram[i];
    } else {
        unsigned span = f->hi >= f->lo ? f->hi - f->lo + 1u : 1u;
        m->bus.wram[i] = (uint8_t)(f->lo + rng() % span);
    }
}

static void apply_area(Snes *m, const Snes *base, const FuzzArea *f)
{
    if (f->base == FUZZ_AT_ADDRESS) {
        for (uint32_t addr = f->lo_addr; addr <= f->hi_addr && addr <= 0xFFFFFF; addr++)
            set_byte(m, base, f, addr);
        return;
    }
    uint16_t pointer;
    uint32_t bank;
    if (f->base == FUZZ_AT_CELL) {
        long i = wram_index(f->pointer), j = wram_index((f->pointer & 0xFF0000) | (uint16_t)(f->pointer + 1));
        pointer = (uint16_t)((i < 0 ? 0 : m->bus.wram[i]) | (j < 0 ? 0 : m->bus.wram[j]) << 8);
        bank = f->pointer >> 16;
    } else {
        pointer = f->base == FUZZ_AT_X ? m->cpu.x : f->base == FUZZ_AT_Y ? m->cpu.y : m->cpu.d;
        bank = 0x7E;
    }
    if (f->bank >= 0)
        bank = (uint32_t)f->bank;
    for (uint32_t k = 0; k < f->count; k++)
        set_byte(m, base, f, bank << 16 | (uint16_t)(pointer + f->offset + (int32_t)k));
}

/* ---- the port's work, and stopping it --------------------------------------------- */

/* The native routine runs on its own fiber, so that it can be abandoned
 * wherever it is: in its own loop, or in a call-out's. */
static const NativeEntry *native_entry;
static Snes *native_machine;
static Fiber *native_fiber;

static unsigned long native_work, native_budget;
static bool in_callout;            /* the interpreter is running one of the port's call-outs */
static enum { RAN_OK, RAN_OUT_OWN, RAN_OUT_CALLOUT } native_ran;

static void native_fiber_main(void *arg)
{
    (void)arg;
    for (;;) {
        native_entry->fn(native_machine);
        fiber_switch(fiber_main());
    }
}

/* Every access the native machine makes: the port's and its call-outs'. */
static void native_access(SnesBus *bus)
{
    (void)bus;
    if (++native_work <= native_budget)
        return;
    native_ran = in_callout ? RAN_OUT_CALLOUT : RAN_OUT_OWN;
    fiber_switch(fiber_main());   /* abandoned: the fiber is destroyed, never resumed */
}

/* A step that makes no access (the CPU halted by WAI or STP in a call-out)
 * doesn't count against the budget, so native_call's own step limit, set to
 * the budget too, stops those. */
static void callout_timeout(void)
{
    native_ran = RAN_OUT_CALLOUT;
    fiber_switch(fiber_main());
}

static CpuStep callout_step(Cpu *cpu)
{
    in_callout = true;
    CpuStep r = cpu_step(cpu);
    in_callout = false;
    return r;
}

static unsigned long original_work;

static void original_access(SnesBus *bus)
{
    (void)bus;
    original_work++;
}

/* The most passes of the original through the point a port stopped at that
 * are compared (a loop's head; shadow mode's MAX_POINT_PASSES). */
#define FUZZ_MAX_POINT_PASSES 0x10000u

/* Run the interpreter until PC and S are back at the sentinel, noting the
 * lowest S on the way, for at most `budget` bus accesses. */
static bool run_until(Snes *s, uint32_t ret_pc, uint16_t ret_s, bool must_step, uint16_t *lowest_s,
                      unsigned long budget)
{
    *lowest_s = s->cpu.s;
    original_work = 0;
    for (unsigned long steps = 0; original_work < budget && steps < budget; steps++) {   /* a halted CPU makes no accesses */
        if (s->cpu.s < *lowest_s)
            *lowest_s = s->cpu.s;
        if (cpu_pc24(&s->cpu) == ret_pc && s->cpu.s == ret_s && (steps > 0 || !must_step))
            return true;
        s->cpu.nmi_pending = false;
        s->cpu.irq_line = false;
        cpu_step(&s->cpu);
    }
    return false;
}

/* ---- describing a mismatch in full ------------------------------------------------ */

#define MAX_RANGES 24
#define RANGE_GAP 8        /* equal bytes that still join two runs of differences */
#define RANGE_SHOWN 32     /* bytes of a range printed */

typedef struct Diff {
    FILE *out;
    unsigned ranges, differ;
} Diff;

/* Every run of differing bytes in one memory, grouped. */
static void diff_memory(Diff *d, const char *name, uint32_t base, int digits, const uint8_t *a, const uint8_t *b,
                        size_t size)
{
    size_t i = 0;
    while (i < size) {
        if (a[i] == b[i]) {
            i++;
            continue;
        }
        size_t start = i, end = i + 1, n = 0;
        for (size_t k = i; k < size && k < end + RANGE_GAP; k++) {
            if (a[k] != b[k]) {
                end = k + 1;
                n++;
            }
        }
        d->differ += (unsigned)n;
        if (d->ranges++ < MAX_RANGES) {
            size_t shown = end - start < RANGE_SHOWN ? end - start : RANGE_SHOWN;
            fprintf(d->out, "  %s %0*zX-%0*zX: %zu byte(s), %zu differ\n", name, digits, base + start, digits,
                    base + end - 1, end - start, n);
            fprintf(d->out, "      original");
            for (size_t k = start; k < start + shown; k++)
                fprintf(d->out, " %02X", a[k]);
            fprintf(d->out, "%s\n        native", shown < end - start ? " ..." : "");
            for (size_t k = start; k < start + shown; k++)
                fprintf(d->out, a[k] == b[k] ? "   " : " %02X", b[k]);
            fprintf(d->out, "\n");
        }
        i = end;
    }
}

static void diff_field(Diff *d, const char *name, unsigned long o, unsigned long n)
{
    if (o == n)
        return;
    d->differ++;
    fprintf(d->out, "  %s: original %lX native %lX\n", name, o, n);
}

/* Everything that differs between the machines, grouped: registers, flags
 * (one by one), then memories and I/O state as runs of bytes. What
 * native_compare leaves out (the don't-care set) is left out here too. */
static void describe_mismatch(FILE *out, const Snes *o, const Snes *c, uint16_t dont_care)
{
    Diff d = { out, 0, 0 };
    const Cpu *oc = &o->cpu, *cc = &c->cpu;
    bool a_low = !(dont_care & (DC_A | DC_AL)), a_high = !(dont_care & (DC_A | DC_AH));
    if (a_low && a_high)
        diff_field(&d, "A", oc->a, cc->a);
    else if (a_low)
        diff_field(&d, "A's low byte", oc->a & 0xFF, cc->a & 0xFF);
    else if (a_high)
        diff_field(&d, "A's high byte", oc->a >> 8, cc->a >> 8);
    if (!(dont_care & DC_X))
        diff_field(&d, "X", oc->x, cc->x);
    if (!(dont_care & DC_Y))
        diff_field(&d, "Y", oc->y, cc->y);
    diff_field(&d, "S", oc->s, cc->s);
    if (!(dont_care & DC_D))
        diff_field(&d, "D", oc->d, cc->d);
    if (!(dont_care & DC_DB))
        diff_field(&d, "DB", oc->db, cc->db);
    diff_field(&d, "PB", oc->pb, cc->pb);
    diff_field(&d, "PC", oc->pc, cc->pc);
    diff_field(&d, "E", oc->e, cc->e);
    static const struct { const char *name; uint8_t flag; uint16_t dc; } flags[] = {
        { "flag N", CPU_FLAG_N, DC_N }, { "flag V", CPU_FLAG_V, DC_V }, { "flag M", CPU_FLAG_M, 0 },
        { "flag X", CPU_FLAG_X, 0 },    { "flag D", CPU_FLAG_D, 0 },    { "flag I", CPU_FLAG_I, 0 },
        { "flag Z", CPU_FLAG_Z, DC_Z }, { "flag C", CPU_FLAG_C, DC_C },
    };
    for (size_t k = 0; k < sizeof flags / sizeof flags[0]; k++)
        if (!(dont_care & flags[k].dc))
            diff_field(&d, flags[k].name, (oc->p & flags[k].flag) != 0, (cc->p & flags[k].flag) != 0);

    diff_memory(&d, "WRAM", 0x7E0000, 6, o->bus.wram, c->bus.wram, sizeof o->bus.wram);
    diff_memory(&d, "SRAM", 0, 4, o->bus.sram, c->bus.sram, sizeof o->bus.sram);
    diff_memory(&d, "VRAM", 0, 4, o->ppu.vram, c->ppu.vram, sizeof o->ppu.vram);
    diff_memory(&d, "CGRAM", 0, 3, (const uint8_t *)o->ppu.cgram, (const uint8_t *)c->ppu.cgram, sizeof o->ppu.cgram);
    diff_memory(&d, "OAM", 0, 3, o->ppu.oam, c->ppu.oam, sizeof o->ppu.oam);
    diff_memory(&d, "PPU $21", 0, 2, o->ppu.reg, c->ppu.reg, sizeof o->ppu.reg);
    for (int ch = 0; ch < 8; ch++) {
        char name[16];
        snprintf(name, sizeof name, "DMA $43%X", ch);
        diff_memory(&d, name, 0, 1, o->bus.dma[ch].reg, c->bus.dma[ch].reg, sizeof o->bus.dma[ch].reg);
    }
    diff_memory(&d, "APU port", 0, 1, o->apu.cpu_in, c->apu.cpu_in, sizeof o->apu.cpu_in);
    const Ppu *po = &o->ppu, *pc = &c->ppu;
    diff_field(&d, "VMADD", po->vram_addr, pc->vram_addr);
    diff_field(&d, "CGADD", po->cg_addr, pc->cg_addr);
    diff_field(&d, "OAMADD", po->oam_addr, pc->oam_addr);
    const SnesBus *bo = &o->bus, *bc = &c->bus;
    diff_field(&d, "NMITIMEN", bo->nmitimen, bc->nmitimen);
    diff_field(&d, "WRMPYA", bo->wrmpya, bc->wrmpya);
    diff_field(&d, "WRMPYB", bo->wrmpyb, bc->wrmpyb);
    diff_field(&d, "WRDIV", bo->wrdiv, bc->wrdiv);
    diff_field(&d, "RDDIV", bo->rddiv, bc->rddiv);
    diff_field(&d, "RDMPY", bo->rdmpy, bc->rdmpy);
    diff_field(&d, "HTIME", bo->htime, bc->htime);
    diff_field(&d, "VTIME", bo->vtime, bc->vtime);
    diff_field(&d, "HDMAEN", bo->hdmaen, bc->hdmaen);
    diff_field(&d, "WRAM port address", bo->wram_port_addr, bc->wram_port_addr);
    if (d.ranges > MAX_RANGES)
        fprintf(out, "  ... and %u more range(s)\n", d.ranges - MAX_RANGES);
    fprintf(out, "  (%u difference(s) in all, %u memory range(s))\n", d.differ, d.ranges);
}

/* ---- the loop ---------------------------------------------------------------------- */

void fuzz_default_options(FuzzOptions *o)
{
    memset(o, 0, sizeof *o);
    o->iterations = 100000;
    o->seed = 1;
    for (int i = 0; i < 3; i++)
        o->range_hi[i] = 0xFFFF;
    o->budget = FUZZ_DEFAULT_BUDGET;
    o->details = 10;
}

static void report(FuzzResult *r, const FuzzOptions *o, FILE *log, unsigned long it, const char *what,
                   const Snes *orig, const Snes *native, uint16_t dont_care)
{
    unsigned long n = r->mismatched++;
    if (n >= (unsigned long)o->details)
        return;
    fprintf(log, "iteration %lu: %s\n", it, what);
    if (orig)
        describe_mismatch(log, orig, native, dont_care);
}

FuzzResult fuzz_run(const Snes *base, const NativeEntry *e, const FuzzOptions *o, FILE *log)
{
    static Snes orig, native, first_pass;
    FuzzResult r = { 0 };
    native_entry = e;
    native_machine = &native;
    native_budget = o->budget;
    /* The access budget stops call-outs too; native_call's own limit, in
     * instructions, only catches a callee that halts. */
    native_call_step_limit = o->budget;
    native_call_timeout_hook = callout_timeout;
    rng_state = o->seed * 0x9E3779B97F4A7C15ull | 1;
    uint16_t stack_page = base->cpu.s & 0xFF00;
    for (unsigned long it = 0; it < o->iterations; it++) {
        snes_clone(&orig, base);
        Snes *m = &orig;
        for (uint32_t i = 0; i < SNES_WRAM_SIZE; i += 8) {
            uint64_t bits = rng();
            memcpy(m->bus.wram + i, &bits, 8);
        }
        memcpy(m->bus.wram + stack_page, base->bus.wram + stack_page, 0x100);
        for (int k = 0; k < o->nareas; k++)
            if (o->areas[k].base == FUZZ_AT_ADDRESS && o->areas[k].keep)
                apply_area(m, base, &o->areas[k]);
        for (int k = 0; k < o->nareas; k++)
            if (o->areas[k].base == FUZZ_AT_ADDRESS && !o->areas[k].keep)
                apply_area(m, base, &o->areas[k]);
        uint64_t bits = rng();
        const uint8_t arith = CPU_FLAG_N | CPU_FLAG_V | CPU_FLAG_Z | CPU_FLAG_C;
        m->cpu.p = (uint8_t)((m->cpu.p & ~arith) | (bits & arith));
        m->cpu.p = (uint8_t)((m->cpu.p & ~e->p_mask) | e->p_value);
        uint16_t *regs[3] = { &m->cpu.a, &m->cpu.x, &m->cpu.y };
        for (int i = 0; i < 3; i++) {
            unsigned span = o->range_hi[i] >= o->range_lo[i] ? o->range_hi[i] - o->range_lo[i] + 1 : 1;
            *regs[i] = (uint16_t)(o->range_lo[i] + (unsigned)(bits >> (8 + 16 * i)) % span);
        }
        if (m->cpu.p & CPU_FLAG_X) {
            m->cpu.x &= 0xFF;
            m->cpu.y &= 0xFF;
        }
        /* relative to pointers, now that the state holds them */
        for (int k = 0; k < o->nareas; k++)
            if (o->areas[k].base != FUZZ_AT_ADDRESS)
                apply_area(m, base, &o->areas[k]);
        if (e->guard && !e->guard(m)) {
            r.guard++;
            continue;
        }

        /* Call it: return to address $0000 in the routine's bank. */
        uint16_t ret_s = m->cpu.s;
        uint32_t ret_pc = e->addr & 0xFF0000;
        if (o->interrupt) {
            /* As an interrupt: PB, PC ($0000), P pushed; I set, D cleared. RTI
             * returns to exactly that address. */
            snes_bus_write(&m->bus, m->cpu.s--, (uint8_t)(e->addr >> 16));
            snes_bus_write(&m->bus, m->cpu.s--, 0x00);
            snes_bus_write(&m->bus, m->cpu.s--, 0x00);
            snes_bus_write(&m->bus, m->cpu.s--, m->cpu.p);
            m->cpu.p = (uint8_t)((m->cpu.p | CPU_FLAG_I) & ~CPU_FLAG_D);
        } else {
            if (o->long_call)
                snes_bus_write(&m->bus, m->cpu.s--, (uint8_t)(e->addr >> 16));
            snes_bus_write(&m->bus, m->cpu.s--, 0xFF);
            snes_bus_write(&m->bus, m->cpu.s--, 0xFF);
        }
        m->cpu.pb = (uint8_t)(e->addr >> 16);
        m->cpu.pc = (uint16_t)e->addr;
        uint16_t entry_s = m->cpu.s;
        snes_clone(&native, &orig);
        native.interpreter = callout_step;
        native.bus.on_access = native_access;
        orig.bus.on_access = original_access;

        /* The native routine either returns or stops early at an instruction
         * boundary of the original; either way the original must get to the
         * same place. */
        native_work = 0;
        native_ran = RAN_OK;
        in_callout = false;
        native_call_steps = 0;
        native_stop_dont_care_direct = 0;
        native_stop_at_direct = 0;
        native_stop_own_direct = false;
        native_returned_direct = false;
        if (!native_fiber)
            native_fiber = fiber_create(native_fiber_main, NULL);
        fiber_switch(native_fiber);
        native.bus.on_access = NULL;
        uint16_t lowest_s;
        if (native_ran != RAN_OK) {
            /* Abandoned where it was: the fiber can't be resumed. */
            fiber_destroy(native_fiber);
            native_fiber = NULL;
            in_callout = false;
            if (native_ran == RAN_OUT_CALLOUT) {
                r.callout++;   /* a callee didn't return on this random state */
                continue;
            }
            /* The port's own code ran past the budget. If the original gets
             * back from the call on a small part of it, the port loops where
             * the original doesn't: a finding. Otherwise both run long. */
            if (run_until(&orig, ret_pc, ret_s, false, &lowest_s, o->budget / FUZZ_LOOP_SHARE)) {
                r.looped++;
                char what[160];
                snprintf(what, sizeof what,
                         "the port ran past %lu bus accesses; the original returned after %lu", o->budget,
                         original_work);
                report(&r, o, log, it, what, NULL, NULL, 0);
            } else {
                r.both_long++;
            }
            continue;
        }
        uint32_t stop_pc = cpu_pc24(&native.cpu);
        uint16_t stop_s = native.cpu.s;
        char what[160];
        if (native_stop_at_direct && native_stop_at_direct != stop_pc && !native_is_return(&orig, native_stop_at_direct)) {
            snprintf(what, sizeof what, "stopped at %06X, but declared what is dead at %06X", stop_pc,
                     native_stop_at_direct);
            report(&r, o, log, it, what, NULL, NULL, 0);
            continue;
        }
        if (!native_stop_at_direct && !native_returned_direct && e->dont_care) {
            snprintf(what, sizeof what, "stopped at %06X without declaring it (m_stop_at), with a don't-care set",
                     stop_pc);
            report(&r, o, log, it, what, NULL, NULL, 0);
            continue;
        }
        /* A routine that jumps back to its own entry stops where it started,
         * so the original has to run at least one instruction to get there. */
        bool back_at_entry = stop_pc == e->addr && stop_s == entry_s &&
                             (e->idle || native.bus.clocks != orig.bus.clocks);
        /* The original's budget: the port's, and a multiple of what the port
         * did (see FUZZ_ORIGINAL_FACTOR). */
        unsigned long budget = o->budget + FUZZ_ORIGINAL_FACTOR * native_work;
        if (!run_until(&orig, stop_pc, stop_s, back_at_entry, &lowest_s, budget)) {
            if (stop_pc == ret_pc && stop_s == ret_s) {
                r.original_long++;
            } else {
                snprintf(what, sizeof what, "original never reached %06X where the native routine stopped",
                         stop_pc);
                report(&r, o, log, it, what, NULL, NULL, 0);
            }
            continue;
        }
        /* Where the original passes that point more than once (a loop's
         * head), the first pass where the machines are the same counts, as in
         * shadow mode (native.c, reach_point); one that is the same at no pass
         * is reported with the first pass's difference. */
        uint16_t dont_care =
            native_finish_dont_care(e->dont_care, native_stop_dont_care_direct, native_stop_own_direct);
        bool same = false;
        char first_what[160] = "";
        for (unsigned pass = 0; pass < FUZZ_MAX_POINT_PASSES; pass++) {
            native_copy_popped_stack(&orig, &native, lowest_s);
            if (!native_compare(&orig, &native, dont_care, what, sizeof what)) {
                same = true;
                break;
            }
            if (!pass) {
                memcpy(first_what, what, sizeof first_what);
                snes_clone(&first_pass, &orig);
            }
            uint16_t low;
            if (!run_until(&orig, stop_pc, stop_s, true, &low, budget))
                break;
            if (low < lowest_s)
                lowest_s = low;
        }
        if (same)
            r.matched++;
        else
            report(&r, o, log, it, first_what, &first_pass, &native, dont_care);
    }
    return r;
}
