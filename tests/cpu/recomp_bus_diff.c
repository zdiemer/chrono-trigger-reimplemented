/* Runs the C generated for every opcode and context (tools/recomp/sst.py,
 * bound to the development system's bus) against the interpreter on the SNES
 * machine, from random states.
 *
 *   recomp_bus_diff [--iterations N] [--seed S]
 *
 * SingleStepTests (sst_recomp_runner) check instruction semantics on a flat
 * memory. This checks what that memory can't show: I/O side effects and the
 * order of bus accesses, open bus, and bus time, including the bytes the
 * interpreter fetches and generated code doesn't. Instructions are placed in
 * WRAM, and operands, the data bank and the direct page are biased toward the
 * PPU, APU and CPU I/O registers. After each instruction the machines must match
 * in everything shadow validation compares, plus open bus, bus clocks, internal
 * cycles and the interrupt and halt state. N instructions run per opcode and
 * context (default 200). Prints a summary; exits non-zero on any mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/native.h"
#include "sst_recomp.h"

static uint64_t rng_state;

static uint64_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static uint16_t io_address(void)
{
    static const uint16_t base[] = { 0x2100, 0x2130, 0x2140, 0x2180, 0x4016, 0x4200, 0x4210, 0x4300 };
    static const uint16_t span[] = { 0x30, 0x10, 0x04, 0x04, 0x02, 0x10, 0x10, 0x80 };
    int i = (int)(rng() % 8);
    return (uint16_t)(base[i] + rng() % span[i]);
}

static void randomize_wram(Snes *s)
{
    for (uint32_t i = 0; i < SNES_WRAM_SIZE; i += 8) {
        uint64_t bits = rng();
        memcpy(s->bus.wram + i, &bits, 8);
    }
}

/* Registers, operand and the instruction's bytes at a random WRAM address. */
static void setup(Snes *s, uint8_t opcode, int ctx)
{
    static const uint8_t banks[] = { 0x00, 0x7E, 0x7F, 0x80 };
    static const uint16_t pages[] = { 0x0000, 0x2100, 0x4200, 0x4300 };
    Cpu *c = &s->cpu;
    c->a = (uint16_t)rng();
    c->x = (uint16_t)(rng() & 1 ? rng() % 0x20 : rng());
    c->y = (uint16_t)(rng() & 1 ? rng() % 0x20 : rng());
    c->db = rng() % 5 ? banks[rng() % 4] : (uint8_t)rng();
    c->d = rng() % 5 ? pages[rng() % 4] : (uint16_t)rng();
    c->p = (uint8_t)rng();
    c->e = ctx == 4;
    if (c->e) {
        c->s = (uint16_t)(0x0100 | (rng() & 0xFF));
    } else {
        c->s = (uint16_t)rng();
        c->p = (uint8_t)((c->p & ~(CPU_FLAG_M | CPU_FLAG_X)) | (ctx < 2 ? CPU_FLAG_M : 0) |
                         (ctx % 2 == 0 ? CPU_FLAG_X : 0));
    }
    c->nmi_pending = c->irq_line = c->waiting = c->stopped = false;
    c->pb = 0x7E;
    c->pc = (uint16_t)(0x2000 + rng() % 0xDFF0);
    uint16_t code = c->pc;
    s->bus.wram[code] = opcode;
    uint16_t operand = rng() & 1 ? io_address() : (uint16_t)rng();
    s->bus.wram[code + 1] = rng() % 4 ? (uint8_t)operand : (uint8_t)(rng() % 0x20);
    s->bus.wram[code + 2] = (uint8_t)(operand >> 8);
    s->bus.wram[code + 3] = (uint8_t)(rng() % 3 ? banks[rng() % 4] : rng());
    s->bus.open_bus = (uint8_t)rng();
    s->bus.clocks = 0;
    c->io_cycles = 0;
}

#define CHECK(label, x, y)                                                                               \
    if ((x) != (y)) {                                                                                    \
        snprintf(out, n, "%s interpreter %lX generated %lX", label, (unsigned long)(x), (unsigned long)(y)); \
        return true;                                                                                     \
    }

static bool differ(const Snes *a, const Snes *b, char *out, size_t n)
{
    if (native_compare(a, b, 0, out, n))
        return true;
    CHECK("open bus", a->bus.open_bus, b->bus.open_bus)
    CHECK("bus clocks", a->bus.clocks, b->bus.clocks)
    CHECK("internal cycles", a->cpu.io_cycles, b->cpu.io_cycles)
    CHECK("waiting", a->cpu.waiting, b->cpu.waiting)
    CHECK("stopped", a->cpu.stopped, b->cpu.stopped)
    CHECK("NMI flag", a->bus.nmi_flag, b->bus.nmi_flag)
    CHECK("IRQ flag", a->bus.irq_flag, b->bus.irq_flag)
    CHECK("NMI request", a->bus.nmi_request, b->bus.nmi_request)
    CHECK("joypad serial 1", a->bus.serial[0], b->bus.serial[0])
    CHECK("joypad serial 2", a->bus.serial[1], b->bus.serial[1])
    return false;
}

int main(int argc, char **argv)
{
    unsigned long iterations = 200;
    uint64_t seed = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--iterations") && i + 1 < argc) {
            iterations = strtoul(argv[++i], NULL, 0);
        } else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            seed = strtoull(argv[++i], NULL, 0);
        } else {
            fprintf(stderr, "usage: recomp_bus_diff [--iterations N] [--seed S]\n");
            return 2;
        }
    }
    rng_state = seed * 0x9E3779B97F4A7C15ull | 1;

    static uint8_t rom[0x400000];
    for (size_t i = 0; i < sizeof rom; i++)
        rom[i] = (uint8_t)rng();
    Snes *interp = malloc(sizeof *interp), *gen = malloc(sizeof *gen);
    snes_init(interp, rom, sizeof rom);
    snes_power_on(interp);

    static const char *ctx_names[] = { "m8x8", "m8x16", "m16x8", "m16x16", "emu" };
    unsigned long runs = 0, failures = 0;
    for (int op = 0; op < 256; op++) {
        for (int ctx = 0; ctx < 5; ctx++) {
            randomize_wram(interp);
            unsigned long op_failures = 0;
            for (unsigned long it = 0; it < iterations; it++) {
                setup(interp, (uint8_t)op, ctx);
                snes_clone(gen, interp);
                cpu_step(&interp->cpu);
                sst_recomp_table[op][ctx](&gen->cpu);
                runs++;
                char why[200];
                if (differ(interp, gen, why, sizeof why)) {
                    if (op_failures++ < 2)
                        printf("  %02X %s: %s\n", op, ctx_names[ctx], why);
                    failures++;
                }
            }
        }
    }
    printf("%lu instructions, %lu mismatches\n", runs, failures);
    free(interp);
    free(gen);
    return failures != 0;
}
