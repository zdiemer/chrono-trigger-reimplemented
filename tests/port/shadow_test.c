/* Checks shadow mode's comparisons (runtime/port/native.c) on a small
 * synthetic program with hand-written native routines (no ROM needed):
 *
 * - a routine that stops at, or calls out from, an instruction the original
 *   passes more than once (a loop's head) is compared at the pass where the
 *   machines are the same, whichever it is; one that is the same at no pass is
 *   a mismatch, when the original leaves the loop or an interrupt comes;
 * - an entry the original reaches inside another routine's compared stretch
 *   is compared when its comparison ends there, and set aside when it would
 *   outlast it, so it can't take over the call the enclosing routine's
 *   comparison waits for;
 * - DC_AL leaves out A's low byte alone, at a return and at a call-out, and a
 *   routine that relies on a low byte it declared dead at a call-out is caught;
 * - a stop declared with m_stop_at_own is compared without the entry's set,
 *   where m_stop_at adds it;
 * - the report describes mismatches before notes (abandoned comparisons);
 * - an interrupt entry (NativeEntry.interrupt) whose handler moves to a stack
 *   above the interrupted code's is compared to its RTI, where the stack test
 *   would end it at the move; and is not blind to the work RAM between the
 *   two stacks;
 * - stack bytes a port declares dead (native_stack_dont_care) stay uncompared
 *   at its stop, where it leaves them on the stack.
 *
 * Each case registers a few routines, runs the program for a few frames in
 * SHADOW mode and reads its counters (native_stats_named) and the mismatches
 * described (native_detail). Exits non-zero on failure.
 *
 *   shadow_test */
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

/* ---- the program ------------------------------------------------------------------ */

#define ROM_SIZE 0x10000

/* Bank $00, 8-bit A and 16-bit index registers, direct page and data bank 0.
 * The main loop calls each routine in turn; $30 turns the long loop on, $31 is
 * written to NMITIMEN, $20 counts the loop's passes. */
