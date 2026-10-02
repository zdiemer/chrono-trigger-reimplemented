/* Touches the menus' screens make to a tilemap they are building in work RAM
 * (menu/tiles.c): a character's large portrait, and the attributes of
 * stretches of tiles. */
#ifndef CT_GAME_MENU_TILES_H
#define CT_GAME_MENU_TILES_H

#include <stdbool.h>
#include <stdint.h>

#include "snes/system.h"
#include "state.h"

/* A character's large portrait, six tiles by six from `origin`, numbered on
 * from the character's first in the attribute the portrait's tile word
 * (menu_scratch_portrait_tile) holds in its high byte, and its colours into
 * the palette row that attribute names. */
void menu_draw_portrait(GameState *g, uint16_t origin, uint8_t character);

/* A stretch of `tiles` tiles of a row and the row below it, from `at`,
 * given one attribute. */
void menu_colour_row_pair(GameState *g, uint16_t at, uint32_t tiles, uint8_t attribute);

/* `rows` rows of `tiles` tiles from `at` given the palette and priority in
 * `colour` (the attribute's top bits); the drawing origin is left a row past
 * the block. */
void menu_recolour_block(GameState *g, uint16_t at, uint32_t rows, uint32_t tiles, uint8_t colour);

void menu_draw_large_portrait(Snes *s);
void menu_set_row_pair_attribute(Snes *s);
void menu_recolour_rows(Snes *s);

bool menu_large_portrait_is_drawable(Snes *s);
bool menu_row_pair_is_colourable(Snes *s);
bool menu_rows_are_colourable(Snes *s);

#endif
