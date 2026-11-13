/* The map scrolling in (field/streaming.h): as the screen moves over a map
 * larger than itself, each layer's tile map in VRAM holds only the map around
 * the view, and what comes into view is built from the map as it does.
 *
 * A map tile is 16 pixels and each layer's tile map entries are 8: the map
 * gives each layer's tile a look (field_tile_looks, BG3's own), the four
 * entries of its quarters. A layer's line of entries along a row or down a
 * column is built from the map tiles it crosses, starting on a tile's second
 * half where the line starts on an odd 8-pixel tile, into the layer's staging
 * buffer, which the vblank's deferred uploads send (field/display.c).
 *
 * Every frame after the layers' scroll (field/layers.c), each layer takes its
 * move a pixel at a time: its scroll, its place in the 16-pixel tile, and,
 * each time it crosses an 8-pixel tile, the screen's edges in 8-pixel tiles
 * move with it, and the row or column it crossed onto is due. BG2 may scroll
 * at half the pace, a pixel for every two it moves, with edges of its own; a
 * layer drawn whole at the map's load has nothing due. Then the due rows and
 * columns are built, each layer's place in its tile map moves on, the uploads
 * are asked for, and the layers' edge runs are worked out afresh.
 *
 * The original writes the lines through the work RAM port, a byte at a time;
 * the port writes the staging buffers directly. */
#include "field/streaming.h"

#include "charge.h"
#include "core/program.h"
#include "cpu_io.h"
#include "field/bridge.h"
#include "field/field.h"
#include "field/layers.h"
#include "platform/video.h"
#include "vars.h"

/* ---- The map's looks ------------------------------------------------------------------------ */

/* A look's four entries, by the quarter of the map tile. */
typedef enum { TOP_LEFT, TOP_RIGHT, BOTTOM_LEFT, BOTTOM_RIGHT } Quarter;

static Quarter quarter(bool bottom, bool right) { return (Quarter)((bottom ? BOTTOM_LEFT : TOP_LEFT) + (right ? 1 : 0)); }

enum { ENTRY_BYTES = 2 };

static uint16_t look_entry(const GameState *g, unsigned layer, uint16_t look, Quarter q)
{
    bool bg3 = layer == FIELD_BG3;
    switch (q) {
    case TOP_LEFT: return bg3 ? field_bg3_tile_looks_top_left(g, look) : field_tile_looks_top_left(g, look);
    case TOP_RIGHT: return bg3 ? field_bg3_tile_looks_top_right(g, look) : field_tile_looks_top_right(g, look);
    case BOTTOM_LEFT: return bg3 ? field_bg3_tile_looks_bottom_left(g, look) : field_tile_looks_bottom_left(g, look);
    default: return bg3 ? field_bg3_tile_looks_bottom_right(g, look) : field_tile_looks_bottom_right(g, look);
    }
}

/* A layer's look for a map tile: BG1 and BG2 take theirs from one of two
 * halves of the looks, as the tile's properties say. */
enum { SECOND_HALF_OF_THE_LOOKS = 0x100 };

static uint16_t look_of(const GameState *g, unsigned layer, MapPlace p)
{
    uint16_t look = field_layer_maps(g, layer, p.row, p.column);
    uint8_t properties = field_tile_properties(g, p.row, p.column);
    if (layer == FIELD_BG1 && (properties & BG1_TILE_SET_HALF))
        look |= SECOND_HALF_OF_THE_LOOKS;
    if (layer == FIELD_BG2 && (properties & BG2_TILE_SET_HALF))
        look |= SECOND_HALF_OF_THE_LOOKS;
    return look;
}

/* Where each layer's map repeats: a mask for the row and one for the column. */
static uint16_t index_mask(const GameState *g, unsigned layer)
{
    return layer == FIELD_BG1 ? field_map_index_mask(g) : field_layer_index_masks(g, layer - 1);
}

/* ---- Building a line ------------------------------------------------------------------------ */

/* Where the lines go: the three layers' staging buffers one after another,
 * a line along a row from a buffer's start and one down a column from $80
 * into it (field/field.h); a layer drawn whole at a map's load runs on from
 * the first buffer's start through the next. */

typedef struct {
    uint16_t at;   /* from the first buffer's start */
    uint8_t *own;  /* or a line of a wider view's own (below), which isn't the game's memory */
} Staging;

static void stage_byte(GameState *g, uint16_t at, uint8_t v)
{
    set_field_layer_staging_byte(g, at / FIELD_LAYER_STAGING_BYTES, at % FIELD_LAYER_STAGING_BYTES, v);
}

static void stage(GameState *g, Staging *s, uint16_t entry)
{
    if (s->own) {
        s->own[s->at] = (uint8_t)entry;
        s->own[s->at + 1] = (uint8_t)(entry >> 8);
    } else {
        stage_byte(g, s->at, (uint8_t)entry);
        stage_byte(g, (uint16_t)(s->at + 1), (uint8_t)(entry >> 8));
    }
    s->at = (uint16_t)(s->at + ENTRY_BYTES);
}

