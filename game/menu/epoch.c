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
 * the mode's line-effect slots (menu/line_effects.h). Temporary calculation
 * cells have no readers after retirement; their calibrated work remains
 * charged without writing the cells.
 *
 * Checked C calls load graphics and text. Held waits let the driver save
 * and resume the map and time-dial screens between frames.
 */
#include "menu/epoch.h"

#include "charge.h"
#include "menu/bridge.h"
#include "cpu_io.h"
#include "core/program.h"
#include "menu/display.h"
#include "menu/handlers.h"
#include "menu/line_effects.h"
#include "menu/map_picture.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/typeset.h"
#include "menu/object.h"
#include "menu/sprites.h"
#include "menu/vram.h"
#include "overworld/setup.h"
#include "platform/audio.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "sound/sound.h"
#include "vars.h"
#include "wram.h"

/* ---- What both screens use ---------------------------------------------------------------- */

/* The turns' sine and cosine are the menus' (menu_sine, menu_cosine). */
enum { EIGHTH_TURN = MENU_SINE_STEPS / 8 };

/* The mode's work RAM bank, where its buffers are. */
enum { WORK_RAM_BANK = 0x7E, EXTRA_RAM_BANK = 0x7F };
static uint32_t in_work_ram(uint16_t at) { return (uint32_t)WORK_RAM_BANK << 16 | at; }

/* The Mode 7 layer's pixels and tile numbers share VRAM words: a tile's 64
 * pixels are a byte each in the high halves of its 64 words, and the plane's
 * 128 by 128 tile numbers the low halves of the first 16384. */
enum { MODE7_TILE_PIXELS = MENU_MODE7_TILE_PIXELS, MODE7_MAP_ROW = 128 };

/* How VRAM steps as the screens' uploads go: a word at a time, stepping
 * after its high byte, or (the plane's tile numbers, the low halves alone)
 * after its low byte. */
static const VidVramPort after_high = { .step = 1, .after_high = true };
static const VidVramPort after_low = { .step = 1, .after_high = false };

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
}

/* The picture (menu/map_picture.h) is built in the mode's upload buffer
 * four map rows (a row of Mode 7 tiles, twelve) at a time and sent to the
 * plane's tiles from tile 1. */
enum { MAP_COLUMNS = MENU_MAP_COLUMNS, MAP_ROWS = MENU_MAP_ROWS, PICTURE_TILES_ACROSS = MENU_MAP_PICTURE_TILES_ACROSS, PICTURE_TILES_DOWN = MENU_MAP_PICTURE_TILES_DOWN, TILE_ROW_BYTES = MENU_MAP_PICTURE_TILE_ROW, MAP_ROWS_A_TILE_ROW = MENU_MAP_ROWS_A_TILE_ROW, FIRST_PICTURE_TILE = 1,  };
enum { GROUP_BYTES = MENU_MAP_GROUP_BYTES, TOP_RIGHT = MENU_MAP_TOP_RIGHT, BOTTOM_RIGHT = MENU_MAP_BOTTOM_RIGHT };
enum { BLACK = 0 };

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

/* The view's pixel: the map's, black where its colour isn't shown. */
static MenuMapPixel group_pixel(const GameState *g, uint16_t group, unsigned corner, uint8_t palette)
{
    MenuMapPixel p = menu_map_pixel(g, group_word(g, group, corner), palette);
    if (p.drawn && !shown(g, p.pixel))
        p.pixel = BLACK;
    return p;
}

static void put_pixel(GameState *g, uint16_t buffer, uint16_t at, MenuMapPixel p)
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
    uint16_t at = menu_map_picture_offset(column, row);
    /* Both palettes are read before either pixel is drawn: where the buffer
     * lies over the groups, the first pixel can change the second's tile. */
    uint8_t top = menu_map_palette(group_word(g, record, TOP_RIGHT));
    uint8_t bottom = menu_map_palette(group_word(g, record, BOTTOM_RIGHT));
    put_pixel(g, buffer, at, group_pixel(g, record, TOP_RIGHT, top));
    put_pixel(g, buffer, (uint16_t)(at + 8), group_pixel(g, record, BOTTOM_RIGHT, bottom));
    return record;
}

/* The first strip clears a full kilobyte. Its storage must stay above
 * low RAM, where the mode's working cells and stacks live, and within its
 * bank. Later strips use less of the same buffer. */
static bool map_picture_buffer_fits(const GameState *g)
{
    enum { LOW_RAM_END = 0x2000, FIRST_STRIP_BYTES = TILE_ROW_BYTES + TILE_ROW_BYTES / 3 };
    uint16_t buffer = (uint16_t)menu_object_upload_buffer(g);
    return buffer >= LOW_RAM_END && (uint32_t)buffer + FIRST_STRIP_BYTES <= 0x10000u;
}

/* Clear the mode's upload buffer for a tile row, with a third more space
 * for the first strip, and return its place. */
