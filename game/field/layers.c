/* Field mode: the three background layers. Every frame, how far each layer
 * scrolls and which of its tile map's columns and rows come into view; and
 * when a map loads, its tile maps and tile attributes unpacked from the map's
 * data.
 *
 * A layer's tile map in VRAM is 64 tiles wide and 32 high, in two halves of
 * 32 columns side by side ($400 words apart), indexed row * 32 + column. The
 * screen scrolls a pixel a frame toward a target tile when the field asks it
 * to (a scene's pan), the other layers follow at their own speeds or stay
 * still, and a setting can shake it; each layer's move is kept in 4.4 fixed
 * point, whose whole pixels are the frame's move. As a layer scrolls, the
 * deferred uploads send the column and row that come into view at the
 * screen's edges, and the edge runs worked out here say where those are and
 * how they split where a run crosses from one half to the other. These run
 * on the layers' page. */
#include "field/layers.h"

#include "cpu_io.h"
#include "field/bridge.h"
#include "field/field.h"
#include "charge.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

enum {
    SECOND_HALF = FIELD_TILE_MAP_HALF_COLUMNS * FIELD_TILE_MAP_ROWS,   /* a half's entries: where the second starts */
    LAST_ROW_OF_HALF = (FIELD_TILE_MAP_ROWS - 1) * FIELD_TILE_MAP_HALF_COLUMNS,
};
enum { COLUMN_MASK = FIELD_TILE_MAP_COLUMNS - 1, ROW_MASK = FIELD_TILE_MAP_ROWS - 1 };

/* ---- The edges -------------------------------------------------------------------------- */

/* The tile map index of a column on a row. */
static uint16_t map_index(uint16_t column, uint16_t row);
uint16_t field_tile_map_index(uint16_t column, uint16_t row) { return map_index(column, row); }

static uint16_t map_index(uint16_t column, uint16_t row)
{
    uint16_t start = (uint16_t)(row * FIELD_TILE_MAP_HALF_COLUMNS);
    return column < FIELD_TILE_MAP_HALF_COLUMNS ? (uint16_t)(start + column) : (uint16_t)(column - FIELD_TILE_MAP_HALF_COLUMNS + start + SECOND_HALF);
}

/* Where the same row starts in the other half. */
static uint16_t other_half_row(uint16_t column, uint16_t row)
{
    uint16_t start = (uint16_t)(row * FIELD_TILE_MAP_HALF_COLUMNS);
    return column < FIELD_TILE_MAP_HALF_COLUMNS ? (uint16_t)(start + SECOND_HALF) : start;
}

/* A column's index as the top and bottom edges use it: the column's own, in
 * the last row of its half. */
static uint16_t column_in_last_row(uint16_t column)
{
    return column < FIELD_TILE_MAP_HALF_COLUMNS ? column : (uint16_t)(column + LAST_ROW_OF_HALF);
}

/* A row run: its tile map indexes from its start, from the row's start in
 * the other half, and from where it wraps; and the parts' sizes in bytes. */
typedef struct {
    uint16_t start, other_half, wrap;
} RowRun;

typedef struct {
    uint16_t to_half_end, across, wrapped;
} RowSizes;

/* A column run: its tile map indexes from its start, and from the top of the
 * map where it wraps; and the parts' sizes. */
typedef struct {
    uint16_t start, wrap;
} ColumnRun;

typedef struct {
    uint16_t to_map_end, from_map_top;
} ColumnSizes;

typedef struct {
    RowRun top_row, bottom_row;
    RowSizes row_sizes;
    ColumnRun left_column, right_column;
    ColumnSizes column_sizes;
} LayerEdges;

// $C0:813D
/* The view's top row, from a column left of the screen: its start, the row's
 * start in the other half, and the columns to its half's end and past it.
 * Where it starts on its half's last column, it is that column and a whole
 * row of the other half, and where the run wraps round is kept for the
 * uploads (every layer sends the last one kept). */
