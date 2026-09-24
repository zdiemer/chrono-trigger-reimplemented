/* The Epoch: its two Mode 7 screens, and where it may fly.
 *
 * Both screens run in the sprite-object mode (menu/object.h): each blanks
 * the screen, builds a Mode 7 plane, starts the objects that drive it, and
 * then runs frame after frame until the menus' mode leaves it.
 *
 * - **The map view** ($C2:631F), opened while the Epoch flies. It draws the
 *   world map the party is on as a small Mode 7 picture, a pixel a map tile
 *   across and two down (draw_map_picture), and a cursor object walks a fixed
 *   path out and back, turning and zooming the picture as it goes
 *   (map_view_cursor). Markers show where the party stands, where the Epoch is
 *   parked and where it would land; the fixed colour's blue steps down the
 *   screen behind it (map_view_gradient).
 * - **The time dial** ($C2:6A34), where the Epoch travels in time. The gate is
 *   a triangle cut out of the screen by window 1, whose edges are drawn line
 *   by line as the gate turns (draw_gate_window); the picture behind it spins
 *   through a Mode 7 matrix that changes every line (gate_spin). The player
 *   turns the dial through seven periods, each a location to travel to, and
 *   seven digit objects show the period the dial stands at.
 *
 * What they put on the screen goes through the video service. Their objects'
 * state is kept in their slots (menu/object.h), and their per-line tables in
 * the mode's line-effect slots (menu/line_effects.h). The original also leaves
 * working values in the mode's direct-page cells, the multiplier, the divider
 * and the work RAM port; this port computes in C and leaves those as
 * scaffolding, once, at the end of each routine (the leave_* functions).
 *
 * The screens call routines of the menus that are still the original's
 * (clearing VRAM, resetting the mode, waiting for a frame, the graphics
 * loader and the decompressor); those calls are made from bridges on the
 * machine, at the original's call sites. */
#include "menu/epoch.h"

#include "charge.h"
#include "cpu_io.h"
#include "menu/line_effects.h"
#include "menu/menu_data.h"
#include "menu/object.h"
#include "menu/sprites.h"
#include "menu/vram.h"
#include "original_effects.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "sound/sound.h"
#include "vars.h"
#include "wram.h"

/* ---- What both screens use ---------------------------------------------------------------- */

/* A sine wave of 1024 steps a turn, its values -255 to 255: the menus' wave
 * table's size at the step, below zero in the second half of the turn. */
enum { SINE_STEPS = 0x400, HALF_TURN = 0x200, QUARTER_TURN = 0x100, EIGHTH_TURN = 0x80 };

static int16_t sine(const GameState *g, uint16_t phase)
{
    uint16_t step = phase & (SINE_STEPS - 1);
    int16_t size = menu_wave_size(g, step);
    return step >= HALF_TURN ? (int16_t)-size : size;
}
static int16_t cosine(const GameState *g, uint16_t phase) { return sine(g, (uint16_t)(phase + QUARTER_TURN)); }

/* The mode's work RAM bank, where its buffers are. */
enum { WORK_RAM_BANK = 0x7E, EXTRA_RAM_BANK = 0x7F };
static uint32_t in_work_ram(uint16_t at) { return (uint32_t)WORK_RAM_BANK << 16 | at; }

/* The Mode 7 layer's pixels and tile numbers share VRAM words: a tile's 64
 * pixels are a byte each in the high halves of its 64 words, and the plane's
 * 128 by 128 tile numbers the low halves of the first 16384. */
enum { MODE7_TILE_PIXELS = 64, MODE7_MAP_ROW = 128 };

/* How the screens' uploads program the channel on the SNES (channel 7, in
 * the order of the steps), and how VRAM steps as they go. */
enum { EPOCH_DMA = 7, DMA_ONE_REGISTER = 0x00, DMA_WORDS = 0x01 };
static const VidVramPort after_high = { .step = 1, .after_high = true };
static const VidVramPort after_low = { .step = 1, .after_high = false };
static const uint8_t upload_steps[] = {
    VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_VRAM_PORT, VID_STEP_CONTROL,
    VID_STEP_DEST,      VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK,
    VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,   VID_STEP_START,     VID_STEP_END,
};
static const VidSnesXfer word_upload = { EPOCH_DMA, DMA_WORDS, upload_steps, &after_high };
static const VidSnesXfer high_byte_upload = { EPOCH_DMA, DMA_ONE_REGISTER, upload_steps, &after_high };
static const VidSnesXfer low_byte_upload = { EPOCH_DMA, DMA_ONE_REGISTER, upload_steps, &after_low };

/* The mode's frame: bit 0 has the vblank handler send the frame and run the
 * objects, bit 1 send the palette too. */
enum { SEND_FRAME_AND_PALETTE = 0x03 };

/* The screens' display, as the mode's shadows hold it. */
enum { MODE_7 = 0x07, MENU_BG_MODE = 0x09 };

static void set_mode7_matrix(GameState *g, int16_t a, int16_t b, int16_t c, int16_t d)
{
    const int16_t m[4] = { a, b, c, d };
    for (unsigned i = 0; i < 4; i++) {
        set_menu_object_shadows_mode7_matrix_byte(g, 2 * i, (uint8_t)m[i]);
        set_menu_object_shadows_mode7_matrix_byte(g, 2 * i + 1, (uint8_t)((uint16_t)m[i] >> 8));
    }
}

/* The original's direct-page cells ($00-$1B), as the leave_* functions put
 * what it leaves there. */
static void leave_cell8(GameState *g, unsigned cell, uint8_t v) { set_menu_object_scratch_bytes_byte(g, cell, v); }
static void leave_cell16(GameState *g, unsigned cell, uint16_t v)
{
    leave_cell8(g, cell, (uint8_t)v);
    leave_cell8(g, cell + 1, (uint8_t)(v >> 8));
}

/* The world maps count from the first's location. */
static uint16_t world_map(const GameState *g) { return (uint16_t)((location_id(g) & 0x01FF) - MENU_FIRST_WORLD_MAP); }
static uint16_t location_here(const GameState *g) { return location_id(g) & 0x01FF; }

/* ---- The map view: its picture -------------------------------------------------------------- */

/* A world map tile's colour on the view is the colour of one of its pixels,
 * the fourth across and third down: its four bitplanes' bits there, the
 * fourth plane's the highest. */
enum { TILE_BYTES = 32, SAMPLED_BIT = 0x10 };
static const uint8_t sampled_planes[4] = { 0x15, 0x14, 0x05, 0x04 };
enum { WORLD_MAP_TILES = MENU_WORLD_MAP_TILES_COUNT };

// $C2:6400, $C2:6453
/* Every world map tile's colour on the view. The original writes them
 * through the work RAM port. */
static void sample_map_colours(GameState *g)
{
    for (unsigned tile = 0; tile < WORLD_MAP_TILES; tile++) {
        uint8_t colour = 0;
        for (unsigned plane = 0; plane < 4; plane++)
            colour = (uint8_t)(colour << 1 | ((menu_world_map_tiles_byte(g, tile, sampled_planes[plane]) & SAMPLED_BIT) != 0));
        set_menu_map_colours(g, tile, colour);
    }
    ORIGINAL_EFFECT(hw_port_seek(g, (uint16_t)(RAM_MENU_MAP_COLOURS + WORLD_MAP_TILES), WORK_RAM_BANK));
}

/* The picture is 96 pixels across, a map tile's column each, and 128 down,
 * two a map row; it is built in the mode's upload buffer four map rows (a
 * row of Mode 7 tiles, twelve) at a time and sent to the plane's tiles from
 * tile 1. */
enum {
    MAP_COLUMNS = 96, MAP_ROWS = 64,
    PICTURE_TILES_ACROSS = MAP_COLUMNS / 8, PICTURE_TILES_DOWN = 2 * MAP_ROWS / 8,
    TILE_ROW_BYTES = PICTURE_TILES_ACROSS * MODE7_TILE_PIXELS,
    MAP_ROWS_A_TILE_ROW = 4, FIRST_PICTURE_TILE = 1,
};

/* A map tile is drawn from its tile group's right-hand tiles (the group's
 * top right and bottom right words): each one's colour in its palette, so
 * the pixel is the CGRAM entry. A colour of 0 draws nothing, and leaves what
 * is under it. */
enum { GROUP_BYTES = 8, TOP_RIGHT = 2, BOTTOM_RIGHT = 6, TILE_NUMBER = 0x03FF, PALETTE_BITS = 0x1C00 };
enum { BLACK = 0 };

typedef struct {
    bool drawn;
    uint8_t pixel;
} MapPixel;

/* Some colours aren't shown, and come out black: on one of the world maps
 * those of its fifth palette, on the rest a handful of its first three. */
enum { MAP_WITH_ITS_OWN_BLACKS = 0x01F3 };
static bool shown(const GameState *g, uint8_t pixel)
{
    if (location_id(g) == MAP_WITH_ITS_OWN_BLACKS)
        return !(pixel >= 0x40 && pixel < 0x4F);
    return !(pixel == 0x0F || pixel == 0x2F || (pixel >= 0x24 && pixel < 0x2C));
}

static uint16_t group_word(const GameState *g, uint16_t group, unsigned corner)
{
    return wram_read16(g, in_work_ram((uint16_t)(group + corner)));
}

static uint8_t palette_of(uint16_t word) { return (uint8_t)((word & PALETTE_BITS) >> 6); }

static MapPixel group_pixel(const GameState *g, uint16_t group, unsigned corner, uint8_t palette)
{
    uint16_t tile = group_word(g, group, corner) & TILE_NUMBER;
    uint8_t colour = tile < WORLD_MAP_TILES ? menu_map_colours(g, tile) : menu_map_colours_unchecked(g, tile);
    if (colour == 0)
        return (MapPixel){ false, 0 };
    uint8_t pixel = (uint8_t)(colour | palette);
    return (MapPixel){ true, shown(g, pixel) ? pixel : BLACK };
}

/* Where a map tile's pair of pixels goes in the tile row being built. */
static uint16_t picture_offset(unsigned column, unsigned row)
{
    return (uint16_t)((column / 8) * MODE7_TILE_PIXELS + column % 8 + (2 * row % 8) * 8);
}

static void put_pixel(GameState *g, uint16_t buffer, uint16_t at, MapPixel p)
{
    if (p.drawn)
        wram_write8(g, in_work_ram((uint16_t)(buffer + at)), p.pixel);
}

/* A map tile's pixels, from one plane's map; says where the tile's group
 * is. */
static uint16_t draw_map_tile(GameState *g, uint16_t buffer, uint8_t plane, unsigned column, unsigned row)
{
    uint8_t group = wram_read8(g, in_work_ram((uint16_t)(menu_plane_map(g, plane) + row * MAP_COLUMNS + column)));
    uint16_t record = (uint16_t)(menu_plane_groups(g, plane) + group * GROUP_BYTES);
    uint16_t at = picture_offset(column, row);
    /* Both palettes are read before either pixel is drawn: where the buffer
     * lies over the groups, the first pixel can change the second's tile. */
    uint8_t top = palette_of(group_word(g, record, TOP_RIGHT));
    uint8_t bottom = palette_of(group_word(g, record, BOTTOM_RIGHT));
    put_pixel(g, buffer, at, group_pixel(g, record, TOP_RIGHT, top));
    put_pixel(g, buffer, (uint16_t)(at + 8), group_pixel(g, record, BOTTOM_RIGHT, bottom));
    return record;
}

/* A tile row started: the mode's upload buffer cleared, the first time a
 * quarter more than a tile row, and its place returned. The original looks
 * the buffer up again for each row and clears it with a block move. */
static uint16_t start_tile_row(GameState *g, uint16_t bytes)
{
    uint16_t buffer = (uint16_t)menu_object_upload_buffer(g);
    wram_fill8(g, in_work_ram(buffer), bytes, 0);
    ORIGINAL_EFFECT(charge_block_move(g, bytes));
    return buffer;
}

/* The map's land is its second plane, and the first is drawn over it; one
 * world map has nothing on its first. */
enum { LAND_PLANE = 1, OVER_THE_LAND = 0, MAP_WITH_ONE_PLANE = 0x01F5 };

// $C2:645C, $C2:653E, $C2:658E
/* Draw the picture a tile row at a time and send each to the plane. Returns
 * where the last tile group it looked at is. */