static const struct {
    uint16_t at;
    uint8_t bytes[28];
    uint8_t n;
} code[] = {
    /* reset */
    { 0x8000, { 0x78, 0x18, 0xFB, 0xC2, 0x10, 0xE2, 0x20, 0xA2, 0xFF, 0x01, 0x9A, 0xA5, 0x31, 0x8D, 0x00, 0x42 }, 16 },
    /* 8010 main: JSR each routine; the long loop when $30 is set; INC $20; BRA 8010 */
    { 0x8010, { 0x20, 0x00, 0x81, 0x20, 0x20, 0x81, 0x20, 0x00, 0x82, 0x20, 0x02, 0x82, 0x20, 0x00, 0x83,
                0x20, 0x20, 0x83, 0x20, 0x00, 0x84, 0xA5, 0x30, 0xF0 }, 24 },
    { 0x8028, { 0x03, 0x20, 0x00, 0x85, 0xE6, 0x20, 0x80, 0xE0 }, 8 },
    /* 8100 LDX #0 : 8103 INC $10 : INX : CPX #5 : BNE 8103 : RTS */
    { 0x8100, { 0xA2, 0x00, 0x00, 0xE6, 0x10, 0xE8, 0xE0, 0x05, 0x00, 0xD0, 0xF8, 0x60 }, 12 },
    /* 8120 LDX #0 : 8123 JSR 8180 : INX : CPX #3 : BNE 8123 : RTS */
    { 0x8120, { 0xA2, 0x00, 0x00, 0x20, 0x80, 0x81, 0xE8, 0xE0, 0x03, 0x00, 0xD0, 0xF7, 0x60 }, 13 },
    /* 8180 INC $12 : RTS */
    { 0x8180, { 0xE6, 0x12, 0x60 }, 3 },
    /* 8200 INC $14 : 8202 INC $16 : JSR 8180 : RTS */
    { 0x8200, { 0xE6, 0x14, 0xE6, 0x16, 0x20, 0x80, 0x81, 0x60 }, 8 },
    /* 8300 LDA #5 : STA $18 : REP #$20 : LDA #$1234 : SEP #$20 : LDA #7 : RTS (A = $1207) */
    { 0x8300, { 0xA9, 0x05, 0x85, 0x18, 0xC2, 0x20, 0xA9, 0x34, 0x12, 0xE2, 0x20, 0xA9, 0x07, 0x60 }, 14 },
    /* 8320 LDA #3 : JSR 8180 : STA $1C : RTS */
    { 0x8320, { 0xA9, 0x03, 0x20, 0x80, 0x81, 0x85, 0x1C, 0x60 }, 8 },
    /* 8400 LDY #9 : LDA $20 : AND #1 : BNE 840A : RTS : 840A JMP 8480 */
    { 0x8400, { 0xA0, 0x09, 0x00, 0xA5, 0x20, 0x29, 0x01, 0xD0, 0x01, 0x60, 0x4C, 0x80, 0x84 }, 13 },
    /* 8480 STY $22 : RTS (reads Y) */
    { 0x8480, { 0x84, 0x22, 0x60 }, 3 },
    /* 8500 LDX #0 : 8503 INX : CPX #$FFFF : BNE 8503 : RTS (longer than a frame) */
    { 0x8500, { 0xA2, 0x00, 0x00, 0xE8, 0xE0, 0xFF, 0xFF, 0xD0, 0xFA, 0x60 }, 10 },
    /* 8600 the NMI handler: RTI */
    { 0x8600, { 0x40 }, 1 },
    /* 8700 the NMI handler that moves the stack (NMI_MOVES): PHX : PHA : PEA #0 : PLB : PLB : TSX : STX $40 :
     * LDX #$0FFF : TXS : 870E INC $42 : 8710 JSR 8780 : LDX $40 : TXS : PLA : PLX : RTI */
    { 0x8700, { 0xDA, 0x48, 0xF4, 0x00, 0x00, 0xAB, 0xAB, 0xBA, 0x86, 0x40, 0xA2, 0xFF, 0x0F, 0x9A, 0xE6, 0x42, 0x20,
                0x80, 0x87, 0xA6, 0x40, 0x9A, 0x68, 0xFA, 0x40 }, 25 },
    /* 8780 INC $44 : RTS */
    { 0x8780, { 0xE6, 0x44, 0x60 }, 3 },
};

enum { NMI_RETURNS = 0x86, NMI_MOVES = 0x87 };

static uint8_t *make_rom(uint8_t nmi_page)
{
    uint8_t *rom = malloc(ROM_SIZE);
    memset(rom, 0xDB, ROM_SIZE);   /* STP */
    for (size_t i = 0; i < sizeof code / sizeof code[0]; i++)
        memcpy(rom + code[i].at, code[i].bytes, code[i].n);
    rom[0xFFFC] = 0x00, rom[0xFFFD] = 0x80;   /* reset (emulation mode) */
    rom[0xFFEA] = 0x00, rom[0xFFEB] = nmi_page;   /* NMI (native mode) */
    return rom;
}

/* ---- native routines ----------------------------------------------------------------- */

static uint8_t dp(Snes *s, uint8_t a) { return m_read8(s, m_dp(s, a)); }
static void inc_dp(Snes *s, uint8_t a, uint8_t by)
{
    uint8_t v = (uint8_t)(dp(s, a) + by);
    m_write8(s, m_dp(s, a), v);
    m_set_nz8(s, v);
}

enum { LOOP_HEAD = 0x008103, CALL_HEAD = 0x008123, CALLEE = 0x008180, READS_Y = 0x008480, LONG_HEAD = 0x008503 };
enum { LOOP_DEAD = DC_N | DC_Z | DC_C };