static void top_row(GameState *g, LayerEdges *e, uint16_t column, uint16_t row)
{
    uint16_t index = map_index(column, row);
    uint16_t to_half_end = (uint16_t)((column < FIELD_TILE_MAP_HALF_COLUMNS ? FIELD_TILE_MAP_HALF_COLUMNS : FIELD_TILE_MAP_COLUMNS) - column);
    bool wraps = to_half_end == 1;
    e->row_sizes = (RowSizes){ (uint16_t)(to_half_end * 2),
                               (uint16_t)((wraps ? FIELD_TILE_MAP_HALF_COLUMNS : FIELD_TILE_MAP_HALF_COLUMNS + 2 - to_half_end) * 2),
                               wraps ? 2 : 0 };
    if (wraps)
        set_field_layer_edge_wrap(g, (uint16_t)(index - (FIELD_TILE_MAP_HALF_COLUMNS - 1)));
    e->top_row = (RowRun){ index, other_half_row(column, row), field_layer_edge_wrap(g) };
}

// $C0:81A1
/* The row below the view's bottom, from the same column: its start, the
 * row's start in the other half, and 31 columns back. */
static void bottom_row(LayerEdges *e, uint16_t column, uint16_t row)
{
    uint16_t index = map_index(column, row);
    e->bottom_row = (RowRun){ index, other_half_row(column, row), (uint16_t)(index - (FIELD_TILE_MAP_HALF_COLUMNS - 1)) };
}

// $C0:81D6
/* The view's left column, from the row above the screen: its start, the
 * column in its half's last row, and the rows from it to the map's end and
 * before it. */
static void left_column(LayerEdges *e, uint16_t column, uint16_t row)
{
    e->left_column = (ColumnRun){ map_index(column, row), column_in_last_row(column) };
    e->column_sizes = (ColumnSizes){ (uint16_t)((FIELD_TILE_MAP_ROWS - row) * 2), (uint16_t)(row * 2) };
}

// $C0:8218
/* The column right of the view, from the same row: its start, and the
 * column in its half's last row. */
static void right_column(LayerEdges *e, uint16_t column, uint16_t row)
{
    e->right_column = (ColumnRun){ map_index(column, row), column_in_last_row(column) };
}

/* The runs as the uploads read them: VRAM addresses (the tile map index plus
 * the layer's base) and sizes, a word each, in the words layers.h names. */
static void set_word(GameState *g, void (*set)(GameState *, unsigned, unsigned, uint8_t), unsigned layer,
                     unsigned word, uint16_t v)
{
    set(g, layer, 2 * word, (uint8_t)v);
    set(g, layer, 2 * word + 1, (uint8_t)(v >> 8));
}

static void store_layer_edges(GameState *g, unsigned layer, uint16_t base, const LayerEdges *e)
{
    const uint16_t addresses[EDGE_ADDRESSES] = {
        [EDGE_TOP_ROW] = e->top_row.start,           [EDGE_TOP_ROW + 1] = e->top_row.other_half,
        [EDGE_TOP_ROW + 2] = e->top_row.wrap,        [EDGE_BOTTOM_ROW] = e->bottom_row.start,
        [EDGE_BOTTOM_ROW + 1] = e->bottom_row.other_half, [EDGE_BOTTOM_ROW + 2] = e->bottom_row.wrap,
        [EDGE_LEFT_COLUMN] = e->left_column.start,   [EDGE_LEFT_COLUMN + 1] = e->left_column.wrap,
        [EDGE_RIGHT_COLUMN] = e->right_column.start, [EDGE_RIGHT_COLUMN + 1] = e->right_column.wrap,
    };
    const uint16_t sizes[EDGE_SIZES] = {
        [EDGE_ROW_SIZES] = e->row_sizes.to_half_end,        [EDGE_ROW_SIZES + 1] = e->row_sizes.across,
        [EDGE_ROW_SIZES + 2] = e->row_sizes.wrapped,        [EDGE_COLUMN_SIZES] = e->column_sizes.to_map_end,
        [EDGE_COLUMN_SIZES + 1] = e->column_sizes.from_map_top,
    };
    for (unsigned k = 0; k < EDGE_ADDRESSES; k++)
        set_word(g, set_field_layer_edge_vram_byte, layer, k, (uint16_t)(addresses[k] + base));
    for (unsigned k = 0; k < EDGE_SIZES; k++)
        set_word(g, set_field_layer_edge_sizes_byte, layer, k, sizes[k]);
}

