/* The menus' tasks: the screens' work, a turn of each a frame.
 *
 * The menus do their work as up to eight tasks (menu/menu.h): the screen's
 * main task, which reads the pad and moves between the screen's steps, and
 * the helpers a screen starts and stops -- the description typed a letter at
 * a time, the cursor's sprite, the scroll bar, the cursors a screen holds,
 * the healing's rising numbers. A task can wait for the next frame anywhere
 * in its work and go on from there next frame, or wait some frames, or end.
 *
 * Each frame, once the vblank handler has sent what the last frame queued
 * (menu_wait_ready_loop, menu/frame.c), every task gets its turn in order:
 * the countdown of a task that is waiting runs down a frame, and a task whose
 * wait is over goes on from where it waited, or from its start if it is
 * starting; its turn lasts until it waits or ends. Once every task has had
 * its turn this frame, the menus wait for the next one.
 *
 * On the original's machine each task runs on a stack of its own, and where
 * a task waits is its stack: the registers it had go onto it and its stack
 * pointer into its record, and going on puts them back. That is the entries'
 * part, at the bottom of this file; which task goes on, and how, is the
 * scheduler's, over the tasks' records. A build with no machine runs each
 * task as a coroutine (runtime/port/fiber.h). */
#include "menu/menu.h"

#include "charge.h"
#include "core/program.h"
#include "core/tasks.h"
#include "menu/description.h"
#include "menu/fades.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/gate_screen.h"
#include "menu/healing.h"
#include "menu/held_cursors.h"
#include "menu/sound.h"
#include "menu/sprites.h"
#include "original_effects.h"
#include "platform/timing.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"

/* What a task's record says it is doing. The original runs a task whose
 * state is anything but WAITING from its start, and takes no turns for one
 * whose state is 0 or past $80 (it tests the sign of the state less one). */
typedef enum { TASK_STOPPED = 0, TASK_STARTING = MENU_TASK_STARTING, TASK_WAITING = 2, TASK_RUNNING = 3 } TaskState;
enum { TAKES_NO_TURNS = 0x80 };

static bool takes_turns(uint8_t state) { return !((uint8_t)(state - 1) & TAKES_NO_TURNS); }

/* The task's record, as the original's index registers name it. */
enum { RECORD_BYTES = 8 };
static unsigned running_task(const GameState *g) { return menu_current_task(g) / RECORD_BYTES; }

/* The task that goes on next, if any, and what it was doing: waiting, to go
 * on from where it waited, or anything else, to run from its start. */
typedef struct {
    bool due;
    unsigned task;
    TaskState was;
} TaskTurn;

static bool turn_starts(const TaskTurn *turn) { return turn->was != TASK_WAITING; }

// $C2:81D1
/* The next task due a turn, from `task` on: each waiting task's countdown
 * runs down a frame, and the first whose wait is over runs. Once there is
 * none, the frame's turns are over: the menus wait for the vblank handler to
 * send what they queued. */
static TaskTurn next_turn(GameState *g, unsigned task)
{
    for (; task < MENU_TASKS; task++) {
        uint8_t state = menu_tasks_state(g, task);
        if (!takes_turns(state))
            continue;
        uint8_t left = (uint8_t)(menu_tasks_countdown(g, task) - 1);
        set_menu_tasks_countdown(g, task, left);
        if (left != 0)
            continue;
        set_menu_current_task(g, (uint8_t)(task * RECORD_BYTES));
        set_menu_tasks_state(g, task, TASK_RUNNING);
        return (TaskTurn){ true, task, (TaskState)state };
    }
    set_menu_vblank_done(g, 0);
    return (TaskTurn){ false, 0, TASK_STOPPED };
}

// $C2:821E, $C2:822B
/* The running task waits `frames` frames (a frame, mostly), in the screen
 * step it is in; the next task goes on. Where it goes on from is the
 * machine's stack, or the task's coroutine. The task that waited. */
static unsigned record_the_wait(GameState *g, uint8_t frames)
{
    unsigned task = running_task(g);
    set_menu_tasks_state(g, task, TASK_WAITING);
    set_menu_tasks_countdown(g, task, frames);
    set_menu_tasks_step(g, task, menu_screen_step(g));
    return task;
}

static TaskTurn task_waits(GameState *g, uint8_t frames) { return next_turn(g, record_the_wait(g, frames) + 1); }

// $C2:8216
/* The running task ends; the next task goes on. */
static unsigned record_the_end(GameState *g)
{
    unsigned task = running_task(g);
    set_menu_tasks_state(g, task, TASK_STOPPED);
    return task;
}

static TaskTurn task_ends(GameState *g) { return next_turn(g, record_the_end(g) + 1); }

