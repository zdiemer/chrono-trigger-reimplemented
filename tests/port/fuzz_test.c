/* Checks the differential fuzzer's core (dev/fuzz.c; docs/PORTING.md,
 * "Fuzzing") on made-up routines and ports (no ROM needed):
 *
 * - a correct port matches on every state, and one that differs is described
 *   in full: every register and flag that differs and every run of memory;
 * - a port whose own loop runs away on a random state, where the original
 *   returns, is abandoned at its budget and reported as a mismatch; one whose
 *   loop runs away where the original's does too is skipped;
 * - the original gets a budget in proportion to the port's work, so a port
 *   that does in a few accesses what the original loops through at length is
 *   compared, not skipped, at the default budget;
 * - a fill at a pointer the state holds ([cell]+offset,count) lands where the
 *   pointer points, after the pointer itself is filled;
 * - a call-out that halts the CPU is stopped too;
 * - a port that stops at a loop's head after some passes is compared at the
 *   original's pass where the machines are the same, and one the same at no
 *   pass is a mismatch;
 * - --poison-dc: a call-out whose callee reads a register the call declared
 *   dead mismatches once the register is scrambled, though the port passes
 *   the original's value; one whose callee only writes a declared register, or
 *   leaves it alone, still matches (what it left alone is the caller's again);
 * - areas parse, and malformed ones don't.
 *
 * Exits non-zero on failure.
 *
 *   fuzz_test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fuzz.h"
#include "port/machine.h"

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* ---- the originals, in bank $C0 (8-bit A, 16-bit index registers) --------------------- */

#define ROM_SIZE 0x10000

static const struct {
    uint16_t at;
    uint8_t code[24];
    uint8_t len;
} originals[] = {
    /* $C0:8000: copy $10 to $20 */
    { 0x8000, { 0xAF, 0x10, 0x00, 0x7E, 0x8F, 0x20, 0x00, 0x7E, 0x60 }, 9 },
    /* $C0:8100: LDX #$0010 : DEX : BNE : RTS -- always returns */
    { 0x8100, { 0xA2, 0x10, 0x00, 0xCA, 0xD0, 0xFD, 0x60 }, 7 },
    /* $C0:8200: wait until $7E:0040 is 0 -- loops for good on most random states */
    { 0x8200, { 0xAF, 0x40, 0x00, 0x7E, 0xD0, 0xFA, 0x60 }, 7 },
    /* $C0:8300: LDX $0040 : LDA $0020,X : STA $0050 : RTS (data bank $7E) */
    { 0x8300, { 0xAE, 0x40, 0x00, 0xBD, 0x20, 0x00, 0x8D, 0x50, 0x00, 0x60 }, 10 },
    /* $C0:8400: copy bank $7F onto itself four times over, a byte at a time */
    { 0x8400, { 0xA0, 0x04, 0x00, 0xA2, 0x00, 0x00, 0xBF, 0x00, 0x00, 0x7F, 0x9F, 0x00, 0x00, 0x7F, 0xE8, 0xD0,
                0xF5, 0x88, 0xD0, 0xEF, 0x60 }, 21 },
    /* $C0:8500: JSR $8600 : RTS, and $C0:8600: WAI -- a callee that halts, and
     * makes no more accesses */
    { 0x8500, { 0x20, 0x00, 0x86, 0x60 }, 4 },
    { 0x8600, { 0xCB, 0x60 }, 2 },
    /* $C0:8700: JSR $8800 : RTS, and $C0:8800: STA $0070 : LDX #$0001 : RTS --
     * a callee that reads A, writes X without reading it and leaves Y alone */
    { 0x8700, { 0x20, 0x00, 0x88, 0x60 }, 4 },
    { 0x8800, { 0x8D, 0x70, 0x00, 0xA2, 0x01, 0x00, 0x60 }, 7 },
};

/* ---- the ports ------------------------------------------------------------------------- */

static void copy_ok(Snes *s)
{
    m_lda8(s, m_read8(s, 0x7E0010));
    m_write8(s, 0x7E0020, (uint8_t)s->cpu.a);
    m_rts(s);
}

