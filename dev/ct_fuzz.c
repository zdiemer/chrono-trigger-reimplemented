/* ct_fuzz: differential testing of a ported routine against the original, on
 * random machine states rather than states a movie happens to reach.
 *
 *   ct_fuzz --rom roms/ct-usa.sfc --load-state base.state --routine <name|BBAAAA>
 *           [--iterations N] [--seed S] [--long | --interrupt] [--keep BBAAAA-BBAAAA]... [--step-limit N]
 *           [--db BB] [--d HHHH] [--apu-loopback]
 *
 * Each iteration starts from the base state (or power-on), randomizes A, X, Y,
 * the N V Z C flags and all of work RAM except the stack page, applies the
 * routine's entry precondition on P, and calls the routine as a JSR (JSL with
 * --long) would, or as an interrupt (--interrupt). The original runs on the interpreter until it returns; the
 * native routine runs on a copy (call-outs interpreted synchronously). The
 * machines are compared where the native routine left off, normally the
 * return, as in shadow mode: ignoring the routine's don't-care set (and what it
 * declares dead where it stops, native_dont_care_at_stop), and the stack bytes
 * the original pushed and pulled again (native_copy_popped_stack). Nothing
 * advances time, and interrupts don't happen. States where the native
 * routine's call-outs together run more than --step-limit instructions (default
 * 100000), or the original doesn't get back within four times that, are skipped.
 * An idle entry is checked the same way over one pass of its loop: the original
 * runs until it is back at the loop head, or at the exit the pass took.
 *
 * --keep leaves a WRAM range (bus addresses in $7E/$7F) as it is in the base
 * state, for routines that need some structure intact. --range A|X|Y=lo-hi
 * limits a register's random value, for example to keep script offsets inside
 * the script buffer: indexing past the end of a data area reaches I/O space,
 * whose open bus reads depend on instruction fetches native code doesn't do.
 * --fill BBAAAA-BBAAAA=lo-hi gives a WRAM range random bytes from lo to hi
 * instead, for example to make a pointer in script operands point into WRAM.
 * --db sets the data bank, which the base state otherwise decides, for entries
 * that address registers through it; --d the direct page likewise, for entries
 * guarded on the page their subsystem runs with. --apu-loopback makes reads of the APU ports
 * return what the CPU last wrote to them: the SPC700 doesn't run here, so the
 * sound driver's echo waits would otherwise never end.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/fiber.h"
#include "registry.h"
#include "snes/system.h"

#define MAX_KEEP 16

static uint64_t rng_state;

static uint64_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)n);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static unsigned long step_limit = 100000;

/* --apu-loopback: the SNES's APU read hook, answering with the CPU's own writes. */
static uint8_t apu_loopback_read(void *ctx, uint8_t port)
{
    return ((Snes *)ctx)->apu.cpu_in[port & 3];
}

/* The native routine runs on its own fiber, so that when its call-outs run out
 * of steps it can be abandoned wherever it is. */
static const NativeEntry *native_entry;
static Snes *native_machine;
static Fiber *native_fiber;

static void native_fiber_main(void *arg)
{
    (void)arg;
    for (;;) {
        native_entry->fn(native_machine);
        fiber_switch(fiber_main());
    }
}

static void abandon_native(void)
{
    fiber_switch(fiber_main());
}

/* Run the interpreter until PC and S are back at the return sentinel, noting
 * the lowest S on the way. The original gets four times the call-outs' budget:
 * it also runs the instructions the native routine did itself, so on a state
 * where the call-outs came close to the limit, the original alone can pass it. */
