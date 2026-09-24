/* Menu mode: the routines of bank C6 the sprite objects' scripts call.
 *
 * A script can call a routine by its long address (menu/object_script.c's
 * call command); the ones here sit in bank C6 beside the scenes' resource
 * tables. They do small things to the scene around the object: note the
 * scene's mode for the script to test, set and clear story flags, bob the
 * second plane, make an object fall faster, let the layer over the map drift
 * with an object, load a scene's graphics and palettes. The Epoch's map screen
 * also calls its area check directly. The per-line effects the scripts start
 * from bank C6 are menu/line_effects.c's.
 *
 * After them the scripts read only X and the carry (and the map screen the
 * area check's outcome), so each entry says what it leaves in those and
 * declares the rest dead. */
#include "menu/object_commands.h"

#include "menu/menu_data.h"
#include "menu/object.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- the scene around the object ------------------------------------------------------------- */

// $C6:E237
/* The scene's mode: bits 2-5 of the story flags the map screens copy in. */
static uint8_t scene_mode(const GameState *g) { return (uint8_t)((menu_story_flags_scene_mode_bits(g) & 0x3C) >> 2); }

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

// $C6:E2B5
/* The layer over the map drifts with the object: the object's velocity moves
 * the layer's scroll, the fraction of a pixel kept in the object's place, and
 * the scroll the menu code has left pending is added and used up; then the
 * drift speeds up. True where the speed down wrapped round. */
enum { DRIFT_SPEED_UP_X = 0x0DDB, DRIFT_SPEED_UP_Y = 0x0800 };

static uint32_t layer_place(uint16_t scroll, uint32_t place) { return (uint32_t)scroll << 16 | (uint16_t)place; }

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

/* ---- the Epoch's areas ----------------------------------------------------------------------- */

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

/* The areas of the world maps (locations $1F0-$1F7) the Epoch may be flown
 * in; a map with none has nowhere. */
enum { WORLD_MAPS = 0x01F0, WORLD_MAP_COUNT = 8, TILE_SHIFT = 3 };
static const struct {
    bool has_area;
    Area area;
} world_map_areas[WORLD_MAP_COUNT] = {
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
 * them in its cells. */
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
    unsigned map = (uint16_t)(location_id(g) - WORLD_MAPS);
    if (!world_map_areas[map].has_area) {
        ORIGINAL_EFFECT(leave_area_cells(g, tile_x, tile_y, NULL, 0));
        return false;
    }
    const Area *a = &world_map_areas[map].area;
    uint8_t sides = sides_outside((uint8_t)tile_x, (uint8_t)tile_y, *a);
    ORIGINAL_EFFECT(leave_area_cells(g, tile_x, tile_y, a, sides));
    return sides == 0;
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

/* And the area check on one of the world maps, which the original picks the
 * area of with a jump through a table as long as the maps. */
bool menu_epoch_on_world_map(Snes *s)
{
    if (!menu_object_routine_runs(s))
        return false;
    vars_peek(true);
    bool ok = (uint16_t)(location_id(state_of(s)) - WORLD_MAPS) < WORLD_MAP_COUNT;
    vars_peek(false);
    return ok;
}

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

/* Decompress one of the scene's resources into its buffer in bank $7F with
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

/* Where the resources go: the graphics to the Mode 7 plane's buffer, the
 * palettes to the loaded palette's. */
enum { SCENE_GRAPHICS_BUFFER = 0x7F9000, SCENE_PALETTE_BUFFER = 0x7F8C00 };

// $C6:E028
void menu_object_load_scene_graphics_2(Snes *s)
{
    load_scene_resource(s, 0xC6E048, MENU_SCENE_GRAPHICS, 2, SCENE_GRAPHICS_BUFFER);
}
// $C6:E04D
void menu_object_load_scene_palette_1(Snes *s)
{
    load_scene_resource(s, 0xC6E06D, MENU_SCENE_PALETTES, 1, SCENE_PALETTE_BUFFER);
}
// $C6:E64A
void menu_object_load_scene_graphics_13(Snes *s)
{
    load_scene_resource(s, 0xC6E66A, MENU_SCENE_GRAPHICS, 13, SCENE_GRAPHICS_BUFFER);
}
// $C6:E66F
void menu_object_load_scene_palette_2(Snes *s)
{
    load_scene_resource(s, 0xC6E68F, MENU_SCENE_PALETTES, 2, SCENE_PALETTE_BUFFER);
}
