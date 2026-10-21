/* Checks how the runtime holds a hand port that waits across the end of a
 * frame or across another task's turn (runtime/port/native.c; docs/MOVIES.md,
 * "Suspended routines and owed time"), on two small synthetic programs (no ROM
 * needed):
 *
 * - two tasks on stacks of their own, switched by a routine of the original's
 *   that keeps the running task's S in its record and takes the other's
 *   (as the menus' task switch, $C2:821E, does). Task A calls a routine the
 *   port stands for, which counts, yields to task B in the middle and counts
 *   again; task B's stack lies above A's, which once had the runtime take the
 *   switch for the callee unwinding past its return and abandon the port.
 *   - the port is parked across the switch and goes on when its task does, in
 *     the shipping mode and in shadow mode (compared, no mismatch), and counts
 *     as suspended while it waits, since no save state can hold it;
 *   - dropped where its task can't go on (the program's answer, NativeGoesOn,
 *     says no, for a frame): nothing is left suspended for good, the
 *     original's code runs the rest when the task goes on after all, and the
 *     waits made after it go on again;
 *   - dropped where its task starts afresh and yields again from the same
 *     place on the same stack: the new wait takes its place;
 *   - saved while it is parked (the player's PLAYER_SAVE_HANDS_BACK_A_STEP):
 *     the state holds the original's machine at the switch, so loaded into a
 *     fresh machine and runtime the original's code finishes the step from
 *     the yield's return and the tasks play on, the port running again from
 *     its next call, in the shipping mode and in shadow mode (compared, no
 *     mismatch).
 * - the menus' own answer whether a task can go on from a wait
 *   (menu_task_goes_on_from, game/menu/tasks.c) over the tasks' records: a
 *   task waiting there, with its index registers kept either width, goes on;
 *   one waiting elsewhere, stopped, or running over the wait from its start
 *   doesn't; the task the scheduler is taking back into the wait does.
 * - a main loop that waits for its vblank handler in a loop with an idle entry,
 *   the wait a port makes as native_wait_loop_held: in the shipping mode
 *   nothing is suspended at any frame's end, and a save state taken there loads
 *   into a fresh machine and runtime and plays on exactly as the run it was
 *   taken from; shadow mode compares the port across the wait. The same port
 *   waiting with native_wait_loop is suspended at frame ends, which is what
 *   the held wait is for.
 *
 * Exits non-zero on failure.
 *
 *   suspend_test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "menu/menu.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "snes/system.h"
#include "vars.h"

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

typedef struct {
    uint16_t at;
    uint8_t bytes[32];
    uint8_t n;
} Code;

static uint8_t *make_rom(const Code *code, size_t n, uint16_t nmi)
{
    uint8_t *rom = malloc(ROM_SIZE);
    memset(rom, 0xDB, ROM_SIZE);   /* STP */
    for (size_t i = 0; i < n; i++)
        memcpy(rom + code[i].at, code[i].bytes, code[i].n);
    rom[0xFFFC] = 0x00, rom[0xFFFD] = 0x80;   /* reset (emulation mode) */
    rom[0xFFEA] = (uint8_t)nmi, rom[0xFFEB] = (uint8_t)(nmi >> 8);
    return rom;
}

static Snes *machine(const uint8_t *rom)
{
    Snes *s = malloc(sizeof *s);
    snes_init(s, rom, ROM_SIZE);
    s->interpreter = cpu_step;
    snes_power_on(s);
    return s;
}

enum { P_M8_X16 = 0x20 };

/* ---- Two tasks and a yield ------------------------------------------------------------------ */

/* Bank $00, 8-bit A and 16-bit index registers. Task records at $10 (A's S)
 * and $12 (B's), the running task at $14 (0 or 2). Task A loops calling
 * $8300, which counts in $20, switches tasks from $8302 and counts in $21;
 * task B counts its turns in $30. Whether task A can still go on is the
 * test's to say (a_goes_on): the program's own answer would be in its memory,
 * which a test that changed it from outside would change under shadow mode's
 * comparisons. */
enum {
    YIELD_SITE = 0x008302, SWITCH = 0x008200, COUNT_BEFORE = 0x20, COUNT_AFTER = 0x21, B_TURNS = 0x30,
    A_RECORD = 0x10, B_RECORD = 0x12, A_TOP = 0x03FF, B_TOP = 0x04FF,
};

