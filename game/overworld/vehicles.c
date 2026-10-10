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
 * as the frame began, which the walkers compare to start their steps over. */
#include "overworld/vehicles.h"

#include "charge.h"
#include "core/input.h"
#include "menu/object.h"
#include "original_effects.h"
#include "overworld/places.h"
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

/* ---- Boarding ------------------------------------------------------------------------------ */

/* The Epoch is on the map where the party is in its period and it stands on
 * this map; the dactyl only on the one map it lives on. */
enum { DACTYL_MAP = 0x01F3, LOCATION_NUMBER = 0x01FF };

static bool vehicle_here(const GameState *g, Vehicle v)
{
    return (overworld_vehicle_state(g, v) & VEHICLE_HERE) != 0;
}

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
