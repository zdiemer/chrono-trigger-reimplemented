/* The Day of Lavos's turning world.
 *
 * When the party loses to Lavos the game shows what that means instead of
 * the game-over screen: Lavos rising out of the 1999 A.D. world map, the
 * operators' room, and then the world itself. The 1000 A.D. world map's scene
 * lays the map flat in Mode 7, leaning back from the viewer, and turns it
 * while the needles fall. Its object's script calls the routines here:
 *
 * - **draw the world** ($C6:E7FA): the map as a Mode 7 picture
 *   (menu/map_picture.h), both its planes, the second over the first, built
 *   whole in bank $7F and sent to the plane's tiles from tile 1; before it the
 *   script has each map tile's colour sampled, and after it the picture laid
 *   out on the plane, both as the Epoch's map view does them (menu/epoch.c);
 * - **start the world turning** ($C6:E90F): the Mode 7 matrix put on four
 *   line-effect slots, one a value (a, b, c and d on slots 2-5), each reading
 *   a table of a value a line, so the matrix can change down the screen; the
 *   object's turn set up (its zoom, distances, pivot and reach); then the
 *   first frame;
 * - **turn the world** ($C6:E9F5), every frame: the matrix for each pair of
 *   lines (the picture rotated by the object's angle and scaled by its zoom
 *   over a distance that grows down the screen by the tilt, so the near edge
 *   is drawn larger: the perspective), into the tables not on the screen,
 *   which it then stages; and the plane's centre and scroll, so the screen's
 *   bottom middle shows the pivot moved by the reach along the turned
 *   picture;
 * - **turn it on** ($C6:EE23): the angle moved by the turn the frame's work
 *   handed on.
 *
 * The original keeps its working values in the mode's direct-page cells and
 * the CPU's multiplier and divider; this port works in C and leaves those
 * once, as scaffolding, at the end of each routine (the leave_* functions).
 * Two uses of the cells are more than that: the scene names the planes the
 * drawing draws in them, and the frame's work hands the turn on there to the
 * routine that turns the picture by it (menu_object_scratch's world_* cells). */
#include "menu/turning_world.h"

#include "charge.h"
#include "cpu_io.h"
#include "menu/line_effects.h"
#include "menu/map_picture.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/object.h"
#include "original_effects.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"
#include "wram.h"

/* ---- drawing the world ----------------------------------------------------------------------- */

/* The map's two planes, as the scene leaves them in the mode's cells for the
 * drawing: each its map (a group number a map tile, 96 a row) and its tile
 * groups, in bank $7E. The land is the map's plane 1, and the plane over it,
 * drawn over it, plane 0. */
typedef struct {
    uint16_t map, groups;
} Plane;

static Plane the_land(const GameState *g)
{
    return (Plane){ menu_object_scratch_world_land_map(g), menu_object_scratch_world_land_groups(g) };
}
static Plane over_the_land(const GameState *g)
{
    return (Plane){ menu_object_scratch_world_over_map(g), menu_object_scratch_world_over_groups(g) };
}

enum { MAP_BANK = 0x7E };

/* An address in the maps' bank: a plane's map or groups, and an index into
 * them, which the drawing's guard keeps inside the bank (above_low_ram). */
static uint32_t in_map_bank(uint16_t at, unsigned past) { return ((uint32_t)MAP_BANK << 16 | at) + past; }

static uint16_t map_word(const GameState *g, uint16_t at, unsigned past)
{
    return (uint16_t)(wram_read8(g, in_map_bank(at, past)) | wram_read8(g, in_map_bank(at, past + 1)) << 8);
}

/* The tile group a plane's map puts at a map tile, and where its record is. */
static uint16_t group_record(const GameState *g, Plane p, unsigned column, unsigned row)
{
    uint16_t row_at = (uint16_t)(p.map + row * MENU_MAP_COLUMNS);
    uint8_t group = wram_read8(g, in_map_bank(row_at, column));
    return (uint16_t)(p.groups + group * MENU_MAP_GROUP_BYTES);
}

/* A map tile of a plane: its tile group's record, and the group's two
 * right-hand words, which the picture takes. */
