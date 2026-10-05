/* The overworld's followers: the second and third members of the party, who
 * walk after the leader over the world map.
 *
 * A follower waits until the leader has drawn away -- out of a box 16 pixels
 * round it for the second member, 32 for the third -- then heads for a spot
 * on the leader's trail (overworld.h): the leader's last step for the second
 * member, the one before for the third. It goes there in straight legs, at
 * most two: across then down, or down then across. Before it sets out it
 * looks along both ways round to the leader for walls (the tiles above the
 * row it walks along, or both tiles of the column it walks down); it takes
 * the way that is clear, or where both are, the one along the longer leg
 * first. Where both have a wall it looks back along the trail for a spot it
 * can reach either way, and failing that heads for its usual spot anyway. A
 * follower that has walked eight frames and stands on a tile's edge decides
 * again, so it keeps after a leader that turns.
 *
 * When the party boards a vehicle each follower walks the same way to the
 * spot the leader stands on, climbs aboard when it gets there and starts over
 * when it doesn't; getting off, it stands where the vehicle landed.
 *
 * While an event the party stepped onto runs, a follower runs the script the
 * event gives it (overworld.h). */
#include "overworld/frames.h"
#include "overworld/overworld.h"

#include "charge.h"
#include "original_effects.h"
#include "overworld/terrain.h"
#include "ram.h"
#include "vars.h"

static PartyPlace place_of(const GameState *g, MenuObject self)
{
    return (PartyPlace)menu_object_slots_party_place(g, self);
}

static uint8_t character_of(const GameState *g, MenuObject self)
{
    return overworld_character_in(g, place_of(g, self));
}

/* ---- The way to go ------------------------------------------------------------------------ */

/* A follower's route: the legs it has left, and which way its first goes. */
enum { ROUTE_LEGS = 0x0F, ROUTE_ACROSS_FIRST = 0x10, ROUTE_DOWN_FIRST = 0x20 };

static uint8_t route_of(const GameState *g, MenuObject self) { return menu_object_slots_route(g, self); }
static void set_route(GameState *g, MenuObject self, uint8_t route) { set_menu_object_slots_route(g, self, route); }

/* Which of the two ways round has a wall: across first, or down first. */
enum { WALL_ACROSS_FIRST = 0x01, WALL_DOWN_FIRST = 0x02, WALLS_BOTH_WAYS = WALL_ACROSS_FIRST | WALL_DOWN_FIRST };

/* A tile of the ground, as the search goes by them: 8x8, a column of 192 and
 * a row of 128, counted in bytes, so a leg that runs past the first or the
 * last comes round through the 256 a byte holds. */
typedef struct {
    uint8_t column, row;
} SearchTile;

static SearchTile tile_of(WorldSpot at) { return (SearchTile){ (uint8_t)(at.x / TILE_PIXELS), (uint8_t)(at.y / TILE_PIXELS) }; }

static bool wall(Ground g) { return (g & GROUND_WALL) != 0; }

/* The original keeps the search's working values in the mode's cells as it
 * goes: where it looked along a leg last and where the leg ran; the map's and
 * the kinds' pointers its lookups read through. */
static void leave_leg(GameState *g, uint8_t at, uint8_t line, uint8_t end)
{
    set_overworld_scratch_ground_map(g, RAM_OVERWORLD_MAP);
    set_overworld_scratch_ground_kinds(g, RAM_OVERWORLD_GROUND_KINDS);
    set_overworld_scratch_search_at(g, at);
    set_overworld_scratch_search_line(g, line);
    set_overworld_scratch_search_end(g, end);
}

/* The way a leg runs: a tile at a time towards its end, the shorter way as a
 * byte counts it. */
static int8_t way_along(uint8_t from, uint8_t to) { return (int8_t)(uint8_t)(to - from) < 0 ? -1 : 1; }

// $C2:4205
/* A leg across the row `row`, from `from` (not looked at) to `to`: whether
 * any tile of the row above it is a wall. Where the leg ends comes back
 * through `reached`. */
