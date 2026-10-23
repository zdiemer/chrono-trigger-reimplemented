/* Field mode: a location's map, unpacked.
 *
 * Each location shows one of the game's maps (its record names it,
 * field_data.h), packed in the ROM. Loading the location unpacks it into work
 * RAM (data/decompress.h): a header of six bytes, then the layers' tile maps
 * and the tiles' attributes. The header says how big each of the three layers'
 * tile maps is (16 to 64 tiles each way), whether there is a third, how BG2
 * and BG3 move as the screen scrolls -- following it, BG2 at half its pace,
 * standing still, or drifting on their own -- which layers make up the main
 * and the sub screen, and which take color math. The field keeps those, works
 * out from the sizes where each layer's map repeats, and from the location's
 * record how far the screen and the leader may go; unpacks the tile maps
 * (field/layers.c); and marks the tiles the location's exits are on, which
 * the leader's walk takes the party out by (field/walk.c). */
#include "field/field.h"
#include "field/layers.h"
#include "field/map.h"

#include "charge.h"
#include "data/decompress.h"
#include "event/chests.h"
#include "field/bridge.h"
#include "field/field_data.h"
#include "field/scratch.h"
#include "original_effects.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"

/* ---- The header ------------------------------------------------------------------------------- */

/* A layer's size, two bits each way: 16 tiles and 16 more a step. */
enum { SIZE_BITS = 0x03, TILES_A_SIZE_STEP = 16 };
static uint8_t size_of(uint8_t bits) { return (uint8_t)(((bits & SIZE_BITS) + 1) * TILES_A_SIZE_STEP); }

enum { HEADER_HAS_BG3 = 0x80, HEADER_BG2_AT_HALF_PACE = 0x40, HEADER_BG3_STILL = 0x20, HEADER_BG2_STILL = 0x10 };

static void set_layer_size(GameState *g, unsigned layer, uint8_t columns, uint8_t rows)
{
    set_field_map_layer_sizes_width(g, layer, columns);
    set_field_map_layer_sizes_height(g, layer, rows);
}

/* The layers' sizes, BG3's, and how BG2 and BG3 follow the screen. */
static void take_the_layers(GameState *g)
{
    uint8_t sizes = field_map_header_sizes(g), layers = field_map_header_layers(g);
    for (unsigned layer = FIELD_BG1; layer <= FIELD_BG3; layer++) {
        ORIGINAL_EFFECT(set_field_map_layer_sizes_width_high(g, layer, 0));
        ORIGINAL_EFFECT(set_field_map_layer_sizes_height_high(g, layer, 0));
    }
    set_field_map_flags(g, (layers & HEADER_HAS_BG3) ? FIELD_MAP_HAS_BG3 : 0);
    set_field_bg2_half_scroll(g, (layers & HEADER_BG2_AT_HALF_PACE) ? BG2_HALF_PACE : 0);
    uint8_t still = 0;
    if (layers & HEADER_BG3_STILL)
        still |= FIELD_LAYER_BIT(FIELD_BG3);
    if (layers & HEADER_BG2_STILL)
        still |= FIELD_LAYER_BIT(FIELD_BG2);
    set_field_layer_still(g, still);
    set_layer_size(g, FIELD_BG3, size_of(layers), size_of(layers >> 2));
    set_layer_size(g, FIELD_BG1, size_of(sizes), size_of(sizes >> 2));
    set_layer_size(g, FIELD_BG2, size_of(sizes >> 4), size_of(sizes >> 6));
}

/* The screens: BG1-BG3 and the sprites on each, from three bits and one.
 * Without a tile set for BG3 neither screen shows it. */
enum { SCREEN_LAYERS = 0x07, SCREEN_SPRITES = 0x08, SUB_SCREEN_SHIFT = 4, SPRITES_SHIFT = 1 };
enum { WITHOUT_BG3 = VID_BG1_BIT | VID_BG2_BIT | VID_OBJ_BIT };

static uint8_t screen_layers(uint8_t bits)
{
    return (uint8_t)((bits & SCREEN_LAYERS) | (bits & SCREEN_SPRITES) << SPRITES_SHIFT);
}

static void take_the_screens(GameState *g)
{
    uint8_t math = field_map_header_color_math(g);
    set_field_color_math_layers(g, math);
    set_field_fixed_color_math_layers(g, math);
    uint8_t screens = field_map_header_screens(g);
    uint8_t main_layers = screen_layers(screens), sub_layers = screen_layers((uint8_t)(screens >> SUB_SCREEN_SHIFT));
    bool no_bg3 = field_map_has_no_bg3_set(g) != 0;
    set_field_main_screen(g, no_bg3 ? (uint8_t)(main_layers & WITHOUT_BG3) : main_layers);
    set_field_sub_screen(g, no_bg3 ? (uint8_t)(sub_layers & WITHOUT_BG3) : sub_layers);
}

