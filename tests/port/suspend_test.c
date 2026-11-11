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
 *   The shipping cases also call the scheduler as C and park after the
 *   switch. Both paths handle an interrupt pending before the switch records
 *   its wait. Restarting a task happens while the other task owns the machine.
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
 * - a main loop's held wait named by the machine (native_hold, m_hold_at): a
 *   port stops at the loop's head holding it, and the loop's look is the
 *   wait's continuation, which the runtime runs as C there: nothing suspended
 *   at a frame end, the census sees no entry by the machine, and a state taken
 *   there names the wait, so a fresh machine and runtime go on in it and play
 *   on exactly as the run did; one with no wait named (a state from before)
 *   enters the loop once and holds it from there. Shadow mode names nothing.
 *
 * Exits non-zero on failure.
 *
 *   suspend_test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "menu/menu.h"
#include "core/input.h"
#include "core/console.h"
#include "field/battle_return.h"
#include "warp/warp.h"
#include "core/machine_forms.h"
#include "menu/bridge.h"
#include "menu/object.h"
#include "menu/script_call.h"
#include "overworld/entries.h"
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
    /* An interrupt at the task switch records that it ran and returns. */
    { 0x8500, { 0xE6, 0x40, 0x40 }, 3 },
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
static bool wait_not_recorded;
static bool switch_in_c;
static unsigned c_switches;
static bool interrupt_at_yield;

static void task_interrupt(Snes *s)
{
    m_write8(s, 0x40, (uint8_t)(m_read8(s, 0x40) + 1));
    m_rti(s);
}

/* The same task records and stacks as the synthetic switch above. */
static void switch_tasks(Snes *s)
{
    m_push8(s, s->cpu.p);
    uint16_t task = m_read16(s, 0x14);
    m_write16(s, A_RECORD + task, s->cpu.s);
    task ^= 2;
    m_write16(s, 0x14, task);
    s->cpu.s = m_read16(s, A_RECORD + task);
    uint8_t p = m_pull8(s);
    m_rep(s, (uint8_t)~p);
    m_sep(s, p);
    m_rts(s);
    c_switches++;
}

static bool task_a_goes_on(Snes *s, uint16_t s_after)
{
    if (wait_not_recorded && cpu_pc24(&s->cpu) == SWITCH && s->cpu.s == (uint16_t)(s_after - 2))
        return false;   /* closing cleared the record; the switch has yet to write the new wait */
    return a_goes_on;
}

/* The port: the count, the yield, the count again. */
static void count_and_yield(Snes *s)
{
    m_write8(s, COUNT_BEFORE, (uint8_t)(m_read8(s, COUNT_BEFORE) + 1));
    if (interrupt_at_yield) {
        s->cpu.nmi_pending = true;
        interrupt_at_yield = false;
    }
    if (switch_in_c && m_runs_retired_as_c(s)) {
        uint16_t stack = s->cpu.s;
        m_push16(s, YIELD_SITE + 2);
        m_jump_to(s, SWITCH);
        native_sync_boundary(s);
        switch_tasks(s);
        native_park_after_task_switch(s, YIELD_SITE + 3, stack,
                                     DC_A | DC_X | DC_Y | DC_FLAGS, task_a_goes_on);
    } else {
        native_yield_call_dc(s, YIELD_SITE, SWITCH, false, DC_A | DC_X | DC_Y | DC_FLAGS, task_a_goes_on);
    }
    m_write8(s, COUNT_AFTER, (uint8_t)(m_read8(s, COUNT_AFTER) + 1));
    m_rts(s);
}

static const NativeEntry tasks_table[] = {
    { .addr = 0x008300, .name = "count_and_yield", .fn = count_and_yield, .p_mask = 0x30, .p_value = P_M8_X16,
      .cost_milli = 3000, .dont_care = DC_A | DC_X | DC_Y | DC_FLAGS },
};

typedef enum { RUN_THROUGH, DROP_THE_TASK, RESTART_THE_TASK, WAIT_NOT_RECORDED } TaskCase;