static const Code tasks_code[] = {
    /* reset: SEI, native mode, REP #$10, SEP #$20, S = A's top, task A running, JMP 8100 */
    { 0x8000, { 0x78, 0x18, 0xFB, 0xC2, 0x10, 0xE2, 0x20, 0xA2, 0xFF, 0x03, 0x9A, 0x64, 0x14, 0x64, 0x15, 0x4C, 0x00,
                0x81 }, 18 },
    /* 8100 task A: JSR 8300 : BRA 8100 */
    { 0x8100, { 0x20, 0x00, 0x83, 0x80, 0xFB }, 5 },
    /* 8180 task B: INC $30 : JSR 8200 : BRA 8180 */
    { 0x8180, { 0xE6, 0x30, 0x20, 0x00, 0x82, 0x80, 0xF9 }, 7 },
    /* 8200 the switch: PHP : REP #$20 : TSC : LDX $14 : STA $10,X : TXA : EOR #2 : TAX : STX $14 :
     * LDA $10,X : TCS : SEP #$20 : PLP : RTS */
    { 0x8200, { 0x08, 0xC2, 0x20, 0x3B, 0xA6, 0x14, 0x95, 0x10, 0x8A, 0x49, 0x02, 0x00, 0xAA, 0x86, 0x14, 0xB5, 0x10,
                0x1B, 0xE2, 0x20, 0x28, 0x60 }, 22 },
    /* 8300 the routine the port stands for: INC $20 : JSR 8200 : INC $21 : RTS */
    { 0x8300, { 0xE6, 0x20, 0x20, 0x00, 0x82, 0xE6, 0x21, 0x60 }, 8 },
};

/* A task's record and stack as the switch leaves them, for it to go on at
 * `resume` (pushed as a JSR's return, less one) with P. */
static void set_task_waiting(Snes *s, uint16_t record, uint16_t top, uint16_t resume)
{
    uint8_t *w = s->bus.wram;
    uint16_t kept = (uint16_t)(top - 3);
    w[top] = (uint8_t)((resume - 1) >> 8);
    w[top - 1] = (uint8_t)(resume - 1);
    w[top - 2] = P_M8_X16;
    w[record] = (uint8_t)kept;
    w[record + 1] = (uint8_t)(kept >> 8);
}

static bool a_goes_on = true;

static bool task_a_goes_on(Snes *s, uint16_t s_after)
{
    (void)s;
    (void)s_after;
    return a_goes_on;
}

/* The port: the count, the yield, the count again. */
static void count_and_yield(Snes *s)
{
    m_write8(s, COUNT_BEFORE, (uint8_t)(m_read8(s, COUNT_BEFORE) + 1));
    native_yield_call_dc(s, YIELD_SITE, SWITCH, false, DC_A | DC_X | DC_Y | DC_FLAGS, task_a_goes_on);
    m_write8(s, COUNT_AFTER, (uint8_t)(m_read8(s, COUNT_AFTER) + 1));
    m_rts(s);
}

static const NativeEntry tasks_table[] = {
    { .addr = 0x008300, .name = "count_and_yield", .fn = count_and_yield, .p_mask = 0x30, .p_value = P_M8_X16,
      .cost_milli = 3000, .dont_care = DC_A | DC_X | DC_Y | DC_FLAGS },
};

typedef enum { RUN_THROUGH, DROP_THE_TASK, RESTART_THE_TASK } TaskCase;