typedef struct {
    uint16_t record, top, bottom;
} MapTile;

static MapTile map_tile(const GameState *g, Plane p, unsigned column, unsigned row)
{
    MapTile tile = { .record = group_record(g, p, column, row) };
    tile.top = map_word(g, tile.record, MENU_MAP_TOP_RIGHT);
    tile.bottom = map_word(g, tile.record, MENU_MAP_BOTTOM_RIGHT);
    return tile;
}

/* The last map tile the drawing looks at: the plane over the land's, at the
 * map's bottom right. */
static MapTile last_map_tile(const GameState *g, Plane over) { return map_tile(g, over, MENU_MAP_COLUMNS - 1, MENU_MAP_ROWS - 1); }

static void put_pixel(GameState *g, uint16_t at, MenuMapPixel p)
{
    if (p.drawn)
        set_menu_world_picture(g, at, p.pixel);
}

/* A map tile's two pixels from one plane, at `at` in the picture and the
 * picture's next line. */
static void draw_map_tile(GameState *g, Plane p, unsigned column, unsigned row, uint16_t at)
{
    MapTile tile = map_tile(g, p, column, row);
    put_pixel(g, at, menu_map_pixel(g, tile.top, menu_map_palette(tile.top)));
    put_pixel(g, (uint16_t)(at + 8), menu_map_pixel(g, tile.bottom, menu_map_palette(tile.bottom)));
}

/* The picture's place in VRAM: the plane's tiles from tile 1, the pixels in
 * the high bytes of its words. */
enum { FIRST_TILE = 1 };
static const VidVramPort after_high = { .step = 1, .after_high = true };

/* What drawing leaves in the mode's cells: the last tile's palettes, the
 * picture's place past its end, the rows' and columns' counts at their ends,
 * the last group record and the two maps a whole map on. */
static void leave_drawn(GameState *g, Plane land, Plane over)
{
    enum { TOP_PALETTE = 0x00, BOTTOM_PALETTE = 0x01, PICTURE_AT = 0x05, PICTURE_BANK = 0x07, ROWS = 0x08,
           COLUMNS = 0x0A, RECORD = 0x0E };
    MapTile last = last_map_tile(g, over);
    menu_object_leave_cell8(g, TOP_PALETTE, menu_map_palette(last.top));
    menu_object_leave_cell8(g, BOTTOM_PALETTE, menu_map_palette(last.bottom));
    menu_object_leave_cell16(g, PICTURE_AT, (uint16_t)(RAM_MENU_WORLD_PICTURE + MENU_WORLD_PICTURE_COUNT));
    menu_object_leave_cell8(g, PICTURE_BANK, (uint8_t)(RAM_MENU_WORLD_PICTURE >> 16));
    menu_object_leave_cell16(g, ROWS, MENU_MAP_ROWS);
    menu_object_leave_cell16(g, COLUMNS, MENU_MAP_COLUMNS);
    menu_object_leave_cell16(g, RECORD, last.record);
    set_menu_object_scratch_world_land_map(g, (uint16_t)(land.map + MENU_MAP_ROWS * MENU_MAP_COLUMNS));
    set_menu_object_scratch_world_over_map(g, (uint16_t)(over.map + MENU_MAP_ROWS * MENU_MAP_COLUMNS));
}

// $C6:E7FA, $C6:E8C7
/* Draw the map, the land and the plane over it, into the picture cleared,
 * and send it to VRAM. */
static void draw_the_world(GameState *g, Platform *pf, Plane land, Plane over)
{
    wram_fill8(g, RAM_MENU_WORLD_PICTURE, MENU_WORLD_PICTURE_COUNT, 0);
    ORIGINAL_EFFECT(charge_block_move(g, MENU_WORLD_PICTURE_COUNT));
    for (unsigned row = 0; row < MENU_MAP_ROWS; row++) {
        uint16_t tile_row = (uint16_t)(row / MENU_MAP_ROWS_A_TILE_ROW * MENU_MAP_PICTURE_TILE_ROW);
        for (unsigned column = 0; column < MENU_MAP_COLUMNS; column++) {
            uint16_t at = (uint16_t)(tile_row + menu_map_picture_offset(column, row));
            draw_map_tile(g, land, column, row, at);
            draw_map_tile(g, over, column, row, at);
        }
    }
    vid_send_vram_half(pf, PF_AT(0xC6E898), FIRST_TILE * MENU_MODE7_TILE_PIXELS, PF_GAME(RAM_MENU_WORLD_PICTURE),
                       MENU_WORLD_PICTURE_COUNT, VID_VRAM_HIGH_BYTES, &after_high);
    ORIGINAL_EFFECT(leave_drawn(g, land, over));
}

