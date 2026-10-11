/* The ground of the world map (game/overworld/terrain.c).
 *
 * The world map (overworld_map) is 96 x 64 map tiles of 16 x 16 pixels, each
 * showing a tile group of four 8x8 tiles; each of those is a kind of ground,
 * four bits (overworld_ground_kinds): a wall in its low two, a place's ground
 * -- the tiles round a town or a landmark, where its name shows and its
 * entrance is -- in the next. The walkers go by the 8x8 tiles: a tile of the
 * ground is a column of 192 and a row of 128.
 *
 * The map is also split into regions, a nibble a map tile (overworld_regions),
 * each with the song it plays (music.c). */
#ifndef CT_GAME_OVERWORLD_TERRAIN_H
#define CT_GAME_OVERWORLD_TERRAIN_H

#include <stdbool.h>
#include <stdint.h>

#include "menu/map.h"
#include "overworld/overworld.h"

/* A kind of ground. */
typedef uint8_t Ground;
enum { GROUND_WALL = 0x03, GROUND_OF_A_PLACE = 0x04 };

/* The ground of an 8x8 tile. The original keeps neither the column nor the
 * row on the map: one past an edge reads on into the map row before or after,
 * or past the map's ends, which a follower's search for walls does from the
 * map's first or last row (docs/TODO.md), so the port reads there too. */
Ground overworld_ground_at(GameState *g, uint8_t column, uint8_t row);

/* The ground of the four 8x8 tiles that meet at a spot (a walker's feet stand
 * at the corner of four), the map round across and down. */
typedef struct {
    Ground up_left, up_right, down_left, down_right;
} GroundAround;
GroundAround overworld_look_around(GameState *g, WorldSpot at);

/* The ground of the two 8x8 tiles below the four that meet at a spot (the
 * Epoch needs open ground under all six to come down). */
typedef struct {
    Ground left, right;
} GroundBelow;
GroundBelow overworld_look_below(GameState *g, WorldSpot at);

/* The leader keeps what it found round its feet, or at the spot it steps
 * to. */
void overworld_keep_ground(GameState *g, MenuObject walker, GroundAround around);
GroundAround overworld_ground_kept(const GameState *g, MenuObject walker);

/* A walker can't step to a spot where either tile above it is a wall; the
 * leader stands on a place's ground where both are. */
static inline bool ground_walls_off(GroundAround a) { return ((a.up_left | a.up_right) & GROUND_WALL) != 0; }
static inline bool ground_of_a_place(GroundAround a)
{
    return (a.up_left & GROUND_OF_A_PLACE) && (a.up_right & GROUND_OF_A_PLACE);
}

/* The region of the map a spot is in. The region map starts at the map's
 * second row of map tiles: a spot in the first row has none, and the original
 * reads before the region map there (the guards keep the party off it). */
uint8_t overworld_region_at(GameState *g, WorldSpot at);
bool overworld_spot_has_a_region(WorldSpot at);

/* The region the song of what the view shows is looked up in (music.c): the
 * original takes the middle tile's map row as a row of the region map, which,
 * that map starting at the map's second row, is the region of the map tile
 * below the middle. */
uint8_t overworld_region_in_view(GameState *g, MenuViewCentre centre);

/* The region the party's place in the location record is in (music.c's
 * location song), and whether the place is on the region map. */
uint8_t overworld_region_of_the_location(GameState *g);
bool overworld_location_has_a_region(const GameState *g);

/* For the original's code and shadow mode: the carry the region lookup of
 * the location's place leaves (terrain.c says which bit). */
bool overworld_region_lookup_carry_at_the_location(const GameState *g);

/* The registered entries (game/registry.c) and their guards: a lookup of the
 * ground with the map and the kinds where the overworld keeps them, and one
 * of the regions on the region map. */
void overworld_ground_at_entry(Snes *s);
bool overworld_ground_at_runs(Snes *s);
void overworld_region_at_entry(Snes *s);
bool overworld_region_at_runs(Snes *s);

#endif
