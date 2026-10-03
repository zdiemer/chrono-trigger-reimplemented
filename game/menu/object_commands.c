/* Menu mode: the routines the world maps' scenes call from their scripts.
 *
 * The sprite-object mode plays a scene on each world map (menu/object.h), a
 * script for each of its objects. A script can call a routine by its long
 * address (menu/object_script.c's call command); these are the ones the
 * scenes call to do what the script language can't:
 *
 * - the scene's own state: its mode, which a script tests to pick its way
 *   (set in the story's flags), and the flag a scene clears as it ends;
 * - the song: whether the map screen that follows keeps the one playing;
 * - motion: an object that falls faster and faster, the layer over the map
 *   drifting with an object, the second plane bobbing up and down, an
 *   object jolted a random step in a random direction;
 * - pictures: the scenes' Mode 7 graphics and colours, decompressed into
 *   the Mode 7 plane's buffer and the loaded palette.
 *
 * The per-line effects the scenes start are menu/line_effects.c's, and the
 * Epoch's area check, which its map screen calls, is beside the Epoch's
 * screens in menu/epoch.c.
 *
 * After them the scripts read only X and the carry, so each entry says what
 * it leaves in those and declares the rest dead. */
#include "menu/object_commands.h"

#include "cpu_io.h"
#include "menu/menu_data.h"
#include "menu/object.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- the scene's own state ------------------------------------------------------------------ */

/* The scene's mode is kept in bits 2-5 of one of the story's flags (the map
 * screens' copy of them). */
enum { SCENE_MODE_BITS = 0x3C, SCENE_MODE_SHIFT = 2 };

// $C6:E237
static uint8_t scene_mode(const GameState *g)
{
    return (uint8_t)((menu_story_flags_scene_mode_bits(g) & SCENE_MODE_BITS) >> SCENE_MODE_SHIFT);
}

/* ---- motion --------------------------------------------------------------------------------- */

// $C6:E583, $C6:E590
/* Bob the second plane up or down a couple of lines (its scroll's low byte,
 * which the original steps on its own). */
static void bob_second_plane(GameState *g, bool up)
{
    enum { BOB_LINES = 2 };
    uint16_t scroll = menu_object_shadows_bg2_scroll_y(g);
    uint8_t line = (uint8_t)(up ? scroll - BOB_LINES : scroll + BOB_LINES);
    set_menu_object_shadows_bg2_scroll_y(g, (uint16_t)((scroll & 0xFF00) | line));
}

/* A 32-bit sum, and whether it ran past the top. */
static bool add_overflows(uint32_t *to, uint32_t by)
{
    uint32_t was = *to;
    *to += by;
    return *to < was;
}

// $C6:E77F
/* A falling object: its speed down grows by its acceleration. True where
 * the speed wrapped round. */
static bool accelerate(GameState *g, MenuObject o)
{
    MenuPlace v = menu_object_velocity(g, o);
    bool wrapped = add_overflows(&v.y, menu_object_slots_acceleration(g, o));
    menu_object_set_velocity(g, o, v);
    return wrapped;
}

/* How much the drift speeds up each frame, across and down. */
enum { DRIFT_SPEED_UP_X = 0x0DDB, DRIFT_SPEED_UP_Y = 0x0800 };

static uint32_t layer_place(uint16_t scroll, uint32_t place) { return (uint32_t)scroll << 16 | (uint16_t)place; }

// $C6:E2B5
/* The layer over the map drifts with the object: the object's velocity moves
 * the layer's scroll, the fraction of a pixel kept in the object's place, and
 * the scroll the menu code has left pending is added and used up; then the
 * drift speeds up. True where the speed down wrapped round. */
static bool drift(GameState *g, MenuObject o)
{
    MenuPlace p = menu_object_place(g, o), v = menu_object_velocity(g, o);
    uint32_t x = layer_place(menu_object_shadows_bg3_scroll_x(g), p.x) + v.x;
    uint32_t y = layer_place(menu_object_shadows_bg3_scroll_y(g), p.y) + v.y;
    p.x = (p.x & 0xFFFF0000u) | (uint16_t)x;
    p.y = (p.y & 0xFFFF0000u) | (uint16_t)y;
    menu_object_set_place(g, o, p);
    set_menu_object_shadows_bg3_scroll_x(g, (uint16_t)((x >> 16) + menu_pending_scroll_x(g)));
    set_menu_object_shadows_bg3_scroll_y(g, (uint16_t)((y >> 16) + menu_pending_scroll_y(g)));
    set_menu_pending_scroll_x(g, 0);
    set_menu_pending_scroll_y(g, 0);
    add_overflows(&v.x, DRIFT_SPEED_UP_X);
    bool wrapped = add_overflows(&v.y, DRIFT_SPEED_UP_Y);
    menu_object_set_velocity(g, o, v);
    return wrapped;
}

