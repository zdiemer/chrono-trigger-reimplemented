/* The party on the world map: who is in it, where each stands, the vehicles
 * it boards, and a walker's look -- the way it faces and the animation it
 * shows -- shared by the leader (walk.c) and the followers (followers.c);
 * and the leader's trail, which the followers walk after it. */
#include "overworld/overworld.h"

#include "charge.h"
#include "menu/sprites.h"
#include "original_effects.h"
#include "overworld/overworld_data.h"
#include "vars.h"

/* ---- How the party goes about the map --------------------------------------------------- */

Travel overworld_party_travel(const GameState *g) { return (Travel)overworld_travel(g); }

Vehicle overworld_vehicle_boarded(const GameState *g) { return (Vehicle)overworld_boarding(g); }

/* The Epoch's, unless it is the dactyl's. */
WorldSpot overworld_vehicle_spot(const GameState *g, Vehicle v)
{
    if (v == VEHICLE_DACTYL)
        return (WorldSpot){ overworld_dactyl_x(g), overworld_dactyl_y(g) };
    return (WorldSpot){ overworld_epoch_x(g), overworld_epoch_y(g) };
}

/* A vehicle's count of the party aboard is the low bits of its state, which
 * a climb counts up or down as a byte; with no vehicle there is nothing to
 * climb onto. */
static void count_aboard(GameState *g, Vehicle v, int8_t by)
{
    if (v == VEHICLE_EPOCH)
        set_overworld_epoch_state(g, (uint8_t)(overworld_epoch_state(g) + by));
    else if (v == VEHICLE_DACTYL)
        set_overworld_dactyl_state(g, (uint8_t)(overworld_dactyl_state(g) + by));
}

void overworld_climb_aboard(GameState *g, Vehicle v) { count_aboard(g, v, 1); }
void overworld_climb_down(GameState *g, Vehicle v) { count_aboard(g, v, -1); }

/* ---- The party -------------------------------------------------------------------------- */

enum { EMPTY_PLACE = 0x80 };

bool overworld_place_taken(const GameState *g, PartyPlace place) { return !(party_members(g, place) & EMPTY_PLACE); }
uint8_t overworld_character_in(const GameState *g, PartyPlace place) { return party_members(g, place); }

WorldSpot overworld_party_spot(const GameState *g, PartyPlace place)
{
    return (WorldSpot){ overworld_party_places_x(g, place), overworld_party_places_y(g, place) };
}

void overworld_set_party_spot(GameState *g, PartyPlace place, WorldSpot at)
{
    set_overworld_party_places_x(g, place, at.x);
    set_overworld_party_places_y(g, place, at.y);
}

bool overworld_event_running(const GameState *g) { return overworld_event(g) != 0; }

OverworldMode overworld_mode(const GameState *g) { return (OverworldMode)menu_mode(g); }
void overworld_set_mode(GameState *g, OverworldMode mode) { set_menu_mode(g, mode); }

bool overworld_on_the_map(const GameState *g)
{
    return overworld_mode(g) == OVERWORLD_STARTING || overworld_mode(g) == OVERWORLD_ON_THE_MAP;
}

/* ---- A walker --------------------------------------------------------------------------- */

void overworld_set_step(GameState *g, MenuObject walker, OverworldStep step) { set_menu_object_slots_step(g, walker, step); }
OverworldStep overworld_step(const GameState *g, MenuObject walker) { return menu_object_slots_step(g, walker); }

/* The vehicles' controller and the places' watcher note, as they run, what
 * the travel and the event were; a walker that sees either changed since
 * starts its step over. */
bool overworld_walkers_start_over(const GameState *g)
{
    return overworld_travel(g) != overworld_travel_before(g) || overworld_event(g) != overworld_event_before(g);
}

/* The original picks a walker's step on foot from a table with a row for
 * whether an event runs, which it works out in the mode's first cell. */
void overworld_leave_step_table_row(GameState *g)
{
    enum { ROW_BYTES = 8 };
    set_overworld_scratch_walker_table_row(g, (uint8_t)(overworld_event(g) * ROW_BYTES));
}

void overworld_start_over_if_asked(GameState *g, MenuObject walker)
{
    if (overworld_walkers_start_over(g))
        overworld_set_step(g, walker, 0);
}

/* The party is drawn on the map at the second priority, the third on the one
 * map that asks it ($1F5), each member in a palette and from tiles of its
 * own: the leader's the fourth palette and the first tiles, each member
 * after it the next palette and tiles $20 on. */
enum { LOCATION_DRAWN_HIGHER = 0x01F5, PARTY_FIRST_PALETTE = 4, TILES_A_MEMBER = 0x20 };
enum { PRIORITY_2 = 0x20, PRIORITY_3 = 0x30 };

void overworld_dress(GameState *g, MenuObject walker, PartyPlace place)
{
    uint8_t priority = location_id(g) == LOCATION_DRAWN_HIGHER ? PRIORITY_3 : PRIORITY_2;
    uint8_t palette = (uint8_t)(((PARTY_FIRST_PALETTE + place) << 1) & MENU_LOOK_PALETTE);
    set_menu_object_slots_look(g, walker, (uint8_t)(MENU_LOOK_ON_THE_MAP | priority | palette));
    set_menu_object_slots_tiles(g, walker, (uint16_t)(place * TILES_A_MEMBER));
}

