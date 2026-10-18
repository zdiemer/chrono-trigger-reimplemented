/* Checks time attribution (native_time_as, NATIVE_TIME_AS; runtime/port/
 * native.h) in shadow mode on a made-up program (no ROM needed): a dispatcher
 * that calls one of two handlers, ported as one function that runs the
 * handlers' work inline and names each handler's address.
 *
 * - the timing report lists the clocks attributed to each handler, exactly
 *   the bus clocks the port spent on its work, and the dispatcher's totals
 *   include them;
 * - the handlers, entries of their own, are measured on their own calls
 *   (nested in the dispatcher's comparison), which is what calibrates them;
 * - an attribution to an address with no hand port stays the port's own time
 *   and is reported;
 * - a sibling's scope (native_time_as_sibling) around a handler that calls
 *   out mid-work makes the stretches that are mostly its work the handler's
 *   in the report, not the dispatcher's;
 * - every call still matches: attribution changes no behaviour.
 *
 * Exits non-zero on failure.
 *
 *   time_parts_test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/machine.h"
#include "port/native.h"
#include "snes/system.h"

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

#define ROM_SIZE 0x10000

/* Bank $00 (HiROM maps ROM $8000-$FFFF there). */
static const struct {
    uint16_t at;
    uint8_t code[24];
    uint8_t len;
} program[] = {
    /* 8000: SEI : CLC : XCE : REP #$10 : SEP #$20 : LDX #$1FF0 : TXS;
     * 800B: JSR $8100 : INC $10 : BRA $800B */
    { 0x8000, { 0x78, 0x18, 0xFB, 0xC2, 0x10, 0xE2, 0x20, 0xA2, 0xF0, 0x1F, 0x9A, 0x20, 0x00, 0x81, 0xE6, 0x10, 0x80,
                0xF9 }, 18 },
    /* 8100, the dispatcher: LDA $10 : AND #$01 : BEQ +4 : JSR $8200 : RTS : JSR $8300 : RTS */
    { 0x8100, { 0xA5, 0x10, 0x29, 0x01, 0xF0, 0x04, 0x20, 0x00, 0x82, 0x60, 0x20, 0x00, 0x83, 0x60 }, 14 },
    /* 8200, handler 1: a delay and one store. LDX #$0040 : DEX : BNE : STA $30 : RTS */
    { 0x8200, { 0xA2, 0x40, 0x00, 0xCA, 0xD0, 0xFD, 0x85, 0x30, 0x60 }, 9 },
    /* 8300, handler 2: copies four bytes. LDA $40 : STA $41 : LDA $42 : STA $43 ... : RTS */
    { 0x8300, { 0xA5, 0x40, 0x85, 0x41, 0xA5, 0x42, 0x85, 0x43, 0xA5, 0x44, 0x85, 0x45, 0xA5, 0x46, 0x85, 0x47, 0x60 },
      17 },
    /* 8400, handler 3, which the dispatcher calls in handler 2's place in the
     * sibling run: LDA $40 : STA $41 : JSR $8500 : LDA $42 : STA $43 : JSR $8500 : RTS */
    { 0x8400, { 0xA5, 0x40, 0x85, 0x41, 0x20, 0x00, 0x85, 0xA5, 0x42, 0x85, 0x43, 0x20, 0x00, 0x85, 0x60 }, 15 },
    /* 8500, the original's routine handler 3 calls: RTS */
    { 0x8500, { 0x60 }, 1 },
};

/* The sibling run: the dispatcher's JSR $8300 at $810A made JSR $8400. */
enum { SECOND_CALL = 0x810A, HANDLER3 = 0x8400, HANDLER3_CALLS = 0x8404, HANDLER3_CALLS_AGAIN = 0x840B };

/* The handlers' work, which the dispatcher's port runs inline. */
static void handler1_work(Snes *s, uint8_t a)
{
    m_write8(s, 0x000030, a);
    s->cpu.x = 0;
}

static void handler2_work(Snes *s)
{
    for (uint32_t k = 0; k < 4; k++)
        m_write8(s, 0x000041 + 2 * k, m_read8(s, 0x000040 + 2 * k));
    s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00) | m_read8(s, 0x000046));
}

