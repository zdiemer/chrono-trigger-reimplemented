/* The map's chests, which the player opens with the confirm button (the
 * confirm button is event/contact.c's; it talks first, then looks for a
 * chest).
 *
 * A chest stands on a tile the leader can't walk onto, whose byte in the
 * map's walkable table names it: the walkable bit clear, and the chest's
 * index among the location's below it. The game's chests are one list
 * (field/field_data.h, FieldChest), each with the tile it stands on and what
 * it holds; a location's are a run of it, and a chest's number in the whole
 * list is its story flag's (event/variables.h: the story memory keeps one a
 * chest, eight to a byte, from its second byte).
 *
 * Opening one raises its flag, asks the field's main loop to show it open,
 * and gives the party what it holds -- an item or gold, which the field then
 * announces in a message of its own -- or nothing of its own, where the
 * location's script does the rest (a chest with a monster in it). An opened
 * chest's message says it is empty. */
#include "event/chests.h"

#include "event/dialog.h"
#include "event/event_data.h"
#include "event/leaves.h"
#include "event/variables.h"
#include "field/field.h"
#include "field/field_data.h"
#include "field/object.h"
#include "field/layers.h"
#include "field/scratch.h"

/* A tile's byte in the walkable table. */
enum { WALKABLE_BIT = 0x80, CHEST_INDEX_BITS = 0x7F };

/* What a chest holds, by its contents word: gold (halved, in the bits below
 * this one), nothing of its own, or else the item in the low nine bits. */
enum { HOLDS_GOLD = 0x8000, HOLDS_NOTHING_OF_ITS_OWN = 0x4000, ITEM_BITS = 0x01FF };

/* The field's messages for a chest, by their line. */
enum { MESSAGE_ITEM = 0, MESSAGE_GOLD = 1, MESSAGE_EMPTY = 2 };

/* The variable the item goes in, which its message names. */
enum { THE_ITEM_FOUND = 0 };

/* A tile as the chests keep it: its row in the high byte. */
static uint16_t tile_number(uint8_t row, uint8_t column) { return (uint16_t)(row << 8 | column); }


/* ---- The chest before the leader ------------------------------------------------------ */

/* A tile the leader can't walk onto before it: the next one the way it
 * faces, and facing up the one past that too, where the next is open. */
typedef struct {
    bool found;
    uint8_t row, column;
    uint8_t walkable;
} Wall;

static void step_toward(Facing facing, uint8_t *row, uint8_t *column)
{
    switch (facing) {
    case FACING_UP: (*row)--; break;
    case FACING_DOWN: (*row)++; break;
    case FACING_LEFT: (*column)--; break;
    default: (*column)++; break;
    }
}

/* A tile off the map, which only a leader who is nobody looks at (field/
 * object.h, FieldLeader): the original reads its byte at its row and column
 * all the same, past the table's rows and columns, up into bank $7F for the
 * last rows. The one place the port reads them (docs/TODO.md, "Nobody
 * leading"). */
static uint8_t walkable_past_the_map(const GameState *g, uint8_t row, uint8_t column)
{
    return field_tile_walkable_unchecked(g, row, column);
}

static Wall look_at(const GameState *g, uint8_t row, uint8_t column)
{
    uint8_t walkable = tile_on_the_map((MapTile){ column, row }) ? field_tile_walkable(g, row, column)
                                                                  : walkable_past_the_map(g, row, column);
    return (Wall){ !(walkable & WALKABLE_BIT), row, column, walkable };
}

// $C0:1E9A-$C0:1F0D
static Wall wall_before_the_leader(const GameState *g)
{
    FieldLeader leader = field_leader(g);
    MapTile at = field_leader_tile(g, leader);
    Facing facing = field_leader_facing(g, leader);
    uint8_t row = at.y, column = at.x;
    step_toward(facing, &row, &column);
    Wall w = look_at(g, row, column);
    if (!w.found && facing == FACING_UP) {
        step_toward(facing, &row, &column);
        w = look_at(g, row, column);
    }
    return w;
}

bool chest_look_fits(const GameState *g)
{
    FieldLeader leader = field_leader(g);
    if (leader.nobody_leads)
        return true;
    MapTile at = field_leader_tile(g, leader);
    Facing facing = field_leader_facing(g, leader);
    uint8_t row = at.y, column = at.x;
    for (unsigned looks = facing == FACING_UP ? 2 : 1; looks > 0; looks--) {
        step_toward(facing, &row, &column);
        if (!tile_on_the_map((MapTile){ column, row }))
            return false;
    }
    return true;
}

/* ---- Opening it ------------------------------------------------------------------------ */

/* A chest's story flag. */
enum { CHEST_FLAGS = 1, CHEST_FLAG_BYTES = 0x40 };