/* For the original's code and shadow mode (field/scratch.h): the last
 * layer's runs leave the right column's column and the row above the screen
 * they were taken from, that row's start, and the sizes of the top row's
 * wrap and the columns' parts, in cells of the layers' page. */
static void leave_for_original(GameState *g, uint16_t right_column_at, uint16_t row_above, const LayerEdges *last)
{
    original_layer_cell16(g, 0x52, (uint16_t)(row_above * FIELD_TILE_MAP_HALF_COLUMNS));
    original_layer_cell16(g, 0x62, right_column_at);
    original_layer_cell16(g, 0x64, row_above);
    original_layer_cell16(g, 0x6A, last->row_sizes.wrapped);
    original_layer_cell16(g, 0x6C, last->column_sizes.to_map_end);
    original_layer_cell16(g, 0x6E, last->column_sizes.from_map_top);
}

// $C0:7F9A
/* Every frame: each layer's edge runs around its position (the tile at the
 * screen's top left), which the uploads send when the layer has moved onto
 * them. */
void field_find_layer_edges(GameState *g)
{
    LayerEdges e;
    uint16_t right = 0, above = 0;
    for (unsigned layer = 0; layer < FIELD_LAYERS; layer++) {
        uint8_t x = field_layer_position_x(g, layer), y = field_layer_position_y(g, layer);
        uint16_t left_of_screen = (uint16_t)((x - 1) & COLUMN_MASK);
        above = (uint16_t)((y - 1) & ROW_MASK);
        right = (uint16_t)((x + FIELD_TILE_MAP_HALF_COLUMNS) & COLUMN_MASK);
        top_row(g, &e, left_of_screen, (uint16_t)(y & ROW_MASK));
        bottom_row(&e, left_of_screen, (uint16_t)((y + FIELD_SCREEN_ROWS) & ROW_MASK));
        left_column(&e, (uint16_t)(x & COLUMN_MASK), above);
        right_column(&e, right, above);
        store_layer_edges(g, layer, field_layer_map_base(g, layer), &e);
    }
    ORIGINAL_EFFECT(leave_for_original(g, right, above, &e));
}

static void put_the_layer_at_its_start(GameState *g, FieldLayer layer)
{
    set_field_layer_position_x(g, layer, 0);
    set_field_layer_position_y(g, layer, 0);
    set_field_layer_scroll_h(g, layer, 0);
    set_field_layer_scroll_v(g, layer, 0);
    TIMED_AS(g, 0xC07F9A, field_find_layer_edges(g));
}

// $C0:74D4, $C0:74E8, $C0:74F7
/* A layer put back at the top left of its tile map, its place and its scroll
 * both 0, as a whole map drawn afresh is sent from there; and every layer's
 * edges found again for it. */
void field_put_the_layer_at_its_start(GameState *g, FieldLayer layer)
{
    /* the original has a routine for each layer, which the work is timed as */
    switch (layer) {
    case FIELD_BG1: TIMED_AS(g, 0xC074D4, put_the_layer_at_its_start(g, FIELD_BG1)); break;
    case FIELD_BG2: TIMED_AS(g, 0xC074E8, put_the_layer_at_its_start(g, FIELD_BG2)); break;
    case FIELD_BG3: TIMED_AS(g, 0xC074F7, put_the_layer_at_its_start(g, FIELD_BG3)); break;
    default: break;
    }
}

/* ---- Scrolling -------------------------------------------------------------------------- */

enum { STEP_FORWARD = 0x10, STEP_BACK = 0xF0 };   /* a pixel a frame, in 4.4 */
enum { SCROLL_IDLE = 0, SCROLL_DONE = 0x02, ARRIVED_BOTH = 0x03 };   /* field_screen_scroll_state, _arrived */
enum { SHAKE_BACK = 0x01, SHAKE_Y = 0x02, SHAKE_X = 0x04 };   /* the frame counter's bits */

static uint8_t screen_edge(const GameState *g, unsigned axis)
{
    return axis == AXIS_X ? field_screen_left(g) : field_screen_top(g);
}

