/* The menus' fades: the screen brought up out of black as a screen opens,
 * and taken down to black as the menus close, a step of brightness every
 * frame.
 *
 * A screen's opening starts the fade in as a task of its own (the menus'
 * first, menu/menu.h), so that the screen's main task goes on choosing while
 * the screen comes up; the fade out runs in whichever task closes the menus,
 * which waits through it. Either way the fade waits for the next frame
 * between its steps (the menus' task switch, menu/tasks.c), and the vblank
 * handler sets the brightness the fade leaves at each frame's end
 * (menu/display.c). The brightness is the low four bits of the menus'
 * display setting; a fade out that reaches black waits a last frame before
 * it hands the screen on, so that the black is shown. */
#include "menu/fades.h"

#include "menu/bridge.h"
#include "menu/menu.h"
#include "original_effects.h"
#include "original_frame.h"
#include "port/machine.h"
#include "vars.h"

/* The brightness, the low nibble of the menus' display setting: 15 is full,
 * and a fade's step moves it by two. */
enum { BRIGHTNESS = 0x0F, FULL_BRIGHTNESS = 0x0F, BLACK = 0 };
enum { FADE_IN_STEP = 2, FADE_OUT_STEP = -2, FRAMES_A_STEP = 1 };

static uint8_t brightness(const GameState *g) { return (uint8_t)(menu_brightness(g) & BRIGHTNESS); }

static void set_brightness(GameState *g, uint8_t level)
{
    set_menu_brightness(g, (uint8_t)((menu_brightness(g) & ~BRIGHTNESS) | level));
}

/* For the original's code and shadow mode: the fade's step and its frames a
 * step, which its loop reads back between its steps. */
static void leave_the_fade(GameState *g, int8_t step)
{
    set_menu_fade_step(g, (uint8_t)step);
    set_menu_fade_frames(g, FRAMES_A_STEP);
}

/* The waits: the task switch's, for the frames in A ($C2:822B) between the
 * steps, from the fade's loop, which sets the flags again after its wait
 * (tools/dontcare_check.py checks it), and for a frame ($C2:821E) at a fade
 * out's end, which the original jumps to so that it returns to the fade's
 * caller. The task's frame keeps what the last step leaves in A and the flags
 * for that caller: the sum that reached black. */
enum { WAIT_FRAMES = 0xC2822B, BETWEEN_THE_STEPS = 0xC283F8 };
enum { THE_WAITS_IGNORE_THE_FLAGS = DC_N | DC_V | DC_Z | DC_C, THE_LAST_KEEPS_ALL = MENU_FADE_S_LAST_WAIT_READS_ALL };

/* The brightness and the step summed as the original adds them, a byte: a
 * carry past full, never an overflow (the brightness is never negative). At
 * black the last frame's wait is handed them, as the original's last step
 * leaves them. */
static void leave_the_step_s_sum(Snes *s, uint8_t before, int8_t step)
{
    uint16_t sum = (uint16_t)(before + (uint8_t)step);
    m_set_a8(s, (uint8_t)sum);
    m_set_nz8(s, (uint8_t)sum);
    m_set_flag(s, CPU_FLAG_C, sum > 0xFF);
    m_set_flag(s, CPU_FLAG_V, false);
}

/* A fade's steps, in the task it runs in: the brightness moved a step and
 * held for a frame, until it is full or black. A fade that reached black has
 * its last frame still to wait; one stopped is where the port stops (the
 * task never came back from a wait). With the end, the brightness the last
 * step started from. The frame's wait between the steps is the task's: as C,
 * or on the machine (below); false where the task never came back. */
typedef enum { FADE_DONE, FADE_AT_BLACK, FADE_STOPPED } FadeEnd;
typedef struct {
    FadeEnd end;
    uint8_t last_from;
} Fade;
typedef bool (*FadeWait)(void *ctx);

static Fade fade(GameState *g, int8_t step, FadeWait wait, void *ctx)
{
    ORIGINAL_EFFECT(leave_the_fade(g, step));
    for (;;) {
        uint8_t before = brightness(g);
        int level = before + step;
        if (level <= BLACK) {
            set_brightness(g, BLACK);
            return (Fade){ FADE_AT_BLACK, before };
        }
        if (level >= FULL_BRIGHTNESS) {
            set_brightness(g, FULL_BRIGHTNESS);
            return (Fade){ FADE_DONE, before };
        }
        set_brightness(g, (uint8_t)level);
        if (!wait(ctx))
            return (Fade){ FADE_STOPPED, before };
    }
}

/* The fade out, as the routine for it ($C2:83CA) does it, called or jumped
 * to: the steps, inside its frame where it was called. At black its last
 * frame's wait, which hands the screen on, is the caller's: as C a frame's
 * wait, on the machine the task switch's from where the routine was entered. */
