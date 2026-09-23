/* Field mode: the three background layers' per-frame arithmetic, and their
 * tile maps' unpacking when a map loads.
 *
 * A layer's tile map in VRAM is 64 tiles wide and 32 high, in two halves of
 * 32 columns side by side ($400 words apart), indexed row * 32 + column. As a
 * layer scrolls, the deferred uploads send the column and row that come into
 * view at the screen's edges; field_compute_layer_edges works out where those
 * runs are for each layer's position, and how they split where a run crosses
 * from one half to the other. field_update_layer_scroll turns the screen's
 * scroll toward its target, the layers' own speeds and a few effects into the
 * pixels each layer moves this frame. These run on the layers' page. */
#include "field/layers.h"

#include "cpu_io.h"
#include "field/bridge.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

enum { LAYERS = 3 };
enum { HALF_COLUMNS = 32, MAP_COLUMNS = 64, MAP_ROWS = 32, SECOND_HALF = 0x0400, LAST_ROW_OF_HALF = 0x03E0 };
enum { COLUMN_MASK = MAP_COLUMNS - 1, ROW_MASK = MAP_ROWS - 1 };
enum { SCREEN_ROWS = 28 };   /* the row below the screen's is 28 on from its top */

/* ---- For the original's code and shadow mode (field/scratch.h) -------------------------- */

/* The edge helpers keep the column and row they were given, the row's start
 * and the counts they return in cells of the layers' page. */
static void leave_edge_helper(GameState *g, uint16_t column, uint16_t row)
{
    original_layer_cell16(g, 0x62, column);
    original_layer_cell16(g, 0x52, (uint16_t)(row * HALF_COLUMNS));
}

static void leave_edge_counts(GameState *g, uint16_t first, uint16_t rest)
{
    original_layer_cell16(g, 0x6C, first);
    original_layer_cell16(g, 0x6E, rest);
}

/* ---- The edges -------------------------------------------------------------------------- */

/* The tile map index of a column on a row. */
static uint16_t map_index(uint16_t column, uint16_t row)
{
    uint16_t start = (uint16_t)(row * HALF_COLUMNS);
    return column < HALF_COLUMNS ? (uint16_t)(start + column) : (uint16_t)(column - HALF_COLUMNS + start + SECOND_HALF);
}

/* Where the same row starts in the other half. */
static uint16_t other_half_row(uint16_t column, uint16_t row)
{
    uint16_t start = (uint16_t)(row * HALF_COLUMNS);
    return column < HALF_COLUMNS ? (uint16_t)(start + SECOND_HALF) : start;
}

/* A column's index as the top and bottom edges use it: the column's own, in
 * the last row of its half. */
static uint16_t column_in_last_row(uint16_t column)
{
    return column < HALF_COLUMNS ? column : (uint16_t)(column + LAST_ROW_OF_HALF);
}

/* One layer's edge runs: ten VRAM addresses (the tile map index plus the
 * layer's base) and five sizes, both as words, in the order the uploads read
 * them. */
typedef struct {
    uint16_t vram[10], sizes[5];
} LayerEdges;

// $C0:813D
/* The column entering at the left: its index, the row's start in the other
 * half, and the columns to its half's end and past it (doubled, as the
 * uploads count bytes). Where it is its half's last column, the run is the
 * one column and a whole row of the other half, and the wrap is marked. */
static void left_edge(GameState *g, LayerEdges *e, uint16_t column, uint16_t row)
{
    leave_edge_helper(g, column, row);
    uint16_t index = map_index(column, row);
    uint16_t to_half_end = (uint16_t)((column < HALF_COLUMNS ? HALF_COLUMNS : MAP_COLUMNS) - column);
    uint16_t past = (uint16_t)(HALF_COLUMNS + 2 - to_half_end);
    bool wraps = past >= HALF_COLUMNS + 1;
    e->vram[0] = index;
    e->vram[1] = other_half_row(column, row);
    e->sizes[0] = (uint16_t)(to_half_end * 2);
    e->sizes[1] = (uint16_t)((wraps ? HALF_COLUMNS : past) * 2);
    e->sizes[2] = wraps ? 2 : 0;
    if (wraps)
        set_field_layer_edge_wrap(g, (uint16_t)(index - (HALF_COLUMNS - 1)));
    e->vram[2] = field_layer_edge_wrap(g);
    leave_edge_counts(g, e->sizes[0], e->sizes[1]);
    original_layer_cell16(g, 0x6A, e->sizes[2]);
}

