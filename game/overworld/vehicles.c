/* The overworld's vehicles: when the party boards one, rides it and gets off.
 *
 * Two things carry the party over the world map: the Epoch, in any period it
 * has been left in (epoch.c flies it), and the dactyl on the 65,000,000 B.C.
 * map. An object of the mode, the vehicles' controller, watches them once a
 * frame while the overworld is on the map and no event runs:
 *
 * - on foot, the player's confirm button next to a vehicle has the party
 *   board it: the travel turns to boarding, and the walkers (walk.c,
 *   followers.c) make their way to it and climb aboard;
 * - once all the party is aboard, the party rides it;
 * - as the vehicle lets the party out (the Epoch lands, or comes back from
 *   travelling through time without its wings), the party gets off, and once
 *   all are off it is on foot again.
 *
 * Whether the party is next to a vehicle is a test of reach: each thing on
 * the map reaches a few pixels from its place in each direction
 * (overworld_data.h), and two are within reach of each other where those
 * reaches meet across and down. The controller notes how the party travelled
 * as the frame began, which the walkers compare to start their steps over.
 *
 * In the air the two fly alike (vehicles.h, "In the air"): their handlers
 * (epoch.c, dactyl.c) stand them, board them and hand them here to rise,
 * fly and come down. */
#include "overworld/vehicles.h"

#include "charge.h"
#include "core/input.h"
#include "menu/epoch.h"
#include "menu/menu_data.h"
#include "menu/object.h"
#include "menu/sprites.h"
#include "original_effects.h"
#include "overworld/music.h"
#include "overworld/places.h"
#include "overworld/terrain.h"
#include "vars.h"

/* ---- Within reach ------------------------------------------------------------------------- */

/* Along one axis: the gap between the two, less how far the nearer side of
 * each reaches towards the other, is less than nothing (as a signed word);
 * two at the same place always are. The map's wrap across isn't counted. */
static bool reach_along(uint16_t first, uint16_t first_back, uint16_t first_on, uint16_t second,
                        uint16_t second_back, uint16_t second_on)
{
    if (second == first)
        return true;
    uint16_t gap = second > first ? (uint16_t)(second - first - first_on - second_back)
                                  : (uint16_t)(first - second - second_on - first_back);
    return (int16_t)gap < 0;
}

/* The original hands the test its two spots and their reaches' records in
 * the mode's cells. */
void overworld_leave_reach_from(GameState *g, WorldSpot first, ReachRecord reaches)
{
    set_overworld_scratch_reach_first_x(g, first.x);
    set_overworld_scratch_reach_first_y(g, first.y);
    set_overworld_scratch_reach_first(g, OVERWORLD_REACHES_BANK | reaches);
}

static void leave_reach_to(GameState *g, WorldSpot second, ReachRecord reaches)
{
    set_overworld_scratch_reach_second_x(g, second.x);
    set_overworld_scratch_reach_second_y(g, second.y);
    set_overworld_scratch_reach_second(g, OVERWORLD_REACHES_BANK | reaches);
}

// $C2:2345, $C2:2353, $C2:2379
bool overworld_within_reach(GameState *g, WorldSpot first, ReachRecord first_reaches, WorldSpot second,
                            ReachRecord second_reaches)
{
    ORIGINAL_EFFECT(overworld_leave_reach_from(g, first, first_reaches));
    ORIGINAL_EFFECT(leave_reach_to(g, second, second_reaches));
    Reach a = overworld_reach(g, first_reaches), b = overworld_reach(g, second_reaches);
    return reach_along(first.x, a.left, a.right, second.x, b.left, b.right)
           && reach_along(first.y, a.up, a.down, second.y, b.up, b.down);
}

/* ---- In the air ---------------------------------------------------------------------------- */

bool overworld_vehicle_is(const GameState *g, Vehicle v, uint8_t bits) { return (overworld_vehicle_state(g, v) & bits) != 0; }

