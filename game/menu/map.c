/* Menu mode: the map the sprite-object mode scrolls and redraws.
 *
 * The mode at $C2:0000 (menu/object.h) shows a map in three planes. A plane
 * is 96 x 64 map tiles of 16 x 16 pixels, one byte each in work RAM; the byte
 * picks one of 256 tile groups, four background tiles laid out two by two,
 * also in work RAM; and the group's tiles go into a 64 x 32 tilemap in VRAM,
 * which holds two 32 x 32 screens side by side. menu_data.h's plane tables
 * say where a plane's groups, map and tilemap are.
 *
 * The object scripts drive it (menu/object_script.c's scroll and redraw
 * commands):
 *
 * - **Scrolling** moves a plane's scroll a pixel at a time, carrying its
 *   top-left background tile along, and each time a whole tile column or row
 *   comes into view it builds the strip of tilemap words that fills it in the
 *   mode's upload buffer and queues the strip for the next vblank (menu/vram.h).
 * - **A redraw** puts a plane's scroll where its top-left tile says and builds
 *   the whole screen's worth of it, eighteen blocks of sixteen map tiles, each
 *   block sent straight to VRAM rather than queued. The screen is blanked while
 *   it runs.
 *
 * The map bytes are put there by the screen's own setup and by the object
 * scripts' write-a-map-byte command, so nothing here loads them.
 *
 * Only the first two planes scroll: the third plane's column is the first
 * plane's row, as the game keeps them, so scrolling it across would move the
 * first plane down. No screen does.
 */
#include "menu/map.h"

#include "charge.h"
#include "menu/menu_data.h"
#include "menu/object.h"
#include "menu/vram.h"
#include "original_effects.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* ---- the shape of a plane ------------------------------------------------------------------ */

enum {
    TILE_PIXELS = 8,              /* pixels across a background tile, and down it */
    MAP_COLUMNS = 96,             /* map tiles across a plane: one row of map bytes */
    MAP_ROWS = 64,                /* and down it */
    MAP_BYTES = MAP_COLUMNS * MAP_ROWS,
    TILE_COLUMNS = 2 * MAP_COLUMNS,   /* background tiles across it, two a map tile ... */
    TILE_ROWS = 2 * MAP_ROWS,         /* ... and down */
    TILEMAP_COLUMNS = 32,         /* background tiles across one tilemap screen ... */
    TILEMAP_ROWS = 32,            /* ... and down */
    TILEMAP_WIDTH = 2 * TILEMAP_COLUMNS * TILE_PIXELS,  /* pixels across both screens ... */
    TILEMAP_HEIGHT = TILEMAP_ROWS * TILE_PIXELS,        /* ... and down one */
    TILEMAP_SCREEN = 0x400,       /* words from one tilemap screen to the next */
    STRIP_WORDS = TILEMAP_ROWS,   /* tilemap words in one strip: a whole tilemap column */
    STRIP_BYTES = 2 * STRIP_WORDS,
    SCREEN_COLUMNS = TILEMAP_COLUMNS,  /* tiles across the screen, which a scroll looks ahead by ... */
    SCREEN_ROWS = 28,                  /* ... and down it */
};

/* A tile group: the four background tiles of one map tile, as words. */
enum { GROUP_BYTES = 8 };
typedef enum { TOP_LEFT = 0, TOP_RIGHT = 2, BOTTOM_LEFT = 4, BOTTOM_RIGHT = 6 } Corner;

/* The maps, the tile groups and the strips are in bank $7E; an address past
 * its end runs on into $7F, as the original's indexed reads do. */
enum { MAP_BANK = 0x7E0000 };

/* A plane (0-2) and where its pieces are. */
typedef struct {
    uint8_t plane;
    uint16_t groups, map, tilemap;
} MapPlane;

static MapPlane plane_of(const GameState *g, uint8_t plane)
{
    return (MapPlane){ plane, menu_plane_groups(g, plane), menu_plane_map(g, plane), menu_plane_tilemap(g, plane) };
}

/* ---- where a plane is ---------------------------------------------------------------------- */

/* The plane's top-left background tile: the third plane's column is the
 * first plane's row. */
