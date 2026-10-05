/* The overworld: the world map, which the party walks on foot between the
 * places it enters.
 *
 * The world map is a scene of the sprite-object mode ($C2:0000, menu/object.h):
 * the map is the mode's (menu/map.c), and the party, the place's sign, the
 * watcher of the ground under the leader and the region's music are objects
 * of the mode, each a handler run once a frame. While the party walks the
 * world map, the mode's main loop (main_loop.c) waits for frames and does what
 * the leader's buttons asked for: open the menus or the map view, enter a
 * place.
 *
 * - **The leader** (walk.c) takes the pad and walks the map an 8-pixel tile at
 *   a time, the map scrolling with it, unless the tiles ahead are a wall. Each
 *   step it finishes goes on its trail of the last sixteen.
 * - **The followers** (followers.c), the two other members, walk after it
 *   along the trail, finding their own way round walls, and stand once they
 *   are close.
 * - **The ground** (terrain.c) is the map's: each 8x8 tile is a kind, a wall
 *   or not, a place's ground or not; and the map is split into regions, each
 *   with its song (music.c).
 * - **The places** (places.c): when the leader steps onto a place's ground the
 *   map's lists say what is there -- an exit the player can enter with the
 *   first button, a place's name for the sign (sign.c), an event whose script
 *   runs once.
 * - **The vehicles**: the party walks to the Epoch or a dactyl to board it and
 *   gets off it again; flying them is the vehicles' own handlers' (still the
 *   original's), as is deciding when the party boards.
 *
 * A walker's position is a pixel of the map, which is MENU_MAP_WIDTH pixels
 * round and MENU_MAP_HEIGHT high (menu/object.h); a tile is TILE_PIXELS of it. */
#ifndef CT_GAME_OVERWORLD_OVERWORLD_H
#define CT_GAME_OVERWORLD_OVERWORLD_H

#include <stdbool.h>
#include <stdint.h>

#include "field/object.h"
#include "field/party.h"
#include "menu/object.h"
#include "state.h"

/* ---- How the party goes about the map --------------------------------------------------- */

/* How the party is going about the world map (overworld_travel). The
 * vehicles' controller (still the original's, $C2:309E) moves the party from
 * walking to boarding, to riding once all are aboard, to getting off when the
 * vehicle lands and back to walking once all are off. While the party stands
 * as it is, neither the leader nor the followers do anything: no routine of
 * the mode sets that, only a script's write to the cell could. */
typedef enum {
    TRAVEL_ON_FOOT = 0,
    TRAVEL_STANDING = 1,
    TRAVEL_IN_THE_EPOCH = 2,
    TRAVEL_ON_A_DACTYL = 3,
    TRAVEL_BOARDING = 4,
    TRAVEL_GETTING_OFF = 5,
    TRAVELS = 6,
} Travel;

/* The vehicle the party boards or gets off (overworld_boarding), as the
 * travel it rides in. */
typedef enum { NO_VEHICLE = 0, VEHICLE_EPOCH = TRAVEL_IN_THE_EPOCH, VEHICLE_DACTYL = TRAVEL_ON_A_DACTYL } Vehicle;

Travel overworld_party_travel(const GameState *g);
Vehicle overworld_vehicle_boarded(const GameState *g);

/* Where a vehicle stands, and its count of the party aboard (its state's low
 * bits): a walker that reaches it climbs aboard, one getting off climbs down. */
typedef struct {
    uint16_t x, y;
} WorldSpot;
WorldSpot overworld_vehicle_spot(const GameState *g, Vehicle v);
void overworld_climb_aboard(GameState *g, Vehicle v);
void overworld_climb_down(GameState *g, Vehicle v);

/* The Epoch's state bit the sign reads: the Epoch leaves through time (the
 * time dial runs, and the map holds still). */
enum { EPOCH_LEAVING_THROUGH_TIME = 0x10 };

/* ---- The party -------------------------------------------------------------------------- */

/* Whether the party has a member in the place (the places count from the
 * leader's, field/party.h); an empty place's character byte has its top bit
 * set. */
bool overworld_place_taken(const GameState *g, PartyPlace place);
uint8_t overworld_character_in(const GameState *g, PartyPlace place);

/* Where each of the party stands (overworld_party_places): the leader's is the
 * party's, which the menus, the map view and the places go by. */
WorldSpot overworld_party_spot(const GameState *g, PartyPlace place);
void overworld_set_party_spot(GameState *g, PartyPlace place, WorldSpot at);

/* What the overworld's main loop is doing (the menus' mode, menu_mode): the
 * leader's buttons ask it to enter a place or open a screen, a script's
 * command to leave for a location, the Epoch to travel through time; the
 * screens put it back on the map. */
typedef enum {
    OVERWORLD_STARTING = 0,
    OVERWORLD_ON_THE_MAP = 1,
    OVERWORLD_LEAVING = 2,
    OVERWORLD_ENTERING_A_PLACE = 3,
    OVERWORLD_HALTED = 4,
    OVERWORLD_OPENING_THE_MENUS = 5,
    OVERWORLD_OPENING_THE_MAP_VIEW = 6,
    OVERWORLD_BACK_ON_THE_MAP = 7,
    OVERWORLD_TRAVELLING_THROUGH_TIME = 8,
    OVERWORLD_EXCHANGING_THE_PARTY = 9,
    OVERWORLD_MODES = 10,
} OverworldMode;
OverworldMode overworld_mode(const GameState *g);
void overworld_set_mode(GameState *g, OverworldMode mode);