static bool run_until(Snes *s, uint32_t ret_pc, uint16_t ret_s, bool must_step, uint16_t *lowest_s)
{
    *lowest_s = s->cpu.s;
    for (unsigned long steps = 0; steps < 4 * step_limit; steps++) {
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

int main(int argc, char **argv)
{
    const char *rom_path = NULL, *state_path = NULL, *routine = NULL;
    unsigned long iterations = 100000;
    uint64_t seed = 1;
    bool long_call = false, interrupt = false, apu_loopback = false;
    uint32_t keep_lo[MAX_KEEP], keep_hi[MAX_KEEP];
    unsigned range_lo[3] = { 0, 0, 0 }, range_hi[3] = { 0xFFFF, 0xFFFF, 0xFFFF };   /* A, X, Y */
    int nkeep = 0;
    unsigned fill_lo[MAX_KEEP], fill_hi[MAX_KEEP], fill_min[MAX_KEEP], fill_max[MAX_KEEP];
    int nfill = 0;
    int data_bank = -1, direct_page = -1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--long")) {
            long_call = true;
            continue;
        }
        if (!strcmp(a, "--interrupt")) {
            interrupt = true;
            continue;
        }
        if (!strcmp(a, "--apu-loopback")) {
            apu_loopback = true;
            continue;
        }
        if (!v)
            goto usage;
        i++;
        if (!strcmp(a, "--rom")) rom_path = v;
        else if (!strcmp(a, "--load-state")) state_path = v;
        else if (!strcmp(a, "--routine")) routine = v;
        else if (!strcmp(a, "--iterations")) iterations = strtoul(v, NULL, 0);
        else if (!strcmp(a, "--seed")) seed = strtoull(v, NULL, 0);
        else if (!strcmp(a, "--step-limit")) step_limit = strtoul(v, NULL, 0);
        else if (!strcmp(a, "--db")) data_bank = (int)(strtoul(v, NULL, 16) & 0xFF);
        else if (!strcmp(a, "--d")) direct_page = (int)(strtoul(v, NULL, 16) & 0xFFFF);
        else if (!strcmp(a, "--range") && strchr("AXY", v[0]) && v[1] == '=' &&
                 sscanf(v + 2, "%x-%x", &range_lo[strchr("AXY", v[0]) - "AXY"],
                        &range_hi[strchr("AXY", v[0]) - "AXY"]) == 2)
            ;
        else if (!strcmp(a, "--fill") && nfill < MAX_KEEP &&
                 sscanf(v, "%x-%x=%x-%x", &fill_lo[nfill], &fill_hi[nfill], &fill_min[nfill], &fill_max[nfill]) == 4)
            nfill++;
        else if (!strcmp(a, "--keep") && nkeep < MAX_KEEP &&
                 sscanf(v, "%x-%x", &keep_lo[nkeep], &keep_hi[nkeep]) == 2)
            nkeep++;
        else
            goto usage;
    }
    if (!rom_path || !routine)
        goto usage;

    const NativeEntry *e = NULL;
    uint32_t want = (uint32_t)strtoul(routine, NULL, 16);
    for (int i = 0; i < game_native_count; i++)
        if (!strcmp(game_natives[i].name, routine) || game_natives[i].addr == want)
            e = &game_natives[i];
    if (!e) {
        fprintf(stderr, "no routine %s\n", routine);
        return 2;
    }

    size_t rom_len;
    uint8_t *rom = read_file(rom_path, &rom_len);
    if (!rom) {
        fprintf(stderr, "cannot read %s\n", rom_path);
        return 1;
    }
    size_t header = rom_len % 0x8000 == 0x200 ? 0x200 : 0;

    static Snes base, orig, native;
    snes_init(&base, rom + header, rom_len - header);
    base.interpreter = cpu_step;   /* call-outs are interpreted (native_call) */
    snes_power_on(&base);
    if (state_path && snes_state_load(&base, state_path) != 0) {
        fprintf(stderr, "cannot load state %s\n", state_path);
        return 1;
    }
    base.cpu.e = false;
    if (data_bank >= 0)
        base.cpu.db = (uint8_t)data_bank;
    if (direct_page >= 0)
        base.cpu.d = (uint16_t)direct_page;
    if (apu_loopback)
        base.bus.io.apu_read = apu_loopback_read;
    native_call_step_limit = step_limit;
    native_call_timeout_hook = abandon_native;
    native_entry = e;
    native_machine = &native;

    rng_state = seed * 0x9E3779B97F4A7C15ull | 1;
    unsigned long matched = 0, mismatched = 0, no_return = 0;
    uint16_t stack_page = base.cpu.s & 0xFF00;
    for (unsigned long it = 0; it < iterations; it++) {
        snes_clone(&orig, &base);
        Snes *m = &orig;
        for (uint32_t i = 0; i < SNES_WRAM_SIZE; i += 8) {
            uint64_t bits = rng();
            memcpy(m->bus.wram + i, &bits, 8);
        }
        memcpy(m->bus.wram + stack_page, base.bus.wram + stack_page, 0x100);
        for (int k = 0; k < nkeep; k++) {
            uint32_t lo = keep_lo[k] - 0x7E0000, hi = keep_hi[k] - 0x7E0000;
            if (lo <= hi && hi < SNES_WRAM_SIZE)
                memcpy(m->bus.wram + lo, base.bus.wram + lo, hi - lo + 1);
        }
        for (int k = 0; k < nfill; k++) {
            unsigned span = fill_max[k] >= fill_min[k] ? fill_max[k] - fill_min[k] + 1 : 1;
            for (uint32_t addr = fill_lo[k]; addr <= fill_hi[k]; addr++)
                if (addr - 0x7E0000u < SNES_WRAM_SIZE)
                    m->bus.wram[addr - 0x7E0000u] = (uint8_t)(fill_min[k] + rng() % span);
        }
        uint64_t r = rng();
        const uint8_t arith = CPU_FLAG_N | CPU_FLAG_V | CPU_FLAG_Z | CPU_FLAG_C;
        m->cpu.p = (uint8_t)((m->cpu.p & ~arith) | (r & arith));
        m->cpu.p = (uint8_t)((m->cpu.p & ~e->p_mask) | e->p_value);
        uint16_t *regs[3] = { &m->cpu.a, &m->cpu.x, &m->cpu.y };
        for (int i = 0; i < 3; i++) {
            unsigned span = range_hi[i] >= range_lo[i] ? range_hi[i] - range_lo[i] + 1 : 1;
            *regs[i] = (uint16_t)(range_lo[i] + (unsigned)(r >> (8 + 16 * i)) % span);
        }
        if (m->cpu.p & CPU_FLAG_X) {
            m->cpu.x &= 0xFF;
            m->cpu.y &= 0xFF;
        }
        if (e->guard && !e->guard(m)) {
            no_return++;
            continue;
        }

        /* Call it: return to address $0000 in the routine's bank. */
        uint16_t ret_s = m->cpu.s;
        uint32_t ret_pc = e->addr & 0xFF0000;
        if (interrupt) {
            /* As an interrupt: PB, PC ($0000), P pushed; I set, D cleared. RTI
             * returns to exactly that address. */
            snes_bus_write(&m->bus, m->cpu.s--, (uint8_t)(e->addr >> 16));
            snes_bus_write(&m->bus, m->cpu.s--, 0x00);
            snes_bus_write(&m->bus, m->cpu.s--, 0x00);
            snes_bus_write(&m->bus, m->cpu.s--, m->cpu.p);
            m->cpu.p = (uint8_t)((m->cpu.p | CPU_FLAG_I) & ~CPU_FLAG_D);
        } else {
            if (long_call)
                snes_bus_write(&m->bus, m->cpu.s--, (uint8_t)(e->addr >> 16));
            snes_bus_write(&m->bus, m->cpu.s--, 0xFF);
            snes_bus_write(&m->bus, m->cpu.s--, 0xFF);
        }
        m->cpu.pb = (uint8_t)(e->addr >> 16);
        m->cpu.pc = (uint16_t)e->addr;
        uint16_t entry_s = m->cpu.s;
        snes_clone(&native, &orig);

        /* The native routine either returns or stops early at an instruction
         * boundary of the original; either way the original must get to the
         * same place. */
        unsigned long timeouts = native_call_timeouts;
        native_call_steps = 0;
        native_stop_dont_care_direct = 0;
        if (!native_fiber)
            native_fiber = fiber_create(native_fiber_main, NULL);
        fiber_switch(native_fiber);
        if (native_call_timeouts != timeouts) {
            /* A callee didn't return on this random state (or they all ran too
             * long); the fiber may be stuck inside the routine. */
            fiber_destroy(native_fiber);
            native_fiber = NULL;
            no_return++;
            continue;
        }
        uint32_t stop_pc = cpu_pc24(&native.cpu);
        uint16_t stop_s = native.cpu.s;
        /* A routine that jumps back to its own entry stops where it started,
         * so the original has to run at least one instruction to get there. */
        bool back_at_entry = stop_pc == e->addr && stop_s == entry_s &&
                             (e->idle || native.bus.clocks != orig.bus.clocks);
        uint16_t lowest_s;
        if (!run_until(&orig, stop_pc, stop_s, back_at_entry, &lowest_s)) {
            if (stop_pc == ret_pc && stop_s == ret_s) {
                no_return++;
            } else {
                mismatched++;
                fprintf(stderr, "iteration %lu: original never reached %06X where the native routine stopped\n", it,
                        stop_pc);
            }
            continue;
        }
        char why[256];
        native_copy_popped_stack(&orig, &native, lowest_s);
        if (native_compare(&orig, &native, e->dont_care | native_stop_dont_care_direct, why, sizeof why)) {
            if (mismatched++ < 10)
                fprintf(stderr, "iteration %lu: %s\n", it, why);
        } else {
            matched++;
        }
    }
    printf("%s: %lu iterations, matched %lu, mismatched %lu, skipped %lu (didn't return, or guard failed)\n",
           e->name, iterations, matched, mismatched, no_return);
    return mismatched ? 1 : 0;

usage:
    fprintf(stderr, "usage: ct_fuzz --rom <rom> --routine <name|BBAAAA> [--load-state f] [--iterations N] "
                    "[--seed S] [--long | --interrupt] [--keep BBAAAA-BBAAAA]... [--range A|X|Y=lo-hi]... [--step-limit N] "
                    "[--db BB] [--d HHHH] [--apu-loopback]\n");
    return 2;
}