/* The menus' random bytes, taken in turn. */
static uint8_t next_random(GameState *g)
{
    uint8_t at = menu_random_index(g);
    set_menu_random_index(g, (uint8_t)(at + 1));
    return menu_random_byte(g, at);
}

/* The sine of an angle of 256 steps a turn, from -128 to 128: the table's,
 * whose bytes can't hold the quarter turns' 128, which are put in by hand. */
enum { QUARTER_TURN = 0x40, THREE_QUARTER_TURN = 0xC0, SINE_PEAK = 128 };

// $C2:2277
static int16_t sine(const GameState *g, uint8_t angle)
{
    if (angle == QUARTER_TURN)
        return SINE_PEAK;
    if (angle == THREE_QUARTER_TURN)
        return -SINE_PEAK;
    return menu_sine_step(g, angle);
}

// $C2:2273
static int16_t cosine(const GameState *g, uint8_t angle) { return sine(g, (uint8_t)(angle + QUARTER_TURN)); }

/* A jolt's size is up to 63, and it moves the object that many 256ths of the
 * sine along each axis, rounded toward zero. */
enum { JOLT_SIZES = 0x40, JOLT_UNIT = 256 };

static int16_t jolt_along(int16_t share, uint8_t size)
{
    int16_t moved = (int16_t)((share < 0 ? -share : share) * size / JOLT_UNIT);
    return share < 0 ? (int16_t)-moved : moved;
}

/* The original works the jolt out in scratch cells and the CPU's multiplier,
 * and leaves them: the direction and the size, and the multiplier holding the
 * second axis's product. */
static void leave_jolt(GameState *g, uint8_t angle, uint8_t size)
{
    set_menu_object_scratch_jolt_angle(g, angle);
    set_menu_object_scratch_jolt_size(g, size);
    int16_t s = sine(g, angle);
    hw_set_multiplicand(g, (uint8_t)(s < 0 ? -s : s));
    hw_multiply_by(g, size);
}

// $C6:E6D0
/* The object jumps a random distance in a random direction: one of the
 * menus' random bytes picks the direction, the next how far, up to 31 pixels
 * along an axis. Its place moves by whole pixels; the fractions stay. */
static void jolt(GameState *g, MenuObject o)
{
    uint8_t angle = next_random(g);
    uint8_t size = next_random(g) & (JOLT_SIZES - 1);
    set_menu_object_slots_x_pixel(g, o, (uint16_t)(menu_object_slots_x_pixel(g, o) + jolt_along(cosine(g, angle), size)));
    set_menu_object_slots_y_pixel(g, o, (uint16_t)(menu_object_slots_y_pixel(g, o) + jolt_along(sine(g, angle), size)));
    ORIGINAL_EFFECT(leave_jolt(g, angle, size));
}

/* ---- objects put on the map at random ----------------------------------------------------------- */

/* The view's top-left, in the map's tiles of 8 pixels: the second plane's,
 * which the objects on the map move with. A new object goes in just under
 * the view: 32 tiles down, the view being 28. */
enum { MAP_TILE_PIXELS = 8, UNDER_THE_VIEW = 0x20 };

static uint16_t under_the_view(const GameState *g)
{
    return (uint16_t)((menu_map_row(g, 1) + UNDER_THE_VIEW) * MAP_TILE_PIXELS);
}

/* The byte the placing leaves in the mode's cells: the random offset across. */
static void leave_offset(GameState *g, uint8_t offset)
{
    enum { OFFSET = 0x08 };
    set_menu_object_scratch_bytes_byte(g, OFFSET, offset);
    set_menu_object_scratch_bytes_byte(g, OFFSET + 1, 0);
}

// $C2:754D
/* Put the object just under the view, somewhere across its middle half. */
enum { MIDDLE_HALF = 0x7F, QUARTER_IN = 0x40 };

static void place_under_the_view(GameState *g, MenuObject o)
{
    uint8_t offset = next_random(g) & MIDDLE_HALF;
    ORIGINAL_EFFECT(leave_offset(g, offset));
    set_menu_object_slots_x_pixel(g, o, (uint16_t)(menu_map_column(g, 1) * MAP_TILE_PIXELS + offset + QUARTER_IN));
    set_menu_object_slots_y_pixel(g, o, under_the_view(g));
}

// $C2:7575
/* Put the object just under the view, somewhere along the 256 pixels of the
 * map from $178 across. */
enum { STRETCH_START = 0x178 };