static void tasks_case(const char *what, NativeMode mode, TaskCase how)
{
    uint8_t *rom = make_rom(tasks_code, sizeof tasks_code / sizeof tasks_code[0], 0x8500);
    bool wanted_interrupt = interrupt_at_yield;
    Snes *s = machine(rom);
    memset(s->bus.wram, 0, 0x600);
    a_goes_on = true;
    wait_not_recorded = how == WAIT_NOT_RECORDED;
    set_task_waiting(s, B_RECORD, B_TOP, 0x8180);
    NativeEntry entry = tasks_table[0];
    if (how == WAIT_NOT_RECORDED)
        entry.cost_milli = 5000000;   /* cross a frame paying the work before the switch starts */
    const NativeEntry entries[] = {
        entry,
        { .addr = 0x008500, .name = "task_interrupt", .fn = task_interrupt, .cost_milli = 10000 },
    };
    NativeRuntime *rt = native_create(entries, wanted_interrupt && mode == NATIVE_ON ? 2 : 1, mode);
    native_attach(rt, s);
    native_attach_frame_interrupts(rt, s, task_interrupt, NULL);
    enum { PARKED_MIDWAY = 2 };
    int frames = switch_in_c && how == RESTART_THE_TASK ? 24 : 4;
    bool parked_seen = false, restarted = false;
    for (int i = 0; i < frames; i++) {
        snes_run_frame(s, 0);
        if (native_parked(rt))
            parked_seen = true;
        /* task A can't go on for a frame: the waits it makes then are dropped */
        if (how == DROP_THE_TASK)
            a_goes_on = i != PARKED_MIDWAY - 1;
        bool restart_here = switch_in_c ? (s->cpu.s > A_TOP && cpu_pc24(&s->cpu) >= 0x8180 &&
                                           cpu_pc24(&s->cpu) < 0x8187) : i == PARKED_MIDWAY - 1;
        if (i < frames - 1 && restart_here && !restarted && how == RESTART_THE_TASK && native_parked(rt) == 1) {
            /* task A started afresh at its loop's head while its port waits */
            uint16_t s_a = (uint16_t)(s->bus.wram[A_RECORD] | s->bus.wram[A_RECORD + 1] << 8);
            CHECK(s_a < A_TOP && s_a > A_TOP - 0x10, "%s: task A isn't waiting on its stack (S %04X)", what, s_a);
            set_task_waiting(s, A_RECORD, A_TOP, 0x8100);
            restarted = true;
        }
    }
    const uint8_t *w = s->bus.wram;
    const NativeStats *st = native_stats_named(rt, "count_and_yield");
    if (wanted_interrupt)
        CHECK(w[0x40] == 1, "%s: the pending interrupt ran %u times", what, w[0x40]);
    CHECK(!s->cpu.stopped, "%s: ran into the ROM's fill at $%02X:%04X", what, s->cpu.pb, s->cpu.pc);
    CHECK(w[B_TURNS] > (how == WAIT_NOT_RECORDED ? 0 : 10), "%s: task B took %u turns", what, w[B_TURNS]);
    CHECK((uint8_t)(w[COUNT_BEFORE] - w[COUNT_AFTER]) <= (restarted ? 2 : 1),
          "%s: counted %u before the yield and %u after", what, w[COUNT_BEFORE], w[COUNT_AFTER]);
    CHECK(parked_seen, "%s: the port was never parked at a frame's end", what);
    CHECK(native_suspended(rt) >= native_parked(rt) &&
          native_suspended(rt) <= native_parked(rt) + (switch_in_c ? 1 : 0), "%s: %d suspended, %d of them parked", what,
          native_suspended(rt), native_parked(rt));
    if (how == RUN_THROUGH || how == WAIT_NOT_RECORDED)
        CHECK(st->abandoned == 0, "%s: %u calls abandoned", what, st->abandoned);
    if (how == DROP_THE_TASK)
        CHECK(st->abandoned >= 1 && st->abandoned < st->calls, "%s: %u of %u calls abandoned", what, st->abandoned,
              st->calls);
    if (how == RESTART_THE_TASK) {
        CHECK(restarted, "%s: no parked task was available to restart", what);
        CHECK(st->abandoned == 1, "%s: %u calls abandoned, not the one its fresh start replaced", what, st->abandoned);
        CHECK(native_parked(rt) <= 1, "%s: %d parked", what, native_parked(rt));
    }
    if (mode == NATIVE_SHADOW)
        CHECK(st->mismatched == 0 && st->matched > 0 && !native_failed(rt), "%s: %u matched, %u mismatched (%s)",
              what, st->matched, st->mismatched, native_detail(rt, 0) ? native_detail(rt, 0) : "");
    native_destroy(rt);
    free(s);
    free(rom);
    wait_not_recorded = false;
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

/* Two direct calls of the same frame wait, with visible work between them.
 * Waking the first must not compare the second at that same boundary. */
static bool repeated_wait_wrong;

static void wait_twice(Snes *s)
{
    m_push16(s, 0x8402);
    wait_suspended(s);
    uint8_t count = (uint8_t)(m_read8(s, 0x51) + 1);
    m_write8(s, 0x51, count);
    m_set_nz8(s, count);
    s->cpu.x = 0;
    m_push16(s, 0x840D);
    wait_suspended(s);
    if (repeated_wait_wrong)
        m_write8(s, 0x52, 1);
    m_rts(s);
}

static void repeated_wait_case(void)
{
    uint8_t *rom = make_rom(wait_code, sizeof wait_code / sizeof wait_code[0], 0x8600);
    /* The original's work spans an interrupt before reaching its next wait. */
    const uint8_t twice[] = { 0x20, 0x00, 0x83, 0xE6, 0x51, 0xA2, 0xFF, 0xFF,
                              0xCA, 0xD0, 0xFD, 0x20, 0x00, 0x83, 0x60 };
    memcpy(rom + 0x8400, twice, sizeof twice);
    rom[0x8104] = 0x84;
    NativeEntry entry = { .addr = 0x008400, .name = "wait_twice", .fn = wait_twice,
                          .p_mask = 0x30, .p_value = P_M8_X16 };
    for (unsigned variant = 0; variant < 2; variant++) {
        repeated_wait_wrong = variant != 0;
        Snes *s = machine(rom);
        NativeRuntime *rt = native_create(&entry, 1, NATIVE_SHADOW);
        native_attach(rt, s);
        for (unsigned frame = 0; frame < 32; frame++)
            snes_run_frame(s, 0);
        const NativeStats *stats = native_stats_named(rt, "wait_twice");
        CHECK(stats && stats->calls >= 2, "repeated wait variant %u was not exercised: %u calls",
              variant, stats ? stats->calls : 0);
        CHECK(stats && (!repeated_wait_wrong ? stats->mismatched == 0 && stats->matched + stats->interrupted >= 2
                                                     : stats->mismatched > 0),
              "repeated wait variant %u: wrong comparison result", variant);
        native_destroy(rt);
        free(s);
    }
    free(rom);
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

/* ---- A main loop's held wait, named by the machine ---------------------------------------- */

/* Bank $00. The start ($8100) clears the frame's flag ($60) and goes on to
 * the main loop's head ($8102), where the main thread rests: the vblank
 * handler sets the flag and counts vblanks in $40, and the loop, seeing it,
 * does the frame's work (counts frames in $70) and clears it. The start is a
 * port that holds the wait (m_hold_at); the loop's look is the wait's
 * continuation (an idle entry with a hold number). */
enum { FRAME_FLAG = 0x60, FRAMES_DONE = 0x70, MAIN_LOOP_HEAD = 0x008102, HOLD_TEST_LOOP = 1 };

static const Code hold_code[] = {
    /* reset: SEI, native mode, REP #$10, SEP #$20, S = $01FF, NMI on, JMP 8100 */
    { 0x8000, { 0x78, 0x18, 0xFB, 0xC2, 0x10, 0xE2, 0x20, 0xA2, 0xFF, 0x01, 0x9A, 0xA9, 0x80, 0x8D, 0x00, 0x42, 0x4C,
                0x00, 0x81 }, 19 },
    /* 8100 STZ $60 : 8102 LDA $60 : BEQ 8102 : INC $70 : STZ $60 : BRA 8102 */
    { 0x8100, { 0x64, 0x60, 0xA5, 0x60, 0xF0, 0xFC, 0xE6, 0x70, 0x64, 0x60, 0x80, 0xF6 }, 12 },
    /* 8600 the vblank handler: INC $60 : INC $40 : RTI */
    { 0x8600, { 0xE6, 0x60, 0xE6, 0x40, 0x40 }, 5 },
};

static void hold_start(Snes *s)
{
    m_write8(s, FRAME_FLAG, 0);
    m_stop_at(s, MAIN_LOOP_HEAD, DC_A | DC_FLAGS);
    native_hold_return(s, HOLD_TEST_LOOP);
}

static void hold_first_start(Snes *s)
{
    native_hold_return(s, HOLD_TEST_LOOP);
    CHECK(s->held_wait == 0, "an unrelated return address named a held continuation");
    m_stop_at(s, MAIN_LOOP_HEAD, DC_A | DC_FLAGS);
    native_hold_return(s, HOLD_TEST_LOOP + 1);
    CHECK(s->held_wait == 0, "a return named a continuation of another wait");
    hold_start(s);
}

/* One look of the loop: the frame's work where the handler has asked for it.
 * It goes round either way, so the main thread rests at the head again. */
static void hold_look(Snes *s)
{
    uint8_t flag = m_read8(s, FRAME_FLAG);
    if (flag) {
        m_write8(s, FRAMES_DONE, (uint8_t)(m_read8(s, FRAMES_DONE) + 1));
        m_write8(s, FRAME_FLAG, 0);
    }
    m_set_a8(s, 0);
    m_set_nz8(s, 0);
}

static NativeRuntime *hold_runtime(NativeMode mode)
{
    /* The live loop sorts after twenty unused holds, beyond the former
     * fixed limit of sixteen continuations. */
    static NativeEntry table[22];
    table[0] = (NativeEntry){ .addr = 0x008100, .name = "hold_start", .fn = hold_first_start, .p_mask = 0x30,
                              .p_value = P_M8_X16, .cost_milli = 1000 };
    table[1] = (NativeEntry){ .addr = MAIN_LOOP_HEAD, .name = "hold_look", .fn = hold_look, .p_mask = 0x20,
                              .p_value = 0x20, .idle = true, .hold = HOLD_TEST_LOOP };
    for (int i = 2; i < 22; i++)
        table[i] = (NativeEntry){ .addr = (uint32_t)(0x007000 + i), .name = "unused_hold",
                                  .fn = hold_look, .idle = true, .hold = HOLD_TEST_LOOP };
    return native_create(table, 22, mode);
}

/* How the census sees the loop's look: entered by the machine, or run as the
 * held wait's continuation. */
static int looks_entered, looks_held;

static void on_look_entered(void *ctx, Snes *s, const NativeEntry *e)
{
    (void)ctx, (void)s;
    looks_entered += e->hold != 0;
}

static void on_look_held(void *ctx, Snes *s, const NativeEntry *e)
{
    (void)ctx, (void)s, (void)e;
    looks_held++;
}

static void run_frames(Snes *s, int frames)
{
    for (int i = 0; i < frames; i++)
        snes_run_frame(s, 0);
}

static void named_hold_case(void)
{
    uint8_t *rom = make_rom(hold_code, sizeof hold_code / sizeof hold_code[0], 0x8600);
    native_census_hooks = (NativeCensusHooks){ .entered = on_look_entered, .held = on_look_held };

    /* the start holds the wait: every look is the continuation's, and the
     * machine names the wait at every frame end, with nothing suspended */
    Snes *s = machine(rom);
    NativeRuntime *rt = hold_runtime(NATIVE_ON);
    native_attach(rt, s);
    looks_entered = looks_held = 0;
    int waiting = suspended_frame_ends(s, rt, 20);
    CHECK(waiting == 0, "the named hold left a routine suspended at %d of 20 frame ends", waiting);
    CHECK(looks_entered == 0 && looks_held > 0, "named hold: the loop entered %d times, held %d", looks_entered,
          looks_held);
    CHECK(s->held_wait == HOLD_TEST_LOOP && s->bus.wram[FRAMES_DONE] >= 18,
          "named hold: wait %u named, %u frames done in 20", s->held_wait, s->bus.wram[FRAMES_DONE]);

    /* a state taken there names the wait: a fresh machine and runtime go on
     * in it, the machine entering nothing, and play on as the run did; with
     * no wait named (a state from before), the loop's code comes to its head,
     * is entered once and holds the wait from there */
    int tries = 0;
    while (native_owed(rt) && tries++ < 60)
        snes_run_frame(s, 0);
    size_t len;
    uint8_t *state = snes_state_write(s, &len);
    CHECK(state != NULL, "the state wasn't written");
    run_frames(s, 30);
    for (int stale = 0; stale < 2; stale++) {
        Snes *copy = machine(rom);
        char err[200] = "";
        CHECK(state && snes_state_read(copy, state, len, err, sizeof err) == 0, "the state didn't load: %s", err);
        CHECK(copy->held_wait == HOLD_TEST_LOOP, "the state named wait %u", copy->held_wait);
        if (stale)
            copy->held_wait = 0;
        NativeRuntime *fresh = hold_runtime(NATIVE_ON);
        native_attach(fresh, copy);
        looks_entered = looks_held = 0;
        run_frames(copy, 30);
        CHECK(looks_entered == stale && looks_held > 0, "loaded%s: the loop entered %d times, held %d",
              stale ? " with no wait named" : "", looks_entered, looks_held);
        CHECK(!memcmp(s->bus.wram, copy->bus.wram, sizeof s->bus.wram) && s->master == copy->master &&
                  !memcmp(&s->cpu, &copy->cpu, offsetof(Cpu, bus)) && copy->held_wait == HOLD_TEST_LOOP,
              "loaded%s: played on otherwise: frames %u/%u, master %llu/%llu", stale ? " with no wait named" : "",
              s->bus.wram[FRAMES_DONE], copy->bus.wram[FRAMES_DONE], (unsigned long long)s->master,
              (unsigned long long)copy->master);
        native_destroy(fresh);
        free(copy);
    }
    free(state);
    native_destroy(rt);
    free(s);

    /* shadow mode names nothing: the stop is compared as any stop, and the
     * original runs the loop */
    s = machine(rom);
    rt = hold_runtime(NATIVE_SHADOW);
    native_attach(rt, s);
    run_frames(s, 20);
    const NativeStats *st = native_stats_named(rt, "hold_start");
    CHECK(st->matched == 1 && st->mismatched == 0 && !native_failed(rt) && !s->held_wait,
          "named hold, shadow: %u matched, %u mismatched, wait %u named", st->matched, st->mismatched, s->held_wait);
    native_destroy(rt);
    free(s);
    native_census_hooks = (NativeCensusHooks){ 0 };
    free(rom);
}

/* A shared wait returns into a caller that is another continuation of the
 * same hold. Unlike a loop that stays at its head, the return changes S. */
static void returning_hold_look(Snes *s)
{
    uint8_t flag = m_read8(s, FRAME_FLAG);
    m_set_a8(s, flag);
    m_set_nz8(s, flag);
    if (flag)
        m_rts(s);
}

static void returning_hold_frame(Snes *s)
{
    m_write8(s, FRAMES_DONE, (uint8_t)(m_read8(s, FRAMES_DONE) + 1));
    m_push16(s, 0x8204);
    hold_start(s);
}

static void returning_hold_first_frame(Snes *s) { returning_hold_frame(s); }

static void returning_hold_bad_frame(Snes *s)
{
    m_write8(s, FRAMES_DONE, (uint8_t)(m_read8(s, FRAMES_DONE) + 1));
    returning_hold_frame(s);
}

static void returning_hold_case(void)
{
    static const Code code[] = {
        { 0x8000, { 0x78, 0x18, 0xFB, 0xC2, 0x10, 0xE2, 0x20, 0xA2, 0xFF, 0x01, 0x9A,
                    0xA9, 0x80, 0x8D, 0x00, 0x42, 0x4C, 0x00, 0x82 }, 19 },
        { 0x8100, { 0x64, 0x60, 0xA5, 0x60, 0xF0, 0xFC, 0x60 }, 7 },
        { 0x8200, { 0xE6, 0x70, 0x20, 0x00, 0x81, 0x80, 0xF9 }, 7 },
        { 0x8600, { 0xE6, 0x60, 0xE6, 0x40, 0x40 }, 5 },
    };
    const NativeEntry table[] = {
        { .addr = 0x008200, .name = "first_frame", .fn = returning_hold_first_frame, .cost_milli = 1000 },
        { .addr = MAIN_LOOP_HEAD, .name = "shared_wait", .fn = returning_hold_look,
          .idle = true, .hold = HOLD_TEST_LOOP },
        { .addr = 0x008205, .name = "next_frame", .fn = returning_hold_frame,
          .cost_milli = 1000, .hold = HOLD_TEST_LOOP },
        { .addr = 0x008205, .name = "registered_frame", .fn = returning_hold_frame,
          .shadow_body = returning_hold_first_frame, .cost_milli = 1000 },
    };
    uint8_t *rom = make_rom(code, sizeof code / sizeof code[0], 0x8600);
    Snes *s = machine(rom);
    NativeRuntime *rt = native_create(table, 4, NATIVE_ON);
    native_attach(rt, s);
    native_census_hooks = (NativeCensusHooks){ .entered = on_look_entered, .held = on_look_held };
    looks_entered = looks_held = 0;
    int waiting = suspended_frame_ends(s, rt, 20);
    CHECK(!waiting && looks_entered == 0 && s->bus.wram[FRAMES_DONE] >= 18 &&
              native_stats_named(rt, "registered_frame")->calls == 0,
          "returning hold: suspended %d, entered %d, frames %u", waiting, looks_entered,
          s->bus.wram[FRAMES_DONE]);
    size_t len;
    uint8_t *state = snes_state_write(s, &len);
    run_frames(s, 30);
    Snes *copy = machine(rom);
    char err[200] = "";
    CHECK(state && snes_state_read(copy, state, len, err, sizeof err) == 0, "returning hold load: %s", err);
    NativeRuntime *fresh = native_create(table, 4, NATIVE_ON);
    native_attach(fresh, copy);
    run_frames(copy, 30);
    CHECK(!memcmp(s->bus.wram, copy->bus.wram, sizeof s->bus.wram) && s->master == copy->master &&
              !memcmp(&s->cpu, &copy->cpu, offsetof(Cpu, bus)) && looks_entered == 0 &&
              native_stats_named(fresh, "registered_frame")->calls == 0,
          "returning hold: loaded state diverged or entered a continuation by address");
    native_census_hooks = (NativeCensusHooks){ 0 };
    native_destroy(fresh);
    native_destroy(rt);
    free(state);
    free(copy);
    free(s);

    /* The address path must use the retained row even though its alias was
     * supplied first. The distinct first-pass wrapper is compared once:
     * thereafter its work is inside the returning pass's comparison. */
    s = machine(rom);
    rt = native_create(table, 4, NATIVE_SHADOW);
    native_attach(rt, s);
    run_frames(s, 20);
    CHECK(native_stats_named(rt, "registered_frame")->matched >= 18 &&
              native_stats_named(rt, "first_frame")->calls == 1 &&
              native_stats_named(rt, "next_frame")->calls == 0 && !native_failed(rt),
          "returning hold: the retained row did not own matching address-path comparisons");
    native_destroy(rt);
    free(s);

    /* Naming an inline body only removes the overlapping comparison. The
     * retained wrapper must still detect a wrong effect in that body. */
    NativeEntry bad[4];
    memcpy(bad, table, sizeof bad);
    bad[3].fn = returning_hold_bad_frame;
    s = machine(rom);
    rt = native_create(bad, 4, NATIVE_SHADOW);
    native_attach(rt, s);
    run_frames(s, 4);
    CHECK(native_stats_named(rt, "registered_frame")->mismatched > 0 && native_failed(rt),
          "returning hold: inline body escaped the retained wrapper's comparison");
    native_destroy(rt);
    free(s);

    /* Naming a wait must not bypass a continuation's P contract. This one
     * requires decimal mode, while the program runs in binary mode: its
     * retained address entry must take every return instead. */
    NativeEntry guarded[4];
    memcpy(guarded, table, sizeof guarded);
    guarded[2].p_mask = CPU_FLAG_D;
    guarded[2].p_value = CPU_FLAG_D;
    s = machine(rom);
    rt = native_create(guarded, 4, NATIVE_ON);
    native_attach(rt, s);
    run_frames(s, 20);
    CHECK(native_stats_named(rt, "next_frame")->skipped >= 18 &&
              native_stats_named(rt, "registered_frame")->calls >= 18 && !native_failed(rt),
          "returning hold bypassed the continuation's decimal-mode contract");
    native_destroy(rt);
    free(s);
    free(rom);
}

/* A callee waits on a higher, separate stack, then restores its caller's
 * stack. The hold must not mistake that switch for an unwound call. */
static bool other_stack_starts_afresh;

static void other_stack_wait(Snes *s)
{
    m_write16(s, A_RECORD, s->cpu.s);
    s->cpu.s = A_TOP + 2 * NATIVE_STACK_SWITCH_DISTANCE;
    m_write8(s, FRAME_FLAG, 0);
    m_hold_at(s, MAIN_LOOP_HEAD, DC_A | DC_FLAGS, HOLD_TEST_LOOP);
}

static void other_stack_look(Snes *s)
{
    if (m_read8(s, FRAME_FLAG)) {
        if (other_stack_starts_afresh) {
            s->cpu.s = A_TOP + 2;
            m_jump_to(s, 0x008180);
        } else {
            s->cpu.s = m_read16(s, A_RECORD);
            m_rts(s);
        }
    }
}

static void other_stack_fresh_loop(Snes *s) { (void)s; }

static void other_stack_caller(Snes *s)
{
    native_call(s, 0x008200, 0x008300, false);
    m_write8(s, COUNT_AFTER, (uint8_t)(m_read8(s, COUNT_AFTER) + 1));
    m_stop_at(s, 0x008200, DC_A | DC_FLAGS);
}

static bool other_stack_ready(Snes *s) { return snes_bus_peek(&s->bus, FRAME_FLAG) != 0; }

static void other_stack_poll(Snes *s)
{
    if (native_poll_held(s, HOLD_TEST_LOOP, MAIN_LOOP_HEAD, other_stack_ready))
        other_stack_look(s);
}

static void other_stack_loop(Snes *s)
{
    do {
        other_stack_look(s);
    } while (native_idle_held(s));
}

static void other_stack_hold_case(unsigned form)
{
    static const Code code[] = {
        { 0x8000, { 0x78, 0x18, 0xFB, 0xC2, 0x10, 0xE2, 0x20, 0xA2, 0xFF, 0x03, 0x9A,
                    0xA9, 0x80, 0x8D, 0x00, 0x42, 0x4C, 0x00, 0x82 }, 19 },
        { 0x8600, { 0xE6, 0x60, 0x40 }, 3 },
    };
    const NativeEntry table[] = {
        { .addr = 0x008200, .name = "caller", .fn = other_stack_caller, .cost_milli = 1000 },
        { .addr = 0x008300, .name = "other_stack", .fn = other_stack_wait, .cost_milli = 1000 },
        { .addr = MAIN_LOOP_HEAD, .name = "other_stack_wait", .fn = form == 1 ? other_stack_poll : other_stack_look,
          .idle = form != 1, .cost_milli = 1000, .hold = HOLD_TEST_LOOP,
          .loop = form == 2 ? other_stack_loop : NULL },
        { .addr = 0x008180, .name = "fresh_loop", .fn = other_stack_fresh_loop,
          .idle = true, .hold = HOLD_TEST_LOOP },
    };
    uint8_t *rom = make_rom(code, sizeof code / sizeof code[0], 0x8600);
    Snes *s = machine(rom);
    NativeRuntime *rt = native_create(table, 4, NATIVE_ON);
    native_attach(rt, s);
    s->bus.wram[COUNT_AFTER] = 0;
    int waiting = suspended_frame_ends(s, rt, 10);
    CHECK(waiting >= 8 && s->bus.wram[COUNT_AFTER] >= 8 &&
              native_stats_named(rt, "caller")->abandoned == 0,
          "other stack hold: suspended %d frames, returned %u, abandoned %u", waiting,
          s->bus.wram[COUNT_AFTER], native_stats_named(rt, "caller")->abandoned);
    native_destroy(rt);
    free(s);

    /* Starting afresh near the caller's old stack really does discard its
     * return. A previous excursion onto another stack must not keep the
     * abandoned C caller suspended forever. */
    other_stack_starts_afresh = true;
    s = machine(rom);
    rt = native_create(table, 4, NATIVE_ON);
    native_attach(rt, s);
    s->bus.wram[COUNT_AFTER] = 0;
    waiting = suspended_frame_ends(s, rt, 10);
    CHECK(waiting <= 1 && s->bus.wram[COUNT_AFTER] == 0 &&
              native_stats_named(rt, "caller")->abandoned == 1,
          "fresh stack hold: suspended %d frames, returned %u, abandoned %u", waiting,
          s->bus.wram[COUNT_AFTER], native_stats_named(rt, "caller")->abandoned);
    native_destroy(rt);
    free(s);
    other_stack_starts_afresh = false;
    free(rom);
}

/* The boot's two beam waits must leave one blank before entering the next
 * line. The title's music wait must remain suspended until the audio side
 * supplies a playing-track mask, even when no interrupt is enabled. */
static bool saw_blank, left_blank, heard_music;
static uint64_t blank_began, blank_ended;

static void scene_wait_probe(Snes *s)
{
    native_wait_loop(s, 0x008300, 0x008301, NATIVE_WAKE_HBLANK);
    saw_blank = s->bus.in_hblank;
    blank_began = s->master;
    native_wait_loop(s, 0x008301, 0x008302, NATIVE_WAKE_HBLANK_END);
    left_blank = !s->bus.in_hblank;
    blank_ended = s->master;
    native_wait_loop(s, 0x008302, 0x008303, NATIVE_WAKE_MUSIC_START);
    heard_music = (s->apu.cpu_out[3] & 0x0F) != 0;
    m_rts(s);
}

static void start_probe_music(void *ctx, Snes *s)
{
    (void)ctx;
    if (s->frame)
        s->apu.cpu_out[3] = 1;
}

/* An empty backedge still consumes a dispatch step and polls interrupts.
 * Calling its next pass inline must preserve that boundary even though no
 * accesses have accrued for native_sync to pay. */
static void boundary_frame(Snes *s)
{
    m_write8(s, FRAMES_DONE, (uint8_t)(m_read8(s, FRAMES_DONE) + 1));
    m_stop_at(s, 0x008205, 0);
}

static void boundary_backedge(Snes *s) { m_stop_at(s, 0x008200, 0); }

static void boundary_direct(Snes *s)
{
    m_jump_to(s, 0x008200);
    native_sync_boundary(s);
    boundary_frame(s);
}

static void empty_boundary_case(void)
{
    static const Code code[] = {
        { 0x8000, { 0x78, 0x18, 0xFB, 0xC2, 0x10, 0xE2, 0x20, 0xA2, 0xFF, 0x01, 0x9A,
                    0xA9, 0x80, 0x8D, 0x00, 0x42, 0x4C, 0x00, 0x82 }, 19 },
        { 0x8200, { 0xE6, 0x70, 0x4C, 0x05, 0x82, 0x80, 0xF9 }, 7 },
        { 0x8600, { 0xE6, 0x40, 0x40 }, 3 },
    };
    NativeEntry entries[] = {
        { .addr = 0x008200, .name = "frame", .fn = boundary_frame, .cost_milli = 1000 },
        { .addr = 0x008205, .name = "backedge", .fn = boundary_backedge, .cost_milli = 1000 },
    };
    uint8_t *rom = make_rom(code, sizeof code / sizeof code[0], 0x8600);
    Snes *reference = machine(rom), *direct = machine(rom);
    NativeRuntime *before = native_create(entries, 2, NATIVE_ON);
    entries[1].fn = boundary_direct;
    NativeRuntime *after = native_create(entries, 2, NATIVE_ON);
    native_attach(before, reference);
    native_attach(after, direct);
    for (int frame = 0; frame < 8; frame++) {
        snes_run_frame(reference, 0);
        snes_run_frame(direct, 0);
        CHECK(reference->master == direct->master &&
                  !memcmp(reference->bus.wram, direct->bus.wram, sizeof reference->bus.wram) &&
                  !memcmp(&reference->cpu, &direct->cpu, offsetof(Cpu, bus)),
              "empty boundary changed frame %d, master %llu/%llu, passes %u/%u", frame,
              (unsigned long long)reference->master, (unsigned long long)direct->master,
              reference->bus.wram[FRAMES_DONE], direct->bus.wram[FRAMES_DONE]);
    }
    CHECK(reference->bus.wram[0x40] >= 7 && !native_failed(before) && !native_failed(after),
          "empty boundary did not preserve the vblank interrupts");
    native_destroy(after);
    native_destroy(before);
    free(direct);
    free(reference);
    free(rom);
}

static void scene_wait_case(void)
{
    static const Code code[] = {
        { 0x8000, { 0x78, 0x18, 0xFB, 0xC2, 0x10, 0xE2, 0x20, 0xA2, 0xFF, 0x01, 0x9A,
                    0x20, 0x00, 0x83, 0xDB }, 15 },
        { 0x8300, { 0x60 }, 1 },
    };
    const NativeEntry entry = { .addr = 0x008300, .name = "scene_wait_probe", .fn = scene_wait_probe,
                                .p_mask = 0x30, .p_value = 0x20, .cost_milli = 1000 };
    uint8_t *rom = make_rom(code, sizeof code / sizeof code[0], 0x8600);
    Snes *s = machine(rom);
    s->on_frame_top = start_probe_music;
    NativeRuntime *rt = native_create(&entry, 1, NATIVE_ON);
    native_attach(rt, s);
    saw_blank = left_blank = heard_music = false;
    snes_run_frame(s, 0);
    CHECK(saw_blank && left_blank && blank_ended > blank_began,
          "beam wait did not leave its blank: %d/%d, %llu/%llu", saw_blank, left_blank,
          (unsigned long long)blank_began, (unsigned long long)blank_ended);
    CHECK(!heard_music && native_suspended(rt), "music wait returned before a track started");
    snes_run_frame(s, 0);
    snes_run_frame(s, 0);
    CHECK(heard_music && !native_suspended(rt) && !native_failed(rt), "music wait did not resume");
    native_destroy(rt);
    free(s);
    free(rom);
}


/* Every direct menu call checks the callee after constructing its call frame.
 * Include a guard that observes the entry PC and frame, as code-image and
 * stack guards do, as well as the decimal and width bits. */
static unsigned menu_contract_calls;
static bool menu_contract_image;

static bool menu_contract_guard(Snes *s)
{
    return menu_contract_image && s->cpu.d == 0x1234 && s->cpu.db == 0x7E &&
           cpu_pc24(&s->cpu) == SWITCH && m_read16(s, s->cpu.s + 1) == YIELD_SITE + 2;
}

static void menu_contract_callee(Snes *s)
{
    menu_contract_calls++;
    m_rts(s);
}

static void menu_contract_case(void)
{
    const NativeEntry callee = { .addr = SWITCH, .name = "menu_contract_probe", .fn = menu_contract_callee,
                                .p_mask = CPU_FLAG_D | CPU_FLAG_M | CPU_FLAG_X, .p_value = CPU_FLAG_M,
                                .guard = menu_contract_guard };
    uint8_t *rom = make_rom(tasks_code, sizeof tasks_code / sizeof tasks_code[0], 0x8000);
    native_direct_contracts(&callee, 1);
    for (unsigned form = 0; form < 4; form++) {
        for (unsigned invalid = 0; invalid < 8; invalid++) {
            Snes *s = machine(rom);
            s->cpu.e = invalid == 1;
            s->cpu.p = CPU_FLAG_M;
            if (invalid == 2) s->cpu.p |= CPU_FLAG_D;
            if (invalid == 3) s->cpu.p &= (uint8_t)~CPU_FLAG_M;
            if (invalid == 4) s->cpu.p |= CPU_FLAG_X;
            s->cpu.d = invalid == 5 ? 0 : 0x1234;
            s->cpu.db = invalid == 6 ? 0 : 0x7E;
            s->cpu.s = A_TOP;
            menu_contract_image = invalid != 7;
            menu_contract_calls = 0;
            bool back;
            if (form == 0)
                back = menu_run(s, YIELD_SITE, SWITCH, menu_contract_callee);
            else if (form == 1)
                back = menu_run_checked(s, YIELD_SITE, SWITCH, DC_A, menu_contract_callee);
            else if (form == 2)
                back = menu_run_or_call(s, YIELD_SITE, SWITCH, DC_A, menu_contract_callee);
            else {
                back = menu_begin_inline_call(s, YIELD_SITE, SWITCH, menu_contract_callee);
                if (back) {
                    menu_contract_calls++;
                    menu_end_inline_call(s, YIELD_SITE);
                }
            }
            CHECK(back == (invalid == 0) && menu_contract_calls == (invalid == 0),
                  "menu call form %u, invalid %u: returned %d, called %u", form, invalid, back, menu_contract_calls);
            CHECK(cpu_pc24(&s->cpu) == (invalid ? SWITCH : YIELD_SITE + 3) &&
                  s->cpu.s == (invalid ? A_TOP - 2 : A_TOP),
                  "menu call form %u, invalid %u: lost the checked call frame", form, invalid);
            free(s);
        }
    }
    native_direct_contracts(NULL, 0);
    free(rom);
}

/* A retired overworld service has no registry row. The menu bridge must
 * still accept its checked machine form and return through the call frame. */
static bool cleared_world_label;

static void menu_world_probe(Snes *s)
{
    CHECK(core_machine_form_takes(s, input_update), "retired input work has no checked form");
    GameState *g = state_of(s);
    set_overworld_sign_label(g, 0, 0xA5);
    void (*entry)(Snes *) = overworld_machine_form(s, 0xC25798);
    CHECK(entry != NULL, "retired world service has no checked form");
    if (entry) {
        bool back = menu_run(s, 0xC28302, 0xC25798, entry);
        cleared_world_label = back && overworld_sign_label(g, 0) == 0;
    }
    m_jump_to(s, 0x008300);
    m_rts(s);
}

static void menu_world_case(void)
{
    static const Code code[] = {
        { 0x8000, { 0x78, 0x18, 0xFB, 0xC2, 0x10, 0xE2, 0x20, 0xA2, 0xFF, 0x01, 0x9A,
                    0x20, 0x00, 0x83, 0xDB }, 15 },
        { 0x8300, { 0x60 }, 1 },
    };
    const NativeEntry entry = { .addr = 0x008300, .name = "menu_world_probe", .fn = menu_world_probe,
                                .p_mask = CPU_FLAG_M | CPU_FLAG_X, .p_value = CPU_FLAG_M,
                                .cost_milli = 1000 };
    uint8_t *rom = make_rom(code, sizeof code / sizeof code[0], 0x8600);
    Snes *s = machine(rom);
    NativeRuntime *rt = native_create(&entry, 1, NATIVE_ON);
    native_attach(rt, s);
    cleared_world_label = false;
    snes_run_frame(s, 0);
    CHECK(cleared_world_label && !native_failed(rt) && !native_suspended(rt),
          "menu bridge refused a retired world service or lost its return");
    native_destroy(rt);
    free(s);
    free(rom);
}

/* A world-map walker calls the interpreter directly. Its real contract
 * accepts either accumulator width and rejects bad machine and object state
 * before the first command can run, preserving the refused call frame. */
static void menu_script_contract_case(void)
{
    uint8_t *rom = make_rom(tasks_code, sizeof tasks_code / sizeof tasks_code[0], 0x8000);
    for (unsigned invalid = 0; invalid < 10; invalid++) {
        Snes *s = machine(rom);
        GameState *g = state_of(s);
        s->cpu.e = invalid == 2;
        s->cpu.p = invalid == 1 ? 0 : CPU_FLAG_M;
        if (invalid == 3) s->cpu.p |= CPU_FLAG_D;
        if (invalid == 4) s->cpu.p |= CPU_FLAG_X;
        s->cpu.d = invalid == 5 ? 0x100 : 0;
        s->cpu.db = invalid == 6 ? 0x40 : 0x7E;
        s->cpu.s = A_TOP;
        menu_object_make_running(g, 0);
        set_menu_object_slots_script(g, 0, invalid == 8 ? 0x400000 : 0x7F1000);
        set_menu_sprite_draw_list_first(g, 0xB500);
        set_menu_sprite_free_list_first(g, invalid == 9 ? 0 : 0xB000);
        set_menu_sprite_nodes_prev(g, 0, 0xB504);
        set_menu_sprite_nodes_next(g, 0, 0xB504);
        m_write8(s, 0x7F1000, 0x52); /* the script ends this object */
        if (invalid == 7) set_menu_object_current(g, 1);
        ScriptEnd end = menu_run_object_script_from(s, 0xC2378B);
        bool accepted = invalid < 2;
        CHECK(end == (accepted ? SCRIPT_DONE : SCRIPT_GAVE_BACK),
              "world script contract case %u returned %u", invalid, (unsigned)end);
        CHECK(cpu_pc24(&s->cpu) == (accepted ? 0xC2378E : 0xC20F63) &&
              s->cpu.s == (accepted ? A_TOP : A_TOP - 2),
              "world script contract case %u lost its return frame", invalid);
        free(s);
    }
    free(rom);
}

/* A C loop waits at a restart point whose entire caller frame is on the
 * machine. Compare its pace with the former held continuations, and reload
 * both old and new states into fresh runtimes. */
static bool checkpoint_enabled;
static unsigned checkpoint_entries;

static bool checkpoint_ready(Snes *s) { return snes_bus_peek(&s->bus, FRAME_FLAG) != 0; }
static bool checkpoint_idle(Snes *s) { return !checkpoint_ready(s); }
static void checkpoint_waits(Snes *s) { (void)s; }

static void checkpoint_start(Snes *s)
{
    m_push16(s, 0x8321);
    hold_start(s);
}

static void checkpoint_frame(Snes *s)
{
    checkpoint_entries++;
    CHECK(!native_poll_held(s, HOLD_TEST_LOOP + 1, MAIN_LOOP_HEAD, checkpoint_ready),
          "a C loop accepted another wait's restart point");
    CHECK(!native_poll_held(s, HOLD_TEST_LOOP, MAIN_LOOP_HEAD + 1, checkpoint_ready),
          "a C loop accepted another entry's restart point");
    do {
        m_write8(s, FRAMES_DONE, (uint8_t)(m_read8(s, FRAMES_DONE) + 1));
        m_write8(s, FRAME_FLAG, 0);
        m_hold_at(s, MAIN_LOOP_HEAD, DC_A | DC_FLAGS, HOLD_TEST_LOOP);
    } while (checkpoint_enabled && native_poll_held(s, HOLD_TEST_LOOP, MAIN_LOOP_HEAD, checkpoint_ready));
}

static NativeRuntime *checkpoint_runtime(Snes *s)
{
    static const NativeEntry table[] = {
        { .addr = 0x008100, .name = "checkpoint_start", .fn = checkpoint_start, .cost_milli = 1000 },
        { .addr = MAIN_LOOP_HEAD, .name = "checkpoint_frame", .fn = checkpoint_frame, .cost_milli = 1000,
          .hold = HOLD_TEST_LOOP, .guard = checkpoint_ready },
        { .addr = MAIN_LOOP_HEAD, .name = "checkpoint_idle", .fn = checkpoint_waits, .idle = true,
          .hold = HOLD_TEST_LOOP, .guard = checkpoint_idle },
    };
    NativeRuntime *rt = native_create(table, sizeof table / sizeof table[0], NATIVE_ON);
    native_attach(rt, s);
    return rt;
}

static void checkpoint_case(void)
{
    uint8_t *rom = make_rom(hold_code, sizeof hold_code / sizeof hold_code[0], 0x8600);
    Snes *old = machine(rom);
    NativeRuntime *original = checkpoint_runtime(old);
    checkpoint_enabled = false;
    checkpoint_entries = 0;
    run_frames(old, 20);
    CHECK(checkpoint_entries >= 18, "the original held continuations did not run");
    size_t old_len;
    uint8_t *old_state = snes_state_write(old, &old_len);
    run_frames(old, 30);

    Snes *s = machine(rom);
    NativeRuntime *rt = checkpoint_runtime(s);
    checkpoint_enabled = true;
    checkpoint_entries = 0;
    CHECK(suspended_frame_ends(s, rt, 20) == 0, "a C loop's checkpoint was not saveable");
    CHECK(checkpoint_entries == 1, "a C loop restarted %u times", checkpoint_entries);
    CHECK(s->cpu.s == 0x01FD && snes_bus_peek(&s->bus, 0x01FE) == 0x21 &&
          snes_bus_peek(&s->bus, 0x01FF) == 0x83, "a C loop lost its saved caller frame");
    size_t len;
    uint8_t *state = snes_state_write(s, &len);
    run_frames(s, 30);
    CHECK(!memcmp(s->bus.wram, old->bus.wram, sizeof s->bus.wram) && s->master == old->master &&
          !memcmp(&s->cpu, &old->cpu, offsetof(Cpu, bus)), "the C loop changed the held loop's pace or state");
    for (unsigned kind = 0; kind < 3; kind++) {
        Snes *copy = machine(rom);
        char err[200] = "";
        CHECK(snes_state_read(copy, kind == 2 ? old_state : state, kind == 2 ? old_len : len,
                              err, sizeof err) == 0, "checkpoint load: %s", err);
        if (kind == 1)
            copy->held_wait = 0;
        NativeRuntime *fresh = checkpoint_runtime(copy);
        run_frames(copy, 30);
        CHECK(!native_suspended(fresh) && !memcmp(s->bus.wram, copy->bus.wram, sizeof s->bus.wram) &&
              s->master == copy->master && !memcmp(&s->cpu, &copy->cpu, offsetof(Cpu, bus)),
              "checkpoint reload %u changed the caller, state or pace", kind);
        native_destroy(fresh);
        free(copy);
    }
    native_destroy(rt);
    native_destroy(original);
    free(state);
    free(old_state);
    free(s);
    free(old);
    free(rom);
}

static void warp_loop_contract_case(void)
{
    uint8_t *rom = make_rom(NULL, 0, 0x8600);
    for (unsigned invalid = 0; invalid < 7; invalid++) {
        Snes *s = machine(rom);
        s->cpu.e = false;
        s->cpu.p = CPU_FLAG_M;
        s->cpu.d = 0x0500;
        s->cpu.db = 0;
        s->cpu.pb = 0xFD;
        s->cpu.pc = 0xEAED;
        s->cpu.s = A_TOP;
        m_push16(s, invalid == 6 ? 0xFFFF : 0xDA05);
        set_warp_zero(state_of(s), invalid == 5 ? 1 : 0);
        set_warp_frame_ended(state_of(s), 1);
        if (invalid == 0) s->cpu.e = true;
        if (invalid == 1) s->cpu.p = 0;
        if (invalid == 2) s->cpu.p |= CPU_FLAG_X;
        if (invalid == 3) s->cpu.d = 0;
        if (invalid == 4) s->cpu.db = 0x40;
        warp_next_frame(s);
        CHECK(cpu_pc24(&s->cpu) == 0xFDEAED && s->cpu.s == A_TOP - 2 &&
              warp_frame_ended(state_of(s)) == 1,
              "warp loop refusal %u consumed its frame or return", invalid);
        free(s);
    }
    free(rom);
}

/* Shared frame waits retain the caller's complete return frame and use the
 * one-pass entry's poll and exit time, whether returning or falling through
 * to another held continuation (the menu scheduler's case). */
static unsigned idle_loop_entries, idle_loop_passes;
static bool idle_loop_image = true;
static bool idle_loop_returns;

static bool idle_loop_guard(Snes *s)
{
    return idle_loop_image && s->cpu.d == 0 && s->cpu.db == 0;
}

static void idle_frame_pass(Snes *s)
{
    idle_loop_passes++;
    wait_pass(s);
    if (idle_loop_returns && s->cpu.pc == WAIT_EXIT)
        m_rts(s);
}

static void idle_frame_loop(Snes *s)
{
    idle_loop_entries++;
    do {
        idle_frame_pass(s);
    } while (native_idle_held(s));
}

static NativeRuntime *idle_loop_runtime(Snes *s, bool loop)
{
    NativeEntry table[] = {
        { .addr = 0x008300, .name = "frame_wait", .fn = wait_held,
          .p_mask = 0x30, .p_value = P_M8_X16, .cost_milli = 3000 },
        { .addr = WAIT_LOOP, .name = "frame_wait_loop", .fn = idle_frame_pass,
          .p_mask = CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, .p_value = CPU_FLAG_M,
          .guard = idle_loop_guard, .idle = true, .hold = HOLD_TEST_LOOP,
          .loop = loop ? idle_frame_loop : NULL },
        { .addr = 0x008180, .name = "fresh_idle", .fn = checkpoint_waits,
          .idle = true, .hold = HOLD_TEST_LOOP },
        { .addr = WAIT_EXIT, .name = "idle_finished", .fn = leave_waited,
          .cost_milli = 1000, .hold = HOLD_TEST_LOOP },
    };
    NativeRuntime *rt = native_create(table, 4, NATIVE_ON);
    native_attach(rt, s);
    return rt;
}

static void idle_loop_case(bool returns)
{
    idle_loop_returns = returns;
    uint8_t *rom = make_rom(wait_code, sizeof wait_code / sizeof wait_code[0], 0x8600);
    Snes *old = machine(rom), *s = machine(rom);
    NativeRuntime *before = idle_loop_runtime(old, false);
    NativeRuntime *after = idle_loop_runtime(s, true);
    run_frames(old, 20);
    size_t old_len, len;
    uint8_t *old_state = snes_state_write(old, &old_len);
    idle_loop_entries = idle_loop_passes = 0;
    CHECK(suspended_frame_ends(s, after, 20) == 0, "idle C loop was not saveable");
    CHECK(idle_loop_entries <= 20 && idle_loop_passes > idle_loop_entries * 100,
          "idle loop restarted at each poll: %u entries, %u passes", idle_loop_entries, idle_loop_passes);
    uint8_t *state = snes_state_write(s, &len);
    CHECK(len == old_len && !memcmp(state, old_state, len), "idle loop changed the saved wait or caller frame");
    run_frames(old, 30);
    run_frames(s, 30);
    size_t end_len;
    uint8_t *end = snes_state_write(s, &end_len);
    size_t old_end_len;
    uint8_t *old_end = snes_state_write(old, &old_end_len);
    CHECK(end_len == old_end_len && !memcmp(end, old_end, end_len), "idle loop changed polling or exit time");
    for (unsigned kind = 0; kind < 3; kind++) {
        Snes *copy = machine(rom);
        CHECK(snes_state_read(copy, kind == 2 ? old_state : state, kind == 2 ? old_len : len,
                              NULL, 0) == 0, "idle wait save did not load");
        if (kind == 1) copy->held_wait = 0;
        NativeRuntime *fresh = idle_loop_runtime(copy, true);
        run_frames(copy, 30);
        size_t copy_len;
        uint8_t *copy_state = snes_state_write(copy, &copy_len);
        CHECK(!native_suspended(fresh) && copy_len == end_len && !memcmp(copy_state, end, end_len),
              "idle wait reload %u changed time or caller state", kind);
        free(copy_state);
        native_destroy(fresh);
        free(copy);
    }
    native_destroy(before);
    native_destroy(after);
    free(old_end);
    free(end);
    free(old_state);
    free(state);
    free(old);
    free(s);

    for (unsigned invalid = 0; invalid < 7; invalid++) {
        s = machine(rom);
        after = idle_loop_runtime(s, true);
        run_frames(s, 2);
        unsigned passes = idle_loop_passes;
        if (invalid == 0) s->cpu.e = true;
        if (invalid == 1) s->cpu.p ^= CPU_FLAG_M;
        if (invalid == 2) s->cpu.p |= CPU_FLAG_X;
        if (invalid == 3) s->cpu.p |= CPU_FLAG_D;
        if (invalid == 4) s->cpu.d = 0x100;
        if (invalid == 5) s->cpu.db = 0x40;
        if (invalid == 6) idle_loop_image = false;
        uint16_t saved_s = s->cpu.s;
        CHECK(s->intercept(s->intercept_ctx, s) < 0 && cpu_pc24(&s->cpu) == WAIT_LOOP &&
              s->cpu.s == saved_s && idle_loop_passes == passes && native_suspended(after) == 0,
              "idle resume bypassed contract %u or consumed its caller", invalid);
        idle_loop_image = true;
        native_destroy(after);
        free(s);
    }
    /* An interrupt can start another task or reset the main stack instead
     * of returning. Nothing local to the abandoned wait may block a save. */
    s = machine(rom);
    after = idle_loop_runtime(s, true);
    run_frames(s, 2);
    m_jump_to(s, 0x008180);
    s->cpu.s = 0x07FF;
    s->held_wait = HOLD_TEST_LOOP;
    s->held_wait_s = s->cpu.s;
    CHECK(suspended_frame_ends(s, after, 2) == 0, "a replaced idle checkpoint stayed suspended");
    native_destroy(after);
    free(s);
    free(rom);
}


typedef struct {
    NativeRuntime *rt;
    int32_t (*intercept)(void *, Snes *);
    void *ctx;
    uint8_t *bytes;
    size_t len;
    bool in_handler;
} IdleSave;

static int32_t save_idle_midframe(void *arg, Snes *s)
{
    IdleSave *save = arg;
    uint32_t pc = cpu_pc24(&s->cpu);
    bool at = save->in_handler ? pc >= 0x008600 && pc < 0x008605 : s->hclock > 200 && pc == WAIT_LOOP;
    if (!save->bytes && s->frame >= 5 && at &&
        !native_suspended(save->rt) && !native_owed(save->rt))
        save->bytes = snes_state_write(s, &save->len);
    return save->intercept(save->ctx, s);
}

static void idle_midframe_case(void)
{
    uint8_t *rom = make_rom(wait_code, sizeof wait_code / sizeof wait_code[0], 0x8600);
    for (unsigned variant = 0; variant < 4; variant++) {
        idle_loop_returns = (variant & 1) != 0;
        Snes *s = machine(rom);
        NativeRuntime *rt = idle_loop_runtime(s, true);
        IdleSave save = { .rt = rt, .intercept = s->intercept, .ctx = s->intercept_ctx,
                          .in_handler = (variant & 2) != 0 };
        s->intercept = save_idle_midframe;
        s->intercept_ctx = &save;
        run_frames(s, 12);
        CHECK(save.bytes != NULL, "idle loop had no mid-frame checkpoint, variant %u", variant);
        if (save.bytes) {
            Snes *copy = machine(rom);
            CHECK(snes_state_read(copy, save.bytes, save.len, NULL, 0) == 0, "idle mid-frame state did not load");
            CHECK(save.in_handler ? cpu_pc24(&copy->cpu) >= 0x008600 && cpu_pc24(&copy->cpu) < 0x008605
                                   : copy->hclock > 200 && cpu_pc24(&copy->cpu) == WAIT_LOOP,
                  "idle snapshot missed its mid-frame wait");
            NativeRuntime *fresh = idle_loop_runtime(copy, true);
            run_frames(copy, 12 - (int)copy->frame);
            size_t a_len, b_len;
            uint8_t *a = snes_state_write(s, &a_len), *b = snes_state_write(copy, &b_len);
            CHECK(a_len == b_len && !memcmp(a, b, a_len), "idle mid-frame reload changed time or caller state");
            free(a);
            free(b);
            native_destroy(fresh);
            free(copy);
        }
        free(save.bytes);
        native_destroy(rt);
        free(s);
    }
    free(rom);
}

/* The console's direct vector calls retain the stubs' complete contracts.
 * A bare return is enough to test every status byte without game assets;
 * invalid stubs and emulation mode must leave the interrupt frame intact. */
static void console_call_probe(Snes *s)
{
    for (unsigned status = 0; status < 256; status++) {
        s->cpu.e = true;
        s->cpu.p = (uint8_t)status;
        s->cpu.s = CONSOLE_STACK_TOP;
        console_start(s, 0);
        CHECK(cpu_pc24(&s->cpu) == 0xFDC000 && s->cpu.e && s->cpu.p == status &&
              s->cpu.s == CONSOLE_STACK_TOP, "console boot refusal %u changed its boundary", status);
    }
    for (unsigned line = 0; line < 2; line++) {
        for (unsigned status = 0; status < 256; status++) {
            for (unsigned rejected = 0; rejected < 3; rejected++) {
                s->cpu.e = false;
                s->cpu.p = (uint8_t)status;
                s->cpu.s = A_TOP;
                s->cpu.d = 0x1234;
                s->cpu.db = 0x40;
                set_nmi_vector(state_of(s), rejected == 1 ? 0 : 0x40);
                set_irq_vector(state_of(s), rejected == 1 ? 0 : 0x40);
                m_push8(s, 0xC0);
                m_push16(s, 0x9000);
                m_push8(s, (uint8_t)status);
                s->cpu.e = rejected == 2;
                if (line)
                    console_line_vector(s);
                else
                    console_vblank_vector(s);
                CHECK(cpu_pc24(&s->cpu) == (rejected ? (line ? 0x000504u : 0x000500u) : 0xC09000u),
                      "console vector %u status %u refusal %u lost its destination", line, status, rejected);
                CHECK(s->cpu.s == A_TOP - (rejected ? 4 : 0) && s->cpu.p == status &&
                      s->cpu.d == 0x1234 && s->cpu.db == 0x40,
                      "console vector %u status %u refusal %u changed its caller", line, status, rejected);
            }
        }
    }
    s->cpu.e = false;
    s->cpu.stopped = true;
}

static void console_call_case(void)
{
    uint8_t *rom = make_rom(NULL, 0, 0x8600);
    Snes *s = machine(rom);
    static const NativeEntry entries[] = {
        { .addr = 0x008000, .name = "console_call_probe", .fn = console_call_probe, .cost_milli = 1000 },
        { .addr = 0x000500, .name = "console_vblank_stub", .fn = console_vblank_stub, .cost_milli = 1000,
          .guard = console_vblank_stub_is_set },
        { .addr = 0x000504, .name = "console_line_stub", .fn = console_line_stub, .cost_milli = 1000,
          .guard = console_line_stub_is_set },
    };
    s->cpu.e = false;
    NativeRuntime *rt = native_create(entries, sizeof entries / sizeof entries[0], NATIVE_ON);
    native_attach(rt, s);
    run_frames(s, 5);
    CHECK(s->cpu.stopped, "console call contract probe did not finish");
    native_destroy(rt);
    free(s);
    free(rom);
}

/* The host's interrupt calls have the same pace and caller frames as vector
 * dispatch, including interrupts inside a port's unpaid time. Capture saves
 * between entry and dispatch, and reload both the named event and the older
 * address-only form into a fresh runtime. */
static void driver_vblank(Snes *s)
{
    m_write8(s, 0x40, (uint8_t)(m_read8(s, 0x40) + 1));
    (void)m_read8(s, 0x4210);
    m_rti(s);
}

static bool driver_nests;

static void driver_line(Snes *s)
{
    m_write8(s, 0x41, (uint8_t)(m_read8(s, 0x41) + 1));
    if (driver_nests && m_read8(s, 0x41) == 3 && !m_read8(s, 0x43)) {
        m_write8(s, 0x43, 1);
        s->cpu.nmi_pending = true;
        native_sync_boundary(s);
    }
    (void)m_read8(s, 0x4211);
    m_rti(s);
}

static void driver_work(Snes *s)
{
    m_write8(s, 0x42, (uint8_t)(m_read8(s, 0x42) + 1));
    m_rts(s);
}

static const NativeEntry driver_entries[] = {
    { .addr = 0x008500, .name = "driver_vblank", .fn = driver_vblank, .cost_milli = 12000 },
    { .addr = 0x008510, .name = "driver_line", .fn = driver_line, .cost_milli = 12000 },
    { .addr = 0x008300, .name = "driver_work", .fn = driver_work, .cost_milli = 100000 },
};

static uint8_t *driver_saves[2];
static size_t driver_save_lengths[2];
static int32_t (*driver_dispatch)(void *, Snes *);

static int32_t save_before_interrupt(void *ctx, Snes *s)
{
    unsigned k = s->frame_interrupt - SNES_FRAME_INTERRUPT_VBLANK;
    if (k < 2 && !driver_saves[k]) {
        CHECK(!native_suspended(ctx) && !native_owed(ctx), "queued interrupt has an unsaved C caller");
        driver_saves[k] = snes_state_write(s, &driver_save_lengths[k]);
    }
    return driver_dispatch(ctx, s);
}

static void check_driver_state(Snes *a, Snes *b, const char *what)
{
    size_t alen, blen;
    uint8_t *as = snes_state_write(a, &alen), *bs = snes_state_write(b, &blen);
    CHECK(as && bs && alen == blen && !memcmp(as, bs, alen), "%s: serialized state differs", what);
    free(as);
    free(bs);
}

static void driver_interrupt_case(void)
{
    static const Code code[] = {
        { 0x8000, { 0x20, 0x00, 0x83, 0x80, 0xFB }, 5 },
        { 0x8300, { 0x60 }, 1 },
    };
    uint8_t *rom = make_rom(code, sizeof code / sizeof code[0], 0x8500);
    rom[0xFFEE] = 0x10;
    rom[0xFFEF] = 0x85;
    for (unsigned with_debt = 0; with_debt < 2; with_debt++) {
        driver_nests = with_debt != 0;
        Snes *old = machine(rom), *direct = machine(rom);
        Snes *machines[] = { old, direct };
        NativeRuntime *runtimes[2];
        for (unsigned i = 0; i < 2; i++) {
            Snes *s = machines[i];
            s->cpu.e = false;
            s->cpu.p = CPU_FLAG_M;
            s->cpu.s = A_TOP;
            s->bus.nmitimen = 0x90;
            s->bus.htime = 20;
            s->bus.wram[0x40] = s->bus.wram[0x41] = s->bus.wram[0x42] = 0;
            s->bus.wram[0x43] = 0;
            runtimes[i] = native_create(driver_entries, 2 + (int)with_debt, NATIVE_ON);
            native_attach(runtimes[i], s);
        }
        native_attach_frame_interrupts(runtimes[1], direct, driver_vblank, driver_line);
        if (!with_debt) {
            driver_dispatch = direct->run_frame_interrupt;
            direct->run_frame_interrupt = save_before_interrupt;
        }
        for (unsigned frame = 0; frame < 8; frame++) {
            snes_run_frame(old, 0);
            snes_run_frame(direct, 0);
            check_driver_state(old, direct, "driver interrupt pace");
        }
        CHECK(direct->bus.wram[0x40] == 7 + with_debt && direct->bus.wram[0x41] &&
              !native_failed(runtimes[0]) && !native_failed(runtimes[1]), "driver interrupts did not run");
        if (!with_debt) {
            for (unsigned k = 0; k < 2; k++) {
                CHECK(driver_saves[k] != NULL, "interrupt kind %u was not saved", k);
                if (!driver_saves[k])
                    continue;
                for (unsigned legacy = 0; legacy < 2; legacy++) {
                    Snes *copy = machine(rom);
                    NativeRuntime *fresh = native_create(driver_entries, 2, NATIVE_ON);
                    native_attach(fresh, copy);
                    native_attach_frame_interrupts(fresh, copy, driver_vblank, driver_line);
                    CHECK(snes_state_read(copy, driver_saves[k], driver_save_lengths[k], NULL, 0) == 0,
                          "queued interrupt state did not load");
                    CHECK(copy->frame_interrupt == k + SNES_FRAME_INTERRUPT_VBLANK && copy->run_frame_interrupt,
                          "queued interrupt or host hook was lost on load");
                    if (legacy)
                        copy->frame_interrupt = SNES_FRAME_INTERRUPT_NONE;
                    while (copy->frame < direct->frame)
                        snes_run_frame(copy, 0);
                    check_driver_state(direct, copy, "queued interrupt reload");
                    native_destroy(fresh);
                    free(copy);
                }
                free(driver_saves[k]);
                driver_saves[k] = NULL;
            }
        }
        native_destroy(runtimes[0]);
        native_destroy(runtimes[1]);
        free(old);
        free(direct);
    }
    free(rom);
}

static unsigned driver_boot_calls;
static void driver_boot(Snes *s)
{
    driver_boot_calls++;
    m_write8(s, 0x44, 0xA7);
    s->cpu.e = false;
    s->cpu.stopped = true;
}

static void driver_power_on_case(void)
{
    const NativeEntry entry = { .addr = 0x008000, .name = "driver_boot", .fn = driver_boot,
                                .emulation = true, .cost_milli = 1000 };
    uint8_t *rom = make_rom(NULL, 0, 0x8500);
    Snes *old = machine(rom), *direct = machine(rom);
    size_t len;
    uint8_t *state = snes_state_write(direct, &len);
    CHECK(direct->frame_interrupt == SNES_FRAME_POWER_ON, "power-on did not queue the driver's start");
    NativeRuntime *before = native_create(&entry, 1, NATIVE_ON);
    NativeRuntime *after = native_create(&entry, 1, NATIVE_ON);
    native_attach(before, old);
    native_attach(after, direct);
    native_attach_frame_interrupts(after, direct, NULL, NULL);
    native_attach_power_on(after, direct, driver_boot);
    driver_boot_calls = 0;
    run_frames(old, 2);
    run_frames(direct, 2);
    CHECK(driver_boot_calls == 2, "power-on did not call each boot once");
    check_driver_state(old, direct, "driver power-on pace");
    for (unsigned legacy = 0; legacy < 2; legacy++) {
        Snes *copy = machine(rom);
        NativeRuntime *fresh = native_create(&entry, 1, NATIVE_ON);
        native_attach(fresh, copy);
        native_attach_frame_interrupts(fresh, copy, NULL, NULL);
        native_attach_power_on(fresh, copy, driver_boot);
        CHECK(snes_state_read(copy, state, len, NULL, 0) == 0, "power-on save did not load");
        if (legacy) copy->frame_interrupt = SNES_FRAME_INTERRUPT_NONE;
        run_frames(copy, 2);
        check_driver_state(direct, copy, "driver power-on reload");
        native_destroy(fresh);
        free(copy);
    }
    native_destroy(before);
    native_destroy(after);
    free(state);
    free(old);
    free(direct);
    free(rom);
}

static bool driver_contract_image;
static bool driver_contract_guard(Snes *s)
{
    return driver_contract_image && s->cpu.d == 0x1234 && s->cpu.db == 0x7E;
}

static void driver_contract_case(void)
{
    const NativeEntry entry = { .addr = 0x008500, .name = "driver_contract", .fn = driver_vblank,
        .p_mask = CPU_FLAG_D | CPU_FLAG_M | CPU_FLAG_X, .p_value = CPU_FLAG_M,
        .guard = driver_contract_guard, .cost_milli = 12000 };
    uint8_t *rom = make_rom(NULL, 0, 0x8500);
    for (unsigned invalid = 1; invalid <= 8; invalid++) {
        Snes *s = machine(rom);
        s->cpu.e = false;
        s->cpu.s = A_TOP;
        m_push8(s, 0);
        m_push16(s, 0x8000);
        m_push8(s, CPU_FLAG_M);
        s->cpu.pc = invalid == 8 ? 0x8501 : 0x8500;
        s->cpu.e = invalid == 1;
        s->cpu.p = CPU_FLAG_M;
        if (invalid == 2) s->cpu.p |= CPU_FLAG_D;
        if (invalid == 3) s->cpu.p &= (uint8_t)~CPU_FLAG_M;
        if (invalid == 4) s->cpu.p |= CPU_FLAG_X;
        s->cpu.d = invalid == 5 ? 0 : 0x1234;
        s->cpu.db = invalid == 6 ? 0 : 0x7E;
        driver_contract_image = invalid != 7;
        NativeRuntime *rt = native_create(&entry, 1, NATIVE_ON);
        native_attach(rt, s);
        native_attach_frame_interrupts(rt, s, driver_vblank, NULL);
        s->frame_interrupt = SNES_FRAME_INTERRUPT_VBLANK;
        CHECK(s->run_frame_interrupt(s->frame_interrupt_ctx, s) == -1 &&
              s->cpu.s == A_TOP - 4 && s->cpu.pc == (invalid == 8 ? 0x8501 : 0x8500) &&
              !s->frame_interrupt, "driver contract refusal %u consumed the caller", invalid);
        native_destroy(rt);
        free(s);
    }
    free(rom);
}

/* A game-over restart intentionally replaces the stack. A refused title
 * contract must leave that fresh stack and the original destination, with
 * no title work performed. Decimal mode is not part of this contract. */
static void field_restart_probe(Snes *s)
{
    for (unsigned decimal = 0; decimal < 2; decimal++) {
        for (unsigned invalid = 0; invalid < 3; invalid++) {
            s->cpu.e = invalid == 0;
            uint8_t status = invalid == 1 ? 0 : CPU_FLAG_M;
            if (invalid == 2) status |= CPU_FLAG_X;
            if (decimal) status |= CPU_FLAG_D;
            s->cpu.p = status;
            s->cpu.s = A_TOP;
            set_field_brightness(state_of(s), 0);
            field_game_over_fades(s);
            CHECK(cpu_pc24(&s->cpu) == 0xC0000E && s->cpu.s == CONSOLE_STACK_TOP &&
                  s->cpu.p == (status | CPU_FLAG_I) && s->cpu.e == (invalid == 0),
                  "field restart refusal %u, decimal %u changed its contract or destination", invalid, decimal);
        }
    }
    s->cpu.e = false;
    s->cpu.stopped = true;
}

static void field_restart_case(void)
{
    const NativeEntry entry = { .addr = 0x008000, .name = "field_restart_probe", .fn = field_restart_probe,
                                .cost_milli = 1000 };
    uint8_t *rom = make_rom(NULL, 0, 0x8600);
    Snes *s = machine(rom);
    s->cpu.e = false;
    NativeRuntime *rt = native_create(&entry, 1, NATIVE_ON);
    native_attach(rt, s);
    run_frames(s, 2);
    CHECK(s->cpu.stopped && !native_failed(rt), "field restart contract probe did not finish");
    native_destroy(rt);
    free(s);
    free(rom);
}

int main(void)
{
    field_restart_case();
    driver_interrupt_case();
    driver_power_on_case();
    driver_contract_case();
    console_call_case();
    checkpoint_case();
    idle_loop_case(true);
    idle_loop_case(false);
    idle_midframe_case();
    warp_loop_contract_case();
    menu_contract_case();
    menu_script_contract_case();
    menu_world_case();
    tasks_case("a yield, shipping mode", NATIVE_ON, RUN_THROUGH);
    tasks_case("a yield, shadow mode", NATIVE_SHADOW, RUN_THROUGH);
    tasks_case("a task dropped, shipping mode", NATIVE_ON, DROP_THE_TASK);
    tasks_case("a task dropped, shadow mode", NATIVE_SHADOW, DROP_THE_TASK);
    tasks_case("a task started afresh, shipping mode", NATIVE_ON, RESTART_THE_TASK);
    tasks_case("the switch has yet to record the wait", NATIVE_ON, WAIT_NOT_RECORDED);
    parked_save_case("saved parked, loaded in the shipping mode", NATIVE_ON);
    parked_save_case("saved parked, loaded in shadow mode", NATIVE_SHADOW);
    interrupt_at_yield = true;
    tasks_case("interrupt pending at the yield entry", NATIVE_ON, WAIT_NOT_RECORDED);
    switch_in_c = true;
    interrupt_at_yield = true;
    tasks_case("interrupt pending at a C task switch", NATIVE_ON, WAIT_NOT_RECORDED);
    tasks_case("a C task switch", NATIVE_ON, RUN_THROUGH);
    tasks_case("a C task switch, task dropped", NATIVE_ON, DROP_THE_TASK);
    tasks_case("a C task switch, task restarted", NATIVE_ON, RESTART_THE_TASK);
    tasks_case("a C task switch, wait not yet recorded", NATIVE_ON, WAIT_NOT_RECORDED);
    parked_save_case("saved after a C task switch, loaded in shipping mode", NATIVE_ON);
    parked_save_case("saved after a C task switch, loaded in shadow mode", NATIVE_SHADOW);
    CHECK(c_switches > 0, "the C task switch never ran");
    switch_in_c = false;
    menu_answer_case();
    held_wait_case();
    repeated_wait_case();
    named_hold_case();
    returning_hold_case();
    other_stack_hold_case(false);
    other_stack_hold_case(true);
    other_stack_hold_case(2);
    empty_boundary_case();
    scene_wait_case();
    if (failures)
        printf("%d failure(s)\n", failures);
    else
        printf("suspend_test: all passed\n");
    return failures ? 1 : 0;
}
