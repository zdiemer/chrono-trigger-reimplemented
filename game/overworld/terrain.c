/* The ground of the world map: what kind each 8x8 tile is, what the four
 * round a walker's feet are, and which region a spot is in
 * (overworld/terrain.h says how the map keeps them). */
#include "overworld/terrain.h"

#include "charge.h"
#include "ram.h"
#include "vars.h"

/* The map in map tiles, and in the 8x8 tiles the walkers go by: two of those
 * a map tile each way. */
enum { MAP_COLUMNS = 96, TILE_COLUMNS = 2 * MAP_COLUMNS, TILE_ROWS = 128 };

/* A tile group's kinds: a byte for its upper two tiles and one for its lower,
 * the left one in the high nibble. */
enum { KINDS_A_GROUP = 2, NIBBLE = 4, LOW_NIBBLE = 0x0F };

/* A map tile's place in the map. Past the map's last byte the original reads
 * on into what follows it in work RAM, as it reads a row past the last column
 * into the next row (both only a search for walls at the map's edge does). */
static uint8_t map_tile(const GameState *g, unsigned tile)
{
    return tile < OVERWORLD_MAP_COUNT ? overworld_map(g, tile) : overworld_map_unchecked(g, tile);
}

// $C2:62AE
Ground overworld_ground_at(GameState *g, uint8_t column, uint8_t row)
{
    uint8_t group = map_tile(g, (unsigned)(row >> 1) * MAP_COLUMNS + (column >> 1));
    uint8_t kinds = overworld_ground_kinds(g, (unsigned)group * KINDS_A_GROUP + (row & 1));
    return (column & 1) ? (Ground)(kinds & LOW_NIBBLE) : (Ground)(kinds >> NIBBLE);
}

// $C2:39D5
/* The tile left of the first column is the last; the row above the first is
 * the last's, as the map's height keeps it. */
GroundAround overworld_look_around(GameState *g, WorldSpot at)
{
    uint16_t column = at.x / TILE_PIXELS, row = at.y / TILE_PIXELS;
    uint8_t left = column == 0 ? (uint8_t)(TILE_COLUMNS - 1) : (uint8_t)(column - 1);
    uint8_t above = (uint8_t)((row - 1) & (TILE_ROWS - 1));
    GroundAround around;
    TIMED_AS(g, 0xC262AE, around.up_left = overworld_ground_at(g, left, above));
    TIMED_AS(g, 0xC262AE, around.up_right = overworld_ground_at(g, (uint8_t)column, above));
    TIMED_AS(g, 0xC262AE, around.down_left = overworld_ground_at(g, left, (uint8_t)row));
    TIMED_AS(g, 0xC262AE, around.down_right = overworld_ground_at(g, (uint8_t)column, (uint8_t)row));
    return around;
}

// $C2:4A58
/* The tile left of the first column is the last, and the row below the last
 * the first, as the map's height keeps it. */
GroundBelow overworld_look_below(GameState *g, WorldSpot at)
{
    uint16_t column = at.x / TILE_PIXELS, row = at.y / TILE_PIXELS;
    uint8_t left = column == 0 ? (uint8_t)(TILE_COLUMNS - 1) : (uint8_t)(column - 1);
    uint8_t below = (uint8_t)((row + 1) & (TILE_ROWS - 1));
    GroundBelow ground;
    TIMED_AS(g, 0xC262AE, ground.left = overworld_ground_at(g, left, below));
    TIMED_AS(g, 0xC262AE, ground.right = overworld_ground_at(g, (uint8_t)column, below));
    return ground;
}

void overworld_keep_ground(GameState *g, MenuObject walker, GroundAround around)
{
    set_menu_object_slots_ground_up_left(g, walker, around.up_left);
    set_menu_object_slots_ground_up_right(g, walker, around.up_right);
    set_menu_object_slots_ground_down_left(g, walker, around.down_left);
    set_menu_object_slots_ground_down_right(g, walker, around.down_right);
}

GroundAround overworld_ground_kept(const GameState *g, MenuObject walker)
{
    return (GroundAround){ menu_object_slots_ground_up_left(g, walker), menu_object_slots_ground_up_right(g, walker),
                           menu_object_slots_ground_down_left(g, walker),
                           menu_object_slots_ground_down_right(g, walker) };
}

bool overworld_open_ground_kept(const GameState *g, MenuObject vehicle)
{
    enum { NOT_OPEN = GROUND_WALL | GROUND_OF_A_PLACE };
    GroundAround a = overworld_ground_kept(g, vehicle);
    uint8_t all = (uint8_t)(a.up_left | a.up_right | a.down_left | a.down_right
                            | menu_object_slots_ground_below_left(g, vehicle)
                            | menu_object_slots_ground_below_right(g, vehicle));
    return !(all & NOT_OPEN);
}

/* ---- The regions ------------------------------------------------------------------------ */

/* A region map's row holds a nibble for each of the map's columns, the even
 * one high; its first row is the map's second. */
enum { MAP_TILE_PIXELS = 16, REGION_ROW_BYTES = MAP_COLUMNS / 2 };

/* The byte holding a map tile's region, and its neighbour's. */
static unsigned region_byte(uint8_t column, uint8_t row) { return (unsigned)row * REGION_ROW_BYTES + (column >> 1); }
static bool on_the_region_map(uint8_t column, uint8_t row) { return region_byte(column, row) < OVERWORLD_REGIONS_COUNT; }

// $C2:62ED
static uint8_t region_in(const GameState *g, uint8_t column, uint8_t row)
{
    uint8_t pair = overworld_regions(g, region_byte(column, row));
    return (column & 1) ? (uint8_t)(pair & LOW_NIBBLE) : (uint8_t)(pair >> NIBBLE);
}

static uint8_t region_row_of(WorldSpot at) { return (uint8_t)(at.y / MAP_TILE_PIXELS - 1); }

// $C2:2F54-$C2:2F66
uint8_t overworld_region_in_view(GameState *g, MenuViewCentre centre)
{
    enum { REGION_ROWS = 0x40 };
    return region_in(g, (uint8_t)(centre.column / 2), (uint8_t)((centre.row / 2) & (REGION_ROWS - 1)));
}

bool overworld_spot_has_a_region(WorldSpot at)
{
    return on_the_region_map((uint8_t)(at.x / MAP_TILE_PIXELS), region_row_of(at));
}

/* The location's place is kept in 8x8 tiles: a map tile is two of them each
 * way, and its row is taken as a row of the region map, as the view's
 * centre's is. */
static uint8_t location_column(const GameState *g) { return (uint8_t)(location_x(g) >> 1); }
static uint8_t location_row(const GameState *g) { return (uint8_t)(location_y(g) >> 1); }

// $C2:2F15-$C2:2F21
uint8_t overworld_region_of_the_location(GameState *g) { return region_in(g, location_column(g), location_row(g)); }

bool overworld_location_has_a_region(const GameState *g)
{
    return on_the_region_map(location_column(g), location_row(g));
}

uint8_t overworld_region_at(GameState *g, WorldSpot at)
{
    return region_in(g, (uint8_t)(at.x / MAP_TILE_PIXELS), region_row_of(at));
}