static uint16_t draw_map_picture(GameState *g, Platform *pf)
{
    uint16_t buffer = start_tile_row(g, TILE_ROW_BYTES + TILE_ROW_BYTES / 3);
    bool both_planes = location_id(g) != MAP_WITH_ONE_PLANE;
    uint16_t last_group = 0;
    for (unsigned row = 0; row < MAP_ROWS; row++) {
        for (unsigned column = 0; column < MAP_COLUMNS; column++) {
            last_group = draw_map_tile(g, buffer, LAND_PLANE, column, row);
            if (both_planes)
                last_group = draw_map_tile(g, buffer, OVER_THE_LAND, column, row);
        }
        if ((row + 1) % MAP_ROWS_A_TILE_ROW == 0) {
            unsigned tile_row = row / MAP_ROWS_A_TILE_ROW;
            uint16_t first_tile = (uint16_t)(FIRST_PICTURE_TILE + tile_row * PICTURE_TILES_ACROSS);
            static const uint8_t steps[] = {
                VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
                VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,   VID_STEP_CONTROL,   VID_STEP_DEST,
                VID_STEP_VRAM_PORT, VID_STEP_SOURCE_BANK, VID_STEP_START,   VID_STEP_END,
            };
            static const VidSnesXfer tile_row_upload = { EPOCH_DMA, DMA_ONE_REGISTER, steps, &after_high };
            vid_upload_vram_half(pf, PF_AT(0xC264EB), (uint16_t)(first_tile * MODE7_TILE_PIXELS),
                                 PF_GAME(menu_object_upload_buffer(g)), TILE_ROW_BYTES, VID_VRAM_HIGH_BYTES,
                                 &tile_row_upload);
            buffer = start_tile_row(g, TILE_ROW_BYTES);
        }
    }
    return last_group;
}

/* Where the picture stands on the plane: tile rows 6 down and columns 2 in. */
enum { PICTURE_ROW = 6, PICTURE_COLUMN = 2, NAME_ROWS_SENT = 32, NAME_TABLE_BYTES = 0x400 };

// $C2:65B2
/* Lay the picture's tiles out on the plane: its tile numbers in order,
 * twelve across and sixteen down, the rest of the plane's corner tile 0,
 * built in work RAM and sent to the low halves of the plane's first 32
 * rows. */
static void place_map_picture(GameState *g, Platform *pf)
{
    for (unsigned i = 0; i < NAME_TABLE_BYTES; i++)
        set_menu_map_name_table(g, i, 0);
    ORIGINAL_EFFECT(charge_block_move(g, NAME_TABLE_BYTES));
    uint8_t tile = FIRST_PICTURE_TILE;
    for (unsigned row = 0; row < PICTURE_TILES_DOWN; row++)
        for (unsigned column = 0; column < PICTURE_TILES_ACROSS; column++)
            set_menu_map_name_table(g, (PICTURE_ROW + row) * NAME_ROWS_SENT + PICTURE_COLUMN + column, tile++);
    for (unsigned row = 0; row < NAME_ROWS_SENT; row++)
        vid_upload_vram_half(pf, PF_AT(0xC2662F), (uint16_t)(row * MODE7_MAP_ROW),
                             PF_GAME(RAM_MENU_MAP_NAME_TABLE + row * NAME_ROWS_SENT), NAME_ROWS_SENT,
                             VID_VRAM_LOW_BYTES, &low_byte_upload);
}

/* What building the view leaves in the mode's cells: the name table's last
 * row sent (its count run out, the row it started the picture at, the tile
 * after the last, the next row's source and VRAM word), where it started the
 * picture's rows, and the picture's building (the buffer's place, the last
 * group it looked at, the two planes' maps a row past the last and their
 * groups); and the multiplier as it found the picture's row. */
static void leave_map_view_built(GameState *g, uint16_t last_group)
{
    enum { NAMES_LEFT = 0x00, NAMES_ROW = 0x01, BUFFER_AT = 0x05, NEXT_TILE = 0x08, NEXT_SOURCE = 0x0A,
           NEXT_VRAM = 0x0C, LAST_GROUP = 0x0E, NAMES_AT = 0x10, OVER_MAP = 0x13, LAND_GROUPS = 0x16,
           OVER_GROUPS = 0x19 };
    leave_cell8(g, NAMES_LEFT, 0);
    leave_cell8(g, NAMES_ROW, PICTURE_ROW);
    leave_cell16(g, BUFFER_AT, (uint16_t)menu_object_upload_buffer(g));
    leave_cell16(g, NEXT_TILE, FIRST_PICTURE_TILE + PICTURE_TILES_ACROSS * PICTURE_TILES_DOWN);
    leave_cell16(g, NEXT_SOURCE, (uint16_t)(RAM_ABS(RAM_MENU_MAP_NAME_TABLE) + NAME_TABLE_BYTES));
    leave_cell16(g, NEXT_VRAM, NAME_ROWS_SENT * MODE7_MAP_ROW);
    leave_cell16(g, LAST_GROUP, last_group);
    leave_cell16(g, NAMES_AT, (uint16_t)(RAM_ABS(RAM_MENU_MAP_NAME_TABLE) + (PICTURE_ROW + PICTURE_TILES_DOWN) * NAME_ROWS_SENT
                                         + PICTURE_COLUMN));
    leave_cell16(g, OVER_MAP, (uint16_t)(menu_plane_map(g, OVER_THE_LAND) + MAP_ROWS * MAP_COLUMNS));
    leave_cell16(g, LAND_GROUPS, menu_plane_groups(g, LAND_PLANE));
    leave_cell16(g, OVER_GROUPS, menu_plane_groups(g, OVER_THE_LAND));
    hw_set_multiplicand(g, PICTURE_ROW);
    (void)hw_multiply_by(g, NAME_ROWS_SENT);
}

// $C2:63DF
/* The map view's plane, from the world map just loaded: every tile's colour,
 * the picture drawn from them, and the picture laid out on the plane. */
static void build_map_view(GameState *g, Platform *pf)
{
    sample_map_colours(g);
    uint16_t last_group = draw_map_picture(g, pf);
    place_map_picture(g, pf);
    ORIGINAL_EFFECT(leave_map_view_built(g, last_group));
}

/* ---- The map view: its graphics --------------------------------------------------------------- */

/* The graphics set two of the world maps' markers are drawn from changes
 * with the story: before it the map uses the set every map starts with. */
enum { EARLY_SET = 0x6F, SET_OF_A_LATER_STORY = 0x6C, SET_OF_THE_EPOCH_WINGS = 0x6B };
enum { STORY_OF_THE_LATER_SET = 0x36, EPOCH_WINGS_FLAG = 0x01 };

/* Ask the menus' graphics loader for the view's graphics: this map's set
 * from the table of sets at $C6:F400, the loader's second way of reading,
 * into its buffer at $7E:8621. */
enum { MAP_VIEW_SETS = 0xC6F400, LOADER_KIND = 0x02, MAP_VIEW_GRAPHICS_BYTES = 0x0C };

static void request_map_view_graphics(GameState *g)
{
    uint8_t set = menu_map_view_graphics(g, world_map(g));
    if ((set == SET_OF_A_LATER_STORY && menu_story_progress(g) < STORY_OF_THE_LATER_SET)
        || (set == SET_OF_THE_EPOCH_WINGS && !(menu_story_flags_epoch(g) & EPOCH_WINGS_FLAG)))
        set = EARLY_SET;
    set_menu_load_request_entry(g, set);
    set_menu_load_request_table(g, MAP_VIEW_SETS);
    set_menu_load_request_kind(g, LOADER_KIND);
    set_menu_load_request_dest(g, RAM_MENU_MAP_GRAPHICS);
}

/* ---- The map view: its objects ---------------------------------------------------------------- */

/* The cursor walks its path out and back, a step a frame; each step is a
 * zoom across and down, and the picture turns with the cursor's angle. */
enum { PATH_BYTES = 0x40, PATH_STEP = 2, PATH_END = PATH_BYTES - PATH_STEP };
typedef enum { CURSOR_STARTS, CURSOR_WALKS_OUT, CURSOR_WAITS, CURSOR_WALKS_BACK } CursorStep;

/* The view is centred on the party: the picture a pixel a map tile across
 * (sixteen of the world map's pixels) and two down, from where it stands on
 * the plane. */
enum { PICTURE_LEFT = PICTURE_COLUMN * 8, PICTURE_TOP = PICTURE_ROW * 8 };

/* What the cursor did this frame. */
typedef enum { CURSOR_WALKED, CURSOR_WAITED, CURSOR_DONE } CursorOutcome;

/* The scripts the cursor starts as it reaches its path's end and as the
 * player leaves (bank C2). */
enum { CURSOR_ARRIVED_SCRIPT = 0xC26A0A, CURSOR_LEAVING_SCRIPT = 0xC269FF };

/* The menus' mode the view hands back to as it ends. */
enum { MENU_MODE_BACK_TO_THE_MAP = 0x07 };

/* The button that closes the view. */
enum { CLOSE_BUTTON = 0x0008 };

/* An eighth of a sine's value is a matrix's whole unit; the zooms divide it. */
enum { MATRIX_SCALE = 8 };

/* A value of the sine, at the matrix's unit and shrunk by a zoom (a larger
 * zoom, a smaller picture). The original divides its size and puts the sign
 * back after, so it rounds toward zero; a zoom of 0 gives the divider's
 * all-ones. */
static uint16_t quotient(uint16_t dividend, uint8_t divisor)
{
    return divisor ? (uint16_t)(dividend / divisor) : 0xFFFF;
}

static uint16_t size_at_the_unit(int16_t value) { return (uint16_t)((value < 0 ? -value : value) * MATRIX_SCALE); }

static uint16_t zoomed(int16_t value, uint8_t zoom)
{
    uint16_t q = quotient(size_at_the_unit(value), zoom);
    return value < 0 ? (uint16_t)(0 - q) : q;
}

/* What working out the matrix leaves in the mode's cells and the divider:
 * the four quotients, the sine's and cosine's sizes, the zooms and the two
 * signs (the second a byte on from the first). */
static void leave_matrix(GameState *g, int16_t sin, int16_t cos, const uint16_t matrix[4], uint16_t zoom_x,
                         uint16_t zoom_y)
{
    enum { QUOTIENTS = 0x00, COS8 = 0x08, SIN8 = 0x0A, ZOOM_X = 0x0C, ZOOM_Y = 0x0E, SIN_SIGN = 0x10, COS_SIGN = 0x11 };
    for (unsigned i = 0; i < 4; i++)
        leave_cell16(g, QUOTIENTS + 2 * i, matrix[i]);
    leave_cell16(g, COS8, size_at_the_unit(cos));
    leave_cell16(g, SIN8, size_at_the_unit(sin));
    leave_cell16(g, ZOOM_X, zoom_x);
    leave_cell16(g, ZOOM_Y, zoom_y);
    leave_cell8(g, SIN_SIGN, sin < 0);
    leave_cell8(g, COS_SIGN, cos < 0);
    hw_set_dividend(g, size_at_the_unit(cos));
    hw_divide_by(g, (uint8_t)zoom_y);
}

// $C2:6736, $C2:67B3
/* Turn the picture by the cursor's angle and zoom it by its zooms across and
 * down. The original takes the angle's sine from another object's angle,
 * `sine_from`: the object it last started or the cursor, which in the game
 * always has the cursor's angle (menu_epoch_map_cursor). */
static void turn_map_view(GameState *g, MenuObject cursor, MenuObject sine_from)
{
    uint16_t zoom_x = menu_object_slots_zoom_x(g, cursor), zoom_y = menu_object_slots_zoom_y(g, cursor);
    int16_t sin = sine(g, menu_object_slots_phase(g, sine_from));
    int16_t cos = cosine(g, menu_object_slots_phase(g, cursor));
    const uint16_t matrix[4] = { zoomed(cos, (uint8_t)zoom_x), zoomed(sin, (uint8_t)zoom_x),
                                 (uint16_t)(0 - zoomed(sin, (uint8_t)zoom_y)), zoomed(cos, (uint8_t)zoom_y) };
    set_mode7_matrix(g, (int16_t)matrix[0], (int16_t)matrix[1], (int16_t)matrix[2], (int16_t)matrix[3]);
    ORIGINAL_EFFECT(leave_matrix(g, sin, cos, matrix, zoom_x, zoom_y));
}

/* The cursor takes the zoom of the step it is on. */
static void take_path_step(GameState *g, MenuObject cursor, uint16_t at)
{
    set_menu_object_slots_zoom_x(g, cursor, menu_cursor_path_zoom_x(g, at));
    set_menu_object_slots_zoom_y(g, cursor, menu_cursor_path_zoom_y(g, at));
}

// $C2:666C
/* The view's cursor: it centres the picture on the party, walks its path
 * out, waits for the player to close the view, walks back and ends the
 * screen. */
