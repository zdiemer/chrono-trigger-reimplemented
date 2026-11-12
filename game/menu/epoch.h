/* The Epoch: its two Mode 7 screens, and where it may fly
 * (game/menu/epoch.c). */
#ifndef CT_GAME_MENU_EPOCH_H
#define CT_GAME_MENU_EPOCH_H

#include "menu/object.h"
#include "platform/platform.h"
#include <stdbool.h>
#include <stdint.h>
#include "state.h"
#include "vars.h"

/* The map view and the time dial as the program runs them: each from its
 * start until the menus' mode leaves it. False where one went no further. */
bool menu_epoch_map_view_shown(GameState *g, Platform *pf);
bool menu_epoch_time_dial_shown(GameState *g, Platform *pf);

/* The map view */

/* The time dial */
/* Where the Epoch may fly */

/* Whether the Epoch, flying over the world map, is over the part of it it
 * may not come down in. */
bool menu_epoch_kept_off_the_ground(GameState *g, MenuObject epoch);
/* The same over world map `map` (counted from the first, MENU_FIRST_WORLD_MAP): the
 * dactyls' map's is the part the dactyl doesn't come down in either. */
bool menu_epoch_kept_off_the_ground_of(GameState *g, MenuObject o, unsigned map);

/* Guards */

#endif