// $C2:83CA
static Fade fade_out(GameState *g, FadeWait between, void *ctx, bool called)
{
    Fade f;
    if (called)
        IN_THE_ORIGINALS_FRAME(g, ORIGINAL_RETURN_BYTES, f = fade(g, FADE_OUT_STEP, between, ctx));
    else
        f = fade(g, FADE_OUT_STEP, between, ctx);
    return f;
}

/* The fade in's task: a frame for the screen to be drawn, then the fade up
 * to full brightness, called as the routine's; the task ends after. */
// $C2:83BD
static bool fade_in_task(GameState *g, FadeWait first_frame, FadeWait between, void *ctx)
{
    if (!first_frame(ctx))
        return false;
    Fade f;
    IN_THE_ORIGINALS_FRAME(g, ORIGINAL_RETURN_BYTES, f = fade(g, FADE_IN_STEP, between, ctx));
    return f.end == FADE_DONE;
}

/* ---- As C, from inside a task (menu/tasks.c's coroutines) ----------------------------------- */

static bool wait_as_c(void *ctx)
{
    menu_task_waits_as_c(ctx, FRAMES_A_STEP);
    return true;
}

void menu_fade_out_as_c(GameState *g)
{
    if (fade_out(g, wait_as_c, g, true).end == FADE_AT_BLACK)
        menu_task_waits_as_c(g, 1);
}

void menu_fade_in_task_as_c(GameState *g, Platform *pf)
{
    (void)pf;
    (void)fade_in_task(g, wait_as_c, wait_as_c, g);
}

/* ---- On the machine ------------------------------------------------------------------------ */

/* The wait between the steps: the task switch's for the frames in A. */
static bool wait_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    m_set_a8(s, FRAMES_A_STEP);
    return menu_yield(s, BETWEEN_THE_STEPS, WAIT_FRAMES, THE_WAITS_IGNORE_THE_FLAGS);
}

/* Called from the JSR at `site`, the last wait returns to the caller as the
 * routine's return would. */
bool menu_fade_out_from(Snes *s, uint32_t site)
{
    Fade f = fade_out(state_of(s), wait_on_the_machine, s, true);
    if (f.end != FADE_AT_BLACK)
        return f.end == FADE_DONE;
    leave_the_step_s_sum(s, f.last_from, FADE_OUT_STEP);
    return menu_yield(s, site, MENU_TASK_WAITS_A_FRAME, THE_LAST_KEEPS_ALL);
}

/* Jumped to as a screen's step's last, the last wait is the jump's, and
 * returns to the step's caller. */
void menu_fade_the_screen_out(Snes *s)
{
    Fade f = fade_out(state_of(s), wait_on_the_machine, s, false);
    switch (f.end) {
    case FADE_AT_BLACK:
        leave_the_step_s_sum(s, f.last_from, FADE_OUT_STEP);
        menu_go_on_in(s, MENU_TASK_WAITS_A_FRAME, menu_task_waits_a_frame, THE_LAST_KEEPS_ALL);
        break;
    case FADE_DONE: m_rts(s); break;
    case FADE_STOPPED: break;
    }
}

/* ---- The fade in, a task of its own ---------------------------------------------------------- */

/* The task's start, in the menus' bank. */
enum { FADE_IN_TASK = 0, FADE_IN_TASK_START = MENU_FADE_IN_TASK_START };

// $C2:83B2
void menu_start_the_fade_in(GameState *g) { menu_start_task(g, FADE_IN_TASK, FADE_IN_TASK_START); }

/* What the task's start leaves the screen's step it ends: 16-bit registers,
 * the record's state and countdown in A, and the task's place in X with the
 * flags of its load. */
static void leave_the_task_started(Snes *s)
{
    menu_set_widths(s, false, false);
    s->cpu.a = MENU_TASK_STARTING | 1 << 8;
    s->cpu.x = FADE_IN_TASK;
    m_set_nz16(s, FADE_IN_TASK);
}

void menu_start_the_fade_in_entry(Snes *s)
{
    menu_start_the_fade_in(state_of(s));
    ORIGINAL_EFFECT(leave_the_task_started(s));
    m_rts(s);
}

/* Where the task ends ($C2:8216), which reads nothing the fade leaves. */
enum { THE_TASK_ENDS = 0xC28216, ENDING_READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

/* The task on the machine: its first frame's wait from the task's start, the
 * fade from its JSR with an 8-bit accumulator, and the end the scheduler's. */
static bool first_frame_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    if (!menu_wait_a_frame_from(s, 0xC283BD))
        return false;
    menu_set_widths(s, true, false);
    return true;
}

void menu_fade_in_task(Snes *s)
{
    if (fade_in_task(state_of(s), first_frame_on_the_machine, wait_on_the_machine, s))
        menu_go_on_in(s, THE_TASK_ENDS, menu_task_ends, ENDING_READS_NOTHING);
}