// $C0:81A1
/* The column entering at the right: its index, 31 columns back, and the
 * row's start in the other half. */
static void right_edge(GameState *g, LayerEdges *e, uint16_t column, uint16_t row)
{
    leave_edge_helper(g, column, row);
    uint16_t index = map_index(column, row);
    e->vram[3] = index;
    e->vram[4] = other_half_row(column, row);
    e->vram[5] = (uint16_t)(index - (HALF_COLUMNS - 1));
}

// $C0:81D6
/* The row entering at the top: its index, the column in its half's last
 * row, and the rows to the map's end and before the row (doubled). */
static void top_edge(GameState *g, LayerEdges *e, uint16_t column, uint16_t row)
{
    leave_edge_helper(g, column, row);
    original_layer_cell16(g, 0x64, row);
    e->vram[6] = map_index(column, row);
    e->vram[7] = column_in_last_row(column);
    uint16_t below = (uint16_t)(MAP_ROWS - row);
    e->sizes[3] = (uint16_t)(below * 2);
    e->sizes[4] = (uint16_t)((MAP_ROWS - below) * 2);
    leave_edge_counts(g, e->sizes[3], e->sizes[4]);
}

// $C0:8218
/* The row entering at the bottom: its index, and the column in its half's
 * last row. */
static void bottom_edge(GameState *g, LayerEdges *e, uint16_t column, uint16_t row)
{
    leave_edge_helper(g, column, row);
    e->vram[8] = map_index(column, row);
    e->vram[9] = column_in_last_row(column);
}

static void set_word(GameState *g, void (*set)(GameState *, unsigned, unsigned, uint8_t), unsigned layer,
                     unsigned word, uint16_t v)
{
    set(g, layer, 2 * word, (uint8_t)v);
    set(g, layer, 2 * word + 1, (uint8_t)(v >> 8));
}

// $C0:7F9A
/* Every layer's edge runs around its position: the columns one left of it and
 * 32 right of that, the rows one above it and 28 below that. */
static void compute_layer_edges(GameState *g)
{
    for (unsigned layer = 0; layer < LAYERS; layer++) {
        uint8_t x = field_layer_position_x(g, layer), y = field_layer_position_y(g, layer);
        uint16_t left = (uint16_t)((x - 1) & COLUMN_MASK), top = (uint16_t)((y - 1) & ROW_MASK);
        LayerEdges e;
        left_edge(g, &e, left, (uint16_t)(y & ROW_MASK));
        right_edge(g, &e, left, (uint16_t)((y + SCREEN_ROWS) & ROW_MASK));
        top_edge(g, &e, (uint16_t)(x & COLUMN_MASK), top);
        bottom_edge(g, &e, (uint16_t)((x + HALF_COLUMNS) & COLUMN_MASK), top);
        uint16_t base = field_layer_map_base(g, layer);
        for (unsigned k = 0; k < 10; k++)
            set_word(g, set_field_layer_edge_vram_byte, layer, k, (uint16_t)(e.vram[k] + base));
        for (unsigned k = 0; k < 5; k++)
            set_word(g, set_field_layer_edge_sizes_byte, layer, k, e.sizes[k]);
    }
}

/* ---- Scrolling -------------------------------------------------------------------------- */

