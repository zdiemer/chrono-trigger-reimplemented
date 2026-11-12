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
 * screens in menu/epoch.c. */
#include "menu/effect_noise.h"
#include "menu/object_commands.h"

#include "charge.h"
#include "cpu_io.h"
#include "data/decompress.h"
#include "menu/display.h"
#include "menu/handlers.h"
#include "menu/menu_data.h"
#include "menu/object.h"
#include "overworld/overworld_data.h"
#include "wram.h"
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

/* The table's bytes can't hold the quarter turns' 128, which are put in by
 * hand. */
enum { QUARTER_TURN = MENU_OBJECT_TURN / 4, THREE_QUARTER_TURN = 3 * MENU_OBJECT_TURN / 4, SINE_PEAK = 128 };

// $C2:2277
int16_t menu_object_sine(const GameState *g, uint8_t angle)
{
    if (angle == QUARTER_TURN)
        return SINE_PEAK;
    if (angle == THREE_QUARTER_TURN)
        return -SINE_PEAK;
    return menu_sine_step(g, angle);
}

// $C2:2273
int16_t menu_object_cosine(const GameState *g, uint8_t angle) { return menu_object_sine(g, (uint8_t)(angle + QUARTER_TURN)); }

/* A jolt's size is up to 63, and it moves the object that many 256ths of the
 * sine along each axis, rounded toward zero. */
enum { JOLT_SIZES = 0x40, JOLT_UNIT = 256 };

static int16_t jolt_along(int16_t share, uint8_t size)
{
    int16_t moved = (int16_t)((share < 0 ? -share : share) * size / JOLT_UNIT);
    return share < 0 ? (int16_t)-moved : moved;
}

// $C6:E6D0
/* The object jumps a random distance in a random direction: one of the
 * menus' random bytes picks the direction, the next how far, up to 31 pixels
 * along an axis. Its place moves by whole pixels; the fractions stay. */
