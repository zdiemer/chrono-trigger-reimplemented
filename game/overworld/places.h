/* The places on the world map (game/overworld/places.c): the map's three
 * lists of map tiles, which the leader's tile is looked up in when it steps
 * onto a place's ground -- the exits, from which the party enters a location
 * (or starts an event), each with the place's name for the sign; the events,
 * whose scripts run once as the party steps onto them; and the halts, which
 * stop the game. A record is open while the top bit of its first byte is
 * set: the map's scripts open and close them (menu/object_script.c's marks),
 * and an event closes as it starts. */
#ifndef CT_GAME_OVERWORLD_PLACES_H
#define CT_GAME_OVERWORLD_PLACES_H

#include <stdbool.h>
#include <stdint.h>

#include "overworld/overworld.h"

typedef enum { LIST_EXITS = 0, LIST_EVENTS = 1, LIST_HALTS = 2, PLACE_LISTS = 3 } PlaceList;

/* Whether the leader stands on an open exit, as the last look found. */
bool overworld_on_an_exit(const GameState *g);

/* Where the exit the leader stands on leads: a location, how the party
 * arrives there (the byte whose top seven bits are the location's flags) and
 * the 8x8 tile it arrives at; or, for the location $1FF, an event, whose
 * script's entry the arrival's column names. */
typedef struct {
    uint16_t location;
    uint8_t arrival, x, y;
} ExitTo;
enum { EXIT_TO_AN_EVENT = 0x01FF };
ExitTo overworld_exit_here(const GameState *g);
uint16_t overworld_exit_here_event_script(const GameState *g);

/* The world map's event scripts, in bank $7F, by entry. */
enum { EVENT_SCRIPTS_BANK = 0x7F };
uint16_t overworld_event_script_entry(const GameState *g, unsigned entry);

/* Start an event's script (an object running it in bank $7F): the party's
 * own scripts for it are cleared, for the event to give, and the event runs
 * until its script says it is over. */
void overworld_start_event(GameState *g, uint16_t script);

/* Leave the world map for a location: where the party stands on the world
 * map is kept to come back to (the map's second plane's top-left tile, sixteen
 * tiles in each way), the way back faces opposite the way in, and the
 * location to go to is set. The scripts' command does it too
 * (menu/object_script.c). */
void overworld_leave_for(GameState *g, uint16_t location, uint8_t arrival, uint8_t x, uint8_t y);

/* The guard's: whether the lists are as a map's setup loads them (each with
 * at least one record and no more than its table holds), and whether the
 * exit the leader stands on runs an event whose script is in the table. */
bool overworld_lists_are_loaded(const GameState *g);
bool overworld_exit_here_is_in_range(const GameState *g);

#endif