static bool wall_across(GameState *g, uint8_t from, uint8_t to, uint8_t row, uint8_t *reached)
{
    uint8_t above = (uint8_t)(row - 1), at = from;
    int8_t way = way_along(from, to);
    bool walled = false;
    while (at != to && !walled) {
        at = (uint8_t)(at + way);
        TIMED_AS(g, 0xC262AE, walled = wall(overworld_ground_at(g, at, above)));
    }
    ORIGINAL_EFFECT(leave_leg(g, at, row, to));
    *reached = at;
    return walled;
}

// $C2:425A
/* A leg down the column `column`, from the row `from` to `to`: whether either
 * tile of a row it crosses, beside the column on the left or right of the
 * walker's feet, is a wall. Its rows are each a row above the feet's. */
static bool wall_down(GameState *g, MenuObject self, uint8_t column, uint8_t from, uint8_t to)
{
    uint8_t left = (uint8_t)(column - 1), at = (uint8_t)(from - 1), end = (uint8_t)(to - 1);
    int8_t way = way_along(at, end);
    Ground left_ground = 0;
    bool walled = false, looked = false;
    while (at != end && !walled) {
        at = (uint8_t)(at + way);
        TIMED_AS(g, 0xC262AE, left_ground = overworld_ground_at(g, left, at));
        Ground right_ground;
        TIMED_AS(g, 0xC262AE, right_ground = overworld_ground_at(g, column, at));
        walled = wall(left_ground | right_ground);
        looked = true;
    }
    if (looked)
        ORIGINAL_EFFECT(set_menu_object_slots_wall_search_left(g, self, left_ground));
    ORIGINAL_EFFECT(leave_leg(g, column, (uint8_t)(at + 1), end));
    return walled;
}

// $C2:41B5
/* Which ways round from one tile to another have a wall: across then down,
 * and down then across. */
static uint8_t walls_between(GameState *g, MenuObject self, SearchTile from, SearchTile to)
{
    uint8_t walls = 0, reached;
    if (wall_across(g, from.column, to.column, from.row, &reached) || wall_down(g, self, reached, from.row, to.row))
        walls |= WALL_ACROSS_FIRST;
    if (wall_down(g, self, from.column, from.row, to.row) || wall_across(g, from.column, to.column, to.row, &reached))
        walls |= WALL_DOWN_FIRST;
    ORIGINAL_EFFECT(set_overworld_scratch_search_blocked(g, walls));
    return walls;
}

/* The search's own cells: where from and where to, in tiles, each kept as a
 * word over the next's first byte. */
static void leave_search(GameState *g, WorldSpot from, WorldSpot to)
{
    set_overworld_scratch_search_from_column(g, (uint8_t)(from.x / TILE_PIXELS));
    set_overworld_scratch_search_to_column(g, (uint8_t)(to.x / TILE_PIXELS));
    set_overworld_scratch_search_from_row(g, (uint8_t)(from.y / TILE_PIXELS));
    set_overworld_scratch_search_to_row(g, (uint16_t)(to.y / TILE_PIXELS));
}

/* The goal, and how far it is from the follower as it sets out. */
static void aim_at(GameState *g, MenuObject self, WorldSpot goal)
{
    WorldSpot at = overworld_walker_spot(g, self);
    overworld_set_goal(g, self, goal);
    set_menu_object_slots_to_goal_x(g, self, (uint16_t)(goal.x - at.x));
    set_menu_object_slots_to_goal_y(g, self, (uint16_t)(goal.y - at.y));
}

static int16_t to_goal(const GameState *g, MenuObject self, MenuAxis axis)
{
    return (int16_t)(axis == MENU_ACROSS ? menu_object_slots_to_goal_x(g, self) : menu_object_slots_to_goal_y(g, self));
}

static uint16_t distance(int16_t d) { return (uint16_t)(d < 0 ? -d : d); }

// $C2:3FF0
/* With both ways clear: one leg where the goal is level with the follower,
 * else the longer leg first (down, where they are as long). */
static uint8_t route_by_distance(GameState *g, MenuObject self)
{
    int16_t across = to_goal(g, self, MENU_ACROSS), down = to_goal(g, self, MENU_DOWN);
    if (across == 0)
        return ROUTE_DOWN_FIRST | 1;
    ORIGINAL_EFFECT(set_overworld_scratch_distance_across(g, distance(across)));
    if (down == 0)
        return ROUTE_ACROSS_FIRST | 1;
    return distance(down) >= distance(across) ? (ROUTE_DOWN_FIRST | 2) : (ROUTE_ACROSS_FIRST | 2);
}

