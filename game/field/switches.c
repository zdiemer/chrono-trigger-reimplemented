/* Field mode: the map's switches and chests.
 *
 * Some of BG1's map tiles are floor switches, pressed when the leader steps
 * onto them (the step event field/walk.c asks the main loop for), and chests,
 * shown open when the leader opens one (the confirm button's, event/chests.c).
 * Both change the map: a pressed switch's tiles, and an opened chest's, move
 * on to the next tile in the map's tile set, which is how the tile set draws
 * them pressed or open. The main loop changes the map and works out where in
 * BG1's tile map on VRAM each changed tile's four entries are; the line
 * interrupt's time after the picture (field/display.c) then writes the new
 * looks there, from the looks of the pressed and the open tiles.
 *
 * A switch is one of three shapes: a tile and the one above it, two by two,
 * or two across by three down, and the leader steps onto one of its bottom
 * tiles, which says which switch and which of its columns the step is in.
 * The step is also remembered on the map (field_pressed_tiles). Pressing a
 * switch makes a sound (field/requests.c). */
#include "field/switches.h"
#include "field/field.h"
#include "field/field_data.h"
#include "field/layers.h"
#include "field/object.h"

#include "field/bridge.h"
#include "field/scratch.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The map's bytes, by the original's index ----------------------------------------------- */

/* The main loop reaches a tile by its index in the map's tables, row << 8 |
 * column, which runs on past a layer's 64 columns into the next table of the
 * row (BG2's, BG3's, the properties); the switches' neighbours are reached
 * by moving the index, as the original does. */
enum { MAP_ROW = 0x100, TABLE_COLUMNS = 0x40, PROPERTIES_TABLE = 3 };

uint8_t field_map_byte(const GameState *g, uint16_t at)
{
    unsigned row = at >> 8, table = (at >> 6) & 3, column = at & (TABLE_COLUMNS - 1);
    return table == PROPERTIES_TABLE ? field_tile_properties(g, row, column) : field_layer_maps(g, table, row, column);
}

static void set_map_byte(GameState *g, uint16_t at, uint8_t v)
{
    unsigned row = at >> 8, table = (at >> 6) & 3, column = at & (TABLE_COLUMNS - 1);
    if (table == PROPERTIES_TABLE)
        set_field_tile_properties(g, row, column, v);
    else
        set_field_layer_maps(g, table, row, column, v);
}

/* A tile moved on to the next in the tile set. */
static void next_tile(GameState *g, uint16_t at) { set_map_byte(g, at, (uint8_t)(field_map_byte(g, at) + 1)); }

/* ---- The switches -------------------------------------------------------------------------- */

/* Each shape's tiles, column by column from the left and each column from
 * the bottom up, as the map has them before they are pressed; pressed, each
 * is the next tile. The leader steps onto a column's bottom tile. */
enum { MOST_SWITCH_TILES = 6 };
typedef struct {
    uint8_t columns, rows;
    uint8_t tiles[MOST_SWITCH_TILES];
} SwitchShapeTiles;

static const SwitchShapeTiles shapes[] = {
    [SWITCH_ONE_BY_TWO] = { 1, 2, { 0xE6, 0xE4 } },
    [SWITCH_TWO_BY_TWO] = { 2, 2, { 0xEC, 0xE8, 0xEE, 0xEA } },
    [SWITCH_TWO_BY_THREE] = { 2, 3, { 0xFA, 0xF6, 0xF2, 0xFC, 0xF8, 0xF4 } },
};
enum { SHAPES = 3 };

/* The switch a stepped-on tile is the bottom of, if any. */
static bool switch_of(uint8_t tile, SwitchShape *shape, unsigned *column)
{
    for (unsigned s = 0; s < SHAPES; s++)
        for (unsigned c = 0; c < shapes[s].columns; c++)
            if (shapes[s].tiles[c * shapes[s].rows] == tile) {
                *shape = (SwitchShape)s;
                *column = c;
                return true;
            }
    return false;
}

bool field_is_switch(const GameState *g, uint16_t step)
{
    SwitchShape shape;
    unsigned column;
    return switch_of(field_map_byte(g, step), &shape, &column);
}

bool field_is_one_by_two_switch(const GameState *g, uint16_t step)
{
    SwitchShape shape;
    unsigned column;
    return switch_of(field_map_byte(g, step), &shape, &column) && shape == SWITCH_ONE_BY_TWO;
}

/* The index of a switch's tile, from the stepped-on tile's. */
static uint16_t switch_tile_at(uint16_t step, unsigned step_column, unsigned column, unsigned up)
{
    return (uint16_t)(step + column - step_column - up * MAP_ROW);
}