/* A line of entries: along a row or down a column, from the 8-pixel tile
 * (column, row), across `tiles` map tiles. A line starting on an odd tile
 * starts with its first map tile's second half; the original's builders
 * differ in what that half counts for (below). */
typedef enum { ALONG_A_ROW, DOWN_A_COLUMN } LineWay;

typedef struct {
    LineWay way;
    uint8_t column, row;   /* the first entry's 8-pixel tile */
    uint8_t tiles;
} Line;

/* The half a line starting on an odd tile begins with: the rows of BG1 and
 * BG2 count it as one of the line's map tiles and end with a half of the map
 * tile after the last, so the line is as long either way -- the second half
 * of that tile, where the first would continue the line (the entry lies
 * past the screen's right edge, and the column streaming in there as the
 * screen moves right writes over it); BG3's rows and every column count it
 * on top, a half longer. */
static bool odd_start_counts(unsigned layer, LineWay way) { return way == ALONG_A_ROW && layer != FIELD_BG3; }

/* A place on the line: the map tile along it and across it, and which
 * halves of the tile the entries there are (a row takes the top or bottom
 * half's left or right quarter, a column the left or right half's top or
 * bottom one). */
typedef struct {
    uint8_t along, across;
    bool second_across;
} LinePoint;

static uint16_t look_at(const GameState *g, unsigned layer, LineWay way, LinePoint p)
{
    MapPlace place = way == ALONG_A_ROW ? (MapPlace){ p.across, p.along } : (MapPlace){ p.along, p.across };
    return look_of(g, layer, place);
}

static void stage_half(GameState *g, Staging *out, unsigned layer, LineWay way, LinePoint p, uint16_t look,
                       bool second_along)
{
    Quarter q = way == ALONG_A_ROW ? quarter(p.second_across, second_along) : quarter(second_along, p.second_across);
    stage(g, out, look_entry(g, layer, look, q));
}

// $C0:7612 (BG1's rows), $C0:77E4 (BG1's columns), $C0:79CF, $C0:7BA9 (BG2's), $C0:7D66, $C0:7E60 (BG3's)
/* The map tile along the line moves on after each, within the layer's
 * mask. */
static void build_line(GameState *g, unsigned layer, Line line, Staging *out)
{
    uint16_t mask = index_mask(g, layer);
    bool row = line.way == ALONG_A_ROW;
    uint8_t along_mask = row ? (uint8_t)mask : (uint8_t)(mask >> 8);
    uint8_t across_mask = row ? (uint8_t)(mask >> 8) : (uint8_t)mask;
    uint8_t along8 = row ? line.column : line.row, across8 = row ? line.row : line.column;
    LinePoint p = { (uint8_t)((along8 >> 1) & along_mask), (uint8_t)((across8 >> 1) & across_mask), (across8 & 1) != 0 };
    uint8_t tiles = line.tiles;
    bool odd = along8 & 1;
    if (odd) {
        stage_half(g, out, layer, line.way, p, look_at(g, layer, line.way, p), true);
        p.along = (uint8_t)((p.along + 1) & along_mask);
        if (odd_start_counts(layer, line.way))
            tiles--;
    }
    while (tiles > 0) {
        uint16_t look = look_at(g, layer, line.way, p);
        stage_half(g, out, layer, line.way, p, look, false);
        stage_half(g, out, layer, line.way, p, look, true);
        tiles--;
        p.along = (uint8_t)((p.along + 1) & along_mask);
    }
    if (odd && odd_start_counts(layer, line.way))   /* the second half again: see above */
        stage_half(g, out, layer, line.way, p, look_at(g, layer, line.way, p), true);
    /* the original's rows leave the map tile after the last, its columns the last */
}

/* ---- The layers' moves, a pixel at a time ------------------------------------------------ */

/* The screen's four sides, in the order of the layers' edges crossed and
 * their uploads: the row below the view, the row above it, the column right
 * of it and the column left of it. */
typedef enum { SIDE_BELOW, SIDE_ABOVE, SIDE_RIGHT, SIDE_LEFT, SIDES } Side;

/* The screen's edges in 8-pixel tiles: BG1's, which BG3 and BG2 go by too,
 * or BG2's own where it scrolls at half the pace. */
typedef struct {
    uint8_t left, right, top, bottom;
} ScreenEdges;

static bool keeps_own_edges(const GameState *g, unsigned layer)
{
    return layer == FIELD_BG2 && (field_bg2_half_scroll(g) & BG2_HALF_PACE);
}

static ScreenEdges screen_edges(const GameState *g, bool own)
{
    if (own)
        return (ScreenEdges){ field_bg2_screen_left(g), field_bg2_screen_right(g), field_bg2_screen_top(g),
                              field_bg2_screen_bottom(g) };
    return (ScreenEdges){ field_screen_left(g), field_screen_right(g), field_screen_top(g), field_screen_bottom(g) };
}