/* 8100 after `passes` passes of its loop, stopping at the head. */
static void loop_after(Snes *s, uint16_t passes, uint8_t extra)
{
    m_ldx16(s, 0);
    if (passes) {
        inc_dp(s, 0x10, (uint8_t)(passes + extra));
        m_ldx16(s, passes);
        m_cmp16(s, passes, 5);
    }
    m_stop_at(s, LOOP_HEAD, LOOP_DEAD);
}
static void loop_first(Snes *s) { loop_after(s, 0, 0); }
static void loop_third(Snes *s) { loop_after(s, 2, 0); }
static void loop_wrong(Snes *s) { loop_after(s, 2, 1); }

/* 8120: the first two passes' calls inlined, the third called out. */
static void call_third(Snes *s)
{
    inc_dp(s, 0x12, 2);
    m_ldx16(s, 2);
    m_cmp16(s, 2, 3);
    m_jsr(s, CALL_HEAD, CALLEE);
    m_ldx16(s, 3);
    m_cmp16(s, 3, 3);
    m_rts(s);
}

/* 8180 as an entry of its own */
static void callee(Snes *s)
{
    inc_dp(s, 0x12, 1);
    m_rts(s);
}

/* 8200 and, in the middle of its path, 8202. */
static void path_whole(Snes *s)
{
    inc_dp(s, 0x14, 1);
    inc_dp(s, 0x16, 1);
    m_jsr(s, 0x008204, CALLEE);
    m_rts(s);
}
static void path_middle(Snes *s)
{
    inc_dp(s, 0x16, 1);
    m_jsr(s, 0x008204, CALLEE);
    m_rts(s);
}

/* 8300 leaving A's low byte or its high byte wrong. */
static void ret_a(Snes *s, uint16_t a)
{
    m_write8(s, m_dp(s, 0x18), 5);
    s->cpu.a = a;
    m_set_nz8(s, 7);
    m_rts(s);
}
static void ret_a_low_wrong(Snes *s) { ret_a(s, 0x1299); }
static void ret_a_high_wrong(Snes *s) { ret_a(s, 0x9907); }

/* 8320 with A's low byte wrong at the call, declared dead there (the callee
 * doesn't read it); the second relies on it after the call. */
static void call_low_ok(Snes *s)
{
    m_set_a8(s, 0x55);
    m_jsr_dc(s, 0x008322, CALLEE, DC_AL);
    m_set_a8(s, 3);
    m_write8(s, m_dp(s, 0x1C), 3);
    m_rts(s);
}
static void call_low_relies(Snes *s)
{
    m_set_a8(s, 0x55);
    m_jsr_dc(s, 0x008322, CALLEE, DC_AL);
    m_write8(s, m_dp(s, 0x1C), (uint8_t)s->cpu.a);
    m_rts(s);
}

/* 8400: Y is dead after it returns (its entry declares it), but read where it
 * leaves early. */
static void leave(Snes *s, uint16_t y, bool own)
{
    m_set_index(s, &s->cpu.y, y);
    uint8_t bit = dp(s, 0x20) & 0x01;
    m_lda8(s, bit);
    if (!bit) {
        m_rts(s);
        return;
    }
    if (own)
        m_stop_at_own(s, READS_Y, 0);
    else
        m_stop_at(s, READS_Y, 0);
}
static void leave_own(Snes *s) { leave(s, 9, true); }
static void leave_own_wrong(Snes *s) { leave(s, 8, true); }
static void leave_shared_wrong(Snes *s) { leave(s, 8, false); }

/* 8500, a loop longer than a frame. */
static void long_after(Snes *s, uint16_t passes, bool wrong)
{
    if (wrong)
        m_write8(s, m_dp(s, 0x24), 1);
    m_ldx16(s, passes);
    m_cmp16(s, passes, 0xFFFF);
    m_stop_at(s, LONG_HEAD, LOOP_DEAD);
}
static void long_fourth(Snes *s) { long_after(s, 3, false); }
static void long_wrong(Snes *s) { long_after(s, 3, true); }