void overworld_mark_vehicle(GameState *g, Vehicle v, uint8_t bits, bool on)
{
    uint8_t state = overworld_vehicle_state(g, v);
    overworld_set_vehicle_state(g, v, (uint8_t)(on ? state | bits : state & ~bits));
}

void overworld_draw_vehicle(GameState *g, MenuObject self, uint8_t palette, bool over_the_party)
{
    uint8_t priority = over_the_party ? OVERWORLD_PRIORITY_3 : OVERWORLD_PRIORITY_2;
    set_menu_object_slots_look(g, self, (uint8_t)(MENU_LOOK_ON_THE_MAP | priority | palette));
}

/* The leader's dactyl carries the leader. */
static uint8_t palette_of(Vehicle v) { return v == VEHICLE_DACTYL ? overworld_member_palette(PARTY_LEADER) : EPOCH_PALETTE; }

void overworld_set_vehicle_flying(GameState *g, Vehicle v, bool flying)
{
    if (v == VEHICLE_EPOCH)
        set_overworld_epoch_flying(g, flying);
    else
        set_overworld_dactyl_flying(g, flying);
}

void overworld_show_vehicle_flying(GameState *g, Vehicle v, MenuObject self, Facing way)
{
    uint8_t animation = v == VEHICLE_EPOCH
                            ? overworld_epoch_flying_animation(g, overworld_vehicle_is(g, v, EPOCH_HAS_ITS_WINGS), way)
                            : overworld_dactyl_flying_animation(g, way);
    menu_give_sprite_script(g, self, animation);
}

/* How fast it flies the way it faces, in whole pixels a frame. */
static int16_t pace(const GameState *g, Vehicle v, Facing way, MenuAxis axis)
{
    return v == VEHICLE_EPOCH ? overworld_epoch_pace(g, way, axis) : overworld_dactyl_pace(g, way, axis);
}

void overworld_note_the_ground_under(GameState *g, Vehicle v, MenuObject self, WorldSpot at)
{
    overworld_keep_ground(g, self, overworld_look_around(g, at));
    if (v != VEHICLE_EPOCH)
        return;
    GroundBelow below = overworld_look_below(g, at);
    set_menu_object_slots_ground_below_left(g, self, below.left);
    set_menu_object_slots_ground_below_right(g, self, below.right);
}

/* The highest walls bar the dactyl's way; the Epoch flies over every wall. */
static bool way_barred(const GameState *g, Vehicle v, MenuObject self)
{
    return v == VEHICLE_DACTYL && ground_bars_flight(overworld_ground_kept(g, self));
}

void overworld_play_the_song_of_flight(GameState *g)
{
    enum { WINGS_OF_TIME = 0x13 };
    overworld_play_song(g, WINGS_OF_TIME);
}

static Facing facing(const GameState *g, MenuObject self) { return (Facing)menu_object_slots_facing(g, self); }
static WorldSpot place(const GameState *g, MenuObject self) { return overworld_walker_spot(g, self); }
static uint8_t frames_on(const GameState *g, MenuObject self) { return menu_object_slots_frames_since_step(g, self); }

static void put(GameState *g, MenuObject self, int16_t below)
{
    WorldSpot at = place(g, self);
    overworld_put_walker(g, self, (WorldSpot){ at.x, (uint16_t)(at.y + below) });
}

enum { HALF_A_PIXEL = MENU_POSITION_UNITS / 2 };

/* How many frames its take-off or landing lasts: the Epoch's slot keeps it
 * as it starts one, the dactyl's are always the same. */
static uint8_t manoeuvre_frames(const GameState *g, Vehicle v, MenuObject self, uint8_t frames)
{
    return v == VEHICLE_EPOCH ? menu_object_slots_epoch_manoeuvre_frames(g, self) : frames;
}

