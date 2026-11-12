/* Field mode: a map's screen set up, and faded in.
 *
 * Once a location is loaded (field/location.c) -- or the field comes back
 * from a menu, a battle or a script's scene -- its screen is put together
 * from nothing, with the display dark and the vblank handler off:
 *
 * - the map: the switches the party pressed shown pressed again, the tiles
 *   the leader can walk onto and the chests marked, the video mode and the
 *   map's screens set, the screen placed around the party, and each layer
 *   drawn whole into the staging buffers and sent to VRAM, with the columns
 *   right of the view that the first frame's scrolling needs;
 * - the objects: every block of sprite tiles, palette slot and frame area
 *   free, every row empty and every object out of view; then each object
 *   near enough the screen comes into view and is drawn at once, its frame
 *   sent, and the sprite table built from them;
 * - the dialog window's looks, the window design's tiles and colours, the
 *   choice pointer's tiles and the field's own sprites (field/dialog_window.c);
 * - the line effects readied, and at the next vblank the field's handlers
 *   turned back on.
 *
 * Then, unless the location keeps its screen dark (field_setting_1e), the
 * picture brightens a step a frame, the leader walking on the spot and the
 * frame's work going on (or, coming back from a menu, the effects only),
 * and the play clock runs again. */
#include "field/screen_setup.h"
#include "field/cast.h"
#include "field/dialog_window.h"
#include "field/display.h"
#include "field/field.h"
#include "field/frames.h"
#include "field/layers.h"
#include "field/line_effects.h"
#include "field/main_loop.h"
#include "field/object.h"
#include "field/object_update.h"
#include "field/objects_start.h"
#include "field/palettes.h"
#include "field/sprites.h"
#include "core/program.h"
#include "field/streaming.h"
#include "field/switches.h"
#include "field/walk.h"

#include "charge.h"
#include "event/chests.h"
#include "field/bridge.h"
#include "platform/timing.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The screen placed around the party ------------------------------------------------------ */

/* The screen is 32 tiles of 8 pixels across and 28 down; it starts with the
 * party's tile (16 pixels) the eighth from its left and from its top, held
 * within the map's edges: at the left or top edge where the party is nearer
 * it, and at the right or bottom edge where the party is nearer that (on
 * the bottom, nearer than the sixth tile up). The original works in bytes,
 * and the sums wrap. */
enum { SCREEN_TILES_ACROSS = 32, SCREEN_TILES_DOWN = 28, PARTY_TILE_FROM_THE_EDGE = 8, PARTY_TILE_FROM_THE_BOTTOM = 6 };
enum { TILES_A_MAP_TILE = 2 };

typedef struct {
    uint8_t first, last;   /* the screen's edges, in 8-pixel tiles */
} ScreenSpan;

static ScreenSpan span_around(uint8_t party, uint8_t near_edge, uint8_t far_edge, uint8_t from_far, uint8_t extent)
{
    if (party <= (uint8_t)(near_edge + PARTY_TILE_FROM_THE_EDGE)) {
        uint8_t first = (uint8_t)(near_edge * TILES_A_MAP_TILE);
        return (ScreenSpan){ first, (uint8_t)(first + extent) };
    }
    uint8_t first = (uint8_t)((party - PARTY_TILE_FROM_THE_EDGE) * TILES_A_MAP_TILE);
    if ((uint8_t)(far_edge - from_far) < party) {
        uint8_t last = (uint8_t)((far_edge + 1) * TILES_A_MAP_TILE);
        return (ScreenSpan){ (uint8_t)(last - extent), last };
    }
    return (ScreenSpan){ first, (uint8_t)(first + extent) };
}

/* BG2 at half the pace keeps edges of its own: half the screen's, its left
 * on an even tile. */
enum { EVEN_TILE = 0xFE };