/* 8700, the handler that moves the stack: the interrupted X and A kept on the
 * interrupted stack, the data bank 0 (through two bytes pushed and pulled
 * there, which the port leaves alone), its S at $40, the handler's own stack
 * at $0FFF, $42 counted, a call to 8780, then all of it put back and RTI.
 * `by` counts $42 wrong where it isn't 1; `stray` writes a byte between the
 * two stacks that the original doesn't. */
enum { HANDLER_STACK = 0x0FFF, HANDLER_WORK = 0x00870E, HANDLER_CALL = 0x008710, HANDLER_CALLEE = 0x008780 };

static uint16_t handler_enter(Snes *s)
{
    m_push16(s, s->cpu.x);
    m_push8(s, (uint8_t)s->cpu.a);
    s->cpu.db = 0x00;
    uint16_t interrupted = s->cpu.s;
    m_write16(s, m_dp(s, 0x40), interrupted);
    s->cpu.x = HANDLER_STACK;
    s->cpu.s = HANDLER_STACK;
    m_set_nz16(s, HANDLER_STACK);
    return interrupted;
}

static void handler(Snes *s, uint8_t by, uint16_t stray)
{
    uint16_t interrupted = handler_enter(s);
    inc_dp(s, 0x42, by);
    if (stray)
        m_write8(s, stray, 1);
    m_jsr(s, HANDLER_CALL, HANDLER_CALLEE);
    s->cpu.x = interrupted;
    s->cpu.s = interrupted;
    s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00) | m_pull8(s));
    s->cpu.x = m_pull16(s);
    m_rti(s);
}
static void handler_whole(Snes *s) { handler(s, 1, 0); }
static void handler_wrong(Snes *s) { handler(s, 2, 0); }
static void handler_stray(Snes *s) { handler(s, 1, 0x0800); }

/* ...and its entry alone, stopping where its work begins on the new stack. */
static void handler_entry(Snes *s)
{
    (void)handler_enter(s);
    m_stop_at(s, HANDLER_WORK, 0);
}

/* ...and its entry alone keeping a wrong X on the interrupted stack: a
 * mismatch at the stop, unless the port declares the two bytes dead
 * (native_stack_dont_care), which holds at the stop with them still there. */
static void handler_entry_wrong_x(Snes *s, bool declared)
{
    uint16_t kept_at = (uint16_t)(s->cpu.s - 1);
    s->cpu.x ^= 0x5555;
    if (declared)
        native_stack_dont_care(s, kept_at, 2);
    (void)handler_enter(s);
    m_stop_at(s, HANDLER_WORK, 0);
}
static void handler_entry_wrong_x_dead(Snes *s) { handler_entry_wrong_x(s, true); }
static void handler_entry_wrong_x_live(Snes *s) { handler_entry_wrong_x(s, false); }

#define M8X16 .p_mask = CPU_FLAG_M | CPU_FLAG_X, .p_value = CPU_FLAG_M
#define ENTRY(address, function, ...) { .addr = address, .name = #function, .fn = function, M8X16, __VA_ARGS__ }

/* ---- running a case ------------------------------------------------------------------ */

typedef struct {
    unsigned calls, matched, mismatched, interrupted, abandoned;
} Counts;

/* What the last case's run left: each entry's counters, and the details. */
static Counts counts[8];
static const char *names[8];
static int ncounts;
static char details[4096];

static Counts counts_of(const char *name)
{
    for (int i = 0; i < ncounts; i++)
        if (!strcmp(names[i], name))
            return counts[i];
    return (Counts){ 0 };
}

