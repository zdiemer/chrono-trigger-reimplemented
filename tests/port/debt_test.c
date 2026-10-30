/* Checks how the shipping mode (NATIVE_ON) pays a port's time when an
 * interrupt comes due while it is paid (runtime/port/native.c, pay_debt), on a
 * small synthetic program (no ROM needed):
 *
 * - a port that owes more than a frame, with a vblank handler that moves the
 *   stack above the port's (as the menus' does, to $1DFD): the handler taken
 *   at the port's boundary runs to its return on time, and the next vblank's
 *   is taken at the boundary again, never inside the last one's handler. Paid
 *   inside the handler, the port's time held interrupts there until the next
 *   vblank nested in it, and its stack switch overwrote the S the outer one
 *   had saved (docs/PORTING.md, "A port that spans frames");
 * - the same with a handler that stays on the interrupted stack;
 * - a handler that never comes back to the boundary (it restarts the main
 *   loop, so the work is never finished, as in the original): the program
 *   still runs and every vblank is handled;
 * - a port that loads with the frame's interrupt off and turns it on as it
 *   ends (the world map's set up, $C2:23A8): no vblank is handled before the
 *   port's time has passed. Turned on as the port's run ends, ahead of the
 *   time it is charged, the frame's handler ran through the whole of that
 *   time, a frame at a time, and the world map's opening ran 41 frames ahead
 *   of the original's (docs/TODO.md, "Long movies desync").
 *
 * Exits non-zero on failure.
 *
 *   debt_test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "port/machine.h"
#include "port/native.h"
#include "platform/timing.h"
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

/* Bank $00, 8-bit A and 16-bit index registers. The main loop calls the port
 * at $8100 and counts its passes in $20. The handlers count vblanks in $40,
 * their depth in $42, and a handler entered inside another in $44. */
static const struct {
    uint16_t at;
    uint8_t bytes[32];
    uint8_t n;
} code[] = {
    /* reset: SEI, native mode, REP #$10, SEP #$20, S = $01FF, NMI on */
    { 0x8000, { 0x78, 0x18, 0xFB, 0xC2, 0x10, 0xE2, 0x20, 0xA2, 0xFF, 0x01, 0x9A, 0xA9, 0x80, 0x8D, 0x00, 0x42 }, 16 },
    /* 8010 main: JSR 8100 : INC $20 : BRA 8010 */
    { 0x8010, { 0x20, 0x00, 0x81, 0xE6, 0x20, 0x80, 0xF9 }, 7 },
    /* 8100 the original of the port: RTS */
    { 0x8100, { 0x60 }, 1 },
    /* 8600 a handler on a stack of its own: INC $42 : LDA $42 : CMP #2 : BCC +2 : INC $44 :
     * TSX : STX $1FFE : LDX #$1F00 : TXS : INC $40 : LDX $1FFE : TXS : DEC $42 : RTI */
    { 0x8600, { 0xE6, 0x42, 0xA5, 0x42, 0xC9, 0x02, 0x90, 0x02, 0xE6, 0x44, 0xBA, 0x8E, 0xFE, 0x1F, 0xA2, 0x00,
                0x1F, 0x9A, 0xE6, 0x40, 0xAE, 0xFE, 0x1F, 0x9A, 0xC6, 0x42, 0x40 }, 27 },
    /* 8700 a handler on the interrupted stack: INC $42 : LDA $42 : CMP #2 : BCC +2 : INC $44 :
     * INC $40 : DEC $42 : RTI */
    { 0x8700, { 0xE6, 0x42, 0xA5, 0x42, 0xC9, 0x02, 0x90, 0x02, 0xE6, 0x44, 0xE6, 0x40, 0xC6, 0x42, 0x40 }, 15 },
    /* 8800 a handler that never returns: INC $40 : LDX #$01FF : TXS : JMP 8010 */
    { 0x8800, { 0xE6, 0x40, 0xA2, 0xFF, 0x01, 0x9A, 0x4C, 0x10, 0x80 }, 9 },
    /* 8900 reset with the frame's interrupt off: SEI, native mode, REP #$10, SEP #$20, S = $01FF,
     * STZ $4200, JSR 8120 (the port that turns it on) */
    { 0x8900, { 0x78, 0x18, 0xFB, 0xC2, 0x10, 0xE2, 0x20, 0xA2, 0xFF, 0x01, 0x9A, 0x9C, 0x00, 0x42, 0x20, 0x20,
                0x81 }, 17 },
    /* 8911 then: INC $20 : BRA 8911 */
    { 0x8911, { 0xE6, 0x20, 0x80, 0xFC }, 4 },
    /* 8120 the original of the port that turns the interrupt on: RTS */
    { 0x8120, { 0x60 }, 1 },
};