static uint8_t scroll_target(const GameState *g, unsigned axis)
{
    return axis == AXIS_X ? field_screen_scroll_target_x(g) : field_screen_scroll_target_y(g);
}

/* BG1's fine scroll on the axis, which says whether the screen is on a tile. */
static uint8_t fine_scroll(const GameState *g, unsigned axis)
{
    return (uint8_t)(axis == AXIS_X ? field_layer_scroll_h(g, FIELD_BG1) : field_layer_scroll_v(g, FIELD_BG1));
}

// $C0:91B0
/* One axis of the screen's pan toward its target tile: a pixel forward or
 * back each frame, and once on the tile, back until the fine scroll is on it
 * too, when the axis has arrived. True if it arrived now. */
static bool pan_toward_target(GameState *g, unsigned axis)
{
    uint8_t target = scroll_target(g, axis), at = screen_edge(g, axis);
    if (target == at && (fine_scroll(g, axis) & 0x0F) == 0) {
        set_field_screen_scroll_arrived(g, (uint8_t)(field_screen_scroll_arrived(g) | (1u << axis)));
        return true;
    }
    set_field_screen_scroll_step(g, axis, target > at ? STEP_FORWARD : STEP_BACK);
    return false;
}

/* The shake's offset on an axis this frame: a bit of the frame counter as
 * pixels, backward on odd frames. */
static uint8_t shake(uint8_t frames, uint8_t bit, uint8_t shift)
{
    uint8_t v = (uint8_t)((frames & bit) << shift);
    return (frames & SHAKE_BACK) ? (uint8_t)-v : v;
}

static uint8_t accumulator(const GameState *g, unsigned layer, unsigned axis)
{
    return axis == AXIS_X ? field_layer_scroll_accumulator_x(g, layer) : field_layer_scroll_accumulator_y(g, layer);
}

static void set_accumulator(GameState *g, unsigned layer, unsigned axis, uint8_t v)
{
    if (axis == AXIS_X)
        set_field_layer_scroll_accumulator_x(g, layer, v);
    else
        set_field_layer_scroll_accumulator_y(g, layer, v);
}

static uint8_t extra_speed(const GameState *g, unsigned layer, unsigned axis)
{
    return axis == AXIS_X ? field_layer_extra_speed_x(g, layer) : field_layer_extra_speed_y(g, layer);
}

/* The screen's own motion on the axis: its pan and its shake. */
static uint8_t screen_motion(const GameState *g, unsigned axis)
{
    uint8_t offset = axis == AXIS_X ? field_scroll_offset_x(g) : field_scroll_offset_y(g);
    return (uint8_t)(field_screen_scroll_step(g, axis) + offset);
}

// $C0:9238
/* BG1 moves with the screen; BG2 and BG3 at their own speeds, and with the
 * screen too unless they stay still; any of them faster for a while. The sums
 * are bytes and wrap. */
static void accumulate(GameState *g, unsigned layer, unsigned axis)
{
    uint8_t sum = (uint8_t)(accumulator(g, layer, axis) + extra_speed(g, layer, axis));
    if (layer == FIELD_BG1) {
        sum = (uint8_t)(sum + screen_motion(g, axis));
    } else {
        uint8_t speed = axis == AXIS_X ? field_layer_speed_x(g, layer - 1) : field_layer_speed_y(g, layer - 1);
        sum = (uint8_t)(sum + speed);
        if (!(field_layer_still(g) & FIELD_LAYER_BIT(layer)))
            sum = (uint8_t)(sum + screen_motion(g, axis));
    }
    set_accumulator(g, layer, axis, sum);
}

/* The accumulator's whole pixels become this frame's move (rounded toward
 * zero), and the fraction stays. */
static void take_pixels(GameState *g, unsigned layer, unsigned axis)
{
    unsigned k = layer * AXES + axis;
    int8_t v = (int8_t)accumulator(g, layer, axis);
    if (v >= 0) {
        set_field_layer_move_positive(g, k, (uint8_t)(v >> 4));
        set_field_layer_move_negative(g, k, 0);
    } else {
        set_field_layer_move_negative(g, k, (uint8_t)(-v >> 4));
        set_field_layer_move_positive(g, k, 0);
    }
    set_accumulator(g, layer, axis, field_scroll_fraction((int8_t)accumulator(g, layer, axis)));
}