static uint16_t tile_column(const GameState *g, uint8_t plane)
{
    return plane < MENU_MAP_COLUMN_COUNT ? menu_map_column(g, plane) : menu_map_row(g, 0);
}
static void set_tile_column(GameState *g, uint8_t plane, uint16_t v) { set_menu_map_column(g, plane, v); }
static uint16_t tile_row(const GameState *g, uint8_t plane) { return menu_map_row(g, plane); }
static void set_tile_row(GameState *g, uint8_t plane, uint16_t v) { set_menu_map_row(g, plane, v); }

static uint16_t tile_at(const GameState *g, uint8_t plane, MenuAxis axis)
{
    return axis == MENU_ACROSS ? tile_column(g, plane) : tile_row(g, plane);
}
static void set_tile_at(GameState *g, uint8_t plane, MenuAxis axis, uint16_t v)
{
    if (axis == MENU_ACROSS)
        set_tile_column(g, plane, v);
    else
        set_tile_row(g, plane, v);
}

/* The plane's scroll, which the vblank handler sends to its background. */
static uint16_t scroll_of(const GameState *g, uint8_t plane, MenuAxis axis)
{
    static uint16_t (*const across[3])(const GameState *) = {
        menu_object_shadows_bg1_scroll_x, menu_object_shadows_bg2_scroll_x, menu_object_shadows_bg3_scroll_x };
    static uint16_t (*const down[3])(const GameState *) = {
        menu_object_shadows_bg1_scroll_y, menu_object_shadows_bg2_scroll_y, menu_object_shadows_bg3_scroll_y };
    return (axis == MENU_ACROSS ? across : down)[plane](g);
}
static void set_scroll(GameState *g, uint8_t plane, MenuAxis axis, uint16_t v)
{
    static void (*const across[3])(GameState *, uint16_t) = {
        set_menu_object_shadows_bg1_scroll_x, set_menu_object_shadows_bg2_scroll_x,
        set_menu_object_shadows_bg3_scroll_x };
    static void (*const down[3])(GameState *, uint16_t) = {
        set_menu_object_shadows_bg1_scroll_y, set_menu_object_shadows_bg2_scroll_y,
        set_menu_object_shadows_bg3_scroll_y };
    (axis == MENU_ACROSS ? across : down)[plane](g, v);
}

/* A step back or on round the map. A scroll that goes below zero wraps round
 * to the far edge. */
static uint16_t back_one(uint16_t at, uint16_t round)
{
    return (int16_t)(at - 1) < 0 ? (uint16_t)(round - 1) : (uint16_t)(at - 1);
}
static uint16_t on_one(uint16_t at, uint16_t round) { return (uint16_t)(at + 1) < round ? (uint16_t)(at + 1) : 0; }

static bool on_tile_edge(uint16_t pixel) { return (pixel & (TILE_PIXELS - 1)) == 0; }

/* Which of a map tile's two halves a tile position or a pixel is on. */
static bool second_half(uint16_t pixel) { return (pixel & TILE_PIXELS) != 0; }

/* The word in the tilemap a background tile column starts at: the second 32
 * columns are the second screen's. */
static uint16_t tilemap_column(uint16_t column)
{
    return column < TILEMAP_COLUMNS ? column : (uint16_t)(column - TILEMAP_COLUMNS + TILEMAP_SCREEN);
}

/* ---- the map's bytes and the tile groups ---------------------------------------------------- */

static uint8_t map_byte(const GameState *g, uint16_t map, uint16_t at) { return wram_read8(g, MAP_BANK + map + at); }

static uint16_t bank_word(const GameState *g, uint32_t at)
{
    return (uint16_t)(wram_read8(g, at) | wram_read8(g, at + 1) << 8);
}

/* One of the four tiles of the group a map byte picks. */
static uint16_t group_tile(const GameState *g, const MapPlane *p, uint8_t byte, Corner corner)
{
    return bank_word(g, MAP_BANK + corner + (uint16_t)(byte * GROUP_BYTES + p->groups));
}

/* A word of the strip being built at `buffer`, `out` bytes in. */
static void put_strip_word(GameState *g, uint16_t buffer, uint16_t out, uint16_t word)
{
    uint32_t at = MAP_BANK + buffer + out;
    wram_write8(g, at, (uint8_t)word);
    wram_write8(g, at + 1, (uint8_t)(word >> 8));
}