static CursorOutcome map_view_cursor(GameState *g, MenuObject cursor)
{
    CursorStep step = (CursorStep)menu_object_slots_step(g, cursor);
    if (step == CURSOR_STARTS) {
        step = CURSOR_WALKS_OUT;
        set_menu_object_slots_step(g, cursor, step);
        uint16_t centre_x = (uint16_t)((world_position_x(g) >> 4) + PICTURE_LEFT);
        uint16_t centre_y = (uint16_t)((world_position_y(g) >> 3) + PICTURE_TOP);
        set_menu_object_shadows_mode7_center(g, (uint32_t)centre_y << 16 | centre_x);
        set_menu_object_shadows_bg1_scroll_x(g, (uint16_t)(0 - centre_x));
        set_menu_object_shadows_bg1_scroll_y(g, 0);
        set_menu_object_slots_phase(g, cursor, 0);
        set_menu_object_slots_path_step(g, cursor, 0);
    }
    switch (step) {
    case CURSOR_STARTS:
    case CURSOR_WALKS_OUT: {
        uint16_t at = menu_object_slots_path_step(g, cursor);
        take_path_step(g, cursor, at);
        at = (uint16_t)(at + PATH_STEP);
        set_menu_object_slots_path_step(g, cursor, at);
        /* On the step the path ends, the sine's angle is the object's just
         * started: a copy of the cursor, unless every slot was busy and the
         * script went to the last one over what it held. */
        MenuObject angled = cursor;
        if (at >= PATH_BYTES) {
            set_menu_object_slots_path_step(g, cursor, PATH_END);
            set_menu_object_slots_step(g, cursor, CURSOR_WAITS);
            TIMED_AS(g, 0xC204D9, angled = menu_start_script(g, CURSOR_ARRIVED_SCRIPT, MENU_ORDINARY_SLOT));
        }
        TIMED_AS(g, 0xC26736, turn_map_view(g, cursor, angled));
        return CURSOR_WALKED;
    }
    case CURSOR_WAITS:
        if (input_mapped(g, 0) & CLOSE_BUTTON) {
            set_menu_object_slots_step(g, cursor, CURSOR_WALKS_BACK);
            TIMED_AS(g, 0xC204D9, menu_start_script(g, CURSOR_LEAVING_SCRIPT, MENU_ORDINARY_SLOT));
        }
        return CURSOR_WAITED;
    default: {
        uint16_t at = menu_object_slots_path_step(g, cursor);
        take_path_step(g, cursor, at);
        at = (uint16_t)(at - PATH_STEP);
        set_menu_object_slots_path_step(g, cursor, at);
        TIMED_AS(g, 0xC26736, turn_map_view(g, cursor, cursor));
        if (at != 0)
            return CURSOR_WALKED;
        set_menu_mode(g, MENU_MODE_BACK_TO_THE_MAP);
        return CURSOR_DONE;
    }
    }
}

/* A marker is a sprite on the view: a place on the picture, a sprite script
 * of bank C2's, drawn in front at priority 3. A world position (a pixel of
 * the world map) is on the picture an eighth of it across and down, rounded,
 * from where the picture stands on the plane. */
enum { MARKER_LOOK = 0x30, MARKER_SCRIPT_BANK = 0xC2, PARTY_MARKER = 0x6A18, EPOCH_MARKER = 0x6A20 };
enum { LANDING_MARKER_SCRIPT = 0x47, LANDING_MARKER_Y = 0x00C0, PICTURE_ORIGIN_X = 0x20, PICTURE_ORIGIN_Y = 0x30 };

static uint16_t on_the_picture(uint16_t world, uint16_t origin) { return (uint16_t)((((uint32_t)world + 4) >> 3) + origin); }

static void start_marker(GameState *g, MenuObject marker)
{
    set_menu_object_slots_step(g, marker, (uint8_t)(menu_object_slots_step(g, marker) + 1));
    set_menu_object_slots_look(g, marker, MARKER_LOOK);
    set_menu_object_slots_tiles(g, marker, 0);
}

static void place_marker(GameState *g, MenuObject marker, uint16_t world_x, uint16_t world_y, uint16_t script)
{
    start_marker(g, marker);
    set_menu_object_slots_x_pixel(g, marker, on_the_picture(world_x, PICTURE_ORIGIN_X));
    set_menu_object_slots_y_pixel(g, marker, on_the_picture(world_y, PICTURE_ORIGIN_Y));
    set_menu_object_slots_sprite_script(g, marker, (uint32_t)MARKER_SCRIPT_BANK << 16 | script);
    set_menu_object_slots_sprite_frames(g, marker, 0);
}

/* What a marker did this frame: showed its sprites, ended because another
 * object's sprite script was running, or ended because the Epoch isn't on
 * this map. */
typedef enum { MARKER_SHOWN, MARKER_ENDED, MARKER_NOT_HERE } MarkerOutcome;

static MarkerOutcome show_marker(GameState *g)
{
    if (menu_sprite_script_busy(g) != 0)
        return MARKER_ENDED;
    TIMED_AS(g, 0xC20E1D, menu_object_animate(g));
    return MARKER_SHOWN;
}

// $C2:67FA
/* The marker where the party stands. */
static MarkerOutcome party_marker(GameState *g, MenuObject marker)
{
    if (menu_object_slots_step(g, marker) == 0)
        place_marker(g, marker, world_position_x(g), world_position_y(g), PARTY_MARKER);
    return show_marker(g);
}

// $C2:683B
/* The marker where the Epoch is parked, on the map it is parked on alone;
 * on another it ends at once. */
static MarkerOutcome epoch_marker(GameState *g, MenuObject marker)
{
    if (menu_object_slots_step(g, marker) == 0) {
        if (location_here(g) != menu_epoch_location(g))
            return MARKER_NOT_HERE;
        place_marker(g, marker, menu_epoch_position(g, 0), menu_epoch_position(g, 1), EPOCH_MARKER);
    }
    return show_marker(g);
}

// $C2:68A3
/* The marker over the map the Epoch would land on: its place is the map's
 * own. */
static MarkerOutcome landing_marker(GameState *g, MenuObject marker)
{
    if (menu_object_slots_step(g, marker) == 0) {
        start_marker(g, marker);
        set_menu_object_slots_x_pixel(g, marker, menu_landing_marker_x(g, world_map(g)));
        set_menu_object_slots_y_pixel(g, marker, LANDING_MARKER_Y);
        menu_give_sprite_script(g, marker, LANDING_MARKER_SCRIPT);
    }
    return show_marker(g);
}

// $C2:68F4
/* The view's backdrop: the fixed colour's blue stepping down the screen,
 * seven lines a level, on line-effect slot 1. Its values are a byte a line,
 * written from slot 1's first table on: the second sixteen levels run the
 * count all the way round, on over the tables after it, as the original's
 * byte does. */
enum { GRADIENT_SLOT = 1, LINES_A_LEVEL = 7, LEVELS = 16, BLUE = 0x80, GRADIENT_HALF = 112 };

static void map_view_gradient(GameState *g, Platform *pf)
{
    uint16_t at = (uint16_t)RAM_MENU_SLOT1_LINE_VALUES;
    uint8_t level = 0;
    for (unsigned pass = 0; pass < 2; pass++) {
        do {
            for (unsigned line = 0; line < LINES_A_LEVEL; line++)
                wram_write8(g, in_work_ram(at++), (uint8_t)(level | BLUE));
        } while (++level != LEVELS);
    }
    ORIGINAL_EFFECT(hw_port_seek(g, at, WORK_RAM_BANK));
    ORIGINAL_EFFECT(leave_cell8(g, 0x00, level));
    menu_start_line_effect(g, pf, 0xC2694F, GRADIENT_SLOT, VID_LINE_FIXED_COLOR);
    set_menu_object_hdma_pointers(g, 0, (uint16_t)RAM_MENU_SLOT1_LINE_VALUES);
    set_menu_object_hdma_pointers(g, 1, (uint16_t)(RAM_MENU_SLOT1_LINE_VALUES + GRADIENT_HALF));
}

/* ---- The time dial: its periods ------------------------------------------------------------- */

static bool later_periods(const GameState *g) { return menu_story_progress(g) >= MENU_LATER_PERIODS; }

/* The original notes the table of the periods' locations it reads in its
 * cells: its address and bank. */
static void leave_period_table(GameState *g)
{
    enum { PERIODS_AT = 0x10 };
    leave_cell16(g, PERIODS_AT, later_periods(g) ? 0x6CA5 : 0x6C97);
    leave_cell8(g, PERIODS_AT + 2, 0xC2);
}

// $C2:747A
/* The period the party is in: the one whose location is here, or the last. */
static uint8_t period_here(GameState *g)
{
    ORIGINAL_EFFECT(leave_period_table(g));
    for (uint8_t period = 0; period < MENU_PERIODS; period++)
        if (menu_period_location(g, later_periods(g), period) == location_here(g))
            return period;
    return MENU_PERIODS - 1;
}

// $C2:745A
/* The location a period travels to. */
static uint16_t period_location(GameState *g, uint8_t period)
{
    ORIGINAL_EFFECT(leave_period_table(g));
    return menu_period_location(g, later_periods(g), period);
}

/* ---- The time dial: the gate's window --------------------------------------------------------- */

/* The gate's triangle is cut out of the screen by window 1, its two edges
 * drawn into a table of a line's left and right edge each; two tables, one
 * drawn while the other shows, on line-effect slot 1. */
enum { WINDOW_SLOT = 1, SCREEN_CENTRE_X = 0x84, SCREEN_CENTRE_Y = 0x70, CORNERS = 3 };
enum { CLOSED_LINE = 0x0001 };
enum { DRAWING_THE_OTHER_WINDOW = 0x01, DRAWING_THE_OTHER_SPIN = 0x02, DRAWING_THE_OTHER_SINES = 0x04 };

static void set_line(GameState *g, uint8_t bank, uint16_t table, uint16_t line, uint16_t v)
{
    uint16_t at = (uint16_t)(table + 2 * line);
    wram_write8(g, (uint32_t)bank << 16 | at, (uint8_t)v);
    wram_write8(g, (uint32_t)bank << 16 | (uint16_t)(at + 1), (uint8_t)(v >> 8));
}
static uint16_t line_value(const GameState *g, uint8_t bank, uint16_t table, uint16_t line)
{
    uint16_t at = (uint16_t)(table + 2 * line);
    return (uint16_t)(wram_read8(g, (uint32_t)bank << 16 | at) | wram_read8(g, (uint32_t)bank << 16 | (uint16_t)(at + 1)) << 8);
}

/* A signed multiply of a sine's value by a corner's offset, as the
 * original's routine does it: the two made positive, multiplied, and the
 * sign put back, so it leaves both operands positive in its cells. */
typedef struct {
    int32_t product;     /* 24 bits */
    uint8_t by;          /* the operands as it leaves them */
    uint16_t value;
} SignedProduct;

static SignedProduct multiply_signed(int8_t by, int16_t value)
{
    SignedProduct p;
    p.by = (uint8_t)(by < 0 ? -by : by);
    p.value = (uint16_t)(value < 0 ? -value : value);
    uint32_t magnitude = (uint32_t)p.by * p.value;
    p.product = (by < 0) != (value < 0) ? -(int32_t)magnitude : (int32_t)magnitude;
    return p;
}

/* What the multiply leaves: its operands, positive, the 24-bit product, and
 * the multiplier as it worked out the high byte's part. */
static void leave_multiply(GameState *g, const SignedProduct *p)
{
    enum { BY = 0x07, VALUE = 0x0E, PRODUCT = 0x19 };
    leave_cell8(g, BY, p->by);
    leave_cell16(g, VALUE, p->value);
    leave_cell16(g, PRODUCT, (uint16_t)p->product);
    leave_cell8(g, PRODUCT + 2, (uint8_t)((uint32_t)p->product >> 16));
    hw_set_multiplicand(g, p->by);
    (void)hw_multiply_by(g, (uint8_t)(p->value >> 8));
}

/* A turned corner's place on the screen: the centre, moved by the whole
 * pixels of its turned offset (8.8). The original works the offset out from
 * the two products and adds the centre with the first sum's carry still in,
 * so an offset whose products wrap past zero lands a pixel further on. */
static uint8_t on_the_screen(uint16_t offset, bool wrapped, uint8_t centre)
{
    return (uint8_t)((offset >> 8) + centre + wrapped);
}

typedef struct {
    uint8_t x, y;
} ScreenPoint;

// $C2:6D1B
/* The triangle's corners, turned by the gate's angle about the screen's
 * centre. */
static void turn_corners(GameState *g, uint16_t angle, ScreenPoint corners[CORNERS])
{
    int16_t cos = cosine(g, angle), sin = sine(g, angle);
    SignedProduct last = { 0 };
    uint16_t last_height = 0;
    for (unsigned c = 0; c < CORNERS; c++) {
        int8_t dx = menu_gate_corner_x(g, c), dy = menu_gate_corner_y(g, c);
        uint16_t across = (uint16_t)multiply_signed(dx, cos).product;
        uint16_t lifted = (uint16_t)multiply_signed(dy, sin).product;
        uint16_t x = (uint16_t)(across + lifted);
        corners[c].x = on_the_screen(x, x < across, SCREEN_CENTRE_X);
        SignedProduct leant = multiply_signed(dx, sin);
        last = multiply_signed(dy, cos);
        uint16_t y = (uint16_t)((uint16_t)last.product - (uint16_t)leant.product);
        corners[c].y = on_the_screen(y, (uint16_t)last.product >= (uint16_t)leant.product, SCREEN_CENTRE_Y);
        last_height = (uint16_t)leant.product;
        set_menu_gate_corners(g, 2 * c, corners[c].x);
        set_menu_gate_corners(g, 2 * c + 1, corners[c].y);
    }
    ORIGINAL_EFFECT(leave_multiply(g, &last));
    ORIGINAL_EFFECT(leave_cell16(g, 0x04, last_height));
}

