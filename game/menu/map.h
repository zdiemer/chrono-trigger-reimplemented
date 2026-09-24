/* Menu: the map the sprite-object mode scrolls and redraws (game/menu/map.c). */
#ifndef CT_GAME_MENU_MAP_H
#define CT_GAME_MENU_MAP_H

#include "snes/system.h"

/* The entries run on the menus' page, with the data bank on low RAM; the
 * scrolls and the redraw take the plane and the step from the object mode's
 * first cells (map.c's guards say what else the game keeps them to). */
bool menu_map_page_is_menu(Snes *s);
bool menu_map_scroll_x_runs(Snes *s);
bool menu_map_scroll_y_runs(Snes *s);
bool menu_map_redraw_runs(Snes *s);

void menu_map_centre_on_party(Snes *s);
void menu_map_scroll_x(Snes *s);
void menu_map_scroll_y(Snes *s);
void menu_map_redraw(Snes *s);
void menu_map_upload_block(Snes *s);

#endif