// $C0:91AC
/* Every frame: the screen pans toward its target while the field has asked it
 * to, and is done once both axes arrive; it shakes while the setting is on
 * (but not on an axis where its edge is at the map's edge); each layer's
 * accumulators move, and their whole pixels are the layers' moves; and the
 * layers' extra speeds run out with their countdown. */
static void update_layer_scroll(GameState *g)
{
    if (field_screen_scroll_state(g) != SCROLL_IDLE) {
        pan_toward_target(g, AXIS_X);
        if (pan_toward_target(g, AXIS_Y) && field_screen_scroll_arrived(g) == ARRIVED_BOTH) {
            set_field_screen_scroll_state(g, SCROLL_DONE);
            set_field_screen_scroll_arrived(g, 0);
        }
    }
    if (field_screen_shake(g) != 0) {
        uint8_t frames = play_time_frames(g);
        set_field_scroll_offset_x(g, shake(frames, SHAKE_X, 2));
        set_field_scroll_offset_y(g, shake(frames, SHAKE_Y, 3));
        if (field_screen_left(g) == 0)
            set_field_scroll_offset_x(g, 0);
        if (field_screen_top(g) == 0)
            set_field_scroll_offset_y(g, 0);
    }
    for (unsigned layer = 0; layer < FIELD_LAYERS; layer++)
        for (unsigned axis = 0; axis < AXES; axis++)
            accumulate(g, layer, axis);
    for (unsigned layer = 0; layer < FIELD_LAYERS; layer++)
        for (unsigned axis = 0; axis < AXES; axis++)
            take_pixels(g, layer, axis);
    uint8_t time = field_layer_extra_speed_time(g);
    if (time != 0) {
        set_field_layer_extra_speed_time(g, (uint8_t)(time - 1));
        if (time == 1)
            for (unsigned layer = 0; layer < FIELD_LAYERS; layer++) {
                set_field_layer_extra_speed_x(g, layer, 0);
                set_field_layer_extra_speed_y(g, layer, 0);
            }
    }
}

/* ---- Unpacking a map's tile maps --------------------------------------------------------- */

/* Where the map loader leaves the map's decompressed data (field_map_data),
 * which the unpacking streams through the work RAM port. */
enum { MAP_DATA = 0xB506, MAP_DATA_BANK = 0 };

/* A tile map's size as the map gives it: the rows, and the columns of each.
 * The unpacking always writes a first row and a first column. */
typedef struct {
    uint8_t rows, columns;
} MapSize;

static MapSize layer_size(const GameState *g, unsigned layer)
{
    return (MapSize){ field_map_layer_sizes_height(g, layer), field_map_layer_sizes_width(g, layer) };
}

/* The next tile: false past the map's last row. */
static bool next_tile(MapSize size, MapPlace *p)
{
    if (++p->column < size.columns)
        return true;
    p->column = 0;
    return ++p->row < size.rows;
}

/* A layer's tile map, a byte a tile from the stream. */
static void unpack_layer_map(GameState *g, unsigned layer)
{
    MapSize size = layer_size(g, layer);
    MapPlace p = { 0, 0 };
    do
        set_field_layer_maps(g, layer, p.row, p.column, hw_port_read(g));
    while (next_tile(size, &p));
}

static void set_tile_attributes(GameState *g, MapPlace p, TileAttributes a)
{
    set_field_tile_properties(g, p.row, p.column, a.properties);
    set_field_tile_upper_attributes(g, p.row, p.column, a.upper);
    set_field_tile_lower_attributes(g, p.row, p.column, a.lower);
}

TileAttributes map_tile_attributes(const GameState *g, MapPlace p)
{
    return (TileAttributes){ field_tile_properties(g, p.row, p.column), field_tile_upper_attributes(g, p.row, p.column),
                             field_tile_lower_attributes(g, p.row, p.column) };
}

