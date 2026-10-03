/* The digits two of the menus' screens draw their numbers with
 * (game/menu/digits.c): a screen's small number tiles, and the items
 * screen's amounts. */
#ifndef CT_GAME_MENU_DIGITS_H
#define CT_GAME_MENU_DIGITS_H

#include <stdbool.h>

#include "snes/system.h"
#include "state.h"

/* Build the number tiles (menu_number_tiles, menu_hundreds_tiles) and the
 * amounts' digits (menu_amount_digit_tiles) in work RAM. */
void menu_build_number_tiles(GameState *g);
void menu_build_amount_digits(GameState *g);

/* The registered entries ($FF:F548, $FF:F7D9) and their guard. */
void menu_build_number_tiles_entry(Snes *s);
void menu_build_amount_digits_entry(Snes *s);
bool menu_digits_are_buildable(Snes *s);
bool menu_number_tiles_are_buildable(Snes *s);

#endif