/* An edge of the triangle, from its higher corner to the line above its
 * lower one: where it starts across, 8.8, and how far it moves a line. */
typedef struct {
    bool drawn;          /* a flat edge takes no lines */
    uint8_t x, top, bottom, width, height;
    bool swapped;
    uint16_t step;
} GateEdge;

static GateEdge edge_between(ScreenPoint from, ScreenPoint to)
{
    GateEdge e = { 0 };
    if (from.y == to.y)
        return e;
    e.drawn = true;
    e.swapped = to.y < from.y;
    if (e.swapped) {
        ScreenPoint t = from;
        from = to;
        to = t;
    }
    e.x = from.x;
    e.top = from.y;
    e.bottom = (uint8_t)(to.y - 1);
    e.height = (uint8_t)(e.bottom - e.top);
    bool leftward = to.x < from.x;
    e.width = (uint8_t)(leftward ? from.x - to.x : to.x - from.x);
    uint16_t slope = quotient((uint16_t)(e.width << 8), e.height);
    e.step = leftward ? (uint16_t)(0 - slope) : slope;
    return e;
}

/* The edges kept in the game's cells for the fill: on the line, where
 * across, how far it moves, its lines. */
static void keep_edge(GameState *g, unsigned e, const GateEdge *edge)
{
    set_menu_gate_edge_active(g, e, menu_gate_edge_active(g, e) & 0xFF00);
    set_menu_gate_edge_x(g, e, (uint16_t)(edge->x << 8));
    set_menu_gate_edge_top(g, e, (uint16_t)(edge->top << 8));
    set_menu_gate_edge_lines(g, e, edge->height);
    set_menu_gate_edge_bottom(g, e, (uint16_t)(edge->bottom << 8));
    set_menu_gate_edge_step(g, e, edge->step);
}

/* The edges crossing a line, their places across in the order of the edges,
 * each moved on to the next line; an edge that has run out of lines leaves
 * the fill. */
static void cross_line(GameState *g, unsigned edges, uint8_t line, uint16_t crossings[CORNERS])
{
    for (unsigned e = 0; e < edges; e++)
        if (menu_gate_edge_top(g, e) >> 8 == line)
            set_menu_gate_edge_active(g, e, (menu_gate_edge_active(g, e) & 0xFF00)
                                                | (uint8_t)(menu_gate_edge_active(g, e) + 1));
    unsigned crossing = 0;
    for (unsigned e = 0; e < edges; e++) {
        if ((uint8_t)menu_gate_edge_active(g, e) == 0)
            continue;
        uint16_t x = menu_gate_edge_x(g, e);
        crossings[crossing++] = x;
        set_menu_gate_edge_x(g, e, (uint16_t)(x + menu_gate_edge_step(g, e)));
        uint8_t left = (uint8_t)(menu_gate_edge_lines(g, e) - 1);
        set_menu_gate_edge_lines(g, e, left);
        if (left & 0x80)
            set_menu_gate_edge_active(g, e, menu_gate_edge_active(g, e) & 0xFF00);
    }
}

/* What drawing the window leaves in the mode's cells, the divider and the
 * work RAM port: the last line's two crossings, the edge loop's counts and
 * places (the last edge's corners, as its swap left them), the lines' span,
 * and the table's place. */
typedef struct {
    uint16_t crossings[CORNERS];
    GateEdge last_edge;
    ScreenPoint last_from, last_to;
    bool any_swapped;
    unsigned edges;
    uint8_t after_the_last_line, bottom;
    uint16_t table;
    uint8_t bank;
    int16_t cos, sin;
} WindowDrawn;

static void leave_window_drawn(GameState *g, const WindowDrawn *w, const GateEdge *divided)
{
    enum { CROSSINGS = 0x00, SECOND_CROSSING = 0x02, HEIGHT = 0x04, CORNERS_LEFT = 0x06, LINE = 0x08, BOTTOM = 0x0A,
           TO_X = 0x0C, TO_Y = 0x0E, EDGES_LEFT = 0x10, EDGE_AT = 0x11, LINES_LEFT = 0x13, TABLE = 0x16,
           EDGES = 0x19 };
    leave_cell16(g, CROSSINGS, w->crossings[0]);
    leave_cell16(g, SECOND_CROSSING, w->crossings[1]);
    if (divided->drawn)
        leave_cell8(g, HEIGHT, divided->height);
    leave_cell8(g, CORNERS_LEFT, 0);
    leave_cell8(g, LINE, w->after_the_last_line);
    leave_cell8(g, LINE + 1, w->any_swapped ? 0 : (uint8_t)((uint16_t)w->cos >> 8));
    leave_cell8(g, BOTTOM, w->bottom);
    leave_cell8(g, BOTTOM + 1, (uint8_t)((uint16_t)w->sin >> 8));
    leave_cell8(g, TO_X, w->last_edge.swapped ? w->last_from.x : w->last_to.x);
    leave_cell8(g, TO_X + 1, 0);
    leave_cell8(g, TO_Y, w->last_edge.drawn ? w->last_edge.bottom : w->last_to.y);
    if (w->any_swapped)
        leave_cell8(g, TO_Y + 1, (uint8_t)((uint16_t)w->sin >> 8));
    leave_cell8(g, EDGES_LEFT, 0);
    leave_cell8(g, EDGE_AT, 0);
    leave_cell8(g, LINES_LEFT, 0);
    leave_cell8(g, LINES_LEFT + 1, 0);
    leave_cell16(g, TABLE, w->table);
    leave_cell16(g, EDGES, (uint16_t)w->edges);
    if (divided->drawn) {
        hw_set_dividend(g, (uint16_t)(divided->width << 8));
        hw_divide_by(g, divided->height);
    }
    hw_port_seek(g, (uint16_t)(w->table + 2 * w->after_the_last_line), w->bank);
}

/* The original's time for the window follows its lines more closely than
 * the port's accesses do: each line runs its loops over every edge. It is
 * charged, in accesses at the window's rate. */
enum { LINE_ACCESSES = 7, FIXED_ACCESSES = 330 };

// $C2:6CC1
/* Draw the gate's window for the angle the gate stands at, into the table
 * not shown this frame, and show it from the next: the table starts closed
 * at its top, its middle lines take the one above them, the triangle's
 * corners are turned about the centre, and every line from the highest
 * corner to the lowest takes the two edges that cross it. */
static void draw_gate_window(GameState *g, MenuObject dial)
{
    bool first = !(menu_gate_flags(g) & DRAWING_THE_OTHER_WINDOW);
    set_menu_gate_flags(g, (uint8_t)(menu_gate_flags(g) ^ DRAWING_THE_OTHER_WINDOW));
    uint16_t table = menu_line_values(WINDOW_SLOT, first ? 1 : 0);
    menu_show_line_values(g, WINDOW_SLOT, table);
    uint8_t bank = menu_object_scratch_gate_window_bank(g);

    /* The first line closed, and the line under it half; the middle lines
     * all take the one above them, which the original repeats with an
     * overlapping block move. */
    enum { MIDDLE_FROM = 72, MIDDLE_TO = 152 };
    set_line(g, bank, table, 0, CLOSED_LINE);
    wram_write8(g, (uint32_t)bank << 16 | (uint16_t)(table + 2), 0);
    uint16_t middle = line_value(g, WORK_RAM_BANK, table, MIDDLE_FROM);
    wram_fill16(g, in_work_ram((uint16_t)(table + 2 * (MIDDLE_FROM + 1))), MIDDLE_TO - MIDDLE_FROM - 1, middle);
    ORIGINAL_EFFECT(charge_block_move(g, 2 * (MIDDLE_TO - MIDDLE_FROM - 1)));

    uint16_t angle = menu_object_slots_angle(g, dial);
    ScreenPoint corners[CORNERS];
    turn_corners(g, angle, corners);

    WindowDrawn w = { .table = table, .bank = bank, .cos = cosine(g, angle), .sin = sine(g, angle) };
    GateEdge divided = { 0 };
    uint8_t top = 0xFF, bottom = 0;
    for (unsigned c = 0; c < CORNERS; c++) {
        ScreenPoint to = corners[(c + 1) % CORNERS];
        GateEdge edge = edge_between(corners[c], to);
        w.last_edge = edge;
        w.last_from = corners[c];
        w.last_to = to;
        if (!edge.drawn)
            continue;
        divided = edge;
        w.any_swapped = w.any_swapped || edge.swapped;
        keep_edge(g, w.edges++, &edge);
        if (edge.top < top)
            top = edge.top;
        if (edge.bottom >= bottom)
            bottom = edge.bottom;
    }

    /* Every triangle the gate turns to has two edges across each of its
     * lines. */
    uint8_t lines = (uint8_t)(bottom - top);
    for (uint8_t n = 0; n < lines; n++) {
        uint8_t line = (uint8_t)(top + n);
        cross_line(g, w.edges, line, w.crossings);
        uint8_t first_x = (uint8_t)(w.crossings[0] >> 8), second_x = (uint8_t)(w.crossings[1] >> 8);
        set_line(g, bank, table, line, first_x < second_x ? (uint16_t)(second_x << 8 | first_x)
                                                          : (uint16_t)(first_x << 8 | second_x));
    }
    ORIGINAL_EFFECT(charge_as(g, 0xC26CC1, FIXED_ACCESSES + LINE_ACCESSES * lines));
    w.after_the_last_line = (uint8_t)(top + lines);
    w.bottom = bottom;
    ORIGINAL_EFFECT(leave_window_drawn(g, &w, &divided));
}

/* ---- The time dial: its objects --------------------------------------------------------------- */

typedef enum { DIAL_STARTS, DIAL_WAITS, DIAL_TURNS } DialStep;

/* The buttons that turn the dial back and on, tried in this order, and the
 * ones that travel and that leave. */
static const struct {
    uint16_t button;
    int8_t turn;
} dial_buttons[] = {
    { 0x0010, -1 }, { 0x0020, 1 }, { 0x0100, -1 }, { 0x0200, 1 }, { 0x0800, -1 }, { 0x0400, 1 },
};
enum { TRAVEL_BUTTON = 0x0080, LEAVE_BUTTON = 0x0008, NOWHERE = 0xFFFF };

/* How far the gate turns a frame, and the sound of a period the Epoch is
 * already at. */
enum { DIAL_SPEED = 4 };
enum { BUZZ_EFFECT = 0x01, BUZZ_PAN = 0x80 };

/* The screen the dial starts on: the window's tables all closed, and the
 * window on slot 1. */
static void open_the_window(GameState *g, Platform *pf)
{
    set_menu_object_scratch_gate_window_bank(g, WORK_RAM_BANK);
    menu_start_line_effect(g, pf, 0xC26AFA, WINDOW_SLOT, VID_LINE_WINDOW1);
    wram_fill16(g, RAM_MENU_SLOT1_LINE_VALUES, MENU_LINE_TABLE_LINES, CLOSED_LINE);
    wram_fill16(g, RAM_MENU_SLOT1_OTHER_LINE_VALUES, MENU_LINE_TABLE_LINES, CLOSED_LINE);
    ORIGINAL_EFFECT(charge_block_move(g, 4 * MENU_LINE_TABLE_LINES));
}

/* Stand the dial at a period: the gate turns to its angle. */
static void aim_at_period(GameState *g, MenuObject dial, uint8_t period)
{
    set_menu_object_slots_period(g, dial, period);
    set_menu_object_slots_target_angle(g, dial, menu_period_angle(g, period));
}

/* Where turning moves on past the ends of the dial: it stops there. */
static uint8_t turned_period(uint8_t period, int8_t turn)
{
    uint8_t next = (uint8_t)(period + turn);
    if (turn < 0)
        return next & 0x80 ? 0 : next;
    return next >= MENU_PERIODS ? MENU_PERIODS - 1 : next;
}

/* What the dial did this frame. */
typedef enum {
    DIAL_IDLE,        /* nothing pressed */
    DIAL_TRAVELS,     /* to the period it stands at, or away */
    DIAL_BUZZES,      /* asked to travel to where the party is */
    DIAL_TURNING,     /* on towards the period it was turned to */
    DIAL_ARRIVED,     /* at that period */
    DIAL_PASSES,      /* over a period whose location is here, on to the next */
} DialOutcome;

// $C2:6AC8
/* The dial: it starts at the party's period with its window open, then turns
 * through the periods as the player asks. A period whose location is where
 * the party is is turned past, back at the ends. The player travels to the
 * period the dial stands at, or leaves; asked to travel to where the party
 * is, the dial buzzes (its entry sends the sound). It is never done: the
 * screen ends by the menus' mode. */