/* The one-by-two switch's upper tile, as the guard asks. */
uint16_t field_switch_upper_tile(uint16_t step) { return switch_tile_at(step, 0, 0, 1); }

// $C0:0D45-$C0:16DB (the step event's switches)
/* Press the switch the leader stepped on, if the tile is one: its tiles move
 * on in the map. (The original moves the one-by-two switch's upper tile on
 * as a word with the tile to its right, which only a tile of $FF would carry
 * into; the guard refuses one.) */
SwitchPress field_press_switch(GameState *g, uint16_t step)
{
    SwitchPress p = { false, SWITCH_ONE_BY_TWO, 0 };
    if (!switch_of(field_map_byte(g, step), &p.shape, &p.step_column))
        return p;
    p.pressed = true;
    const SwitchShapeTiles *s = &shapes[p.shape];
    for (unsigned c = 0; c < s->columns; c++)
        for (unsigned up = 0; up < s->rows; up++) {
            next_tile(g, switch_tile_at(step, p.step_column, c, up));
        }
    return p;
}

/* ---- Where a changed tile's entries are ------------------------------------------------------ */

/* A map tile's four entries in BG1's tile map in VRAM: where the tile is
 * from the screen's top left edge, on BG1's place in its tile map (which
 * wraps), with the layer's base. */
enum { BG1 = 0, ENTRIES_A_TILE = FIELD_ENTRIES_A_MAP_TILE, COLUMN_BITS = FIELD_TILE_MAP_COLUMNS - 1,
       ROW_BITS = FIELD_TILE_MAP_ROWS - 1 };

typedef struct {
    uint16_t entry[4];   /* top left, top right, bottom left, bottom right */
} TileEntries;

static TileEntries tile_entries(const GameState *g, int column, int row)
{
    uint16_t x = (uint16_t)((ENTRIES_A_TILE * column - field_screen_left(g) + field_layer_position_x(g, BG1)) & COLUMN_BITS);
    uint16_t y = (uint16_t)((ENTRIES_A_TILE * row - field_screen_top(g) + field_layer_position_y(g, BG1)) & ROW_BITS);
    uint16_t base = field_layer_map_base(g, BG1);
    uint16_t right = (uint16_t)((x + 1) & COLUMN_BITS), below = (uint16_t)((y + 1) & ROW_BITS);
    return (TileEntries){ { (uint16_t)(field_tile_map_index(x, y) + base), (uint16_t)(field_tile_map_index(right, y) + base),
                            (uint16_t)(field_tile_map_index(x, below) + base),
                            (uint16_t)(field_tile_map_index(right, below) + base) } };
}

/* For the original's code and shadow mode: working out the entries leaves
 * the screen's edges as words in two of the page's scratch cells, and the
 * last entry's row times 32. */
enum { LEFT_EDGE_CELL = 0xDB, TOP_EDGE_CELL = 0xDD, ENTRIES_A_ROW = FIELD_TILE_MAP_HALF_COLUMNS };
static void leave_entries(GameState *g, int last_row)
{
    original_cell16(g, LEFT_EDGE_CELL, (uint16_t)(field_screen_edges_high(g, 0) << 8 | field_screen_left(g)));
    original_cell16(g, TOP_EDGE_CELL, (uint16_t)(field_screen_edges_high(g, 1) << 8 | field_screen_top(g)));
    uint16_t y = (uint16_t)((ENTRIES_A_TILE * last_row - field_screen_top(g) + field_layer_position_y(g, BG1) + 1) & ROW_BITS);
    original_cell16(g, FIELD_LAST_WORKED_CELL, (uint16_t)(y * ENTRIES_A_ROW));
}

enum { REDRAW_CHEST = 0x02, REDRAW_SWITCH = 0x10 };

// $C0:0D45-$C0:16DB
/* The pressed switch's tiles to be redrawn: each tile's four entries, in
 * the shape's order. */
void field_queue_switch_redraw(GameState *g, SwitchPress p, uint16_t step)
{
    const SwitchShapeTiles *s = &shapes[p.shape];
    MapPlace at = map_place_at(step);
    unsigned n = 0;
    int row = 0;
    for (unsigned c = 0; c < s->columns; c++)
        for (unsigned up = 0; up < s->rows; up++) {
            row = at.row - (int)up;
            TileEntries e = tile_entries(g, at.column + (int)c - (int)p.step_column, row);
            for (unsigned k = 0; k < 4; k++)
                set_field_switch_entries(g, n++, e.entry[k]);
        }
    set_field_switch_shape(g, (uint8_t)p.shape);
    set_field_tiles_to_redraw(g, (uint8_t)(field_tiles_to_redraw(g) | REDRAW_SWITCH));
    ORIGINAL_EFFECT(leave_entries(g, row));
}

