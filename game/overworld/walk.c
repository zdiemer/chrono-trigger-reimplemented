/* The overworld's leader: the member the player walks over the world map.
 *
 * On foot the leader stands until the pad asks for something. A direction
 * turns it that way and, unless the two tiles ahead of its feet are a wall,
 * walks it to the next 8x8 tile, a pixel a frame, the map scrolling with it
 * and the layer over the map with that; each step it finishes goes on its
 * trail (overworld.h), and one ending on a place's ground has the places'
 * watcher look up what is there (places.c). Its buttons ask the main loop
 * (main_loop.c) for the rest, by the buttons the player assigned: confirming
 * enters the place whose exit it stands on, X's assignment opens the menus,
 * Start's the party's exchange (once the story allows it) and Y's the map
 * view.
 *
 * When the party boards a vehicle the leader walks to it, down first and
 * then across, the map still scrolling with it, and climbs aboard; while the
 * party rides, where the leader is is where the vehicle is; getting off, it
 * stands where the vehicle landed, facing down.
 *
 * While an event the leader stepped onto runs, the leader runs the script the
 * event gives it: that is the original's still (the guard, entries.c). */
#include "overworld/frames.h"
#include "overworld/overworld.h"

#include "charge.h"
#include "core/input.h"
#include "menu/map.h"
#include "original_effects.h"
#include "overworld/places.h"
#include "overworld/terrain.h"
#include "vars.h"

static uint8_t leader_character(const GameState *g) { return overworld_character_in(g, PARTY_LEADER); }

/* ---- The map moves with the leader ------------------------------------------------------- */

/* The original hands each scroll its plane and its step in the mode's first
 * two cells. */
static void leave_scroll_named(GameState *g, uint8_t plane, int8_t step)
{
    set_menu_object_scratch_plane(g, plane);
    set_menu_object_scratch_step(g, (uint8_t)step);
}

/* The map's two planes scroll by the leader's pace along the axis. */
static void scroll_the_map(GameState *g, MenuAxis axis, int8_t step)
{
    for (uint8_t plane = 1; plane <= 2; plane++) {
        ORIGINAL_EFFECT(leave_scroll_named(g, plane, step));
        TIMED_AS(g, axis == MENU_ACROSS ? 0xC20568 : 0xC2066C, menu_map_scroll(g, plane, axis, step));
    }
}

/* The layer over the map takes the same move with the next frame's drift. */
static void move_the_layer_over_the_map(GameState *g, MenuObject self)
{
    set_menu_pending_scroll_x(g, (uint16_t)overworld_pace(g, self, MENU_ACROSS));
    set_menu_pending_scroll_y(g, (uint16_t)overworld_pace(g, self, MENU_DOWN));
}

/* Where the leader is is where the party is, and the tile a location keeps
 * to come back to. */
static void party_is_where_the_leader_is(GameState *g, MenuObject self)
{
    overworld_set_party_spot(g, PARTY_LEADER, overworld_walker_spot(g, self));
    overworld_note_party_tile(g);
}

/* ---- On foot ------------------------------------------------------------------------------- */

static void note_the_ground(GameState *g, GroundAround around)
{
    uint8_t news = overworld_ground_news(g);
    if (ground_of_a_place(around))
        news |= GROUND_NEWS_LOOK | GROUND_NEWS_ON_A_PLACE;
    else
        news &= (uint8_t)~GROUND_NEWS_ON_A_PLACE;
    set_overworld_ground_news(g, news);
}

// $C2:36ED
/* A frame of a step: the leader moves a pixel, the map with it; at the next
 * tile the step is over, goes on the trail, and the ground there is noted. */