static void place_under_the_stretch(GameState *g, MenuObject o)
{
    uint8_t offset = next_random(g);
    ORIGINAL_EFFECT(leave_offset(g, offset));
    set_menu_object_slots_x_pixel(g, o, (uint16_t)(STRETCH_START + offset));
    set_menu_object_slots_y_pixel(g, o, under_the_view(g));
}

// $C2:7598
/* Give the object one of the scenes' sixteen velocities, at random. */
enum { VELOCITIES = 16 };

static void give_random_velocity(GameState *g, MenuObject o)
{
    uint8_t pick = next_random(g) & (VELOCITIES - 1);
    menu_object_set_velocity(g, o, (MenuPlace){ menu_scene_velocity_x(g, pick), menu_scene_velocity_y(g, pick) });
}

/* ---- the entries ----------------------------------------------------------------------------- */

/* They run on the mode's page, reaching the object and the scene's cells
 * through a data bank on low RAM; the object's routines are run by an object.
 * The loaders reach the decompressor's cells through the data bank alone, and
 * the story flags and the staged line values are reached by long address. */
enum { MODE_PAGE = 0x0000 };

bool menu_object_bank_reaches_low_ram(Snes *s) { return m_bank_reaches_low_ram(s); }
bool menu_object_routine_page(Snes *s) { return m_direct_page(s) == MODE_PAGE && m_bank_reaches_low_ram(s); }

bool menu_object_routine_runs(Snes *s)
{
    if (!menu_object_routine_page(s))
        return false;
    vars_peek(true);
    bool ok = menu_object_address_names_one(menu_object_current(state_of(s)));
    vars_peek(false);
    return ok;
}

/* -- the scene's own state -- */

// $C6:E237
/* The scene's mode, where the script tests it. The carry is clear. */
void menu_object_read_scene_mode(Snes *s)
{
    GameState *g = state_of(s);
    set_menu_object_scratch_scene_mode(g, scene_mode(g));
    m_set_flag(s, CPU_FLAG_C, false);
    m_rtl(s);
}

// $C6:E2AF
/* Clear the story flag the scenes clear when they end. */
void menu_object_clear_scene_flag(Snes *s)
{
    set_menu_scene_flag(state_of(s), 0);
    m_rtl(s);
}

/* -- the song -- */

// $C6:E63D
/* Keep the song that is playing through the map screen that follows. */
void menu_object_keep_song(Snes *s)
{
    set_menu_song_kept(state_of(s), 1);
    m_rtl(s);
}

// $C6:E644
/* And let the next screen start its own. */
void menu_object_let_song_go(Snes *s)
{
    set_menu_song_kept(state_of(s), 0);
    m_rtl(s);
}

/* -- motion -- */

// $C6:E583
/* Bob the second plane: up where the object's count is odd, down where it
 * is even. */
void menu_object_bob_second_plane(Snes *s)
{
    GameState *g = state_of(s);
    bob_second_plane(g, menu_object_slots_phase(g, menu_object_running(g)) & 1);
    m_rtl(s);
}

// $C6:E590
void menu_object_raise_second_plane(Snes *s)
{
    bob_second_plane(state_of(s), true);
    m_rtl(s);
}

/* The motion routines come back with a 16-bit accumulator, X the object and
 * the carry the speed down's wrapping round. */
static void leave_moved(Snes *s, bool wrapped)
{
    s->cpu.x = menu_object_current(state_of(s));
    m_set_flag(s, CPU_FLAG_C, wrapped);
    m_rep(s, CPU_FLAG_M);
    m_rtl(s);
}

// $C6:E77F
void menu_object_accelerate(Snes *s)
{
    GameState *g = state_of(s);
    leave_moved(s, accelerate(g, menu_object_running(g)));
}

// $C6:E2B5
void menu_object_drift(Snes *s)
{
    GameState *g = state_of(s);
    leave_moved(s, drift(g, menu_object_running(g)));
}

/* The jolt reads its sine table through the data bank, whose bank 0 reaches
 * the cartridge's table. */
bool menu_object_jolt_runs(Snes *s) { return s->cpu.db == 0x00 && menu_object_routine_runs(s); }

// $C6:E6D0
/* The carry: whether the object's place down wrapped round past the top of
 * its 16 bits, as the jolt's add down leaves it. */
void menu_object_jolt(Snes *s)
{
    GameState *g = state_of(s);
    MenuObject o = menu_object_running(g);
    uint16_t was = menu_object_slots_y_pixel(g, o);
    jolt(g, o);
    leave_moved(s, menu_object_slots_y_pixel(g, o) < was);
}

/* -- objects put on the map at random -- */