void overworld_vehicle_climb(GameState *g, Vehicle v, MenuObject self, int8_t way)
{
    menu_object_set_velocity(g, self, (MenuPlace){ 0, (uint32_t)(int32_t)(way * HALF_A_PIXEL) });
    set_menu_object_slots_age(g, self, 0);
    if (v == VEHICLE_EPOCH)
        set_menu_object_slots_epoch_manoeuvre_frames(g, self, way < 0 ? VEHICLE_RISING_FRAMES : VEHICLE_COMING_DOWN_FRAMES);
}

void overworld_vehicle_stop(GameState *g, MenuObject vehicle)
{
    overworld_halt(g, vehicle);
    set_menu_object_slots_age(g, vehicle, 0);
}

// $C2:443C, $C2:4D79
bool overworld_vehicle_rises(GameState *g, Vehicle v, MenuObject self)
{
    if (frames_on(g, self) == VEHICLE_OVER_THE_PARTY_AFTER)
        overworld_draw_vehicle(g, self, palette_of(v), true);
    if (frames_on(g, self) >= manoeuvre_frames(g, v, self, VEHICLE_RISING_FRAMES))
        return false;
    overworld_walk_a_frame(g, self);
    overworld_animate(g);
    return true;
}

/* Risen, it flies the way it faced as it rose, and the party will arrive
 * facing that way: the Epoch to its song of flight; the dactyl, which
 * stands and so takes off facing down, shows its flying that way. */
void overworld_vehicle_takes_to_the_air(GameState *g, Vehicle v, MenuObject self)
{
    overworld_set_step(g, self, VEHICLE_FLIES);
    overworld_show_vehicle_flying(g, v, self, v == VEHICLE_EPOCH ? facing(g, self) : FACING_DOWN);
    if (v == VEHICLE_EPOCH)
        overworld_play_the_song_of_flight(g);
    overworld_set_vehicle_flying(g, v, true);
    put(g, self, VEHICLE_HEIGHT_IN_THE_AIR);
    overworld_vehicle_stop(g, self);
    overworld_turn_arrival_facing(g, facing(g, self));
    overworld_vehicle_flies(g, v, self);
}

// $C2:4675, $C2:4F7E
void overworld_vehicle_flies_a_tile(GameState *g, Vehicle v, MenuObject self)
{
    overworld_walk_a_frame(g, self);
    overworld_carry_the_map(g, self);
    overworld_animate(g);
    WorldSpot at = place(g, self), goal = overworld_goal(g, self);
    overworld_keep_vehicle_spot(g, v, at);
    if (at.x != goal.x || at.y != goal.y)
        return;
    /* The original stores the step as a word, which clears the count of
     * frames beside it. */
    overworld_set_step(g, self, VEHICLE_FLIES);
    set_menu_object_slots_frames_since_step(g, self, 0);
}

// $C2:461D, $C2:4EE2
/* Turn it the way the pad says and fly a tile on, unless a wall too high for
 * it is there; turned, it shows that way, and the party will arrive facing
 * it. The Epoch's slot keeps the way it faced before a turn, as a
 * follower's does; the dactyl's keeps the way it set down facing. */
static void head(GameState *g, Vehicle v, MenuObject self, Facing way)
{
    Facing was = facing(g, self);
    if (v == VEHICLE_EPOCH)
        set_menu_object_slots_facing_before(g, self, was);
    set_menu_object_slots_facing(g, self, way);
    menu_object_set_velocity(g, self, (MenuPlace){ (uint32_t)(int32_t)pace(g, v, way, MENU_ACROSS) << 16,
                                                   (uint32_t)(int32_t)pace(g, v, way, MENU_DOWN) << 16 });
    if (way != was) {
        overworld_turn_arrival_facing(g, way);
        overworld_show_vehicle_flying(g, v, self, way);
    }
    WorldSpot goal = overworld_a_tile_on(g, self);
    overworld_set_goal(g, self, goal);
    overworld_note_the_ground_under(g, v, self, goal);
    if (way_barred(g, v, self)) {
        overworld_halt(g, self);
        overworld_note_the_ground_under(g, v, self, place(g, self));
        overworld_animate(g);
        return;
    }
    set_overworld_step_begun(g, 0);
    overworld_set_step(g, self, VEHICLE_FLIES_A_TILE);
    overworld_vehicle_flies_a_tile(g, v, self);
}