enum { AXIS_X, AXIS_Y, AXES };
enum { STEP_FORWARD = 0x10, STEP_BACK = 0xF0 };   /* a 4.4 step of one pixel */
enum { SCROLL_IDLE = 0, SCROLL_DONE = 0x02, ARRIVED_BOTH = 0x03 };   /* field_screen_scroll_state, _arrived */
enum { STILL_BG2 = 0x02, STILL_BG3 = 0x04 };   /* field_layer_still */
enum { WOBBLE_NEGATIVE = 0x01, WOBBLE_Y = 0x02, WOBBLE_X = 0x04 };   /* the frame counter's bits */

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
    return (uint8_t)(axis == AXIS_X ? field_layer_scroll_h(g, 0) : field_layer_scroll_v(g, 0));
}

// $C0:91B0
/* One axis of the screen's scroll toward its target tile: a pixel forward or
 * back each frame, and once on the tile, back until the fine scroll is on it
 * too, when the axis is marked arrived. True if it arrived now. */
static bool scroll_toward_target(GameState *g, unsigned axis)
{
    uint8_t target = scroll_target(g, axis), at = screen_edge(g, axis);
    if (target == at && (fine_scroll(g, axis) & 0x0F) == 0) {
        set_field_screen_scroll_arrived(g, (uint8_t)(field_screen_scroll_arrived(g) | (1u << axis)));
        return true;
    }
    set_field_screen_scroll_step(g, axis, target > at ? STEP_FORWARD : STEP_BACK);
    return false;
}

/* A wobble offset from the frame counter: its bit `bit` shifted to a pixel
 * step, negated on odd frames. */
static uint8_t wobble(uint8_t frames, uint8_t bit, uint8_t shift)
{
    uint8_t v = (uint8_t)((frames & bit) << shift);
    return (frames & WOBBLE_NEGATIVE) ? (uint8_t)-v : v;
}

/* An accumulator's 8-bit sum of the cells that move it. */
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

/* The screen's own motion on the axis: its step and its wobble. */
static uint8_t screen_motion(const GameState *g, unsigned axis)
{
    uint8_t offset = axis == AXIS_X ? field_scroll_offset_x(g) : field_scroll_offset_y(g);
    return (uint8_t)(field_screen_scroll_step(g, axis) + offset);
}

// $C0:9238
/* BG1 moves with the screen; BG2 and BG3 at their own speeds, and with the
 * screen too unless they stay still. */
static void accumulate(GameState *g, unsigned layer, unsigned axis)
{
    uint8_t sum = (uint8_t)(accumulator(g, layer, axis) + extra_speed(g, layer, axis));
    if (layer == 0) {
        sum = (uint8_t)(sum + screen_motion(g, axis));
    } else {
        uint8_t speed = axis == AXIS_X ? field_layer_speed_x(g, layer - 1) : field_layer_speed_y(g, layer - 1);
        sum = (uint8_t)(sum + speed);
        if (!(field_layer_still(g) & (layer == 1 ? STILL_BG2 : STILL_BG3)))
            sum = (uint8_t)(sum + screen_motion(g, axis));
    }
    set_accumulator(g, layer, axis, sum);
}

/* A 4.4 accumulator's fraction, sign-extended to the byte. */
static uint8_t fraction_of(uint8_t v)
{
    if ((v & 0x0F) == 0)
        return 0;
    return (v & 0x80) ? (uint8_t)(v | 0xF0) : (uint8_t)(v & 0x0F);
}

/* The accumulator's whole pixels become this frame's move, and the fraction
 * stays. */
static void take_pixels(GameState *g, unsigned layer, unsigned axis)
{
    unsigned k = layer * AXES + axis;
    uint8_t v = accumulator(g, layer, axis);
    if (!(v & 0x80)) {
        set_field_layer_move_positive(g, k, (uint8_t)(v >> 4));
        set_field_layer_move_negative(g, k, 0);
    } else {
        set_field_layer_move_negative(g, k, (uint8_t)((uint8_t)-v >> 4));
        set_field_layer_move_positive(g, k, 0);
    }
    set_accumulator(g, layer, axis, fraction_of(accumulator(g, layer, axis)));
}

// $C0:91AC
/* This frame's scroll: the screen steps toward its target while it scrolls
 * (field_screen_scroll_state), done once both axes arrive; the wobble, when
 * set; then each layer's accumulators, whose whole pixels are the layers'
 * moves; and the extra speeds run out with their countdown. */