TileAttributes map_tile_attributes_past_the_map(const GameState *g, MapPlace p)
{
    return (TileAttributes){ .upper = field_tile_upper_attributes_unchecked(g, p.row, p.column),
                             .lower = field_tile_lower_attributes_unchecked(g, p.row, p.column) };
}

/* A tile's bytes from the stream, each written as it is read: a big map's
 * attribute tables reach the stream's bytes still to come, and change them
 * as the original does. */
static TileAttributes read_tile_attributes(GameState *g, MapPlace p, uint8_t properties)
{
    TileAttributes a = { .properties = properties };
    set_field_tile_properties(g, p.row, p.column, a.properties);
    a.upper = hw_port_read(g);
    set_field_tile_upper_attributes(g, p.row, p.column, a.upper);
    a.lower = hw_port_read(g);
    set_field_tile_lower_attributes(g, p.row, p.column, a.lower);
    return a;
}

enum { RUN = 0x80, RUN_OF_256 = 0x00 };

/* The last run the unpacking repeated, which it leaves in cells of the
 * layers' page for the original's code and shadow mode (field/scratch.h, "What
 * a wrapper can't work out"): written only by record_run, read only by
 * leave_for_original_unpack. */
static struct {
    bool had_run;
    TileAttributes last_run;
} unpack_leaves;

static void record_run(TileAttributes a)
{
    unpack_leaves.had_run = true;
    unpack_leaves.last_run = a;
}

/* For the original's code and shadow mode (field/scratch.h): the attributes'
 * size, the row the unpacking stopped on (past the last, which a map of no
 * rows puts at the second), and the last run's bytes. */
static void leave_for_original_unpack(GameState *g, MapSize size)
{
    original_layer_cell16(g, 0x52, (uint16_t)(size.rows << 8));
    original_layer_cell8(g, 0x54, size.columns);
    original_layer_cell8(g, 0x55, size.rows ? size.rows : 1);
    if (unpack_leaves.had_run) {
        original_layer_cell8(g, 0x58, unpack_leaves.last_run.properties);
        original_layer_cell8(g, 0x5A, unpack_leaves.last_run.upper);
        original_layer_cell8(g, 0x5C, unpack_leaves.last_run.lower);
    }
}

/* The attributes: three bytes a tile, over BG1's size. A first byte with RUN
 * set starts a run: the tile's bytes (RUN taken off) repeat for the count
 * after them, 0 meaning 256. */
static void unpack_attributes(GameState *g, MapSize size)
{
    MapPlace p = { 0, 0 };
    for (;;) {
        uint8_t first = hw_port_read(g);
        TileAttributes a = read_tile_attributes(g, p, (uint8_t)(first & ~RUN));
        if (first & RUN) {
            ORIGINAL_EFFECT(record_run(a));
            uint8_t count = hw_port_read(g);
            for (unsigned repeats = (count == RUN_OF_256 ? 0x100u : count) - 1; repeats != 0; repeats--) {
                if (!next_tile(size, &p))
                    return;
                set_tile_attributes(g, p, a);
            }
        }
        if (!next_tile(size, &p))
            return;
    }
}

// $C0:A521
/* When a map loads: unpack its tile maps from the decompressed map data,
 * through the work RAM port: BG1's and BG2's, BG3's where the map has one,
 * then the tile attributes. */
void field_unpack_the_tile_maps(GameState *g)
{
    hw_port_seek(g, MAP_DATA, MAP_DATA_BANK);
    unpack_layer_map(g, FIELD_BG1);
    unpack_layer_map(g, FIELD_BG2);
    if (field_map_flags(g) & FIELD_MAP_HAS_BG3)
        unpack_layer_map(g, FIELD_BG3);
    MapSize size = layer_size(g, FIELD_BG1);
    ORIGINAL_EFFECT(unpack_leaves.had_run = false);
    unpack_attributes(g, size);
    ORIGINAL_EFFECT(leave_for_original_unpack(g, size));
}

/* ---- Entries: on the layers' page ------------------------------------------------------- */

/* Whether a layer of that size fits the tables it unpacks into, 64 tiles
 * each way. A wider or taller map would run on into the tables' neighbours
 * (the next row's other layers, the other attribute table); no map the game
 * loads is one, so it is the original's to unpack. */