/* The location a vehicle is parked on, as the vehicles in the air see it
 * (the location compared whole, where the boarding below takes the
 * location's number alone). */
static bool parked_here(const GameState *g, Vehicle v)
{
    if (!overworld_vehicle_is(g, v, VEHICLE_HERE))
        return false;
    return location_id(g) == (v == VEHICLE_EPOCH ? menu_epoch_location(g) : DACTYL_MAP);
}

static ReachRecord reach_in_the_air(Vehicle v)
{
    return v == VEHICLE_EPOCH ? REACH_OF_THE_EPOCH_IN_FLIGHT : REACH_OF_THE_DACTYL_FROM_THE_AIR;
}

// $C2:4917, $C2:48E8
/* The other vehicle parked on this map within reach: neither comes down on
 * the other. */
static bool over_the_other_vehicle(GameState *g, Vehicle v)
{
    Vehicle other = v == VEHICLE_EPOCH ? VEHICLE_DACTYL : VEHICLE_EPOCH;
    WorldSpot at = overworld_vehicle_spot(g, v);
    ORIGINAL_EFFECT(overworld_leave_reach_from(g, at, reach_in_the_air(v)));
    return parked_here(g, other)
           && overworld_within_reach(g, at, reach_in_the_air(v), overworld_vehicle_spot(g, other), reach_in_the_air(other));
}

/* Over the part of its map no vehicle comes down in: the map the party is
 * on, for the Epoch; the dactyls' own, for the dactyl. */
static bool kept_off_the_ground(GameState *g, Vehicle v, MenuObject self)
{
    bool kept_off;
    if (v == VEHICLE_EPOCH)
        TIMED_AS(g, 0xC6E797, kept_off = menu_epoch_kept_off_the_ground(g, self));
    else
        TIMED_AS(g, 0xC6E74E, kept_off = menu_epoch_kept_off_the_ground_of(g, self, DACTYL_MAP - MENU_FIRST_WORLD_MAP));
    return kept_off;
}

static bool may_come_down(GameState *g, Vehicle v, MenuObject self)
{
    return overworld_open_ground_kept(g, self) && !over_the_other_vehicle(g, v) && !kept_off_the_ground(g, v, self);
}

// $C2:4569, $C2:4E78
/* Coming down, from as high as it flies, facing down: the party will arrive
 * facing down. The Epoch's song of flight gives way to the map's; the
 * dactyl lands as it takes off, and the others come down with it. */
static void come_down(GameState *g, Vehicle v, MenuObject self)
{
    overworld_vehicle_climb(g, v, self, 1);
    put(g, self, -VEHICLE_HEIGHT_IN_THE_AIR);
    set_menu_object_slots_facing(g, self, FACING_DOWN);
    overworld_turn_arrival_facing(g, FACING_DOWN);
    menu_give_sprite_script(g, self, v == VEHICLE_EPOCH ? EPOCH_LANDS : DACTYL_TAKES_OFF);
    overworld_set_vehicle_flying(g, v, false);
    if (v == VEHICLE_EPOCH)
        overworld_play_the_song_in_view(g);
    else
        overworld_mark_vehicle(g, v, DACTYL_COMING_DOWN, true);
    overworld_set_step(g, self, VEHICLE_COMES_DOWN);
}

// $C2:4510, $C2:4E20
/* The confirm button brings it down where it may (over the Black Omen, the
 * Epoch's takes the party in instead). False where it may not, and the pad
 * steers it on. */
