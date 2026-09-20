/* Menu: the map the sprite-object mode scrolls and redraws (bank C2). */
#ifndef CT_GAME_MENU_MAP_H
#define CT_GAME_MENU_MAP_H

#include "snes/system.h"

/* Where a plane keeps its 96 x 64 map bytes, one per 16 x 16 pixel map tile.
 * `plane` is the plane number already doubled, as the original indexes it. */
uint16_t menu_map_data(Snes *s, uint16_t plane);

/* These run on the menus' page. */
bool menu_map_page_is_menu(Snes *s);
/* The block upload sets the data bank from the direct page and writes the PPU
 * through it, so the page's low byte has to name an I/O bank. */
bool menu_map_page_is_io(Snes *s);

void menu_map_centre_on_party(Snes *s);
void menu_map_scroll_x(Snes *s);
void menu_map_scroll_y(Snes *s);
void menu_map_redraw(Snes *s);
void menu_map_upload_block(Snes *s);

#endif