static void handler1(Snes *s)
{
    handler1_work(s, (uint8_t)s->cpu.a);
    m_rts(s);
}

static void handler2(Snes *s)
{
    handler2_work(s);
    m_rts(s);
}

/* Handler 3's work, a copy either side of each call to the original's $8500:
 * its middle stretch, between the two calls, is its work alone. */
static void copy_byte(Snes *s, uint32_t from)
{
    uint8_t v = m_read8(s, from);
    m_write8(s, from + 1, v);
    s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00) | v);
}

static void handler3_work(Snes *s)
{
    copy_byte(s, 0x000040);
    m_jsr_dc(s, HANDLER3_CALLS, 0x008500, DC_A | DC_X | DC_Y | DC_FLAGS);
    copy_byte(s, 0x000042);
    m_jsr_dc(s, HANDLER3_CALLS_AGAIN, 0x008500, DC_A | DC_X | DC_Y | DC_FLAGS);
}

static void handler3(Snes *s)
{
    handler3_work(s);
    m_rts(s);
}

static uint32_t handler1_tag = 0x008200;
static bool sibling_run;

static void dispatcher(Snes *s)
{
    uint8_t a = m_read8(s, 0x000010) & 1;
    s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00) | a);
    if (a)
        NATIVE_TIME_AS(s, handler1_tag, handler1_work(s, a));
    else if (sibling_run) {
        m_push16(s, SECOND_CALL + 2);   /* the return the original's JSR pushes, under handler 3's calls */
        NATIVE_TIME_AS_SIBLING(s, HANDLER3, handler3_work(s));
        m_pull16(s);
    } else
        NATIVE_TIME_AS(s, 0x008300, handler2_work(s));
    m_rts(s);
}

#define M8_X16 .p_mask = CPU_FLAG_M | CPU_FLAG_X, .p_value = CPU_FLAG_M
#define LOOSE .dont_care = DC_A | DC_X | DC_Y | DC_FLAGS

static const NativeEntry entries[] = {
    { .addr = 0x008100, .name = "dispatcher", .fn = dispatcher, M8_X16, LOOSE, .cost_milli = 5000 },
    { .addr = 0x008200, .name = "handler1", .fn = handler1, M8_X16, LOOSE, .cost_milli = 40000 },
    { .addr = 0x008300, .name = "handler2", .fn = handler2, M8_X16, LOOSE, .cost_milli = 3000 },
    { .addr = 0x008400, .name = "handler3", .fn = handler3, M8_X16, LOOSE, .cost_milli = 3000 },
};

/* One run of `frames` frames in shadow mode; the report in `out`. */
static void run(int frames, char *out, size_t n)
{
    static uint8_t rom[ROM_SIZE];
    for (size_t k = 0; k < sizeof program / sizeof program[0]; k++)
        memcpy(rom + program[k].at, program[k].code, program[k].len);
    rom[0xFFFC] = 0x00, rom[0xFFFD] = 0x80;
    if (sibling_run)
        rom[SECOND_CALL + 2] = HANDLER3 >> 8;
    static Snes s;
    snes_init(&s, rom, sizeof rom);
    s.interpreter = cpu_step;
    snes_power_on(&s);
    NativeRuntime *rt = native_create(entries, (int)(sizeof entries / sizeof entries[0]), NATIVE_SHADOW);
    native_set_timing_report(rt, true);
    native_attach(rt, &s);
    for (int f = 0; f < frames; f++)
        snes_run_frame(&s, 0);
    FILE *tmp = tmpfile();
    native_report(rt, tmp ? tmp : stdout);
    out[0] = 0;
    if (tmp) {
        rewind(tmp);
        size_t got = fread(out, 1, n - 1, tmp);
        out[got] = 0;
        fclose(tmp);
    }
    CHECK(!native_failed(rt), "a call mismatched:\n%s", out);
    native_destroy(rt);
}

/* The number after `key` in `entry`'s part of the report (up to the next
 * entry's line), or ~0. */
static unsigned long long field(const char *report, const char *entry, const char *key)
{
    const char *e = strstr(report, entry);
    if (!e)
        return ~0ull;
    const char *end = strstr(e + 1, "\n  0");   /* the next entry's line: addresses in bank $00 */
    const char *k = strstr(e, key);
    if (!k || (end && k > end))
        return ~0ull;
    return strtoull(k + strlen(key), NULL, 10);
}