/* The edges on an axis move a tile together. */
static void move_screen_edges(GameState *g, bool own, Axis axis, int8_t by)
{
    ScreenEdges e = screen_edges(g, own);
    if (axis == AXIS_X) {
        e.left = (uint8_t)(e.left + by);
        e.right = (uint8_t)(e.right + by);
    } else {
        e.top = (uint8_t)(e.top + by);
        e.bottom = (uint8_t)(e.bottom + by);
    }
    if (own) {
        set_field_bg2_screen_left(g, e.left);
        set_field_bg2_screen_right(g, e.right);
        set_field_bg2_screen_top(g, e.top);
        set_field_bg2_screen_bottom(g, e.bottom);
    } else {
        set_field_screen_left(g, e.left);
        set_field_screen_right(g, e.right);
        set_field_screen_top(g, e.top);
        set_field_screen_bottom(g, e.bottom);
    }
}

static bool drawn_whole(const GameState *g, unsigned layer) { return (field_layer_drawn_whole(g) >> layer) & 1; }

static void edge_crossed(GameState *g, Side side, unsigned layer)
{
    set_field_layer_edges_crossed(g, side, (uint8_t)(field_layer_edges_crossed(g, side) | 1u << layer));
}

static bool crossed(const GameState *g, Side side, unsigned layer)
{
    return (field_layer_edges_crossed(g, side) >> layer) & 1;
}

/* A layer's move on an axis one way, as it is taken: its place in its tile
 * and its scroll (and BG3's wave phase, going down), kept here and stored
 * once it is taken; and what a crossing does. */
typedef struct {
    unsigned layer;
    Axis axis;
    bool forward;
    bool half_pace;        /* BG2 at half the pace: a place in 32 pixels, a scroll every other one */
    bool moves_edges;      /* its crossings move the screen's edges it goes by: BG1's, or a half-pace BG2's own */
    bool always_due;       /* its crossings bring their lines in even where it is drawn whole: the same two */
    uint8_t fine;
    uint16_t scroll;
    uint8_t wave;
    bool crossed;
} LayerMove;

/* BG3's waves go with it down the screen: their phase moves on four lines a
 * pixel. */
enum { WAVE_LINES_A_PIXEL = 4 };

static bool moves_the_waves(const LayerMove *m) { return m->layer == FIELD_BG3 && m->axis == AXIS_Y; }

static LayerMove start_moving(const GameState *g, unsigned layer, Axis axis, bool forward)
{
    LayerMove m = { layer, axis, forward, keeps_own_edges(g, layer), false, false, 0, 0, 0, false };
    m.moves_edges = layer == FIELD_BG1 || m.half_pace;
    m.always_due = m.moves_edges;
    m.fine = axis == AXIS_X ? field_layer_fine_x(g, layer) : field_layer_fine_y(g, layer);
    m.scroll = axis == AXIS_X ? field_layer_scroll_h(g, layer) : field_layer_scroll_v(g, layer);
    if (moves_the_waves(&m))
        m.wave = field_line_wave_phase(g);
    return m;
}

static void finish_moving(GameState *g, const LayerMove *m)
{
    if (m->axis == AXIS_X) {
        set_field_layer_fine_x(g, m->layer, m->fine);
        set_field_layer_scroll_h(g, m->layer, m->scroll);
    } else {
        set_field_layer_fine_y(g, m->layer, m->fine);
        set_field_layer_scroll_v(g, m->layer, m->scroll);
    }
    if (moves_the_waves(m))
        set_field_line_wave_phase(g, m->wave);
}

// $C0:944B-$C0:979A
/* A layer moves a pixel: it scrolls, and it counts its place in its tile.
 * Where it crosses an 8-pixel tile, the screen's edges it goes by move
 * (BG1's, or BG2's own), and the row or column it crossed onto is due,
 * unless the layer is drawn whole (BG1 and a half-pace BG2 always have
 * theirs due). */
static void move_a_pixel(GameState *g, LayerMove *m)
{
    uint8_t span = m->half_pace ? 32 : 16, at = m->fine;
    bool crosses = m->forward ? (at == span - 1 || at == span / 2 - 1) : (at == 0 || at == span / 2);
    int8_t by = m->forward ? 1 : -1;
    m->fine = m->forward ? (at == span - 1 ? 0 : (uint8_t)(at + 1)) : (at == 0 ? (uint8_t)(span - 1) : (uint8_t)(at - 1));
    if (moves_the_waves(m))
        m->wave = (uint8_t)(m->wave + by * WAVE_LINES_A_PIXEL);
    if (!m->half_pace || (m->fine & 1) == 0)
        m->scroll = (uint16_t)(m->scroll + by);
    if (crosses) {
        if (m->moves_edges)
            move_screen_edges(g, m->half_pace, m->axis, by);
        m->crossed = true;
    }
}

/* Once the move is taken: the side it crossed onto, if it did, is due. */
static void note_the_crossing(GameState *g, const LayerMove *m)
{
    if (!m->crossed || !(m->always_due || !drawn_whole(g, m->layer)))
        return;
    Side side = m->axis == AXIS_X ? (m->forward ? SIDE_RIGHT : SIDE_LEFT) : (m->forward ? SIDE_BELOW : SIDE_ABOVE);
    edge_crossed(g, side, m->layer);
}