static DialOutcome time_dial(GameState *g, Platform *pf, MenuObject dial)
{
    uint8_t step = menu_object_slots_step(g, dial);
    if (step == DIAL_STARTS) {
        set_menu_object_slots_step(g, dial, DIAL_WAITS);
        uint8_t period = period_here(g);
        open_the_window(g, pf);
        aim_at_period(g, dial, period);
        set_menu_object_slots_angle(g, dial, menu_period_angle(g, period));
        set_menu_time_period(g, period);
        TIMED_AS(g, 0xC26CC1, draw_gate_window(g, dial));
        step = DIAL_WAITS;
    }
    if (step == DIAL_WAITS) {
        uint16_t buttons = input_mapped(g, 1);
        int8_t turn = 0;
        for (unsigned i = 0; i < sizeof dial_buttons / sizeof dial_buttons[0] && turn == 0; i++)
            if (buttons & dial_buttons[i].button)
                turn = dial_buttons[i].turn;
        if (turn == 0) {
            if (buttons & TRAVEL_BUTTON) {
                uint16_t there = period_location(g, menu_object_slots_period(g, dial));
                ORIGINAL_EFFECT(leave_cell16(g, 0x08, location_here(g)));
                if (there != location_here(g)) {
                    set_menu_travel_location(g, there);
                    set_menu_mode(g, MENU_MODE_BACK_TO_THE_MAP);
                    return DIAL_TRAVELS;
                }
                set_sound_command(g, SOUND_EFFECT_ON_VOICES_6_7);
                set_sound_command_parameters(g, 0, BUZZ_EFFECT);
                set_sound_command_parameters(g, 1, BUZZ_PAN);
                return DIAL_BUZZES;
            }
            if (buttons & LEAVE_BUTTON) {
                set_menu_travel_location(g, NOWHERE);
                set_menu_mode(g, MENU_MODE_BACK_TO_THE_MAP);
                return DIAL_TRAVELS;
            }
            return DIAL_IDLE;
        }
        aim_at_period(g, dial, turned_period(menu_object_slots_period(g, dial), turn));
        set_menu_object_slots_turn(g, dial, (uint16_t)(turn * DIAL_SPEED));
        set_menu_object_slots_step(g, dial, DIAL_TURNS);
    }
    uint16_t angle = menu_object_slots_angle(g, dial);
    if (angle != menu_object_slots_target_angle(g, dial)) {
        set_menu_object_slots_angle(g, dial, (uint16_t)(angle + menu_object_slots_turn(g, dial)));
        TIMED_AS(g, 0xC26CC1, draw_gate_window(g, dial));
        return DIAL_TURNING;
    }
    uint8_t period = menu_object_slots_period(g, dial);
    uint16_t there = period_location(g, period);
    ORIGINAL_EFFECT(leave_cell16(g, 0x08, there));
    if (location_here(g) != there) {
        set_menu_time_period(g, period);
        set_menu_object_slots_step(g, dial, DIAL_WAITS);
        return DIAL_ARRIVED;
    }
    /* Here already: on past it the way it was turning, and back at an end. */
    int16_t turn = (int16_t)menu_object_slots_turn(g, dial);
    uint8_t next = (uint8_t)(period + (turn >= 0 ? 1 : -1));
    if (turn >= 0 ? next >= MENU_PERIODS : (next & 0x80) != 0) {
        next = (uint8_t)(turn >= 0 ? next - 2 : next + 2);
        set_menu_object_slots_turn(g, dial, (uint16_t)-turn);
    }
    aim_at_period(g, dial, next);
    return DIAL_PASSES;
}

/* The gate's spin: the Mode 7 matrix changes every line, its cosines on
 * line-effect slot 2 (the matrix's A and D) and its sines on slot 3 (B and
 * C), each double-buffered; every frame the lines move up one and the last
 * takes the next angle, so the gate turns down the screen. */
enum { COSINE_SLOT = 2, SINE_SLOT = 3, MATRIX_A = 2, MATRIX_C = 4, MATRIX_D = 5, MATRIX_B = 3 };
enum { SPIN_SPEED = 4, LAST_LINE = MENU_LINE_TABLE_LINES - 1 };

static void gate_spin_line(GameState *g, uint16_t cosines, uint16_t sines, uint16_t line, uint16_t angle)
{
    wram_write16(g, in_work_ram((uint16_t)(cosines + 2 * line)), (uint16_t)cosine(g, angle));
    wram_write16(g, in_work_ram((uint16_t)(sines + 2 * line)), (uint16_t)sine(g, (uint16_t)(angle + EIGHTH_TURN)));
}

/* What the spin leaves in the mode's cells: the two tables' places, past
 * the lines it filled the first time, and on the first, its loop's count and
 * angle. */
static void leave_spin(GameState *g, uint16_t cosines, uint16_t sines)
{
    enum { COSINES_AT = 0x10, SINES_AT = 0x13 };
    leave_cell16(g, COSINES_AT, cosines);
    leave_cell16(g, SINES_AT, sines);
}

// $C2:6F85
/* The gate's spin object. The first frame fills both first tables from its
 * angle, a spin speed's turn a line, and starts the four channels; every
 * frame after moves the lines up into the other table and shows that. */
static void gate_spin(GameState *g, Platform *pf, MenuObject spin)
{
    if (menu_object_slots_step(g, spin) == 0) {
        set_menu_object_slots_step(g, spin, 1);
        set_menu_object_slots_angle(g, spin, 0);
        menu_lay_out_line_slot(g, COSINE_SLOT);
        menu_lay_out_line_slot(g, SINE_SLOT);
        menu_program_line_channel(pf, 0xC26FBB, MATRIX_A, VID_LINE_MODE7_A, menu_line_slot_table(COSINE_SLOT));
        menu_program_line_channel(pf, 0xC26FC1, MATRIX_D, VID_LINE_MODE7_D, menu_line_slot_table(COSINE_SLOT));
        menu_program_line_channel(pf, 0xC26FDE, MATRIX_B, VID_LINE_MODE7_B, menu_line_slot_table(SINE_SLOT));
        menu_program_line_channel(pf, 0xC26FE4, MATRIX_C, VID_LINE_MODE7_C, menu_line_slot_table(SINE_SLOT));
        menu_turn_on_line_slots(g, 1u << MATRIX_A | 1u << MATRIX_B | 1u << MATRIX_C | 1u << MATRIX_D);
        set_menu_gate_flags(g, (uint8_t)(menu_gate_flags(g) & ~(DRAWING_THE_OTHER_SPIN | DRAWING_THE_OTHER_SINES)));
        uint16_t cosines = menu_line_values(COSINE_SLOT, 0), sines = menu_line_values(SINE_SLOT, 0);
        menu_show_line_values(g, COSINE_SLOT, cosines);
        menu_show_line_values(g, SINE_SLOT, sines);
        uint16_t angle = 0;
        for (uint16_t line = 0; line < MENU_LINE_TABLE_LINES; line++, angle = (uint16_t)(angle + SPIN_SPEED))
            gate_spin_line(g, cosines, sines, line, angle);
        set_menu_object_slots_angle(g, spin, angle);
        ORIGINAL_EFFECT(leave_spin(g, (uint16_t)(cosines + 2 * MENU_LINE_TABLE_LINES),
                                   (uint16_t)(sines + 2 * MENU_LINE_TABLE_LINES)));
        ORIGINAL_EFFECT(leave_cell16(g, 0x00, 0));
        ORIGINAL_EFFECT(leave_cell16(g, 0x08, angle));
        return;
    }
    bool first = !(menu_gate_flags(g) & DRAWING_THE_OTHER_SPIN);
    set_menu_gate_flags(g, (uint8_t)(menu_gate_flags(g) ^ DRAWING_THE_OTHER_SPIN));
    uint16_t cosines = menu_line_values(COSINE_SLOT, first ? 1 : 0), sines = menu_line_values(SINE_SLOT, first ? 1 : 0);
    uint16_t cosines_were = menu_line_values(COSINE_SLOT, first ? 0 : 1), sines_were = menu_line_values(SINE_SLOT, first ? 0 : 1);
    menu_show_line_values(g, COSINE_SLOT, cosines);
    menu_show_line_values(g, SINE_SLOT, sines);
    /* Every line moves up one, and the byte past the tables comes with them
     * into the last line's low half, which the next angle then covers. */
    enum { MOVED = 2 * LAST_LINE + 1 };
    wram_copy(g, in_work_ram((uint16_t)(cosines_were + 2)), in_work_ram(cosines), MOVED);
    wram_copy(g, in_work_ram((uint16_t)(sines_were + 2)), in_work_ram(sines), MOVED);
    ORIGINAL_EFFECT(charge_as(g, 0xC26F85, 2 * 2 * MOVED));
    uint16_t angle = menu_object_slots_angle(g, spin);
    gate_spin_line(g, cosines, sines, LAST_LINE, angle);
    set_menu_object_slots_angle(g, spin, (uint16_t)(angle + SPIN_SPEED));
    ORIGINAL_EFFECT(leave_spin(g, cosines, sines));
}

typedef enum { DIGIT_STARTS, DIGIT_HIDDEN, DIGIT_SHOWING, DIGIT_SHOWN, DIGIT_HIDING } DigitStep;
enum { DIGIT_LOOK = 0xB0 };

// $C2:7143
/* One of the dial's seven digits, its period's: it appears while the dial
 * stands at its period, and goes when the dial moves on, each by a sprite
 * script that plays out before it looks again. */
static void dial_digit(GameState *g, MenuObject digit)
{
    uint8_t period = menu_object_slots_period(g, digit);
    DigitStep step = (DigitStep)menu_object_slots_step(g, digit);
    if (step == DIGIT_STARTS) {
        step = DIGIT_HIDDEN;
        set_menu_object_slots_step(g, digit, step);
        set_menu_object_slots_look(g, digit, DIGIT_LOOK);
        set_menu_object_slots_tiles(g, digit, 0);
        set_menu_object_slots_x_pixel(g, digit, menu_digit_x(g, period));
        set_menu_object_slots_y_pixel(g, digit, menu_digit_y(g, period));
    }
    switch (step) {
    case DIGIT_STARTS:
    case DIGIT_HIDDEN:
        if (menu_time_period(g) == period) {
            set_menu_object_slots_step(g, digit, DIGIT_SHOWING);
            menu_give_sprite_script(g, digit, menu_digit_show_script(g, period));
            TIMED_AS(g, 0xC20E1D, menu_object_animate(g));
        }
        return;
    case DIGIT_SHOWING:
        if (menu_object_slots_sprite_frames(g, digit) == 0)
            set_menu_object_slots_step(g, digit, DIGIT_SHOWN);
        TIMED_AS(g, 0xC20E1D, menu_object_animate(g));
        return;
    case DIGIT_SHOWN:
        if (menu_time_period(g) != period) {
            set_menu_object_slots_step(g, digit, DIGIT_HIDING);
            menu_give_sprite_script(g, digit, menu_digit_hide_script(g, period));
        }
        TIMED_AS(g, 0xC20E1D, menu_object_animate(g));
        return;
    default:
        if (menu_object_slots_sprite_frames(g, digit) == 0)
            set_menu_object_slots_step(g, digit, DIGIT_HIDDEN);
        TIMED_AS(g, 0xC20E1D, menu_object_animate(g));
        return;
    }
}

/* ---- The time dial: its graphics -------------------------------------------------------------- */

/* The gate's tiles, twelve by sixteen of them, go to sixteen rows of VRAM,
 * each filled with the same run of 32 pieces from the plane's buffer. */
enum { GATE_TILE_ROWS = 16, GATE_PIECES = 32, GATE_PIECE_BYTES = 0x40, GATE_PIECE_WORDS = 0x80 };

// $C2:7254
static void upload_gate_tiles(GameState *g, Platform *pf)
{
    for (unsigned row = 0; row < GATE_TILE_ROWS; row++) {
        uint16_t vram = menu_gate_tile_row(g, row);
        for (unsigned piece = 0; piece < GATE_PIECES; piece++)
            vid_upload_vram(pf, PF_AT(0xC2726D), (uint16_t)(vram + piece * GATE_PIECE_WORDS),
                            PF_GAME(RAM_MENU_MODE7_PLANE + piece * GATE_PIECE_BYTES), GATE_PIECE_BYTES, &word_upload);
    }
}

static void leave_gate_tiles(GameState *g)
{
    enum { PIECES_LEFT = 0x00, VRAM = 0x08, SOURCE = 0x0A, ROWS_AT = 0x0C };
    leave_cell8(g, PIECES_LEFT, 0);
    leave_cell16(g, VRAM, (uint16_t)(menu_gate_tile_row(g, GATE_TILE_ROWS - 1) + GATE_PIECES * GATE_PIECE_WORDS));
    leave_cell16(g, SOURCE, (uint16_t)(RAM_ABS(RAM_MENU_MODE7_PLANE) + GATE_PIECES * GATE_PIECE_BYTES));
    leave_cell16(g, ROWS_AT, 2 * GATE_TILE_ROWS);
}

/* A tilemap row is 32 bytes, sixteen words. */
enum { TILEMAP_ROW_BYTES = 0x20, TILEMAP_ROW_WORDS = 0x10 };

