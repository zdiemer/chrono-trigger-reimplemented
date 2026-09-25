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
 * the port writes the staging buffers directly and leaves the port's cursor
 * where the original's is, as scaffolding (cpu_io.h). */
#include "field/streaming.h"

#include "charge.h"
#include "cpu_io.h"
#include "field/bridge.h"
#include "field/field.h"
#include "field/layers.h"
#include "field/scratch.h"
#include "port/machine.h"
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
enum { STAGING_BYTES = FIELD_LAYER_STAGING_BYTES };

typedef struct {
    uint16_t at;   /* from the first buffer's start */
} Staging;

static void stage_byte(GameState *g, uint16_t at, uint8_t v)
{
    set_field_layer_staging_byte(g, at / STAGING_BYTES, at % STAGING_BYTES, v);
}

static void stage(GameState *g, Staging *s, uint16_t entry)
{
    stage_byte(g, s->at, (uint8_t)entry);
    stage_byte(g, (uint16_t)(s->at + 1), (uint8_t)(entry >> 8));
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

/* Where the original's builders leave their working cells, for its code and
 * shadow mode: the map column and row they got to, the line's start, and the
 * count, run down. */
typedef struct {
    uint8_t map_column, map_row;
} LineEnd;

static void leave_line(GameState *g, Line line, LineEnd end)
{
    original_layer_cell8(g, 0x52, end.map_column);
    original_layer_cell8(g, 0x54, end.map_row);
    original_layer_cell8(g, 0x58, line.column);
    original_layer_cell8(g, 0x5A, line.row);
    original_layer_cell8(g, 0x5C, 0);
}

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
    uint8_t last = p.along;
    while (tiles > 0) {
        uint16_t look = look_at(g, layer, line.way, p);
        stage_half(g, out, layer, line.way, p, look, false);
        stage_half(g, out, layer, line.way, p, look, true);
        tiles--;
        last = p.along;
        p.along = (uint8_t)((p.along + 1) & along_mask);
    }
    if (odd && odd_start_counts(layer, line.way))   /* the second half again: see above */
        stage_half(g, out, layer, line.way, p, look_at(g, layer, line.way, p), true);
    /* the original's rows leave the map tile after the last, its columns the last */
    ORIGINAL_EFFECT(leave_line(g, line, row ? (LineEnd){ p.along, p.across } : (LineEnd){ p.across, last }));
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

enum { HALF_PACE = 0x80 };   /* field_bg2_half_scroll */

static bool keeps_own_edges(const GameState *g, unsigned layer)
{
    return layer == FIELD_BG2 && (field_bg2_half_scroll(g) & HALF_PACE);
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

/* A row spans the screen and a tile either side, a column the screen and a
 * tile above, in map tiles. */
enum { ROW_TILES = FIELD_ROW_MAP_TILES, COLUMN_TILES = FIELD_COLUMN_MAP_TILES };

/* A layer's place in its tile map, in 8-pixel tiles: 64 across and 32 down,
 * round which it wraps. */
enum { TILE_MAP_COLUMNS = FIELD_TILE_MAP_COLUMNS, TILE_MAP_ROWS = FIELD_TILE_MAP_ROWS };

static void move_place_in_tile_map(GameState *g, unsigned layer, Side side)
{
    switch (side) {
    case SIDE_BELOW: set_field_layer_position_y(g, layer, (uint8_t)((field_layer_position_y(g, layer) + 1) & (TILE_MAP_ROWS - 1))); break;
    case SIDE_ABOVE: set_field_layer_position_y(g, layer, (uint8_t)((field_layer_position_y(g, layer) - 1) & (TILE_MAP_ROWS - 1))); break;
    case SIDE_RIGHT: set_field_layer_position_x(g, layer, (uint8_t)((field_layer_position_x(g, layer) + 1) & (TILE_MAP_COLUMNS - 1))); break;
    default: set_field_layer_position_x(g, layer, (uint8_t)((field_layer_position_x(g, layer) - 1) & (TILE_MAP_COLUMNS - 1))); break;
    }
}

/* The line a side's crossing brings in: the row just below or above the
 * view, from a tile left of it, or the column just right or left of it, from
 * a tile above. */
static Line line_coming_in(ScreenEdges e, Side side)
{
    switch (side) {
    case SIDE_BELOW: return (Line){ ALONG_A_ROW, (uint8_t)(e.left - 1), e.bottom, ROW_TILES };
    case SIDE_ABOVE: return (Line){ ALONG_A_ROW, (uint8_t)(e.left - 1), e.top, ROW_TILES };
    case SIDE_RIGHT: return (Line){ DOWN_A_COLUMN, e.right, (uint8_t)(e.top - 1), COLUMN_TILES };
    default: return (Line){ DOWN_A_COLUMN, e.left, (uint8_t)(e.top - 1), COLUMN_TILES };
    }
}

/* Where the staging buffers lie, for the work RAM port's cursor the
 * original leaves after its last line. */
enum { WORK_RAM_LOWER_BANK = 0 };

static void leave_port_after(GameState *g, Staging s)
{
    hw_port_seek(g, (uint16_t)(field_staged_line(FIELD_BG1, false) + s.at), WORK_RAM_LOWER_BANK);
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
    ORIGINAL_EFFECT(mark = charge_uncounted_begin(g));
    build_line(g, layer, line, out);
    ORIGINAL_EFFECT(charge_uncounted_end(g, mark));
    ORIGINAL_EFFECT(charge_as(g, STREAMING, (uint32_t)(out->at - start) / ENTRY_BYTES * accesses_16_entries / 16));
    (void)mark;
    (void)start;
}

static void bring_in(GameState *g, unsigned layer, Side side)
{
    Line line = line_coming_in(screen_edges(g, keeps_own_edges(g, layer)), side);
    Staging out = { (uint16_t)(field_staged_line(layer, line.way == DOWN_A_COLUMN) - field_staged_line(FIELD_BG1, false)) };
    build_line_charged(g, layer, line, &out, line.way == ALONG_A_ROW ? ACCESSES_16_ROW_ENTRIES : ACCESSES_16_COLUMN_ENTRIES);
    ORIGINAL_EFFECT(leave_port_after(g, out));
    move_place_in_tile_map(g, layer, side);
    set_field_layer_upload_flags(g, side, (uint8_t)(field_layer_upload_flags(g, side) | 1u << layer));
}

/* The count the original's loops run down, a cell of the layers' page, which
 * is left 0 once a layer has moved at all. */
static void leave_moves_counted(GameState *g) { original_layer_cell8(g, 0x66, 0); }

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
    bool moved = false;
    for (unsigned layer = 0; layer < FIELD_LAYERS; layer++) {
        for (unsigned w = 0; w < WAYS; w++) {
            unsigned k = layer * AXES + ways[w].axis;
            uint8_t pixels = ways[w].forward ? field_layer_move_positive(g, k) : field_layer_move_negative(g, k);
            if (pixels == 0)
                continue;
            uint32_t mark = 0;
            ORIGINAL_EFFECT(mark = charge_uncounted_begin(g));   /* the pixels' charge below stands for it */
            (void)mark;
            LayerMove m = start_moving(g, layer, ways[w].axis, ways[w].forward);
            for (unsigned n = 0; n < pixels; n++)
                move_a_pixel(g, &m);
            finish_moving(g, &m);
            note_the_crossing(g, &m);
            ORIGINAL_EFFECT(charge_uncounted_end(g, mark));
            ORIGINAL_EFFECT(charge_as(g, STREAMING, (uint32_t)pixels * ACCESSES_FOUR_PIXELS / 4));
            moved = true;
        }
    }
    if (moved)
        ORIGINAL_EFFECT(leave_moves_counted(g));
    for (unsigned side = 0; side < SIDES; side++)
        set_field_layer_upload_flags(g, side, 0);
    static const Side order[SIDES] = { SIDE_ABOVE, SIDE_BELOW, SIDE_LEFT, SIDE_RIGHT };
    for (unsigned k = 0; k < SIDES; k++)
        for (unsigned layer = 0; layer < FIELD_LAYERS; layer++)
            if (crossed(g, order[k], layer))
                bring_in(g, layer, order[k]);
    TIMED_AS(g, 0xC07F9A, field_find_layer_edges(g));
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
    ORIGINAL_EFFECT(original_layer_cell8(g, 0x5A, below));   /* the original's row, moved on past the last */
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
    Staging out = { 0 };
    uint32_t mark = 0;
    ORIGINAL_EFFECT(mark = charge_uncounted_begin(g));   /* the entries' charge below stands for it */
    (void)mark;
    if (layer != FIELD_BG1 && drawn_whole(g, layer))
        draw_whole(g, layer, &out);
    else if (keeps_own_edges(g, layer)) {
        ScreenEdges own = screen_edges(g, true);
        draw_rows(g, layer, &out, own.left, own.top, own.bottom);
    } else
        draw_the_screen(g, layer, &out, screen_edges(g, false));
    ORIGINAL_EFFECT(charge_uncounted_end(g, mark));
    ORIGINAL_EFFECT(charge_as(g, drawing[layer], (uint32_t)out.at / ENTRY_BYTES * ACCESSES_A_DRAWN_ENTRY));
    ORIGINAL_EFFECT(leave_port_after(g, out));
}

/* ---- The entry ---------------------------------------------------------------------------- */

/* From the walk and the recentring, on the layers' page; it leaves the
 * overflow the edge runs' last sum leaves (field/layers.h), which the
 * field's main loop carries on. */
void field_stream_the_layers(Snes *s)
{
    GameState *g = state_of(s);
    stream_the_layers(g);
    m_set_flag(s, CPU_FLAG_V, field_layer_edges_overflow(g));
    m_rts(s);
}

/* The guard: on the layers' page, and each layer's map repeats within the
 * map's 64 by 64 tables. */
enum { WITHIN_THE_MAPS = 0x3F3F };

/* BG2's drawing at half the pace draws the rows between its own edges,
 * which for the game are the screen's, its top above its bottom; edges the
 * other way round, or so far apart that the rows would run past the staging
 * buffers (past the bank, as the original's port goes on, or for ever to a
 * bottom of 255), are left to the original. */
enum { STAGED_ROWS = 3 * STAGING_BYTES / (2 * DRAWN_TILES * ENTRY_BYTES), LAST_ROW = 0xFF };

bool field_bg2_drawing_can_run(Snes *s)
{
    if (!field_streaming_can_run(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    ScreenEdges own = screen_edges(g, true);
    bool ok = !keeps_own_edges(g, FIELD_BG2) || drawn_whole(g, FIELD_BG2) ||
              (own.top <= own.bottom && own.bottom - own.top < STAGED_ROWS && own.bottom != LAST_ROW);
    vars_peek(false);
    return ok;
}

bool field_streaming_can_run(Snes *s)
{
    if (!field_on_layers_page_io_bank(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = true;
    for (unsigned layer = 0; layer < FIELD_LAYERS; layer++)
        ok = ok && (index_mask(g, layer) & ~WITHIN_THE_MAPS) == 0;
    vars_peek(false);
    return ok;
}

/* The drawings at a map's load, on the layers' page; they leave nothing in
 * the registers their callers read. */
void field_draw_bg1_at_the_load(Snes *s)
{
    draw_layer_at_the_maps_load(state_of(s), FIELD_BG1);
    m_rts(s);
}

void field_draw_bg2_at_the_load(Snes *s)
{
    draw_layer_at_the_maps_load(state_of(s), FIELD_BG2);
    m_rts(s);
}

void field_draw_bg3_at_the_load(Snes *s)
{
    draw_layer_at_the_maps_load(state_of(s), FIELD_BG3);
    m_rts(s);
}