// $C0:74A6 (with $C0:7506 across and $C0:7553 down)
static void place_the_screen(GameState *g)
{
    ScreenSpan across = span_around(location_x(g), field_map_edges_left(g), field_map_edges_right(g),
                                    PARTY_TILE_FROM_THE_EDGE, SCREEN_TILES_ACROSS);
    set_field_screen_left(g, across.first);
    set_field_screen_right(g, across.last);
    ScreenSpan down = span_around(location_y(g), field_map_edges_top(g), field_map_edges_bottom(g),
                                  PARTY_TILE_FROM_THE_BOTTOM, SCREEN_TILES_DOWN);
    set_field_screen_top(g, down.first);
    set_field_screen_bottom(g, down.last);
    if (field_bg2_half_scroll(g) & BG2_HALF_PACE) {
        uint8_t left = (uint8_t)((field_screen_left(g) >> 1) & EVEN_TILE), top = (uint8_t)(field_screen_top(g) >> 1);
        set_field_bg2_screen_left(g, left);
        set_field_bg2_screen_right(g, (uint8_t)(left + SCREEN_TILES_ACROSS));
        set_field_bg2_screen_top(g, top);
        set_field_bg2_screen_bottom(g, (uint8_t)(top + SCREEN_TILES_DOWN));
    }
    for (unsigned layer = 0; layer < FIELD_LAYERS; layer++) {
        set_field_layer_position_x(g, layer, 0);
        set_field_layer_position_y(g, layer, 0);
    }
    TIMED_AS(g, FIELD_COMPUTE_LAYER_EDGES_ENTRY, field_find_layer_edges(g));
}

/* ---- The map drawn and sent ----------------------------------------------------------------- */

/* The layers' tile maps' places in VRAM, two screens wide each, and the
 * place a script's map transition stages into (field/nmi.c). */
static const uint16_t layer_map_places[FIELD_LAYERS] = { 0x6000, 0x6800, 0x7000 };
enum { TRANSITION_MAP_PLACE = 0x7800 };

static void place_the_layers_maps(GameState *g)
{
    for (unsigned layer = 0; layer < FIELD_LAYERS; layer++)
        set_field_layer_map_base(g, layer, layer_map_places[layer]);
    set_field_transition_map_base(g, TRANSITION_MAP_PLACE);
    for (unsigned layer = 0; layer < FIELD_LAYERS; layer++)
        set_field_layer_map_size(g, layer, FIELD_MAP_TWO_SCREENS_WIDE);
}

/* A layer's drawing at the load (field/streaming.h), on the layers' page,
 * where the port draws the layer. */

static const struct {
    uint32_t routine, site;
} drawing[FIELD_LAYERS] = { { 0xC075E9, 0xC00A96 }, { 0xC078EC, 0xC00AAB }, { 0xC07CB5, 0xC00AD8 } };

static bool draws_bg3(const GameState *g)
{
    return (field_map_flags(g) & FIELD_MAP_HAS_BG3) && field_map_has_no_bg3_set(g) == 0;
}

// $C0:0A50
/* The map as the screen starts: its chests and walkable tiles, the video
 * mode and its screens, the screen around the party, the layers drawn and
 * sent, the columns right of the view, and BG3's tile map's size where the
 * location gives it one. A step the original runs from the screen's setup:
 * false where a call it made didn't come back as the field runs. */
enum { SET_THE_VIDEO_MODE = 0xFDC0D7, SHOW_THE_SCREENS = 0xC0A509 };

/* ---- The objects ------------------------------------------------------------------------------ */

/* An object in view at the start is drawn at once: on the screen, in its
 * row, its whole frame drawn and sent, its frame areas let go. False where
 * the port stopped. */
enum { UPLOAD_THE_QUEUE = 0xC0B1B2 };

// $C0:B0E6 (with $C0:B127 for each)
/* Every object that shows and is near enough the screen comes into view and
 * is drawn at once (but those that only matter on the screen itself, which
 * the screen marks as it goes); then the sprite table is built from them,
 * which returns for this routine. */
enum { SPRITE_TABLE = 0xC0B271 };

/* The objects that may come into view: those that show on the map, but those
 * that only matter on the screen itself, which the screen marks. */
static bool may_come_into_view(const GameState *g, FieldObject obj)
{
    ObjectRole role = object_role(g, obj);
    return !role.disabled && role.kind != ROLE_WINDOW_ONLY;
}

/* ---- The screen back on ----------------------------------------------------------------------- */

/* The line effects readied for the first frame: their values, their tables
 * built and set up, each the port it is, from the original's calls in its
 * routine ($C0:0B28). */
enum { LINE_EFFECT_VALUES = 0xFDC124, LINE_EFFECTS = 0xFDC2C1, LINE_EFFECTS_SET_UP = 0xFDC1EE };

/* The wait for the vblank with the handlers off, after which they come on:
 * the original's routine's own (from $C0:0B38), the screen's setup having
 * called its routine at $C0:28A1. */
enum { SCREEN_ON = 0xC00B38, FIELD_PICTURE_LAST_LINE = 211 };