static uint8_t route_round(GameState *g, MenuObject self, uint8_t walls)
{
    switch (walls) {
    case WALL_ACROSS_FIRST: return ROUTE_DOWN_FIRST | 2;
    case WALL_DOWN_FIRST: return ROUTE_ACROSS_FIRST | 2;
    default: return route_by_distance(g, self);
    }
}

/* Where on the trail a follower heads: the leader's last step for the second
 * member, the one before for the third. */
static WorldSpot usual_spot(const GameState *g, MenuObject self)
{
    return overworld_trail_back(g, (place_of(g, self) & 1) ? 1 : 2);
}

enum { TRAIL_TRIES = 16, TILE_SPOT = (uint16_t)~(TILE_PIXELS - 1) };

static void leave_trail_search(GameState *g, unsigned tries_left, unsigned steps_back)
{
    set_overworld_scratch_trail_tries(g, (uint8_t)tries_left);
    set_overworld_scratch_trail_looked_at(g, (uint16_t)((overworld_trail_next(g) - TRAIL_STEP_BYTES * steps_back)
                                                        & (TRAIL_STEPS * TRAIL_STEP_BYTES - 1)));
}

// $C2:4060
/* Boxed in both ways: back along the trail, from the leader's last step, for
 * a spot one of the ways round reaches, which the follower heads for, on its
 * tile's corner. Sixteen steps back and none, it heads for its usual spot,
 * whichever way is longer first. */
static uint8_t route_back_along_the_trail(GameState *g, MenuObject self)
{
    WorldSpot at = overworld_walker_spot(g, self);
    for (unsigned back = 1; back <= TRAIL_TRIES; back++) {
        WorldSpot spot = overworld_trail_back(g, back);
        ORIGINAL_EFFECT(leave_search(g, at, spot));
        uint8_t walls = walls_between(g, self, tile_of(at), tile_of(spot));
        if (walls != WALLS_BOTH_WAYS) {
            ORIGINAL_EFFECT(leave_trail_search(g, TRAIL_TRIES - back + 1, back));
            ORIGINAL_EFFECT(set_overworld_scratch_trail_way_round(g, walls));
            aim_at(g, self, (WorldSpot){ spot.x & TILE_SPOT, spot.y & TILE_SPOT });
            return route_round(g, self, walls);
        }
    }
    ORIGINAL_EFFECT(leave_trail_search(g, 0, TRAIL_TRIES + 1));
    aim_at(g, self, usual_spot(g, self));
    return route_by_distance(g, self);
}

// $C2:3FBC
/* The route to the goal, the ways round looked at as far as the leader. */
static uint8_t plan_the_route(GameState *g, MenuObject self)
{
    WorldSpot at = overworld_walker_spot(g, self), leader = overworld_party_spot(g, PARTY_LEADER);
    ORIGINAL_EFFECT(leave_search(g, at, leader));
    uint8_t walls = walls_between(g, self, tile_of(at), tile_of(leader));
    return walls == WALLS_BOTH_WAYS ? route_back_along_the_trail(g, self) : route_round(g, self, walls);
}

/* ---- Walking the legs ---------------------------------------------------------------------- */

// $C2:4138, $C2:4152, $C2:40E4, $C2:4184
/* Set the follower walking a leg along an axis, towards its goal as it set
 * out (or on, past a goal level with it), and facing that way. */
static void walk_a_leg(GameState *g, MenuObject self, MenuAxis axis)
{
    static const Facing FACING[2][2] = { [MENU_ACROSS] = { FACING_RIGHT, FACING_LEFT },
                                        [MENU_DOWN] = { FACING_DOWN, FACING_UP } };
    bool back = to_goal(g, self, axis) < 0;
    overworld_head(g, self, axis, back ? -1 : 1);
    set_menu_object_slots_facing_before(g, self, overworld_facing(g, self));
    overworld_stop_waiting(g, place_of(g, self));
    overworld_walk_facing(g, self, character_of(g, self), FACING[axis][back]);
}