static bool fits_the_tables(MapSize s)
{
    return s.rows <= FIELD_LAYER_MAPS_ROWS && s.columns <= FIELD_LAYER_MAPS_COLUMNS;
}

/* The unpacking's guard: every layer it unpacks fits the tables, and a map
 * whose data can't come round the banks to the layers' page before it is
 * unpacked. The data starts at MAP_DATA in bank
 * $7E and the stream runs on through bank $7F and back into $7E; the port
 * leaves its cells on the layers' page when it is done, where the original
 * writes them as it goes, so a stream long enough to read them back would
 * differ. A tile takes a byte of each layer's map and at most four of
 * attributes (a run of one). The guard is a claim about every map the game
 * loads, which the movies check: none is turned away. */
enum { STREAM_REACHES_LAYERS_PAGE = 0x10000 - MAP_DATA + 0x10000 + 0x1D50 };

static uint32_t tiles(MapSize s) { return (uint32_t)(s.rows ? s.rows : 1) * (s.columns ? s.columns : 1); }

bool field_tile_maps_fit(const GameState *g)
{
    bool bg3 = field_map_flags(g) & FIELD_MAP_HAS_BG3;
    bool fits = fits_the_tables(layer_size(g, FIELD_BG1)) && fits_the_tables(layer_size(g, FIELD_BG2)) &&
                (!bg3 || fits_the_tables(layer_size(g, FIELD_BG3)));
    uint32_t bytes = tiles(layer_size(g, FIELD_BG1)) * 5 + tiles(layer_size(g, FIELD_BG2));
    if (bg3)
        bytes += tiles(layer_size(g, FIELD_BG3));
    return fits && bytes < STREAM_REACHES_LAYERS_PAGE;
}

bool field_unpack_stays_clear(Snes *s)
{
    if (!field_on_layers_page(s) || !m_bank_reaches_io(s))
        return false;
    vars_peek(true);
    bool ok = field_tile_maps_fit(state_of(s));
    vars_peek(false);
    return ok;
}

/* The overflow of BG3's right column's address, the last sum, is the one
 * thing its callers read; and it leaves A 8-bit, whatever it came with. */
bool field_layer_edges_overflow(const GameState *g)
{
    uint16_t base = field_layer_map_base(g, FIELD_BG3);
    uint16_t sum = (uint16_t)(field_layer_edge_vram_byte(g, FIELD_BG3, 2 * EDGE_RIGHT_COLUMN) |
                              field_layer_edge_vram_byte(g, FIELD_BG3, 2 * EDGE_RIGHT_COLUMN + 1) << 8);
    return field_add_overflows16((uint16_t)(sum - base), base);
}

void field_compute_layer_edges(Snes *s)
{
    GameState *g = state_of(s);
    field_find_layer_edges(g);
    m_set_flag(s, CPU_FLAG_V, field_layer_edges_overflow(g));
    m_set_flag(s, CPU_FLAG_M, true);
    m_rts(s);
}

/* It leaves the edges' overflow, as the edges' own entry does, and A
 * 8-bit. */
static void put_at_its_start(Snes *s, FieldLayer layer)
{
    GameState *g = state_of(s);
    field_put_the_layer_at_its_start(g, layer);
    m_set_flag(s, CPU_FLAG_V, field_layer_edges_overflow(g));
    m_set_flag(s, CPU_FLAG_M, true);
    m_rts(s);
}

void field_put_bg1_at_its_start(Snes *s) { put_at_its_start(s, FIELD_BG1); }
void field_put_bg2_at_its_start(Snes *s) { put_at_its_start(s, FIELD_BG2); }
void field_put_bg3_at_its_start(Snes *s) { put_at_its_start(s, FIELD_BG3); }

/* Nothing they leave in the registers is read after (the entries' don't-care
 * sets). */
void field_layer_scroll_updated(GameState *g) { update_layer_scroll(g); }

void field_update_layer_scroll(Snes *s)
{
    update_layer_scroll(state_of(s));
    m_rts(s);
}

void field_unpack_tile_maps(Snes *s)
{
    field_unpack_the_tile_maps(state_of(s));
    m_rts(s);
}