/* Where the rows' table and source are, long, as the original hands them to
 * the rows' upload in its cells. */
static void leave_rows_asked_for(GameState *g, uint32_t picks, uint32_t source)
{
    enum { PICKS = 0x10, SOURCE = 0x13 };
    leave_cell16(g, PICKS, (uint16_t)picks);
    leave_cell8(g, PICKS + 2, (uint8_t)(picks >> 16));
    leave_cell16(g, SOURCE, (uint16_t)source);
    leave_cell8(g, SOURCE + 2, (uint8_t)(source >> 16));
}

// $C2:72D8
/* Rows of the gate's tilemap: each picks a row of the source by its number
 * in a table, and goes to the next row of VRAM. A count of 0 is 65536. */
static void upload_gate_rows(GameState *g, Platform *pf, uint16_t rows, uint32_t picks, uint32_t source, uint16_t vram)
{
    static const uint8_t steps[] = {
        VID_STEP_CONTROL,   VID_STEP_DEST,      VID_STEP_VRAM_PORT, VID_STEP_TARGET_LO,
        VID_STEP_TARGET_HI, VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK,
        VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,   VID_STEP_START,     VID_STEP_END,
    };
    static const VidSnesXfer row_upload = { EPOCH_DMA, DMA_WORDS, steps, &after_high };
    uint16_t left = rows;
    do {
        uint8_t pick = menu_gate_row_pick(g, picks);
        picks = (picks & 0xFF0000u) | (uint16_t)(picks + 1);
        uint32_t from = (source & 0xFF0000u) | (uint16_t)(pick * TILEMAP_ROW_BYTES + source);
        vid_upload_vram(pf, PF_AT(0xC272EE), vram, PF_GAME(from), TILEMAP_ROW_BYTES, &row_upload);
        vram = (uint16_t)(vram + TILEMAP_ROW_WORDS);
        ORIGINAL_EFFECT(leave_cell16(g, 0x0A, (uint16_t)from));
    } while (--left != 0);
    ORIGINAL_EFFECT(leave_cell16(g, 0x00, 0));
    ORIGINAL_EFFECT(leave_cell16(g, 0x08, vram));
    ORIGINAL_EFFECT(leave_cell16(g, 0x10, (uint16_t)picks));
}

// $C2:7361
/* The gate's picture: its pixels, packed two a byte, a byte each in the
 * plane's buffer. The original writes them through the work RAM port. */
enum { GATE_PIXEL_PAIRS = 0x400 };

static void unpack_gate_pixels(GameState *g)
{
    for (unsigned i = 0; i < GATE_PIXEL_PAIRS; i++) {
        uint8_t pair = menu_gate_pixel_pair(g, i);
        set_menu_mode7_plane(g, 2 * i, (uint8_t)(pair >> 4));
        set_menu_mode7_plane(g, 2 * i + 1, (uint8_t)(pair & 0x0F));
    }
    ORIGINAL_EFFECT(hw_port_seek(g, (uint16_t)(RAM_ABS(RAM_MENU_MODE7_PLANE) + 2 * GATE_PIXEL_PAIRS), EXTRA_RAM_BANK));
}

// $C2:722C
/* The gate's picture to the plane's first tiles. */
static void upload_gate_pixels(Platform *pf)
{
    vid_upload_vram_half(pf, PF_AT(0xC2722F), 0x0000, PF_GAME(RAM_MENU_MODE7_PLANE), 2 * GATE_PIXEL_PAIRS,
                         VID_VRAM_HIGH_BYTES, &high_byte_upload);
}

/* The dial's palette: kept as the mode loaded it, and sent with one colour
 * white. */
enum { WHITE = 0xFFFF, WHITE_COLOUR = 0xD1, PALETTE_BYTES = 0x200 };

static void keep_dial_palette(GameState *g)
{
    wram_copy(g, RAM_MENU_LOADED_PALETTE, RAM_MENU_GATE_PALETTE_COPY, PALETTE_BYTES);
    set_menu_loaded_palette(g, WHITE_COLOUR, WHITE);
    wram_copy(g, RAM_MENU_LOADED_PALETTE, RAM_MENU_OBJECT_PALETTE, PALETTE_BYTES);
    ORIGINAL_EFFECT(charge_block_move(g, 2 * PALETTE_BYTES));
}

/* The dial's objects: the dial, the gate's spin, two scripts and the seven
 * digits, one a period (a new object takes on the period of the one that
 * starts it, which counts them). */
enum { DIAL_HANDLER = 0x6AC8, SPIN_HANDLER = 0x6F85, DIGIT_HANDLER = 0x7143 };
enum { DIAL_SCRIPT = 0xC274B8, DIAL_SECOND_SCRIPT = 0xC27504, DIAL_LAST_SCRIPT = 0xC274AE, DIAL_ENDING_SCRIPT = 0xC274B3 };

static void start_dial_objects(GameState *g)
{
    MenuObject started;
    TIMED_AS(g, 0xC2048A, (void)menu_start_object(g, DIAL_HANDLER, MENU_ORDINARY_SLOT, &started));
    TIMED_AS(g, 0xC204D9, menu_start_script(g, DIAL_SCRIPT, MENU_ORDINARY_SLOT));
    TIMED_AS(g, 0xC204D9, menu_start_script(g, DIAL_SECOND_SCRIPT, MENU_ORDINARY_SLOT));
    TIMED_AS(g, 0xC2048A, (void)menu_start_object(g, SPIN_HANDLER, MENU_ORDINARY_SLOT, &started));
    MenuObject self = menu_object_running(g);
    uint8_t kept = menu_object_slots_period(g, self);
    set_menu_object_slots_period(g, self, 0);
    for (uint8_t period = 0; period < MENU_PERIODS; period++) {
        TIMED_AS(g, 0xC2048A, (void)menu_start_object(g, DIGIT_HANDLER, MENU_ORDINARY_SLOT, &started));
        set_menu_object_slots_period(g, self, (uint8_t)(period + 1));
    }
    set_menu_object_slots_period(g, self, kept);
    TIMED_AS(g, 0xC204D9, menu_start_script(g, DIAL_LAST_SCRIPT, MENU_ORDINARY_SLOT));
}

/* ---- Where the Epoch may fly ------------------------------------------------------------------ */

/* A rectangle of tiles, each start inclusive and end exclusive, and the sides
 * of it a tile can be outside of. */
typedef struct {
    uint8_t top, bottom, left, right;
} Area;
enum { OUTSIDE_LEFT = 1, OUTSIDE_RIGHT = 2, OUTSIDE_ABOVE = 4, OUTSIDE_BELOW = 8 };

// $C6:E74E
static uint8_t sides_outside(uint8_t x, uint8_t y, Area a)
{
    uint8_t sides = 0;
    if (x < a.left)
        sides = OUTSIDE_LEFT;
    else if (x >= a.right)
        sides = OUTSIDE_RIGHT;
    if (y < a.top)
        sides |= OUTSIDE_ABOVE;
    else if (y >= a.bottom)
        sides |= OUTSIDE_BELOW;
    return sides;
}

/* The areas of the world maps the Epoch may be flown in; a map with none has
 * nowhere. */
enum { TILE_SHIFT = 3 };
static const struct {
    bool has_area;
    Area area;
} world_map_areas[MENU_WORLD_MAPS] = {
    { true, { 0x64, 0x7F, 0x41, 0x72 } },
    { true, { 0x76, 0x7F, 0x60, 0x6E } },
    { false, { 0, 0, 0, 0 } },
    { true, { 0x00, 0x34, 0x00, 0x0E } },
    { true, { 0x60, 0x7F, 0x00, 0x20 } },
    { false, { 0, 0, 0, 0 } },
    { false, { 0, 0, 0, 0 } },
    { false, { 0, 0, 0, 0 } },
};

/* The tile the check takes and the area it compared, as the original leaves
 * them in its cells. It stores the tile's row as a word, whose high byte
 * lands in the area's first row's cell, which the area then covers. */
static void leave_area_cells(GameState *g, uint16_t tile_x, uint16_t tile_y, const Area *a, uint8_t sides)
{
    set_menu_object_scratch_area_tile_x(g, (uint8_t)tile_x);
    set_menu_object_scratch_area_tile_y(g, (uint8_t)tile_y);
    set_menu_object_scratch_area_top(g, (uint8_t)(tile_y >> 8));
    if (!a)
        return;
    set_menu_object_scratch_area_top(g, a->top);
    set_menu_object_scratch_area_bottom(g, a->bottom);
    set_menu_object_scratch_area_left(g, a->left);
    set_menu_object_scratch_area_right(g, a->right);
    set_menu_object_scratch_area_outside(g, sides);
}

// $C6:E797
/* Whether the object is inside the area of the world map the party is on. */
static bool in_area(GameState *g, MenuObject o)
{
    MenuPlace p = menu_object_place(g, o);
    uint16_t tile_x = menu_pixel(p.x) >> TILE_SHIFT, tile_y = menu_pixel(p.y) >> TILE_SHIFT;
    unsigned map = (uint16_t)(location_id(g) - MENU_FIRST_WORLD_MAP);
    if (!world_map_areas[map].has_area) {
        ORIGINAL_EFFECT(leave_area_cells(g, tile_x, tile_y, NULL, 0));
        return false;
    }
    const Area *a = &world_map_areas[map].area;
    uint8_t sides = sides_outside((uint8_t)tile_x, (uint8_t)tile_y, *a);
    ORIGINAL_EFFECT(leave_area_cells(g, tile_x, tile_y, a, sides));
    return sides == 0;
}

/* ---- The End of Time's gate screen ------------------------------------------------------------ */

/* The menus proper start the End of Time's gate screen: its third layer's
 * tilemap moves to its own place, the screen's frame task is added to the
 * menus' tasks, and the screen's state is set up from the ROM, where the task
 * reads it. The task and the rest of the screen are still the original's. */
enum { GATE_SCREEN_TILEMAP = 0x6800, TILEMAP_TILES = 32 };

/* The screen's state: two bytes the screen counts from zero, then its setup. */
enum { GATE_SCREEN_COUNTS = 2, GATE_SCREEN_SETUP_BYTES = MENU_GATE_SCREEN_STATE_COUNT - GATE_SCREEN_COUNTS };

static void start_counting(GameState *g)
{
    for (unsigned i = 0; i < GATE_SCREEN_COUNTS; i++)
        set_menu_gate_screen_state(g, i, 0);
}

static void set_up_gate_screen(GameState *g)
{
    for (unsigned i = 0; i < GATE_SCREEN_SETUP_BYTES; i++)
        set_menu_gate_screen_state(g, GATE_SCREEN_COUNTS + i, menu_gate_screen_setup(g, i));
}

/* ---- The bridges: the screens on the machine -------------------------------------------------- */

/* Registers the original's routines the screens call don't read
 * (tools/dontcare_check.py checks each). */
enum {
    READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_A = DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_X = DC_A | DC_Y | DC_N | DC_V | DC_Z | DC_C | DC_DB,
    READS_X_AND_A_HIGH = DC_Y | DC_N | DC_V | DC_Z | DC_C | DC_AL,
    JSR_BYTES = 2,
};

/* A sibling the port runs as C where the original calls it with a JSR, and
 * which calls out itself: its return address is pushed as the JSR pushes it,
 * so the machine's stack is the original's at the call-outs inside. */
static void push_return(Snes *s, uint32_t jsr_site) { m_push16(s, (uint16_t)(jsr_site + JSR_BYTES)); }
static void pull_return(Snes *s) { (void)m_pull16(s); }

// $C2:6965
/* Load the view's marker graphics with the menus' loader, and send them to
 * VRAM $6200. */
static void load_map_view_graphics(Snes *s)
{
    GameState *g = state_of(s);
    m_jsr_dc(s, 0xC26965, 0xC25798, READS_NOTHING | DC_DB);   /* clear the loader's buffer */
    request_map_view_graphics(g);
    m_jsl_dc(s, 0xC269BB, 0xC257DF, READS_NOTHING | DC_DB | DC_D);   /* the loader */
    set_menu_load_request_size(g, MAP_VIEW_GRAPHICS_BYTES);
    m_jsl_dc(s, 0xC269C4, 0xC25823, READS_NOTHING | DC_D);
    vid_upload_vram(m_platform(s), PF_AT(0xC269C8), 0x6200, PF_GAME(RAM_MENU_MAP_GRAPHICS), 0x0400, &word_upload);
}

// $C2:63DF
/* Load the world map ($C2:27DE), then build the view's plane from it. */
static void build_map_view_on_the_machine(Snes *s)
{
    m_jsr_dc(s, 0xC263DF, 0xC227DE, READS_NOTHING);
    NATIVE_TIME_AS(s, 0xC263DF, build_map_view(state_of(s), m_platform(s)));
}

/* The routines both screens call: the menus' VRAM cleared (menu/display.c's
 * entry), the mode reset, every object slot freed, a frame waited, a number
 * of frames waited while the sound's requests go on. */
