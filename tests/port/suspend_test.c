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
#include "core/machine_forms.h"
#include "menu/bridge.h"
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
    NativeRuntime *rt = native_create(&entry, 1, mode);
    native_attach(rt, s);
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
    m_hold_at(s, MAIN_LOOP_HEAD, DC_A | DC_FLAGS, HOLD_TEST_LOOP);
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
    table[0] = (NativeEntry){ .addr = 0x008100, .name = "hold_start", .fn = hold_start, .p_mask = 0x30,
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

static void other_stack_hold_case(void)
{
    static const Code code[] = {
        { 0x8000, { 0x78, 0x18, 0xFB, 0xC2, 0x10, 0xE2, 0x20, 0xA2, 0xFF, 0x03, 0x9A,
                    0xA9, 0x80, 0x8D, 0x00, 0x42, 0x4C, 0x00, 0x82 }, 19 },
        { 0x8600, { 0xE6, 0x60, 0x40 }, 3 },
    };
    const NativeEntry table[] = {
        { .addr = 0x008200, .name = "caller", .fn = other_stack_caller, .cost_milli = 1000 },
        { .addr = 0x008300, .name = "other_stack", .fn = other_stack_wait, .cost_milli = 1000 },
        { .addr = MAIN_LOOP_HEAD, .name = "other_stack_wait", .fn = other_stack_look,
          .idle = true, .hold = HOLD_TEST_LOOP },
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
    for (unsigned form = 0; form < 3; form++) {
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
            else
                back = menu_run_or_call(s, YIELD_SITE, SWITCH, DC_A, menu_contract_callee);
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

int main(void)
{
    menu_contract_case();
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
    named_hold_case();
    returning_hold_case();
    other_stack_hold_case();
    empty_boundary_case();
    scene_wait_case();
    if (failures)
        printf("%d failure(s)\n", failures);
    else
        printf("suspend_test: all passed\n");
    return failures ? 1 : 0;
}
