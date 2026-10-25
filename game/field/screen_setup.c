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
#include "field/scratch.h"
#include "platform/timing.h"
#include "port/machine.h"
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

/* A layer's drawing at the load (field/streaming.h), as C where its guard
 * takes the state and the original's otherwise, on the layers' page. */
enum { READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };
static const struct {
    uint32_t routine, site;
} drawing[FIELD_LAYERS] = { { 0xC075E9, 0xC00A96 }, { 0xC078EC, 0xC00AAB }, { 0xC07CB5, 0xC00AD8 } };

static bool draw_a_layer(Snes *s, unsigned layer)
{
    GameState *g = state_of(s);
    if (layer_drawing_fits(g, layer)) {
        TIMED_AS(g, drawing[layer].routine, draw_layer_at_the_maps_load(g, layer));
        return true;
    }
    return bridge_call_on_page(s, drawing[layer].site, drawing[layer].routine, READS_NOTHING, FIELD_LAYERS_PAGE);
}

/* The layer drawn and sent to its place: both screens where it is drawn
 * whole. */
static bool draw_and_send_a_layer(Snes *s, unsigned layer)
{
    GameState *g = state_of(s);
    if (!draw_a_layer(s, layer))
        return false;
    set_field_transfer_vram(g, field_layer_map_base(g, layer));
    bool whole = layer != FIELD_BG1 && (field_layer_drawn_whole(g) & FIELD_LAYER_BIT(layer));
    TIMED_AS(g, whole ? FIELD_SEND_STAGED_SCREENS_ENTRY : FIELD_SEND_STAGED_SCREEN_ENTRY,
             field_send_staged_layer(g, m_platform(s), whole));
    return true;
}

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

static bool draw_the_map(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    TIMED_AS(g, CHESTS_MARK_THE_MAP_ENTRY, chests_mark_the_map(g));
    TIMED_AS(g, SET_THE_VIDEO_MODE, field_set_the_video_mode(pf));
    TIMED_AS(g, SHOW_THE_SCREENS, field_show_the_maps_screens(g, pf));
    place_the_screen(g);
    place_the_layers_maps(g);
    if (!draw_and_send_a_layer(s, FIELD_BG1) || !draw_and_send_a_layer(s, FIELD_BG2))
        return false;
    if (draws_bg3(g) && !draw_and_send_a_layer(s, FIELD_BG3))
        return false;
    if (layers_repeat_within_the_maps(g))
        TIMED_AS(g, FIELD_BRING_IN_THE_RIGHT_COLUMNS_ENTRY, stream_the_right_columns(g));
    else if (!bridge_call(s, 0xC00AF3, FIELD_BRING_IN_THE_RIGHT_COLUMNS_ENTRY, READS_NOTHING, on_the_field_page(s, false)))
        return false;
    if (field_bg3_map_size(g) != 0)
        set_field_layer_map_size(g, FIELD_BG3, field_bg3_map_size(g));
    return true;
}

/* ---- The objects ------------------------------------------------------------------------------ */

/* A part of the object's frame drawn as C where its guard takes the state,
 * and the original's otherwise: true while more of it is to draw. False in
 * `came_back` where the call didn't come back as the field runs. */
enum { THE_BUILDER_READS_THE_OBJECT = DC_A | DC_X | DC_Y | DC_N | DC_Z };

static bool draw_a_frame_part(Snes *s, bool *came_back)
{
    *came_back = field_run(s, 0xC0B109, FIELD_DRAW_NEXT_FRAME_ENTRY, THE_BUILDER_READS_THE_OBJECT, field_draw_next_frame,
                           on_the_field_page(s, true));
    return *came_back && m_flag(s, CPU_FLAG_C);
}

/* An object in view at the start is drawn at once: on the screen, in its
 * row, its whole frame drawn and sent, its frame areas let go. False where
 * the port stopped. */
enum { JOINS_ITS_ROW_READS = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C, UPLOAD_THE_QUEUE = 0xC0B1B2 };