static void walk_the_first_leg(GameState *g, MenuObject self, uint8_t route)
{
    set_route(g, self, route);
    walk_a_leg(g, self, (route & ROUTE_DOWN_FIRST) ? MENU_DOWN : MENU_ACROSS);
    set_menu_object_slots_age(g, self, 0);
}

/* At a leg's end: on to the other leg, or the route is walked. */
static bool walk_the_next_leg(GameState *g, MenuObject self, MenuAxis turned_to)
{
    uint8_t legs = (uint8_t)((route_of(g, self) & ROUTE_LEGS) - 1);
    if (legs == 0)
        return false;
    set_route(g, self, legs);
    walk_a_leg(g, self, turned_to);
    return true;
}

static bool at_goal(const GameState *g, MenuObject self, MenuAxis axis)
{
    WorldSpot at = overworld_walker_spot(g, self), goal = overworld_goal(g, self);
    return axis == MENU_ACROSS ? at.x == goal.x : at.y == goal.y;
}

/* ---- On foot ------------------------------------------------------------------------------- */

/* Close enough to the leader not to follow yet: a box round the follower,
 * the third member's twice the second's. Its edges are words, so a box that
 * would reach over the map's first rows or columns comes round to the word's
 * far end and takes nothing in. */
typedef struct {
    uint16_t top, bottom, left, right;
} Box;

static void leave_box(GameState *g, Box b, WorldSpot leader)
{
    set_overworld_scratch_box_top(g, b.top);
    set_overworld_scratch_box_bottom(g, b.bottom);
    set_overworld_scratch_box_left(g, b.left);
    set_overworld_scratch_box_right(g, b.right);
    set_overworld_scratch_box_leader_x(g, leader.x);
    set_overworld_scratch_box_leader_y(g, leader.y);
}

// $C2:3EE1, $C2:3F27
static bool near_the_leader(GameState *g, MenuObject self)
{
    enum { NEAR = 0x10 };
    uint16_t reach = (place_of(g, self) & 1) ? NEAR : 2 * NEAR;
    WorldSpot at = overworld_walker_spot(g, self), leader = overworld_party_spot(g, PARTY_LEADER);
    Box box = { (uint16_t)(at.y - reach), (uint16_t)(at.y + reach + 1), (uint16_t)(at.x - reach),
                (uint16_t)(at.x + reach + 1) };
    ORIGINAL_EFFECT(leave_box(g, box, leader));
    return leader.y >= box.top && leader.y < box.bottom && leader.x >= box.left && leader.x < box.right;
}

/* A follower stands, counting towards its idle animation. */
static void wait(GameState *g, MenuObject self)
{
    overworld_wait_idly(g, self, place_of(g, self), character_of(g, self));
    overworld_animate(g);
}

/* Frames a follower walks before it may decide its way again. */
enum { FRAMES_BEFORE_DECIDING = 8 };

static void walk_the_leg(GameState *g, MenuObject self, MenuAxis axis);

// $C2:3C39
/* Once it has stood (or walked) long enough and the leader is out of reach,
 * the follower heads for its spot on the trail. */
static void decide(GameState *g, MenuObject self)
{
    if (menu_object_slots_age(g, self) < FRAMES_BEFORE_DECIDING) {
        wait(g, self);
        return;
    }
    if (near_the_leader(g, self)) {
        set_menu_object_slots_age(g, self, 0);
        wait(g, self);
        return;
    }
    aim_at(g, self, usual_spot(g, self));
    uint8_t route = plan_the_route(g, self);
    walk_the_first_leg(g, self, route);
    if (route & ROUTE_ACROSS_FIRST) {
        overworld_set_step(g, self, FOLLOWER_WALKS_ACROSS);
        walk_the_leg(g, self, MENU_ACROSS);
    } else {
        overworld_set_step(g, self, FOLLOWER_WALKS_DOWN);
        walk_the_leg(g, self, MENU_DOWN);
    }
}

// $C2:3CDA, $C2:3D3B
/* A frame of a leg across or down. Having walked long enough to decide
 * again, on a tile's edge, it does; at the leg's end it turns onto its other
 * leg, or stands. */