static void tasks_case(const char *what, NativeMode mode, TaskCase how)
{
    uint8_t *rom = make_rom(tasks_code, sizeof tasks_code / sizeof tasks_code[0], 0x8000);
    Snes *s = machine(rom);
    memset(s->bus.wram, 0, 0x600);
    a_goes_on = true;
    set_task_waiting(s, B_RECORD, B_TOP, 0x8180);
    NativeRuntime *rt = native_create(tasks_table, 1, mode);
    native_attach(rt, s);
    enum { FRAMES = 4, PARKED_MIDWAY = 2 };
    bool parked_seen = false;
    for (int i = 0; i < FRAMES; i++) {
        snes_run_frame(s, 0);
        if (native_parked(rt))
            parked_seen = true;
        /* task A can't go on for a frame: the waits it makes then are dropped */
        if (how == DROP_THE_TASK)
            a_goes_on = i != PARKED_MIDWAY - 1;
        if (i == PARKED_MIDWAY - 1 && how == RESTART_THE_TASK && native_parked(rt) == 1) {
            /* task A started afresh at its loop's head while its port waits */
            uint16_t s_a = (uint16_t)(s->bus.wram[A_RECORD] | s->bus.wram[A_RECORD + 1] << 8);
            CHECK(s_a < A_TOP && s_a > A_TOP - 0x10, "%s: task A isn't waiting on its stack (S %04X)", what, s_a);
            set_task_waiting(s, A_RECORD, A_TOP, 0x8100);
        }
    }
    const uint8_t *w = s->bus.wram;
    const NativeStats *st = native_stats_named(rt, "count_and_yield");
    CHECK(!s->cpu.stopped, "%s: ran into the ROM's fill at $%02X:%04X", what, s->cpu.pb, s->cpu.pc);
    CHECK(w[B_TURNS] > 10, "%s: task B took %u turns", what, w[B_TURNS]);
    CHECK(w[COUNT_BEFORE] == w[COUNT_AFTER] || w[COUNT_BEFORE] == (uint8_t)(w[COUNT_AFTER] + 1),
          "%s: counted %u before the yield and %u after", what, w[COUNT_BEFORE], w[COUNT_AFTER]);
    CHECK(parked_seen, "%s: the port was never parked at a frame's end", what);
    CHECK(native_suspended(rt) == native_parked(rt), "%s: %d suspended, %d of them parked", what,
          native_suspended(rt), native_parked(rt));
    if (how == RUN_THROUGH)
        CHECK(st->abandoned == 0, "%s: %u calls abandoned", what, st->abandoned);
    if (how == DROP_THE_TASK)
        CHECK(st->abandoned >= 1 && st->abandoned < st->calls, "%s: %u of %u calls abandoned", what, st->abandoned,
              st->calls);
    if (how == RESTART_THE_TASK) {
        CHECK(st->abandoned == 1, "%s: %u calls abandoned, not the one its fresh start replaced", what, st->abandoned);
        CHECK(native_parked(rt) <= 1, "%s: %d parked", what, native_parked(rt));
    }
    if (mode == NATIVE_SHADOW)
        CHECK(st->mismatched == 0 && st->matched > 0 && !native_failed(rt), "%s: %u matched, %u mismatched (%s)",
              what, st->matched, st->mismatched, native_detail(rt, 0) ? native_detail(rt, 0) : "");
    native_destroy(rt);
    free(s);
    free(rom);
}

/* Saved at the first frame's end with the port parked in its yield, and the
 * state loaded into a fresh machine with a fresh runtime in `loaded_mode`. */