/* ---- The setup -------------------------------------------------------------------------------- */

/* The sprite table's first four entries hidden, which the field's own
 * sprites take. */
enum { FIRST_SPRITES_HIDDEN = 4 };

/* The objects drawn at the start, by their routine's timing key. */
enum { DRAW_THE_OBJECTS = 0xC0B0E6 };

/* The dialog window's looks, the window design's tiles and colours, the
 * choice pointer's tiles and the field's own sprites. */
static void put_up_the_window(GameState *g, Platform *pf)
{
    TIMED_AS(g, FIELD_SEND_WINDOW_DESIGN_ENTRY, field_send_the_window_design(g, pf));
    field_colour_the_window(g);
    TIMED_AS(g, FIELD_BUILD_WINDOW_FRAME_ENTRY, field_build_the_window_frame(g));
    TIMED_AS(g, FIELD_SEND_WINDOW_FRAME_ENTRY, field_send_the_window_frame(g, pf));
    TIMED_AS(g, FIELD_BUILD_TEXT_AREA_ENTRY, field_build_the_text_area(g));
    TIMED_AS(g, FIELD_SEND_TEXT_AREA_ENTRY, field_send_the_text_area(g, pf));
    field_send_the_fields_own_sprites(g, pf);
    sprites_hide_the_first(g, FIRST_SPRITES_HIDDEN);
    TIMED_AS(g, FIELD_SEND_CHOICE_POINTER_ENTRY, field_send_the_choice_pointer(g, pf));
}

/* ---- The screen set up ------------------------------------------------------------------------ */

/* Where a step's state is none the port takes, the program goes no further
 * (core/program.h). */
static bool refused(GameState *g, uint32_t routine, const char *what)
{
    program_breaks_a_contract(g, routine, what);
    return false;
}

static bool draw_and_send_a_layer(GameState *g, Platform *pf, unsigned layer)
{
    if (!layer_drawing_fits(g, layer))
        return refused(g, drawing[layer].routine, "a layer the port doesn't draw");
    TIMED_AS(g, drawing[layer].routine, draw_layer_at_the_maps_load(g, layer));
    set_field_transfer_vram(g, field_layer_map_base(g, layer));
    bool whole = layer != FIELD_BG1 && (field_layer_drawn_whole(g) & FIELD_LAYER_BIT(layer));
    TIMED_AS(g, whole ? FIELD_SEND_STAGED_SCREENS_ENTRY : FIELD_SEND_STAGED_SCREEN_ENTRY,
             field_send_staged_layer(g, pf, whole));
    return true;
}

// $C0:0A50
static bool draw_the_map(GameState *g, Platform *pf)
{
    TIMED_AS(g, CHESTS_MARK_THE_MAP_ENTRY, chests_mark_the_map(g));
    TIMED_AS(g, SET_THE_VIDEO_MODE, field_set_the_video_mode(pf));
    TIMED_AS(g, SHOW_THE_SCREENS, field_show_the_maps_screens(g, pf));
    place_the_screen(g);
    place_the_layers_maps(g);
    if (!draw_and_send_a_layer(g, pf, FIELD_BG1) || !draw_and_send_a_layer(g, pf, FIELD_BG2))
        return false;
    if (draws_bg3(g) && !draw_and_send_a_layer(g, pf, FIELD_BG3))
        return false;
    if (!layers_repeat_within_the_maps(g))
        return refused(g, FIELD_BRING_IN_THE_RIGHT_COLUMNS_ENTRY, "columns the port doesn't bring in");
    TIMED_AS(g, FIELD_BRING_IN_THE_RIGHT_COLUMNS_ENTRY, stream_the_right_columns(g));
    if (field_bg3_map_size(g) != 0)
        set_field_layer_map_size(g, FIELD_BG3, field_bg3_map_size(g));
    return true;
}

// $C0:B0E6 (with $C0:B127 for each)
static bool draw_at_once(GameState *g, Platform *pf, FieldObject obj)
{
    TIMED_AS(g, FIELD_UPDATE_SCREEN_POSITION_ENTRY, object_update_screen_position(g, obj));
    if (!field_rows_are_exact(g, obj))
        return refused(g, FIELD_JOIN_ROW_ENTRY, "a row the port doesn't join");
    TIMED_AS(g, FIELD_JOIN_ROW_ENTRY, object_join_row(g, obj));
    set_field_vram_queue_end(g, 0);
    bool more = true;
    while (more)
        TIMED_AS(g, FIELD_DRAW_NEXT_FRAME_ENTRY, more = field_frame_part_drawn(g, obj));
    TIMED_AS(g, UPLOAD_THE_QUEUE, field_upload_queued_transfers(g, pf));
    TIMED_AS(g, FIELD_RELEASE_FRAME_AREAS_ENTRY, sprites_release_frame_areas(g, obj));
    return true;
}