static StoryOffset chest_flag_byte(uint16_t number)
{
    return (StoryOffset)(CHEST_FLAGS + ((number >> 3) & (CHEST_FLAG_BYTES - 1)));
}
static uint8_t chest_flag_bit(const GameState *g, uint16_t number) { return bit_mask(g, (uint8_t)(number & 7)); }

/* A chest's number among the game's, from where its record is. */
static uint16_t chest_number(const GameState *g, uint16_t place)
{
    return (uint16_t)((uint16_t)(place - field_games_chests(g)) / FIELD_CHEST_BYTES);
}

static bool chest_opened(const GameState *g, uint16_t number)
{
    return (story_byte(g, chest_flag_byte(number)) & chest_flag_bit(g, number)) != 0;
}

static void mark_chest_opened(GameState *g, uint16_t number)
{
    StoryOffset at = chest_flag_byte(number);
    set_story_byte(g, at, (uint8_t)(story_byte(g, at) | chest_flag_bit(g, number)));
}

static ChestGift what_the_chest_gives(GameState *g, uint16_t contents)
{
    if (contents & HOLDS_GOLD) {
        uint16_t gold = (uint16_t)(contents << 1);
        set_field_chest_gold(g, gold);
        return (ChestGift){ GIFT_GOLD, gold };
    }
    if (contents & HOLDS_NOTHING_OF_ITS_OWN)
        return (ChestGift){ GIFT_NOTHING, 0 };
    uint16_t item = contents & ITEM_BITS;
    set_variable_word(g, THE_ITEM_FOUND, item);
    return (ChestGift){ GIFT_ITEM, item };
}

/* For the original's code and shadow mode (event/leaves.h): the look keeps
 * the last tile it looked at in the second scratch word, and the lookup the
 * chest's place in the third and its number in the first, whose low byte its
 * flag's bit then takes; each leaves its overflow. */
static void leave_chest_lookup(uint8_t index, uint16_t first)
{
    event_leave_overflow(field_add_overflows16((uint16_t)(index * FIELD_CHEST_BYTES), first));
}

static void leave_chest_found(GameState *g, uint16_t place, uint16_t number)
{
    uint8_t bit = chest_flag_bit(g, number);
    keep_word(g, KEPT_FIRST, number);
    keep_byte(g, KEPT_FIRST, bit);
    keep_word(g, KEPT_THIRD, place);
    event_leave_overflow((bit & 0x40) != 0);
}

/* The chest on the wall before the leader: the location's chest the wall's
 * tile names, if it stands there, and its number among the game's. */
typedef struct {
    Wall wall;
    uint8_t index;
    bool there;
    uint16_t place, number;
    FieldChest chest;
} ChestBefore;

static ChestBefore chest_before_the_leader(const GameState *g)
{
    ChestBefore c = { .wall = wall_before_the_leader(g) };
    if (!c.wall.found)
        return c;
    c.index = c.wall.walkable & CHEST_INDEX_BITS;
    c.place = field_chest_place(field_location_chests(g), c.index);
    c.chest = field_chest_at(g, c.place);
    c.there = c.chest.tile == tile_number(c.wall.row, c.wall.column);
    c.number = chest_number(g, c.place);
    return c;
}

/* The inventory holds the items numbered below 256, as every chest of the
 * game's gives: a chest found elsewhere in the bank can name one past them,
 * which only the original gives (the guards'). */
enum { INVENTORY_ITEMS = 0x100 };

/* ... and with nobody leading, there is none: the message a chest opens
 * stands over the leader. */
bool chest_gift_fits(const GameState *g)
{
    ChestBefore c = chest_before_the_leader(g);
    uint16_t contents = c.chest.contents;
    if (c.there && field_leader(g).nobody_leads)
        return false;
    return !c.there || (contents & (HOLDS_GOLD | HOLDS_NOTHING_OF_ITS_OWN)) || (contents & ITEM_BITS) < INVENTORY_ITEMS;
}

// $C0:1DF4
ChestGift open_the_chest_before_the_leader(GameState *g)
{
    ChestGift nothing = { GIFT_NOTHING, 0 };
    if (dialog_on_screen(g))
        return nothing;
    ChestBefore c = chest_before_the_leader(g);
    ORIGINAL_EFFECT(keep_word(g, KEPT_SECOND, tile_number(c.wall.row, c.wall.column)));
    if (!c.wall.found)
        return nothing;
    ORIGINAL_EFFECT(leave_chest_lookup(c.index, field_location_chests(g)));
    if (!c.there)
        return nothing;
    uint16_t number = c.number;
    FieldChest chest = c.chest;
    ORIGINAL_EFFECT(leave_chest_found(g, c.place, number));
    if (chest_opened(g, number)) {
        field_message_opens(g, field_leader(g).obj, MESSAGE_EMPTY);
        return nothing;
    }
    mark_chest_opened(g, number);
    field_request(g, FIELD_REQUEST_OPEN_CHEST);
    set_field_opened_chest_tile(g, chest.tile);
    return what_the_chest_gives(g, chest.contents);
}