static void parked_save_case(const char *what, NativeMode loaded_mode)
{
    uint8_t *rom = make_rom(tasks_code, sizeof tasks_code / sizeof tasks_code[0], 0x8000);
    Snes *s = machine(rom);
    memset(s->bus.wram, 0, 0x600);
    a_goes_on = true;
    set_task_waiting(s, B_RECORD, B_TOP, 0x8180);
    NativeRuntime *rt = native_create(tasks_table, 1, NATIVE_ON);
    native_attach(rt, s);
    enum { FRAMES = 20, AFTER = 6 };
    for (int i = 0; i < FRAMES && !(native_parked(rt) == 1 && native_suspended(rt) == 1); i++)
        snes_run_frame(s, 0);
    const uint8_t *w = s->bus.wram;
    CHECK(native_parked(rt) == 1 && native_suspended(rt) == 1, "%s: no frame ended with only the port parked", what);
    CHECK(w[COUNT_BEFORE] == (uint8_t)(w[COUNT_AFTER] + 1), "%s: parked with %u counted before the yield, %u after",
          what, w[COUNT_BEFORE], w[COUNT_AFTER]);
    uint8_t after = w[COUNT_AFTER], b_turns = w[B_TURNS];
    size_t len;
    uint8_t *state = snes_state_write(s, &len);
    CHECK(state != NULL, "%s: the state wasn't written", what);

    Snes *copy = machine(rom);
    char err[200] = "";
    CHECK(state && snes_state_read(copy, state, len, err, sizeof err) == 0, "%s: the state didn't load: %s", what, err);
    NativeRuntime *fresh = native_create(tasks_table, 1, loaded_mode);
    native_attach(fresh, copy);
    for (int i = 0; i < AFTER; i++)
        snes_run_frame(copy, 0);
    const uint8_t *c = copy->bus.wram;
    const NativeStats *st = native_stats_named(fresh, "count_and_yield");
    CHECK(!copy->cpu.stopped, "%s: ran into the ROM's fill at $%02X:%04X", what, copy->cpu.pb, copy->cpu.pc);
    CHECK((uint8_t)(c[COUNT_AFTER] - after) > 1 && (uint8_t)(c[B_TURNS] - b_turns) > 1,
          "%s: loaded, A finished %u steps and B took %u turns", what, (uint8_t)(c[COUNT_AFTER] - after),
          (uint8_t)(c[B_TURNS] - b_turns));
    CHECK(c[COUNT_BEFORE] == c[COUNT_AFTER] || c[COUNT_BEFORE] == (uint8_t)(c[COUNT_AFTER] + 1),
          "%s: loaded, counted %u before the yield and %u after", what, c[COUNT_BEFORE], c[COUNT_AFTER]);
    CHECK(st->calls > 0 && st->abandoned == 0, "%s: loaded, the port ran %u calls, %u abandoned", what, st->calls,
          st->abandoned);
    if (loaded_mode == NATIVE_SHADOW)
        CHECK(st->mismatched == 0 && st->matched > 0 && !native_failed(fresh), "%s: %u matched, %u mismatched (%s)",
              what, st->matched, st->mismatched, native_detail(fresh, 0) ? native_detail(fresh, 0) : "");
    free(state);
    native_destroy(fresh);
    native_destroy(rt);
    free(copy);
    free(s);
    free(rom);
}

/* ---- The menus' answer whether a task goes on -------------------------------------------------- */

/* The menus' task records (game/menu/tasks.c): the third task's stack, 64
 * bytes below $0ABF, with a wait's return at $0AB0; the switch keeps the
 * task's S under its return and the registers it keeps (P and the index
 * registers, a byte or a word each), and records whose turn it is as the
 * record's offset. */
enum {
    TASK = 2, ITS_TOP = 0x0A3F + TASK * 64, WAITED_AT = 0x0AB0, RETURN_BYTES = 2, KEPT_BYTE_WIDE = 5,
    KEPT_WORD_WIDE = 7, STOPPED = 0, WAITING = 2, RUNNING = 3, RECORD_BYTES = 8,
};

static void menu_answer_case(void)
{
    uint8_t *rom = make_rom(tasks_code, 0, 0x8000);
    Snes *s = machine(rom);
    GameState *g = state_of(s);
    s->cpu.s = 0x09F0;   /* the menus' own stack, between turns */

    set_menu_tasks_state(g, TASK, WAITING);
    set_menu_tasks_stack(g, TASK, WAITED_AT - RETURN_BYTES - KEPT_BYTE_WIDE);
    CHECK(menu_task_goes_on_from(s, WAITED_AT), "a task waiting there, its index registers kept a byte each");
    set_menu_tasks_stack(g, TASK, WAITED_AT - RETURN_BYTES - KEPT_WORD_WIDE);
    CHECK(menu_task_goes_on_from(s, WAITED_AT), "a task waiting there, its index registers kept a word each");
    set_menu_tasks_stack(g, TASK, WAITED_AT - 0x10);
    CHECK(!menu_task_goes_on_from(s, WAITED_AT), "a task waiting somewhere else");
    set_menu_tasks_stack(g, TASK, WAITED_AT - RETURN_BYTES - KEPT_BYTE_WIDE);
    CHECK(!menu_task_goes_on_from(s, 0x01F0), "a wait on no task's stack");

    set_menu_tasks_state(g, TASK, STOPPED);
    CHECK(!menu_task_goes_on_from(s, WAITED_AT), "a task stopped (the menus closed)");

    set_menu_tasks_state(g, TASK, RUNNING);
    set_menu_current_task(g, TASK * RECORD_BYTES);
    s->cpu.s = WAITED_AT - RETURN_BYTES - KEPT_BYTE_WIDE + 3;
    CHECK(menu_task_goes_on_from(s, WAITED_AT), "the task the scheduler is taking back into the wait");
    s->cpu.s = ITS_TOP - 2;
    CHECK(!menu_task_goes_on_from(s, WAITED_AT), "the task started afresh, running over the wait from its top");
    s->cpu.s = WAITED_AT - RETURN_BYTES - KEPT_BYTE_WIDE + 3;
    set_menu_current_task(g, (TASK + 1) * RECORD_BYTES);
    CHECK(!menu_task_goes_on_from(s, WAITED_AT), "another task's turn, the task not waiting");
    free(s);
    free(rom);
}

