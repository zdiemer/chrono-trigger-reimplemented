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
 * Which task goes on, and how, is the scheduler's, over the tasks' records;
 * each task runs as a coroutine (core/tasks.h), whose own stack is where it
 * goes on from. */
#include "menu/menu.h"

#include "charge.h"
#include "core/program.h"
#include "core/tasks.h"
#include "menu/description.h"
#include "menu/fades.h"
#include "menu/cursor.h"
#include "menu/gate_screen.h"
#include "menu/healing.h"
#include "menu/held_cursors.h"
#include "menu/sound.h"
#include "menu/sprites.h"
#include "platform/timing.h"
#include "port/vars_ram.h"
#include "vars.h"

/* What a task's record says it is doing. The original runs a task whose
 * state is anything but WAITING from its start, and takes no turns for one
 * whose state is 0 or past $80 (it tests the sign of the state less one). */
typedef enum { TASK_STOPPED = 0, TASK_STARTING = MENU_TASK_STARTING, TASK_WAITING = 2, TASK_RUNNING = 3 } TaskState;
enum { TAKES_NO_TURNS = 0x80 };

static bool takes_turns(uint8_t state) { return !((uint8_t)(state - 1) & TAKES_NO_TURNS); }

/* The running task's record, by its offset among the records. */
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
 * step it is in; the next task goes on, and the waiting one goes on from
 * its coroutine. The task that waited. */
static unsigned record_the_wait(GameState *g, uint8_t frames)
{
    unsigned task = running_task(g);
    set_menu_tasks_state(g, task, TASK_WAITING);
    set_menu_tasks_countdown(g, task, frames);
    set_menu_tasks_step(g, task, menu_screen_step(g));
    return task;
}

// $C2:8216
/* The running task ends; the next task goes on. */
static unsigned record_the_end(GameState *g)
{
    unsigned task = running_task(g);
    set_menu_tasks_state(g, task, TASK_STOPPED);
    return task;
}

/* A task going on from where it waited is back in the screen step it waited
 * in. */
static void task_goes_on(GameState *g, unsigned task) { set_menu_screen_step(g, menu_tasks_step(g, task)); }

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
    return menu_tasks_start(g, task);
}

enum { MENUS_BANK = 0xC2 };

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
    charge_block_move(g, THE_RECORDS_BYTES - 2);
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

/* ---- The tasks that run all the time ------------------------------------------------------- */

/* A loop's turn takes a wait for the next frame; false where the task never
 * came back to it. */
typedef bool (*LoopWait)(void *ctx);

// $C2:826F
/* The main task's turn, from its start and from its wait: the screen's step,
 * then the wait. The step is the screen's (menu/screens.c); false where
 * either went no further. */
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

/* ---- The tasks' coroutines ------------------------------------------------------------------ */

/* Each task is a coroutine (core/tasks.h), whose stack is where it goes on
 * from; the records say which takes its turn and when (the scheduler
 * above). A task's work is found by its start, as the original's is by its
 * address. */
typedef void (*MenuTaskBody)(GameState *g, Platform *pf);

static void main_task(GameState *g, Platform *pf);
static void cursor_task(GameState *g, Platform *pf);
static void scroll_bar_task(GameState *g, Platform *pf);
static void records_task(GameState *g, Platform *pf);
static void held_cursor_task(GameState *g, Platform *pf);
static void list_cursor_task(GameState *g, Platform *pf);
static void member_cursor_task(GameState *g, Platform *pf);
static void members_cursors_task(GameState *g, Platform *pf);

enum { SCROLL_BAR_TASK_HEAD = 0x8BB8, RECORDS_TASK_HEAD = 0xF657 };
enum {
    HELD_CURSOR_TASK_HEAD = 0x8AB2,
    LIST_CURSOR_TASK_HEAD = 0x89F2,
    MEMBER_CURSOR_TASK_HEAD = 0x8A45,
    MEMBERS_CURSORS_TASK_HEAD = 0x8AF5,
};

static const struct {
    uint16_t start;
    MenuTaskBody run;
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
enum { TASK_COUNT = sizeof tasks / sizeof tasks[0] };

/* The menus' coroutines while they are open, and whether a task has closed
 * them: the one closing them waits for good, and the turns end there. */
static Tasks the_tasks;
static bool closed;

/* A task's coroutine: its work, then its end, which the next turn reads in
 * its record. */
static void run_a_task(GameState *g, Platform *pf, void *arg)
{
    const MenuTaskBody *run = arg;
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
 * port has, or the turn closed the menus. */
static bool take_a_turn(GameState *g, const TaskTurn *turn)
{
    if (turn_starts(turn)) {
        uint16_t at = task_starts(g, turn->task);
        unsigned i = 0;
        while (i < TASK_COUNT && tasks[i].start != at)
            i++;
        if (i == TASK_COUNT) {
            program_breaks_a_contract(g, (uint32_t)MENUS_BANK << 16 | at, "a menus' task the port doesn't run");
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

/* The two tasks that run all the time: their turns over and over, the
 * wait coming back each next frame, until a step goes no further. */
typedef struct {
    GameState *game;
    Platform *pf;
} MenuTasksContext;

static bool screen_step(void *ctx)
{
    MenuTasksContext *c = ctx;
    return menu_run_the_screen_step(c->game, c->pf);
}

static bool loop_waits(void *ctx)
{
    MenuTasksContext *c = ctx;
    menu_task_waits(c->game, 1);
    return true;
}

static void main_task(GameState *g, Platform *pf)
{
    MenuTasksContext c = { g, pf };
    while (main_task_turn(screen_step, loop_waits, &c))
        ;
}

/* The cursor's turns while it can be placed; otherwise the program goes no
 * further there. */
static void cursor_task(GameState *g, Platform *pf)
{
    MenuTasksContext c = { g, pf };
    while (menu_cursor_can_be_placed(g) && cursor_task_turn(g, loop_waits, &c))
        ;
    if (!menu_cursor_can_be_placed(g))
        program_breaks_a_contract(g, (uint32_t)MENUS_BANK << 16 | CURSOR_TASK_HEAD, "a cursor the port doesn't place");
}

/* The scroll bar's turns while it can be drawn, each frame's arrows timed
 * as the original's routine. */
static void scroll_bar_task(GameState *g, Platform *pf)
{
    (void)pf;
    enum { FIRST_TURN = 0xC28BB8, LATER_TURNS = 0xC28C33 };
    for (uint32_t turn = FIRST_TURN; menu_scroll_bar_can_be_drawn(g); turn = LATER_TURNS) {
        TIMED_AS(g, turn, menu_draw_the_scroll_bar(g));
        menu_task_waits(g, 1);
    }
    program_breaks_a_contract(g, (uint32_t)MENUS_BANK << 16 | SCROLL_BAR_TASK_HEAD, "a scroll bar the port doesn't draw");
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
    program_breaks_a_contract(g, (uint32_t)MENUS_BANK << 16 | RECORDS_TASK_HEAD, "records the port doesn't run");
}

/* The held cursors' turns, the same way: a frame's drawing timed as the
 * task's own routine from its start, then from its wait. */
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
    program_breaks_a_contract(g, (uint32_t)MENUS_BANK << 16 | t->head, t->what);
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
