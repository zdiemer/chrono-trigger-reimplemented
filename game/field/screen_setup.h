/* Field mode: a map's screen set up, and faded in (field/screen_setup.c). */
#ifndef CT_GAME_FIELD_SCREEN_SETUP_H
#define CT_GAME_FIELD_SCREEN_SETUP_H

#include <stdbool.h>

#include "platform/platform.h"
#include <stdint.h>
#include "state.h"

/* The screen set up and faded in, the frame's work going on as it
 * brightens; false where a step is one the port doesn't take, the program
 * going no further there (core/program.h). */
bool field_screen_set_up(GameState *g, Platform *pf);
bool field_fade_in(GameState *g, Platform *pf);
/* The same after a menu, the effects only going on as it brightens. */
bool field_fade_in_after_a_menu(GameState *g, Platform *pf);
enum {
    FIELD_SET_THE_SCREEN_UP_ENTRY = 0xC0286C,
    FIELD_FADE_IN_ENTRY = 0xC02848,
    FIELD_FADE_IN_AFTER_A_MENU_ENTRY = 0xC02824,
};

#endif