static void walk_a_frame_of_the_step(GameState *g, MenuObject self)
{
    overworld_walk_a_frame(g, self);
    scroll_the_map(g, MENU_ACROSS, (int8_t)overworld_pace(g, self, MENU_ACROSS));
    scroll_the_map(g, MENU_DOWN, (int8_t)overworld_pace(g, self, MENU_DOWN));
    move_the_layer_over_the_map(g, self);
    overworld_animate(g);
    party_is_where_the_leader_is(g, self);
    WorldSpot at = overworld_walker_spot(g, self);
    WorldSpot goal = overworld_goal(g, self);
    if (at.x != goal.x || at.y != goal.y)
        return;
    overworld_leave_footprint(g, at);
    overworld_set_step(g, self, LEADER_STANDS);
    note_the_ground(g, overworld_ground_kept(g, self));
}

// $C2:3619, $C2:3697
/* Turn the leader to walk a way and, where the two tiles ahead of its feet
 * are no wall, set off: the next tile is the step's goal. Against a wall it
 * stands, turned. */
static void step_towards(GameState *g, MenuObject self, Facing facing, MenuAxis axis, int8_t way)
{
    overworld_head(g, self, axis, way);
    overworld_walk_facing(g, self, leader_character(g), facing);
    overworld_stop_waiting(g, PARTY_LEADER);
    WorldSpot goal = overworld_spot_ahead(overworld_walker_spot(g, self), axis, way);
    overworld_set_goal(g, self, goal);
    GroundAround ahead = overworld_look_around(g, goal);
    overworld_keep_ground(g, self, ahead);
    if (ground_walls_off(ahead)) {
        overworld_halt(g, self);
        overworld_keep_ground(g, self, overworld_look_around(g, overworld_walker_spot(g, self)));
        overworld_stand(g, self, leader_character(g));
        overworld_animate(g);
        return;
    }
    set_overworld_step_begun(g, 0);
    overworld_set_step(g, self, LEADER_WALKS);
    walk_a_frame_of_the_step(g, self);
}

/* What the leader's buttons ask of the main loop, by the buttons the player
 * assigned, in this order: entering a place (only on an exit), the menus, the
 * party's exchange (only once the story has got far enough), the map view. */
enum { ASSIGNED_MENUS = BUTTON_X, ASSIGNED_MAP_VIEW = BUTTON_Y, ASSIGNED_EXCHANGE = BUTTON_START };
enum { STORY_LETS_THE_PARTY_EXCHANGE = 0x49 };

static bool asks_the_main_loop(GameState *g, Buttons held)
{
    if ((held & ASSIGNED_CONFIRM) && overworld_on_an_exit(g)) {
        overworld_set_mode(g, OVERWORLD_ENTERING_A_PLACE);
        return true;
    }
    if (held & ASSIGNED_MENUS) {
        overworld_set_mode(g, OVERWORLD_OPENING_THE_MENUS);
        return true;
    }
    if ((held & ASSIGNED_EXCHANGE) && story_line(g) >= STORY_LETS_THE_PARTY_EXCHANGE) {
        overworld_set_mode(g, OVERWORLD_EXCHANGING_THE_PARTY);
        return true;
    }
    if (held & ASSIGNED_MAP_VIEW) {
        overworld_set_mode(g, OVERWORLD_OPENING_THE_MAP_VIEW);
        return true;
    }
    return false;
}

/* The pad's directions, up first. */
static const struct {
    Buttons button;
    Facing facing;
    MenuAxis axis;
    int8_t way;
} DIRECTIONS[] = {
    { BUTTON_UP, FACING_UP, MENU_DOWN, -1 },
    { BUTTON_DOWN, FACING_DOWN, MENU_DOWN, 1 },
    { BUTTON_LEFT, FACING_LEFT, MENU_ACROSS, -1 },
    { BUTTON_RIGHT, FACING_RIGHT, MENU_ACROSS, 1 },
};

// $C2:3567
/* The leader stands, and the pad may ask for something or a step; standing
 * still, it counts towards its idle animation. Until the overworld is on the
 * map it only keeps its animation going. */