/* Where a map row starts in the map. */
static uint16_t row_start(uint16_t row) { return (uint16_t)(row * MAP_COLUMNS); }

/* ---- building a strip ---------------------------------------------------------------------- */

/* A strip walks map tiles down a column or along a row, taking each map tile's
 * two tiles in the strip's direction from one side of its group, and fills
 * the tilemap's column (30 tiles, the screen's height and a tile either side)
 * or row (34, two screens' worth of words). */
typedef struct {
    uint16_t words;            /* tiles in the strip */
    uint16_t step, round;      /* how far along the map the next map tile is, and where that wraps */
    uint16_t out_round;        /* the strip's bytes, round which it is filled */
    Corner first, second;      /* the group's tiles for a map tile's two halves, on the left or top side */
    uint8_t other_side;        /* how far from those the right or bottom side's are */
} StripShape;

static const StripShape COLUMN_STRIP = { 30, MAP_COLUMNS, MAP_BYTES, STRIP_BYTES, TOP_LEFT, BOTTOM_LEFT,
                                         TOP_RIGHT - TOP_LEFT };
static const StripShape ROW_STRIP = { 34, 1, MAP_COLUMNS, 2 * STRIP_BYTES, TOP_LEFT, TOP_RIGHT,
                                      BOTTOM_LEFT - TOP_LEFT };

/* Where the last strip a scroll built ended up, for the original's cells
 * (leave_scroll_cells): scaffolding, noted through ORIGINAL_EFFECT alone. */
static struct {
    bool any;
    uint16_t map, at, buffer, out, vram;
} last_strip;

static void note_strip(uint16_t map, uint16_t at, uint16_t buffer, uint16_t out)
{
    last_strip.any = true;
    last_strip.map = map;
    last_strip.at = at;
    last_strip.buffer = buffer;
    last_strip.out = out;
}

// $C2:0773, $C2:07C2, $C2:0843, $C2:088F
/* Build a strip from map tile `at` of the map at `map`, starting on a map
 * tile's second half where `from_second`, with the first word `out` bytes
 * into the strip (where the top or left of the screen falls in the tilemap),
 * in the upload buffer. */
static void build_strip(GameState *g, const MapPlane *p, const StripShape *shape, uint16_t map, uint16_t at,
                        bool from_second, uint16_t out, bool far_side)
{
    uint16_t buffer = (uint16_t)menu_object_upload_buffer(g);
    uint8_t side = far_side ? shape->other_side : 0;
    bool second = from_second;
    for (uint16_t i = 0; i < shape->words; i++) {
        uint8_t byte = map_byte(g, map, at);
        Corner corner = (Corner)((second ? shape->second : shape->first) + side);
        put_strip_word(g, buffer, out, group_tile(g, p, byte, corner));
        if (second) {
            at = (uint16_t)(at + shape->step);
            if (at >= shape->round)
                at = 0;
        }
        out = (uint16_t)(out + 2) & (shape->out_round - 1);
        second = !second;
    }
    ORIGINAL_EFFECT(note_strip(map, at, buffer, out));
}

// $C2:0811
/* The strip for a tile column that has come into view: down the map from the
 * map row above the screen's top, filled from where the top falls in the
 * tilemap; `right` takes the right-hand tiles of each group. */
static void build_column_strip(GameState *g, const MapPlane *p, uint16_t column, bool right)
{
    uint16_t row = tile_row(g, p->plane);
    uint16_t top = (uint16_t)(scroll_of(g, p->plane, MENU_DOWN) - TILE_PIXELS);
    uint16_t out = (uint16_t)((top & (TILEMAP_HEIGHT - 1) & ~(TILE_PIXELS - 1)) >> 2);
    build_strip(g, p, &COLUMN_STRIP, (uint16_t)(p->map + (column >> 1)), row_start(back_one(row, TILE_ROWS) >> 1),
                (row & 1) == 0, out, right);
}

// $C2:08DB
/* The strip for a tile row: along the map from the map column left of the
 * screen, filled from where the screen's left edge falls; `bottom` takes the
 * bottom tiles of each group. */