/* ---- A frame wait a save can hold -------------------------------------------------------- */

/* Bank $00. The main loop counts its passes in $20 and calls the wait at
 * $8300, which asks for the frame ($50 = 1) and loops at $8304 until the
 * vblank handler has taken it ($50 = 0); the handler counts vblanks in $40. */
enum { PASSES = 0x20, VBLANKS = 0x40, PENDING = 0x50, WAIT_LOOP = 0x008304, WAIT_EXIT = 0x008308 };

static const Code wait_code[] = {
    /* reset: SEI, native mode, REP #$10, SEP #$20, S = $01FF, NMI on, JMP 8100 */
    { 0x8000, { 0x78, 0x18, 0xFB, 0xC2, 0x10, 0xE2, 0x20, 0xA2, 0xFF, 0x01, 0x9A, 0xA9, 0x80, 0x8D, 0x00, 0x42, 0x4C,
                0x00, 0x81 }, 19 },
    /* 8100 main: INC $20 : JSR 8300 : BRA 8100 */
    { 0x8100, { 0xE6, 0x20, 0x20, 0x00, 0x83, 0x80, 0xF9 }, 7 },
    /* 8300 the wait: LDA #1 : STA $50 : 8304 LDA $50 : BNE 8304 : RTS */
    { 0x8300, { 0xA9, 0x01, 0x85, 0x50, 0xA5, 0x50, 0xD0, 0xFC, 0x60 }, 9 },
    /* 8600 the vblank handler: STZ $50 : INC $40 : RTI */
    { 0x8600, { 0x64, 0x50, 0xE6, 0x40, 0x40 }, 5 },
};

/* The loop's pass, an idle entry: what the port's wait goes on as in the
 * shipping mode. */
static void wait_pass(Snes *s)
{
    uint8_t pending = m_read8(s, PENDING);
    m_set_a8(s, pending);
    m_set_nz8(s, pending);
    if (!pending)
        s->cpu.pc = (uint16_t)WAIT_EXIT;
}

/* The wait as a port: the frame asked for, waited for, the loop's last load
 * of the flag left in A. */
static void leave_waited(Snes *s)
{
    m_set_a8(s, 0);
    m_set_nz8(s, 0);
    m_rts(s);
}

static void wait_held(Snes *s)
{
    m_write8(s, PENDING, 1);
    while (m_read8(s, PENDING))
        if (!native_wait_loop_held(s, WAIT_LOOP, WAIT_EXIT, NATIVE_WAKE_HANDLER, DC_A | DC_FLAGS))
            return;
    leave_waited(s);
}

static void wait_suspended(Snes *s)
{
    m_write8(s, PENDING, 1);
    while (m_read8(s, PENDING))
        native_wait_loop(s, WAIT_LOOP, WAIT_EXIT, NATIVE_WAKE_HANDLER);
    leave_waited(s);
}

static NativeRuntime *wait_runtime(NativeMode mode, bool held)
{
    static NativeEntry table[2];
    table[0] = (NativeEntry){ .addr = 0x008300, .name = "frame_wait", .fn = held ? wait_held : wait_suspended,
                              .p_mask = 0x30, .p_value = P_M8_X16, .cost_milli = 3000 };
    table[1] = (NativeEntry){ .addr = WAIT_LOOP, .name = "frame_wait_loop", .fn = wait_pass, .p_mask = 0x20,
                              .p_value = 0x20, .idle = true };
    return native_create(table, 2, mode);
}

/* Frame ends with something suspended, over `frames` frames. */
static int suspended_frame_ends(Snes *s, NativeRuntime *rt, int frames)
{
    int n = 0;
    for (int i = 0; i < frames; i++) {
        snes_run_frame(s, 0);
        n += native_suspended(rt) != 0;
    }
    return n;
}

