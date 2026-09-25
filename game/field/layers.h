/* Field mode: the three background layers -- their scrolling and edges each frame,
 * and a map's tables of tiles. */
#ifndef CT_GAME_FIELD_LAYERS_H
#define CT_GAME_FIELD_LAYERS_H

#include <stdbool.h>
#include <stdint.h>

#include "snes/system.h"
#include "state.h"

/* A layer's edge runs: the stretches of its tile map that come into view as
 * the layer scrolls, which field/layers.c works out every frame and the
 * deferred uploads (field/display.c) send. There are four: the view's top
 * row and the row below its bottom, each from a column left of the screen
 * across the view, split where it crosses into the tile map's other half and
 * where it wraps round; and the view's left column and the column right of
 * it, each from a row above the screen down, split where it wraps round the
 * map. The original keeps them as two tables of words, the VRAM addresses
 * (field_layer_edge_vram, ten a layer) and the parts' sizes
 * (field_layer_edge_sizes, five): these say which word is which. The two
 * rows share their sizes, as do the two columns. */
enum {
    EDGE_TOP_ROW = 0,          /* three addresses: from the start, the other half, the wrap */
    EDGE_BOTTOM_ROW = 3,       /* the same three */
    EDGE_LEFT_COLUMN = 6,      /* two addresses: from the start, the wrap */
    EDGE_RIGHT_COLUMN = 8,     /* the same two */
    EDGE_ADDRESSES = 10,
};
enum {
    EDGE_ROW_SIZES = 0,        /* three sizes, the rows' parts */
    EDGE_COLUMN_SIZES = 3,     /* two, the columns' */
    EDGE_SIZES = 5,
};

/* ---- A map's tiles ----------------------------------------------------------------------- */

/* A place in a map's tables, a row and a column: the unpacked tile maps, and
 * the three bytes the map gives each tile. The original indexes them by
 * row << 8 | column. */
typedef struct {
    uint8_t row, column;
} MapPlace;

static inline MapPlace map_place_at(uint16_t index) { return (MapPlace){ (uint8_t)(index >> 8), (uint8_t)index }; }

/* What the map says of a tile: a byte of properties, and two of attributes.
 *
 * The properties' top six bits are the tile's shape, how it is parted between
 * the map's two levels (field/walk.c, TileShape).
 *
 * The upper attributes: bit 6 raises the upper half of an object standing
 * there in front of the map; bit 4 is a step event, set off by the leader
 * walking onto the tile beyond it; bits 0-3 a moving floor, bits 2-3 how fast
 * it pulls (none, slow, walking pace, fast) and bits 0-1 which way (up, down,
 * left, right).
 *
 * The lower attributes: bit 7 blocks objects' moves onto the tile, and bit 6
 * raises an object's lower half; bits 0-2 the level of the tile's lower part
 * and bits 3-5 its upper part's, each the level (0 none, 1, 2, or 3 stairs
 * joining them) and a crossing bit above it (a way over or under that leaves
 * the leader on its level). */
typedef struct {
    uint8_t properties, upper, lower;
} TileAttributes;
enum { TILE_RAISES_SPRITES = 0x40, TILE_BLOCKED = 0x80 };
enum { TILE_SHAPE_SHIFT = 2 };
enum { TILE_STEP_EVENT = 0x10, TILE_FLOOR_SPEED = 0x0C, TILE_FLOOR_WAY = 0x03 };
enum { TILE_LOWER_PART = 0, TILE_UPPER_PART_SHIFT = 3, TILE_LEVEL_BITS = 0x03, TILE_CROSSING_BIT = 0x04 };

/* The priority a sprite's half takes on a tile (object.h's 0-3): in front of
 * the map where the tile raises it, among the map's layers otherwise. */
enum { AMONG_THE_MAP = 2, IN_FRONT_OF_THE_MAP = 3 };
static inline uint8_t tile_priority(uint8_t attributes)
{
    return (attributes & TILE_RAISES_SPRITES) ? IN_FRONT_OF_THE_MAP : AMONG_THE_MAP;
}

TileAttributes map_tile_attributes(const GameState *g, MapPlace p);

/* For the original's effects only (original_effects.h): the attributes the
 * original reads for a place off the map, whose row and column it indexes
 * the tables by all the same -- the next bytes of the row (column 64's upper
 * attributes are column 0's lower ones), or bank $7F's past the last row for
 * (255, 255). Only the two attribute bytes, which is what layering reads. */
TileAttributes map_tile_attributes_past_the_map(const GameState *g, MapPlace p);

/* A script's change to the map (field/tile_copy.c): make the copy
 * field_tile_copy describes; and whether a copy of the rectangle from one
 * tile to another to a destination stays on the map, as the game's copies
 * do (the guards'). */
void map_copy_tiles(GameState *g);
bool map_tile_copy_fits(uint16_t from, uint16_t to, uint16_t destination);

/* What the copy leaves its callers, worked out beside the other field
 * routines' (field/leaves.c): Y and the overflow, as its last table's copy
 * leaves them, or as it found them where it copied nothing. */
uint16_t field_tile_copy_y(const GameState *g, uint16_t before);
bool field_tile_copy_overflow(const GameState *g, bool before);

/* field/tile_copy.c */
void field_copy_tiles(Snes *s);
bool field_tile_copy_fits(Snes *s);

/* field/layers.c */
void field_compute_layer_edges(Snes *s);
void field_update_layer_scroll(Snes *s);
void field_unpack_tile_maps(Snes *s);
bool field_unpack_stays_clear(Snes *s);

#endif