static void build_row_strip(GameState *g, const MapPlane *p, uint16_t row, bool bottom)
{
    uint16_t column = tile_column(g, p->plane);
    uint16_t left = (uint16_t)(scroll_of(g, p->plane, MENU_ACROSS) - TILE_PIXELS);
    uint16_t out = (uint16_t)((left & (TILEMAP_WIDTH - 1) & ~(TILE_PIXELS - 1)) >> 2);
    build_strip(g, p, &ROW_STRIP, (uint16_t)(row_start(row >> 1) + p->map), back_one(column, TILE_COLUMNS) >> 1,
                (column & 1) == 0, out, bottom);
}

/* ---- queueing a strip ---------------------------------------------------------------------- */

/* Strips are taken from the upload buffer one after another; the vblank
 * handler puts it back once the queue has gone. */
static void take_buffer(GameState *g, uint16_t bytes)
{
    uint32_t buffer = menu_object_upload_buffer(g);
    set_menu_object_upload_buffer(g, (buffer & 0xFF0000u) | (uint16_t)(buffer + bytes));
}

/* The records a strip takes in the object queue: a column strip one, a row
 * strip one for each of the tilemap's screens. */
enum { COLUMN_STRIP_RECORDS = 1, ROW_STRIP_RECORDS = 2 };

// $C2:090D
/* Queue a column strip for the tilemap column at `pixel` across the plane,
 * the VRAM port stepping a tilemap row at a time. */
static void queue_column_strip(GameState *g, const MapPlane *p, uint16_t pixel)
{
    uint16_t column = (uint16_t)(pixel / TILE_PIXELS) & (2 * TILEMAP_COLUMNS - 1);
    uint16_t vram = (uint16_t)(tilemap_column(column) + p->tilemap);
    const MenuObjectUpload strip = { MAP_BANK | (uint16_t)menu_object_upload_buffer(g), vram, STRIP_BYTES,
                                     MENU_PORT_ROW_BY_ROW };
    (void)menu_queue_object_uploads(g, &strip, COLUMN_STRIP_RECORDS);
    take_buffer(g, STRIP_BYTES);
    ORIGINAL_EFFECT(last_strip.vram = vram);
}

// $C2:0961
/* Queue a row strip for the tilemap row at `pixel` down the plane: its first
 * half to the left screen and its second to the right, a word at a time. */
static void queue_row_strip(GameState *g, const MapPlane *p, uint16_t pixel)
{
    uint16_t row = (uint16_t)((pixel & (TILEMAP_HEIGHT - 1) & ~(TILE_PIXELS - 1)) << 2);
    uint16_t vram = (uint16_t)(row + p->tilemap);
    uint16_t buffer = (uint16_t)menu_object_upload_buffer(g);
    const MenuObjectUpload halves[2] = {
        { MAP_BANK | buffer, vram, STRIP_BYTES, MENU_PORT_WORD_BY_WORD },
        { MAP_BANK | (uint16_t)(buffer + STRIP_BYTES), (uint16_t)(vram + TILEMAP_SCREEN), STRIP_BYTES,
          MENU_PORT_WORD_BY_WORD },
    };
    (void)menu_queue_object_uploads(g, halves, ROW_STRIP_RECORDS);
    take_buffer(g, 2 * STRIP_BYTES);
    ORIGINAL_EFFECT(last_strip.vram = vram);
}

/* ---- scrolling ----------------------------------------------------------------------------- */

/* How a plane scrolls along each axis: how many tiles and pixels round the
 * map, and how far ahead of the top-left tile the screen's far edge is. */
typedef struct {
    uint16_t tiles, pixels, screen_tiles;
} AxisShape;
static const AxisShape AXES[2] = {
    [MENU_ACROSS] = { TILE_COLUMNS, TILE_COLUMNS * TILE_PIXELS, SCREEN_COLUMNS },
    [MENU_DOWN] = { TILE_ROWS, TILE_ROWS * TILE_PIXELS, SCREEN_ROWS },
};

// $C2:059D, $C2:0607, $C2:06A1, $C2:070B
/* Move the plane one pixel back (left or up) or on, carrying its top-left
 * tile with it; where a tile's worth of the map comes into view at the edge
 * it moved towards, build and queue the strip that fills it. */
