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
#include "field/scratch.h"

/* A tile's byte in the walkable table. */
enum { WALKABLE_BIT = 0x80, CHEST_INDEX_BITS = 0x7F };

/* Where field_chests says the game's list starts, and the location's run. */
enum { THE_GAMES_CHESTS = 0, THE_LOCATIONS_CHESTS = 1 };

/* What a chest holds, by its contents word: gold (halved, in the bits below
 * this one), nothing of its own, or else the item in the low nine bits. */
enum { HOLDS_GOLD = 0x8000, HOLDS_NOTHING_OF_ITS_OWN = 0x4000, ITEM_BITS = 0x01FF };

/* The field's messages for a chest, by their line. */
enum { MESSAGE_ITEM = 0, MESSAGE_GOLD = 1, MESSAGE_EMPTY = 2 };

/* The variable the item goes in, which its message names. */
enum { THE_ITEM_FOUND = 0 };

/* A tile as the chests keep it: its row in the high byte. */
static uint16_t tile_number(uint8_t row, uint8_t column) { return (uint16_t)(row << 8 | column); }

static FieldObject the_leader(const GameState *g) { return field_linked_object(party_member_link(g, PARTY_LEADER)); }

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

static Wall look_at(const GameState *g, uint8_t row, uint8_t column)
{
    uint8_t walkable = field_tile_walkable(g, row, column);
    return (Wall){ !(walkable & WALKABLE_BIT), row, column, walkable };
}

// $C0:1E9A-$C0:1F0D
static Wall wall_before_the_leader(const GameState *g)
{
    FieldObject leader = the_leader(g);
    MapTile at = object_tile(g, leader);
    Facing facing = object_facing(g, leader);
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
    FieldObject leader = the_leader(g);
    MapTile at = object_tile(g, leader);
    Facing facing = object_facing(g, leader);
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

// $C0:1DF4
ChestGift open_the_chest_before_the_leader(GameState *g)
{
    ChestGift nothing = { GIFT_NOTHING, 0 };
    if (dialog_on_screen(g))
        return nothing;
    Wall w = wall_before_the_leader(g);
    ORIGINAL_EFFECT(keep_word(g, KEPT_SECOND, tile_number(w.row, w.column)));
    if (!w.found)
        return nothing;
    uint8_t index = w.walkable & CHEST_INDEX_BITS;
    uint16_t place = field_chest_place(field_chests(g, THE_LOCATIONS_CHESTS), index);
    ORIGINAL_EFFECT(leave_chest_lookup(index, field_chests(g, THE_LOCATIONS_CHESTS)));
    FieldChest chest = field_chest_at(g, place);
    if (chest.tile != tile_number(w.row, w.column))
        return nothing;
    uint16_t number = (uint16_t)((uint16_t)(place - field_chests(g, THE_GAMES_CHESTS)) / FIELD_CHEST_BYTES);
    ORIGINAL_EFFECT(leave_chest_found(g, place, number));
    if (chest_opened(g, number)) {
        field_message_opens(g, the_leader(g), MESSAGE_EMPTY);
        return nothing;
    }
    mark_chest_opened(g, number);
    field_request(g, FIELD_REQUEST_OPEN_CHEST);
    set_field_opened_chest_tile(g, chest.tile);
    return what_the_chest_gives(g, chest.contents);
}

void announce_the_gift(GameState *g, ChestGift gift)
{
    field_message_opens(g, the_leader(g), gift.kind == GIFT_ITEM ? MESSAGE_ITEM : MESSAGE_GOLD);
}
