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
#include "menu/effect_noise.h"
#include "menu/map.h"

#include "charge.h"
#include "menu/menu_data.h"
#include "menu/object.h"
#include "menu/vram.h"
#include "overworld/setup.h"
#include "platform/video.h"
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
    (void)0;
}

// $C2:0773, $C2:07C2 (with $C2:0811)
/* The strip for a map column that has come into view: down it from map row
 * `row` (the one above the screen's top), filled from where the top falls in
 * the tilemap; `right` takes the right-hand tiles of each group. */
static void build_column_strip(GameState *g, const MapPlane *p, uint16_t column, uint16_t row, bool right)
{
    uint16_t top_tile = tile_row(g, p->plane);
    uint16_t top = (uint16_t)(scroll_of(g, p->plane, MENU_DOWN) - TILE_PIXELS);
    uint16_t out = (uint16_t)((top & (TILEMAP_HEIGHT - 1) & ~(TILE_PIXELS - 1)) >> 2);
    build_strip(g, p, &COLUMN_STRIP, (uint16_t)(p->map + column), row_start(row), (top_tile & 1) == 0, out, right);
}

// $C2:0843, $C2:088F (with $C2:08DB)
/* The strip for a map row: along it from map column `column` (the one left
 * of the screen), filled from where the screen's left edge falls; `bottom`
 * takes the bottom tiles of each group. */
static void build_row_strip(GameState *g, const MapPlane *p, uint16_t row, uint16_t column, bool bottom)
{
    uint16_t left_tile = tile_column(g, p->plane);
    uint16_t left = (uint16_t)(scroll_of(g, p->plane, MENU_ACROSS) - TILE_PIXELS);
    uint16_t out = (uint16_t)((left & (TILEMAP_WIDTH - 1) & ~(TILE_PIXELS - 1)) >> 2);
    build_strip(g, p, &ROW_STRIP, (uint16_t)(row_start(row) + p->map), column, (left_tile & 1) == 0, out, bottom);
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

/* Queue a column strip for the tilemap column at `pixel` across the plane,
 * the VRAM port stepping a tilemap row at a time. */
static uint16_t queue_column_strip(GameState *g, const MapPlane *p, uint16_t pixel)
{
    uint16_t column = (uint16_t)(pixel / TILE_PIXELS) & (2 * TILEMAP_COLUMNS - 1);
    uint16_t vram = (uint16_t)(tilemap_column(column) + p->tilemap);
    const MenuObjectUpload strip = { MAP_BANK | (uint16_t)menu_object_upload_buffer(g), vram, STRIP_BYTES,
                                     MENU_PORT_ROW_BY_ROW };
    (void)menu_queue_object_uploads(g, &strip, COLUMN_STRIP_RECORDS);
    take_buffer(g, STRIP_BYTES);
    (void)0;
    return vram;
}

/* Queue a row strip for the tilemap row at `pixel` down the plane: its first
 * half to the left screen and its second to the right, a word at a time. */
static uint16_t queue_row_strip(GameState *g, const MapPlane *p, uint16_t pixel)
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
    (void)0;
    return vram;
}

/* ---- scrolling ----------------------------------------------------------------------------- */

/* The original's routines that build a strip (from the groups' left or right
 * tiles, or their top or bottom ones) and queue it: a strip's work is
 * estimated at their rates, a pixel's move at the scroll's. */