static uint8_t *make_rom(uint16_t nmi, uint16_t reset)
{
    uint8_t *rom = malloc(ROM_SIZE);
    memset(rom, 0xDB, ROM_SIZE);   /* STP */
    for (size_t i = 0; i < sizeof code / sizeof code[0]; i++)
        memcpy(rom + code[i].at, code[i].bytes, code[i].n);
    rom[0xFFFC] = (uint8_t)reset, rom[0xFFFD] = (uint8_t)(reset >> 8);   /* reset (emulation mode) */
    rom[0xFFEA] = (uint8_t)nmi, rom[0xFFEB] = (uint8_t)(nmi >> 8);   /* NMI (native mode) */
    return rom;
}

/* The port: a hundred reads of work RAM, at a rate that makes them about a
 * frame and a half of the original's time. */
static void long_work(Snes *s)
{
    for (int i = 0; i < 100; i++)
        (void)m_read8(s, 0x7E0100 + (uint32_t)i);
    m_rts(s);
}

enum { FRAMES = 12 };
#define FRAME_CLOCKS ((uint64_t)SNES_LINES_PER_FRAME * SNES_CLOCKS_PER_LINE)

static void run_case(const char *what, uint16_t nmi, bool returns)
{
    uint8_t *rom = make_rom(nmi, 0x8000);
    Snes *s = malloc(sizeof *s);
    snes_init(s, rom, ROM_SIZE);
    s->interpreter = cpu_step;
    snes_power_on(s);
    memset(s->bus.wram, 0, 0x100);
    static const NativeEntry table[] = {
        { .addr = 0x008100, .name = "long_work", .fn = long_work, .p_mask = 0x30, .p_value = 0x20,
          .cost_milli = 700000 },
    };
    NativeRuntime *rt = native_create(table, 1, NATIVE_ON);
    native_attach(rt, s);
    for (int i = 0; i < FRAMES; i++)
        snes_run_frame(s, 0);
    const uint8_t *w = s->bus.wram;
    CHECK(!s->cpu.stopped, "%s: the program ran into the ROM's fill (STP) at $%02X:%04X", what, s->cpu.pb, s->cpu.pc);
    CHECK(w[0x44] == 0, "%s: %u vblank handler(s) entered inside another", what, w[0x44]);
    CHECK(w[0x40] >= FRAMES - 2, "%s: %u vblanks handled in %d frames", what, w[0x40], FRAMES);
    if (returns) {
        CHECK(w[0x20] >= 3, "%s: the main loop passed %u times (the port's time held it)", what, w[0x20]);
        CHECK(native_owed(rt) < 2 * FRAME_CLOCKS, "%s: %llu clocks still owed", what,
              (unsigned long long)native_owed(rt));
    }
    native_destroy(rt);
    free(s);
    free(rom);
}

/* The port that loads with the frame's interrupt off: the same work, then
 * the interrupt turned on. */
static void work_then_turn_the_interrupt_on(Snes *s)
{
    for (int i = 0; i < 100; i++)
        (void)m_read8(s, 0x7E0100 + (uint32_t)i);
    tm_enable_interrupts(m_platform(s), PF_AT(0x008120), TM_INT_VBLANK);
    m_rts(s);
}

static void run_interrupt_turned_on(void)
{
    const char *what = "a port that turns the frame's interrupt on as it ends";
    uint8_t *rom = make_rom(0x8700, 0x8900);
    Snes *s = malloc(sizeof *s);
    snes_init(s, rom, ROM_SIZE);
    s->interpreter = cpu_step;
    snes_power_on(s);
    memset(s->bus.wram, 0, 0x100);
    static const NativeEntry table[] = {
        { .addr = 0x008120, .name = "work_then_turn_the_interrupt_on", .fn = work_then_turn_the_interrupt_on,
          .p_mask = 0x30, .p_value = 0x20, .cost_milli = 700000 },
    };
    NativeRuntime *rt = native_create(table, 1, NATIVE_ON);
    native_attach(rt, s);
    /* The port's time is about a frame and a half: the first vblank comes
     * while it is still being paid, with the interrupt off, and the second
     * after it (a frame's run ends as its vblank starts, before the handler). */
    snes_run_frame(s, 0);
    snes_run_frame(s, 0);
    const uint8_t *w = s->bus.wram;
    CHECK(w[0x40] == 0, "%s: %u vblank(s) handled inside the port's time", what, w[0x40]);
    for (int i = 2; i < FRAMES; i++)
        snes_run_frame(s, 0);
    CHECK(!s->cpu.stopped, "%s: the program ran into the ROM's fill (STP) at $%02X:%04X", what, s->cpu.pb, s->cpu.pc);
    CHECK(w[0x40] >= FRAMES - 3, "%s: %u vblanks handled in %d frames", what, w[0x40], FRAMES);
    CHECK(w[0x20] >= 3, "%s: the main loop passed %u times", what, w[0x20]);
    native_destroy(rt);
    free(s);
    free(rom);
}

int main(void)
{
    run_case("a handler on a stack of its own", 0x8600, true);
    run_case("a handler on the interrupted stack", 0x8700, true);
    run_case("a handler that never returns", 0x8800, false);
    run_interrupt_turned_on();
    if (failures)
        printf("%d failure(s)\n", failures);
    else
        printf("debt_test: all passed\n");
    return failures ? 1 : 0;
}