static void scroll_a_pixel(GameState *g, const MapPlane *p, MenuAxis axis, bool back)
{
    const AxisShape *a = &AXES[axis];
    uint16_t tile = tile_at(g, p->plane, axis);
    uint16_t scroll = scroll_of(g, p->plane, axis);
    uint16_t uncovered, edge;
    bool far_half;
    if (back) {
        if (on_tile_edge(scroll))
            set_tile_at(g, p->plane, axis, tile = back_one(tile, a->tiles));
        set_scroll(g, p->plane, axis, scroll = back_one(scroll, a->pixels));
        if (!on_tile_edge(scroll))
            return;
        uncovered = back_one(tile, a->tiles);
        far_half = !second_half(scroll);
        edge = (uint16_t)(scroll - TILE_PIXELS);
    } else {
        set_scroll(g, p->plane, axis, scroll = on_one(scroll, a->pixels));
        if (!on_tile_edge(scroll))
            return;
        set_tile_at(g, p->plane, axis, tile = on_one(tile, a->tiles));
        uncovered = (uint16_t)(tile + a->screen_tiles);
        if (uncovered >= a->tiles)
            uncovered = (uint16_t)(uncovered - a->tiles);
        far_half = second_half(scroll);
        edge = (uint16_t)(scroll + a->screen_tiles * TILE_PIXELS);
    }
    if (axis == MENU_ACROSS) {
        build_column_strip(g, p, uncovered, far_half);
        queue_column_strip(g, p, edge);
    } else {
        build_row_strip(g, p, uncovered, far_half);
        queue_row_strip(g, p, edge);
    }
}

/* What the original leaves in its direct-page cells: the plane and its
 * tables, and the last strip's working values. */
static void leave_scroll_cells(GameState *g, const MapPlane *p, MenuAxis axis, int8_t step)
{
    set_menu_object_scratch_map_plane_index(g, (uint16_t)(p->plane << 1));
    set_menu_object_scratch_map_groups(g, p->groups);
    set_menu_object_scratch_map_data(g, p->map);
    set_menu_object_scratch_map_tilemap(g, p->tilemap);
    if (step != 0)
        set_menu_object_scratch_map_tiles_left(g, 0);
    if (!last_strip.any)
        return;
    set_menu_object_scratch_map_strip_map(g, last_strip.map);
    set_menu_object_scratch_map_strip_at(g, last_strip.at);
    set_menu_object_scratch_map_strip_buffer(g, last_strip.buffer);
    set_menu_object_scratch_map_strip_vram(g, last_strip.vram);
    set_menu_object_scratch_map_strip_end(g, axis == MENU_ACROSS ? last_strip.out
                                                                : (uint16_t)(last_strip.vram + TILEMAP_SCREEN));
    set_menu_object_scratch_map_strip_tiles_left(g, 0);
}

// $C2:0568, $C2:066C
/* Scroll a plane by `step` pixels (negative back), filling in the map as it
 * comes into view. */
static void scroll_plane(GameState *g, uint8_t plane, MenuAxis axis, int8_t step)
{
    MapPlane p = plane_of(g, plane);
    ORIGINAL_EFFECT(last_strip.any = false);
    unsigned pixels = step < 0 ? (unsigned)-step : (unsigned)step;
    for (unsigned i = 0; i < pixels; i++)
        scroll_a_pixel(g, &p, axis, step < 0);
    ORIGINAL_EFFECT(leave_scroll_cells(g, &p, axis, step));
}

void menu_map_scroll(GameState *g, uint8_t plane, MenuAxis axis, int8_t step)
{
    scroll_plane(g, (uint8_t)(plane - 1), axis, step);
}

/* The camera: the map's top-left tile (the second plane's, which the first
 * shares) is the game's own record of what the screen looks at, kept in the
 * original's convention whatever the screen's size. Centring puts it sixteen
 * tiles left of the party's tile and fourteen above (half the original's
 * 256x224 screen); what the camera looks at, as the original works it out to
 * keep where the party stood, is sixteen tiles on from it both ways (half the
 * original's width, the height's too). A screen wider or taller than the
 * original's draws round the same camera rather than moving it, so a place
 * kept or restored doesn't depend on the screen it was played on. */
enum { CENTRING_COLUMNS = 16, CENTRING_ROWS = 14, LOOKS_AT_TILES = 16 };

