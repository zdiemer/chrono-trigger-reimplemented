/* Field mode: a map's objects started, as a location loads.
 *
 * Once the location's map and its event script are unpacked
 * (field/location.c), its objects are set up before the screen is
 * (field/screen_setup.c):
 *
 * - whatever the last map's objects held is let go: every block of sprite
 *   tiles, palette slot and frame area free, the graphics sets the last map's
 *   scripts unpacked forgotten, every row empty and every object out of view;
 * - each of the map's objects starts as its script first finds it: at the
 *   script's first function (its opening), at the lowest level, off the map,
 *   with the field's usual speed and blocking; the slots past the map's
 *   objects disabled; the script variables, the objects' velocities and the
 *   scripts' saved places cleared;
 * - each object runs its opening, and the party starts on the map
 *   (event/bridge.c, $C0:58DE);
 * - the map's own script starts ($C0:595C, still the original's);
 * - and what the party wears shows (menu/party.h).
 *
 * The objects' scripts then take their turns a frame at a time
 * (field/script_turns.c). */
#include "field/objects_start.h"
#include "field/cast.h"
#include "field/display.h"
#include "field/field.h"
#include "field/object.h"
#include "field/object_update.h"
#include "field/palettes.h"
#include "field/scratch.h"
#include "field/sprites.h"

#include "charge.h"
#include "cpu_io.h"
#include "event/calls.h"
#include "event/event.h"
#include "field/bridge.h"
#include "menu/party.h"
#include "menu/party_leaves.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"

// $C0:56B3-$C0:56C2 (with $C0:5929, the graphics sets forgotten)
void field_objects_let_go(GameState *g, bool forget_the_graphics)
{
    sprite_blocks_all_free(g);
    palette_slots_all_free(g);
    TIMED_AS(g, FIELD_FREE_EVERY_SPRITE_AREA_ENTRY, sprites_free_every_frame_area(g));
    if (forget_the_graphics)
        cast_forget_the_unpacked_graphics(g);
    field_empty_the_rows(g);
    objects_all_out_of_view(g);
}

/* ---- Each object as its script first finds it ------------------------------------------- */

/* The level an object's script starts at, the least urgent, and the steps
 * its walks take; its speed; and its colours, which no effect runs on. */
enum { LOWEST_LEVEL = EVENT_LEVELS - 1, FIRST_MOVE_LENGTH = 4, USUAL_SPEED = 0x10, NO_COLOUR_EFFECT = 0xFFFF };

/* Off the map, where nothing draws it, until its opening places it. */
enum { OFF_THE_MAP_TILE = 0xFF };

/* Layered as the map's tiles say where it stands. */
enum { FOLLOWS_THE_MAP = 0x80 };

/* The opening is its script's first function. */
enum { OPENING = 0 };

static void object_start_afresh(GameState *g, FieldObject obj)
{
    set_event_object_script_position(g, obj, object_function_start(g, obj, OPENING));
    set_event_object_call_state_level(g, obj, LOWEST_LEVEL);
    set_event_object_call_state_refusing(g, obj, 0);
    set_field_object_step_length(g, obj, FIRST_MOVE_LENGTH);
    set_field_object_turn_countdown(g, obj, 0);
    set_field_object_script_link(g, obj, field_link_byte(field_no_link()));
    set_field_object_animation_link(g, obj, field_link_byte(field_no_link()));
    object_become(g, obj, ROLE_WINDOW_ONLY);
    object_set_character(g, obj, 0);
    set_field_object_x_fine(g, obj, 0);
    set_field_object_x_tile(g, obj, OFF_THE_MAP_TILE);
    set_field_object_y_fine(g, obj, 0);
    set_field_object_y_tile(g, obj, OFF_THE_MAP_TILE);
    set_field_object_speed(g, obj, USUAL_SPEED);
    set_field_object_steps_left(g, obj, 0);
    set_field_object_counted_move(g, obj, 0);
    set_field_object_sprite_flag(g, obj, SPRITE_SHOWN);
    set_field_object_animation_mode(g, obj, ANIMATE_WITH_MOVEMENT);
    set_field_object_one_off_animation(g, obj, 0);
    set_field_object_blocking(g, obj, BLOCKED_BY_THE_MAP | BLOCKED_BY_OBJECTS);
    set_field_object_arrival_flags(g, obj, 0);
    set_field_object_priority_mode(g, obj, FOLLOWS_THE_MAP);
    set_field_object_palette(g, obj, 0);
    set_field_object_tile_crossing(g, obj, STAY_IN_TILE);
    set_field_object_one_off_half_passes(g, obj, 0);
    set_field_object_colour_effect(g, obj, NO_COLOUR_EFFECT);
}