/* A layer's drift, each way: a speed in 1/16 pixels a frame, 1 doubled for
 * each step past the first (0, none, leaves the speed it had), and its sign. */
enum { DRIFT_ACROSS = 0, DRIFT_DOWN = 4, DRIFT_STEPS = 0x07, DRIFT_BACK = 0x08 };

static bool drift_speed(uint8_t drift, unsigned way, uint8_t *speed)
{
    unsigned steps = (drift >> way) & DRIFT_STEPS;
    if (steps == 0)
        return false;
    uint8_t s = (uint8_t)(1u << (steps - 1));
    *speed = (drift >> way) & DRIFT_BACK ? (uint8_t)-s : s;
    return true;
}

/* A drifting layer moves on its own, and is drawn whole when the map loads. */
static void take_the_drift(GameState *g, unsigned layer, uint8_t drift)
{
    if (drift == 0)
        return;
    uint8_t speed;
    if (drift_speed(drift, DRIFT_ACROSS, &speed))
        set_field_layer_speed_x(g, layer - 1, speed);
    if (drift_speed(drift, DRIFT_DOWN, &speed))
        set_field_layer_speed_y(g, layer - 1, speed);
    set_field_layer_drawn_whole(g, (uint8_t)(field_layer_drawn_whole(g) | FIELD_LAYER_BIT(layer)));
}

/* For the original's code and shadow mode (field/scratch.h): the original
 * reads the header through a cell of the layers' page, which it leaves
 * holding the screens' byte. (The cells after it the tile maps' unpacking
 * leaves its own values in.) */
static void leave_the_header(GameState *g) { original_layer_cell8(g, 0x50, field_map_header_screens(g)); }

// $C0:A33B (from the unpacked header on)
static void take_the_header(GameState *g)
{
    take_the_layers(g);
    take_the_screens(g);
    set_field_layer_drawn_whole(g, field_layer_still(g));
    take_the_drift(g, FIELD_BG2, field_map_header_bg2_drift(g));
    take_the_drift(g, FIELD_BG3, field_map_header_bg3_drift(g));
    ORIGINAL_EFFECT(leave_the_header(g));
}

// $C0:7399
/* Where each layer's map repeats, for an index of its tiles (row << 8 |
 * column): the size each way rounded up to a power of two, less one; and the
 * tiles the screen and the leader keep within, the record's or the whole
 * map. */
enum { REPEATS_EVERY_16 = 0x0F, REPEATS_EVERY_32 = 0x1F, REPEATS_EVERY_64 = 0x3F };

static uint8_t repeat_mask(uint8_t tiles)
{
    switch (tiles) {
    case 64:
    case 48:
        return REPEATS_EVERY_64;
    case 32:
        return REPEATS_EVERY_32;
    default:
        return REPEATS_EVERY_16;
    }
}

static uint16_t layer_repeat(const GameState *g, unsigned layer)
{
    return (uint16_t)(repeat_mask(field_map_layer_sizes_height(g, layer)) << 8 |
                      repeat_mask(field_map_layer_sizes_width(g, layer)));
}

static void set_the_edges(GameState *g, MapEdges e)
{
    set_field_map_edges_left(g, e.left);
    set_field_map_edges_top(g, e.top);
    set_field_map_edges_right(g, e.right);
    set_field_map_edges_bottom(g, e.bottom);
}

static void take_the_repeats_and_edges(GameState *g)
{
    set_field_map_index_mask(g, layer_repeat(g, FIELD_BG1));
    set_field_layer_index_masks(g, FIELD_BG2 - 1, layer_repeat(g, FIELD_BG2));
    set_field_layer_index_masks(g, FIELD_BG3 - 1, layer_repeat(g, FIELD_BG3));
    MapEdges e = location_map_edges(g, location_record_offset(g));
    if (e.left & MAP_EDGES_THE_WHOLE_MAP)
        e = (MapEdges){ 0, 0, (uint8_t)(field_map_layer_sizes_width(g, FIELD_BG1) - 1),
                        (uint8_t)(field_map_layer_sizes_height(g, FIELD_BG1) - 1) };
    set_the_edges(g, e);
}

// $C0:A66B
/* The tiles the location's exits are on, each marked with its exit (a run of
 * tiles across or down, which wraps round the map as BG1's map repeats); the
 * other tiles none. */
enum { NO_EXIT = 0x80, EXIT_TABLE_COLUMNS = 64, EXIT_TABLE_ROWS = 64 };

static void mark(GameState *g, uint16_t tile, uint8_t exit)
{
    MapPlace p = map_place_at(tile);
    set_field_tile_exits(g, p.row, p.column, exit);
}