static bool draw_at_once(Snes *s, FieldObject obj)
{
    GameState *g = state_of(s);
    TIMED_AS(g, FIELD_UPDATE_SCREEN_POSITION_ENTRY, object_update_screen_position(g, obj));
    if (!field_rows_step_as_c(s, obj, true) &&
        !bridge_call(s, 0xC0B104, FIELD_JOIN_ROW_ENTRY, JOINS_ITS_ROW_READS, on_the_field_page(s, true)))
        return false;
    set_field_vram_queue_end(g, 0);
    bool came_back = true;
    while (draw_a_frame_part(s, &came_back))
        ;
    if (!came_back)
        return false;
    TIMED_AS(g, UPLOAD_THE_QUEUE, field_upload_queued_transfers(g, m_platform(s)));
    TIMED_AS(g, FIELD_RELEASE_FRAME_AREAS_ENTRY, sprites_release_frame_areas(g, obj));
    return true;
}

// $C0:B0E6 (with $C0:B127 for each)
/* Every object that shows and is near enough the screen comes into view and
 * is drawn at once (but those that only matter on the screen itself, which
 * the screen marks as it goes); then the sprite table is built from them,
 * which returns for this routine. On the field's page with 8-bit index
 * registers for the objects. */
enum { SPRITE_TABLE = 0xC0B271, THE_TABLE_READS_THE_OVERFLOW = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C };

/* For the original's code and shadow mode: the loop's end, the object count
 * doubled, in event_object_end's low byte. */
static void leave_loop_end(GameState *g, unsigned objects)
{
    set_event_object_end(g, (uint16_t)((event_object_end(g) & 0xFF00) | (uint8_t)(objects * 2)));
}

/* For the original's code and shadow mode: the overflow the loop leaves for
 * the sprite table, which carries it on where it makes no sum of its own:
 * the last object looked at for the view (one that shows) had the start's
 * window worked out, whose last sum is the screen's bottom widened. */
enum { START_WINDOW_BELOW = 5 };

/* The objects that may come into view: those that show on the map, but those
 * that only matter on the screen itself, which the screen marks. */
static bool may_come_into_view(const GameState *g, FieldObject obj)
{
    ObjectRole role = object_role(g, obj);
    return !role.disabled && role.kind != ROLE_WINDOW_ONLY;
}

static bool looked_at_for_the_view(const GameState *g, FieldObject obj)
{
    return may_come_into_view(g, obj) && (int8_t)field_object_sprite_flag(g, obj) > 0;
}

static void leave_the_overflow(Snes *s, unsigned objects)
{
    const GameState *g = state_of(s);
    for (unsigned obj = objects; obj-- > 0;)
        if (looked_at_for_the_view(g, (FieldObject)obj)) {
            if (!object_in_view(g, (FieldObject)obj))
                m_set_flag(s, CPU_FLAG_V, field_add_overflows8(field_screen_bottom(g), START_WINDOW_BELOW));
            return;
        }
}

void field_draw_the_objects_in_view(Snes *s)
{
    GameState *g = state_of(s);
    unsigned objects = event_object_count(g);
    ORIGINAL_EFFECT(leave_loop_end(g, objects));
    m_sep(s, CPU_FLAG_X);
    for (FieldObject obj = 0; obj < objects; obj++) {
        object_make_current(g, obj);
        if (!may_come_into_view(g, obj) || !object_in_view_at_the_start(g, obj))
            continue;
        if (!draw_at_once(s, obj))
            return;
    }
    m_rep(s, CPU_FLAG_X);
    ORIGINAL_EFFECT(leave_the_overflow(s, objects));
    if (!field_go_on_with(s, SPRITE_TABLE, field_build_sprite_table))
        m_stop_at_own(s, SPRITE_TABLE, THE_TABLE_READS_THE_OVERFLOW);
}

/* Its guard: on the field's page with the data bank $00, a count of
 * objects the tables hold (none would have the original's loop run round
 * the bytes 128 times), and each object that may come into view one whose
 * palette and block the port takes as the view does (field/object_view.c). */
