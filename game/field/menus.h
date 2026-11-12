/* Field mode: the pause, and the menus opened from the field (field/menus.c). */
#ifndef CT_GAME_FIELD_MENUS_H
#define CT_GAME_FIELD_MENUS_H

#include <stdbool.h>

#include "field/requests.h"
#include "platform/platform.h"
#include <stdint.h>
#include "state.h"

/* The pause and the menus, once a frame ($C0:18D9): false where a step is
 * one the port doesn't take, the program going no further there. */
bool field_pause_and_menus(GameState *g, Platform *pf);
/* The menu a script asked for, from the main loop's requests
 * (field/requests.c): true where it came back to the main loop's caller's
 * state, false where it stopped. */
/* Where the pass goes after it. */
FieldPass field_script_menu_opened(GameState *g, Platform *pf);

#endif