static void walk_the_leg(GameState *g, MenuObject self, MenuAxis axis)
{
    WorldSpot at = overworld_walker_spot(g, self);
    uint16_t along = axis == MENU_ACROSS ? at.x : at.y;
    if (menu_object_slots_age(g, self) >= FRAMES_BEFORE_DECIDING && (along & (TILE_PIXELS - 1)) == 0) {
        overworld_set_step(g, self, FOLLOWER_DECIDES);
        decide(g, self);
        return;
    }
    MenuAxis other = axis == MENU_ACROSS ? MENU_DOWN : MENU_ACROSS;
    if (!at_goal(g, self, axis))
        overworld_walk_a_frame(g, self);
    else if (walk_the_next_leg(g, self, other))
        overworld_set_step(g, self, other == MENU_ACROSS ? FOLLOWER_WALKS_ACROSS : FOLLOWER_WALKS_DOWN);
    else {
        overworld_set_step(g, self, FOLLOWER_DECIDES);
        overworld_stand(g, self, character_of(g, self));
    }
    overworld_animate(g);
}

// $C2:3BC2
/* The follower sets off where the party is, a still and dressed member,
 * facing the way the party came onto the map. */
static void set_off(GameState *g, MenuObject self)
{
    PartyPlace place = place_of(g, self);
    overworld_set_step(g, self, FOLLOWER_DECIDES);
    overworld_dress(g, self, place);
    WorldSpot at = overworld_party_spot(g, PARTY_LEADER);
    menu_object_set_place(g, self, (MenuPlace){ (uint32_t)at.x * MENU_POSITION_UNITS, (uint32_t)at.y * MENU_POSITION_UNITS });
    overworld_halt(g, self);
    set_menu_object_slots_age(g, self, 0);
    Facing facing = overworld_arrival_facing(g);
    set_menu_object_slots_facing_before(g, self, facing);
    overworld_face(g, self, facing);
    set_menu_object_slots_gait(g, self, 0);
    overworld_stand(g, self, overworld_character_in(g, place));
}

/* ---- In an event --------------------------------------------------------------------------- */

// $C2:3D9C, $C2:3DC1
/* Once the event has given the follower its script, it takes it up and runs
 * it a frame at a time; where the script moves it, its place in the party
 * is where it is. */
static bool take_part_in_the_event(GameState *g, MenuObject self, const WalkerScripts *scripts)
{
    PartyPlace place = place_of(g, self);
    switch (overworld_step(g, self)) {
    case WALKER_AWAITS_ITS_SCRIPT: {
        uint16_t script = overworld_event_script_for(g, place);
        if (script == 0)
            return true;
        overworld_take_up_script(g, self, script);
        return scripts->run(scripts->ctx, WALKER_STARTS_ITS_SCRIPT) != WALKER_SCRIPT_HANDED_OVER;
    }
    case WALKER_RUNS_ITS_SCRIPT: {
        WalkerScriptEnd end = scripts->run(scripts->ctx, WALKER_GOES_ON_WITH_ITS_SCRIPT);
        if (end == WALKER_SCRIPT_HANDED_OVER)
            return false;
        if (end == WALKER_SCRIPT_DONE)
            overworld_set_step(g, self, WALKER_HAS_RUN_ITS_SCRIPT);
        overworld_set_party_spot(g, place, overworld_walker_spot(g, self));
        return true;
    }
    default: /* WALKER_HAS_RUN_ITS_SCRIPT */
        return true;
    }
}

// $C2:3B1B
/* On foot: while the overworld isn't on the map (a screen is opening) the
 * follower stands; while an event runs, it takes part in it. */
static bool walk_on_foot(GameState *g, MenuObject self, const WalkerScripts *scripts)
{
    if (!overworld_on_the_map(g)) {
        overworld_stand(g, self, character_of(g, self));
        overworld_animate(g);
        return true;
    }
    ORIGINAL_EFFECT(overworld_leave_step_table_row(g));
    if (overworld_event_running(g))
        return take_part_in_the_event(g, self, scripts);
    switch (overworld_step(g, self)) {
    case FOLLOWER_SETS_OFF:
        set_off(g, self);
        decide(g, self);
        break;
    case FOLLOWER_DECIDES:
        decide(g, self);
        break;
    case FOLLOWER_WALKS_ACROSS:
        walk_the_leg(g, self, MENU_ACROSS);
        break;
    default: /* FOLLOWER_WALKS_DOWN */
        walk_the_leg(g, self, MENU_DOWN);
        break;
    }
    return true;
}

