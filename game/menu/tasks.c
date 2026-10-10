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

#include "original_effects.h"
#include "port/machine.h"
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

/* Where a task waits: on the original's machine, its stack pointer. */
typedef uint16_t TaskPlace;

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
/* The running task waits `frames` frames (a frame, mostly), from `place`,
 * in the screen step it is in; the next task goes on. */
static TaskTurn task_waits(GameState *g, uint8_t frames, TaskPlace place)
{
    unsigned task = running_task(g);
    set_menu_tasks_state(g, task, TASK_WAITING);
    set_menu_tasks_countdown(g, task, frames);
    set_menu_tasks_stack(g, task, place);
    set_menu_tasks_step(g, task, menu_screen_step(g));
    return next_turn(g, task + 1);
}

// $C2:8216
/* The running task ends; the next task goes on. */
static TaskTurn task_ends(GameState *g)
{
    unsigned task = running_task(g);
    set_menu_tasks_state(g, task, TASK_STOPPED);
    return next_turn(g, task + 1);
}

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

/* Where the menus wait for the frame's uploads to be sent (menu/frame.c's
 * idle entry), which reads none of the registers. */
enum { WAIT_FOR_THE_UPLOADS = 0xC281CA };
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

/* A task from its start, on its own stack, with the registers the original's
 * jump into it leaves: its start in A, its record in X, what it was doing
 * less two in Y (the loop's decrements of it), 16-bit, on the menus' page; the
 * flags of its start's load and of the stack's add. */
static void start(Snes *s, const TaskTurn *turn)
{
    GameState *g = state_of(s);
    uint16_t at = task_starts(g, turn->task);
    s->cpu.s = (uint16_t)(FIRST_TASK_STACK + turn->task * TASK_STACK_BYTES);
    s->cpu.d = 0;
    s->cpu.a = at;
    s->cpu.x = (uint16_t)(turn->task * RECORD_BYTES);
    s->cpu.y = (uint16_t)((uint8_t)(turn->was - 1) - 1);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_set_nz16(s, at);
    m_set_flag(s, CPU_FLAG_V, false);
    m_set_flag(s, CPU_FLAG_C, false);
    m_jump_to(s, (uint32_t)MENUS_BANK << 16 | at);
}

/* For shadow mode: the bytes the original's way onto the menus' page leaves
 * just below a stack, which it puts the page's number through. */
static void leave_the_page_below(Snes *s, uint16_t stack)
{
    m_write8(s, stack, 0);
    m_write8(s, (uint16_t)(stack - 1), 0);
}

/* No task goes on this frame: back on the menus' main stack and page, the
 * data bank work RAM's, to wait for the frame's uploads. */
static void wait_for_the_next_frame(Snes *s)
{
    ORIGINAL_EFFECT(leave_the_page_below(s, THE_MAIN_STACK));
    s->cpu.s = THE_MAIN_STACK;
    s->cpu.db = MENUS_DATA_BANK;
    s->cpu.d = 0;
    m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_stop_at(s, WAIT_FOR_THE_UPLOADS, THE_WAIT_READS_NOTHING);
}

static void take_turn(Snes *s, TaskTurn turn)
{
    if (!turn.due)
        wait_for_the_next_frame(s);
    else if (turn_starts(&turn))
        start(s, &turn);
    else
        go_back_into(s, turn.task);
}

/* The task's registers kept on its stack, where it goes on from; then on the
 * menus' page. */
static void wait_entry(Snes *s, uint8_t frames)
{
    m_keep_registers_to_switch(s);
    TaskPlace place = s->cpu.s;
    ORIGINAL_EFFECT(leave_the_page_below(s, place));
    s->cpu.d = 0;
    take_turn(s, task_waits(state_of(s), frames, place));
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
/* The menus, once open, wait for the first frame's uploads. */
void menu_wait_for_the_first_frame(Snes *s) { wait_for_the_next_frame(s); }

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

/* A task's wait or end: the running task one of the records, the waiting
 * tasks' stacks in low RAM, and a data bank that reaches low RAM, where the
 * frame's end marks the uploads waiting. The end reads the running task on
 * the menus' page. */
bool menu_task_can_wait(Snes *s)
{
    if (!m_bank_reaches_low_ram(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = running_task_is_one(g) && stacks_in_low_ram(g);
    vars_peek(false);
    return ok;
}

bool menu_task_can_end(Snes *s) { return m_direct_page(s) == 0x0000 && menu_task_can_wait(s); }

/* The frame's turns: on the menus' page, the data bank work RAM's, the
 * waiting tasks' stacks in low RAM. */
bool menu_tasks_can_run(Snes *s)
{
    if (m_direct_page(s) != 0x0000 || !m_bank_reaches_low_ram(s))
        return false;
    vars_peek(true);
    bool ok = stacks_in_low_ram(state_of(s));
    vars_peek(false);
    return ok;
}