/* Run the program with these routines registered, in SHADOW mode. */
static void run_case(const uint8_t *rom, const NativeEntry *table, int n, bool nmi, bool long_loop, int frames)
{
    Snes *s = malloc(sizeof *s);
    snes_init(s, rom, ROM_SIZE);
    s->interpreter = cpu_step;
    snes_power_on(s);
    memset(s->bus.wram, 0, 0x100);
    s->bus.wram[0x30] = long_loop;
    s->bus.wram[0x31] = nmi ? 0x80 : 0x00;
    NativeRuntime *rt = native_create(table, n, NATIVE_SHADOW);
    native_attach(rt, s);
    for (int i = 0; i < frames; i++)
        snes_run_frame(s, 0);
    ncounts = 0;
    for (int i = 0; i < n && i < 8; i++) {
        const NativeStats *st = native_stats_named(rt, table[i].name);
        names[ncounts] = table[i].name;
        counts[ncounts++] = (Counts){ st->calls, st->matched, st->mismatched, st->interrupted, st->abandoned };
    }
    details[0] = 0;
    size_t used = 0;
    for (int i = 0; native_detail(rt, i) && used < sizeof details; i++) {
        int n = snprintf(details + used, sizeof details - used, "%s\n", native_detail(rt, i));
        used += n > 0 ? (size_t)n : 0;
    }
    native_destroy(rt);
    free(s);
}

static void expect(const char *name, bool match)
{
    Counts c = counts_of(name);
    if (match)
        CHECK(c.matched > 0 && c.mismatched == 0, "%s: calls %u matched %u mismatched %u (expected to match)\n%s",
              name, c.calls, c.matched, c.mismatched, details);
    else
        CHECK(c.mismatched > 0, "%s: calls %u matched %u mismatched %u (expected a mismatch)\n%s", name, c.calls,
              c.matched, c.mismatched, details);
}

#define RUN(nmi, long_loop, frames, ...)                                                      \
    do {                                                                                      \
        static const NativeEntry t[] = { __VA_ARGS__ };                                       \
        run_case(rom, t, (int)(sizeof t / sizeof t[0]), nmi, long_loop, frames);              \
    } while (0)