static void stand(GameState *g, MenuObject self)
{
    if (overworld_mode(g) != OVERWORLD_ON_THE_MAP) {
        overworld_animate(g);
        return;
    }
    Buttons held = input_mapped(g, ASSIGNED_HELD);
    if (held != 0 && !asks_the_main_loop(g, held)) {
        for (unsigned d = 0; d < sizeof DIRECTIONS / sizeof DIRECTIONS[0]; d++)
            if (held & DIRECTIONS[d].button) {
                step_towards(g, self, DIRECTIONS[d].facing, DIRECTIONS[d].axis, DIRECTIONS[d].way);
                return;
            }
    }
    overworld_stop_showing_walking(g, self);
    overworld_wait_idly(g, self, PARTY_LEADER, leader_character(g));
    overworld_animate(g);
}

// $C2:34F8
/* The leader sets off where the party is, facing the way the party came onto
 * the map (or got off a vehicle), still; its trail starts there; and the
 * ground under it is noted as a step's would be. */
static void set_off(GameState *g, MenuObject self)
{
    overworld_set_step(g, self, LEADER_STANDS);
    overworld_dress(g, self, PARTY_LEADER);
    WorldSpot at = overworld_party_spot(g, PARTY_LEADER);
    overworld_put_walker(g, self, at);
    overworld_halt(g, self);
    overworld_note_party_tile(g);
    set_menu_object_slots_gait(g, self, 0);
    overworld_face(g, self, overworld_arrival_facing(g));
    overworld_lay_trail(g, at);
    overworld_stand(g, self, leader_character(g));
    GroundAround under = overworld_look_around(g, at);
    overworld_keep_ground(g, self, under);
    if (ground_of_a_place(under))
        set_overworld_ground_news(g, (uint8_t)(overworld_ground_news(g) | GROUND_NEWS_LOOK | GROUND_NEWS_ON_A_PLACE));
}

// $C2:3439
/* On foot: while the overworld isn't on the map (a screen is opening) the
 * leader only keeps its animation going. */
static void walk_on_foot(GameState *g, MenuObject self)
{
    if (!overworld_on_the_map(g)) {
        overworld_animate(g);
        return;
    }
    ORIGINAL_EFFECT(overworld_leave_step_table_row(g));
    switch (overworld_step(g, self)) {
    case LEADER_SETS_OFF:
        set_off(g, self);
        stand(g, self);
        break;
    case LEADER_STANDS:
        stand(g, self);
        break;
    default: /* LEADER_WALKS */
        walk_a_frame_of_the_step(g, self);
        break;
    }
}

/* ---- Boarding and getting off -------------------------------------------------------------- */

/* A frame's walk towards the vehicle along one axis: the leader moves, the
 * party with it, and the map follows along that axis. */
static void walk_to_the_vehicle(GameState *g, MenuObject self, MenuAxis axis)
{
    overworld_walk_a_frame(g, self);
    party_is_where_the_leader_is(g, self);
    scroll_the_map(g, axis, (int8_t)overworld_pace(g, self, axis));
    move_the_layer_over_the_map(g, self);
}

/* Set the leader walking along an axis towards a coordinate of the goal,
 * facing the way it goes; already level with it, it doesn't turn. */
static void head_along(GameState *g, MenuObject self, MenuAxis axis, int16_t to_go)
{
    static const Facing FACING[2][2] = { [MENU_ACROSS] = { FACING_LEFT, FACING_RIGHT },
                                        [MENU_DOWN] = { FACING_UP, FACING_DOWN } };
    overworld_halt(g, self);
    if (to_go == 0)
        return;
    overworld_head(g, self, axis, to_go < 0 ? -1 : 1);
    overworld_face(g, self, FACING[axis][to_go > 0]);
}