static bool draw_the_objects_in_view(GameState *g, Platform *pf)
{
    unsigned objects = event_object_count(g);
    for (FieldObject obj = 0; obj < objects; obj++) {
        object_make_current(g, obj);
        if (may_come_into_view(g, obj) && object_in_view_at_the_start(g, obj) && !draw_at_once(g, pf, obj))
            return false;
    }
    bool built = false;
    TIMED_AS(g, SPRITE_TABLE, built = field_sprite_table_built(g));
    return built || refused(g, SPRITE_TABLE, "a sprite table the port doesn't build");
}

bool field_screen_set_up(GameState *g, Platform *pf)
{
    if (!field_remembered_switches_on_the_map(g))
        return refused(g, FIELD_PRESS_REMEMBERED_SWITCHES_ENTRY, "switches the port doesn't press again");
    TIMED_AS(g, FIELD_PRESS_REMEMBERED_SWITCHES_ENTRY, field_press_the_remembered_switches(g));
    if (!draw_the_map(g, pf))
        return false;
    field_objects_let_go(g, false);
    bool done = false;
    TIMED_AS(g, DRAW_THE_OBJECTS, done = draw_the_objects_in_view(g, pf));
    if (!done)
        return false;
    put_up_the_window(g, pf);
    field_line_effects_ready(g, pf);
    tm_wait_frame_then_enable(pf, PF_CALL(0xC028A1, SCREEN_ON), TM_INT_VBLANK | TM_INT_AUTO_JOYPAD,
                              FIELD_PICTURE_LAST_LINE);
    field_finish_the_frame(g, pf);
    return true;
}

/* ---- The fade in -------------------------------------------------------------------------------- */

/* The picture brightens a step a frame to full brightness, as the original
 * compares it (a signed difference: a brightness past $8F counts as dark). */
enum { FULL_BRIGHTNESS = 0x0F, WALK = 0xC0881E };
static bool still_brightening(uint8_t brightness) { return (int8_t)(brightness - FULL_BRIGHTNESS) < 0; }

/* Each frame of it the leader walks with the player's control taken away
 * (the screen following it), and the frame's work goes on: all of it, or,
 * coming back from a menu, the effects only. */
typedef enum { WITH_THE_FRAMES_WORK, WITH_THE_EFFECTS_ONLY } FadeInKind;

// $C0:2848, $C0:2824
/* The screen set up, then, unless the location starts dark, brightened a
 * step a frame; the play time's clock runs again. False where a step went
 * no further. */
static bool fade_in(GameState *g, Platform *pf, FadeInKind kind)
{
    bool done = false;
    TIMED_AS(g, FIELD_SET_THE_SCREEN_UP_ENTRY, done = field_screen_set_up(g, pf));
    if (!done)
        return false;
    if (field_setting_1e(g) == 0) {
        do {
            set_field_brightness(g, (uint8_t)(field_brightness(g) + 1));
            uint8_t control = field_player_control(g);
            hand_the_party_to_the_player(g, false);
            TIMED_AS(g, WALK, done = field_the_leader_walks(g));
            if (!done)
                return false;
            set_field_player_control(g, control);
            if (kind == WITH_THE_FRAMES_WORK)
                TIMED_AS(g, FIELD_FRAME_WORK_ENTRY, done = field_frame_work_done(g, pf));
            else
                TIMED_AS(g, FIELD_REFRESH_EFFECTS_ENTRY, done = field_effects_refreshed(g));
            if (!done)
                return false;
            TIMED_AS(g, FIELD_FRAME_WAIT, field_finish_the_frame(g, pf));
        } while (still_brightening(field_brightness(g)));
    }
    set_field_setting_1e(g, 0);
    start_play_clock(g);
    return true;
}

bool field_fade_in(GameState *g, Platform *pf) { return fade_in(g, pf, WITH_THE_FRAMES_WORK); }

bool field_fade_in_after_a_menu(GameState *g, Platform *pf) { return fade_in(g, pf, WITH_THE_EFFECTS_ONLY); }