static void jolt(GameState *g, MenuObject o)
{
    uint8_t angle = next_random(g);
    uint8_t size = next_random(g) & (JOLT_SIZES - 1);
    set_menu_object_slots_x_pixel(g, o, (uint16_t)(menu_object_slots_x_pixel(g, o) + jolt_along(menu_object_cosine(g, angle), size)));
    set_menu_object_slots_y_pixel(g, o, (uint16_t)(menu_object_slots_y_pixel(g, o) + jolt_along(menu_object_sine(g, angle), size)));
    menu_note_effect_noise(g, angle);
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

// $C2:754D
/* Put the object just under the view, somewhere across its middle half. */
enum { MIDDLE_HALF = 0x7F, QUARTER_IN = 0x40 };

static void place_under_the_view(GameState *g, MenuObject o)
{
    uint8_t offset = next_random(g) & MIDDLE_HALF;
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

// $C2:79E0
/* Move the object a random step to the right, up to 48 pixels: a random
 * byte scaled to 48 256ths of itself, rounded to the nearest pixel. */
enum { STEP_SCALE = 0x30, HALF_A_PIXEL = 0x80 };

static void step_right_at_random(GameState *g, MenuObject o)
{
    uint8_t random = next_random(g);
    uint16_t scaled = (uint16_t)(random * STEP_SCALE);
    uint8_t step = (uint8_t)((scaled >> 8) + ((scaled & HALF_A_PIXEL) ? 1 : 0));
    set_menu_object_slots_x_pixel(g, o, (uint16_t)(menu_object_slots_x_pixel(g, o) + step));
}

/* A stretch of the map 256 pixels square, from its top-left corner (which
 * may lie above the map's top: the place then comes round from its foot). */
typedef struct {
    uint16_t left, top;
} Stretch;

static const Stretch EAST_STRETCH = { 0x220, 0x38 };
static const Stretch WEST_STRETCH = { 0xE0, (uint16_t)-0x48 };

// $C2:7A0C, $C2:7A31
/* Put the object somewhere in a stretch of the map, at random: one random
 * byte for how far across, the next for how far down. */
static void place_in_the_stretch(GameState *g, MenuObject o, Stretch at)
{
    set_menu_object_slots_x_pixel(g, o, (uint16_t)(at.left + next_random(g)));
    set_menu_object_slots_y_pixel(g, o, (uint16_t)(at.top + next_random(g)));
}

// $C6:E694
/* Put the object somewhere about the view at random: across, from a quarter
 * in from the second plane's left over the middle half; down, from line 96
 * of the map over the next 64 (the map's own lines, not the view's). The
 * original adds in the carry of the column's multiply by the tile's pixels,
 * which a column the map holds (view_is_on_the_map) never puts out. */
enum { DOWN_FROM = 0x60, DOWN_RANDOM = 0x3F };

static void place_about_the_view(GameState *g, MenuObject o)
{
    uint8_t across = next_random(g) & MIDDLE_HALF;
    uint8_t down = next_random(g) & DOWN_RANDOM;
    set_menu_object_slots_x_pixel(g, o, (uint16_t)(menu_map_column(g, 1) * MAP_TILE_PIXELS + across + QUARTER_IN));
    set_menu_object_slots_y_pixel(g, o, (uint16_t)(DOWN_FROM + down));
}

/* ---- the party ------------------------------------------------------------------------------- */

/* A scene that plays without the party empties its three places, keeping
 * who was in them to bring back after. */
enum { NO_MEMBER = 0x80, PARTY_SLOTS = 3 };

// $C2:7860
static void hide_the_party(GameState *g)
{
    for (unsigned slot = 0; slot < PARTY_SLOTS; slot++)
        set_party_members(g, slot, NO_MEMBER);
}

// $C2:786F
static void keep_the_party(GameState *g)
{
    for (unsigned slot = 0; slot < PARTY_SLOTS; slot++)
        set_menu_kept_party(g, slot, party_members(g, slot));
}

// $C2:7888
static void bring_the_party_back(GameState *g)
{
    for (unsigned slot = 0; slot < PARTY_SLOTS; slot++)
        set_party_members(g, slot, menu_kept_party(g, slot));
}

/* ---- the story -------------------------------------------------------------------------------- */

/* The two story flags the scenes look after themselves, as bits of event
 * memory bytes: one a scene sets as it has played, one a scene asks about
 * to choose its way. */
typedef struct {
    uint16_t byte;
    uint8_t bit;
} StoryFlag;

static const StoryFlag SCENE_HAS_PLAYED = { 0xAA, 0x01 };
static const StoryFlag SCENE_ASKS_ABOUT = { 0xF7, 0x02 };

static bool story_flag(const GameState *g, StoryFlag f) { return (event_memory_byte(g, f.byte) & f.bit) != 0; }

static void set_story_flag(GameState *g, StoryFlag f)
{
    set_event_memory_byte(g, f.byte, (uint8_t)(event_memory_byte(g, f.byte) | f.bit));
}

/* ---- what the scenes ask of themselves -------------------------------------------------------- */

// $C6:E237
/* The scene's mode, where the script tests it. */
static void read_the_scene_mode(GameState *g, MenuObject self)
{
    (void)self;
    set_menu_object_scratch_scene_mode(g, scene_mode(g));
}

// $C6:E2AF
/* Clear the story flag the scenes clear when they end. */
static void clear_the_scene_flag(GameState *g, MenuObject self) { (void)self, set_menu_scene_flag(g, 0); }

// $C6:E63D, $C6:E644
/* Keep the song that is playing through the map screen that follows, or let
 * the next screen start its own. */
static void keep_the_song(GameState *g, MenuObject self) { (void)self, set_menu_song_kept(g, 1); }
static void let_the_song_go(GameState *g, MenuObject self) { (void)self, set_menu_song_kept(g, 0); }

// $C6:E583
/* Bob the second plane: up where the object's count is odd, down where it
 * is even. */
static void bob(GameState *g, MenuObject self) { bob_second_plane(g, menu_object_slots_phase(g, self) & 1); }

// $C6:E590
static void raise(GameState *g, MenuObject self) { (void)self, bob_second_plane(g, true); }

// $C2:7B44
static void mark_played(GameState *g, MenuObject self) { (void)self, set_story_flag(g, SCENE_HAS_PLAYED); }

// $C2:7BC4
/* The answer goes to the mode's first cell, which the script tests next. */
static void ask(GameState *g, MenuObject self)
{
    (void)self;
    set_menu_object_scratch_answer(g, story_flag(g, SCENE_ASKS_ABOUT));
}

// $C2:7BD1
/* The scene names the song it leaves playing, handed over in the mode's
 * first cell, as the song the field goes on with (current_song). */
static void note_the_song(GameState *g, MenuObject self)
{
    (void)self;
    set_current_song(g, menu_object_scratch_answer(g));
}

// $C2:79C8
/* Accelerate the falling object by an eighth of a pixel per frame on each
 * script call. Its fixed-point velocity wraps, including its fraction. */
static bool accelerate_down(GameState *g, MenuObject self)
{
    uint32_t before = menu_object_slots_velocity_y(g, self), after = before + MENU_POSITION_UNITS / 8;
    set_menu_object_slots_velocity_y(g, self, after);
    return after < before;
}

// $C2:7B4F
/* Keep the scene's story event set after the scene gives control back. */
static void mark_scene_event(GameState *g, MenuObject self)
{
    static const StoryFlag event = { 0x19A, 0x08 };
    (void)self;
    set_story_flag(g, event);
}

/* The scene script hands back two independent results in its answer cell.
 * These story bytes persist after the object mode reuses that cell. */
typedef enum { SCENE_FIRST_RESULT = 0xCE, SCENE_SECOND_RESULT = 0xDA } SceneResult;

// $C2:7C48, $C2:7C4F
static void keep_scene_result(GameState *g, SceneResult result)
{
    set_event_memory_byte(g, result, menu_object_scratch_answer(g));
}

/* Pick a line near the scene's lower edge, rounded to the nearest line.
 * A zero height retains the original's byte underflow to line 255. */
static uint8_t scene_edge_line(uint8_t random, uint8_t height)
{
    uint16_t product = (uint16_t)random * height;
    uint8_t line = (uint8_t)((product >> 8) + ((product & 0x80) != 0));
    return line >= height ? (uint8_t)(line - 1) : line;
}

enum { SCENE_EDGE_TOP = 0x88, SCENE_EDGE_VIEW_INSET = 8 };

static uint16_t scene_edge_table_offset(const GameState *g, MenuObject self)
{
    uint8_t random = menu_random_byte(g, menu_random_index(g));
    uint8_t line = scene_edge_line(random, menu_object_slots_scene_edge_height(g, self));
    return (uint16_t)(2u * (SCENE_EDGE_TOP - SCENE_EDGE_VIEW_INSET + line));
}

/* The edge table is WRAM data above the object state, and the selected
 * pair of edges must remain within its bank. This also excludes aliasing
 * the result and scratch cells written before the original reads the pair. */
static bool scene_edge_is_readable(const GameState *g, MenuObject self)
{
    uint32_t table = menu_object_scratch_scene_window_edges(g);
    return (table >> 16 == 0x7E || table >> 16 == 0x7F) && (uint16_t)table >= 0x2000
           && (uint32_t)(uint16_t)table + scene_edge_table_offset(g, self) + 2u <= 0x10000u;
}

// $C6:E101
/* Scatter an object onto either edge of the scene's window. The random
 * pair chooses the line and side independently; its second byte comes
 * from the following table byte even at the random index's wrap. */
static void place_on_scene_edge(GameState *g, MenuObject self)
{
    uint8_t index = menu_random_index(g);
    uint8_t random = menu_random_byte(g, index), choose_side = menu_random_byte(g, (unsigned)index + 1);
    uint8_t height = menu_object_slots_scene_edge_height(g, self);
    uint16_t y = (uint16_t)(SCENE_EDGE_TOP + scene_edge_line(random, height));
    uint16_t offset = (uint16_t)(2u * (y - SCENE_EDGE_VIEW_INSET));
    uint32_t at = menu_object_scratch_scene_window_edges(g) + offset;
    set_menu_random_index(g, (uint8_t)(index + 2));
    set_menu_object_slots_y_pixel(g, self, y);
    uint16_t edges = wram_read16(g, at);
    set_menu_object_slots_x_pixel(g, self, (choose_side & 0x80) ? (uint8_t)(edges >> 8) : (uint8_t)edges);
    menu_note_effect_noise(g, offset);
}

/* ---- what the routines need ----------------------------------------------------------------- */

/* -- objects put on the map at random -- */

/* The view is on the map: its top-left column well under $2000 (the original
 * carries the multiply's top bit into the sum). */
static bool view_is_on_the_map(const GameState *g, MenuObject self)
{
    enum { COLUMNS_THE_SUM_TAKES = 0x2000 };
    (void)self;
    return menu_map_column(g, 1) < COLUMNS_THE_SUM_TAKES;
}

/* -- pictures -- */

/* Name one of the scenes' resources and its buffer in bank $7F to the
 * decompressor; the stream. */
static uint32_t name_scene_resource(GameState *g, MenuSceneResource kind, uint8_t number, uint32_t buffer)
{
    uint32_t stream = menu_scene_stream(g, kind, number);
    set_decompress_dest(g, buffer);
    set_decompress_source(g, stream);
    return stream;
}

/* Where the pictures go: the graphics to the Mode 7 plane's buffer, the
 * colours to the loaded palette's. The scenes of six of the eight world maps
 * share one picture; one scene has a second of its own. */
enum { MODE7_GRAPHICS_BUFFER = 0x7F9000, MODE7_PALETTE_BUFFER = 0x7F8C00 };
enum { SHARED_GRAPHICS = 2, SHARED_PALETTE = 1, SECOND_GRAPHICS = 13, SECOND_PALETTE = 2 };

/* The rising Black Omen scene uses the same streams as the world map's
 * setup, but loads them on demand before its sprite and palette uploads. */
enum { OMEN_GRAPHICS = 3, OMEN_SCENE_PALETTE = 18, OMEN_PALETTE_BUFFER = 0x7EC600 };

/* The scene selects a resource by its pointer-table byte offset. Validate
 * that selection and the decoded size before reusing the Mode 7 buffer. */
typedef enum { SCENE_MAP_GRAPHICS, SCENE_TOP_LAYER } SceneMapResource;
enum { SCENE_MAP_GRAPHICS_COUNT = 37, SCENE_TOP_LAYER_COUNT = 6, SCENE_MAP_BUFFER_BYTES = 0x7000 };

static uint32_t scene_map_stream(const GameState *g, SceneMapResource kind)
{
    uint8_t number = (uint8_t)(menu_object_scratch_scene_resource_offset(g) / MENU_SCENE_POINTER_BYTES);
    return overworld_stream(g, kind == SCENE_MAP_GRAPHICS ? OVERWORLD_MAP_GRAPHICS : OVERWORLD_TOP_LAYER_TILEMAPS, number);
}

static bool scene_map_resource_fits(const GameState *g, SceneMapResource kind)
{
    uint16_t offset = menu_object_scratch_scene_resource_offset(g);
    unsigned count = kind == SCENE_MAP_GRAPHICS ? SCENE_MAP_GRAPHICS_COUNT : SCENE_TOP_LAYER_COUNT;
    return offset % MENU_SCENE_POINTER_BYTES == 0 && offset / MENU_SCENE_POINTER_BYTES < count
           && unpack_stream_fits((GameState *)g, scene_map_stream(g, kind), SCENE_MAP_BUFFER_BYTES);
}

static bool scene_map_graphics_fit(const GameState *g, MenuObject self)
{
    (void)self;
    return scene_map_resource_fits(g, SCENE_MAP_GRAPHICS);
}

static bool scene_top_layer_fits(const GameState *g, MenuObject self)
{
    (void)self;
    return scene_map_resource_fits(g, SCENE_TOP_LAYER);
}

// $C2:7AFC, $C2:7B20
static void name_scene_map_resource(GameState *g, SceneMapResource kind)
{
    set_decompress_source(g, scene_map_stream(g, kind));
    set_decompress_dest(g, MODE7_GRAPHICS_BUFFER);
}

static bool load_scene_map_resource(GameState *g, Platform *pf, MenuObject self, bool far, SceneMapResource kind)
{
    (void)pf, (void)self, (void)far;
    name_scene_map_resource(g, kind);
    (void)unpack_stream(g);
    return true;
}

static bool load_scene_map_graphics_routine(GameState *g, Platform *pf, MenuObject self, bool far)
{
    return load_scene_map_resource(g, pf, self, far, SCENE_MAP_GRAPHICS);
}

static bool load_scene_top_layer_routine(GameState *g, Platform *pf, MenuObject self, bool far)
{
    return load_scene_map_resource(g, pf, self, far, SCENE_TOP_LAYER);
}

// $C2:7BD8
/* Repeat each of the scene's four eight-tile rows across two Mode 7 rows.
 * A solid tile after them supplies the plane outside that small picture. */
static void lay_out_scene_pattern(GameState *g, MenuObject self)
{
    enum { ROW_TILES = 8, ROW_BYTES = 0x20, ROWS = 4, EMPTY_BYTES = 0x100, SOLID_BYTES = 0x40 };
    (void)self;
    wram_fill8(g, MODE7_GRAPHICS_BUFFER, EMPTY_BYTES, 0);
    wram_fill8(g, MODE7_GRAPHICS_BUFFER + EMPTY_BYTES, SOLID_BYTES, 0xFF);
    charge_block_move(g, EMPTY_BYTES + SOLID_BYTES - 2);
    for (unsigned row = 0; row < ROWS; row++) {
        for (unsigned tile = 0; tile < ROW_TILES; tile++) {
            uint8_t v = menu_scene_pattern_tile(g, row * ROW_TILES + tile);
            uint32_t at = MODE7_GRAPHICS_BUFFER + row * 2u * ROW_BYTES + tile * 2u;
            wram_write8(g, at, v);
            wram_write8(g, at + ROW_BYTES, v);
        }
    }
}

/* The routines as a script calls them (menu/handlers.h). */
#define ROUTINE(name)                                                                \
    static bool name##_routine(GameState *g, Platform *pf, MenuObject self, bool far) \
    {                                                                                \
        (void)pf, (void)self, (void)far;                                             \
        name(g, self);                                                               \
        return true;                                                                 \
    }

static void fall_faster(GameState *g, MenuObject self) { (void)accelerate(g, self); }
static void drift_the_layer(GameState *g, MenuObject self) { (void)drift(g, self); }
static void place_in_the_east(GameState *g, MenuObject self) { place_in_the_stretch(g, self, EAST_STRETCH); }
static void place_in_the_west(GameState *g, MenuObject self) { place_in_the_stretch(g, self, WEST_STRETCH); }
static void hide(GameState *g, MenuObject self) { (void)self, hide_the_party(g); }
static void keep(GameState *g, MenuObject self) { (void)self, keep_the_party(g); }
static void bring_back(GameState *g, MenuObject self) { (void)self, bring_the_party_back(g); }

ROUTINE(lay_out_scene_pattern)
ROUTINE(place_on_scene_edge)
ROUTINE(read_the_scene_mode)
ROUTINE(clear_the_scene_flag)
ROUTINE(keep_the_song)
ROUTINE(let_the_song_go)
ROUTINE(bob)
ROUTINE(raise)
ROUTINE(fall_faster)
ROUTINE(drift_the_layer)
ROUTINE(jolt)
ROUTINE(place_under_the_view)
ROUTINE(place_under_the_stretch)
ROUTINE(give_random_velocity)
ROUTINE(step_right_at_random)
ROUTINE(place_in_the_east)
ROUTINE(place_in_the_west)
ROUTINE(hide)
ROUTINE(keep)
ROUTINE(bring_back)
ROUTINE(mark_played)
ROUTINE(ask)
ROUTINE(note_the_song)
ROUTINE(place_about_the_view)

static void accelerate_down_a_step(GameState *g, MenuObject self) { (void)accelerate_down(g, self); }
static void keep_first_scene_result(GameState *g, MenuObject self) { (void)self; keep_scene_result(g, SCENE_FIRST_RESULT); }
static void keep_second_scene_result(GameState *g, MenuObject self) { (void)self; keep_scene_result(g, SCENE_SECOND_RESULT); }
ROUTINE(accelerate_down_a_step)
ROUTINE(mark_scene_event)
ROUTINE(keep_first_scene_result)
ROUTINE(keep_second_scene_result)

/* The pictures, unpacked by the decompressor. */
#define PICTURE(name, kind, number, buffer)                                          \
    static bool name##_routine(GameState *g, Platform *pf, MenuObject self, bool far) \
    {                                                                                \
        (void)pf, (void)self, (void)far;                                             \
        (void)name_scene_resource(g, kind, number, buffer);                          \
        (void)unpack_stream(g);                                                      \
        return true;                                                                 \
    }
PICTURE(omen_graphics, MENU_SCENE_GRAPHICS, OMEN_GRAPHICS, MODE7_GRAPHICS_BUFFER)
PICTURE(omen_palette, MENU_SCENE_PALETTES, OMEN_SCENE_PALETTE, OMEN_PALETTE_BUFFER)
PICTURE(shared_graphics, MENU_SCENE_GRAPHICS, SHARED_GRAPHICS, MODE7_GRAPHICS_BUFFER)
PICTURE(shared_palette, MENU_SCENE_PALETTES, SHARED_PALETTE, MODE7_PALETTE_BUFFER)
PICTURE(second_graphics, MENU_SCENE_GRAPHICS, SECOND_GRAPHICS, MODE7_GRAPHICS_BUFFER)
PICTURE(second_palette, MENU_SCENE_PALETTES, SECOND_PALETTE, MODE7_PALETTE_BUFFER)

/* A scene that changes the whole picture starts from a clean VRAM. */
static bool clear_all_of_vram_routine(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)g, (void)self, (void)far;
    menu_clear_all_of_vram(pf);
    return true;
}

MENU_SCRIPT_ROUTINES(menu_scene_routines,
    { 0xC27B5A, "the Black Omen graphics", NULL, omen_graphics_routine },
    { 0xC27B9F, "the Black Omen scene colours", NULL, omen_palette_routine },
    { 0xC6E101, "an object placed on a window edge", scene_edge_is_readable, place_on_scene_edge_routine },
    { 0xC27AFC, "load scene map graphics", scene_map_graphics_fit, load_scene_map_graphics_routine },
    { 0xC27B20, "load scene top layer", scene_top_layer_fits, load_scene_top_layer_routine },
    { 0xC27BD8, "lay out scene pattern", NULL, lay_out_scene_pattern_routine },

    { 0xC279C8, "accelerate down", NULL, accelerate_down_a_step_routine },
    { 0xC27B4F, "mark scene event", NULL, mark_scene_event_routine },
    { 0xC27C48, "keep first scene result", NULL, keep_first_scene_result_routine },
    { 0xC27C4F, "keep second scene result", NULL, keep_second_scene_result_routine },

    { 0xC6E237, "the scene's mode read", NULL, read_the_scene_mode_routine },
    { 0xC6E2AF, "the scene's flag cleared", NULL, clear_the_scene_flag_routine },
    { 0xC6E63D, "the song kept", NULL, keep_the_song_routine },
    { 0xC6E644, "the song let go", NULL, let_the_song_go_routine },
    { 0xC6E583, "the second plane bobbed", NULL, bob_routine },
    { 0xC6E590, "the second plane raised", NULL, raise_routine },
    { 0xC6E77F, "an object falling faster", NULL, fall_faster_routine },
    { 0xC6E2B5, "the layer drifting with an object", NULL, drift_the_layer_routine },
    { 0xC6E6D0, "an object jolted", NULL, jolt_routine },
    { 0xC2754D, "an object put under the view", view_is_on_the_map, place_under_the_view_routine },
    { 0xC27575, "an object put under the stretch", view_is_on_the_map, place_under_the_stretch_routine },
    { 0xC27598, "an object given a random velocity", view_is_on_the_map, give_random_velocity_routine },
    { 0xC279E0, "an object stepped right at random", NULL, step_right_at_random_routine },
    { 0xC27A0C, "an object put in the east stretch", NULL, place_in_the_east_routine },
    { 0xC27A31, "an object put in the west stretch", NULL, place_in_the_west_routine },
    { 0xC27860, "the party hidden", NULL, hide_routine },
    { 0xC2786F, "the party kept", NULL, keep_routine },
    { 0xC27888, "the party brought back", NULL, bring_back_routine },
    { 0xC27B44, "the scene marked as played", NULL, mark_played_routine },
    { 0xC27BC4, "the story asked", NULL, ask_routine },
    { 0xC27BD1, "the song noted", NULL, note_the_song_routine },
    { 0xC6E694, "an object put about the view", view_is_on_the_map, place_about_the_view_routine },
    { 0xC6E028, "the scenes' Mode 7 graphics", NULL, shared_graphics_routine },
    { 0xC6E04D, "the scenes' Mode 7 colours", NULL, shared_palette_routine },
    { 0xC6E64A, "a scene's second Mode 7 graphics", NULL, second_graphics_routine },
    { 0xC6E66F, "a scene's second Mode 7 colours", NULL, second_palette_routine },
    { 0xC226A8, "all of VRAM cleared", NULL, clear_all_of_vram_routine },
);