/* Wrong in several places at once: two runs of memory, a register and a flag. */
static void copy_wrong(Snes *s)
{
    m_lda8(s, m_read8(s, 0x7E0010));
    m_write8(s, 0x7E0020, (uint8_t)~s->cpu.a);
    m_write8(s, 0x7E0100, (uint8_t)~m_read8(s, 0x7E0100));
    m_write8(s, 0x7E0103, (uint8_t)~m_read8(s, 0x7E0103));
    s->cpu.y ^= 0x1234;
    s->cpu.p ^= CPU_FLAG_C;
    m_rts(s);
}

/* The countdown, but first a wait the original doesn't have: it loops for
 * good unless $7E:0030 happens to hold $5A. */
static void countdown_loops(Snes *s)
{
    while (m_read8(s, 0x7E0030) != 0x5A)
        ;
    s->cpu.x = 0;
    m_set_nz16(s, 0);
    m_rts(s);
}

static void wait_ok(Snes *s)
{
    while (m_lda8(s, m_read8(s, 0x7E0040)) != 0)
        ;
    m_rts(s);
}

/* Right only when the byte at [$40] + $20 is 5, which a fill at the pointer makes it. */
static void pointer_assumes_5(Snes *s)
{
    s->cpu.x = m_read16(s, 0x7E0040);
    m_write8(s, 0x7E0050, 0x05);
    m_rts(s);
}

static void copy_bank(Snes *s)
{
    for (int pass = 0; pass < 4; pass++)
        for (uint32_t x = 0; x < 0x10000; x++)
            m_write8(s, 0x7F0000 + x, m_read8(s, 0x7F0000 + x));
    s->cpu.x = 0;
    s->cpu.y = 0;
    m_rts(s);
}

/* $C0:8100 after three passes of its loop, stopping at the loop's head
 * ($C0:8103, DEX), where the original is on its fourth; the wrong one also
 * writes a byte the original doesn't. */
static void countdown_after(Snes *s, bool wrong)
{
    s->cpu.x = 0x0D;
    m_set_nz16(s, 0x0D);
    if (wrong)
        m_write8(s, 0x7E0060, (uint8_t)~m_read8(s, 0x7E0060));
    m_stop_at(s, 0xC08103, 0);
}
static void countdown_third(Snes *s) { countdown_after(s, false); }
static void countdown_wrong(Snes *s) { countdown_after(s, true); }

static void calls_halting(Snes *s)
{
    m_jsr(s, 0xC08500, 0xC08600);
    m_rts(s);
}

/* The call as the original makes it, the registers as the original has them,
 * declaring dead what the callee doesn't read (right), or A, which it does
 * (wrong, and only scrambling shows it). */
static void calls_declaring_x_y(Snes *s)
{
    m_jsr_dc(s, 0xC08700, 0xC08800, DC_X | DC_Y);
    m_rts(s);
}

static void calls_declaring_a(Snes *s)
{
    m_jsr_dc(s, 0xC08700, 0xC08800, DC_A);
    m_rts(s);
}

#define M8_X16 .p_mask = CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, .p_value = CPU_FLAG_M

static const NativeEntry entries[] = {
    { .addr = 0xC08000, .name = "copy_ok", .fn = copy_ok, M8_X16, .dont_care = DC_FLAGS },
    { .addr = 0xC08000, .name = "copy_wrong", .fn = copy_wrong, M8_X16, .dont_care = DC_N | DC_Z },
    { .addr = 0xC08100, .name = "countdown_loops", .fn = countdown_loops, M8_X16, .dont_care = DC_A },
    { .addr = 0xC08200, .name = "wait_ok", .fn = wait_ok, M8_X16 },
    { .addr = 0xC08300, .name = "pointer_assumes_5", .fn = pointer_assumes_5, M8_X16, .dont_care = DC_A | DC_FLAGS },
    { .addr = 0xC08400, .name = "copy_bank", .fn = copy_bank, M8_X16, .dont_care = DC_A | DC_FLAGS },
    { .addr = 0xC08500, .name = "calls_halting", .fn = calls_halting, M8_X16 },
    { .addr = 0xC08100, .name = "countdown_third", .fn = countdown_third, M8_X16, .dont_care = DC_A },
    { .addr = 0xC08100, .name = "countdown_wrong", .fn = countdown_wrong, M8_X16, .dont_care = DC_A },
    { .addr = 0xC08700, .name = "calls_declaring_x_y", .fn = calls_declaring_x_y, M8_X16 },
    { .addr = 0xC08700, .name = "calls_declaring_a", .fn = calls_declaring_a, M8_X16 },
};