/* ---- Streaming ------------------------------------------------------------------------- */

/* A layer's place in its tile map, in 8-pixel tiles, moved a step; it wraps
 * round the map (field/field.h). */
static void move_place_in_tile_map(GameState *g, unsigned layer, Side side)
{
    switch (side) {
    case SIDE_BELOW: set_field_layer_position_y(g, layer, (uint8_t)((field_layer_position_y(g, layer) + 1) & (FIELD_TILE_MAP_ROWS - 1))); break;
    case SIDE_ABOVE: set_field_layer_position_y(g, layer, (uint8_t)((field_layer_position_y(g, layer) - 1) & (FIELD_TILE_MAP_ROWS - 1))); break;
    case SIDE_RIGHT: set_field_layer_position_x(g, layer, (uint8_t)((field_layer_position_x(g, layer) + 1) & (FIELD_TILE_MAP_COLUMNS - 1))); break;
    default: set_field_layer_position_x(g, layer, (uint8_t)((field_layer_position_x(g, layer) - 1) & (FIELD_TILE_MAP_COLUMNS - 1))); break;
    }
}

/* The line a side's crossing brings in: the row just below or above the
 * view, from a tile left of it, or the column just right or left of it, from
 * a tile above. */
static Line line_coming_in(ScreenEdges e, Side side)
{
    switch (side) {
    case SIDE_BELOW: return (Line){ ALONG_A_ROW, (uint8_t)(e.left - 1), e.bottom, FIELD_ROW_MAP_TILES };
    case SIDE_ABOVE: return (Line){ ALONG_A_ROW, (uint8_t)(e.left - 1), e.top, FIELD_ROW_MAP_TILES };
    case SIDE_RIGHT: return (Line){ DOWN_A_COLUMN, e.right, (uint8_t)(e.top - 1), FIELD_COLUMN_MAP_TILES };
    default: return (Line){ DOWN_A_COLUMN, e.left, (uint8_t)(e.top - 1), FIELD_COLUMN_MAP_TILES };
    }
}

/* The streaming's time: a steady part, and a part the lines' lengths and the
 * moves' pixels set, which the port's own reads and writes don't follow (it
 * keeps a move in locals, and works a map tile's look out once for its two
 * entries). So the lines' and the moves' own accesses are left out of the
 * estimate, and each entry (a row's costs the original more than a
 * column's) and each pixel is charged at the streaming's own rate instead
 * (charge.h); the edge runs are timed as their own routine. The drawings at
 * a map's load charge their entries the same way. */
enum { STREAMING = 0xC093E1, ACCESSES_16_ROW_ENTRIES = 65, ACCESSES_16_COLUMN_ENTRIES = 60, ACCESSES_FOUR_PIXELS = 11 };

static void build_line_charged(GameState *g, unsigned layer, Line line, Staging *out, uint32_t accesses_16_entries)
{
    uint32_t mark = 0;
    uint16_t start = out->at;
    mark = charge_uncounted_begin(g);
    build_line(g, layer, line, out);
    charge_uncounted_end(g, mark);
    charge_as(g, STREAMING, (uint32_t)(out->at - start) / ENTRY_BYTES * accesses_16_entries / 16);
    (void)mark;
    (void)start;
}

static void bring_in(GameState *g, unsigned layer, Side side)
{
    Line line = line_coming_in(screen_edges(g, keeps_own_edges(g, layer)), side);
    Staging out = { (uint16_t)(field_staged_line(layer, line.way == DOWN_A_COLUMN) - field_staged_line(FIELD_BG1, false)), NULL };
    build_line_charged(g, layer, line, &out, line.way == ALONG_A_ROW ? ACCESSES_16_ROW_ENTRIES : ACCESSES_16_COLUMN_ENTRIES);
    charge_io_accesses(g, 3); /* the completed staging cursor */
    move_place_in_tile_map(g, layer, side);
    set_field_layer_upload_flags(g, side, (uint8_t)(field_layer_upload_flags(g, side) | 1u << layer));
}

/* The ways a layer moves, in the order it takes them: right, left, up and
 * down. */
static const struct {
    Axis axis;
    bool forward;
} ways[] = { { AXIS_X, true }, { AXIS_X, false }, { AXIS_Y, false }, { AXIS_Y, true } };
enum { WAYS = sizeof ways / sizeof ways[0] };

// $C0:93E1
/* Every frame after the layers' scroll: each layer takes its move a pixel at
 * a time, right, left, up and down; then each side's crossings bring in their
 * lines, the row above the view first, then below, left and right, BG1 to
 * BG3; and the layers' edge runs are worked out for the uploads. */