// $C2:2720
/* Put the camera where the party is, round the map. The two planes that
 * scroll get the same tile. */
static void centre_on_party(GameState *g)
{
    int column = location_x(g) - CENTRING_COLUMNS;
    int row = location_y(g) - CENTRING_ROWS;
    if (column < 0)
        column += TILE_COLUMNS;
    if (row < 0)
        row += TILE_ROWS;
    for (uint8_t plane = 0; plane < 2; plane++) {
        set_tile_column(g, plane, (uint16_t)column);
        set_tile_row(g, plane, (uint16_t)row);
    }
}

MenuMapTile menu_map_camera_looks_at(const GameState *g)
{
    enum { CAMERA_PLANE = 1 };
    uint16_t column = (uint16_t)(tile_column(g, CAMERA_PLANE) + LOOKS_AT_TILES);
    if (column >= TILE_COLUMNS)
        column = (uint16_t)(column - TILE_COLUMNS);
    return (MenuMapTile){ (uint8_t)column, (uint8_t)((tile_row(g, CAMERA_PLANE) + LOOKS_AT_TILES) & (TILE_ROWS - 1)) };
}

/* ---- redrawing a whole plane --------------------------------------------------------------- */

/* A redraw works in blocks: sixteen map tiles down one map column, which
 * fill two tilemap columns from the top of the screen round its 32 rows. */
enum { REDRAW_BLOCKS = 18, BLOCK_TILES = 16, BLOCK_PIXELS = 2 * TILE_PIXELS };

// $C2:0A6C, $C2:0AAE
/* Build a block in the upload buffer: each map tile's left-hand pair in the
 * block's first column, its right-hand pair a strip further on in the second.
 * Returns how far into the map it got. */
static uint16_t build_block(GameState *g, const MapPlane *p, uint16_t map, uint16_t at, uint16_t out)
{
    uint16_t buffer = (uint16_t)menu_object_upload_buffer(g);
    for (unsigned i = 0; i < BLOCK_TILES; i++) {
        uint8_t byte = map_byte(g, map, at);
        put_strip_word(g, buffer, out, group_tile(g, p, byte, TOP_LEFT));
        put_strip_word(g, buffer, (uint16_t)(out + 2), group_tile(g, p, byte, BOTTOM_LEFT));
        put_strip_word(g, buffer, (uint16_t)(out + STRIP_BYTES), group_tile(g, p, byte, TOP_RIGHT));
        put_strip_word(g, buffer, (uint16_t)(out + STRIP_BYTES + 2), group_tile(g, p, byte, BOTTOM_RIGHT));
        at = (uint16_t)(at + MAP_COLUMNS);
        if (at >= MAP_BYTES)
            at = 0;
        out = (uint16_t)(out + 4) & (STRIP_BYTES - 1);
    }
    return at;
}

// $C2:0ADC
/* Send a block's two tilemap columns from the upload buffer to the plane's
 * tilemap at the block's place across it, the VRAM port stepping a tilemap
 * row at a time. */
static uint16_t block_vram(uint16_t tilemap, uint16_t x) { return (uint16_t)(tilemap_column(x / TILE_PIXELS) + tilemap); }

static void upload_block(GameState *g, Platform *pf, uint16_t vram)
{
    static const VidVramPort column_port = { .step = 32, .after_high = true };
    uint32_t buffer = menu_object_upload_buffer(g);
    vid_send_vram(pf, PF_AT(0xC20AF6), vram, PF_GAME(buffer), STRIP_BYTES, &column_port);
    vid_send_vram(pf, PF_AT(0xC20B22), (uint16_t)(vram + 1),
                  PF_GAME((buffer & 0xFF0000u) | (uint16_t)(buffer + STRIP_BYTES)), STRIP_BYTES, &column_port);
}

/* Where the redraw got to, for the original's cells. */
typedef struct {
    uint16_t column, row, x, y, at, out, vram, map;
} Redraw;

/* The original multiplies a row by 96 as a quarter of the row's word swapped
 * (the row times 64) and a half of that, and leaves the quarter. */
enum { ROW_QUARTER = MAP_COLUMNS * 2 / 3 };