static uint16_t start_tile_row(GameState *g, uint16_t bytes)
{
    uint16_t buffer = (uint16_t)menu_object_upload_buffer(g);
    wram_fill8(g, in_work_ram(buffer), bytes, 0);
    charge_block_move(g, bytes);
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
            vid_send_vram_half(pf, PF_AT(0xC264EB), (uint16_t)(first_tile * MODE7_TILE_PIXELS),
                                 PF_GAME(menu_object_upload_buffer(g)), TILE_ROW_BYTES, VID_VRAM_HIGH_BYTES,
                                 &after_high);
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
    charge_block_move(g, NAME_TABLE_BYTES);
    uint8_t tile = FIRST_PICTURE_TILE;
    for (unsigned row = 0; row < PICTURE_TILES_DOWN; row++)
        for (unsigned column = 0; column < PICTURE_TILES_ACROSS; column++)
            set_menu_map_name_table(g, (PICTURE_ROW + row) * NAME_ROWS_SENT + PICTURE_COLUMN + column, tile++);
    for (unsigned row = 0; row < NAME_ROWS_SENT; row++)
        vid_send_vram_half(pf, PF_AT(0xC2662F), (uint16_t)(row * MODE7_MAP_ROW),
                           PF_GAME(RAM_MENU_MAP_NAME_TABLE + row * NAME_ROWS_SENT), NAME_ROWS_SENT,
                           VID_VRAM_LOW_BYTES, &after_low);
}

/* What each part of building the view leaves in the mode's cells. */
enum { CELL_NAMES_LEFT = 0x00, CELL_NAMES_ROW = 0x01, CELL_BUFFER_AT = 0x05, CELL_NEXT_TILE = 0x08, CELL_NEXT_SOURCE = 0x0A, CELL_NEXT_VRAM = 0x0C, CELL_LAST_GROUP = 0x0E, CELL_NAMES_AT = 0x10, CELL_OVER_MAP = 0x13, CELL_LAND_GROUPS = 0x16, CELL_OVER_GROUPS = 0x19 };

// $C2:63DF
/* The map view's plane, from the world map just loaded: every tile's colour,
 * the picture drawn from them, and the picture laid out on the plane. */
static void build_map_view(GameState *g, Platform *pf)
{
    sample_map_colours(g);
    (void)draw_map_picture(g, pf);
    place_map_picture(g, pf);
}

/* ---- The map view: its graphics --------------------------------------------------------------- */

/* The graphics set two of the world maps' markers are drawn from changes
 * with the story: before it the map uses the set every map starts with. */
enum { EARLY_SET = 0x6F, SET_OF_A_LATER_STORY = 0x6C, SET_OF_THE_EPOCH_WINGS = 0x6B };
enum { STORY_OF_THE_LATER_SET = 0x36, EPOCH_WINGS_FLAG = 0x01 };

/* The view's markers are glyphs of the proportional font: this map's string
 * of the table at $C6:F400, set by the typesetter (typeset.c), its twelve
 * glyphs in one call, in sixteen-colour tiles laid out a tile further left,
 * into its buffer at $7E:8621. */
enum { MAP_VIEW_SETS = 0xC6F400, MAP_VIEW_GLYPHS = 0x0C };

static void request_map_view_graphics(GameState *g)
{
    uint8_t set = menu_map_view_graphics(g, world_map(g));
    if ((set == SET_OF_A_LATER_STORY && menu_story_progress(g) < STORY_OF_THE_LATER_SET)
        || (set == SET_OF_THE_EPOCH_WINGS && !(menu_story_flags_epoch(g) & EPOCH_WINGS_FLAG)))
        set = EARLY_SET;
    set_menu_text_string(g, set);
    set_menu_text_strings(g, MAP_VIEW_SETS);
    set_menu_text_layout(g, MENU_TEXT_A_TILE_FURTHER_LEFT);
    set_menu_text_tiles(g, RAM_MENU_MAP_GRAPHICS);
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
 * zoom, a smaller picture). */
static uint16_t size_at_the_unit(int16_t value) { return (uint16_t)((value < 0 ? -value : value) * MATRIX_SCALE); }

static int16_t zoomed(int16_t value, uint8_t zoom) { return menu_signed_quotient(size_at_the_unit(value), zoom, value < 0); }

// $C2:6736, $C2:67B3
/* Turn the picture by the cursor's angle and zoom it by its zooms across and
 * down. The original takes the angle's sine from another object's angle,
 * `sine_from`: the object it last started or the cursor, which in the game
 * always has the cursor's angle (menu_epoch_map_cursor). */
static void turn_map_view(GameState *g, MenuObject cursor, MenuObject sine_from)
{
    uint16_t zoom_x = menu_object_slots_zoom_x(g, cursor), zoom_y = menu_object_slots_zoom_y(g, cursor);
    int16_t sin = menu_sine(g, menu_object_slots_phase(g, sine_from));
    int16_t cos = menu_cosine(g, menu_object_slots_phase(g, cursor));
    const int16_t matrix[4] = { zoomed(cos, (uint8_t)zoom_x), zoomed(sin, (uint8_t)zoom_x),
                                (int16_t)-zoomed(sin, (uint8_t)zoom_y), zoomed(cos, (uint8_t)zoom_y) };
    set_mode7_matrix(g, matrix[0], matrix[1], matrix[2], matrix[3]);
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
    menu_start_line_effect(g, pf, 0xC2694F, GRADIENT_SLOT, VID_LINE_FIXED_COLOR);
    set_menu_object_hdma_pointers(g, 0, (uint16_t)RAM_MENU_SLOT1_LINE_VALUES);
    set_menu_object_hdma_pointers(g, 1, (uint16_t)(RAM_MENU_SLOT1_LINE_VALUES + GRADIENT_HALF));
}