static void clear_the_screen(Snes *s, uint32_t clear_vram, uint32_t reset_the_mode, uint32_t free_the_slots)
{
    m_jsr_dc(s, clear_vram, 0xC226A8, READS_NOTHING);
    m_jsr_dc(s, reset_the_mode, 0xC203EF, READS_NOTHING | DC_DB);
    m_jsr_dc(s, free_the_slots, 0xC20471, READS_NOTHING);
}

static void wait_frames(Snes *s, uint32_t site, uint16_t frames)
{
    s->cpu.x = frames;
    m_jsr_dc(s, site, 0xC20454, READS_X_AND_A_HIGH);
}

/* The screen runs until the menus' mode leaves `mode`, a frame at a time. */
static void run_until_the_mode_changes(Snes *s, uint32_t site, uint8_t mode)
{
    GameState *g = state_of(s);
    do
        m_jsr_dc(s, site, 0xC2046E, READS_NOTHING | DC_DB);
    while (menu_mode(g) == mode);
    m_set_a8(s, menu_mode(g));
}

/* The screen shows from the next frame: the mode sends its frame and palette,
 * and the vblank and joypad interrupts are on. */
static void show_from_the_next_frame(Snes *s, uint32_t site)
{
    GameState *g = state_of(s);
    set_menu_object_frame_state(g, (uint8_t)(menu_object_frame_state(g) | SEND_FRAME_AND_PALETTE));
    tm_enable_interrupts(m_platform(s), PF_AT(site), TM_INT_VBLANK | TM_INT_AUTO_JOYPAD);
}

/* And back to the menus' own display as it ends. */
static void back_to_the_menus(Snes *s, uint32_t sprites_site, uint32_t interrupts_site, uint32_t uploads_site,
                              uint32_t effects_site)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    set_menu_object_shadows_bg_mode(g, MENU_BG_MODE);
    const VidObjConfig menus = { .size_select = 3, .base = 0x0000, .name_select = 0 };
    vid_set_obj_config(pf, PF_AT(sprites_site), &menus);
    tm_enable_interrupts(pf, PF_AT(interrupts_site), 0);
    vid_stop_uploads(pf, PF_AT(uploads_site));
    vid_enable_line_effects(pf, PF_AT(effects_site), 0);
}

/* Starting a script leaves A its address's high byte over its bank, which
 * the frames' wait after it reads. */
static void start_script_as_the_original(Snes *s, uint32_t script)
{
    menu_start_script(state_of(s), script, MENU_ORDINARY_SLOT);
    s->cpu.a = (uint16_t)((script & 0xFF00) | (uint8_t)(script >> 16));
}

// $C2:631F
/* The map view: Mode 7 behind sprites at $6000, the colour math adding the
 * fixed colour to the backdrop, its graphics and plane built, the cursor and
 * its script started, and the screen run until the menus' mode leaves it. */
enum { MAP_VIEW_MODE = 0x06, BACKDROP = 0x20, MAP_VIEW_CURSOR = 0x666C, MAP_VIEW_SCRIPT = 0xC269F8 };

void menu_epoch_map_screen(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    set_menu_object_shadows_bg_mode(g, MODE_7);
    const VidObjConfig sprites = { .size_select = 3, .base = 0x6000, .name_select = 0 };
    vid_set_obj_config(pf, PF_AT(0xC26325), &sprites);
    set_menu_object_shadows_math_control(g, 0);
    set_menu_object_shadows_math_layers(g, BACKDROP);
    clear_the_screen(s, 0xC2632E, 0xC26331, 0xC26334);
    push_return(s, 0xC26337);
    load_map_view_graphics(s);
    pull_return(s);
    NATIVE_TIME_AS(s, 0xC268F4, map_view_gradient(g, pf));
    push_return(s, 0xC2633D);
    build_map_view_on_the_machine(s);
    pull_return(s);

    vid_upload_vram(pf, PF_AT(0xC26340), 0x6000, PF_GAME(MENU_MAP_VIEW_TILES), 0x0020, &word_upload);
    vid_upload_vram(pf, PF_AT(0xC26367), 0x6020, PF_GAME(MENU_MAP_VIEW_MORE_TILES), 0x0020, &word_upload);
    enum { VIEW_COLOURS = 0x100, VIEW_COLOUR_BYTES = 0x21 };
    set_menu_object_palette_byte(g, 0, 0);
    set_menu_object_palette_byte(g, 1, 0);
    uint32_t silenced = native_uncounted_begin(s);
    for (unsigned i = 0; i < VIEW_COLOUR_BYTES; i++)
        set_menu_object_palette_byte(g, VIEW_COLOURS + i, menu_map_view_colour_byte(g, i));
    native_uncounted_end(s, silenced);
    ORIGINAL_EFFECT(charge_block_move(g, VIEW_COLOUR_BYTES));
    m_sep(s, CPU_FLAG_M);

    MenuObject cursor;
    NATIVE_TIME_AS(s, 0xC2048A, (void)menu_start_object(g, MAP_VIEW_CURSOR, MENU_ORDINARY_SLOT, &cursor));
    NATIVE_TIME_AS(s, 0xC204D9, start_script_as_the_original(s, MAP_VIEW_SCRIPT));
    show_from_the_next_frame(s, 0xC263B7);
    run_until_the_mode_changes(s, 0xC263BA, MAP_VIEW_MODE);
    wait_frames(s, 0xC263C7, 0x11);
    back_to_the_menus(s, 0xC263D0, 0xC263D5, 0xC263D8, 0xC263DB);
    m_rts(s);
}

/* Load one of the dial's streams from the bank the caller names into bank
 * $7F with the decompressor, from the JSL at $C2:721B. */
static void load_gate_stream(Snes *s, uint8_t stream, uint8_t bank)
{
    GameState *g = state_of(s);
    set_decompress_source(g, (uint32_t)bank << 16 | menu_gate_stream_source(g, stream));
    set_decompress_dest(g, (uint32_t)EXTRA_RAM_BANK << 16 | menu_gate_stream_dest(g, stream));
    m_jsl_dc(s, 0xC2721B, 0xC30002, READS_NOTHING | DC_DB | DC_D);
}

/* The streams: the tiles from bank C3, the palette and the tilemap's rows
 * from bank C6. */
enum { GATE_TILES_STREAM = 0, GATE_PALETTE_STREAM = 1, GATE_ROWS_STREAM = 2 };

static void load_gate_stream_as_the_original(Snes *s, uint32_t jsr_site, uint8_t stream, uint8_t bank)
{
    ORIGINAL_EFFECT(leave_cell8(state_of(s), 0x08, stream));
    push_return(s, jsr_site);
    load_gate_stream(s, stream, bank);
    pull_return(s);
}

// $C2:7389
/* Everything the dial shows: the gate's tiles, its tilemap, its palette and
 * its picture, and the objects that drive it. */
enum { GATE_TILE_VRAM = 0x6400, GATE_TILEMAP_VRAM = 0x6000, GATE_TILEMAP_ROWS = 0x40, GATE_TILES_BYTES = 0x800 };

static void build_the_dial(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    load_gate_stream_as_the_original(s, 0xC2738D, GATE_TILES_STREAM, 0xC3);
    vid_upload_vram(pf, PF_AT(0xC27393), GATE_TILE_VRAM, PF_GAME(RAM_MENU_GATE_TILES), GATE_TILES_BYTES, &word_upload);
    ORIGINAL_EFFECT(leave_rows_asked_for(g, MENU_GATE_TILEMAP_ROWS, RAM_MENU_MODE7_PLANE));
    NATIVE_TIME_AS(s, 0xC272D8, upload_gate_rows(g, pf, GATE_TILEMAP_ROWS, MENU_GATE_TILEMAP_ROWS, RAM_MENU_MODE7_PLANE,
                                                 GATE_TILEMAP_VRAM));
    load_gate_stream_as_the_original(s, 0xC273DC, GATE_PALETTE_STREAM, 0xC6);
    keep_dial_palette(g);
    load_gate_stream_as_the_original(s, 0xC27408, GATE_ROWS_STREAM, 0xC6);
    NATIVE_TIME_AS(s, 0xC27254, upload_gate_tiles(g, pf));
    ORIGINAL_EFFECT(leave_gate_tiles(g));
    NATIVE_TIME_AS(s, 0xC27361, unpack_gate_pixels(g));
    NATIVE_TIME_AS(s, 0xC2722C, upload_gate_pixels(pf));
    start_dial_objects(g);
    s->cpu.a = (uint16_t)((DIAL_LAST_SCRIPT & 0xFF00) | (uint8_t)(DIAL_LAST_SCRIPT >> 16));
}

// $C2:6A34
/* The time dial: Mode 7 behind sprites at $6000, its matrix a unit's
 * across and down, window 1 cutting the gate out of both screens, the colour
 * math adding the fixed colour to what the window leaves; the gate built and
 * its objects started, and the screen run until the menus' mode leaves it. */
enum { DIAL_MODE = 0x08, DIAL_SCROLL_X = 0xFF80, DIAL_SCROLL_Y = 0xFF90, UNIT_HIGH = 0x01 };
enum { WINDOW1_ON_COLOUR = 0x20, BG1_AND_SPRITES = 0x11, MATH_INSIDE_THE_WINDOW = 0x10, MATH_LAYERS = 0x17 };
enum { FIXED_LEVEL = 0x0F };

void menu_epoch_time_dial_screen(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    clear_the_screen(s, 0xC26A34, 0xC26A37, 0xC26A3A);
    set_menu_object_shadows_bg_mode(g, MODE_7);
    const VidObjConfig sprites = { .size_select = 0, .base = 0x6000, .name_select = 0 };
    vid_set_obj_config(pf, PF_AT(0xC26A43), &sprites);
    set_menu_object_shadows_bg1_scroll_x(g, DIAL_SCROLL_X);
    set_menu_object_shadows_bg1_scroll_y(g, DIAL_SCROLL_Y);
    set_menu_object_shadows_bg2_scroll_x(g, 0);
    set_menu_object_shadows_bg2_scroll_y(g, 0);
    set_menu_object_shadows_bg3_scroll_x(g, 0);
    set_menu_object_shadows_bg3_scroll_y(g, 0);
    /* The matrix's B and C cleared and the high bytes of A and D set: a unit
     * across and down, whatever their low bytes held. */
    for (unsigned at = 2; at < 6; at++)
        set_menu_object_shadows_mode7_matrix_byte(g, at, 0);
    set_menu_object_shadows_mode7_center(g, 0);
    set_menu_object_shadows_mode7_matrix_byte(g, 1, UNIT_HIGH);
    set_menu_object_shadows_mode7_matrix_byte(g, 7, UNIT_HIGH);
    set_menu_object_shadows_windows_byte(g, 0, 0);
    set_menu_object_shadows_windows_byte(g, 1, 0);
    set_menu_object_shadows_windows_byte(g, 2, WINDOW1_ON_COLOUR);
    set_menu_object_shadows_main_window_mask(g, BG1_AND_SPRITES);
    set_menu_object_shadows_sub_window_mask(g, BG1_AND_SPRITES);
    set_menu_object_shadows_math_control(g, MATH_INSIDE_THE_WINDOW);
    set_menu_object_shadows_math_layers(g, MATH_LAYERS);
    set_menu_object_shadows_fixed_blue(g, FIXED_LEVEL);
    set_menu_object_shadows_fixed_red(g, FIXED_LEVEL);
    set_menu_object_shadows_fixed_green(g, FIXED_LEVEL);
    set_menu_gate_flags(g, 0);
    push_return(s, 0xC26A8A);
    build_the_dial(s);
    pull_return(s);
    show_from_the_next_frame(s, 0xC26A93);
    m_set_a8(s, 0x81);
    wait_frames(s, 0xC26A99, 0x12);
    run_until_the_mode_changes(s, 0xC26A9C, DIAL_MODE);
    NATIVE_TIME_AS(s, 0xC204D9, start_script_as_the_original(s, DIAL_ENDING_SCRIPT));
    wait_frames(s, 0xC26AB1, 0x12);
    back_to_the_menus(s, 0xC26ABA, 0xC26ABE, 0xC26AC1, 0xC26AC4);
    m_rts(s);
}

/* ---- The entries ---------------------------------------------------------------------------- */

/* Both screens run on the menus' page with the data bank on the I/O banks,
 * their objects' handlers entered with their slot in X. */
bool menu_epoch_page_is_menu(Snes *s) { return s->cpu.d == RAM_PAGE_MENU && s->cpu.db == 0x00; }