static void leave_redraw_cells(GameState *g, const MapPlane *p, const Redraw *r)
{
    set_menu_object_scratch_redraw_tiles_left(g, 0);
    set_menu_object_scratch_redraw_blocks_left(g, 0);
    set_menu_object_scratch_redraw_at(g, r->at);
    set_menu_object_scratch_redraw_out(g, r->out);
    set_menu_object_scratch_redraw_column(g, r->column);
    set_menu_object_scratch_redraw_row(g, r->row);
    set_menu_object_scratch_redraw_x(g, r->x);
    set_menu_object_scratch_redraw_y(g, r->y);
    set_menu_object_scratch_redraw_tilemap(g, p->tilemap);
    set_menu_object_scratch_redraw_vram(g, r->vram);
    set_menu_object_scratch_redraw_groups(g, p->groups);
    set_menu_object_scratch_redraw_data(g, p->map);
    set_menu_object_scratch_redraw_map(g, r->map);
    set_menu_object_scratch_redraw_row_quarter(g, (uint16_t)(r->row * ROW_QUARTER));
}

// $C2:09C5
/* Redraw a whole plane: put its scroll where its top-left tile says, then
 * walk eighteen blocks across it from the map tile the screen's left edge
 * falls in (one back where the edge sits on a map tile's left half), each
 * built in the upload buffer and sent straight to VRAM. */
static void redraw_plane(GameState *g, Platform *pf, uint8_t plane)
{
    MapPlane p = plane_of(g, plane);
    uint16_t column = tile_column(g, plane), row = tile_row(g, plane);
    uint16_t scroll_x = (uint16_t)(column * TILE_PIXELS), scroll_y = (uint16_t)(row * TILE_PIXELS);
    set_scroll(g, plane, MENU_ACROSS, scroll_x);
    set_scroll(g, plane, MENU_DOWN, scroll_y);
    Redraw r = { 0 };
    r.column = (column & 1) ? column >> 1 : back_one(column >> 1, MAP_COLUMNS);
    r.row = (row & 1) ? row >> 1 : back_one(row >> 1, MAP_ROWS);
    r.x = (uint16_t)(scroll_x - (second_half(scroll_x) ? TILE_PIXELS : BLOCK_PIXELS)) & (TILEMAP_WIDTH - 1);
    r.y = (uint16_t)(scroll_y - (second_half(scroll_y) ? TILE_PIXELS : BLOCK_PIXELS)) & (TILEMAP_HEIGHT - 1);
    r.out = (uint16_t)((r.y & ~(BLOCK_PIXELS - 1)) >> 2);
    for (unsigned block = 0; block < REDRAW_BLOCKS; block++) {
        r.map = (uint16_t)(r.column + p.map);
        r.at = build_block(g, &p, r.map, row_start(r.row), r.out);
        r.vram = block_vram(p.tilemap, r.x);
        TIMED_AS(g, 0xC20ADCu, upload_block(g, pf, r.vram));
        r.column = on_one(r.column, MAP_COLUMNS);
        r.x = (uint16_t)(r.x + BLOCK_PIXELS) & (TILEMAP_WIDTH - 1);
    }
    ORIGINAL_EFFECT(leave_redraw_cells(g, &p, &r));
}

/* ---- the entries ----------------------------------------------------------------------------- */

/* The mode's direct page, where its cells are. */
enum { MODE_PAGE = 0x0000 };

/* They run on the mode's page, reaching their cells through a data bank on
 * low RAM. A script names a plane 1-3 in the plane cell; the scrolls take the
 * first two. The game keeps each plane's top-left tile on the map, its upload
 * buffer in work RAM above low RAM, and its queue on a record with room for
 * what a scroll queues (a strip for every tile it crosses, and one more). */
static bool on_the_page(const Snes *s) { return m_direct_page(s) == MODE_PAGE && m_bank_reaches_low_ram(s); }

static uint8_t named_plane(const GameState *g) { return (uint8_t)(menu_object_scratch_plane(g) & 3); }

static bool plane_is_on_the_map(const GameState *g, uint8_t plane)
{
    return tile_column(g, plane) < TILE_COLUMNS && tile_row(g, plane) < TILE_ROWS;
}

static bool buffer_is_in_work_ram(const GameState *g)
{
    enum { LOW_RAM_END = 0x2000 };
    uint32_t buffer = menu_object_upload_buffer(g);
    return (buffer >> 16) == (MAP_BANK >> 16) && (uint16_t)buffer >= LOW_RAM_END;
}