/* ---- Boarding and getting off -------------------------------------------------------------- */

static void go_to_the_vehicle(GameState *g, MenuObject self, MenuAxis axis);

// $C2:3DE5
/* To the spot the leader stands on, on its tile's corner, by the clear way
 * round. */
static void head_for_the_vehicle(GameState *g, MenuObject self)
{
    WorldSpot leader = overworld_party_spot(g, PARTY_LEADER);
    aim_at(g, self, (WorldSpot){ leader.x & TILE_SPOT, leader.y & TILE_SPOT });
    uint8_t route = plan_the_route(g, self);
    walk_the_first_leg(g, self, route);
    if (route & ROUTE_ACROSS_FIRST) {
        overworld_set_step(g, self, FOLLOWER_GOES_ACROSS_TO_IT);
        go_to_the_vehicle(g, self, MENU_ACROSS);
    } else {
        overworld_set_step(g, self, FOLLOWER_GOES_DOWN_TO_IT);
        go_to_the_vehicle(g, self, MENU_DOWN);
    }
}

// $C2:3E17, $C2:3E7B
/* A frame of a leg to the vehicle; at the route's end the follower climbs
 * aboard where it is where the leader is, and starts over where it isn't. */
static void go_to_the_vehicle(GameState *g, MenuObject self, MenuAxis axis)
{
    MenuAxis other = axis == MENU_ACROSS ? MENU_DOWN : MENU_ACROSS;
    if (!at_goal(g, self, axis))
        overworld_walk_a_frame(g, self);
    else if (walk_the_next_leg(g, self, other))
        overworld_set_step(g, self, other == MENU_ACROSS ? FOLLOWER_GOES_ACROSS_TO_IT : FOLLOWER_GOES_DOWN_TO_IT);
    else {
        WorldSpot at = overworld_walker_spot(g, self), leader = overworld_party_spot(g, PARTY_LEADER);
        if (at.x == leader.x && at.y == leader.y) {
            overworld_climb_aboard(g, overworld_vehicle_boarded(g));
            overworld_set_step(g, self, FOLLOWER_IS_ABOARD);
        } else {
            overworld_set_step(g, self, FOLLOWER_HEADS_FOR_THE_VEHICLE);
            overworld_stand(g, self, character_of(g, self));
        }
    }
    overworld_animate(g);
}

static void board(GameState *g, MenuObject self)
{
    switch (overworld_step(g, self)) {
    case FOLLOWER_HEADS_FOR_THE_VEHICLE:
        head_for_the_vehicle(g, self);
        break;
    case FOLLOWER_GOES_ACROSS_TO_IT:
        go_to_the_vehicle(g, self, MENU_ACROSS);
        break;
    case FOLLOWER_GOES_DOWN_TO_IT:
        go_to_the_vehicle(g, self, MENU_DOWN);
        break;
    default:
        break;
    }
}

// $C2:3B8E, $C2:3BA8
static void get_off(GameState *g, MenuObject self)
{
    Vehicle vehicle = overworld_vehicle_boarded(g);
    if (vehicle == NO_VEHICLE)
        return;
    overworld_set_step(g, self, FOLLOWER_SETS_OFF);
    overworld_put_walker(g, self, overworld_vehicle_spot(g, vehicle));
    overworld_climb_down(g, vehicle);
}

/* ---- The follower's frame ------------------------------------------------------------------ */

// $C2:3AE2
bool overworld_follow(GameState *g, MenuObject self, const WalkerScripts *scripts)
{
    if (!overworld_place_taken(g, place_of(g, self)))
        return true;
    overworld_start_over_if_asked(g, self);
    switch (overworld_party_travel(g)) {
    case TRAVEL_ON_FOOT:
        return walk_on_foot(g, self, scripts);
    case TRAVEL_BOARDING:
        board(g, self);
        break;
    case TRAVEL_GETTING_OFF:
        get_off(g, self);
        break;
    default:
        break;
    }
    return true;
}