/* They run on the mode's page with the data bank on the I/O banks, and the
 * view is on the map: its top-left column well under $2000 (the original
 * carries the multiply's top bit into the sum). */
bool menu_object_can_be_placed(Snes *s)
{
    enum { COLUMNS_THE_SUM_TAKES = 0x2000 };
    if (m_direct_page(s) != MODE_PAGE || s->cpu.db != 0x00)
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = menu_object_address_names_one(menu_object_current(g)) && menu_map_column(g, 1) < COLUMNS_THE_SUM_TAKES;
    vars_peek(false);
    return ok;
}

/* They come back 16 bits wide with X on the object, and the carry the top
 * bit the row's multiply by the tile's pixels put out. */
static void leave_placed(Snes *s)
{
    GameState *g = state_of(s);
    s->cpu.x = menu_object_current(g);
    m_set_flag(s, CPU_FLAG_C, ((menu_map_row(g, 1) + UNDER_THE_VIEW) & 0x2000) != 0);
    m_rep(s, CPU_FLAG_M);
    m_rts(s);
}

// $C2:754D
void menu_object_place_under_the_view(Snes *s)
{
    GameState *g = state_of(s);
    place_under_the_view(g, menu_object_running(g));
    leave_placed(s);
}

// $C2:7575
void menu_object_place_under_the_stretch(Snes *s)
{
    GameState *g = state_of(s);
    place_under_the_stretch(g, menu_object_running(g));
    leave_placed(s);
}

// $C2:7598
/* It comes back 16 bits wide with X the velocity's place in its table and
 * the carry clear. */
void menu_object_give_random_velocity(Snes *s)
{
    enum { VELOCITY_BYTES = 8 };
    GameState *g = state_of(s);
    give_random_velocity(g, menu_object_running(g));
    vars_peek(true);
    uint8_t pick = menu_random_byte(g, (uint8_t)(menu_random_index(g) - 1)) & (VELOCITIES - 1);
    vars_peek(false);
    s->cpu.x = (uint16_t)(pick * VELOCITY_BYTES);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rep(s, CPU_FLAG_M);
    m_rts(s);
}

/* -- pictures -- */

/* Decompress one of the scenes' resources into its buffer in bank $7F with
 * the original's decompressor (a call-out), which takes the resource's
 * number times three in X and the pointer's bank over its high byte in A. */
static void load_scene_resource(Snes *s, uint32_t site, MenuSceneResource kind, uint8_t number, uint32_t buffer)
{
    GameState *g = state_of(s);
    uint32_t stream = menu_scene_stream(g, kind, number);
    set_decompress_dest(g, buffer);
    set_decompress_source(g, stream);
    s->cpu.a = (uint16_t)((stream & 0xFF00) | (uint8_t)(stream >> 16));
    s->cpu.x = (uint16_t)(number * MENU_SCENE_POINTER_BYTES);
    m_set_nz8(s, (uint8_t)(stream >> 16));
    m_jsl(s, site, 0xC30002);
    m_rtl(s);
}

/* Where the pictures go: the graphics to the Mode 7 plane's buffer, the
 * colours to the loaded palette's. The scenes of six of the eight world maps
 * share one picture; one scene has a second of its own. */
enum { MODE7_GRAPHICS_BUFFER = 0x7F9000, MODE7_PALETTE_BUFFER = 0x7F8C00 };
enum { SHARED_GRAPHICS = 2, SHARED_PALETTE = 1, SECOND_GRAPHICS = 13, SECOND_PALETTE = 2 };

// $C6:E028
/* The Mode 7 graphics most of the world maps' scenes share. */
void menu_object_load_mode7_graphics(Snes *s)
{
    load_scene_resource(s, 0xC6E048, MENU_SCENE_GRAPHICS, SHARED_GRAPHICS, MODE7_GRAPHICS_BUFFER);
}

// $C6:E04D
/* And their colours. */
void menu_object_load_mode7_palette(Snes *s)
{
    load_scene_resource(s, 0xC6E06D, MENU_SCENE_PALETTES, SHARED_PALETTE, MODE7_PALETTE_BUFFER);
}

// $C6:E64A
/* The Mode 7 graphics one world map's scene shows after the shared ones. */
void menu_object_load_second_mode7_graphics(Snes *s)
{
    load_scene_resource(s, 0xC6E66A, MENU_SCENE_GRAPHICS, SECOND_GRAPHICS, MODE7_GRAPHICS_BUFFER);
}

// $C6:E66F
/* And their colours. */
void menu_object_load_second_mode7_palette(Snes *s)
{
    load_scene_resource(s, 0xC6E68F, MENU_SCENE_PALETTES, SECOND_PALETTE, MODE7_PALETTE_BUFFER);
}