unsigned menu_map_scroll_records(MenuAxis axis, int8_t step)
{
    unsigned pixels = step < 0 ? (unsigned)-step : (unsigned)step;
    return (pixels / TILE_PIXELS + 1) * (axis == MENU_ACROSS ? COLUMN_STRIP_RECORDS : ROW_STRIP_RECORDS);
}

bool menu_map_can_scroll(const GameState *g, uint8_t plane, unsigned records)
{
    enum { QUEUE_BYTES = MENU_OBJECT_VRAM_QUEUE_COUNT * MENU_OBJECT_UPLOAD_BYTES };
    return plane >= 1 && plane <= 2 && plane_is_on_the_map(g, (uint8_t)(plane - 1)) && buffer_is_in_work_ram(g)
           && menu_object_queue_is_whole(g)
           && menu_object_vram_queue_end(g) + records * MENU_OBJECT_UPLOAD_BYTES <= QUEUE_BYTES;
}

static bool scroll_runs(Snes *s, MenuAxis axis)
{
    if (!on_the_page(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = menu_map_can_scroll(g, named_plane(g),
                                  menu_map_scroll_records(axis, (int8_t)menu_object_scratch_step(g)));
    vars_peek(false);
    return ok;
}

bool menu_map_scroll_x_runs(Snes *s) { return scroll_runs(s, MENU_ACROSS); }
bool menu_map_scroll_y_runs(Snes *s) { return scroll_runs(s, MENU_DOWN); }

bool menu_map_redraw_runs(Snes *s)
{
    if (!on_the_page(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    uint8_t plane = named_plane(g);
    bool ok = plane != 0 && plane_is_on_the_map(g, (uint8_t)(plane - 1)) && buffer_is_in_work_ram(g);
    vars_peek(false);
    return ok;
}

bool menu_map_page_is_menu(Snes *s) { return m_direct_page(s) == MODE_PAGE; }

/* The scrolls come back with an 8-bit accumulator, and X the last strip's
 * place in the queue where the last pixel brought one into view (it is then
 * on a tile's edge), or else the plane doubled. */
static void scroll_entry(Snes *s, MenuAxis axis)
{
    GameState *g = state_of(s);
    uint8_t plane = (uint8_t)(named_plane(g) - 1);
    int8_t step = (int8_t)menu_object_scratch_step(g);
    scroll_plane(g, plane, axis, step);
    vars_peek(true);
    bool strip_last = step != 0 && on_tile_edge(scroll_of(g, plane, axis));
    unsigned records = axis == MENU_ACROSS ? COLUMN_STRIP_RECORDS : ROW_STRIP_RECORDS;
    uint8_t last_queued = (uint8_t)(menu_object_vram_queue_end(g) - records * MENU_OBJECT_UPLOAD_BYTES);
    vars_peek(false);
    s->cpu.x = strip_last ? last_queued : (uint16_t)(plane << 1);
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

// $C2:0568
void menu_map_scroll_x(Snes *s) { scroll_entry(s, MENU_ACROSS); }

// $C2:066C
void menu_map_scroll_y(Snes *s) { scroll_entry(s, MENU_DOWN); }

// $C2:2720
void menu_map_centre_on_party(Snes *s)
{
    centre_on_party(state_of(s));
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

// $C2:09C5
/* It comes back with the carry clear and X the block's size, as the block
 * upload leaves it. */
void menu_map_redraw(Snes *s)
{
    GameState *g = state_of(s);
    redraw_plane(g, m_platform(s), (uint8_t)(named_plane(g) - 1));
    s->cpu.x = STRIP_BYTES;
    m_sep(s, CPU_FLAG_M);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

// $C2:0ADC
/* Entered from a redraw the original runs: the block's place and the plane's
 * tilemap are in the redraw's cells. */
void menu_map_upload_block(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t vram = block_vram(menu_object_scratch_redraw_tilemap(g), menu_object_scratch_redraw_x(g));
    set_menu_object_scratch_redraw_vram(g, vram);
    upload_block(g, m_platform(s), vram);
    s->cpu.x = STRIP_BYTES;
    m_rts(s);
}