void stream_the_layers(GameState *g)
{
    for (unsigned side = 0; side < SIDES; side++)
        set_field_layer_edges_crossed(g, side, 0);

    for (unsigned layer = 0; layer < FIELD_LAYERS; layer++) {
        for (unsigned w = 0; w < WAYS; w++) {
            unsigned k = layer * AXES + ways[w].axis;
            uint8_t pixels = ways[w].forward ? field_layer_move_positive(g, k) : field_layer_move_negative(g, k);
            if (pixels == 0)
                continue;
            uint32_t mark = 0;
            mark = charge_uncounted_begin(g);   /* the pixels' charge below stands for it */
            (void)mark;
            LayerMove m = start_moving(g, layer, ways[w].axis, ways[w].forward);
            for (unsigned n = 0; n < pixels; n++)
                move_a_pixel(g, &m);
            finish_moving(g, &m);
            note_the_crossing(g, &m);
            charge_uncounted_end(g, mark);
            charge_as(g, STREAMING, (uint32_t)pixels * ACCESSES_FOUR_PIXELS / 4);

        }
    }
    for (unsigned side = 0; side < SIDES; side++)
        set_field_layer_upload_flags(g, side, 0);
    static const Side order[SIDES] = { SIDE_ABOVE, SIDE_BELOW, SIDE_LEFT, SIDE_RIGHT };
    for (unsigned k = 0; k < SIDES; k++)
        for (unsigned layer = 0; layer < FIELD_LAYERS; layer++)
            if (crossed(g, order[k], layer))
                bring_in(g, layer, order[k]);
    TIMED_AS(g, 0xC07F9A, field_find_layer_edges(g));
}

// $C0:87F1 (with $C0:8345 for BG1, $C0:8365 and $C0:8385 for BG2, $C0:83A5 for BG3)
/* The columns right of the view built, and their uploads asked for, with the
 * layers' places in their tile maps as they are: as a map's screen is set
 * up or the screen is put back where the party is, the vblank sends them with
 * the screen's next frame. A layer drawn whole has none; BG2 at half the pace
 * goes by its own edges. Then the layers' edge runs are worked out. */
void stream_the_right_columns(GameState *g)
{
    for (unsigned layer = 0; layer < FIELD_LAYERS; layer++) {
        if (layer != FIELD_BG1 && drawn_whole(g, layer))
            continue;
        Line line = line_coming_in(screen_edges(g, keeps_own_edges(g, layer)), SIDE_RIGHT);
        Staging out = { (uint16_t)(field_staged_line(layer, true) - field_staged_line(FIELD_BG1, false)), NULL };
        build_line_charged(g, layer, line, &out, ACCESSES_16_COLUMN_ENTRIES);
        charge_io_accesses(g, 3); /* the completed staging cursor */
        set_field_layer_upload_flags(g, SIDE_RIGHT, (uint8_t)(field_layer_upload_flags(g, SIDE_RIGHT) | 1u << layer));
    }
    TIMED_AS(g, 0xC07F9A, field_find_layer_edges(g));
    field_margins_due(g);
}

/* ---- A layer drawn at a map's load ------------------------------------------------------- */

/* A layer drawn at a map's load, into the staging buffers from the first's
 * start, which the load sends on: the screen's rows as the screen stands,
 * a row after another, 16 map tiles each; or, for a layer drawn whole, its
 * map's first 16 by 16 tiles and then the tile map's other half -- the map's
 * next 16 columns where it is wider, its next 16 rows where it is taller
 * (which makes the layer's tile map two screens tall), or the same tiles
 * again. */
enum { DRAWN_TILES = 16, SCREEN_ROWS_DRAWN = 32, MAP_SCREEN_TILES = 16, WHOLE_ROWS = 32, TWO_SCREENS_TALL = 0x02 };

/* The rows from the first down to the last, or the first alone where the
 * last lies above it (a BG2 whose edges read so). */
static void draw_rows(GameState *g, unsigned layer, Staging *out, uint8_t column, uint8_t first_row, uint8_t last_row)
{
    uint8_t row = first_row;
    do {
        build_line(g, layer, (Line){ ALONG_A_ROW, column, row, DRAWN_TILES }, out);
        row++;
    } while (row <= last_row);
}

/* The screen's rows: the count the original runs down is kept in the cell
 * the layers' edge runs read their wrap from (field_layer_edge_wrap), which
 * the drawing leaves 0 for them to read. */
static void draw_the_screen(GameState *g, unsigned layer, Staging *out, ScreenEdges e)
{
    uint8_t below = (uint8_t)(e.top + SCREEN_ROWS_DRAWN);
    for (uint8_t row = e.top; row != below; row++)
        build_line(g, layer, (Line){ ALONG_A_ROW, e.left, row, DRAWN_TILES }, out);
    set_field_layer_edge_wrap(g, (uint16_t)(field_layer_edge_wrap(g) & 0xFF00));
}

static void draw_whole(GameState *g, unsigned layer, Staging *out)
{
    draw_rows(g, layer, out, 0, 0, WHOLE_ROWS - 1);
    if (field_map_layer_sizes_width(g, layer) != MAP_SCREEN_TILES) {
        draw_rows(g, layer, out, 2 * MAP_SCREEN_TILES, 0, WHOLE_ROWS - 1);
    } else if (field_map_layer_sizes_height(g, layer) != MAP_SCREEN_TILES) {
        draw_rows(g, layer, out, 0, WHOLE_ROWS, 2 * WHOLE_ROWS - 1);
        set_field_layer_map_size(g, layer, TWO_SCREENS_TALL);
    } else {
        draw_rows(g, layer, out, 0, 0, WHOLE_ROWS - 1);
    }
}