static bool come_down_if_it_may(GameState *g, Vehicle v, MenuObject self)
{
    if (v == VEHICLE_EPOCH && overworld_epoch_takes_the_party_into_the_omen(g)) {
        overworld_animate(g);
        return true;
    }
    if (!may_come_down(g, v, self))
        return false;
    come_down(g, v, self);
    overworld_animate(g);
    return true;
}

// $C2:44CC, $C2:4DE4
/* The buttons in flight: the time gate (the Epoch's), the menus, the map
 * view, or a way to fly, in that order. */
static void steer(GameState *g, Vehicle v, MenuObject self, Buttons buttons)
{
    Facing way;
    if (v == VEHICLE_EPOCH && (buttons & OVERWORLD_ASSIGNED_START)) {
        overworld_epoch_opens_the_time_gate(g, self);
    } else if (buttons & OVERWORLD_ASSIGNED_MENUS) {
        overworld_set_mode(g, OVERWORLD_OPENING_THE_MENUS);
    } else if (buttons & OVERWORLD_ASSIGNED_MAP_VIEW) {
        overworld_set_mode(g, OVERWORLD_OPENING_THE_MAP_VIEW);
    } else if (overworld_way_held(buttons, &way)) {
        head(g, v, self, way);
        return;
    }
    overworld_animate(g);
}

// $C2:44B7, $C2:4DCF
/* In the air it waits on the pad while the overworld is on the map. */
void overworld_vehicle_flies(GameState *g, Vehicle v, MenuObject self)
{
    Buttons buttons = input_mapped(g, ASSIGNED_HELD);
    if (overworld_mode(g) != OVERWORLD_ON_THE_MAP || buttons == 0) {
        overworld_animate(g);
        return;
    }
    if ((buttons & ASSIGNED_CONFIRM) && come_down_if_it_may(g, v, self))
        return;
    steer(g, v, self, buttons);
}

// $C2:46E2, $C2:4FEB
void overworld_vehicle_comes_down(GameState *g, Vehicle v, MenuObject self)
{
    if (frames_on(g, self) == VEHICLE_UNDER_THE_PARTY_AFTER)
        overworld_draw_vehicle(g, self, palette_of(v), false);
    if (frames_on(g, self) < manoeuvre_frames(g, v, self, VEHICLE_COMING_DOWN_FRAMES)) {
        overworld_walk_a_frame(g, self);
        overworld_animate(g);
        return;
    }
    bool epoch = v == VEHICLE_EPOCH;
    overworld_mark_vehicle(g, v, (uint8_t)(VEHICLE_OUT | (epoch ? 0 : DACTYL_COMING_DOWN)), false);
    overworld_set_step(g, self, epoch ? EPOCH_SETS_DOWN : DACTYL_WAITS_FOR_THE_PARTY);
    menu_give_sprite_script(g, self, epoch ? EPOCH_STANDS_WITH_ITS_WINGS : DACTYL_STANDS);
    overworld_vehicle_stop(g, self);
    overworld_animate(g);
}

/* ---- Boarding ------------------------------------------------------------------------------ */

/* The Epoch is on the map where the party is in its period and it stands on
 * this map; the dactyl only on the one map it lives on. */
enum { LOCATION_NUMBER = 0x01FF };

static bool vehicle_here(const GameState *g, Vehicle v) { return overworld_vehicle_is(g, v, VEHICLE_HERE); }

static bool epoch_on_this_map(const GameState *g)
{
    return vehicle_here(g, VEHICLE_EPOCH) && (location_id(g) & LOCATION_NUMBER) == menu_epoch_location(g);
}

static bool dactyl_on_this_map(const GameState *g)
{
    return vehicle_here(g, VEHICLE_DACTYL) && location_id(g) == DACTYL_MAP;
}

static void board(GameState *g, Vehicle v)
{
    set_overworld_boarding(g, (uint8_t)v);
    set_overworld_travel(g, TRAVEL_BOARDING);
    overworld_step_off_the_places(g);
}

