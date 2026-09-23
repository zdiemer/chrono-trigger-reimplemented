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
 * The upper attributes' bit 6 raises the upper half of an object standing
 * there in front of the map; the lower attributes' bit 7 blocks moves onto
 * the tile, and bit 6 raises an object's lower half. */
typedef struct {
    uint8_t properties, upper, lower;
} TileAttributes;
enum { TILE_RAISES_SPRITES = 0x40, TILE_BLOCKED = 0x80 };

TileAttributes map_tile_attributes(const GameState *g, MapPlace p);

/* field/layers.c */
void field_compute_layer_edges(Snes *s);
void field_update_layer_scroll(Snes *s);
void field_unpack_tile_maps(Snes *s);
bool field_unpack_stays_clear(Snes *s);

#endif