// $C2:37BD
static void head_for_the_vehicle(GameState *g, MenuObject self, Vehicle vehicle)
{
    if (vehicle != NO_VEHICLE)
        overworld_set_goal(g, self, overworld_vehicle_spot(g, vehicle));
    head_along(g, self, MENU_DOWN, (int16_t)(overworld_goal(g, self).y - overworld_walker_spot(g, self).y));
    overworld_set_step(g, self, LEADER_WALKS_DOWN_TO_IT);
    overworld_show_walking(g, self, leader_character(g));
}

// $C2:3837
/* Down to the vehicle's row, then turned across to it. */
static void walk_down_to_the_vehicle(GameState *g, MenuObject self)
{
    walk_to_the_vehicle(g, self, MENU_DOWN);
    WorldSpot at = overworld_walker_spot(g, self), goal = overworld_goal(g, self);
    if (at.y == goal.y) {
        head_along(g, self, MENU_ACROSS, (int16_t)(goal.x - at.x));
        overworld_set_step(g, self, LEADER_WALKS_ACROSS_TO_IT);
        overworld_show_walking(g, self, leader_character(g));
    }
    overworld_animate(g);
}

// $C2:38C5
/* Across to the vehicle, and aboard. */
static void walk_across_to_the_vehicle(GameState *g, MenuObject self, Vehicle vehicle)
{
    overworld_walk_a_frame(g, self);
    if (overworld_walker_spot(g, self).x == overworld_goal(g, self).x) {
        overworld_set_step(g, self, LEADER_IS_ABOARD);
        overworld_climb_aboard(g, vehicle);
    }
    party_is_where_the_leader_is(g, self);
    scroll_the_map(g, MENU_ACROSS, (int8_t)overworld_pace(g, self, MENU_ACROSS));
    move_the_layer_over_the_map(g, self);
    overworld_animate(g);
}

// $C2:3489
static void board(GameState *g, MenuObject self)
{
    Vehicle vehicle = overworld_vehicle_boarded(g);
    switch (overworld_step(g, self)) {
    case LEADER_HEADS_FOR_THE_VEHICLE:
        head_for_the_vehicle(g, self, vehicle);
        walk_down_to_the_vehicle(g, self);
        break;
    case LEADER_WALKS_DOWN_TO_IT:
        walk_down_to_the_vehicle(g, self);
        break;
    case LEADER_WALKS_ACROSS_TO_IT:
        walk_across_to_the_vehicle(g, self, vehicle);
        break;
    default:
        break;
    }
}

// $C2:34AD, $C2:34CD
/* Off the vehicle: the leader stands where it landed, facing down when it
 * sets off again, and the party sets off from there. */
static void get_off(GameState *g, MenuObject self)
{
    Vehicle vehicle = overworld_vehicle_boarded(g);
    if (vehicle == NO_VEHICLE)
        return;
    overworld_set_step(g, self, LEADER_SETS_OFF);
    WorldSpot at = overworld_vehicle_spot(g, vehicle);
    overworld_put_walker(g, self, at);
    overworld_set_party_spot(g, PARTY_LEADER, at);
    overworld_climb_down(g, vehicle);
    set_menu_object_slots_gait(g, self, 0);
    overworld_set_arrival_facing(g, FACING_DOWN);
    overworld_note_party_tile(g);
}

/* ---- The leader's frame -------------------------------------------------------------------- */

// $C2:3404
void overworld_lead(GameState *g, MenuObject self)
{
    if (!overworld_place_taken(g, PARTY_LEADER))
        return;
    overworld_start_over_if_asked(g, self);
    Travel travel = overworld_party_travel(g);
    switch (travel) {
    case TRAVEL_ON_FOOT:
        walk_on_foot(g, self);
        break;
    case TRAVEL_IN_THE_EPOCH:
    case TRAVEL_ON_A_DACTYL:
        overworld_set_party_spot(g, PARTY_LEADER, overworld_vehicle_spot(g, (Vehicle)travel));
        break;
    case TRAVEL_BOARDING:
        board(g, self);
        break;
    case TRAVEL_GETTING_OFF:
        get_off(g, self);
        break;
    default:
        break;
    }
}
