/* A world map as a Mode 7 picture: a pixel a map tile across and two down.
 *
 * Two screens draw one: the Epoch's map view (menu/epoch.c), from whichever
 * world map the party is on, and the Day of Lavos's turning world
 * (menu/turning_world.c), from the 1000 A.D. map. A map tile is drawn from
 * its tile group's right-hand tiles (the group's top right and bottom right
 * words, a pixel each): the tile's colour on the picture (menu_map_colours,
 * sampled from its graphics), in the word's palette, so the pixel is the
 * CGRAM entry. A colour of 0 draws nothing and leaves what is under it, which
 * is how a map's second plane is drawn over its first.
 *
 * The map is 96 tiles across and 64 down; the pixels go into Mode 7 tiles of
 * 64, eight map columns a tile and four map rows a row of tiles, twelve
 * tiles across. */
#ifndef CT_GAME_MENU_MAP_PICTURE_H
#define CT_GAME_MENU_MAP_PICTURE_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"
#include "vars.h"

enum {
    MENU_MAP_COLUMNS = 96,
    MENU_MAP_ROWS = 64,
    MENU_MODE7_TILE_PIXELS = 64,
    MENU_MAP_PICTURE_TILES_ACROSS = MENU_MAP_COLUMNS / 8,
    MENU_MAP_PICTURE_TILES_DOWN = 2 * MENU_MAP_ROWS / 8,
    MENU_MAP_PICTURE_TILE_ROW = MENU_MAP_PICTURE_TILES_ACROSS * MENU_MODE7_TILE_PIXELS,
    MENU_MAP_ROWS_A_TILE_ROW = 4,
};

/* A tile group: four words, a tile each; the picture takes the right-hand
 * two, whose tile numbers and palettes it reads. */
enum { MENU_MAP_GROUP_BYTES = 8, MENU_MAP_TOP_RIGHT = 2, MENU_MAP_BOTTOM_RIGHT = 6 };
enum { MENU_MAP_TILE_NUMBER = 0x03FF, MENU_MAP_PALETTE_BITS = 0x1C00 };

typedef struct {
    bool drawn;
    uint8_t pixel;
} MenuMapPixel;

/* The palette a group's word names, as the CGRAM row's first entry. */
static inline uint8_t menu_map_palette(uint16_t word) { return (uint8_t)((word & MENU_MAP_PALETTE_BITS) >> 6); }

/* The pixel a group's word draws, in `palette` (which the original reads
 * before the colour). */
static inline MenuMapPixel menu_map_pixel(const GameState *g, uint16_t word, uint8_t palette)
{
    uint16_t tile = word & MENU_MAP_TILE_NUMBER;
    uint8_t colour = tile < MENU_MAP_COLOURS_COUNT ? menu_map_colours(g, tile) : menu_map_colours_unchecked(g, tile);
    if (colour == 0)
        return (MenuMapPixel){ false, 0 };
    return (MenuMapPixel){ true, (uint8_t)(colour | palette) };
}

/* Where a map tile's top pixel goes in its row of picture tiles (the bottom
 * one is the next line of the same tile, eight bytes on). */
static inline uint16_t menu_map_picture_offset(unsigned column, unsigned row)
{
    return (uint16_t)((column / 8) * MENU_MODE7_TILE_PIXELS + column % 8 + (2 * row % 8) * 8);
}

#endif