/* A task going on from where it waited is back in the screen step it waited
 * in. */
static void task_goes_on(GameState *g, unsigned task) { set_menu_screen_step(g, menu_tasks_step(g, task)); }

/* For the original's code and shadow mode: a task's start jumps through its
 * start in the menus' first cell. */
static void leave_the_start(GameState *g, unsigned task) { set_menu_scratch_task_start(g, menu_tasks_start(g, task)); }

/* A task starting starts in the menus' first screen step. The original clears
 * the step and the task's own as words, and the bytes after them with them:
 * the one after the screen step is other modes' (the sprite-object mode's
 * upload queue, the map's rows), which the menus' tasks never run beside. */
static uint16_t task_starts(GameState *g, unsigned task)
{
    set_menu_tasks_step(g, task, 0);
    set_menu_tasks_step_high(g, task, 0);
    set_menu_screen_step(g, 0);
    set_menu_screen_step_high(g, 0);
    ORIGINAL_EFFECT(leave_the_start(g, task));
    return menu_tasks_start(g, task);
}

/* ---- The entries ------------------------------------------------------------------------------- */

/* Each task's stack lies below the next's: the first's top at $0A3F, 64
 * bytes a task. */
enum { FIRST_TASK_STACK = 0x0A3F, TASK_STACK_BYTES = 64 };
enum { MENUS_BANK = 0xC2, THE_MAIN_STACK = 0x09FF, MENUS_DATA_BANK = 0x7E };

/* Where the menus wait for the vblank handler to have sent the frame's
 * uploads (menu/frame.c's idle entry), which reads none of the registers, and
 * where the frame's turns start once it has. */