/* The slots past the map's objects are disabled (but the last, which the
 * original leaves as it was). */
enum { SLOTS_DISABLED_TO = FIELD_OBJECTS - 1 };

static void slot_unused(GameState *g, FieldObject obj)
{
    object_set_aside(g, obj);
    object_set_character(g, obj, 0);
}

/* Work RAM cleared for the new map: the script variables; both velocities'
 * tables; and the scripts' saved places, waits and results, the party's
 * trail and the line effects' tables, as far as the dialog window's edges. */
static const struct {
    uint32_t from;
    uint16_t bytes;
} clears[] = {
    { RAM_EVENT_VARIABLES, 0x0200 },
    { RAM_FIELD_OBJECT_VELOCITY_X, 0x0100 },
    { RAM_EVENT_SAVED_POSITIONS, 0x15C0 },
};
enum { CLEARS = sizeof clears / sizeof clears[0] };

static void clear(GameState *g, Platform *pf, unsigned which)
{
    TIMED_AS(g, FIELD_FILL_MEMORY_ENTRY, field_clear_memory(g, pf, clears[which].from, clears[which].bytes));
}

/* For the original's code and shadow mode: the work RAM port where the
 * original's writes of the defaults leave it, after the last object's colour
 * effect; and the end of the objects' function tables, a cell of the field's
 * page. */
enum { UPPER_BANK = 1, FUNCTION_TABLE_BYTES = 0x20 };

static void leave_the_port(GameState *g, unsigned objects)
{
    hw_port_seek(g, (uint16_t)(RAM_ABS(RAM_FIELD_OBJECT_COLOUR_EFFECT) + 2 * objects), UPPER_BANK);
    original_cell16(g, 0xD9, (uint16_t)(objects * FUNCTION_TABLE_BYTES));
}

/* Where the next graphics set a script unpacks goes, the middle of bank $7F,
 * and the people and creatures loaded: none yet. */
enum { UNPACKED_GRAPHICS_START = 0x8000 };

// $C0:5709
void field_objects_start_afresh(GameState *g, Platform *pf)
{
    unsigned objects = event_object_count(g);
    clear(g, pf, 0);
    for (FieldObject obj = 0; obj < objects; obj++)
        object_start_afresh(g, obj);
    for (FieldObject obj = (FieldObject)objects; obj < SLOTS_DISABLED_TO; obj++)
        slot_unused(g, obj);
    ORIGINAL_EFFECT(leave_the_port(g, objects));
    clear(g, pf, 1);
    clear(g, pf, 2);
    set_field_script_queue_head(g, field_link_byte(field_no_link()));
    set_field_script_queue_tail(g, field_link_byte(field_no_link()));
    set_field_animation_queue_head(g, field_link_byte(field_no_link()));
    set_field_animation_queue_tail(g, field_link_byte(field_no_link()));
    set_field_unpacked_graphics_end(g, UNPACKED_GRAPHICS_START);
    set_field_npc_graphics_count(g, 0);
}

/* ---- The entries ------------------------------------------------------------------------- */