enum {
    BUILD_COLUMN_STRIP = 0xC20773,
    BUILD_COLUMN_STRIP_RIGHT = 0xC207C2,
    BUILD_ROW_STRIP = 0xC20843,
    BUILD_ROW_STRIP_BOTTOM = 0xC2088F,
    QUEUE_COLUMN_STRIP = 0xC2090D,
    QUEUE_ROW_STRIP = 0xC20961,
};

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
static bool scroll_a_pixel(GameState *g, const MapPlane *p, MenuAxis axis, bool back, uint16_t *last_vram)
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
            return false;
        uncovered = back_one(tile, a->tiles);
        far_half = !second_half(scroll);
        edge = (uint16_t)(scroll - TILE_PIXELS);
    } else {
        set_scroll(g, p->plane, axis, scroll = on_one(scroll, a->pixels));
        if (!on_tile_edge(scroll))
            return false;
        set_tile_at(g, p->plane, axis, tile = on_one(tile, a->tiles));
        uncovered = (uint16_t)(tile + a->screen_tiles);
        if (uncovered >= a->tiles)
            uncovered = (uint16_t)(uncovered - a->tiles);
        far_half = second_half(scroll);
        edge = (uint16_t)(scroll + a->screen_tiles * TILE_PIXELS);
    }
    /* A strip's work is timed as the original's strip routines', strip by
     * strip, and the pixel's move as the scroll's own. */
    if (axis == MENU_ACROSS) {
        uint16_t above = back_one(tile_row(g, p->plane), TILE_ROWS) >> 1;
        TIMED_AS(g, far_half ? BUILD_COLUMN_STRIP_RIGHT : BUILD_COLUMN_STRIP,
                 build_column_strip(g, p, uncovered >> 1, above, far_half));
        TIMED_AS(g, QUEUE_COLUMN_STRIP, *last_vram = queue_column_strip(g, p, edge));
    } else {
        uint16_t left_of = back_one(tile_column(g, p->plane), TILE_COLUMNS) >> 1;
        TIMED_AS(g, far_half ? BUILD_ROW_STRIP_BOTTOM : BUILD_ROW_STRIP,
                 build_row_strip(g, p, uncovered >> 1, left_of, far_half));
        TIMED_AS(g, QUEUE_ROW_STRIP, *last_vram = queue_row_strip(g, p, edge));
    }
    return true;
}

// $C2:0568, $C2:066C
/* Scroll a plane by `step` pixels (negative back), filling in the map as it
 * comes into view. */
