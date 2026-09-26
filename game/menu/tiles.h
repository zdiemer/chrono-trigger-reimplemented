/* Touches the menus' screens make to a tilemap they are building in work RAM
 * (menu/tiles.c): a character's large portrait, and the attributes of
 * stretches of tiles. */
#ifndef CT_GAME_MENU_TILES_H
#define CT_GAME_MENU_TILES_H

#include <stdbool.h>

#include "snes/system.h"

void menu_draw_large_portrait(Snes *s);
void menu_set_row_pair_attribute(Snes *s);
void menu_recolour_rows(Snes *s);

bool menu_large_portrait_is_drawable(Snes *s);
bool menu_row_pair_is_colourable(Snes *s);
bool menu_rows_are_colourable(Snes *s);

#endif