/* At one point of the story the party, next to the Epoch, is shown a scene
 * instead of boarding it (a script holds the map meanwhile). */
enum { STORY_SHOWS_A_SCENE_AT_THE_EPOCH = 0xD2 };

// $C2:30CD
/* The player's confirm button, with the party next to a vehicle, has it
 * board that vehicle: the Epoch before the dactyl. */
static void board_when_asked(GameState *g)
{
    if (!(input_mapped(g, ASSIGNED_HELD) & ASSIGNED_CONFIRM))
        return;
    WorldSpot party = overworld_party_spot(g, PARTY_LEADER);
    ORIGINAL_EFFECT(overworld_leave_reach_from(g, party, REACH_OF_THE_PARTY));
    if (epoch_on_this_map(g)
        && overworld_within_reach(g, party, REACH_OF_THE_PARTY, overworld_vehicle_spot(g, VEHICLE_EPOCH),
                                  REACH_OF_THE_EPOCH)) {
        if (menu_story_progress(g) == STORY_SHOWS_A_SCENE_AT_THE_EPOCH) {
            overworld_set_mode(g, OVERWORLD_BACK_ON_THE_MAP);
            TIMED_AS(g, 0xC204D9, (void)menu_start_script(g, EPOCH_BOARDED_IN_A_SCENE, MENU_ORDINARY_SLOT));
            return;
        }
        board(g, VEHICLE_EPOCH);
        return;
    }
    if (dactyl_on_this_map(g)
        && overworld_within_reach(g, party, REACH_OF_THE_PARTY, overworld_vehicle_spot(g, VEHICLE_DACTYL),
                                  REACH_OF_THE_DACTYL))
        board(g, VEHICLE_DACTYL);
}

/* How many of the party there are to climb aboard or down. */
static uint8_t party_size(const GameState *g)
{
    uint8_t members = 0;
    for (PartyPlace place = PARTY_LEADER; place <= PARTY_SECOND_MEMBER; place++)
        if (overworld_place_taken(g, place))
            members++;
    return members;
}

static uint8_t aboard(const GameState *g, Vehicle v) { return overworld_vehicle_state(g, v) & VEHICLE_ABOARD; }

static bool rides(Vehicle v) { return v == VEHICLE_EPOCH || v == VEHICLE_DACTYL; }

// $C2:309E
void overworld_control_the_vehicles(GameState *g)
{
    if (!overworld_on_the_map(g) || overworld_event_running(g))
        return;
    Travel travel = overworld_party_travel(g);
    set_overworld_travel_before(g, (uint8_t)travel);
    Vehicle vehicle = overworld_vehicle_boarded(g);
    switch (travel) {
    case TRAVEL_ON_FOOT:
        board_when_asked(g);
        break;
    case TRAVEL_IN_THE_EPOCH:
    case TRAVEL_ON_A_DACTYL:
        // $C2:31B4, $C2:31C2
        if (!(overworld_vehicle_state(g, (Vehicle)travel) & VEHICLE_OUT))
            set_overworld_travel(g, TRAVEL_GETTING_OFF);
        break;
    case TRAVEL_BOARDING: {
        // $C2:31D0
        uint8_t members = party_size(g);
        ORIGINAL_EFFECT(set_overworld_scratch_boarders(g, members));
        if (rides(vehicle) && aboard(g, vehicle) == members)
            set_overworld_travel(g, (uint8_t)vehicle);
        break;
    }
    case TRAVEL_GETTING_OFF:
        // $C2:321C
        if (rides(vehicle) && aboard(g, vehicle) == 0) {
            set_overworld_travel(g, TRAVEL_ON_FOOT);
            set_overworld_boarding(g, NO_VEHICLE);
        }
        break;
    default: /* TRAVEL_STANDING */
        break;
    }
}