// $C0:75E9 (BG1), $C0:78EC (BG2), $C0:7CB5 (BG3)
/* BG1 is drawn as the screen stands; BG2 and BG3 whole where they are drawn
 * whole, and BG2 at half the pace from its own edges, its own rows down to
 * its bottom edge. */
enum { DRAW_BG1 = 0xC075E9, DRAW_BG2 = 0xC078EC, DRAW_BG3 = 0xC07CB5, ACCESSES_A_DRAWN_ENTRY = 6 };

void draw_layer_at_the_maps_load(GameState *g, unsigned layer)
{
    static const uint32_t drawing[FIELD_LAYERS] = { DRAW_BG1, DRAW_BG2, DRAW_BG3 };
    Staging out = { 0, NULL };
    uint32_t mark = 0;
    mark = charge_uncounted_begin(g);   /* the entries' charge below stands for it */
    (void)mark;
    if (layer != FIELD_BG1 && drawn_whole(g, layer))
        draw_whole(g, layer, &out);
    else if (keeps_own_edges(g, layer)) {
        ScreenEdges own = screen_edges(g, true);
        draw_rows(g, layer, &out, own.left, own.top, own.bottom);
    } else
        draw_the_screen(g, layer, &out, screen_edges(g, false));
    charge_uncounted_end(g, mark);
    charge_as(g, drawing[layer], (uint32_t)out.at / ENTRY_BYTES * ACCESSES_A_DRAWN_ENTRY);
    charge_io_accesses(g, 3); /* outside the explicitly priced drawing */
}

/* The layers the port streams: each layer's map repeats within the map's 64
 * by 64 tables. */
enum { WITHIN_THE_MAPS = 0x3F3F };

/* BG2's drawing at half the pace draws the rows between its own edges,
 * which for the game are the screen's, its top above its bottom; edges the
 * other way round, or so far apart that the rows would run past the staging
 * buffers (past the bank, as the original's port goes on, or for ever to a
 * bottom of 255), the port refuses. */
enum { STAGED_ROWS = 3 * FIELD_LAYER_STAGING_BYTES / (2 * DRAWN_TILES * ENTRY_BYTES), LAST_ROW = 0xFF };

bool layers_repeat_within_the_maps(const GameState *g)
{
    bool ok = true;
    for (unsigned layer = 0; layer < FIELD_LAYERS; layer++)
        ok = ok && (index_mask(g, layer) & ~WITHIN_THE_MAPS) == 0;
    return ok;
}

bool layer_drawing_fits(const GameState *g, unsigned layer)
{
    if (!layers_repeat_within_the_maps(g))
        return false;
    if (layer != FIELD_BG2)
        return true;
    ScreenEdges own = screen_edges(g, true);
    return !keeps_own_edges(g, FIELD_BG2) || drawn_whole(g, FIELD_BG2) ||
           (own.top <= own.bottom && own.bottom - own.top < STAGED_ROWS && own.bottom != LAST_ROW);
}

/* The drawings at a map's load, each where the port draws it: BG2's drawing
 * fitting the staging buffers, BG1's and BG3's every layer's map repeating
 * within the map's tables. */
static const uint32_t drawing_entries[] = { [FIELD_BG1] = 0xC075E9, [FIELD_BG2] = 0xC078EC, [FIELD_BG3] = 0xC07CB5 };

bool field_layer_drawn_at_the_load(GameState *g, unsigned layer)
{
    bool fits = layer == FIELD_BG2 ? layer_drawing_fits(g, FIELD_BG2) : layers_repeat_within_the_maps(g);
    if (!fits) {
        program_breaks_a_contract(g, drawing_entries[layer], "a layer the port doesn't draw at the map's load");
        return false;
    }
    TIMED_AS(g, drawing_entries[layer], draw_layer_at_the_maps_load(g, layer));
    return true;
}

/* ---- A wider view's margins ------------------------------------------------------------------ */

/* On a view wider than the original's screen (vid_view), the layers show
 * columns of their tile maps either side of the screen's, which the game
 * never streams: they hold what the ring held last. So each frame the
 * columns the margins show are built from the map and sent, beside the game's
 * work and outside it -- into a line of the view's own rather than the
 * staging buffers, its reads of the map left out of the game's time and its
 * sends made in the vblank, which the original's time doesn't charge -- so
 * the game runs the same at any view. The screen's edges, which decide which
 * objects run, stay the original's. Past the map's edges a margin's column is
 * built from the edge's (clamp_to_the_map).
 *
 * A layer's tile map is 64 columns round: the screen's 33 and a margin of up
 * to 15 columns either side fit, which is what holds the view to
 * VID_MAX_VIEW_WIDTH. Layers drawn whole at the map's load hold their whole
 * map already, and layers the map doesn't show have no looks to build from:
 * both are left alone. */
enum { RING_ROWS = FIELD_TILE_MAP_ROWS, MOST_MARGIN_COLUMNS = (FIELD_TILE_MAP_COLUMNS - FIELD_SCREEN_COLUMNS - 1) / 2 };