static void update_layer_scroll(GameState *g)
{
    if (field_screen_scroll_state(g) != SCROLL_IDLE) {
        scroll_toward_target(g, AXIS_X);
        if (scroll_toward_target(g, AXIS_Y) && field_screen_scroll_arrived(g) == ARRIVED_BOTH) {
            set_field_screen_scroll_state(g, SCROLL_DONE);
            set_field_screen_scroll_arrived(g, 0);
        }
    }
    if (field_setting_bc(g) != 0) {
        uint8_t frames = play_time_frames(g);
        set_field_scroll_offset_x(g, wobble(frames, WOBBLE_X, 2));
        set_field_scroll_offset_y(g, wobble(frames, WOBBLE_Y, 3));
        if (field_screen_left(g) == 0)
            set_field_scroll_offset_x(g, 0);
        if (field_screen_top(g) == 0)
            set_field_scroll_offset_y(g, 0);
    }
    for (unsigned layer = 0; layer < LAYERS; layer++)
        for (unsigned axis = 0; axis < AXES; axis++)
            accumulate(g, layer, axis);
    for (unsigned layer = 0; layer < LAYERS; layer++)
        for (unsigned axis = 0; axis < AXES; axis++)
            take_pixels(g, layer, axis);
    uint8_t time = field_layer_extra_speed_time(g);
    if (time != 0) {
        set_field_layer_extra_speed_time(g, (uint8_t)(time - 1));
        if (time == 1)
            for (unsigned layer = 0; layer < LAYERS; layer++) {
                set_field_layer_extra_speed_x(g, layer, 0);
                set_field_layer_extra_speed_y(g, layer, 0);
            }
    }
}

/* ---- Unpacking a map's tile maps --------------------------------------------------------- */

/* Where the map loader leaves the map's decompressed data (field_map_data),
 * which the unpacking streams through the work RAM port. */
enum { MAP_DATA = 0xB506, MAP_DATA_BANK = 0 };
enum { MAP_HAS_BG3 = 0x01 };   /* field_map_flags */

/* A tile map's size as the stream gives it: the rows, and the columns of each.
 * The unpacking always writes a first row and a first column. */
typedef struct {
    uint8_t rows, columns;
} MapSize;

/* For the original's code and shadow mode: the unpacking keeps the map's
 * size and the row it is on in cells of the layers' page, which the stream
 * it reads could reach. */
static void leave_map_size(GameState *g, MapSize size)
{
    original_layer_cell16(g, 0x52, (uint16_t)(size.rows << 8));
    original_layer_cell16(g, 0x54, size.columns);
}

static void leave_row(GameState *g, uint8_t row) { original_layer_cell8(g, 0x55, row); }

static void leave_run_byte(GameState *g, unsigned k, uint8_t v) { original_layer_cell8(g, (uint8_t)(0x58 + 2 * k), v); }

static MapSize layer_size(const GameState *g, unsigned layer)
{
    return (MapSize){ field_map_layer_sizes_height(g, layer), field_map_layer_sizes_width(g, layer) };
}

/* The tile after (row, column): false past the map's last row. */
static bool next_tile(GameState *g, MapSize size, uint8_t *row, uint8_t *column)
{
    if (++*column < size.columns)
        return true;
    *column = 0;
    ++*row;
    leave_row(g, *row);
    return *row < size.rows;
}

/* A layer's tile map, a byte a tile from the stream. */
static void unpack_layer_map(GameState *g, unsigned layer)
{
    MapSize size = layer_size(g, layer);
    leave_map_size(g, size);
    uint8_t row = 0, column = 0;
    do
        set_field_layer_maps_byte(g, layer, (unsigned)(row << 8 | column), hw_port_read(g));
    while (next_tile(g, size, &row, &column));
}

