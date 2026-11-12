/* Field mode: the pause, and the menus opened from the field (field/menus.c). */
#ifndef CT_GAME_FIELD_MENUS_H
#define CT_GAME_FIELD_MENUS_H

#include <stdbool.h>

#include "field/requests.h"
#include "platform/platform.h"
#include <stdint.h>
#include "state.h"

/* The same as C: false where a step is the machine's yet (the menus), the
 * program going no further there. */
bool field_pause_and_menus(GameState *g, Platform *pf);
/* The menu a script asked for, from the main loop's requests
 * (field/requests.c): true where it came back to the main loop's caller's
 * state, false where it stopped. */
/* The same as C: where the pass goes after it. */
FieldPass field_script_menu_opened(GameState *g, Platform *pf);

#endif