/* The map's own columns, which its tables hold (past them they repeat it),
 * whatever the screen's limits within it: a margin shows what lies beside
 * the screen on the map. A column past the map's edge is built as the edge's,
 * which the layer shows only where the edge is a uniform void (below). */
static int map_columns_of(const GameState *g, unsigned layer)
{
    return field_map_layer_sizes_width(g, layer) * FIELD_ENTRIES_A_MAP_TILE;
}

static uint8_t clamp_to_the_map(const GameState *g, unsigned layer, int column)
{
    int last = map_columns_of(g, layer) - 1;
    return (uint8_t)(column < 0 ? 0 : column > last ? last : column);
}

/* A column of the map's entries, down from the row above the screen. */
static void build_a_margin_column(GameState *g, unsigned layer, int column, uint8_t line[(RING_ROWS + 2) * ENTRY_BYTES])
{
    ScreenEdges e = screen_edges(g, keeps_own_edges(g, layer));
    Staging out = { 0, line };
    build_line(g, layer, (Line){ DOWN_A_COLUMN, clamp_to_the_map(g, layer, column), (uint8_t)(e.top - 1), RING_ROWS / 2 },
               &out);
}

/* Whether a map's edge column is one entry all the way down: a void. */
static bool edge_is_a_void(GameState *g, unsigned layer, int column)
{
    uint8_t line[(RING_ROWS + 2) * ENTRY_BYTES];
    build_a_margin_column(g, layer, column, line);
    for (unsigned i = 1; i < RING_ROWS; i++)
        if (line[i * ENTRY_BYTES] != line[0] || line[i * ENTRY_BYTES + 1] != line[1])
            return false;
    return true;
}

/* One margin column: the map's 8-pixel column `column` into the tile map's
 * ring column `ring`. The ring holds the rows from the one above the screen
 * down, round from where that row is in it, as the game's own columns do. */
static void send_a_margin_column(GameState *g, Platform *pf, unsigned layer, int column, uint8_t ring)
{
    uint8_t above = (uint8_t)((field_layer_position_y(g, layer) - 1) & (RING_ROWS - 1));
    uint8_t line[(RING_ROWS + 2) * ENTRY_BYTES], ring_order[RING_ROWS * ENTRY_BYTES];
    build_a_margin_column(g, layer, column, line);
    for (unsigned i = 0; i < RING_ROWS; i++) {
        unsigned r = (above + i) & (RING_ROWS - 1);
        ring_order[r * ENTRY_BYTES] = line[i * ENTRY_BYTES];
        ring_order[r * ENTRY_BYTES + 1] = line[i * ENTRY_BYTES + 1];
    }
    uint16_t vram = (uint16_t)(field_layer_map_base(g, layer) + field_tile_map_index(ring, 0));
    static const VidVramPort column_port = { .step = FIELD_TILE_MAP_HALF_COLUMNS, .after_high = true };
    vid_send_view_vram(pf, vram, (PfData){ .ptr = ring_order }, sizeof ring_order, &column_port);
}

/* The columns either side of the screen a view needs, the fine scroll's
 * included, as many as the ring holds. */
static int margin_columns(VidView view)
{
    int margin = (-vid_view_left(view) + FIELD_ENTRY_PIXELS - 1) / FIELD_ENTRY_PIXELS + 1;
    return margin < MOST_MARGIN_COLUMNS ? margin : MOST_MARGIN_COLUMNS;
}

enum { FRAMES_DUE_AFTER_A_SEND = 2 };

void field_margins_due(GameState *g) { field_margins(g)->due = FRAMES_DUE_AFTER_A_SEND; }

void field_fill_the_views_margins(GameState *g, Platform *pf, bool moved)
{
    VidView view = vid_view(pf);
    FieldMargins *m = field_margins(g);
    if (!vid_view_is_wider(view) || !(moved || m->due))
        return;
    if (m->due)
        m->due--;
    int margin = margin_columns(view);
    uint32_t mark = charge_uncounted_begin(g);
    for (unsigned layer = 0; layer < FIELD_LAYERS; layer++) {
        bool shown = ((field_main_screen(g) | field_sub_screen(g)) >> layer) & 1;
        if (!shown || (layer != FIELD_BG1 && drawn_whole(g, layer)))
            continue;
        ScreenEdges e = screen_edges(g, keeps_own_edges(g, layer));
        uint8_t x = field_layer_position_x(g, layer);
        for (int k = 1; k <= margin; k++) {
            send_a_margin_column(g, pf, layer, e.left - k, (uint8_t)((x - k) & (FIELD_TILE_MAP_COLUMNS - 1)));
            send_a_margin_column(g, pf, layer, e.right + k,
                                 (uint8_t)((x + FIELD_SCREEN_COLUMNS + k) & (FIELD_TILE_MAP_COLUMNS - 1)));
        }
    }
    charge_uncounted_end(g, mark);
}

