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
#include "charge.h"
#include "menu/fades.h"

#include "port/vars_ram.h"
#include "menu/menu.h"
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

/* The fade's direction and cadence, kept in the menus' cells. */
static void start_fade(GameState *g, int8_t step)
{
    set_menu_fade_step(g, (uint8_t)step);
    set_menu_fade_frames(g, FRAMES_A_STEP);
}

/* A fade's steps, in the task it runs in: the brightness moved a step and
 * held for a frame, until it is full or black. A fade that reached black has
 * its last frame still to wait; one stopped is where the port stops (the
 * task never came back from a wait). With the end, the brightness the last
 * step started from. The frame's wait between the steps is the task's
 * (below); false where the task never came back. */
typedef enum { FADE_DONE, FADE_AT_BLACK, FADE_STOPPED, FADE_NEXT_FRAME } FadeEnd;
typedef struct {
    FadeEnd end;
    uint8_t last_from;
} Fade;
typedef bool (*FadeWait)(void *ctx);

/* One step of the fade: the brightness moved by `step`, held at full or
 * black. */
static Fade fade_step(GameState *g, int8_t step)
{
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
    return (Fade){ FADE_NEXT_FRAME, before };
}

static Fade fade(GameState *g, int8_t step, FadeWait wait, void *ctx)
{
    start_fade(g, step);
    for (;;) {
        Fade f = fade_step(g, step);
        if (f.end != FADE_NEXT_FRAME)
            return f;
        if (!wait(ctx))
            return (Fade){ FADE_STOPPED, f.last_from };
    }
}

/* The fade out ($C2:83CA): the steps down to black. At black its last
 * frame's wait, which hands the screen on, is the caller's. */
// $C2:83CA
static Fade fade_out(GameState *g, FadeWait between, void *ctx) { return fade(g, FADE_OUT_STEP, between, ctx); }

/* The fade in's task: a frame for the screen to be drawn, then the fade up
 * to full brightness, called as the routine's; the task ends after. */
// $C2:83BD
static bool fade_in_task(GameState *g, FadeWait first_frame, FadeWait between, void *ctx)
{
    if (!first_frame(ctx))
        return false;
    Fade f;
    f = fade(g, FADE_IN_STEP, between, ctx);
    return f.end == FADE_DONE;
}

/* ---- From inside a task (menu/tasks.c's coroutines) ----------------------------------------- */

static bool wait(void *ctx)
{
    menu_task_waits(ctx, FRAMES_A_STEP);
    return true;
}

void menu_fade_to_black(GameState *g)
{
    if (fade_out(g, wait, g).end == FADE_AT_BLACK)
        menu_task_waits(g, 1);
}

void menu_fade_in_task(GameState *g, Platform *pf)
{
    (void)pf;
    (void)fade_in_task(g, wait, wait, g);
}

/* ---- The fade in, a task of its own ---------------------------------------------------------- */

/* The task's start, in the menus' bank. */
enum { FADE_IN_TASK = 0, FADE_IN_TASK_START = MENU_FADE_IN_TASK_START };

// $C2:83B2
void menu_start_the_fade_in(GameState *g) { menu_start_task(g, FADE_IN_TASK, FADE_IN_TASK_START); }