/* ---- The time dial: its periods ------------------------------------------------------------- */

static bool later_periods(const GameState *g) { return menu_story_progress(g) >= MENU_LATER_PERIODS; }

// $C2:747A
/* The period the party is in: the one whose location is here, or the last. */
static uint8_t period_here(GameState *g)
{
    for (uint8_t period = 0; period < MENU_PERIODS; period++)
        if (menu_period_location(g, later_periods(g), period) == location_here(g))
            return period;
    return MENU_PERIODS - 1;
}

// $C2:745A
/* The location a period travels to. */
static uint16_t period_location(GameState *g, uint8_t period)
{
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

// $C2:6F33
static SignedProduct multiply_signed(int8_t by, int16_t value)
{
    SignedProduct p;
    p.by = (uint8_t)(by < 0 ? -by : by);
    p.value = (uint16_t)(value < 0 ? -value : value);
    uint32_t magnitude = (uint32_t)p.by * p.value;
    p.product = (by < 0) != (value < 0) ? -(int32_t)magnitude : (int32_t)magnitude;
    return p;
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
    int16_t cos = menu_cosine(g, angle), sin = menu_sine(g, angle);
    SignedProduct last = { 0 };
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
        set_menu_gate_corners(g, 2 * c, corners[c].x);
        set_menu_gate_corners(g, 2 * c + 1, corners[c].y);
    }
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
    e.step = (uint16_t)menu_signed_quotient((uint16_t)(e.width << 8), e.height, leftward);
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

/* The original's time for the window follows its lines more closely than
 * the port's accesses do: each line runs its loops over every edge. It is
 * charged, in accesses at the window's rate. */
enum { LINE_ACCESSES = 7, FIXED_ACCESSES = 330 };

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
    charge_block_move(g, 2 * (MIDDLE_TO - MIDDLE_FROM - 1));

    uint16_t angle = menu_object_slots_angle(g, dial);
    ScreenPoint corners[CORNERS];
    turn_corners(g, angle, corners);

    WindowDrawn w = { .table = table, .bank = bank, .cos = menu_cosine(g, angle), .sin = menu_sine(g, angle) };
    uint8_t top = 0xFF, bottom = 0;
    for (unsigned c = 0; c < CORNERS; c++) {
        ScreenPoint to = corners[(c + 1) % CORNERS];
        GateEdge edge = edge_between(corners[c], to);
        w.last_edge = edge;
        w.last_from = corners[c];
        w.last_to = to;
        if (!edge.drawn)
            continue;
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
    charge_as(g, 0xC26CC1, FIXED_ACCESSES + LINE_ACCESSES * lines);
    w.after_the_last_line = (uint8_t)(top + lines);
    w.bottom = bottom;
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
    charge_block_move(g, 4 * MENU_LINE_TABLE_LINES);
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
    wram_write16(g, in_work_ram((uint16_t)(cosines + 2 * line)), (uint16_t)menu_cosine(g, angle));
    wram_write16(g, in_work_ram((uint16_t)(sines + 2 * line)), (uint16_t)menu_sine(g, (uint16_t)(angle + EIGHTH_TURN)));
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
    charge_as(g, 0xC26F85, 2 * 2 * MOVED);
    uint16_t angle = menu_object_slots_angle(g, spin);
    gate_spin_line(g, cosines, sines, LAST_LINE, angle);
    set_menu_object_slots_angle(g, spin, (uint16_t)(angle + SPIN_SPEED));
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

static void upload_gate_tiles(GameState *g, Platform *pf)
{
    for (unsigned row = 0; row < GATE_TILE_ROWS; row++) {
        uint16_t vram = menu_gate_tile_row(g, row);
        for (unsigned piece = 0; piece < GATE_PIECES; piece++)
            vid_send_vram(pf, PF_AT(0xC2726D), (uint16_t)(vram + piece * GATE_PIECE_WORDS),
                            PF_GAME(RAM_MENU_MODE7_PLANE + piece * GATE_PIECE_BYTES), GATE_PIECE_BYTES, &after_high);
    }
}

/* A tilemap row is 32 bytes, sixteen words. */
enum { TILEMAP_ROW_BYTES = 0x20, TILEMAP_ROW_WORDS = 0x10 };

/* Rows of the gate's tilemap: each picks a row of the source by its number
 * in a table, and goes to the next row of VRAM. A count of 0 is 65536. */
static void upload_gate_rows(GameState *g, Platform *pf, uint16_t rows, uint32_t picks, uint32_t source, uint16_t vram)
{
    uint16_t left = rows;
    do {
        uint8_t pick = menu_gate_row_pick(g, picks);
        picks = (picks & 0xFF0000u) | (uint16_t)(picks + 1);
        uint32_t from = (source & 0xFF0000u) | (uint16_t)(pick * TILEMAP_ROW_BYTES + source);
        vid_send_vram(pf, PF_AT(0xC272EE), vram, PF_GAME(from), TILEMAP_ROW_BYTES, &after_high);
        vram = (uint16_t)(vram + TILEMAP_ROW_WORDS);
    } while (--left != 0);
}

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
}

/* The gate's picture to the plane's first tiles. */
static void upload_gate_pixels(Platform *pf)
{
    vid_send_vram_half(pf, PF_AT(0xC2722F), 0x0000, PF_GAME(RAM_MENU_MODE7_PLANE), 2 * GATE_PIXEL_PAIRS,
                       VID_VRAM_HIGH_BYTES, &after_high);
}

/* The dial's palette: kept as the mode loaded it, and sent with one colour
 * white. */
enum { WHITE = 0xFFFF, WHITE_COLOUR = 0xD1, PALETTE_BYTES = 0x200 };

static void keep_dial_palette(GameState *g)
{
    wram_copy(g, RAM_MENU_LOADED_PALETTE, RAM_MENU_GATE_PALETTE_COPY, PALETTE_BYTES);
    set_menu_loaded_palette(g, WHITE_COLOUR, WHITE);
    wram_copy(g, RAM_MENU_LOADED_PALETTE, RAM_MENU_OBJECT_PALETTE, PALETTE_BYTES);
    charge_block_move(g, 2 * PALETTE_BYTES);
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

/* The part of each world map the Epoch may not come down in (its flight's
 * landing, overworld/epoch.c); a map with none has none. */
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

/* Whether the object is inside that part of the world map `map`. */
bool menu_epoch_kept_off_the_ground_of(GameState *g, MenuObject o, unsigned map)
{
    MenuPlace p = menu_object_place(g, o);
    uint16_t tile_x = menu_pixel(p.x) >> TILE_SHIFT, tile_y = menu_pixel(p.y) >> TILE_SHIFT;
    if (!world_map_areas[map].has_area) {
        return false;
    }
    const Area *a = &world_map_areas[map].area;
    uint8_t sides = sides_outside((uint8_t)tile_x, (uint8_t)tile_y, *a);
    return sides == 0;
}

// $C6:E797
/* Whether the object is inside that part of the world map the party is on. */
static bool in_area(GameState *g, MenuObject o)
{
    return menu_epoch_kept_off_the_ground_of(g, o, (uint16_t)(location_id(g) - MENU_FIRST_WORLD_MAP));
}

bool menu_epoch_kept_off_the_ground(GameState *g, MenuObject epoch) { return in_area(g, epoch); }

/* ---- The screens, as one sequence each -------------------------------------------------------- */

/* Which screen a step is for, where the two make it at different places. */
typedef enum { THE_MAP_VIEW, THE_TIME_DIAL } EpochScreen;

/* The waits the screens make while the sound's requests go on: the map view
 * as it closes, the dial as it opens and as it closes. */
typedef enum { MAP_VIEW_CLOSING, DIAL_OPENING, DIAL_CLOSING } EpochWait;
static const uint16_t epoch_wait_frames[] = { [MAP_VIEW_CLOSING] = 0x11, [DIAL_OPENING] = 0x12, [DIAL_CLOSING] = 0x12 };

/* The dial's streams: the tiles from bank C3, the palette and the tilemap's
 * rows from bank C6, each unpacked into bank $7F where the stream's table
 * says. */
typedef enum { GATE_TILES_STREAM, GATE_PALETTE_STREAM, GATE_ROWS_STREAM } GateStream;
static const uint8_t gate_stream_bank[] = { [GATE_TILES_STREAM] = 0xC3, [GATE_PALETTE_STREAM] = 0xC6,
                                            [GATE_ROWS_STREAM] = 0xC6 };

/* The screen services: graphics and text, then waits while the objects
 * and sound advance. Machine bridges check the complete entry contracts;
 * the native program checks their game-state preconditions. False means
 * the screen stopped before its next step. */
typedef struct {
    bool (*clear_vram)(void *ctx, EpochScreen screen);
    bool (*load_markers)(void *ctx);
    bool (*build_map)(void *ctx);
    bool (*build_gate)(void *ctx);
    bool (*load_stream)(void *ctx, GateStream stream);
    bool (*start_text)(void *ctx);
    bool (*set_glyphs)(void *ctx);
    bool (*wait_a_frame)(void *ctx, EpochScreen screen);
    bool (*wait_frames)(void *ctx, EpochWait wait);
    /* A script started, which on the machine leaves A what the waits after
     * it read. */
    void (*script_begun)(void *ctx, uint32_t script);
} EpochScreenSteps;

/* What both screens do first: the menus' VRAM cleared, the mode's upload
 * queue reset and every object slot freed. */
static bool clear_the_screen_with(GameState *g, EpochScreen screen, const EpochScreenSteps *steps, void *ctx)
{
    if (!steps->clear_vram(ctx, screen))
        return false;
    TIMED_AS(g, 0xC203EF, menu_object_reset_the_upload_queue(g));
    TIMED_AS(g, 0xC20471, menu_free_every_object(g));
    return true;
}

/* The screen runs until the menus' mode leaves `mode`, a frame at a time. */
static bool run_until_the_mode_changes_with(GameState *g, EpochScreen screen, uint8_t mode,
                                            const EpochScreenSteps *steps, void *ctx)
{
    do
        if (!steps->wait_a_frame(ctx, screen))
            return false;
    while (menu_mode(g) == mode);
    return true;
}

static void start_a_script_with(GameState *g, uint32_t script, const EpochScreenSteps *steps, void *ctx)
{
    TIMED_AS(g, 0xC204D9, menu_start_script(g, script, MENU_ORDINARY_SLOT));
    steps->script_begun(ctx, script);
}

/* The screen shows from the next frame: the mode sends its frame and palette,
 * and the vblank and joypad interrupts are on. */
static void show_from_the_next_frame(GameState *g, Platform *pf, uint32_t site)
{
    set_menu_object_frame_state(g, (uint8_t)(menu_object_frame_state(g) | SEND_FRAME_AND_PALETTE));
    tm_enable_interrupts(pf, PF_AT(site), TM_INT_VBLANK | TM_INT_AUTO_JOYPAD);
}

/* And back to the menus' own display as it ends. */
static void back_to_the_menus(GameState *g, Platform *pf, uint32_t sprites_site, uint32_t interrupts_site,
                              uint32_t uploads_site, uint32_t effects_site)
{
    set_menu_object_shadows_bg_mode(g, MENU_BG_MODE);
    const VidObjConfig menus = { .size_select = 3, .base = 0x0000, .name_select = 0 };
    vid_set_obj_config(pf, PF_AT(sprites_site), &menus);
    tm_enable_interrupts(pf, PF_AT(interrupts_site), 0);
    vid_stop_uploads(pf, PF_AT(uploads_site));
    vid_enable_line_effects(pf, PF_AT(effects_site), 0);
}

static void finish_the_map_view(GameState *g, Platform *pf)
{
    back_to_the_menus(g, pf, 0xC263D0, 0xC263D5, 0xC263D8, 0xC263DB);
}

static void finish_the_time_dial(GameState *g, Platform *pf)
{
    back_to_the_menus(g, pf, 0xC26ABA, 0xC26ABE, 0xC26AC1, 0xC26AC4);
}

// $C2:6965
/* Set the view's marker glyphs (the typesetter's start and a call), and send
 * them to VRAM $6200. */
static bool load_map_view_graphics_with(GameState *g, Platform *pf, const EpochScreenSteps *steps, void *ctx)
{
    TIMED_AS(g, 0xC25798, overworld_clear_the_sign_label(g));   /* clear the loader's buffer */
    request_map_view_graphics(g);
    if (!steps->start_text(ctx))
        return false;
    set_menu_text_budget(g, MAP_VIEW_GLYPHS);
    if (!steps->set_glyphs(ctx))
        return false;
    vid_send_vram(pf, PF_AT(0xC269C8), 0x6200, PF_GAME(RAM_MENU_MAP_GRAPHICS), 0x0400, &after_high);
    return true;
}

// $C2:63DF
/* Load the world map's tiles (overworld/setup.c), then build the view's
 * plane from them. False where the unpacker stopped. */
static bool build_map_view_from(GameState *g, Platform *pf, const Unpacker *u)
{
    bool loaded = false;
    TIMED_AS(g, 0xC227DE, loaded = overworld_load_the_map_tiles(g, u));
    if (!loaded)
        return false;
    build_map_view(g, pf);
    return true;
}

// $C2:631F
/* The map view: Mode 7 behind sprites at $6000, the colour math adding the
 * fixed colour to the backdrop, its graphics and plane built, the cursor and
 * its script started, and the screen run until the menus' mode leaves it. */
enum { MAP_VIEW_MODE = 0x06, BACKDROP = 0x20, MAP_VIEW_CURSOR = 0x666C, MAP_VIEW_SCRIPT = 0xC269F8 };

static bool show_the_map_view_with(GameState *g, Platform *pf, const EpochScreenSteps *steps, void *ctx)
{
    set_menu_object_shadows_bg_mode(g, MODE_7);
    const VidObjConfig sprites = { .size_select = 3, .base = 0x6000, .name_select = 0 };
    vid_set_obj_config(pf, PF_AT(0xC26325), &sprites);
    set_menu_object_shadows_math_control(g, 0);
    set_menu_object_shadows_math_layers(g, BACKDROP);
    if (!clear_the_screen_with(g, THE_MAP_VIEW, steps, ctx))
        return false;
    if (!steps->load_markers(ctx))
        return false;
    TIMED_AS(g, 0xC268F4, map_view_gradient(g, pf));
    if (!steps->build_map(ctx))
        return false;

    vid_send_vram(pf, PF_AT(0xC26340), 0x6000, menu_map_view_tiles(false), 0x0020, &after_high);
    vid_send_vram(pf, PF_AT(0xC26367), 0x6020, menu_map_view_tiles(true), 0x0020, &after_high);
    enum { VIEW_COLOURS = 0x100, VIEW_COLOUR_BYTES = 0x21 };
    set_menu_object_palette_byte(g, 0, 0);
    set_menu_object_palette_byte(g, 1, 0);
    uint32_t silenced = charge_uncounted_begin(g);
    for (unsigned i = 0; i < VIEW_COLOUR_BYTES; i++)
        set_menu_object_palette_byte(g, VIEW_COLOURS + i, menu_map_view_colour_byte(g, i));
    charge_uncounted_end(g, silenced);
    charge_block_move(g, VIEW_COLOUR_BYTES);

    MenuObject cursor;
    TIMED_AS(g, 0xC2048A, (void)menu_start_object(g, MAP_VIEW_CURSOR, MENU_ORDINARY_SLOT, &cursor));
    start_a_script_with(g, MAP_VIEW_SCRIPT, steps, ctx);
    show_from_the_next_frame(g, pf, 0xC263B7);
    if (!run_until_the_mode_changes_with(g, THE_MAP_VIEW, MAP_VIEW_MODE, steps, ctx)
        || !steps->wait_frames(ctx, MAP_VIEW_CLOSING))
        return false;
    finish_the_map_view(g, pf);
    return true;
}

// $C2:71F9
static bool load_gate_stream_from(GameState *g, const Unpacker *u, uint8_t stream, uint8_t bank)
{
    return u->unpack(u->ctx, 0xC2721B, (uint32_t)bank << 16 | menu_gate_stream_source(g, stream),
                     (uint32_t)EXTRA_RAM_BANK << 16 | menu_gate_stream_dest(g, stream));
}

// $C2:7389
/* Everything the dial shows: the gate's tiles, its tilemap, its palette and
 * its picture, and the objects that drive it. */
enum { GATE_TILE_VRAM = 0x6400, GATE_TILEMAP_VRAM = 0x6000, GATE_TILEMAP_ROWS = 0x40, GATE_TILES_BYTES = 0x800 };

static bool build_the_dial_with(GameState *g, Platform *pf, const EpochScreenSteps *steps, void *ctx)
{
    if (!steps->load_stream(ctx, GATE_TILES_STREAM))
        return false;
    vid_send_vram(pf, PF_AT(0xC27393), GATE_TILE_VRAM, PF_GAME(RAM_MENU_GATE_TILES), GATE_TILES_BYTES, &after_high);
    TIMED_AS(g, 0xC272D8, upload_gate_rows(g, pf, GATE_TILEMAP_ROWS, MENU_GATE_TILEMAP_ROWS, RAM_MENU_MODE7_PLANE,
                                           GATE_TILEMAP_VRAM));
    if (!steps->load_stream(ctx, GATE_PALETTE_STREAM))
        return false;
    keep_dial_palette(g);
    if (!steps->load_stream(ctx, GATE_ROWS_STREAM))
        return false;
    TIMED_AS(g, 0xC27254, upload_gate_tiles(g, pf));
    TIMED_AS(g, 0xC27361, unpack_gate_pixels(g));
    TIMED_AS(g, 0xC2722C, upload_gate_pixels(pf));
    start_dial_objects(g);
    return true;
}

// $C2:6A34
/* The time dial: Mode 7 behind sprites at $6000, its matrix a unit's
 * across and down, window 1 cutting the gate out of both screens, the colour
 * math adding the fixed colour to what the window leaves; the gate built and
 * its objects started, and the screen run until the menus' mode leaves it. */
enum { DIAL_MODE = 0x08, DIAL_SCROLL_X = 0xFF80, DIAL_SCROLL_Y = 0xFF90, UNIT_HIGH = 0x01 };
enum { WINDOW1_ON_COLOUR = 0x20, BG1_AND_SPRITES = 0x11, MATH_INSIDE_THE_WINDOW = 0x10, MATH_LAYERS = 0x17 };
enum { FIXED_LEVEL = 0x0F };

static bool show_the_time_dial_with(GameState *g, Platform *pf, const EpochScreenSteps *steps, void *ctx)
{
    if (!clear_the_screen_with(g, THE_TIME_DIAL, steps, ctx))
        return false;
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
    if (!steps->build_gate(ctx))
        return false;
    show_from_the_next_frame(g, pf, 0xC26A93);
    if (!steps->wait_frames(ctx, DIAL_OPENING) || !run_until_the_mode_changes_with(g, THE_TIME_DIAL, DIAL_MODE, steps, ctx))
        return false;
    start_a_script_with(g, DIAL_ENDING_SCRIPT, steps, ctx);
    if (!steps->wait_frames(ctx, DIAL_CLOSING))
        return false;
    finish_the_time_dial(g, pf);
    return true;
}

/* ---- As C ---- */

typedef struct {
    GameState *game;
    Platform *pf;
} ScreenInC;

static bool clear_all_of_vram(void *ctx, EpochScreen screen)
{
    (void)screen;
    ScreenInC *c = ctx;
    TIMED_AS(c->game, 0xC226A8, menu_clear_all_of_vram(c->pf));
    return true;
}

static const EpochScreenSteps screen_steps_in_c;

static bool load_the_markers(void *ctx)
{
    ScreenInC *c = ctx;
    bool loaded = false;
    TIMED_AS(c->game, 0xC26965, loaded = load_map_view_graphics_with(c->game, c->pf, &screen_steps_in_c, c));
    return loaded;
}

static bool build_the_map(void *ctx)
{
    ScreenInC *c = ctx;
    if (!overworld_map_tiles_can_load(c->game) || !map_picture_buffer_fits(c->game)) {
        program_breaks_a_contract(c->game, 0xC263DF, "a map view without its map record or a strip buffer clear of low RAM");
        return false;
    }
    Unpacker u = overworld_unpacker(c->game);
    bool built = false;
    TIMED_AS(c->game, 0xC263DF, built = build_map_view_from(c->game, c->pf, &u));
    return built;
}

static bool build_the_gate(void *ctx)
{
    ScreenInC *c = ctx;
    if (!menu_object_address_names_one(menu_object_current(c->game))) {
        program_breaks_a_contract(c->game, 0xC27389, "a time dial built with no object running");
        return false;
    }
    bool built = false;
    TIMED_AS(c->game, 0xC27389, built = build_the_dial_with(c->game, c->pf, &screen_steps_in_c, c));
    return built;
}

static bool load_a_stream(void *ctx, GateStream stream)
{
    ScreenInC *c = ctx;
    Unpacker u = overworld_unpacker(c->game);
    bool loaded = false;
    TIMED_AS(c->game, 0xC271F9, loaded = load_gate_stream_from(c->game, &u, (uint8_t)stream, gate_stream_bank[stream]));
    return loaded;
}

static bool start_the_text(void *ctx)
{
    ScreenInC *c = ctx;
    if (!menu_text_can_start(c->game)) {
        program_breaks_a_contract(c->game, 0xC257DF, "a text the typesetter doesn't start");
        return false;
    }
    TIMED_AS(c->game, 0xC257DF, menu_text_start(c->game));
    return true;
}

static bool set_the_glyphs(void *ctx)
{
    ScreenInC *c = ctx;
    if (!menu_text_can_carry_on(c->game)) {
        program_breaks_a_contract(c->game, 0xC25823, "a text the typesetter doesn't go on with");
        return false;
    }
    TIMED_AS(c->game, 0xC25823, menu_text_go_on(c->game));
    return true;
}

static bool wait_for_the_next_frame(void *ctx, EpochScreen screen)
{
    (void)screen;
    ScreenInC *c = ctx;
    menu_wait_for_the_next_frame(c->game, c->pf);
    return true;
}

static bool wait_frames_sending_the_sound(void *ctx, EpochWait wait)
{
    ScreenInC *c = ctx;
    menu_wait_frames_sending_the_sound(c->game, c->pf, epoch_wait_frames[wait]);
    return true;
}

static void nothing_left(void *ctx, uint32_t script) { (void)ctx, (void)script; }

static const EpochScreenSteps screen_steps_in_c = {
    clear_all_of_vram, load_the_markers, build_the_map, build_the_gate, load_a_stream, start_the_text,
    set_the_glyphs, wait_for_the_next_frame, wait_frames_sending_the_sound, nothing_left,
};

bool menu_epoch_map_view_shown(GameState *g, Platform *pf)
{
    ScreenInC c = { g, pf };
    return show_the_map_view_with(g, pf, &screen_steps_in_c, &c);
}

bool menu_epoch_time_dial_shown(GameState *g, Platform *pf)
{
    ScreenInC c = { g, pf };
    return show_the_time_dial_with(g, pf, &screen_steps_in_c, &c);
}

/* The objects whose handlers go by steps are on one of theirs: the original
 * jumps through a table of them, and past its end into code. */
static bool cursor_takes(const GameState *g, MenuObject cursor)
{
    return menu_object_slots_step(g, cursor) <= CURSOR_WALKS_BACK;
}

/* The dial draws its window in the tables its first step put in work RAM. */
static bool dial_takes(const GameState *g, MenuObject dial)
{
    uint8_t step = menu_object_slots_step(g, dial);
    uint8_t bank = menu_object_scratch_gate_window_bank(g);
    return step <= DIAL_TURNS && (step == DIAL_STARTS || bank == WORK_RAM_BANK || bank == EXTRA_RAM_BANK);
}

static bool spin_takes(const GameState *g, MenuObject spin) { return menu_object_slots_step(g, spin) < 2; }

/* The markers and the digits run a sprite script, which reads as the
 * language wherever the game gives one (menu/sprites.c): the one the object
 * has, once it has set its own up, and the ones a digit is given for its
 * period; the sprite list is as the mode lays it out and the upload queue
 * whole. */
static bool marker_takes(const GameState *g, MenuObject o)
{
    return menu_sprite_list_is_sound(g) && menu_object_queue_is_whole(g)
           && (menu_object_slots_step(g, o) == 0 || menu_sprite_script_is_well_formed(g, o));
}

static bool digit_takes(const GameState *g, MenuObject digit)
{
    uint8_t step = menu_object_slots_step(g, digit);
    uint8_t period = menu_object_slots_period(g, digit);
    /* A hidden digit shows nothing until it is given its showing script. */
    bool hidden = step <= DIGIT_HIDDEN;
    return step <= DIGIT_HIDING && menu_sprite_list_is_sound(g) && menu_object_queue_is_whole(g)
           && (hidden || menu_sprite_script_is_well_formed(g, digit))
           && menu_given_sprite_script_is_well_formed(g, menu_digit_show_script(g, period))
           && menu_given_sprite_script_is_well_formed(g, menu_digit_hide_script(g, period));
}

/* The Day of Lavos's turning world (menu/turning_world.c) builds its picture
 * of the 1000 A.D. map from the same pieces: its scene's script calls the
 * colours' sampling and the picture's laying out on their own. What each
 * leaves in X, A's high byte and the carry is what liveness keeps live after
 * a script's call (menu/map_scenes.c): the colours' table, the count of
 * tiles, and the carry of the sampling's last count; the laying out's size
 * of a row sent, and the carry of moving past the last row's source. */
enum { COLOURS_AT = 0xAB25 };

/* ---- What a world map's scene calls of the map view ---------------------------------------- */

/* A scene that lays a world map flat in Mode 7 (the Day of Lavos's turning
 * world, menu/turning_world.c) makes its picture as the map view does: each
 * map tile's colour sampled, the picture laid out on the plane, and the
 * view's blue backdrop behind it. */
static bool sample_the_map_colours(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)pf, (void)self, (void)far;
    sample_map_colours(g);
    return true;
}

static bool lay_out_the_map_picture(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)self, (void)far;
    place_map_picture(g, pf);
    return true;
}