static void scroll_plane(GameState *g, uint8_t plane, MenuAxis axis, int8_t step)
{
    MapPlane p = plane_of(g, plane);
    bool built_strip = false;
    uint16_t last_vram = 0;
    unsigned pixels = step < 0 ? (unsigned)-step : (unsigned)step;
    for (unsigned i = 0; i < pixels; i++)
        built_strip = scroll_a_pixel(g, &p, axis, step < 0, &last_vram) || built_strip;
    if (built_strip)
        menu_note_effect_noise(g, last_vram);
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

/* The middle of the view, as the original works it out to play the song of
 * what the screen shows: the first plane's tile (its bytes', as it reads
 * them) and the centring's offsets on from it, not brought round the map. */
MenuViewCentre menu_map_view_centre(const GameState *g)
{
    enum { VIEW_PLANE = 0 };
    return (MenuViewCentre){ (uint16_t)((uint8_t)tile_column(g, VIEW_PLANE) + CENTRING_COLUMNS),
                             (uint16_t)((uint8_t)tile_row(g, VIEW_PLANE) + CENTRING_ROWS) };
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

// $C2:09C5
/* Redraw a whole plane: put its scroll where its top-left tile says, then
 * walk eighteen blocks across it from the map tile the screen's left edge
 * falls in (one back where the edge sits on a map tile's left half), each
 * built in the upload buffer and sent straight to VRAM. */
void menu_map_draw_plane(GameState *g, Platform *pf, uint8_t plane)
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
    menu_note_effect_noise(g, r.column);
}

/* ---- what a scroll and a redraw need ------------------------------------------------------- */

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

static bool queue_has_room(const GameState *g, unsigned records)
{
    enum { QUEUE_BYTES = MENU_OBJECT_VRAM_QUEUE_COUNT * MENU_OBJECT_UPLOAD_BYTES };
    return menu_object_queue_is_whole(g)
           && menu_object_vram_queue_end(g) + records * MENU_OBJECT_UPLOAD_BYTES <= QUEUE_BYTES;
}

bool menu_map_can_scroll(const GameState *g, uint8_t plane, unsigned records)
{
    return plane >= 1 && plane <= 2 && plane_is_on_the_map(g, (uint8_t)(plane - 1)) && buffer_is_in_work_ram(g)
           && queue_has_room(g, records);
}

// $C2:2720
void menu_map_centre_on_the_party(GameState *g) { centre_on_party(g); }

void menu_map_redraw_plane(GameState *g, Platform *pf, uint8_t plane)
{
    TIMED_AS(g, 0xC209C5, menu_map_draw_plane(g, pf, plane));
}

bool menu_map_can_redraw(const GameState *g, uint8_t plane)
{
    return plane < 2 && plane_is_on_the_map(g, plane) && buffer_is_in_work_ram(g);
}

bool menu_map_party_centres_on_the_map(const GameState *g)
{
    return location_x(g) < TILE_COLUMNS + CENTRING_COLUMNS && location_y(g) < TILE_ROWS + CENTRING_ROWS;
}

/* ---- a wider view's margins ------------------------------------------------------------------ */

/* On a view wider than the original's screen (vid_view), a world map's two
 * scrolling planes show tilemap columns either side of the screen's, which the
 * scrolls never fill: they hold whatever the ring held. So each frame the
 * columns the margins show are built from the map and sent, beside the game's
 * work and outside it -- into a line of the view's own, its reads left out of
 * the game's time, its sends made in the vblank -- so the game runs the same
 * at any view; the camera stays the original's. The tilemap is 64 columns
 * round: the screen's 33 and up to 15 more either side fit. */
enum { MOST_MARGIN_COLUMNS = (2 * TILEMAP_COLUMNS - SCREEN_COLUMNS - 1) / 2, MARGIN_ROWS_ABOVE = 2 };

/* The tilemap column `ring` filled with the plane's background tile column
 * `column`, its rows round from the top of the screen. */
static void send_a_margin_column(GameState *g, Platform *pf, const MapPlane *p, uint16_t column, uint16_t ring)
{
    uint16_t top = tile_row(g, p->plane), top_in_ring = (uint16_t)(scroll_of(g, p->plane, MENU_DOWN) / TILE_PIXELS);
    uint8_t line[STRIP_BYTES];
    for (uint16_t r = 0; r < TILEMAP_ROWS; r++) {
        int down = (int)((r - top_in_ring + MARGIN_ROWS_ABOVE) & (TILEMAP_ROWS - 1)) - MARGIN_ROWS_ABOVE;
        uint16_t row = (uint16_t)((top + TILE_ROWS + down) % TILE_ROWS);
        uint8_t byte = map_byte(g, p->map, (uint16_t)(row_start(row >> 1) + (column >> 1)));
        Corner corner = (Corner)(((row & 1) ? BOTTOM_LEFT : TOP_LEFT) + ((column & 1) ? TOP_RIGHT - TOP_LEFT : 0));
        uint16_t word = group_tile(g, p, byte, corner);
        line[2 * r] = (uint8_t)word;
        line[2 * r + 1] = (uint8_t)(word >> 8);
    }
    static const VidVramPort column_port = { .step = TILEMAP_COLUMNS, .after_high = true };
    vid_send_view_vram(pf, (uint16_t)(tilemap_column(ring) + p->tilemap), (PfData){ .ptr = line }, STRIP_BYTES,
                       &column_port);
}

void menu_map_fill_the_views_margins(GameState *g, Platform *pf)
{
    VidView view = vid_view(pf);
    if (!vid_view_is_wider(view))
        return;
    int margin = (-vid_view_left(view) + TILE_PIXELS - 1) / TILE_PIXELS + 1;
    if (margin > MOST_MARGIN_COLUMNS)
        margin = MOST_MARGIN_COLUMNS;
    uint32_t mark = charge_uncounted_begin(g);
    for (uint8_t plane = 0; plane < 2 && overworld_on_a_world_map(g); plane++) {
        if (!plane_is_on_the_map(g, plane))
            continue;
        MapPlane p = plane_of(g, plane);
        uint16_t left = tile_column(g, plane), in_ring = (uint16_t)(scroll_of(g, plane, MENU_ACROSS) / TILE_PIXELS);
        for (int k = 1; k <= margin; k++) {
            send_a_margin_column(g, pf, &p, (uint16_t)((left + TILE_COLUMNS - k) % TILE_COLUMNS),
                                 (uint16_t)((in_ring - k) & (2 * TILEMAP_COLUMNS - 1)));
            send_a_margin_column(g, pf, &p, (uint16_t)((left + SCREEN_COLUMNS + k) % TILE_COLUMNS),
                                 (uint16_t)((in_ring + SCREEN_COLUMNS + k) & (2 * TILEMAP_COLUMNS - 1)));
        }
    }
    charge_uncounted_end(g, mark);
}

/* The two planes hold the world map across the whole view, every frame its
 * margins are kept filled; anything else holds the original's screen alone. */
void menu_map_show_the_planes_across(GameState *g, Platform *pf)
{
    VidView view = vid_view(pf);
    if (!vid_view_is_wider(view))
        return;
    uint32_t mark = charge_uncounted_begin(g);
    for (uint8_t plane = 0; plane < 2 && overworld_on_a_world_map(g); plane++)
        if (plane_is_on_the_map(g, plane))
            vid_show_a_layer_across(pf, (VidLayer)(VID_BG1 + plane), vid_view_left(view), vid_view_right(view));
    charge_uncounted_end(g, mark);
}
