/* The ground of the world map: what kind each 8x8 tile is, what the four
 * round a walker's feet are, and which region a spot is in
 * (overworld/terrain.h says how the map keeps them). */
#include "overworld/terrain.h"

#include "charge.h"
#include "cpu_io.h"
#include "menu/bridge.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
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

/* What the original leaves of a lookup: its operands halved and the halves'
 * bits, and the multiply by the map's width it found the row with. */
static void leave_ground_lookup(GameState *g, uint8_t column, uint8_t row)
{
    set_overworld_scratch_ground_column(g, (uint8_t)(column >> 1));
    set_overworld_scratch_ground_row(g, (uint8_t)(row >> 1));
    set_overworld_scratch_ground_column_odd(g, 0);
    set_overworld_scratch_ground_row_odd(g, row & 1);
    hw_set_multiplicand(g, (uint8_t)(row >> 1));
    (void)hw_multiply_by(g, MAP_COLUMNS);
}

// $C2:62AE
Ground overworld_ground_at(GameState *g, uint8_t column, uint8_t row)
{
    uint8_t group = map_tile(g, (unsigned)(row >> 1) * MAP_COLUMNS + (column >> 1));
    uint8_t kinds = overworld_ground_kinds(g, (unsigned)group * KINDS_A_GROUP + (row & 1));
    ORIGINAL_EFFECT(leave_ground_lookup(g, column, row));
    return (column & 1) ? (Ground)(kinds & LOW_NIBBLE) : (Ground)(kinds >> NIBBLE);
}

/* The lookups read the map and the kinds through pointers their callers set,
 * which the original leaves; and where the look round went from, in tiles. */
static void leave_pointers(GameState *g)
{
    set_overworld_scratch_ground_map(g, RAM_OVERWORLD_MAP);
    set_overworld_scratch_ground_kinds(g, RAM_OVERWORLD_GROUND_KINDS);
}

static void leave_look_around(GameState *g, uint16_t column, uint16_t row)
{
    leave_pointers(g);
    set_overworld_scratch_feet_column(g, column);
    set_overworld_scratch_feet_row(g, row);
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
    ORIGINAL_EFFECT(leave_look_around(g, column, row));
    return around;
}

static void leave_look_below(GameState *g, uint8_t below)
{
    leave_pointers(g);
    set_overworld_scratch_feet_row(g, below);
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
    ORIGINAL_EFFECT(leave_look_below(g, below));
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

/* ---- The regions ------------------------------------------------------------------------ */

/* A region map's row holds a nibble for each of the map's columns, the even
 * one high; its first row is the map's second. */
enum { MAP_TILE_PIXELS = 16, REGION_ROW_BYTES = MAP_COLUMNS / 2 };

static void leave_region_lookup(GameState *g, uint8_t column, uint8_t row)
{
    set_overworld_scratch_region_column(g, (uint8_t)(column >> 1));
    set_overworld_scratch_region_row(g, row);
    hw_set_multiplicand(g, row);
    uint16_t row_start = hw_multiply_by(g, REGION_ROW_BYTES);
    set_overworld_scratch_region_row_at(g, RAM_OVERWORLD_REGIONS + row_start);
}

/* The byte holding a map tile's region, and its neighbour's. */
static unsigned region_byte(uint8_t column, uint8_t row) { return (unsigned)row * REGION_ROW_BYTES + (column >> 1); }
static bool on_the_region_map(uint8_t column, uint8_t row) { return region_byte(column, row) < OVERWORLD_REGIONS_COUNT; }

static uint8_t region_pair(const GameState *g, uint8_t column, uint8_t row)
{
    return overworld_regions(g, region_byte(column, row));
}

// $C2:62ED
static uint8_t region_in(GameState *g, uint8_t column, uint8_t row)
{
    uint8_t pair = region_pair(g, column, row);
    ORIGINAL_EFFECT(leave_region_lookup(g, column, row));
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
    ORIGINAL_EFFECT(set_overworld_scratch_region_row_word(g, (uint16_t)(at.y / MAP_TILE_PIXELS - 1)));
    return region_in(g, (uint8_t)(at.x / MAP_TILE_PIXELS), region_row_of(at));
}

/* ---- The entries ------------------------------------------------------------------------ */

/* Both run on the mode's page, their operands in its first two cells: the
 * tile's column and row. */
static bool on_the_mode_page(const Snes *s) { return m_direct_page(s) == MENU_PAGE && m_bank_reaches_low_ram(s); }

/* The lookup of the ground reads the map and the kinds through the pointers
 * its caller sets: the overworld's, every time. */
bool overworld_ground_at_runs(Snes *s)
{
    if (!on_the_mode_page(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = overworld_scratch_ground_map(g) == RAM_OVERWORLD_MAP
              && overworld_scratch_ground_kinds(g) == RAM_OVERWORLD_GROUND_KINDS;
    vars_peek(false);
    return ok;
}

// $C2:62AE
/* It hands back the kind in an 8-bit accumulator. */
void overworld_ground_at_entry(Snes *s)
{
    GameState *g = state_of(s);
    Ground kind = overworld_ground_at(g, overworld_scratch_ground_column(g), overworld_scratch_ground_row(g));
    m_sep(s, CPU_FLAG_M);
    m_set_a8(s, kind);
    m_rts(s);
}

/* The region lookup keeps to the region map. */
bool overworld_region_at_runs(Snes *s)
{
    if (!on_the_mode_page(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = on_the_region_map(overworld_scratch_region_column(g), overworld_scratch_region_row(g));
    vars_peek(false);
    return ok;
}

/* The carry the lookup's last shift leaves: the column's half for an odd
 * column, the region byte's fourth bit for an even one. */
static bool lookup_carry(const GameState *g, uint8_t column, uint8_t row)
{
    enum { LAST_BIT_SHIFTED_OUT = 0x08 };
    vars_peek(true);
    uint8_t pair = region_pair(g, column, row);
    vars_peek(false);
    return (column & 1) || (pair & LAST_BIT_SHIFTED_OUT);
}

// $C2:62ED
/* It hands back the region in a 16-bit word (its callers index a table with
 * it), with the carry its last shift left: the column's half for an odd
 * column, the region byte's fourth bit for an even one (lookup_carry). */
void overworld_region_at_entry(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t column = overworld_scratch_region_column(g), row = overworld_scratch_region_row(g);
    bool carry = lookup_carry(g, column, row);
    uint8_t region = region_in(g, column, row);
    m_sep(s, CPU_FLAG_M);
    s->cpu.a = region;
    m_set_flag(s, CPU_FLAG_C, carry);
    m_rts(s);
}

bool overworld_region_lookup_carry_at_the_location(const GameState *g)
{
    return lookup_carry(g, location_column(g), location_row(g));
}