/* ---- the turn ------------------------------------------------------------------------------ */

/* The matrix's four values, a to d, on line-effect slots 2 to 5, each slot's
 * channel the same as its number; and the screen in pairs of lines, a value a
 * pair. */
enum { SLOT_A = 2, SLOT_B = 3, SLOT_C = 4, SLOT_D = 5, MATRIX_SLOTS = 0x3C };
enum { LINE_PAIRS = MENU_LINE_TABLE_LINES / 2, MIDDLE_PAIR = LINE_PAIRS / 2 };

typedef struct {
    int16_t a, b, c, d;
} Matrix;

/* The turn's sine and cosine, scaled by the zoom and taken apart into their
 * sizes and signs, as the matrix is worked out from them. */
typedef struct {
    uint16_t cos, sin;
    bool cos_below, sin_below;
} Turn;

static Turn turn_at(const GameState *g, uint16_t angle, uint8_t zoom)
{
    int16_t cos = menu_cosine(g, angle), sin = menu_sine(g, angle);
    uint8_t cos_size = (uint8_t)(cos < 0 ? -cos : cos), sin_size = (uint8_t)(sin < 0 ? -sin : sin);
    return (Turn){ (uint16_t)(zoom * cos_size), (uint16_t)(zoom * sin_size), cos < 0, sin < 0 };
}

/* The matrix of lines at a distance `across` and `down`: the turn over the
 * distances (menu_signed_quotient). */
static Matrix matrix_at(Turn t, uint8_t across, uint8_t down)
{
    return (Matrix){ menu_signed_quotient(t.cos, across, t.cos_below), menu_signed_quotient(t.sin, across, t.sin_below),
                     menu_signed_quotient(t.sin, down, !t.sin_below), menu_signed_quotient(t.cos, down, t.cos_below) };
}

/* The distances grow down the screen by the tilt's step a pair of lines,
 * from where they reach the object's at the middle pair. A distance is 8.8
 * fixed point, of which the matrix takes the whole part. */
static uint16_t distance_at_the_top(uint8_t middle, uint8_t step) { return (uint16_t)((middle << 8) - step * MIDDLE_PAIR); }

/* The line tables are in bank $7E. */
enum { LINE_TABLE_BANK = 0x7E };

static void put_line_pair(GameState *g, uint16_t table, unsigned pair, uint16_t v)
{
    for (unsigned line = 2 * pair; line < 2 * pair + 2; line++)
        wram_write16(g, (uint32_t)LINE_TABLE_BANK << 16 | (uint16_t)(table + 2 * line), v);
}

static uint16_t size_of(int16_t v) { return v < 0 ? (uint16_t)(0 - (uint16_t)v) : (uint16_t)v; }

/* How far along a matrix value the reach takes the pivot: the reach in 256ths
 * of the value's size, rounded to the nearer, with its sign. */
static int16_t reach_along(int16_t v, uint8_t reach)
{
    uint16_t r = (uint16_t)((reach * (uint32_t)size_of(v) + 0x80) >> 8);
    return (int16_t)(v < 0 ? (uint16_t)(0 - r) : r);
}

/* The screen's bottom middle, where the pivot is shown. */
enum { SCREEN_MIDDLE = 0x80, SCREEN_BOTTOM = 0xE0 };

/* The matrix tables a frame draws into, the other pair of the one the screen
 * shows, by the parity of the frames turned. */
static unsigned tables_drawn_into(const GameState *g) { return (menu_world_frames(g) & 1) ? 0 : 1; }

