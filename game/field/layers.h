/* Field mode: the three background layers -- their scrolling and edges each frame,
 * and a map's tables of tiles. */
#ifndef CT_GAME_FIELD_LAYERS_H
#define CT_GAME_FIELD_LAYERS_H

#include <stdbool.h>
#include <stdint.h>

#include "snes/system.h"
#include "field/field.h"
#include "state.h"
#include "vars.h"

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

/* A map's settings for its layers, a bit for each of BG2 and BG3 (bit n for
 * layer n): which stay still as the screen scrolls (field_layer_still), and
 * which are drawn whole when the map loads (field_layer_drawn_whole); and
 * whether the map has a BG3 tile map at all (field_map_flags). */
#define FIELD_LAYER_BIT(layer) (1u << (layer))
enum { FIELD_MAP_HAS_BG3 = 0x01 };
/* BG2 scrolls at half the screen's pace, with edges of its own
 * (field_bg2_half_scroll). */
enum { BG2_HALF_PACE = 0x80 };
enum { FIELD_COMPUTE_LAYER_EDGES_ENTRY = 0xC07F9A };

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
 * the map's two levels (TileShape, below); bits 0 and 1 which half of the
 * map's tile looks BG1's and BG2's tile takes (field/streaming.c).
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
enum { BG1_TILE_SET_HALF = 0x01, BG2_TILE_SET_HALF = 0x02 };

/* A tile's shape: all the lower part, all the upper, one of the 22 slopes
 * (field/field_data.h, FieldSlope: a line across the tile, one side of it
 * the upper part), a half or a quarter the upper, or a climb (the lower
 * part, where the party goes up and down at a walk). Any other is the lower
 * part. */
typedef enum {
    SHAPE_LOWER = 0,
    SHAPE_UPPER = 1,
    SHAPE_FIRST_SLOPE = 2,
    SHAPE_LEFT_HALF = 24,   /* past the slopes */
    SHAPE_TOP_HALF,
    SHAPE_BOTTOM_LEFT_QUARTER,
    SHAPE_BOTTOM_RIGHT_QUARTER,
    SHAPE_TOP_RIGHT_QUARTER,
    SHAPE_TOP_LEFT_QUARTER,
    SHAPE_CLIMB,
} TileShape;

static inline TileShape tile_shape(TileAttributes tile) { return (TileShape)(tile.properties >> TILE_SHAPE_SHIFT); }
static inline bool tile_shape_is_a_slope(TileShape shape) { return shape >= SHAPE_FIRST_SLOPE && shape < SHAPE_LEFT_HALF; }
static inline uint8_t tile_shape_slope(TileShape shape) { return (uint8_t)(shape - SHAPE_FIRST_SLOPE); }
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
/* The index of a column and a row of a layer's tile map in VRAM, which is
 * two halves of 32 columns side by side. */
uint16_t field_tile_map_index(uint16_t column, uint16_t row);
bool map_tile_copy_fits(uint16_t from, uint16_t to, uint16_t destination);

/* What the copy leaves its callers, worked out beside the other field
 * routines' (field/leaves.c): Y and the overflow, as its last table's copy
 * leaves them, or as it found them where it copied nothing. */
uint16_t field_tile_copy_y(const GameState *g, uint16_t before);
bool field_tile_copy_overflow(const GameState *g, bool before);

/* Every frame: each layer's edge runs around its place in its tile map
 * ($C0:7F9A); and the overflow of the runs' last sum, which the original
 * leaves its callers. */
void field_find_layer_edges(GameState *g);
bool field_layer_edges_overflow(const GameState *g);
/* For the original's code: what the edges' working out leaves in the
 * registers -- where in its tile map the last layer's column right of the
 * view starts and the row above the view it starts on ($C0:8218), and the
 * carry and overflow of that start's VRAM address worked out. */
typedef struct {
    uint16_t right_column_start, row_above;
    bool carry, overflow;
} FieldEdgesLeave;
FieldEdgesLeave field_edges_leave(const GameState *g);

/* field/tile_copy.c */
void field_copy_tiles(Snes *s);
bool field_tile_copy_fits(Snes *s);

/* When a map loads ($C0:A521): its tile maps and tile attributes unpacked
 * from the map's decompressed data; and whether its sizes are ones the
 * tables hold, and its data can't come round the banks to the layers' page
 * (the guard's game part: field/layers.c says why). */
void field_unpack_the_tile_maps(GameState *g);
bool field_tile_maps_fit(const GameState *g);
enum { FIELD_UNPACK_TILE_MAPS_ENTRY = 0xC0A521 };

/* field/layers.c */
void field_compute_layer_edges(Snes *s);
void field_update_layer_scroll(Snes *s);
void field_unpack_tile_maps(Snes *s);
bool field_unpack_stays_clear(Snes *s);

/* A layer's tile map setting: the high byte of its place in VRAM with its
 * size's bits, as the line effects send it and a battle takes it over. */
static inline uint8_t field_layer_map_setting(const GameState *g, FieldLayer layer)
{
    return (uint8_t)((field_layer_map_base(g, layer) >> 8) | field_layer_map_size(g, layer));
}

#endif