void overworld_animate(GameState *g) { TIMED_AS(g, 0xC20E1D, menu_object_animate(g)); }

/* A walker keeps the way it faces in the low bits of its facing byte, and
 * whether it shows its walking animation in the top one. */
enum { FACING_BITS = 0x03, SHOWS_WALKING = 0x80 };

Facing overworld_facing(const GameState *g, MenuObject walker)
{
    return (Facing)(menu_object_slots_facing(g, walker) & FACING_BITS);
}

bool overworld_shows_walking(const GameState *g, MenuObject walker)
{
    return (menu_object_slots_facing(g, walker) & SHOWS_WALKING) != 0;
}

void overworld_face(GameState *g, MenuObject walker, Facing facing) { set_menu_object_slots_facing(g, walker, facing); }

void overworld_stop_showing_walking(GameState *g, MenuObject walker)
{
    set_menu_object_slots_facing(g, walker, (uint8_t)(menu_object_slots_facing(g, walker) & ~SHOWS_WALKING));
}

/* The original hands the animation routines the character in its first cell
 * and leaves the animation it picks in the next. */
static void leave_animation_picked(GameState *g, uint8_t character, uint8_t animation)
{
    set_overworld_scratch_character(g, character);
    set_overworld_scratch_animation(g, animation);
}

static uint8_t as_the_character(uint8_t animation, uint8_t character)
{
    return character == OVERWORLD_MAGUS ? (uint8_t)(animation + OVERWORLD_MAGUS_ANIMATIONS_ON) : animation;
}

static void give_animation(GameState *g, MenuObject walker, uint8_t character, uint8_t animation)
{
    uint8_t own = as_the_character(animation, character);
    menu_give_sprite_script(g, walker, own);
    ORIGINAL_EFFECT(leave_animation_picked(g, character, own));
}

// $C2:416C, $C2:3987
void overworld_stand(GameState *g, MenuObject walker, uint8_t character)
{
    overworld_stop_showing_walking(g, walker);
    give_animation(g, walker, character, overworld_standing_animation(g, overworld_facing(g, walker)));
}

/* The walking animation: the facing's first or second, by the walker's gait.
 * The original picks it by the whole facing byte, doubled in a byte, which
 * drops the bit that says it shows its walk. */
// $C2:3953
void overworld_show_walking(GameState *g, MenuObject walker, uint8_t character)
{
    enum { GAIT_BIT = 0x01 };
    uint8_t facing = (uint8_t)(menu_object_slots_facing(g, walker) & ~SHOWS_WALKING);
    uint8_t gait = menu_object_slots_gait(g, walker) & GAIT_BIT;
    give_animation(g, walker, character, overworld_walking_animation(g, facing, gait));
}

/* A walker that already walks this way keeps its animation going; one that
 * turns, or starts from standing, starts the walk afresh. */
void overworld_walk_facing(GameState *g, MenuObject walker, uint8_t character, Facing facing)
{
    if (overworld_facing(g, walker) == facing && overworld_shows_walking(g, walker))
        return;
    set_menu_object_slots_facing(g, walker, (uint8_t)(SHOWS_WALKING | facing));
    overworld_show_walking(g, walker, character);
}

bool overworld_animations_are_sound(const GameState *g, PartyPlace place, uint8_t character)
{
    enum { FACINGS = 4, GAITS = 2 };
    for (uint8_t facing = 0; facing < FACINGS; facing++) {
        for (uint8_t gait = 0; gait < GAITS; gait++)
            if (!menu_given_sprite_script_is_well_formed(
                    g, as_the_character(overworld_walking_animation(g, facing, gait), character)))
                return false;
        if (!menu_given_sprite_script_is_well_formed(
                g, as_the_character(overworld_standing_animation(g, facing), character)))
            return false;
    }
    return menu_given_sprite_script_is_well_formed(g, (uint8_t)(OVERWORLD_FIRST_IDLE_ANIMATION + place));
}

/* A walker that has stood $F0 frames plays its idle animation; the count then
 * holds just past it, so the animation plays on and isn't started again. */
enum { IDLE_LONG_ENOUGH = 0xF0, IDLE_PLAYING = 0xFE, IDLE_PLAYED = 0xFF };

// $C2:35B3, $C2:3C8C
void overworld_wait_idly(GameState *g, MenuObject walker, PartyPlace place, uint8_t character)
{
    uint8_t frames = (uint8_t)(overworld_idle_frames(g, place) + 1);
    set_overworld_idle_frames(g, place, frames);
    if (frames < IDLE_LONG_ENOUGH) {
        overworld_stand(g, walker, character);
        return;
    }
    if (frames != IDLE_PLAYED)
        menu_give_sprite_script(g, walker, (uint8_t)(OVERWORLD_FIRST_IDLE_ANIMATION + place));
    set_overworld_idle_frames(g, place, IDLE_PLAYING);
}