static unsigned tables_this_frame(GameState *g)
{
    set_menu_world_frames(g, (uint8_t)(menu_world_frames(g) + 1));
    return tables_drawn_into(g);
}

/* What a frame leaves in the mode's cells and the CPU's arithmetic: the
 * middle pair's matrix values' sizes and signs, the reach (as a word, with
 * the byte after it), the pivot's two moves, the line pairs' count run out,
 * the tables' place past the last pair (the original indexes the four tables
 * from slot 2's first) and the tilt's step; the divider as it worked out the
 * middle pair's d, and then the multiplier as it worked out the move down. */
typedef struct {
    Matrix middle;
    int16_t moved_x, moved_y;
    uint8_t reach, step, distance_down;
    uint16_t cos_zoomed;
} Worked;

static void leave_worked(GameState *g, MenuObject o, const Worked *w)
{
    enum { SIZES = 0x00, REACH = 0x0A, MOVED_X = 0x0C, MOVED_Y = 0x0E, SIGNS = 0x10, PAIRS_LEFT = 0x12,
           TABLE_AT = 0x13, STEP = 0x16 };
    const int16_t m[4] = { w->middle.a, w->middle.b, w->middle.c, w->middle.d };
    uint16_t signs = 0;
    for (unsigned i = 0; i < 4; i++) {
        menu_object_leave_cell16(g, SIZES + 2 * i, size_of(m[i]));
        signs |= (uint16_t)((m[i] < 0) << i);
    }
    uint16_t past_the_pairs = (uint16_t)(menu_line_values(SLOT_A, tables_drawn_into(g)) - (uint16_t)RAM_MENU_SLOT2_LINE_VALUES
                                         + 2 * MENU_LINE_TABLE_LINES);
    menu_object_leave_cell16(g, REACH, (uint16_t)(w->reach | menu_object_slots_world_reach_high(g, o) << 8));
    menu_object_leave_cell16(g, MOVED_X, (uint16_t)w->moved_x);
    menu_object_leave_cell16(g, MOVED_Y, (uint16_t)w->moved_y);
    menu_object_leave_cell16(g, SIGNS, signs);
    menu_object_leave_cell8(g, PAIRS_LEFT, 0);
    menu_object_leave_cell16(g, TABLE_AT, past_the_pairs);
    menu_object_leave_cell16(g, STEP, w->step);
    hw_set_dividend(g, w->cos_zoomed);
    hw_divide_by(g, w->distance_down);
    /* after the divider, whose remainder the product's register shares */
    hw_set_multiplicand(g, w->reach);
    (void)hw_multiply_by(g, (uint8_t)(size_of(w->middle.d) >> 8));
}

// $C6:E9F5, $C6:EDE1
/* A frame of the turn. */
static void turn_a_frame(GameState *g, MenuObject o)
{
    Worked w = { .reach = menu_object_slots_world_reach(g, o), .distance_down = menu_object_slots_world_distance_down(g, o) };
    Turn t = turn_at(g, menu_object_slots_phase(g, o), menu_object_slots_world_zoom(g, o));
    w.cos_zoomed = t.cos;
    unsigned which = tables_this_frame(g);
    const uint8_t slots[4] = { SLOT_A, SLOT_B, SLOT_C, SLOT_D };
    uint16_t tables[4];
    for (unsigned i = 0; i < 4; i++) {
        tables[i] = menu_line_values(slots[i], which);
        menu_show_line_values(g, slots[i], tables[i]);
    }

    w.step = menu_world_tilt_step(g, menu_object_slots_world_tilt(g, o));
    uint8_t across_middle = menu_object_slots_world_distance_across(g, o);
    uint16_t across = distance_at_the_top(across_middle, w.step), down = distance_at_the_top(w.distance_down, w.step);
    for (unsigned pair = 0; pair < LINE_PAIRS; pair++) {
        Matrix m = matrix_at(t, (uint8_t)(across >> 8), (uint8_t)(down >> 8));
        put_line_pair(g, tables[0], pair, (uint16_t)m.a);
        put_line_pair(g, tables[1], pair, (uint16_t)m.b);
        put_line_pair(g, tables[2], pair, (uint16_t)m.c);
        put_line_pair(g, tables[3], pair, (uint16_t)m.d);
        across = (uint16_t)(across + w.step);
        down = (uint16_t)(down + w.step);
    }

    w.middle = matrix_at(t, across_middle, w.distance_down);
    w.moved_x = reach_along(w.middle.b, w.reach);
    w.moved_y = reach_along(w.middle.d, w.reach);
    uint16_t centre_x = (uint16_t)(menu_object_slots_world_pivot_x(g, o) + w.moved_x);
    uint16_t centre_y = (uint16_t)(menu_object_slots_world_pivot_y(g, o) + w.moved_y);
    set_menu_object_shadows_mode7_center(g, (uint32_t)centre_y << 16 | centre_x);
    set_menu_object_shadows_bg1_scroll_x(g, (uint16_t)(centre_x - SCREEN_MIDDLE));
    set_menu_object_shadows_bg1_scroll_y(g, (uint16_t)(centre_y - SCREEN_BOTTOM));

    set_menu_object_scratch_world_turn_handed_on(g, menu_object_slots_world_turn(g, o));
    ORIGINAL_EFFECT(leave_worked(g, o, &w));
}

