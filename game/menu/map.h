/* Menu: the map the sprite-object mode scrolls and redraws (game/menu/map.c). */
#ifndef CT_GAME_MENU_MAP_H
#define CT_GAME_MENU_MAP_H

#include <stdbool.h>
#include <stdint.h>

#include "menu/object.h"
#include "snes/system.h"
#include "state.h"

/* Scroll one of the two planes that scroll (1 or 2, as the scripts name
 * them) by `step` pixels along an axis, negative back: the scroll command's
 * work, for a C caller (the overworld's walk moves the map with the
 * leader). A scroll queues a strip of the map for every tile it brings into
 * view; menu_map_scroll_records says the most a scroll of `step` queues, and
 * menu_map_can_scroll whether a plane can be scrolled with that many more
 * records queued (the plane on the map, the upload buffer in work RAM, the
 * queue whole and with room). */
void menu_map_scroll(GameState *g, uint8_t plane, MenuAxis axis, int8_t step);
unsigned menu_map_scroll_records(MenuAxis axis, int8_t step);
bool menu_map_can_scroll(const GameState *g, uint8_t plane, unsigned records);

/* The 8x8 tile of the map the camera looks at (map.c says how the camera is
 * kept), round the map: what the original keeps as where the party stood
 * when it leaves the world map. */
typedef struct {
    uint8_t column, row;
} MenuMapTile;
MenuMapTile menu_map_camera_looks_at(const GameState *g);

/* The 8x8 tile at the middle of the view (map.c says where), counted from
 * the map's top-left and not brought round it. */
typedef struct {
    uint16_t column, row;
} MenuViewCentre;
MenuViewCentre menu_map_view_centre(const GameState *g);

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