enum { WAIT_FOR_THE_UPLOADS = 0xC281CA, THE_UPLOADS_SENT = 0xC281CF };
enum { THE_WAIT_READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

/* Back into a task where it waited: its registers off its stack, as it
 * kept them when it waited (m_keep_registers_to_switch), and the return into
 * its code. A is left with the stack's high byte over the step, as the
 * original's loads leave it. */
static void go_back_into(Snes *s, unsigned task)
{
    GameState *g = state_of(s);
    task_goes_on(g, task);
    uint16_t place = menu_tasks_stack(g, task);
    s->cpu.a = (uint16_t)((place & 0xFF00) | menu_tasks_step(g, task));
    m_switch_back(s, place);
}

/* Where the game's tasks start (the menus' screen, the cursor's sprite, a
 * fade's wait, the description's typing, the scroll bar, the held cursors, the records, the
 * healing's numbers, the gate's song): what each reads of the registers the
 * scheduler's jump leaves is its page, its data bank and, two of them, A's
 * high byte, which the start's address leaves there. The rest are dead
 * (tools/dontcare_check.py checks each row). */
static const uint16_t task_starts_known[] = {
    0x826F, 0x83BD, 0x89F2, 0x8A45, 0x8AB2, 0x8AF5, 0x8BB8, 0x9016, 0xCBBF, 0xCECE, 0xE91B, 0xF657, 0xF9B2, 0xFA06, 0xFA9F,
};
enum { TASK_STARTS_KNOWN = sizeof task_starts_known / sizeof task_starts_known[0] };
enum { A_START_READS_ITS_PAGE = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

static bool start_is_known(uint16_t at)
{
    for (unsigned i = 0; i < TASK_STARTS_KNOWN; i++)
        if (task_starts_known[i] == at)
            return true;
    return false;
}

/* A task from its start, on its own stack, 16-bit, on the menus' page, its
 * start's address in A. */
static void start(Snes *s, const TaskTurn *turn)
{
    GameState *g = state_of(s);
    uint16_t at = task_starts(g, turn->task);
    s->cpu.s = (uint16_t)(FIRST_TASK_STACK + turn->task * TASK_STACK_BYTES);
    s->cpu.d = 0;
    s->cpu.a = at;
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    for (unsigned i = 0; i < TASK_STARTS_KNOWN; i++)
        if (task_starts_known[i] == at)
            m_stop_at(s, (uint32_t)MENUS_BANK << 16 | task_starts_known[i], A_START_READS_ITS_PAGE);
}

/* For shadow mode: the bytes the original's way onto the menus' page leaves
 * just below a stack, which it puts the page's number through. */
static void leave_the_page_below(Snes *s, uint16_t stack)
{
    m_write8(s, stack, 0);
    m_write8(s, (uint16_t)(stack - 1), 0);
}

/* No task goes on this frame: back on the menus' main stack and page, the
 * data bank work RAM's, to wait for the frame's uploads to be sent. The
 * menus are in this wait at nearly every frame's end, so a saved machine has
 * to hold it: the shipping mode leaves it to the loop's idle entry, which goes
 * on into the next frame's turns as the port does, and the port stops at the
 * loop (tm_wait_for_handlers_held). False there. */
static bool wait_for_the_next_frame(Snes *s)
{
    ORIGINAL_EFFECT(leave_the_page_below(s, THE_MAIN_STACK));
    s->cpu.s = THE_MAIN_STACK;
    s->cpu.db = MENUS_DATA_BANK;
    s->cpu.d = 0;
    m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
    const GameState *g = state_of(s);
    while (!menu_vblank_done(g))
        if (!tm_wait_for_handlers_held(m_platform(s),
                                       PF_LOOP_HELD(WAIT_FOR_THE_UPLOADS, THE_UPLOADS_SENT, THE_WAIT_READS_NOTHING)))
            return false;
    return true;
}

/* The next frame's turns go on here where the runtime would run them where
 * the loop leaves (menu_run_the_tasks, whose guard looks at the tasks'
 * records); otherwise the port stops there, for it to run what it has. */
static bool the_turns_go_on_here(Snes *s)
{
    if (menu_runs_as_port(s, THE_UPLOADS_SENT, menu_run_the_tasks))
        return true;
    m_stop_at(s, THE_UPLOADS_SENT, THE_WAIT_READS_NOTHING);
    return false;
}

static void take_turn(Snes *s, TaskTurn turn)
{
    while (!turn.due) {
        if (!wait_for_the_next_frame(s) || !the_turns_go_on_here(s))
            return;
        GameState *g = state_of(s);
        TIMED_AS(g, THE_UPLOADS_SENT, turn = next_turn(g, 0));
    }
    if (turn_starts(&turn))
        start(s, &turn);
    else
        go_back_into(s, turn.task);
}

/* A turn that isn't due: the menus wait for the next frame. */
static const TaskTurn NO_TURN = { false, 0, TASK_STOPPED };

/* The task's registers kept on its stack, and where its stack is kept in its
 * record: where it goes on from. Then on the menus' page. */
static void wait_entry(Snes *s, uint8_t frames)
{
    GameState *g = state_of(s);
    m_keep_registers_to_switch(s);
    set_menu_tasks_stack(g, running_task(g), s->cpu.s);
    ORIGINAL_EFFECT(leave_the_page_below(s, s->cpu.s));
    s->cpu.d = 0;
    take_turn(s, task_waits(g, frames));
}

// $C2:821E
/* The running task waits for the next frame. */
void menu_task_waits_a_frame(Snes *s) { wait_entry(s, 1); }

// $C2:822B
/* The running task waits A's low byte of frames. */
void menu_task_waits_frames(Snes *s) { wait_entry(s, (uint8_t)s->cpu.a); }

// $C2:8216
void menu_task_ends(Snes *s)
{
    GameState *g = state_of(s);
    take_turn(s, task_ends(g));
}

// $C2:81CF
/* The frame's turns, from the first task, once the vblank handler has sent
 * the frame's uploads. */
void menu_run_the_tasks(Snes *s) { take_turn(s, next_turn(state_of(s), 0)); }

// $C2:81B9
/* The menus, once open, wait for the first frame's uploads, then take the
 * frame's turns. */
void menu_wait_for_the_first_frame(Snes *s) { take_turn(s, NO_TURN); }

/* For the runtime, of a port that waits in the middle of a task's work
 * (menu_yield): whether the task that waited with the wait's return just
 * above `s_after` can still go on from there. It can while it waits there --
 * its record says so, and keeps the stack under the registers the wait kept
 * -- and while it is the task the scheduler is taking back into it, below the
 * wait. A task stopped (as every task is when the menus close) or started
 * afresh, which runs from its stack's top over the wait, never goes on from
 * it. Only peeks. */
enum { RETURN_BYTES = 2, KEPT_WITH_BYTE_INDEXES = 5, KEPT_WITH_WORD_INDEXES = 7 };

static bool task_stack_holds(uint16_t at, unsigned *task)
{
    uint16_t bottom = FIRST_TASK_STACK + 1 - TASK_STACK_BYTES;
    if (at < bottom || at > FIRST_TASK_STACK + (MENU_TASKS - 1) * TASK_STACK_BYTES)
        return false;
    *task = (unsigned)(at - bottom) / TASK_STACK_BYTES;
    return true;
}

bool menu_task_goes_on_from(Snes *s, uint16_t s_after)
{
    unsigned task;
    if (!task_stack_holds(s_after, &task))
        return false;
    uint16_t top = (uint16_t)(FIRST_TASK_STACK + task * TASK_STACK_BYTES), now = s->cpu.s;
    vars_peek(true);
    const GameState *g = state_of(s);
    uint8_t state = menu_tasks_state(g, task);
    uint16_t kept = menu_tasks_stack(g, task);
    bool going_back = state == TASK_RUNNING && running_task(g) == task;
    vars_peek(false);
    if (state == TASK_WAITING)
        return kept == s_after - RETURN_BYTES - KEPT_WITH_BYTE_INDEXES ||
               kept == s_after - RETURN_BYTES - KEPT_WITH_WORD_INDEXES;
    return going_back && !(now > s_after && now <= top);
}

/* ---- The tasks as the menus open ------------------------------------------------------------ */

/* The menus open with none of their tasks but the two that run all the
 * time, each from its head: the main task in the second record and the
 * cursor's in the fourth, its sprite the pointing hand. The original clears
 * the records with a block move from the first's first byte. */
enum { MAIN_TASK = 1, MAIN_TASK_HEAD = 0x826F, CURSOR_TASK_HEAD = 0xE91B, THE_POINTING_HAND = 0x3020 };
enum { THE_RECORDS_BYTES = MENU_TASKS * RECORD_BYTES };

static void clear_the_tasks(GameState *g)
{
    for (unsigned task = 0; task < MENU_TASKS; task++) {
        set_menu_tasks_state(g, task, TASK_STOPPED);
        set_menu_tasks_countdown(g, task, 0);
        set_menu_tasks_start(g, task, 0);
        set_menu_tasks_stack(g, task, 0);
        set_menu_tasks_step(g, task, 0);
        set_menu_tasks_step_high(g, task, 0);
    }
    ORIGINAL_EFFECT(charge_block_move(g, THE_RECORDS_BYTES - 2));
}

// $C2:8190
/* The tasks started as the menus open. */
static void start_the_tasks(GameState *g)
{
    clear_the_tasks(g);
    menu_start_task(g, MAIN_TASK, MAIN_TASK_HEAD);
    set_menu_cursor_sprite(g, THE_POINTING_HAND);
    menu_start_task(g, MENU_CURSOR_TASK, CURSOR_TASK_HEAD);
}

/* Then the wait for the first frame's uploads and the frame's turns (its
 * entry's, $C2:81B9). */
void menu_start_the_tasks(Snes *s)
{
    start_the_tasks(state_of(s));
    NATIVE_TIME_AS(s, 0xC281B9, menu_wait_for_the_first_frame(s));
}

bool menu_tasks_can_start(Snes *s) { return m_bank_reaches_low_ram(s); }

/* ---- The tasks that run all the time ------------------------------------------------------- */

/* The two tasks the menus start as they open and stop only as they close are
 * each a loop: a frame's work, then the wait for the next frame. The main
 * task's work is the running screen's step (menu/screens.c), which may wait
 * for frames of its own inside; the cursor's is the menus' sprites hidden and
 * the cursor placed (menu/cursor.c). The scroll bar's task, which a list's
 * screen starts and stops, is the same loop over a frame of the bar's arrows
 * (menu/held_cursors.c), and so is the records' (menu/sprites.c). A task's
 * wait in the middle of its work, which goes on after it rather than round
 * its loop, is menu_yield's (menu/bridge.h). */
enum { MAIN_TASK_WAIT = 0xC2826C, CURSOR_TASK_WAIT = 0xC2E91E, SCROLL_BAR_TASK_WAIT = 0xC28C30 };

/* The loop's wait, from its JSR at `site`: run as the JSR reaches it where
 * the runtime would run it, and otherwise left to the JSR. Either way the
 * next task goes on, and this one goes on from the JSR's return on its next
 * turn, into its work again. The wait keeps the registers on the task's
 * stack for that return, where the loops read none of them. */
enum { THE_LOOPS_READ_NOTHING_AFTER_IT = MENU_TASK_WAIT_READS_NOTHING };

/* The index registers the wait keeps, just under the loop's return (X, then
 * Y below it, each a byte or a word), are what the loop's stop declares it
 * doesn't read after the wait (tools/dontcare_check.py checks the
 * declaration at the wait's JSR, through the wait's keeping them, and
 * tests/test_menu_task_waits.py at the loops' returns): the loops load both
 * before they read either. So where the declaration has them, the port's
 * own, which the work before the wait leaves in them and which aren't the
 * original's, aren't compared while the task waits. */
static void leave_the_kept_index_registers_uncompared(Snes *s, uint16_t dont_care, bool widths_its_own)
{
    enum { RETURN_BYTES = 2, FLAGS_BYTES = 1 };
    uint16_t bytes = m_x8(s) ? 1 : 2;
    uint16_t x_at = (uint16_t)(s->cpu.s - RETURN_BYTES - bytes + 1), y_at = (uint16_t)(x_at - bytes);
    if (dont_care & DC_X)
        native_stack_dont_care(s, x_at, bytes);
    if (dont_care & DC_Y)
        native_stack_dont_care(s, y_at, bytes);
    if (widths_its_own && (dont_care & DC_FLAGS) == DC_FLAGS)
        native_stack_dont_care(s, (uint16_t)(y_at - FLAGS_BYTES), FLAGS_BYTES);
}

/* Under what the wait keeps, the task's stack lies below its S while it
 * waits: nothing reads it, and what the original's work left there (its
 * subroutines' frames, popped) is the original's own. Shadow mode leaves out
 * what was popped of the stack the original is on, and the wait moves to
 * another; so the task's stack under what it keeps isn't compared either. */
static void leave_the_task_s_stack_below_uncompared(Snes *s)
{
    unsigned task;
    uint16_t sp = s->cpu.s;
    if (!task_stack_holds(sp, &task))
        return;
    uint16_t bottom = (uint16_t)(FIRST_TASK_STACK + 1 - TASK_STACK_BYTES + task * TASK_STACK_BYTES);
    uint16_t kept = (uint16_t)(RETURN_BYTES + (m_x8(s) ? KEPT_WITH_BYTE_INDEXES : KEPT_WITH_WORD_INDEXES));
    uint16_t below = (uint16_t)(sp - kept);
    if (below >= bottom)
        native_stack_dont_care(s, bottom, (uint16_t)(below - bottom + 1));
}

/* Under them the wait keeps the flags, the widths with them. Where a loop
 * sets its widths itself, as the original's code has them at the wait (the
 * scroll bar's: A 8-bit from its head, the index registers 16-bit as the
 * task starts), only the flags in that byte can differ, which the loops
 * don't read after the wait either; so that byte isn't compared there. */
/* The wait in a task's loop on the task switch's `wait` (its port `entry`),
 * which reads `reads` of the registers; the frames' wait reads A. */
static void wait_on(Snes *s, uint32_t site, uint32_t wait, void (*entry)(Snes *), uint16_t reads, bool widths_its_own)
{
    if (menu_runs_as_port(s, wait, entry)) {
        leave_the_kept_index_registers_uncompared(s, reads, widths_its_own);
        leave_the_task_s_stack_below_uncompared(s);
        menu_run(s, site, wait, entry);
    } else {
        m_stop_at(s, site, reads);
    }
}

static void wait_in_the_loop(Snes *s, uint32_t site, bool widths_its_own)
{
    wait_on(s, site, MENU_TASK_WAITS_A_FRAME, menu_task_waits_a_frame, THE_LOOPS_READ_NOTHING_AFTER_IT, widths_its_own);
}

void menu_wait_in_the_task_loop(Snes *s, uint32_t site, bool widths_its_own)
{
    wait_in_the_loop(s, site, widths_its_own);
}

/* The same for frames, A's low byte of them: the task switch's wait for
 * frames ($C2:822B) reads A, and nothing else. */
enum { THE_LOOPS_READ_THE_FRAMES = MENU_TASK_WAIT_READS_THE_FRAMES };

void menu_wait_frames_in_the_task_loop(Snes *s, uint32_t site, uint8_t frames, bool widths_its_own)
{
    m_set_a8(s, frames);
    wait_on(s, site, MENU_TASK_WAITS_FRAMES, menu_task_waits_frames, THE_LOOPS_READ_THE_FRAMES, widths_its_own);
}

/* The records' task's loop: a frame of the records' sprites (menu/sprites.c),
 * then the wait, which it waits in with A 16-bit and the index registers
 * 8-bit, set at its head. */
enum { RECORDS_TASK_WAIT = 0xC2F696 };

void menu_records_task_waits(Snes *s) { wait_in_the_loop(s, RECORDS_TASK_WAIT, true); }

/* A loop's turn takes a wait for the next frame, as C or on the machine;
 * false where the task never came back to it. */
typedef bool (*LoopWait)(void *ctx);

// $C2:826F
/* The main task's turn, from its start and from its wait: the screen's step,
 * then the wait. The step is the screen's (menu/screens.c), as C or on the
 * machine; false where either went no further. */
static bool main_task_turn(bool (*screen_step)(void *ctx), LoopWait wait, void *ctx)
{
    return screen_step(ctx) && wait(ctx);
}

// $C2:E91B, $C2:E921
/* The cursor's task's turn: the menus' sprites hidden and the cursor placed,
 * then the wait. */
static bool cursor_task_turn(GameState *g, LoopWait wait, void *ctx)
{
    TIMED_AS(g, 0xC2E92D, menu_reset_sprites_and_cursor(g));
    return wait(ctx);
}

/* On the machine a loop's turn is one call of its entry: the wait goes on
 * into the next task, and the loop's next turn is the entry again, from the
 * wait's return. */
static bool screen_step_on_the_machine(void *ctx) { return menu_run_the_screen_s_step(ctx); }

static bool main_task_waits_on_the_machine(void *ctx)
{
    wait_in_the_loop(ctx, MAIN_TASK_WAIT, false);
    return false;
}

static bool cursor_task_waits_on_the_machine(void *ctx)
{
    wait_in_the_loop(ctx, CURSOR_TASK_WAIT, false);
    return false;
}

void menu_run_the_main_task(Snes *s)
{
    (void)main_task_turn(screen_step_on_the_machine, main_task_waits_on_the_machine, s);
}

// $C2:826C
/* The main task's wait, where a step came back to it through the original's
 * code (one that ended in a sibling's routine). */
void menu_main_task_waits(Snes *s) { wait_in_the_loop(s, MAIN_TASK_WAIT, false); }

/* The cursor's task (MENU_CURSOR_TASK), from its start and from its wait. */
void menu_run_the_cursor_task(Snes *s) { (void)cursor_task_turn(state_of(s), cursor_task_waits_on_the_machine, s); }

// $C2:8BB8, $C2:8C33
/* The scroll bar's task (MENU_SCROLL_BAR_TASK), from its start and from its
 * wait: a frame of its arrows, then the wait, which it waits in with A 8-bit
 * and the index registers 16-bit. */
void menu_run_the_scroll_bar_task(Snes *s)
{
    menu_set_widths(s, true, false);
    menu_draw_the_scroll_bar(state_of(s));
    wait_in_the_loop(s, SCROLL_BAR_TASK_WAIT, true);
}

/* ---- As C, on coroutines -------------------------------------------------------------------- */

/* With no machine each task is a coroutine (core/tasks.h), whose stack is
 * where it goes on from; the records say which takes its turn and when, as
 * they do on the machine, and the scheduler above is the same one. A task's
 * work as C is found by its start, as the original's is by its address. */
typedef void (*MenuTaskAsC)(GameState *g, Platform *pf);

static void main_task(GameState *g, Platform *pf);
static void cursor_task(GameState *g, Platform *pf);
static void scroll_bar_task(GameState *g, Platform *pf);
static void records_task(GameState *g, Platform *pf);
static void held_cursor_task(GameState *g, Platform *pf);
static void list_cursor_task(GameState *g, Platform *pf);
static void member_cursor_task(GameState *g, Platform *pf);
static void members_cursors_task(GameState *g, Platform *pf);

enum { SCROLL_BAR_TASK_HEAD = 0x8BB8, RECORDS_TASK_HEAD = 0xF657 };
enum { HELD_CURSOR_TASK_HEAD = 0x8AB2, LIST_CURSOR_TASK_HEAD = 0x89F2, MEMBER_CURSOR_TASK_HEAD = 0x8A45,
       MEMBERS_CURSORS_TASK_HEAD = 0x8AF5 };

static const struct {
    uint16_t start;
    MenuTaskAsC run;
} tasks[] = {
    { MAIN_TASK_HEAD, main_task },
    { CURSOR_TASK_HEAD, cursor_task },
    { SCROLL_BAR_TASK_HEAD, scroll_bar_task },
    { RECORDS_TASK_HEAD, records_task },
    { HELD_CURSOR_TASK_HEAD, held_cursor_task },
    { LIST_CURSOR_TASK_HEAD, list_cursor_task },
    { MEMBER_CURSOR_TASK_HEAD, member_cursor_task },
    { MEMBERS_CURSORS_TASK_HEAD, members_cursors_task },
    { MENU_FADE_IN_TASK_START, menu_fade_in_task },
    { MENU_TYPE_A_DESCRIPTION, menu_type_the_description_task },
    { MENU_TYPE_A_SETTING, menu_type_the_setting_task },
    { MENU_TYPE_A_VALUE, menu_type_the_value_task },
    { MENU_RAISE_AMOUNTS_TASK_START, menu_raise_amounts_task },
    { MENU_GATE_SONG_TASK_START, menu_play_the_gate_song },
    { MENU_GATE_SCREEN_TASK_START, menu_gate_screen_task },
};
enum { TASKS_AS_C = sizeof tasks / sizeof tasks[0] };

/* The menus' coroutines while they are open, and whether a task has closed
 * them: the one closing them waits for good, and the turns end there. */
static Tasks the_tasks;
static bool closed;

/* A task's coroutine: its work, then its end, which the next turn reads in
 * its record. */
static void run_a_task(GameState *g, Platform *pf, void *arg)
{
    const MenuTaskAsC *run = arg;
    (*run)(g, pf);
    if (!closed && !program_went_no_further(g))
        (void)record_the_end(g);
}

void menu_tasks_open(GameState *g, Platform *pf)
{
    tasks_init(&the_tasks, g, pf);
    closed = false;
    start_the_tasks(g);
}

void menu_task_waits(GameState *g, uint8_t frames)
{
    (void)record_the_wait(g, frames);
    tasks_wait_a_frame(&the_tasks);
}

void menu_tasks_closed(void)
{
    closed = true;
    for (;;)
        tasks_wait_a_frame(&the_tasks);
}

/* A task's turn: from its start, its work found by it, or from where it
 * waited, in the screen step it waited in. False where its work is none the
 * port has as C, or the turn closed the menus. */
static bool take_a_turn(GameState *g, const TaskTurn *turn)
{
    if (turn_starts(turn)) {
        uint16_t at = task_starts(g, turn->task);
        unsigned i = 0;
        while (i < TASKS_AS_C && tasks[i].start != at)
            i++;
        if (i == TASKS_AS_C) {
            program_needs_the_machine(g, (uint32_t)MENUS_BANK << 16 | at, "a menus' task only the machine runs");
            return false;
        }
        tasks_start(&the_tasks, (uint8_t)turn->task, run_a_task, (void *)&tasks[i].run);
    } else {
        task_goes_on(g, turn->task);
    }
    tasks_take_a_turn(&the_tasks, (uint8_t)turn->task);
    return !closed && !program_went_no_further(g);
}

// $C2:81CF
MenuTurns menu_tasks_take_the_turns(GameState *g)
{
    for (TaskTurn turn = next_turn(g, 0); turn.due; turn = next_turn(g, turn.task + 1))
        if (!take_a_turn(g, &turn))
            return closed ? MENU_TURNS_CLOSED : MENU_TURNS_WENT_NO_FURTHER;
    return MENU_TURNS_TAKEN;
}

void menu_tasks_end(void) { tasks_end(&the_tasks); }

/* The two tasks that run all the time, as C: their turns over and over, the
 * wait coming back each next frame, until a step goes no further. */
typedef struct {
    GameState *game;
    Platform *pf;
} LoopAsC;

static bool screen_step(void *ctx)
{
    LoopAsC *c = ctx;
    return menu_run_the_screen_step(c->game, c->pf);
}

static bool loop_waits(void *ctx)
{
    LoopAsC *c = ctx;
    menu_task_waits(c->game, 1);
    return true;
}

static void main_task(GameState *g, Platform *pf)
{
    LoopAsC c = { g, pf };
    while (main_task_turn(screen_step, loop_waits, &c))
        ;
}

/* The cursor's turns while its guard's state half takes them; otherwise the
 * program goes no further there. */
static void cursor_task(GameState *g, Platform *pf)
{
    LoopAsC c = { g, pf };
    while (menu_cursor_can_be_placed(g) && cursor_task_turn(g, loop_waits, &c))
        ;
    if (!menu_cursor_can_be_placed(g))
        program_needs_the_machine(g, (uint32_t)MENUS_BANK << 16 | CURSOR_TASK_HEAD, "a cursor the port doesn't place");
}

/* The scroll bar's turns while its guard's state half takes them, each
 * frame's arrows timed as its entry's. */
static void scroll_bar_task(GameState *g, Platform *pf)
{
    (void)pf;
    enum { FIRST_TURN = 0xC28BB8, LATER_TURNS = 0xC28C33 };
    for (uint32_t turn = FIRST_TURN; menu_scroll_bar_can_be_drawn(g); turn = LATER_TURNS) {
        TIMED_AS(g, turn, menu_draw_the_scroll_bar(g));
        menu_task_waits(g, 1);
    }
    program_needs_the_machine(g, (uint32_t)MENUS_BANK << 16 | SCROLL_BAR_TASK_HEAD, "a scroll bar the port doesn't draw");
}

/* The records' turns, the same way. */
static void records_task(GameState *g, Platform *pf)
{
    (void)pf;
    enum { FIRST_TURN = 0xC2F657, LATER_TURNS = 0xC2F699 };
    for (uint32_t turn = FIRST_TURN; menu_records_can_run(g); turn = LATER_TURNS) {
        TIMED_AS(g, turn, menu_run_the_records(g));
        menu_task_waits(g, 1);
    }
    program_needs_the_machine(g, (uint32_t)MENUS_BANK << 16 | RECORDS_TASK_HEAD, "records the port doesn't run");
}

/* The held cursors' turns, the same way: a frame's drawing timed as the
 * task's entry from its start, then from its wait. */
typedef struct {
    uint16_t head, later;
    bool (*can_be_drawn)(const GameState *g);
    void (*draw)(GameState *g);
    const char *what;
} HeldCursorTask;

static bool always(const GameState *g) { return (void)g, true; }

static void held_cursor_turns(GameState *g, const HeldCursorTask *t)
{
    for (uint32_t turn = (uint32_t)MENUS_BANK << 16 | t->head; t->can_be_drawn(g);
         turn = (uint32_t)MENUS_BANK << 16 | t->later) {
        TIMED_AS(g, turn, t->draw(g));
        menu_task_waits(g, 1);
    }
    program_needs_the_machine(g, (uint32_t)MENUS_BANK << 16 | t->head, t->what);
}

static void held_cursor_task(GameState *g, Platform *pf)
{
    (void)pf;
    static const HeldCursorTask t = { HELD_CURSOR_TASK_HEAD, 0x8AB8, menu_held_cursor_can_be_drawn,
                                      menu_draw_the_held_cursor, "a held cursor the port doesn't draw" };
    held_cursor_turns(g, &t);
}

static void list_cursor_task(GameState *g, Platform *pf)
{
    (void)pf;
    static const HeldCursorTask t = { LIST_CURSOR_TASK_HEAD, 0x89F8, always, menu_draw_the_cursor_held_on_the_list,
                                      "a list's held cursor the port doesn't draw" };
    held_cursor_turns(g, &t);
}

static void member_cursor_task(GameState *g, Platform *pf)
{
    (void)pf;
    static const HeldCursorTask t = { MEMBER_CURSOR_TASK_HEAD, 0x8A94, menu_member_cursor_can_be_drawn,
                                      menu_draw_the_cursor_held_on_a_member, "a member's held cursor the port doesn't draw" };
    held_cursor_turns(g, &t);
}

static void members_cursors_task(GameState *g, Platform *pf)
{
    (void)pf;
    static const HeldCursorTask t = { MEMBERS_CURSORS_TASK_HEAD, 0x8AFB, menu_members_cursors_can_be_drawn,
                                      menu_draw_cursors_on_every_member, "the members' cursors the port doesn't draw" };
    held_cursor_turns(g, &t);
}

/* ---- The guards -------------------------------------------------------------------------------- */

/* The running task one of the eight records. */
static bool running_task_is_one(const GameState *g)
{
    uint8_t at = menu_current_task(g);
    return at % RECORD_BYTES == 0 && at / RECORD_BYTES < MENU_TASKS;
}

/* Every waiting task's stack in low RAM, where the game keeps the tasks'
 * stacks: past it, going back into a task would take its registers from the
 * bus's open reads, which the port doesn't make as the original does. */
enum { LOW_RAM_END = 0x2000, KEPT_BYTES = 7 };

static bool stacks_in_low_ram(const GameState *g)
{
    for (unsigned task = 0; task < MENU_TASKS; task++)
        if (menu_tasks_state(g, task) == TASK_WAITING && menu_tasks_stack(g, task) >= LOW_RAM_END - KEPT_BYTES)
            return false;
    return true;
}

/* Every task that would start, one of the game's starts: the port knows what
 * those read (above), and the game starts no other. The running task (the one
 * waiting or ending now) goes on from where it waits, not from its start. */
static bool starts_known(const GameState *g)
{
    for (unsigned task = 0; task < MENU_TASKS; task++) {
        uint8_t state = menu_tasks_state(g, task);
        bool running = state == TASK_RUNNING && menu_current_task(g) == task * RECORD_BYTES;
        if (takes_turns(state) && state != TASK_WAITING && !running && !start_is_known(menu_tasks_start(g, task)))
            return false;
    }
    return true;
}

/* A task's wait or end: the running task one of the records, the waiting
 * tasks' stacks in low RAM, the starting tasks' starts the game's, and a data
 * bank that reaches low RAM, where the
 * frame's end marks the uploads waiting. The end reads the running task on
 * the menus' page. */
bool menu_task_can_wait(Snes *s)
{
    if (!m_bank_reaches_low_ram(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = running_task_is_one(g) && stacks_in_low_ram(g) && starts_known(g);
    vars_peek(false);
    return ok;
}

bool menu_task_can_end(Snes *s) { return m_direct_page(s) == 0x0000 && menu_task_can_wait(s); }

/* The frame's turns: on the menus' page, the data bank work RAM's, the
 * waiting tasks' stacks in low RAM, the starting tasks' starts the game's. */
bool menu_tasks_can_run(Snes *s)
{
    if (m_direct_page(s) != 0x0000 || !m_bank_reaches_low_ram(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = stacks_in_low_ram(g) && starts_known(g);
    vars_peek(false);
    return ok;
}
