/* Field mode: a map's screen set up, and faded in (field/screen_setup.c). */
#ifndef CT_GAME_FIELD_SCREEN_SETUP_H
#define CT_GAME_FIELD_SCREEN_SETUP_H

#include <stdbool.h>

#include "platform/platform.h"
#include "snes/system.h"
#include "state.h"

/* The entries (game/registry.c): the screen set up ($C0:286C); faded in after
 * it, with the frame's work ($C0:2848, the field's entry) or the effects only
 * ($C0:2824, after a menu); the objects in view drawn at once ($C0:B0E6), and
 * its guard. */
void field_set_the_screen_up(Snes *s);
void field_fade_in(Snes *s);
/* The screen set up and faded in, as C, the frame's work going on as it
 * brightens; false where a step is the machine's yet, the program going no
 * further there (core/program.h). */
bool field_screen_set_up(GameState *g, Platform *pf);
bool field_fade_in_as_c(GameState *g, Platform *pf);
void field_fade_in_after_a_menu(Snes *s);
void field_draw_the_objects_in_view(Snes *s);
bool field_objects_in_view_can_be_drawn(Snes *s);
enum { FIELD_SET_THE_SCREEN_UP_ENTRY = 0xC0286C, FIELD_FADE_IN_ENTRY = 0xC02848, FIELD_FADE_IN_AFTER_A_MENU_ENTRY = 0xC02824 };

#endif