/* How far across the screen each layer holds the map, for the renderer to
 * leave the layer out past it (vid_show_a_layer_across): a streamed layer the
 * columns its margins were built over; a layer drawn whole its tile map's
 * span of the map, once -- a map one screen across fills only half the tile
 * map, which shows its tiles again -- and not past the map's edges. Kept for
 * the battle, which is fought over the field's layers. */
enum { RING_PIXELS = FIELD_TILE_MAP_COLUMNS * FIELD_ENTRY_PIXELS, ONE_SCREEN_TILES = 16 };

static int around_the_ring(int pixels)
{
    pixels &= RING_PIXELS - 1;
    return pixels >= RING_PIXELS / 2 ? pixels - RING_PIXELS : pixels;
}

static FieldLayerSpan layer_span(GameState *g, unsigned layer, VidView view, int margin)
{
    uint16_t scroll = field_layer_scroll_h(g, layer);
    unsigned map_columns = field_map_layer_sizes_width(g, layer) * FIELD_ENTRIES_A_MAP_TILE;
    if (layer != FIELD_BG1 && drawn_whole(g, layer)) {
        if (map_columns > ONE_SCREEN_TILES * FIELD_ENTRIES_A_MAP_TILE)
            return (FieldLayerSpan){ vid_view_left(view), vid_view_right(view), scroll };
        int left = around_the_ring(-(int)scroll);
        return (FieldLayerSpan){ (int16_t)left, (int16_t)(left + ONE_SCREEN_TILES * FIELD_MAP_TILE_PIXELS), scroll };
    }
    bool own = keeps_own_edges(g, layer);
    ScreenEdges e = screen_edges(g, own);
    int fine = around_the_ring(scroll - field_layer_position_x(g, layer) * FIELD_ENTRY_PIXELS);
    int first = e.left - margin, last = e.right + margin + 1;   /* the columns built, the last one past */
    int map_last = map_columns_of(g, layer);
    if (first < 0 && !edge_is_a_void(g, layer, 0))
        first = 0;
    if (last > map_last && !edge_is_a_void(g, layer, map_last - 1))
        last = map_last;
    int left = (first - e.left) * FIELD_ENTRY_PIXELS - fine, right = (last - e.left) * FIELD_ENTRY_PIXELS - fine;
    return (FieldLayerSpan){ (int16_t)left, (int16_t)(right > left ? right : left), scroll };
}

/* The map's edges on the screen, by BG1, which the screen goes by: a layer
 * drawn whole (a still or drifting layer over the map) shows nothing past
 * them, where the map has no picture for it to be over. */
static FieldLayerSpan maps_span(const GameState *g)
{
    int fine = around_the_ring(field_layer_scroll_h(g, FIELD_BG1) -
                               field_layer_position_x(g, FIELD_BG1) * FIELD_ENTRY_PIXELS);
    int screen_left = field_screen_left(g) * FIELD_ENTRY_PIXELS + fine;   /* the map's pixel at the screen's left */
    int right = map_columns_of(g, FIELD_BG1) * FIELD_ENTRY_PIXELS - screen_left;
    return (FieldLayerSpan){ (int16_t)-screen_left, (int16_t)right, 0 };
}

void field_show_the_layers_across(GameState *g, Platform *pf)
{
    VidView view = vid_view(pf);
    if (!vid_view_is_wider(view))
        return;
    FieldMargins *m = field_margins(g);
    uint32_t mark = charge_uncounted_begin(g);
    FieldLayerSpan map = maps_span(g);
    for (unsigned layer = 0; layer < FIELD_LAYERS; layer++) {
        bool shown = ((field_main_screen(g) | field_sub_screen(g)) >> layer) & 1;
        m->span[layer] = shown ? layer_span(g, layer, view, margin_columns(view))
                               : (FieldLayerSpan){ 0, VID_ORIGINAL_VIEW_WIDTH, field_layer_scroll_h(g, layer) };
        bool still = layer != FIELD_BG1 && drawn_whole(g, layer);   /* no map edge of its own to go on past */
        if (still && m->span[layer].left < map.left)
            m->span[layer].left = map.left;
        if (still && m->span[layer].right > map.right)
            m->span[layer].right = map.right;
        m->held[layer] = shown;
        vid_show_a_layer_across(pf, (VidLayer)layer, m->span[layer].left, m->span[layer].right);
    }
    charge_uncounted_end(g, mark);
}

void field_battle_takes_its_own_layer(GameState *g, unsigned layer) { field_margins(g)->held[layer] = false; }

void field_show_the_layers_under_a_battle(GameState *g, Platform *pf, const uint16_t scroll[FIELD_LAYERS])
{
    if (!vid_view_is_wider(vid_view(pf)))
        return;
    const FieldMargins *m = field_margins(g);
    for (unsigned layer = 0; layer < FIELD_LAYERS; layer++) {
        if (!m->held[layer])
            continue;
        int moved = around_the_ring(scroll[layer] - m->span[layer].scroll);
        vid_show_a_layer_across(pf, (VidLayer)layer, (int16_t)(m->span[layer].left - moved),
                                (int16_t)(m->span[layer].right - moved));
    }
}