void announce_the_gift(GameState *g, ChestGift gift)
{
    field_message_opens(g, field_leader(g).obj, gift.kind == GIFT_ITEM ? MESSAGE_ITEM : MESSAGE_GOLD);
}

/* ---- The map's chests, marked ----------------------------------------------------------- */

/* For the original's code and shadow mode: where it was in its lookups, in
 * cells of the layers' page -- the last location whose run it looked up
 * (doubled), or else the last chest's flag bit and its number's high byte;
 * the last chest's place and how many it marked; and where the run ends. */
static void leave_marking(GameState *g, const uint16_t *location_twice, const uint16_t *last_place, uint8_t marked,
                          bool counted, uint16_t end)
{
    if (last_place) {
        uint16_t number = chest_number(g, *last_place);
        original_layer_cell8(g, 0x50, chest_flag_bit(g, number));
        original_layer_cell8(g, 0x51, (uint8_t)(number >> 8));
        original_layer_cell16(g, 0x52, *last_place);
    } else if (location_twice) {
        original_layer_cell16(g, 0x50, *location_twice);
    }
    if (counted)
        original_layer_cell16(g, 0x54, marked);
    original_layer_cell16(g, 0x56, end);
}

/* A location's run whose first chest stands on tile 0 is a pointer: what it
 * holds names the location whose run to take. A run of a run of pointers the
 * game's data never makes; the search gives up after a location's worth. */
enum { NO_TILE = 0, MOST_POINTERS = 0x200 };

/* An opened chest shows open: its BG1 tile the next one, where the tile
 * takes BG1's look from the looks' second half (the changing tiles'). */
enum { CHEST_AT_THE_SET_END_TILE = 0xFE, CHEST_FIRST_OF_THE_CHANGING_TILES_TILE = 0xE0 };

static void show_open(GameState *g, MapPlace p)
{
    if (!(field_tile_properties(g, p.row, p.column) & BG1_TILE_SET_HALF))
        return;
    uint8_t tile = field_layer_maps(g, FIELD_BG1, p.row, p.column);
    if (tile == CHEST_AT_THE_SET_END_TILE || tile == CHEST_FIRST_OF_THE_CHANGING_TILES_TILE)
        set_field_layer_maps(g, FIELD_BG1, p.row, p.column, (uint8_t)(tile + 1));
}

enum { WALKABLE_TABLE_ROWS = 64, WALKABLE_TABLE_COLUMNS = 64 };

// $C0:A712 (with $C0:A7BC, a chest's flag)
/* As a map's screen is set up, every tile is one the leader may walk onto
 * but the location's chests', which hold each chest's index; and each chest
 * opened before shows open. */
void chests_mark_the_map(GameState *g)
{
    for (unsigned row = 0; row < WALKABLE_TABLE_ROWS; row++)
        for (unsigned column = 0; column < WALKABLE_TABLE_COLUMNS; column++)
            set_field_tile_walkable(g, row, column, WALKABLE_BIT);
    set_field_games_chests(g, field_games_chests_start(g));
    uint16_t location_twice = (uint16_t)((location_id(g) & LOCATION_NUMBER_BITS) << 1), looked = 0;
    bool looked_any = false;
    FieldChestRun run = field_location_chest_run(g, location_twice);
    for (unsigned pointers = 0; run.first != run.end && pointers < MOST_POINTERS; pointers++) {
        set_field_location_chests(g, run.first);
        looked = location_twice;
        looked_any = true;
        FieldChest first = field_chest_at(g, run.first);
        if (first.tile != NO_TILE)
            break;
        location_twice = (uint16_t)(first.contents << 1);
        run = field_location_chest_run(g, location_twice);
    }
    if (run.first == run.end) {
        ORIGINAL_EFFECT(leave_marking(g, looked_any ? &looked : NULL, NULL, 0, false, run.end));
        return;
    }
    uint16_t mask = field_map_index_mask(g);
    uint8_t index = 0;
    uint16_t last = run.first;
    bool any = false;
    for (uint16_t at = run.first; at < run.end; at = (uint16_t)(at + FIELD_CHEST_BYTES)) {
        MapPlace p = map_place_at((uint16_t)(field_chest_at(g, at).tile & mask));
        set_field_tile_walkable(g, p.row, p.column, index);
        if (chest_opened(g, chest_number(g, at)))
            show_open(g, p);
        index++;
        last = at;
        any = true;
    }
    ORIGINAL_EFFECT(leave_marking(g, &looked, any ? &last : NULL, index, true, run.end));
}