/* The openings, the original's at $C0:58DE where the port doesn't run them.
 * The party's start after them reads the overflow, which the defaults leave
 * clear, and the original's openings Y, which they leave the last default
 * byte written ($FF). */
enum { OPENINGS = 0xC058DE, THE_OPENINGS_READ_Y_AND_THE_OVERFLOW = DC_A | DC_X | DC_N | DC_Z | DC_C };
enum { THE_DEFAULTS_LEAVE_Y = 0x00FF };

static void go_on_to_the_openings(Snes *s)
{
    m_set_flag(s, CPU_FLAG_V, false);
    s->cpu.y = THE_DEFAULTS_LEAVE_Y;
    if (!field_go_on_with(s, OPENINGS, event_run_init_scripts))
        m_stop_at_own(s, OPENINGS, THE_OPENINGS_READ_Y_AND_THE_OVERFLOW);
}


/* On the field's page; returns from the openings. */
void field_start_the_objects(Snes *s)
{
    field_objects_start_afresh(state_of(s), m_platform(s));
    go_on_to_the_openings(s);
}

/* The original's routines it calls: the map's own script, and the menus'
 * services, whose time the party's looks take. */
enum { MAP_SCRIPT_STARTS = 0xC0595C, MENUS_SERVICES = 0xC28004 };
enum { READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };
enum { THE_MAP_SCRIPT_READS_THE_OVERFLOW = DC_A | DC_N | DC_Z | DC_C };

static BridgeMachine page(const Snes *s) { return on_the_field_page(s, false); }

/* What the party wears shows, as the menus' service shows it. */
static void show_what_the_party_wears(Snes *s)
{
    GameState *g = state_of(s);
    PartyServiceCall call = { 0 };
    ORIGINAL_EFFECT(call = party_service_called(g, (uint8_t)s->cpu.y, (uint8_t)s->cpu.x));
    NATIVE_TIME_AS(s, MENUS_SERVICES, party_wear_it_shows(g));
    ORIGINAL_EFFECT(party_service_answered(g, SERVICE_SHOW_WHAT_IT_WEARS, &call));
    (void)call;
}

/* The scanlines the objects' script turns and their animation stop at each
 * frame, as a map starts (a script can move the animation's). */
enum { SCRIPTS_STOP_AT_LINE = 0x48, ANIMATION_STOPS_AT_LINE = 0x78 };

// $C0:56A6
/* The objects set up: let go, started, their openings run; then the map's
 * own script started, and the party's looks shown. */
void field_set_up_the_objects(Snes *s)
{
    GameState *g = state_of(s);
    set_field_script_line_limit(g, SCRIPTS_STOP_AT_LINE);
    set_field_animation_line_limit(g, ANIMATION_STOPS_AT_LINE);
    field_forget_the_queued_transfers(g);
    field_objects_let_go(g, true);
    if (!field_run(s, 0xC056C5, FIELD_START_THE_OBJECTS_ENTRY, READS_NOTHING, field_start_the_objects,
                   page(s)) ||
        !bridge_call(s, 0xC056C8, MAP_SCRIPT_STARTS, THE_MAP_SCRIPT_READS_THE_OVERFLOW, page(s)))
        return;
    show_what_the_party_wears(s);
    m_rts(s);
}

/* The guards: on the field's page with the data bank $00, a count of objects
 * the tables hold (none would have the original's loops run round the bytes),
 * and a party the menus' service can show. */
static bool objects_fit(Snes *s)
{
    if (!field_on_page_bank_zero(s))
        return false;
    vars_peek(true);
    unsigned objects = event_object_count(state_of(s));
    vars_peek(false);
    return objects != 0 && objects <= FIELD_OBJECTS;
}

bool field_objects_can_start(Snes *s) { return objects_fit(s); }

bool field_objects_can_be_set_up(Snes *s)
{
    if (!objects_fit(s))
        return false;
    vars_peek(true);
    bool shows = party_record_shows_characters(state_of(s));
    vars_peek(false);
    return shows;
}
