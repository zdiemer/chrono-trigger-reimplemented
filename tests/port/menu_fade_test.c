/* Checks the menus' fades' steps (menu/fades.h, menu_fade_a_step, as $C2:83D0
 * takes them): a fade in comes up to full brightness and stops there; and a
 * fade out started while the fade in is still under way -- a cancel pressed
 * as the gate's party exchange comes up -- turns the fade in round, so that
 * the two, each taking a step a frame, come down to black together rather
 * than undoing each other's step for ever. Exits non-zero on failure.
 *
 *   menu_fade_test */
#include <stdio.h>

#include "menu/fades.h"
#include "port/state_host.h"
#include "vars.h"

enum { FADE_IN = 2, FADE_OUT = -2, OTHER_SETTINGS = 0x80, FULL = 0x0F, A_LONG_TIME = 64 };

static int failures;

#define CHECK(cond, what)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, what);                                  \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static unsigned brightness(const GameState *g) { return menu_brightness(g) & FULL; }

int main(void)
{
    GameState *g = state_host_create(NULL);
    if (!g) {
        printf("FAIL: no game state\n");
        return 1;
    }

    set_menu_brightness(g, OTHER_SETTINGS | 1);
    set_menu_fade_step(g, (uint8_t)FADE_IN);
    unsigned steps = 0;
    while (menu_fade_a_step(g) == MENU_FADE_GOES_ON && steps < A_LONG_TIME)
        steps++;
    CHECK(brightness(g) == FULL, "a fade in ends at full brightness");
    CHECK(menu_brightness(g) & OTHER_SETTINGS, "a fade keeps the display's other settings");
    CHECK(steps == 6, "a fade in from 1 takes seven steps of two");

    /* The fade in under way, at half brightness, when a fade out starts. */
    set_menu_brightness(g, 7);
    set_menu_fade_step(g, (uint8_t)FADE_IN);
    CHECK(menu_fade_a_step(g) == MENU_FADE_GOES_ON, "the fade in under way");
    set_menu_fade_step(g, (uint8_t)FADE_OUT);
    MenuFadeEnd fade_in = MENU_FADE_GOES_ON, fade_out = MENU_FADE_GOES_ON;
    for (steps = 0; steps < A_LONG_TIME && (fade_in == MENU_FADE_GOES_ON || fade_out == MENU_FADE_GOES_ON); steps++) {
        if (fade_in == MENU_FADE_GOES_ON)
            fade_in = menu_fade_a_step(g);
        if (fade_out == MENU_FADE_GOES_ON)
            fade_out = menu_fade_a_step(g);
    }
    CHECK(fade_out == MENU_FADE_AT_BLACK, "the fade out reaches black over the fade in");
    CHECK(fade_in == MENU_FADE_AT_BLACK, "the fade in, turned round, ends at black too");
    CHECK(brightness(g) == 0, "the screen black");
    return failures != 0;
}