/* Whether the party's walkers are free to walk: the overworld starting or on
 * the map. */
bool overworld_on_the_map(const GameState *g);

/* Whether an event the party stepped onto is running (overworld_event). */
bool overworld_event_running(const GameState *g);

/* What the leader's last step found under it (overworld_ground_news): a
 * place's ground for the places' watcher to look up, which it clears once it
 * has; and whether the leader stands on a place's ground. */
enum { GROUND_NEWS_LOOK = 0x01, GROUND_NEWS_ON_A_PLACE = 0x02 };

/* ---- A walker: an object of the party ----------------------------------------------------- */

/* An overworld object's step through its handler (the slot's step byte). A
 * walker's starts over whenever how the party travels or whether an event
 * runs changes. */
typedef uint8_t OverworldStep;
void overworld_set_step(GameState *g, MenuObject o, OverworldStep step);
OverworldStep overworld_step(const GameState *g, MenuObject o);
bool overworld_walkers_start_over(const GameState *g);
void overworld_start_over_if_asked(GameState *g, MenuObject walker);
/* Scaffolding: what the original's choice of a step on foot leaves. */
void overworld_leave_step_table_row(GameState *g);

/* Dress a walker as the member in the place: drawn on the map in the
 * member's palette from the member's tiles, a priority higher on the one map
 * that asks it. */
void overworld_dress(GameState *g, MenuObject walker, PartyPlace place);

/* Run the walker's animation for the frame, which draws it. */
void overworld_animate(GameState *g);

/* Which way a walker faces, and whether it shows its walking animation (a
 * walker that stands shows its standing one). */
Facing overworld_facing(const GameState *g, MenuObject walker);
bool overworld_shows_walking(const GameState *g, MenuObject walker);
void overworld_face(GameState *g, MenuObject walker, Facing facing);
void overworld_stop_showing_walking(GameState *g, MenuObject walker);

/* Show the character's standing animation the way the walker faces (it no
 * longer shows its walk), or its walking one; or set it walking a way, its
 * walk started afresh unless it already walks that way. */
void overworld_stand(GameState *g, MenuObject walker, uint8_t character);
void overworld_show_walking(GameState *g, MenuObject walker, uint8_t character);
void overworld_walk_facing(GameState *g, MenuObject walker, uint8_t character, Facing facing);

/* The guard's: whether every animation a walker of the place may be given
 * is one the sprite scripts read. */
bool overworld_animations_are_sound(const GameState *g, PartyPlace place, uint8_t character);

/* A walker standing still counts its frames, and once it has stood long
 * enough plays its idle animation once (the leader's, the second's or the
 * third's); a walker that moves starts the count over. */
void overworld_wait_idly(GameState *g, MenuObject walker, PartyPlace place, uint8_t character);
void overworld_stop_waiting(GameState *g, PartyPlace place);

/* Where a walker stands on the map. */
WorldSpot overworld_walker_spot(const GameState *g, MenuObject walker);
void overworld_put_walker(GameState *g, MenuObject walker, WorldSpot at);

/* Set the walker moving a pixel a frame across (-1 left, 1 right) or down
 * (-1 up, 1 down), or stop it. */
void overworld_head(GameState *g, MenuObject walker, MenuAxis axis, int8_t way);
void overworld_halt(GameState *g, MenuObject walker);
/* Its velocity's pixel part, across and down. */
int16_t overworld_pace(const GameState *g, MenuObject walker, MenuAxis axis);

/* Move the walker a frame on, round the map. */
void overworld_walk_a_frame(GameState *g, MenuObject walker);

/* Where a walker is walking to (its goal). */
WorldSpot overworld_goal(const GameState *g, MenuObject walker);
void overworld_set_goal(GameState *g, MenuObject walker, WorldSpot goal);

/* The spot a tile on from `at` along an axis, the way given, round the map:
 * across, a spot off either edge comes round to the other; down, the map's
 * height keeps it. */
enum { TILE_PIXELS = 8 };
WorldSpot overworld_spot_ahead(WorldSpot at, MenuAxis axis, int8_t way);

/* The way the party faces as it sets off on the map: the way it arrived (a
 * location's flags keep it), or down, off a vehicle. Setting it sets the
 * flags whole. */
Facing overworld_arrival_facing(const GameState *g);
void overworld_set_arrival_facing(GameState *g, Facing facing);

/* ---- The leader's trail ------------------------------------------------------------------ */

/* The leader's trail: where each of its last sixteen steps ended, which the
 * followers walk after it; a step back is a step further from its end. */
enum { TRAIL_STEPS = 16, TRAIL_STEP_BYTES = 2 };
void overworld_lay_trail(GameState *g, WorldSpot at);
void overworld_leave_footprint(GameState *g, WorldSpot at);
WorldSpot overworld_trail_back(const GameState *g, unsigned steps);
/* Whether the trail's next step is one of its sixteen (the guards': the game
 * keeps it so). */
bool overworld_trail_is_kept(const GameState *g);

/* The tile the party stands on, which a location keeps as the place to come
 * back to (location_x, location_y). */
void overworld_note_party_tile(GameState *g);

#endif