static uint16_t next_exit_tile(uint16_t tile, bool down, uint16_t repeat)
{
    if (down)
        return (uint16_t)((tile + 0x100) & repeat);
    return (uint16_t)((tile & 0xFF00) | ((tile + 1) & repeat & 0x00FF));
}

/* For the original's code and shadow mode: where the original kept the last
 * exit's record (where there was one), how many exits it marked, and where
 * the run ends. */
static void leave_the_exits(GameState *g, FieldExitRun run, const uint16_t *last, uint8_t marked)
{
    if (last)
        original_layer_cell16(g, 0x52, *last);
    original_layer_cell16(g, 0x54, marked);
    original_layer_cell16(g, 0x56, run.end);
}

static void mark_the_exits(GameState *g)
{
    for (unsigned row = 0; row < EXIT_TABLE_ROWS; row++)
        for (unsigned column = 0; column < EXIT_TABLE_COLUMNS; column++)
            set_field_tile_exits(g, row, column, NO_EXIT);
    FieldExitRun run = field_location_exit_run(g, location_id(g));
    set_field_location_exits(g, run.first);
    uint16_t repeat = field_map_index_mask(g);
    uint8_t exit = 0;
    uint16_t last = run.first;
    bool any = false;
    for (uint16_t at = run.first; at < run.end; at = (uint16_t)(at + FIELD_EXIT_BYTES)) {
        FieldExit e = field_exit(g, at, 0);
        bool down = (e.reach & EXIT_RUNS_DOWN) != 0;
        uint16_t tile = (uint16_t)((e.y << 8 | e.x) & repeat);
        mark(g, tile, exit);
        for (unsigned more = e.reach & EXIT_MORE_TILES; more != 0; more--) {
            tile = next_exit_tile(tile, down, repeat);
            mark(g, tile, exit);
        }
        exit++;
        last = at;
        any = true;
    }
    ORIGINAL_EFFECT(leave_the_exits(g, run, any ? &last : NULL, exit));
}

/* ---- The map unpacked ------------------------------------------------------------------------- */

/* Where the map is unpacked, from its header on; and where the map's number
 * stays for the original's code and shadow mode. */
enum { UNPACKED_MAP = RAM_FIELD_MAP_HEADER };

void field_ask_to_unpack_the_map(GameState *g)
{
    uint16_t map = location_map(g, location_record_offset(g));
    ORIGINAL_EFFECT(original_cell16(g, FIELD_SCRATCH_CELLS, map));
    set_decompress_source(g, field_packed_map(g, map));
    set_decompress_dest(g, UNPACKED_MAP);
}

void field_take_the_map_in(GameState *g) { take_the_header(g); take_the_repeats_and_edges(g); }

void field_mark_the_exits(GameState *g) { mark_the_exits(g); }

/* The tile maps' unpacking is a port run as C where its sizes are ones its
 * guard takes: every map the game loads. */
static void unpack_the_map(GameState *g)
{
    field_ask_to_unpack_the_map(g);
    TIMED_AS(g, UNPACK_STREAM_ENTRY, (void)unpack_stream(g));
    field_take_the_map_in(g);
}

// $C0:A33B
bool field_load_the_map(GameState *g)
{
    unpack_the_map(g);
    if (!field_tile_maps_fit(g))
        return false;
    TIMED_AS(g, FIELD_UNPACK_TILE_MAPS_ENTRY, field_unpack_the_tile_maps(g));
    field_mark_the_exits(g);
    return true;
}

/* ---- Where it meets the original ---------------------------------------------------------------- */

/* Tile maps the port doesn't take are the original's, on the layers' page;
 * it is entered with the layers' page's high byte in A's, which the original
 * set D through. */
enum { THE_TILE_MAPS_IGNORE = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };
static void leave_the_page_in_a(Snes *s) { s->cpu.a = (uint16_t)((FIELD_LAYERS_PAGE & 0xFF00) | (s->cpu.a & 0x00FF)); }

void field_unpack_the_map(Snes *s)
{
    GameState *g = state_of(s);
    if (!field_load_the_map(g)) {
        ORIGINAL_EFFECT(leave_the_page_in_a(s));
        if (!bridge_call_on_page(s, 0xC0A500, FIELD_UNPACK_TILE_MAPS_ENTRY, THE_TILE_MAPS_IGNORE, FIELD_LAYERS_PAGE))
            return;
        field_mark_the_exits(g);
    }
    m_rts(s);
}

// $C0:A712
/* The map's chests and walkable tiles marked (event/chests.c), on the
 * layers' page; nothing it leaves in the registers is read. */
void field_mark_the_chests(Snes *s)
{
    chests_mark_the_map(state_of(s));
    m_rts(s);
}