static void set_tile_attributes(GameState *g, uint8_t row, uint8_t column, const uint8_t bytes[3])
{
    unsigned at = (unsigned)(row << 8 | column);
    set_field_tile_properties_byte(g, at, bytes[0]);
    set_field_tile_attributes_byte(g, at, bytes[1]);
    set_field_tile_attributes_byte(g, at + MAP_COLUMNS, bytes[2]);
}

/* A tile's three bytes, each written as it is read: a big map's attribute
 * tables reach the stream's bytes still to come, and change them as the
 * original does (and a run's bytes are kept as they come, for the same
 * reason, where the stream has come round to the layers' page). */
static void read_tile_attributes(GameState *g, uint8_t row, uint8_t column, uint8_t bytes[3], bool run)
{
    unsigned at = (unsigned)(row << 8 | column);
    if (run)
        leave_run_byte(g, 0, bytes[0]);
    set_field_tile_properties_byte(g, at, bytes[0]);
    bytes[1] = hw_port_read(g);
    if (run)
        leave_run_byte(g, 1, bytes[1]);
    set_field_tile_attributes_byte(g, at, bytes[1]);
    bytes[2] = hw_port_read(g);
    if (run)
        leave_run_byte(g, 2, bytes[2]);
    set_field_tile_attributes_byte(g, at + MAP_COLUMNS, bytes[2]);
}

enum { RUN = 0x80, RUN_OF_256 = 0x00 };

/* The attributes: three bytes a tile, BG1's size. A first byte with RUN set
 * starts a run: its three bytes (RUN taken off) repeat for the count after
 * them, 0 meaning 256. */
static void unpack_attributes(GameState *g)
{
    MapSize size = layer_size(g, 0);
    leave_map_size(g, size);
    uint8_t row = 0, column = 0;
    for (;;) {
        uint8_t bytes[3];
        bytes[0] = hw_port_read(g);
        bool run = bytes[0] & RUN;
        bytes[0] &= (uint8_t)~RUN;
        read_tile_attributes(g, row, column, bytes, run);
        if (run) {
            uint8_t count = hw_port_read(g);
            for (unsigned left = count == RUN_OF_256 ? 0x100 : count; --left != 0;) {
                if (!next_tile(g, size, &row, &column))
                    return;
                set_tile_attributes(g, row, column, bytes);
            }
        }
        if (!next_tile(g, size, &row, &column))
            return;
    }
}

// $C0:A521
/* Unpack a map's tile maps from the decompressed map data, through the work
 * RAM port: BG1's and BG2's, BG3's where the map has one, then the tile
 * attributes. */
static void unpack_tile_maps(GameState *g)
{
    hw_port_seek(g, MAP_DATA, MAP_DATA_BANK);
    unpack_layer_map(g, 0);
    unpack_layer_map(g, 1);
    if (field_map_flags(g) & MAP_HAS_BG3)
        unpack_layer_map(g, 2);
    unpack_attributes(g);
}

/* ---- Entries: on the layers' page ------------------------------------------------------- */

/* Nothing else they leave in the registers is read after (the entries'
 * don't-care sets); the edges' entry leaves A 8-bit, whatever it came with. */
/* The overflow of BG3's bottom edge's address, the last sum, is the one
 * thing its callers read. */
void field_compute_layer_edges(Snes *s)
{
    GameState *g = state_of(s);
    compute_layer_edges(g);
    uint16_t base = field_layer_map_base(g, LAYERS - 1);
    uint16_t sum = (uint16_t)(field_layer_edge_vram_byte(g, LAYERS - 1, 16) | field_layer_edge_vram_byte(g, LAYERS - 1, 17) << 8);
    uint16_t index = (uint16_t)(sum - base);
    m_set_flag(s, CPU_FLAG_V, (~(index ^ base) & (index ^ sum) & 0x8000) != 0);
    m_set_flag(s, CPU_FLAG_M, true);
    m_rts(s);
}

void field_update_layer_scroll(Snes *s)
{
    update_layer_scroll(state_of(s));
    m_rts(s);
}

void field_unpack_tile_maps(Snes *s)
{
    unpack_tile_maps(state_of(s));
    m_rts(s);
}
