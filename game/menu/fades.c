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
 * held for its frames, until it is full or black. */
typedef enum { FADE_DONE, FADE_AT_BLACK, FADE_NEXT_FRAME } FadeEnd;

/* One step of the fade: the brightness moved by `step`, held at full or
 * black. */
static FadeEnd fade_step(GameState *g, int8_t step)
{
    int level = brightness(g) + step;
    if (level <= BLACK) {
        set_brightness(g, BLACK);
        return FADE_AT_BLACK;
    }
    if (level >= FULL_BRIGHTNESS) {
        set_brightness(g, FULL_BRIGHTNESS);
        return FADE_DONE;
    }
    set_brightness(g, (uint8_t)level);
    return FADE_NEXT_FRAME;
}

static FadeEnd fade(GameState *g, int8_t step)
{
    start_fade(g, step);
    for (;;) {
        FadeEnd end = fade_step(g, step);
        if (end != FADE_NEXT_FRAME)
            return end;
        menu_task_waits(g, FRAMES_A_STEP);
    }
}

/* ---- From inside a task (menu/tasks.c's coroutines) ----------------------------------------- */

// $C2:83CA
/* The fade out: the steps down to black, and at black its last frame's
 * wait, which hands the screen on. */
void menu_fade_to_black(GameState *g)
{
    if (fade(g, FADE_OUT_STEP) == FADE_AT_BLACK)
        menu_task_waits(g, 1);
}

// $C2:83BD
/* The fade in's task: a step's frames for the screen to be drawn, then the
 * fade up to full brightness, called as the routine's; the task ends
 * after. */
void menu_fade_in_task(GameState *g, Platform *pf)
{
    (void)pf;
    menu_task_waits(g, FRAMES_A_STEP);
    (void)fade(g, FADE_IN_STEP);
}

/* ---- The fade in, a task of its own ---------------------------------------------------------- */

/* The task's start, in the menus' bank. */
enum { FADE_IN_TASK = 0, FADE_IN_TASK_START = MENU_FADE_IN_TASK_START };

// $C2:83B2
void menu_start_the_fade_in(GameState *g) { menu_start_task(g, FADE_IN_TASK, FADE_IN_TASK_START); }