int main(void)
{
    uint8_t *rom = make_rom(NMI_RETURNS);

    /* the program runs, and nothing registered is compared */
    RUN(true, false, 2, ENTRY(0x00FFF0, loop_first));
    CHECK(counts_of("loop_first").calls == 0, "an entry nothing reaches was called");

    /* a stop at a loop's head: the first pass, a later one, none */
    RUN(true, false, 2, ENTRY(0x008100, loop_first));
    expect("loop_first", true);
    RUN(true, false, 2, ENTRY(0x008100, loop_third));
    expect("loop_third", true);
    RUN(true, false, 2, ENTRY(0x008100, loop_wrong));
    expect("loop_wrong", false);

    /* the report describes mismatches before notes (an abandoned comparison),
     * whichever came first, so notes can't crowd out what the gate fails on */
    RUN(true, false, 2, ENTRY(0x008200, path_whole), ENTRY(0x008202, path_middle), ENTRY(0x008100, loop_wrong));
    expect("loop_wrong", false);
    CHECK(!strncmp(details, "mismatch ", 9) && strstr(details, "\nabandoned ") != NULL,
          "mismatches first, then the notes:\n%s", details);
    CHECK(strstr(details, "later passes, then the original left"), "%s", details);

    /* a call-out from a loop's head, after two passes inlined */
    RUN(true, false, 2, ENTRY(0x008120, call_third));
    expect("call_third", true);

    /* an entry in the middle of another's path, whose comparison would outlast
     * the stretch it was started in: set aside at the enclosing routine's
     * call, compared where the original reaches it by itself */
    RUN(true, false, 2, ENTRY(0x008200, path_whole), ENTRY(0x008202, path_middle));
    expect("path_whole", true);
    expect("path_middle", true);
    Counts whole = counts_of("path_whole"), middle = counts_of("path_middle");
    CHECK(middle.abandoned + 1 >= whole.calls && middle.abandoned <= whole.calls && middle.matched + 1 >= whole.calls,
          "path_middle: calls %u matched %u abandoned %u; path_whole calls %u", middle.calls, middle.matched,
          middle.abandoned, whole.calls);

    /* a callee the port inlines, registered too: compared inside the stretch
     * too, since its comparison ends there */
    RUN(true, false, 2, ENTRY(0x008120, call_third), ENTRY(0x008180, callee));
    expect("call_third", true);
    expect("callee", true);
    Counts third = counts_of("call_third"), inlined = counts_of("callee");
    CHECK(inlined.abandoned == 0 && inlined.matched >= 3 * third.matched, "callee: matched %u abandoned %u, call_third %u",
          inlined.matched, inlined.abandoned, third.matched);

    /* A's low byte alone at a return */
    RUN(true, false, 2, ENTRY(0x008300, ret_a_low_wrong, .dont_care = DC_AL));
    expect("ret_a_low_wrong", true);
    RUN(true, false, 2, ENTRY(0x008300, ret_a_high_wrong, .dont_care = DC_AL));
    expect("ret_a_high_wrong", false);
    CHECK(strstr(details, "A's high byte original 12 native 99"), "%s", details);
    RUN(true, false, 2, ENTRY(0x008300, ret_a_low_wrong));
    expect("ret_a_low_wrong", false);

    /* ...and at a call-out, where a routine that relies on it is caught */
    RUN(true, false, 2, ENTRY(0x008320, call_low_ok));
    expect("call_low_ok", true);
    RUN(true, false, 2, ENTRY(0x008320, call_low_relies));
    expect("call_low_relies", false);

    /* a stop's own set: without the entry's, which m_stop_at adds */
    RUN(true, false, 2, ENTRY(0x008400, leave_own, .dont_care = DC_Y));
    expect("leave_own", true);
    RUN(true, false, 2, ENTRY(0x008400, leave_own_wrong, .dont_care = DC_Y));
    expect("leave_own_wrong", false);
    RUN(true, false, 2, ENTRY(0x008400, leave_shared_wrong, .dont_care = DC_Y));
    expect("leave_shared_wrong", true);   /* why the check rejects DC_Y there */

    /* a loop longer than a frame, with NMIs: a later pass before the interrupt
     * counts; a routine the same at no pass is reported when it comes */
    RUN(true, true, 3, ENTRY(0x008500, long_fourth));
    expect("long_fourth", true);
    RUN(true, true, 3, ENTRY(0x008500, long_wrong));
    expect("long_wrong", false);
    CHECK(strstr(details, "later passes, then an interrupt"), "%s", details);

    free(rom);

    /* an interrupt entry whose handler moves to a stack above the interrupted
     * code's: compared to its RTI (without the flag, the stack's rise ends the
     * comparison at the move, before the call), where a wrong count and a
     * stray write between the two stacks are caught; and its entry alone,
     * stopped on the stack it moved to */
    rom = make_rom(NMI_MOVES);
    RUN(true, false, 3, ENTRY(0x008700, handler_whole, .interrupt = true));
    expect("handler_whole", true);
    Counts handled = counts_of("handler_whole");
    CHECK(handled.calls >= 2 && handled.matched == handled.calls, "handler_whole: calls %u matched %u\n%s",
          handled.calls, handled.matched, details);
    RUN(true, false, 3, ENTRY(0x008700, handler_whole));
    expect("handler_whole", false);
    CHECK(strstr(details, "original returned without the call to 008780"), "%s", details);
    RUN(true, false, 3, ENTRY(0x008700, handler_wrong, .interrupt = true));
    expect("handler_wrong", false);
    RUN(true, false, 3, ENTRY(0x008700, handler_stray, .interrupt = true));
    expect("handler_stray", false);
    CHECK(strstr(details, "0800"), "the stray write between the stacks: %s", details);
    RUN(true, false, 3, ENTRY(0x008700, handler_entry, .interrupt = true));
    expect("handler_entry", true);
    RUN(true, false, 3, ENTRY(0x008700, handler_entry_wrong_x_dead, .interrupt = true));
    expect("handler_entry_wrong_x_dead", true);
    RUN(true, false, 3, ENTRY(0x008700, handler_entry_wrong_x_live, .interrupt = true));
    expect("handler_entry_wrong_x_live", false);
    free(rom);

    if (failures) {
        printf("shadow_test: %d failures\n", failures);
        return 1;
    }
    printf("shadow_test: all checks passed\n");
    return 0;
}