static bool show_the_backdrop(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)self, (void)far;
    map_view_gradient(g, pf);
    return true;
}

MENU_SCRIPT_ROUTINES(menu_map_view_routines,
    { 0xC26400, "the map's colours sampled", NULL, sample_the_map_colours },
    { 0xC265B2, "the map's picture laid out", NULL, lay_out_the_map_picture },
    { 0xC268F4, "the map view's backdrop", NULL, show_the_backdrop },
);

/* ---- The screens' objects as C (menu/handlers.h) ------------------------------------------- */

/* Each object's turn, which the vblank handler runs: the cursor done once it
 * has walked back, a marker once it isn't shown, and the dial, its gate's
 * spin and its digits never. */
static MenuTurn cursor_turn(GameState *g, Platform *pf, MenuObject self)
{
    (void)pf;
    return menu_turn_done_if(map_view_cursor(g, self) == CURSOR_DONE);
}

static MenuTurn party_marker_turn(GameState *g, Platform *pf, MenuObject self)
{
    (void)pf;
    return menu_turn_done_if(party_marker(g, self) != MARKER_SHOWN);
}

static MenuTurn epoch_marker_turn(GameState *g, Platform *pf, MenuObject self)
{
    (void)pf;
    return menu_turn_done_if(epoch_marker(g, self) != MARKER_SHOWN);
}