bool field_objects_in_view_can_be_drawn(Snes *s)
{
    if (!field_on_page_bank_zero(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    unsigned objects = event_object_count(g);
    bool ok = objects != 0 && objects <= FIELD_OBJECTS;
    for (FieldObject obj = 0; ok && obj < objects; obj++)
        if (may_come_into_view(g, obj))
            ok = field_view_change_in_the_tables(g, obj);
    vars_peek(false);
    return ok;
}

/* ---- The screen back on ----------------------------------------------------------------------- */

/* The line effects readied for the first frame: their values, their tables
 * built (on 8-bit index registers) and set up, each the port it is, from
 * the original's calls in its routine ($C0:0B28). */
enum { LINE_EFFECT_VALUES = 0xFDC124, LINE_EFFECTS = 0xFDC2C1, LINE_EFFECTS_SET_UP = 0xFDC1EE };

static bool ready_the_line_effects(Snes *s)
{
    if (!field_run_long(s, 0xC00B28, LINE_EFFECT_VALUES, READS_NOTHING, field_init_line_effect_values,
                        on_the_field_page(s, false)))
        return false;
    m_sep(s, CPU_FLAG_X);
    if (!field_run_long(s, 0xC00B2E, LINE_EFFECTS, READS_NOTHING, field_build_line_effect_tables,
                        on_the_field_page(s, true)))
        return false;
    m_rep(s, CPU_FLAG_X);
    return field_run_long(s, 0xC00B34, LINE_EFFECTS_SET_UP, READS_NOTHING, field_setup_line_effects,
                          on_the_field_page(s, false));
}

/* The wait for the vblank with the handlers off, after which they come on:
 * the original's routine's own (from $C0:0B38), which the timing service
 * calls on the SNES backend, the screen's setup having called its routine
 * at $C0:28A1 (field/frame.c has its two ports). */
enum { SCREEN_ON = 0xC00B38, FIELD_PICTURE_LAST_LINE = 211 };

/* ---- The setup -------------------------------------------------------------------------------- */

/* The sprite table's first four entries hidden, which the field's own
 * sprites take. */
enum { FIRST_SPRITES_HIDDEN = 4 };

/* The original's routines it calls on the machine where a port won't run. */
enum { FRAME_WAIT = 0xC0EC60, DRAW_THE_OBJECTS = 0xC0B0E6 };
enum { THE_OBJECTS_READ_Y_AND_THE_OVERFLOW = DC_A | DC_X | DC_N | DC_Z | DC_C };

static BridgeMachine page(const Snes *s) { return on_the_field_page(s, false); }

/* For the original's code and shadow mode: what the object tables' fills
 * before the objects are drawn leave in Y, and the overflow the map's
 * drawing leaves (its edge runs' last sum), which the drawing of the
 * objects reads where it calls the original's routines. */
static void leave_y_and_the_overflow(Snes *s)
{
    enum { THE_LAST_FILL_LEAVES = 0x00FF };
    s->cpu.y = THE_LAST_FILL_LEAVES;
    m_set_flag(s, CPU_FLAG_V, field_layer_edges_overflow(state_of(s)));
}

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

// $C0:286C
/* The screen set up from nothing (above). Returns for the routine, with the
 * location's dark setting in A (Z set where it is 0); false where the port
 * stopped. */
static bool set_the_screen_up(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    if (field_remembered_switches_on_the_map(g))
        TIMED_AS(g, FIELD_PRESS_REMEMBERED_SWITCHES_ENTRY, field_press_the_remembered_switches(g));
    else if (!bridge_call(s, 0xC0286C, FIELD_PRESS_REMEMBERED_SWITCHES_ENTRY, READS_NOTHING, page(s)))
        return false;
    if (!field_run_table_step(s, 0xC0286F, draw_the_map))
        return false;
    field_objects_let_go(g, false);   /* a map's screen starts with no object holding anything of it */
    ORIGINAL_EFFECT(leave_y_and_the_overflow(s));
    if (!field_run(s, 0xC02881, DRAW_THE_OBJECTS, THE_OBJECTS_READ_Y_AND_THE_OVERFLOW, field_draw_the_objects_in_view,
                   page(s)))
        return false;
    put_up_the_window(g, pf);
    if (!field_run_table_step(s, 0xC028A1, ready_the_line_effects))
        return false;
    tm_wait_frame_then_enable(pf, PF_CALL_DC(0xC028A1, SCREEN_ON, READS_NOTHING), TM_INT_VBLANK | TM_INT_AUTO_JOYPAD,
                              FIELD_PICTURE_LAST_LINE);
    if (!bridge_back(s, 0xC028A4, page(s)) || !field_wait_frame(s, 0xC028A4, FRAME_WAIT, READS_NOTHING, page(s)))
        return false;
    uint8_t dark = field_setting_1e(g);
    m_set_a8(s, dark);
    m_set_nz8(s, dark);
    return true;
}

void field_set_the_screen_up(Snes *s)
{
    if (set_the_screen_up(s))
        m_rts(s);
}

/* ---- The screen set up, as C ------------------------------------------------------------------ */

/* Where a step is one only the machine runs yet (its state is none the
 * port takes), the program goes no further (core/program.h). */
static bool machine_only(GameState *g, uint32_t routine, const char *what)
{
    program_needs_the_machine(g, routine, what);
    return false;
}

static bool draw_and_send_a_layer_as_c(GameState *g, Platform *pf, unsigned layer)
{
    if (!layer_drawing_fits(g, layer))
        return machine_only(g, drawing[layer].routine, "a layer the port doesn't draw");
    TIMED_AS(g, drawing[layer].routine, draw_layer_at_the_maps_load(g, layer));
    set_field_transfer_vram(g, field_layer_map_base(g, layer));
    bool whole = layer != FIELD_BG1 && (field_layer_drawn_whole(g) & FIELD_LAYER_BIT(layer));
    TIMED_AS(g, whole ? FIELD_SEND_STAGED_SCREENS_ENTRY : FIELD_SEND_STAGED_SCREEN_ENTRY,
             field_send_staged_layer(g, pf, whole));
    return true;
}

// $C0:0A50
static bool draw_the_map_as_c(GameState *g, Platform *pf)
{
    TIMED_AS(g, CHESTS_MARK_THE_MAP_ENTRY, chests_mark_the_map(g));
    TIMED_AS(g, SET_THE_VIDEO_MODE, field_set_the_video_mode(pf));
    TIMED_AS(g, SHOW_THE_SCREENS, field_show_the_maps_screens(g, pf));
    place_the_screen(g);
    place_the_layers_maps(g);
    if (!draw_and_send_a_layer_as_c(g, pf, FIELD_BG1) || !draw_and_send_a_layer_as_c(g, pf, FIELD_BG2))
        return false;
    if (draws_bg3(g) && !draw_and_send_a_layer_as_c(g, pf, FIELD_BG3))
        return false;
    if (!layers_repeat_within_the_maps(g))
        return machine_only(g, FIELD_BRING_IN_THE_RIGHT_COLUMNS_ENTRY, "columns the port doesn't bring in");
    TIMED_AS(g, FIELD_BRING_IN_THE_RIGHT_COLUMNS_ENTRY, stream_the_right_columns(g));
    if (field_bg3_map_size(g) != 0)
        set_field_layer_map_size(g, FIELD_BG3, field_bg3_map_size(g));
    return true;
}

// $C0:B0E6 (with $C0:B127 for each)
static bool draw_at_once_as_c(GameState *g, Platform *pf, FieldObject obj)
{
    TIMED_AS(g, FIELD_UPDATE_SCREEN_POSITION_ENTRY, object_update_screen_position(g, obj));
    if (!field_rows_are_exact(g, obj))
        return machine_only(g, FIELD_JOIN_ROW_ENTRY, "a row the port doesn't join");
    TIMED_AS(g, FIELD_JOIN_ROW_ENTRY, object_join_row(g, obj));
    set_field_vram_queue_end(g, 0);
    bool more = true;
    while (more)
        TIMED_AS(g, FIELD_DRAW_NEXT_FRAME_ENTRY, more = field_frame_part_drawn(g, obj));
    TIMED_AS(g, UPLOAD_THE_QUEUE, field_upload_queued_transfers(g, pf));
    TIMED_AS(g, FIELD_RELEASE_FRAME_AREAS_ENTRY, sprites_release_frame_areas(g, obj));
    return true;
}

static bool draw_the_objects_in_view_as_c(GameState *g, Platform *pf)
{
    unsigned objects = event_object_count(g);
    for (FieldObject obj = 0; obj < objects; obj++) {
        object_make_current(g, obj);
        if (may_come_into_view(g, obj) && object_in_view_at_the_start(g, obj) && !draw_at_once_as_c(g, pf, obj))
            return false;
    }
    bool built = false;
    TIMED_AS(g, SPRITE_TABLE, built = field_sprite_table_built(g));
    return built || machine_only(g, SPRITE_TABLE, "a sprite table the port doesn't build");
}

bool field_screen_set_up(GameState *g, Platform *pf)
{
    if (!field_remembered_switches_on_the_map(g))
        return machine_only(g, FIELD_PRESS_REMEMBERED_SWITCHES_ENTRY, "switches the port doesn't press again");
    TIMED_AS(g, FIELD_PRESS_REMEMBERED_SWITCHES_ENTRY, field_press_the_remembered_switches(g));
    if (!draw_the_map_as_c(g, pf))
        return false;
    field_objects_let_go(g, false);
    bool done = false;
    TIMED_AS(g, DRAW_THE_OBJECTS, done = draw_the_objects_in_view_as_c(g, pf));
    if (!done)
        return false;
    put_up_the_window(g, pf);
    field_line_effects_ready(g, pf);
    tm_wait_frame_then_enable(pf, PF_CALL_DC(0xC028A1, SCREEN_ON, READS_NOTHING), TM_INT_VBLANK | TM_INT_AUTO_JOYPAD,
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

/* Its steps, run as C or on the machine (below): the screen set up, the
 * leader's walk under the control the fade took away, the frame's work and
 * the wait for the next frame; each false where it went no further. */
typedef struct {
    bool (*set_up)(void *ctx, FadeInKind kind);
    bool (*walk)(void *ctx, FadeInKind kind, uint8_t control);
    bool (*work)(void *ctx, FadeInKind kind);
    bool (*wait)(void *ctx, FadeInKind kind);
} FadeInSteps;

// $C0:2848, $C0:2824
/* The screen set up, then, unless the location starts dark, brightened a
 * step a frame; the play time's clock runs again. False where a step went
 * no further; `shown` the brightness it ends at, or the dark setting. */
static bool fade_in(GameState *g, FadeInKind kind, const FadeInSteps *steps, void *ctx, uint8_t *shown)
{
    if (!steps->set_up(ctx, kind))
        return false;
    *shown = field_setting_1e(g);
    if (*shown == 0) {
        do {
            set_field_brightness(g, (uint8_t)(field_brightness(g) + 1));
            uint8_t control = field_player_control(g);
            hand_the_party_to_the_player(g, false);
            if (!steps->walk(ctx, kind, control))
                return false;
            set_field_player_control(g, control);
            if (!steps->work(ctx, kind) || !steps->wait(ctx, kind))
                return false;
            *shown = field_brightness(g);
        } while (still_brightening(*shown));
    }
    set_field_setting_1e(g, 0);
    start_play_clock(g);
    return true;
}

/* ---- As C ----------------------------------------------------------------------------------- */

typedef struct {
    GameState *game;
    Platform *pf;
} FadeInAsC;

static bool set_up_as_c(void *ctx, FadeInKind kind)
{
    (void)kind;
    FadeInAsC *c = ctx;
    bool done = false;
    TIMED_AS(c->game, FIELD_SET_THE_SCREEN_UP_ENTRY, done = field_screen_set_up(c->game, c->pf));
    return done;
}

static bool walk_as_c(void *ctx, FadeInKind kind, uint8_t control)
{
    (void)kind, (void)control;
    FadeInAsC *c = ctx;
    bool done = false;
    TIMED_AS(c->game, WALK, done = field_the_leader_walks(c->game));
    return done;
}

static bool work_as_c(void *ctx, FadeInKind kind)
{
    FadeInAsC *c = ctx;
    bool done = true;
    if (kind == WITH_THE_FRAMES_WORK)
        TIMED_AS(c->game, FIELD_FRAME_WORK_ENTRY, done = field_frame_work_done(c->game, c->pf));
    else
        TIMED_AS(c->game, FIELD_REFRESH_EFFECTS_ENTRY, field_effects_refreshed(c->game));
    return done;
}

static bool wait_as_c(void *ctx, FadeInKind kind)
{
    (void)kind;
    FadeInAsC *c = ctx;
    TIMED_AS(c->game, FIELD_FRAME_WAIT, field_finish_the_frame(c->game, c->pf));
    return true;
}

static const FadeInSteps as_c = { set_up_as_c, walk_as_c, work_as_c, wait_as_c };

bool field_fade_in_as_c(GameState *g, Platform *pf)
{
    FadeInAsC c = { g, pf };
    uint8_t shown;
    return fade_in(g, WITH_THE_FRAMES_WORK, &as_c, &c, &shown);
}

bool field_fade_in_after_a_menu_as_c(GameState *g, Platform *pf)
{
    FadeInAsC c = { g, pf };
    uint8_t shown;
    return fade_in(g, WITH_THE_EFFECTS_ONLY, &as_c, &c, &shown);
}

/* ---- On the machine ------------------------------------------------------------------------- */

/* Each step from the original's call of it, by kind of fade in. The walk
 * runs under the control the fade keeps on the stack over it. */
static const struct {
    uint32_t setup, walk, work, wait;
} fade_in_sites[] = {
    [WITH_THE_FRAMES_WORK] = { 0xC02848, 0xC02854, 0xC0285A, 0xC0285D },
    [WITH_THE_EFFECTS_ONLY] = { 0xC02824, 0xC02830, 0xC02836, 0xC02839 },
};
enum { READS_THE_OVERFLOW = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C };

static bool set_up_on_the_machine(void *ctx, FadeInKind kind)
{
    Snes *s = ctx;
    return field_run(s, fade_in_sites[kind].setup, FIELD_SET_THE_SCREEN_UP_ENTRY, READS_NOTHING,
                     field_set_the_screen_up, page(s));
}

static bool walk_on_the_machine(void *ctx, FadeInKind kind, uint8_t control)
{
    Snes *s = ctx;
    return field_run_holding(s, fade_in_sites[kind].walk, WALK, READS_NOTHING, field_walk_the_leader, page(s), control);
}

static bool work_on_the_machine(void *ctx, FadeInKind kind)
{
    Snes *s = ctx;
    if (kind == WITH_THE_FRAMES_WORK)
        return field_run(s, fade_in_sites[kind].work, FIELD_FRAME_WORK_ENTRY, READS_THE_OVERFLOW, field_frame_work,
                         page(s));
    return field_run(s, fade_in_sites[kind].work, FIELD_REFRESH_EFFECTS_ENTRY, READS_NOTHING, field_refresh_effects,
                     page(s));
}

static bool wait_on_the_machine(void *ctx, FadeInKind kind)
{
    Snes *s = ctx;
    return field_wait_frame(s, fade_in_sites[kind].wait, FRAME_WAIT, READS_NOTHING, page(s));
}

/* A is left the last brightness, or the dark setting, which the callers'
 * callers read the high byte of only. */
static void fade_in_on_the_machine(Snes *s, FadeInKind kind)
{
    static const FadeInSteps on_the_machine = { set_up_on_the_machine, walk_on_the_machine, work_on_the_machine,
                                                wait_on_the_machine };
    uint8_t shown;
    if (!fade_in(state_of(s), kind, &on_the_machine, s, &shown))
        return;
    m_set_a8(s, shown);
    m_set_nz8(s, shown);
    m_rts(s);
}

void field_fade_in(Snes *s) { fade_in_on_the_machine(s, WITH_THE_FRAMES_WORK); }

void field_fade_in_after_a_menu(Snes *s) { fade_in_on_the_machine(s, WITH_THE_EFFECTS_ONLY); }
