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

/* The fade's direction and cadence, kept in the menus' cells. A fade reads
 * them again at every step, as the original does ($C2:83E7): a fade out
 * started while the screen's fade in is still under way (a cancel pressed as
 * a screen comes up) sets the one step both fades take, so the fade in turns
 * round and both come down to black together. */
static void start_fade(GameState *g, int8_t step)
{
    set_menu_fade_step(g, (uint8_t)step);
    set_menu_fade_frames(g, FRAMES_A_STEP);
}

/* One step of the fade under way: the brightness moved by the fade's step,
 * held at full or black. */
MenuFadeEnd menu_fade_a_step(GameState *g)
{
    int level = brightness(g) + (int8_t)menu_fade_step(g);
    if (level <= BLACK) {
        set_brightness(g, BLACK);
        return MENU_FADE_AT_BLACK;
    }
    if (level >= FULL_BRIGHTNESS) {
        set_brightness(g, FULL_BRIGHTNESS);
        return MENU_FADE_AT_FULL;
    }
    set_brightness(g, (uint8_t)level);
    return MENU_FADE_GOES_ON;
}

// $C2:83D0
/* A fade's steps, in the task it runs in, each held for the fade's frames,
 * until full brightness, or black, which is held a last frame so that it is
 * shown. */
static void fade(GameState *g, int8_t step)
{
    start_fade(g, step);
    for (;;) {
        MenuFadeEnd end = menu_fade_a_step(g);
        if (end == MENU_FADE_AT_FULL)
            return;
        if (end == MENU_FADE_AT_BLACK) {
            menu_task_waits(g, 1);
            return;
        }
        menu_task_waits(g, menu_fade_frames(g));
    }
}

/* ---- From inside a task (menu/tasks.c's coroutines) ----------------------------------------- */

// $C2:83CA
/* The fade out: the steps down to black, and at black its last frame's
 * wait, which hands the screen on. */
void menu_fade_to_black(GameState *g) { fade(g, FADE_OUT_STEP); }

// $C2:83BD
/* The fade in's task: a step's frames for the screen to be drawn, then the
 * fade up to full brightness, called as the routine's; the task ends
 * after. */
void menu_fade_in_task(GameState *g, Platform *pf)
{
    (void)pf;
    menu_task_waits(g, FRAMES_A_STEP);
    fade(g, FADE_IN_STEP);
}

/* ---- The fade in, a task of its own ---------------------------------------------------------- */

/* The task's start, in the menus' bank. */
enum { FADE_IN_TASK = 0, FADE_IN_TASK_START = MENU_FADE_IN_TASK_START };

// $C2:83B2
void menu_start_the_fade_in(GameState *g) { menu_start_task(g, FADE_IN_TASK, FADE_IN_TASK_START); }