static MenuTurn landing_marker_turn(GameState *g, Platform *pf, MenuObject self)
{
    (void)pf;
    return menu_turn_done_if(landing_marker(g, self) != MARKER_SHOWN);
}

/* The buzz of a period the party is at goes to the sound driver from the
 * dial's own call. */
enum { BUZZ_SENT = 0xC26BE9, SOUND_DRIVER = 0xC70004 };

static MenuTurn dial_turn(GameState *g, Platform *pf, MenuObject self)
{
    if (time_dial(g, pf, self) == DIAL_BUZZES)
        au_command(pf, PF_CALL(BUZZ_SENT, SOUND_DRIVER),
                   (AudioCommand){ .command = SOUND_EFFECT_ON_VOICES_6_7, .p1 = BUZZ_EFFECT, .p2 = BUZZ_PAN, .count = 2 });
    return MENU_TURN_GOES_ON;
}

static MenuTurn spin_turn(GameState *g, Platform *pf, MenuObject self)
{
    gate_spin(g, pf, self);
    return MENU_TURN_GOES_ON;
}

static MenuTurn digit_turn(GameState *g, Platform *pf, MenuObject self)
{
    (void)pf;
    dial_digit(g, self);
    return MENU_TURN_GOES_ON;
}

MENU_HANDLERS(menu_epoch_handlers,
    { 0x666C, "the map view's cursor", cursor_takes, cursor_turn },
    { 0x67FA, "the map view's marker of the party", marker_takes, party_marker_turn },
    { 0x683B, "the map view's marker of the Epoch", marker_takes, epoch_marker_turn },
    { 0x68A3, "the map view's marker of the landing", marker_takes, landing_marker_turn },
    { DIAL_HANDLER, "the time dial", dial_takes, dial_turn },
    { SPIN_HANDLER, "the time gate's spin", spin_takes, spin_turn },
    { DIGIT_HANDLER, "a time dial's digit", digit_takes, digit_turn },
);