/* ---- The chests ---------------------------------------------------------------------------- */

/* A chest as the map shows it closed: the tile $FE, or $E0; open, the next
 * tile. The main loop's code that opens one is still the original's, which
 * leaves the chest's kind and its entries for the redraw. */
static uint8_t closed_chest_tile(ChestKind kind) { return kind == CHEST_ON_TILE_FE ? 0xFE : 0xE0; }

/* ---- Redrawing, after the picture ----------------------------------------------------------- */

/* A pressed or open tile's look: the upper half of the tile set's looks
 * (field_tile_looks), where the tile set puts its switches and chests. */
enum { UPPER_LOOKS = 0x100 };

/* The shape pressed last; a shape past the three redraws as the largest,
 * as the original's does. */
static const SwitchShapeTiles *pressed_shape(const GameState *g)
{
    unsigned shape = field_switch_shape(g);
    return &shapes[shape < SHAPES ? shape : SWITCH_TWO_BY_THREE];
}

/* A tile's quarter-look, `k` (top left, top right, bottom left, bottom
 * right), written to its entry. */
static void redraw_quarter(Platform *pf, const GameState *g, uint8_t tile, uint16_t entry, unsigned k)
{
    unsigned look = UPPER_LOOKS | tile;
    uint16_t word = k == 0 ? field_tile_looks_top_left(g, look)
                  : k == 1 ? field_tile_looks_top_right(g, look)
                  : k == 2 ? field_tile_looks_bottom_left(g, look)
                           : field_tile_looks_bottom_right(g, look);
    vid_set_vram_address(pf, PF_AT(0xC0ED24), entry);
    vid_write_vram(pf, PF_AT(0xC0ED24), word);
}

// $C0:ED24-$C0:F05D
/* Draw the looks a switch pressed or a chest opened since the last time:
 * the pressed switch's first, and a chest's the next time. The looks are of
 * the tiles the shape presses and the chest opens, whatever the map now
 * holds. */
void field_redraw_changed_tiles(GameState *g, Platform *pf)
{
    uint8_t waiting = field_tiles_to_redraw(g);
    if (waiting & REDRAW_SWITCH) {
        const SwitchShapeTiles *s = pressed_shape(g);
        vid_start_vram_uploads(pf, PF_AT(0xC0ED33), VID_VRAM_WORD_PORT);
        unsigned n = 0;
        for (unsigned t = 0; t < (unsigned)s->columns * s->rows; t++)
            for (unsigned k = 0; k < 4; k++, n++)
                redraw_quarter(pf, g, (uint8_t)(s->tiles[t] + 1), field_switch_entries(g, n), k);
        set_field_tiles_to_redraw(g, (uint8_t)(waiting & ~REDRAW_SWITCH));
        return;
    }
    if (waiting & REDRAW_CHEST) {
        ChestKind kind = field_chest_kind(g) == CHEST_ON_TILE_FE ? CHEST_ON_TILE_FE : CHEST_ON_TILE_E0;
        vid_start_vram_uploads(pf, PF_AT(0xC0EFDC), VID_VRAM_WORD_PORT);
        for (unsigned k = 0; k < 4; k++)
            redraw_quarter(pf, g, (uint8_t)(closed_chest_tile(kind) + 1), field_chest_entries(g, k), k);
        set_field_tiles_to_redraw(g, (uint8_t)(waiting & ~REDRAW_CHEST));
    }
}

/* ---- The step, remembered ------------------------------------------------------------------ */

// $C0:28C0
/* A step event set off is remembered on the map, in the first free place of
 * the list, unless it is there already or the list is full. */
enum { NO_STEP = 0x8000 };
void field_remember_step(GameState *g, uint16_t step)
{
    for (unsigned i = 0; i < FIELD_PRESSED_TILES_COUNT; i++) {
        uint16_t kept = field_pressed_tiles(g, i);
        if (kept & NO_STEP) {
            set_field_pressed_tiles(g, i, step);
            return;
        }
        if (kept == step)
            return;
    }
}

unsigned field_switch_entry_count(const GameState *g)
{
    const SwitchShapeTiles *s = pressed_shape(g);
    return (unsigned)s->columns * s->rows * 4u;
}

unsigned field_switch_rows(SwitchShape shape) { return shapes[shape].rows; }