bool menu_epoch_object_runs(Snes *s)
{
    if (!menu_epoch_page_is_menu(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = menu_object_address_names_one(menu_object_current(g)) && s->cpu.x == menu_object_current(g);
    vars_peek(false);
    return ok;
}

/* The objects whose handlers go by steps are on one of theirs: the original
 * jumps through a table of them, and past its end into code. */
static bool on_a_step(Snes *s, uint8_t steps)
{
    if (!menu_epoch_object_runs(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = menu_object_slots_step(g, menu_object_running(g)) < steps;
    vars_peek(false);
    return ok;
}

bool menu_epoch_cursor_runs(Snes *s) { return on_a_step(s, CURSOR_WALKS_BACK + 1); }
/* The dial draws its window in the tables its first step put in work RAM. */
bool menu_epoch_dial_runs(Snes *s)
{
    if (!on_a_step(s, DIAL_TURNS + 1))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    uint8_t bank = menu_object_scratch_gate_window_bank(g);
    bool ok = menu_object_slots_step(g, menu_object_running(g)) == DIAL_STARTS || bank == WORK_RAM_BANK
              || bank == EXTRA_RAM_BANK;
    vars_peek(false);
    return ok;
}
bool menu_epoch_spin_runs(Snes *s) { return on_a_step(s, 2); }

/* The markers and the digits run a sprite script, which reads as the
 * language wherever the game gives one (menu/sprites.c): the one the object
 * has, once it has set its own up, and the ones a digit is given for its
 * period; the sprite list is as the mode lays it out and the upload queue
 * whole. */
static bool animates(const GameState *g, MenuObject o)
{
    return menu_sprite_list_is_sound(g) && menu_object_queue_is_whole(g)
           && (menu_object_slots_step(g, o) == 0 || menu_sprite_script_is_well_formed(g, o));
}

bool menu_epoch_marker_runs(Snes *s)
{
    if (!menu_epoch_object_runs(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = animates(g, menu_object_running(g));
    vars_peek(false);
    return ok;
}

bool menu_epoch_digit_runs(Snes *s)
{
    if (!on_a_step(s, DIGIT_HIDING + 1))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    MenuObject digit = menu_object_running(g);
    uint8_t period = menu_object_slots_period(g, digit);
    /* A hidden digit shows nothing until it is given its showing script. */
    bool hidden = menu_object_slots_step(g, digit) <= DIGIT_HIDDEN;
    bool ok = menu_sprite_list_is_sound(g) && menu_object_queue_is_whole(g)
              && (hidden || menu_sprite_script_is_well_formed(g, digit))
              && menu_given_sprite_script_is_well_formed(g, menu_digit_show_script(g, period))
              && menu_given_sprite_script_is_well_formed(g, menu_digit_hide_script(g, period));
    vars_peek(false);
    return ok;
}

/* A slot's object is running: the dial's graphics count the digits' periods
 * in its slot. */
bool menu_epoch_slot_runs(Snes *s)
{
    if (!menu_epoch_page_is_menu(s))
        return false;
    vars_peek(true);
    bool ok = menu_object_address_names_one(menu_object_current(state_of(s)));
    vars_peek(false);
    return ok;
}

/* The matrix takes the angle for its sine from the slot in X. */
bool menu_epoch_matrix_runs(Snes *s)
{
    return menu_epoch_slot_runs(s) && menu_object_address_names_one(s->cpu.x);
}

/* The window's drawing runs on the page with a slot's object running (the
 * dial calls it with X on other things), and its tables in work RAM, where
 * the dial puts them. */
bool menu_epoch_window_runs(Snes *s)
{
    if (!menu_epoch_page_is_menu(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    uint8_t bank = menu_object_scratch_gate_window_bank(g);
    bool ok = menu_object_address_names_one(menu_object_current(g)) && (bank == WORK_RAM_BANK || bank == EXTRA_RAM_BANK);
    vars_peek(false);
    return ok;
}

/* The area check on one of the world maps, which the original picks the area
 * of with a jump through a table as long as the maps. */
bool menu_epoch_on_world_map(Snes *s)
{
    if (m_direct_page(s) != 0 || !m_bank_reaches_low_ram(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = menu_object_address_names_one(menu_object_current(g))
              && (uint16_t)(location_id(g) - MENU_FIRST_WORLD_MAP) < MENU_WORLD_MAPS;
    vars_peek(false);
    return ok;
}

/* An object's handler hands back whether it is done in the carry, and the
 * original's comes back with the accumulator as wide as its path left it. */
static void handler_returns(Snes *s, bool done, bool wide)
{
    m_set_flag(s, CPU_FLAG_C, done);
    if (wide)
        m_rep(s, CPU_FLAG_M);
    else
        m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

// $C2:63DF
/* It comes back with an 8-bit accumulator. */
void menu_epoch_build_map(Snes *s)
{
    build_map_view_on_the_machine(s);
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

// $C2:6965
void menu_epoch_load_map_graphics(Snes *s)
{
    load_map_view_graphics(s);
    m_rts(s);
}

// $C2:666C
/* A step of its walk comes back 16 bits wide. */
void menu_epoch_map_cursor(Snes *s)
{
    GameState *g = state_of(s);
    CursorOutcome outcome = map_view_cursor(g, menu_object_running(g));
    handler_returns(s, outcome == CURSOR_DONE, outcome == CURSOR_WALKED);
}

// $C2:6736
void menu_epoch_map_matrix(Snes *s)
{
    GameState *g = state_of(s);
    turn_map_view(g, menu_object_running(g), menu_object_at_address(s->cpu.x));
    m_rep(s, CPU_FLAG_M);
    m_rts(s);
}

/* A marker that ends because the Epoch is elsewhere comes back 16 bits
 * wide. */
static void marker_returns(Snes *s, MarkerOutcome outcome)
{
    handler_returns(s, outcome != MARKER_SHOWN, outcome == MARKER_NOT_HERE);
}

// $C2:67FA
void menu_epoch_map_party_marker(Snes *s)
{
    GameState *g = state_of(s);
    marker_returns(s, party_marker(g, menu_object_running(g)));
}

// $C2:683B
void menu_epoch_map_epoch_marker(Snes *s)
{
    GameState *g = state_of(s);
    marker_returns(s, epoch_marker(g, menu_object_running(g)));
}

// $C2:68A3
void menu_epoch_map_location_marker(Snes *s)
{
    GameState *g = state_of(s);
    marker_returns(s, landing_marker(g, menu_object_running(g)));
}

// $C2:68F4
/* A scene's script calls it too: it comes back with X clear and the carry
 * set. */
void menu_epoch_map_gradient(Snes *s)
{
    map_view_gradient(state_of(s), m_platform(s));
    s->cpu.x = 0;
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

// $C2:6AC8
/* The dial's buzz goes to the sound driver from the dial's own call, which
 * reads A's high byte: the location's there, where the dial compared it. */
void menu_epoch_time_dial_object(Snes *s)
{
    GameState *g = state_of(s);
    DialOutcome outcome = time_dial(g, m_platform(s), menu_object_running(g));
    if (outcome == DIAL_BUZZES) {
        s->cpu.a = (uint16_t)((location_here(g) & 0xFF00) | BUZZ_PAN);
        m_sep(s, CPU_FLAG_M);
        m_jsl_dc(s, 0xC26BE9, 0xC70004, READS_X_AND_A_HIGH);
        m_set_flag(s, CPU_FLAG_C, false);
        m_rts(s);
        return;
    }
    handler_returns(s, false, outcome == DIAL_IDLE || outcome == DIAL_PASSES);
}

// $C2:6CC1
void menu_epoch_build_gate_window(Snes *s)
{
    GameState *g = state_of(s);
    draw_gate_window(g, menu_object_running(g));
    m_rts(s);
}

// $C2:6F33
/* The multiply's operands are in the mode's cells: the byte at $07 and the
 * word at $0E. */
void menu_epoch_multiply_signed(Snes *s)
{
    GameState *g = state_of(s);
    SignedProduct p = multiply_signed((int8_t)menu_object_scratch_bytes_byte(g, 0x07),
                                      (int16_t)(menu_object_scratch_bytes_byte(g, 0x0E)
                                                | menu_object_scratch_bytes_byte(g, 0x0F) << 8));
    leave_multiply(g, &p);
    m_rts(s);
}

// $C2:6F85
void menu_epoch_gate_matrix_object(Snes *s)
{
    GameState *g = state_of(s);
    MenuObject spin = menu_object_running(g);
    bool first_frame = menu_object_slots_step(g, spin) == 0;
    gate_spin(g, m_platform(s), spin);
    handler_returns(s, false, !first_frame);
}

// $C2:7143
void menu_epoch_time_dial_digit(Snes *s)
{
    GameState *g = state_of(s);
    dial_digit(g, menu_object_running(g));
    handler_returns(s, false, false);
}

// $C2:71F9
/* The stream's number is in the mode's cell at $08, its bank in A. */
void menu_epoch_load_gate_stream(Snes *s)
{
    load_gate_stream(s, menu_object_scratch_bytes_byte(state_of(s), 0x08), (uint8_t)s->cpu.a);
    m_rts(s);
}

// $C2:722C
void menu_epoch_upload_gate_pixels(Snes *s)
{
    upload_gate_pixels(m_platform(s));
    m_rts(s);
}

// $C2:7254
void menu_epoch_upload_gate_tiles(Snes *s)
{
    GameState *g = state_of(s);
    upload_gate_tiles(g, m_platform(s));
    ORIGINAL_EFFECT(leave_gate_tiles(g));
    m_rts(s);
}

// $C2:72D8
/* Its count, table, source and VRAM word are in the mode's cells. */
void menu_epoch_upload_gate_rows(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t rows = (uint16_t)(menu_object_scratch_bytes_byte(g, 0x00) | menu_object_scratch_bytes_byte(g, 0x01) << 8);
    uint32_t picks = menu_object_scratch_bytes_byte(g, 0x10) | (uint32_t)menu_object_scratch_bytes_byte(g, 0x11) << 8
                     | (uint32_t)menu_object_scratch_bytes_byte(g, 0x12) << 16;
    uint32_t source = menu_object_scratch_bytes_byte(g, 0x13) | (uint32_t)menu_object_scratch_bytes_byte(g, 0x14) << 8
                      | (uint32_t)menu_object_scratch_bytes_byte(g, 0x15) << 16;
    uint16_t vram = (uint16_t)(menu_object_scratch_bytes_byte(g, 0x08) | menu_object_scratch_bytes_byte(g, 0x09) << 8);
    upload_gate_rows(g, m_platform(s), rows, picks, source, vram);
    m_rts(s);
}

// $C2:7361
void menu_epoch_expand_gate_nibbles(Snes *s)
{
    unpack_gate_pixels(state_of(s));
    m_rts(s);
}

// $C2:7389
void menu_epoch_gate_graphics(Snes *s)
{
    build_the_dial(s);
    m_sep(s, CPU_FLAG_M);
    m_rts(s);
}

// $C6:E74E
/* The map screen's own check of a tile against an area, both in the object
 * mode's cells; the sides come back there. */
void menu_epoch_outside_sides(Snes *s)
{
    GameState *g = state_of(s);
    Area a = { menu_object_scratch_area_top(g), menu_object_scratch_area_bottom(g),
               menu_object_scratch_area_left(g), menu_object_scratch_area_right(g) };
    set_menu_object_scratch_area_outside(
        g, sides_outside(menu_object_scratch_area_tile_x(g), menu_object_scratch_area_tile_y(g), a));
    m_rtl(s);
}

// $C6:E797
/* The carry says whether the object is inside; the accumulator is 8-bit. */
void menu_epoch_in_area(Snes *s)
{
    GameState *g = state_of(s);
    m_set_flag(s, CPU_FLAG_C, in_area(g, menu_object_running(g)));
    m_sep(s, CPU_FLAG_M);
    m_rtl(s);
}

/* The gate screen's frame task ($C2:CECE), added in the menus' task slot $10
 * by $C2:8249, the original's. The setup is copied with a block move, which
 * leaves the data bank on work RAM's and Y past the state, which the caller
 * reads. */
enum { GATE_SCREEN_TASK = 0xCECE, GATE_SCREEN_TASK_SLOT = 0x0010 };

// $C2:CE9D
/* The original keeps its caller's P on the stack round the task's call, and
 * puts it back as it returns. */
void menu_start_gate_screen(Snes *s)
{
    GameState *g = state_of(s);
    m_push8(s, s->cpu.p);
    const VidBg bg3 = { .map_base = GATE_SCREEN_TILEMAP, .map_w = TILEMAP_TILES, .map_h = TILEMAP_TILES };
    vid_set_bg_map(m_platform(s), PF_AT(0xC2CEA2), VID_BG3, &bg3);
    start_counting(g);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    s->cpu.a = GATE_SCREEN_TASK;
    s->cpu.x = GATE_SCREEN_TASK_SLOT;
    m_jsr_dc(s, 0xC2CEB1, 0xC28249, DC_Y | DC_N | DC_V | DC_Z | DC_C | DC_DB);
    set_up_gate_screen(g);
    s->cpu.db = WORK_RAM_BANK;
    s->cpu.y = (uint16_t)(RAM_ABS(RAM_MENU_GATE_SCREEN_STATE) + MENU_GATE_SCREEN_STATE_COUNT);
    s->cpu.p = m_pull8(s);
    if (m_x8(s)) {
        s->cpu.x &= 0xFF;
        s->cpu.y &= 0xFF;
    }
    m_rts(s);
}