void overworld_stop_waiting(GameState *g, PartyPlace place) { set_overworld_idle_frames(g, place, 0); }

WorldSpot overworld_walker_spot(const GameState *g, MenuObject walker)
{
    return (WorldSpot){ menu_object_slots_x_pixel(g, walker), menu_object_slots_y_pixel(g, walker) };
}

void overworld_put_walker(GameState *g, MenuObject walker, WorldSpot at)
{
    set_menu_object_slots_x_pixel(g, walker, at.x);
    set_menu_object_slots_y_pixel(g, walker, at.y);
}

/* A pace is a velocity of whole pixels a frame. */
void overworld_head(GameState *g, MenuObject walker, MenuAxis axis, int8_t way)
{
    uint32_t pace = (uint32_t)(int32_t)way * MENU_POSITION_UNITS;
    menu_object_set_velocity(g, walker, axis == MENU_ACROSS ? (MenuPlace){ pace, 0 } : (MenuPlace){ 0, pace });
}

void overworld_halt(GameState *g, MenuObject walker) { menu_object_set_velocity(g, walker, (MenuPlace){ 0, 0 }); }

int16_t overworld_pace(const GameState *g, MenuObject walker, MenuAxis axis)
{
    MenuPlace v = menu_object_velocity(g, walker);
    return (int16_t)menu_pixel(axis == MENU_ACROSS ? v.x : v.y);
}

void overworld_walk_a_frame(GameState *g, MenuObject walker)
{
    TIMED_AS(g, 0xC20F38, menu_object_move(g, walker));
    menu_object_wrap_onto_map(g, walker);
}

WorldSpot overworld_goal(const GameState *g, MenuObject walker)
{
    return (WorldSpot){ menu_object_slots_goal_x(g, walker), menu_object_slots_goal_y(g, walker) };
}

void overworld_set_goal(GameState *g, MenuObject walker, WorldSpot goal)
{
    set_menu_object_slots_goal_x(g, walker, goal.x);
    set_menu_object_slots_goal_y(g, walker, goal.y);
}

// $C2:3A56, $C2:3A8F
WorldSpot overworld_spot_ahead(WorldSpot at, MenuAxis axis, int8_t way)
{
    int16_t step = (int16_t)(way * TILE_PIXELS);
    int16_t x = (int16_t)(at.x + (axis == MENU_ACROSS ? step : 0));
    uint16_t y = (uint16_t)(at.y + (axis == MENU_DOWN ? step : 0));
    if (x < 0)
        x = (int16_t)(x + MENU_MAP_WIDTH);
    else if (x >= MENU_MAP_WIDTH)
        x = (int16_t)(x - MENU_MAP_WIDTH);
    return (WorldSpot){ (uint16_t)x, (uint16_t)(y & (MENU_MAP_HEIGHT - 1)) };
}

enum { ARRIVAL_FACING = 0x03 };
Facing overworld_arrival_facing(const GameState *g) { return (Facing)(location_flags(g) & ARRIVAL_FACING); }
void overworld_set_arrival_facing(GameState *g, Facing facing) { set_location_flags(g, (uint8_t)facing); }

/* ---- The leader's trail ------------------------------------------------------------------ */

/* The trail is kept round: where the next step goes, in bytes of its two
 * tables of words. */
enum { TRAIL_BYTES = TRAIL_STEPS * TRAIL_STEP_BYTES };

// $C2:391C
void overworld_lay_trail(GameState *g, WorldSpot at)
{
    set_overworld_trail_next(g, 0);
    for (unsigned step = 0; step < TRAIL_STEPS; step++) {
        set_overworld_trail_x(g, step, at.x);
        set_overworld_trail_y(g, step, at.y);
    }
}

// $C2:3934
void overworld_leave_footprint(GameState *g, WorldSpot at)
{
    uint8_t next = overworld_trail_next(g);
    set_overworld_trail_x(g, next / TRAIL_STEP_BYTES, at.x);
    set_overworld_trail_y(g, next / TRAIL_STEP_BYTES, at.y);
    set_overworld_trail_next(g, (uint8_t)((next + TRAIL_STEP_BYTES) & (TRAIL_BYTES - 1)));
}

WorldSpot overworld_trail_back(const GameState *g, unsigned steps)
{
    unsigned at = (unsigned)(overworld_trail_next(g) - steps * TRAIL_STEP_BYTES) & (TRAIL_BYTES - 1);
    return (WorldSpot){ overworld_trail_x(g, at / TRAIL_STEP_BYTES), overworld_trail_y(g, at / TRAIL_STEP_BYTES) };
}

bool overworld_trail_is_kept(const GameState *g)
{
    uint8_t next = overworld_trail_next(g);
    return next < TRAIL_BYTES && next % TRAIL_STEP_BYTES == 0;
}

// $C2:3ACB
/* In 8x8 tiles, as a location keeps where the party stands. */
void overworld_note_party_tile(GameState *g)
{
    WorldSpot at = overworld_party_spot(g, PARTY_LEADER);
    set_location_x(g, (uint8_t)(at.x / TILE_PIXELS));
    set_location_y(g, (uint8_t)(at.y / TILE_PIXELS));
}