/* How the turn starts: unturned, at the zoom, distances, pivot and reach the
 * scene opens with, level, and still. */
enum { START_ZOOM = 0x80, START_ACROSS = 0xA0, START_DOWN = 0x60, START_PIVOT_X = 0x40, START_PIVOT_Y = 0x70,
       START_REACH = 0x70 };

/* The original keeps the zoom, the distances and the reach as words, and
 * clears the bytes after them as it sets them. The turn reads none of them,
 * but the slots' bytes are other state too, which code after it reads (the
 * scene's script runner reads slot 5's as the menus' VRAM queue, the
 * overworld's followers theirs in the mode's other scenes), so the clears are
 * kept plainly. */
static void clear_after_the_turns_bytes(GameState *g, MenuObject o)
{
    set_menu_object_slots_world_zoom_high(g, o, 0);
    set_menu_object_slots_world_distance_across_high(g, o, 0);
    set_menu_object_slots_world_distance_down_high(g, o, 0);
    set_menu_object_slots_world_reach_high(g, o, 0);
}

// $C6:E90F
/* Put the matrix on its four slots and set the turn up. */
static void start_the_world_turning(GameState *g, Platform *pf, MenuObject o)
{
    for (uint8_t slot = SLOT_A; slot <= SLOT_D; slot++)
        menu_lay_out_line_slot(g, slot);
    menu_program_line_channel(pf, 0xC6E949, SLOT_A, VID_LINE_MODE7_A, menu_line_slot_table(SLOT_A));
    menu_program_line_channel(pf, 0xC6E94B, SLOT_B, VID_LINE_MODE7_B, menu_line_slot_table(SLOT_B));
    menu_program_line_channel(pf, 0xC6E94D, SLOT_C, VID_LINE_MODE7_C, menu_line_slot_table(SLOT_C));
    menu_program_line_channel(pf, 0xC6E94F, SLOT_D, VID_LINE_MODE7_D, menu_line_slot_table(SLOT_D));
    menu_turn_on_line_slots(g, MATRIX_SLOTS);
    set_menu_object_slots_phase(g, o, 0);
    set_menu_object_slots_world_zoom(g, o, START_ZOOM);
    set_menu_object_slots_world_distance_across(g, o, START_ACROSS);
    set_menu_object_slots_world_distance_down(g, o, START_DOWN);
    set_menu_object_slots_world_pivot_x(g, o, START_PIVOT_X);
    set_menu_object_slots_world_pivot_y(g, o, START_PIVOT_Y);
    set_menu_object_slots_world_turn(g, o, 0);
    set_menu_object_slots_world_reach(g, o, START_REACH);
    set_menu_object_slots_world_tilt(g, o, 0);
    clear_after_the_turns_bytes(g, o);
}

/* ---- the entries ------------------------------------------------------------------------------ */

/* The drawing keeps its working values in the mode's first cells ($00-$1A)
 * and its return on the stack, which the original reads the maps and groups
 * past: a plane in low RAM could be read changing under it. The scene's
 * planes are well above it (the land's at $5800 and $3800, the other's at
 * $4000 and $3000). */
