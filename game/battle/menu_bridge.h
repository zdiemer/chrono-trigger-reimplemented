/* Battle menu input and cursor sounds. */
#ifndef CT_GAME_BATTLE_MENU_BRIDGE_H
#define CT_GAME_BATTLE_MENU_BRIDGE_H

#include <stdbool.h>

#include "battle/choosing.h"
#include "platform/platform.h"
#include <stdint.h>
#include "state.h"

/* $C1:1B55, the click routine, as the original's callers call it. */

/* The menu's buttons taken ($C1:1153), as C: false where it went no
 * further. */
bool battle_menu_input_taken(GameState *g, Platform *pf);

#endif