int main(void)
{
    static char report[1 << 16];
    run(20, report, sizeof report);

    unsigned long long calls = field(report, "008100 dispatcher", "calls ");
    unsigned long long h1 = field(report, "part 008200 handler1", "native-bus ");
    unsigned long long h2 = field(report, "part 008300 handler2", "native-bus ");
    /* the first entry's totals are the dispatcher's */
    const char *totals = strstr(report, "timing: totals original ");
    unsigned long long total = totals ? strtoull(strstr(totals, "native-bus ") + strlen("native-bus "), NULL, 10) : ~0ull;
    CHECK(calls > 100, "the dispatcher ran %llu times:\n%s", calls, report);
    /* Handler 1's work is one store to work RAM (8 clocks), handler 2's five
     * loads and four stores (72); they alternate, and each call is a stretch. */
    CHECK(h1 != ~0ull && h2 != ~0ull, "the report lists both parts:\n%s", report);
    CHECK(h1 % 8 == 0 && h2 % 72 == 0 && h1 / 8 + h2 / 72 >= calls - 2 && h1 / 8 + h2 / 72 <= calls,
          "part clocks: handler1 %llu, handler2 %llu over %llu calls", h1, h2, calls);
    /* The dispatcher's own clocks: the read of $10 each call. */
    CHECK(total != ~0ull && total > h1 + h2 && (total - h1 - h2) % 8 == 0,
          "the dispatcher's totals %llu include its parts %llu + %llu", total, h1, h2);
    CHECK(field(report, "008200 handler1", "stretches ") > 40 && field(report, "008300 handler2", "stretches ") > 40,
          "the handlers are measured on their own calls:\n%s", report);
    CHECK(strstr(report, "attributions to no hand port") == NULL, "no bad tag yet:\n%s", report);

    /* A tag with no hand port: its clocks stay the port's own, and it's reported. */
    handler1_tag = 0x008250;
    run(20, report, sizeof report);
    CHECK(strstr(report, "part 008200") == NULL, "nothing attributed to handler1 now:\n%s", report);
    CHECK(strstr(report, "timing: attributions to no hand port") != NULL && strstr(report, "(last 008250)") != NULL,
          "the bad tag is reported:\n%s", report);

    /* Handler 3 run as the dispatcher's sibling: of each of its calls from
     * the dispatcher, the stretches most of whose clocks are its work -- the
     * middle one, between its two calls, and the first, where its copy and its
     * call's return address outweigh the dispatcher's read and push -- are
     * handler 3's in the report, beside its own calls' first stretches (nested
     * in the dispatcher's comparison), with the dispatcher's few clocks among
     * them a part at the dispatcher's value. The last, the dispatcher's pull
     * and return, stays the dispatcher's. */
    handler1_tag = 0x008200;
    sibling_run = true;
    run(20, report, sizeof report);
    unsigned long long dispatched = field(report, "008100 dispatcher", "calls ");
    unsigned long long d_stretches = field(report, "008100 dispatcher", "stretches ");
    unsigned long long h3_stretches = field(report, "\n  008400 handler3", "stretches ");
    CHECK(dispatched > 100 && d_stretches != ~0ull && h3_stretches != ~0ull, "the sibling run's report:\n%s", report);
    CHECK(d_stretches + 2 >= dispatched && d_stretches <= dispatched,
          "the dispatcher keeps %llu stretches over %llu calls:\n%s", d_stretches, dispatched, report);
    CHECK(h3_stretches + 2 >= dispatched / 2 * 3 && h3_stretches <= dispatched / 2 * 3 + 2,
          "handler 3 has %llu stretches over %llu dispatches:\n%s", h3_stretches, dispatched, report);
    CHECK(field(report, "\n  008400 handler3", "part 008100 dispatcher native-bus ") != ~0ull,
          "the dispatcher's clocks in handler 3's stretches are a part of them:\n%s", report);

    if (failures)
        printf("%d failure(s)\n", failures);
    else
        printf("time_parts_test: all passed\n");
    return failures ? 1 : 0;
}