enum { LOW_RAM_END = 0x2000, MAP_BYTES = MENU_MAP_ROWS * MENU_MAP_COLUMNS + 1, GROUPS_BYTES = 0x100 * MENU_MAP_GROUP_BYTES };

static bool above_low_ram(uint16_t at, uint32_t bytes) { return at >= LOW_RAM_END && (uint32_t)at + bytes <= 0x10000; }

bool menu_world_can_be_drawn(Snes *s)
{
    if (m_direct_page(s) != 0x0000 || !m_bank_reaches_low_ram(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    const Plane planes[2] = { the_land(g), over_the_land(g) };
    bool ok = true;
    for (unsigned i = 0; i < 2; i++)
        ok = ok && above_low_ram(planes[i].map, MAP_BYTES) && above_low_ram(planes[i].groups, GROUPS_BYTES);
    vars_peek(false);
    return ok;
}

/* The turn reaches the CPU's multiplier and divider through a data bank of
 * $00, on the mode's page, for the object running. */
bool menu_world_can_be_turned(Snes *s)
{
    if (m_direct_page(s) != 0x0000 || s->cpu.db != 0x00)
        return false;
    vars_peek(true);
    bool ok = menu_object_address_names_one(menu_object_current(state_of(s)));
    vars_peek(false);
    return ok;
}

/* A frame reads its tilt's step from the tilts' table, so the tilt is one of
 * them, as every one the scene sets is. */
bool menu_world_can_turn_a_frame(Snes *s)
{
    if (!menu_world_can_be_turned(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = menu_object_slots_world_tilt(g, menu_object_running(g)) < MENU_WORLD_TILTS;
    vars_peek(false);
    return ok;
}

/* After them the script reads only X and the carry. */
static void leave_for_the_script(Snes *s, uint16_t x, bool carry)
{
    s->cpu.x = x;
    m_set_flag(s, CPU_FLAG_C, carry);
    m_rtl(s);
}

// $C6:E7FA
/* X is the last tile the drawing looked at, the bottom right tile of the last
 * map tile's group; the carry is set, its rows' count having reached its end;
 * the accumulator is 8 bits wide, as it came. */
void menu_object_draw_the_world(Snes *s)
{
    GameState *g = state_of(s);
    Plane land = the_land(g), over = over_the_land(g);
    draw_the_world(g, m_platform(s), land, over);
    leave_for_the_script(s, last_map_tile(g, over).bottom & MENU_MAP_TILE_NUMBER, true);
}

/* The turn's routines come back 16 bits wide, with X on the object and the
 * carry set where the plane's centre (its place down, the high half of the
 * shadow) is past the screen's bottom. */
static void leave_turned(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t centre_y = (uint16_t)(menu_object_shadows_mode7_center(g) >> 16);
    m_rep(s, CPU_FLAG_M);
    leave_for_the_script(s, menu_object_current(g), centre_y >= SCREEN_BOTTOM);
}

// $C6:E90F
void menu_object_start_the_world_turning(Snes *s)
{
    GameState *g = state_of(s);
    MenuObject o = menu_object_running(g);
    start_the_world_turning(g, m_platform(s), o);
    NATIVE_TIME_AS(s, 0xC6E9F5, turn_a_frame(g, o));
    leave_turned(s);
}

// $C6:E9F5
void menu_object_turn_the_world(Snes *s)
{
    GameState *g = state_of(s);
    turn_a_frame(g, menu_object_running(g));
    leave_turned(s);
}

// $C6:EE23
/* The angle moved by the turn handed on; the carry is the add's. */
void menu_object_turn_the_world_on(Snes *s)
{
    GameState *g = state_of(s);
    MenuObject o = menu_object_running(g);
    uint16_t angle = menu_object_slots_phase(g, o), turn = menu_object_scratch_world_turn_handed_on(g);
    set_menu_object_slots_phase(g, o, (uint16_t)(angle + turn));
    m_rep(s, CPU_FLAG_M);
    leave_for_the_script(s, menu_object_current(g), angle + turn > 0xFFFF);
}