static void held_wait_case(void)
{
    uint8_t *rom = make_rom(wait_code, sizeof wait_code / sizeof wait_code[0], 0x8600);

    /* the battle's way: the port waits suspended, over every frame end */
    Snes *s = machine(rom);
    NativeRuntime *rt = wait_runtime(NATIVE_ON, false);
    native_attach(rt, s);
    int waiting = suspended_frame_ends(s, rt, 20);
    CHECK(waiting > 10, "a port waiting in its loop was suspended at %d of 20 frame ends", waiting);
    native_destroy(rt);
    free(s);

    /* held: nothing suspended at a frame end */
    s = machine(rom);
    rt = wait_runtime(NATIVE_ON, true);
    native_attach(rt, s);
    waiting = suspended_frame_ends(s, rt, 20);
    CHECK(waiting == 0, "the held wait left a routine suspended at %d of 20 frame ends", waiting);
    CHECK(s->bus.wram[PASSES] >= 18 && s->bus.wram[VBLANKS] >= 18, "held: %u passes, %u vblanks in 20 frames",
          s->bus.wram[PASSES], s->bus.wram[VBLANKS]);

    /* saved where nothing is owed either, it loads into a fresh machine and
     * runtime and plays on as the run it came from */
    int tries = 0;
    while (native_owed(rt) && tries++ < 60)
        snes_run_frame(s, 0);
    CHECK(!native_owed(rt) && !native_suspended(rt), "no frame end with nothing owed or suspended");
    size_t len;
    uint8_t *state = snes_state_write(s, &len);
    CHECK(state != NULL, "the state wasn't written");
    for (int i = 0; i < 30; i++)
        snes_run_frame(s, 0);
    Snes *copy = machine(rom);
    char err[200] = "";
    CHECK(state && snes_state_read(copy, state, len, err, sizeof err) == 0, "the state didn't load: %s", err);
    NativeRuntime *fresh = wait_runtime(NATIVE_ON, true);
    native_attach(fresh, copy);
    for (int i = 0; i < 30; i++)
        snes_run_frame(copy, 0);
    CHECK(!memcmp(s->bus.wram, copy->bus.wram, sizeof s->bus.wram) && s->master == copy->master &&
              !memcmp(&s->cpu, &copy->cpu, offsetof(Cpu, bus)),
          "the loaded state played on otherwise: passes %u/%u, vblanks %u/%u, master %llu/%llu", s->bus.wram[PASSES],
          copy->bus.wram[PASSES], s->bus.wram[VBLANKS], copy->bus.wram[VBLANKS], (unsigned long long)s->master,
          (unsigned long long)copy->master);
    free(state);
    native_destroy(fresh);
    native_destroy(rt);
    free(copy);
    free(s);

    /* shadow mode compares the port across the wait */
    s = machine(rom);
    rt = wait_runtime(NATIVE_SHADOW, true);
    native_attach(rt, s);
    for (int i = 0; i < 20; i++)
        snes_run_frame(s, 0);
    const NativeStats *st = native_stats_named(rt, "frame_wait");
    CHECK(st->matched >= 15 && st->mismatched == 0 && !native_failed(rt), "held, shadow: %u matched, %u mismatched",
          st->matched, st->mismatched);
    native_destroy(rt);
    free(s);
    free(rom);
}

int main(void)
{
    tasks_case("a yield, shipping mode", NATIVE_ON, RUN_THROUGH);
    tasks_case("a yield, shadow mode", NATIVE_SHADOW, RUN_THROUGH);
    tasks_case("a task dropped, shipping mode", NATIVE_ON, DROP_THE_TASK);
    tasks_case("a task dropped, shadow mode", NATIVE_SHADOW, DROP_THE_TASK);
    tasks_case("a task started afresh, shipping mode", NATIVE_ON, RESTART_THE_TASK);
    parked_save_case("saved parked, loaded in the shipping mode", NATIVE_ON);
    parked_save_case("saved parked, loaded in shadow mode", NATIVE_SHADOW);
    menu_answer_case();
    held_wait_case();
    if (failures)
        printf("%d failure(s)\n", failures);
    else
        printf("suspend_test: all passed\n");
    return failures ? 1 : 0;
}
