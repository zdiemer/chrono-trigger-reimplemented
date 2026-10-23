/* Field mode: the pause, and the menus opened from the field (field/menus.c). */
#ifndef CT_GAME_FIELD_MENUS_H
#define CT_GAME_FIELD_MENUS_H

#include <stdbool.h>

#include "snes/system.h"
#include "state.h"

void field_pause_and_menus(Snes *s);
/* The same as C, false where the pause or a menu opens (the machine's yet). */
bool field_pause_and_menus_as_c(GameState *g);
/* The menu a script asked for, from the main loop's requests
 * (field/requests.c): true where it came back to the main loop's caller's
 * state, false where it stopped. */
bool field_open_script_menu(Snes *s);
void field_press_start(Snes *s);
void field_bring_the_party_back(Snes *s);
bool field_party_can_come_back(Snes *s);
bool field_start_can_be_pressed(Snes *s);

#endif