static uint8_t rom[ROM_SIZE];
static Snes base;

static FuzzResult run(int entry, const FuzzOptions *o, char *log, size_t n)
{
    FILE *f = tmpfile();
    FuzzResult r = fuzz_run(&base, &entries[entry], o, f ? f : stderr);
    if (f) {
        rewind(f);
        size_t got = fread(log, 1, n - 1, f);
        log[got] = 0;
        fclose(f);
    } else {
        log[0] = 0;
    }
    return r;
}

static FuzzOptions options(unsigned long iterations)
{
    FuzzOptions o;
    fuzz_default_options(&o);
    o.iterations = iterations;
    return o;
}

int main(void)
{
    for (size_t k = 0; k < sizeof originals / sizeof originals[0]; k++)
        memcpy(rom + originals[k].at, originals[k].code, originals[k].len);
    snes_init(&base, rom, sizeof rom);
    base.interpreter = cpu_step;
    base.cpu.e = false;
    base.cpu.s = 0x1FF0;
    base.cpu.d = 0x0000;
    base.cpu.db = 0x7E;
    base.cpu.p = CPU_FLAG_M | CPU_FLAG_I;
    static char log[1 << 16];

    /* A correct port matches everywhere; a wrong one is described in full. */
    FuzzOptions o = options(200);
    FuzzResult r = run(0, &o, log, sizeof log);
    CHECK(r.matched == 200 && r.mismatched == 0, "copy_ok: matched %lu mismatched %lu", r.matched, r.mismatched);
    o.details = 1;
    r = run(1, &o, log, sizeof log);
    CHECK(r.mismatched == 200, "copy_wrong: mismatched %lu", r.mismatched);
    CHECK(strstr(log, "  Y: original") != NULL, "the report names Y:\n%s", log);
    CHECK(strstr(log, "  flag C: original") != NULL, "the report names the carry:\n%s", log);
    CHECK(strstr(log, "flag Z") == NULL, "the report leaves out the don't-care flags:\n%s", log);
    CHECK(strstr(log, "  WRAM 7E0020-7E0020: 1 byte(s), 1 differ") != NULL, "the report has $20:\n%s", log);
    CHECK(strstr(log, "  WRAM 7E0100-7E0103: 4 byte(s), 2 differ") != NULL,
          "the report groups $100-$103:\n%s", log);
    CHECK(strstr(log, "(5 difference(s) in all, 2 memory range(s))") != NULL, "the report's count:\n%s", log);

    /* The port loops where the original returns: abandoned and a finding. */
    o = options(100);
    o.budget = 100000;
    r = run(2, &o, log, sizeof log);
    CHECK(r.looped > 90 && r.mismatched == r.looped && r.matched + r.looped == 100,
          "countdown_loops: looped %lu mismatched %lu matched %lu", r.looped, r.mismatched, r.matched);
    CHECK(strstr(log, "the port ran past 100000 bus accesses; the original returned after") != NULL,
          "the report says the port looped:\n%s", log);

    /* Both loop for good on most states: skipped, not a finding. */
    r = run(3, &o, log, sizeof log);
    CHECK(r.mismatched == 0 && r.both_long > 90 && r.matched + r.both_long == 100,
          "wait_ok: mismatched %lu both long %lu matched %lu", r.mismatched, r.both_long, r.matched);

    /* A call-out that halts the CPU makes no accesses: stopped all the same,
     * and skipped. */
    o = options(20);
    o.budget = 20000;
    r = run(6, &o, log, sizeof log);
    CHECK(r.callout == 20 && r.mismatched == 0, "calls_halting: call-outs past the budget %lu, mismatched %lu",
          r.callout, r.mismatched);

    /* The original loops through bank $7F four times, 1.05 million
     * instructions, what the port does in 524288 accesses: compared, at the
     * default budget. (The fuzzer before gave the original 400000 instructions,
     * and skipped every state.) */
    o = options(3);
    r = run(5, &o, log, sizeof log);
    CHECK(r.matched == 3, "copy_bank: matched %lu, original past its budget %lu, mismatched %lu", r.matched,
          r.original_long, r.mismatched);

    /* A fill at the pointer in $40: after the pointer's own fill (kept in
     * $20xx-$7Fxx, off the stack page and in the bank), the byte at [$40] + $20 is 5. */
    o = options(300);
    CHECK(fuzz_parse_area("7E0041-7E0041=20-7F", false, &o.areas[o.nareas++]), "parse the pointer's fill");
    r = run(4, &o, log, sizeof log);
    CHECK(r.matched < 10, "pointer_assumes_5 without the fill: matched %lu", r.matched);
    CHECK(fuzz_parse_area("[7E0040]+20,1=05-05", false, &o.areas[o.nareas++]), "parse the fill at the pointer");
    r = run(4, &o, log, sizeof log);
    CHECK(r.matched == 300, "pointer_assumes_5 with the fill: matched %lu mismatched %lu", r.matched, r.mismatched);

    /* A stop at a loop's head: the pass where the machines are the same counts;
     * one the same at no pass is described at the first. */
    o = options(100);
    r = run(7, &o, log, sizeof log);
    CHECK(r.matched == 100, "countdown_third: matched %lu mismatched %lu", r.matched, r.mismatched);
    o.details = 1;
    r = run(8, &o, log, sizeof log);
    CHECK(r.mismatched > 90, "countdown_wrong: mismatched %lu", r.mismatched);
    CHECK(strstr(log, "  X: original 10 native D") != NULL, "the first pass's difference:\n%s", log);

    /* Scrambling what call-outs declare dead: a callee that reads A shows it;
     * one that writes X, and leaves Y alone, doesn't. */
    o = options(100);
    r = run(10, &o, log, sizeof log);
    CHECK(r.matched == 100 && r.scrambled == 0, "calls_declaring_a unscrambled: matched %lu", r.matched);
    o.poison_dc = true;
    r = run(10, &o, log, sizeof log);
    CHECK(r.mismatched > 90 && r.scrambled == 100, "calls_declaring_a scrambled: mismatched %lu scrambled %lu",
          r.mismatched, r.scrambled);
    CHECK(strstr(log, "WRAM 7E0070-7E0070") != NULL, "the callee's store differs:\n%s", log);
    r = run(9, &o, log, sizeof log);
    CHECK(r.matched == 100 && r.scrambled == 100, "calls_declaring_x_y scrambled: matched %lu mismatched %lu",
          r.matched, r.mismatched);
    o.poison_site = 0xC08701;
    r = run(10, &o, log, sizeof log);
    CHECK(r.matched == 100 && r.scrambled == 0, "calls_declaring_a scrambled at another site: matched %lu",
          r.matched);
    o.poison_site = 0xC08700;
    r = run(10, &o, log, sizeof log);
    CHECK(r.mismatched > 90, "calls_declaring_a scrambled at its site: mismatched %lu", r.mismatched);

    /* Areas. */
    FuzzArea a;
    CHECK(fuzz_parse_area("7F:[7E006D]-8,10=00-0F", false, &a) && a.base == FUZZ_AT_CELL && a.pointer == 0x7E006D &&
              a.offset == -8 && a.count == 0x10 && a.bank == 0x7F && a.lo == 0 && a.hi == 0x0F,
          "a banked fill at a pointer, back 8");
    CHECK(fuzz_parse_area("[X]+2,4", true, &a) && a.base == FUZZ_AT_X && a.keep && a.offset == 2 && a.count == 4,
          "a keep at X");
    CHECK(fuzz_parse_area("7E0100-7E01FF", true, &a) && a.base == FUZZ_AT_ADDRESS && a.lo_addr == 0x7E0100 &&
              a.hi_addr == 0x7E01FF,
          "an absolute keep");
    CHECK(!fuzz_parse_area("[7E006D]+20=00-FF", false, &a), "a fill at a pointer needs a count");
    CHECK(!fuzz_parse_area("[7E006D]+20,4", false, &a), "a fill needs its values");
    CHECK(!fuzz_parse_area("7E0100-7E01FF=00-100", false, &a), "values are bytes");
    CHECK(!fuzz_parse_area("[7E006D+20,4", true, &a), "a bracket closes");

    if (failures)
        printf("%d failure(s)\n", failures);
    else
        printf("fuzz_test: all passed\n");
    return failures ? 1 : 0;
}
