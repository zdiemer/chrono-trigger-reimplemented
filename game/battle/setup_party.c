/* Battle: the party as a battle starts.
 *
 * The three party slots name the characters in the fight (party_members; an
 * empty slot has its top bit set). The battle takes from them what its menus
 * and its window need: which slots are filled and where Frog and Magus are,
 * whose techs need them; each member's name (battle/setup_names.c converts
 * it); the member's colors; its tech list, built from its tech records --
 * its single techs, then its double techs after a $FC marker, then a triple
 * tech after a $FD marker, the list ending at the first blank row -- and,
 * when the player's settings ask for it, where its menus' cursors were left
 * in the last battle. Then the members take their places on the battlefield
 * as their objects: position, sprites, first frame, and the graphics and
 * animation lists their character draws with. */
#include "battle/battle.h"
#include "battle/setup.h"

#include "battle/setup_data.h"
#include "original_effects.h"
#include "vars.h"

/* ---- Who is in the party -------------------------------------------------------------------- */

enum { EMPTY_SLOT = 0x80, CHARACTER_BITS = 0x07, FROG = 4, MAGUS = 6 };
enum { NAME_CHARACTERS = 5, NAME_RECORD_BYTES = 6, MEMBER_CARRIED_BYTES = 5 };

static bool slot_filled(uint8_t member) { return !(member & EMPTY_SLOT); }

/* Whether the party is one the game makes: every slot empty or one of the
 * seven characters. */
bool setup_party_is_real(const GameState *g)
{
    for (unsigned slot = 0; slot < BATTLE_PARTY_SIZE; slot++) {
        uint8_t member = party_members(g, slot);
        if (slot_filled(member) && member >= SETUP_CHARACTERS)
            return false;
    }
    return true;
}

/* The character's name as the characters' records hold it, from a byte offset
 * into them. */
static uint8_t character_name_char(const GameState *g, unsigned at)
{
    return character_names_byte(g, at / NAME_RECORD_BYTES, at % NAME_RECORD_BYTES);
}

/* Frog's slot is kept times five, as an index into the members' carried
 * bytes; so is whatever the cell held when Frog isn't in the party. */
static void keep_frogs_slot_as_record(GameState *g)
{
    uint16_t slot = battle_frog_slot(g);
    set_battle_frog_slot(g, (uint16_t)((slot & 0xFF00) | (uint8_t)(slot * MEMBER_CARRIED_BYTES)));
}

/* The original steps through the members' names five bytes at a time in its
 * working cells, which the names' conversion finds them at. */
static void leave_slots_read(GameState *g) { setup_leave_word(g, 0x82, BATTLE_PARTY_SIZE * NAME_CHARACTERS); }

// $CC:E5F3
void setup_party_slots(GameState *g)
{
    for (unsigned slot = 0; slot < BATTLE_PARTY_SIZE; slot++) {
        uint8_t member = party_members(g, slot);
        if (!slot_filled(member))
            continue;
        uint8_t character = member & CHARACTER_BITS;
        if (character == FROG) {
            set_battle_frog_slot(g, (uint16_t)slot);
        } else if (character == MAGUS) {
            set_battle_magus_slot(g, (uint16_t)slot);
            set_battle_magus_present(g, (uint8_t)(battle_magus_present(g) + 1));
        }
        uint8_t name = setup_character_name_at(g, character);
        for (unsigned i = 0; i < NAME_CHARACTERS; i++)
            set_battle_party_names_byte(g, slot, i, character_name_char(g, name + i));
        set_battle_slot_present(g, slot, (uint8_t)(battle_slot_present(g, slot) + 1));
    }
    keep_frogs_slot_as_record(g);
    ORIGINAL_EFFECT(leave_slots_read(g));
}

/* ---- Colors --------------------------------------------------------------------------------- */

/* Each member's sprite palette takes three colors of its color set, which the
 * byte the battle carries for it picks: colors 13-15 of palettes 5-7. */
enum { SET_COLORS = 3, SET_BYTES = 6, FIRST_MEMBER_COLOR = 221, PALETTE_SIZE = 16 };

// $CC:E710
void setup_party_colors(GameState *g)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        uint16_t set = setup_member_color_set(g, battle_member_carried_weapon(g, member));
        for (unsigned color = 0; color < SET_COLORS; color++)
            set_palette_working(g, FIRST_MEMBER_COLOR + member * PALETTE_SIZE + color,
                                setup_member_color(g, (uint16_t)(set * SET_BYTES + 2 * color)));
    }
}

/* ---- Tech lists ----------------------------------------------------------------------------- */

/* A member's tech records: twenty seven-byte records from where its records
 * start, the single techs at 0-7, the double techs at 10-15 and a triple tech
 * at 18. A record whose id has its top bit set is none. */
enum { TECH_RECORD_BYTES = 7, SINGLES = 0, DOUBLES = 10, TRIPLE = 18, SINGLES_END = 8, DOUBLES_END = 16 };
enum { NO_TECH = 0x80, BLANK_ROW = 0xFF, DOUBLES_MARKER = 0xFC, TRIPLES_MARKER = 0xFD, LONGEST_LIST = 20 };

static unsigned tech_record(const GameState *g, uint8_t member, uint8_t entry)
{
    return setup_member_tech_records_at(g, member) / TECH_RECORD_BYTES + entry;
}

static uint8_t tech_id(const GameState *g, uint8_t member, uint8_t entry)
{
    return battle_tech_records_id(g, tech_record(g, member, entry));
}

static bool has_tech(const GameState *g, uint8_t member, uint8_t entry) { return !(tech_id(g, member, entry) & NO_TECH); }

/* Note which members have a double tech and which a triple tech to show. */
static void mark_combination_techs(GameState *g)
{
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++) {
        if (has_tech(g, member, DOUBLES))
            set_battle_double_techs(g, member, 1);
        if (has_tech(g, member, TRIPLE))
            set_battle_triple_techs(g, member, 1);
    }
}

/* Add a member's techs from `first` up to `end` to its list at `place`, each
 * with its entry number: where the list goes on. */
static uint8_t list_techs(GameState *g, uint8_t member, uint8_t place, uint8_t first, uint8_t end)
{
    for (uint8_t entry = first; entry < end; entry++) {
        uint8_t id = tech_id(g, member, entry);
        if (id == BLANK_ROW)
            continue;
        set_battle_tech_list_ids(g, place, id);
        set_battle_tech_list(g, place, entry);
        place++;
    }
    return place;
}

static uint8_t add_marker(GameState *g, uint8_t place, uint8_t marker)
{
    set_battle_tech_list_ids(g, place, marker);
    return (uint8_t)(place + 1);
}

/* A member's triple tech takes the row after its marker; it is listed with
 * its entry number only when it is a tech. */
static void list_triple(GameState *g, uint8_t member, uint8_t place)
{
    uint8_t id = tech_id(g, member, TRIPLE);
    set_battle_tech_list_ids(g, place, id);
    if (!(id & NO_TECH))
        set_battle_tech_list(g, place, TRIPLE);
}

static uint8_t list_length(const GameState *g, uint8_t member)
{
    uint8_t place = setup_member_tech_list_start(g, member), length = 0;
    while (battle_tech_list_ids(g, place) != BLANK_ROW) {
        place++;
        if (++length == LONGEST_LIST)
            break;
    }
    return length;
}

/* The original keeps the last member's list end in its working cells, and
 * how many double techs it listed. */
static uint8_t doubles_listed(const GameState *g, uint8_t member)
{
    uint8_t listed = 0;
    for (uint8_t entry = DOUBLES; entry < DOUBLES_END; entry++)
        listed += tech_id(g, member, entry) != BLANK_ROW;
    return listed;
}

static void leave_lists_built(GameState *g, uint8_t last_end)
{
    setup_leave_word(g, 0x8A, last_end);
    setup_leave(g, 0x8C, doubles_listed(g, BATTLE_PARTY_SIZE - 1));
}

// $CC:E755
void setup_tech_lists(GameState *g)
{
    mark_combination_techs(g);
    uint8_t ends[BATTLE_PARTY_SIZE];
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++) {
        uint8_t place = list_techs(g, member, setup_member_tech_list_start(g, member), SINGLES, SINGLES_END);
        ends[member] = battle_double_techs(g, member) ? add_marker(g, place, DOUBLES_MARKER) : place;
    }
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++) {
        uint8_t place = list_techs(g, member, ends[member], DOUBLES, DOUBLES_END);
        ends[member] = battle_triple_techs(g, member) ? add_marker(g, place, TRIPLES_MARKER) : place;
    }
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        if (battle_triple_techs(g, member))
            list_triple(g, member, ends[member]);
    for (uint8_t member = BATTLE_PARTY_SIZE; member-- > 0;)
        set_battle_tech_list_length(g, member, list_length(g, member));
    ORIGINAL_EFFECT(leave_lists_built(g, ends[BATTLE_PARTY_SIZE - 1]));
}

/* ---- Where the cursors were left ------------------------------------------------------------ */

/* Whether the cursors a member's tech list remembers (battle_remembered_cursors)
 * point within the longest list, as the game leaves them. */
static bool remembered_place_in_list(const GameState *g, uint8_t character)
{
    return battle_remembered_cursors_tech_scroll(g, character) + battle_remembered_cursors_tech_row(g, character)
           < LONGEST_LIST;
}

bool setup_remembered_cursors_real(const GameState *g)
{
    if (!(game_settings(g, SETTINGS_MEMORY) & REMEMBER_LISTS))
        return true;
    for (unsigned slot = 0; slot < BATTLE_PARTY_SIZE; slot++) {
        uint8_t member = party_members(g, slot);
        if (slot_filled(member) && !remembered_place_in_list(g, member))
            return false;
    }
    return true;
}

/* With the settings on, each member's command cursor, and its tech list's
 * scroll, row and the tech under it, as the character left them. A tech list
 * that no longer has that tech there starts at its top. */
static void remember_tech_cursor(GameState *g, uint8_t slot, uint8_t character)
{
    uint8_t scroll = battle_remembered_cursors_tech_scroll(g, character);
    uint8_t row = battle_remembered_cursors_tech_row(g, character);
    uint8_t tech = battle_remembered_cursors_tech(g, character);
    set_battle_remembered_tech(g, slot, tech);
    set_battle_remembered_tech_row(g, slot, row);
    set_battle_remembered_tech_scroll(g, slot, scroll);
    if (battle_tech_list_ids(g, (uint8_t)(scroll + row + setup_member_tech_list_start(g, slot))) != tech) {
        set_battle_remembered_tech_row(g, slot, 0);
        set_battle_remembered_tech_scroll(g, slot, 0);
    }
}

// $CC:E8D2
void setup_remembered_cursors(GameState *g)
{
    for (uint8_t slot = BATTLE_PARTY_SIZE; slot-- > 0;) {
        uint8_t character = party_members(g, slot);
        if (!slot_filled(character))
            continue;
        if (battle_remembers_commands(g))
            set_battle_remembered_command(g, slot, battle_remembered_cursors_command(g, character));
        if (battle_remembers_lists(g))
            remember_tech_cursor(g, slot, character);
    }
    if (battle_remembers_lists(g)) {
        set_battle_item_top(g, battle_remembered_item_top(g));
        set_battle_item_row(g, battle_remembered_item_row(g));
    }
}

/* ---- The members on the battlefield --------------------------------------------------------- */

/* A party member is drawn with fifteen sprites. */
enum { MEMBER_SPRITES = 0x0F, TILE_PIXELS = 16 };

/* Where the combatant starts, in pixels, from its tile and its place in it. */
static uint8_t start_x(const GameState *g, unsigned slot)
{
    return (uint8_t)(battle_start_positions_tile_x(g, slot) * TILE_PIXELS + battle_start_positions_x_offset(g, slot));
}
static uint8_t start_y(const GameState *g, unsigned slot)
{
    return (uint8_t)(battle_start_positions_tile_y(g, slot) * TILE_PIXELS + battle_start_positions_y_offset(g, slot));
}

/* The frame each combatant's object starts with is kept a byte on from its
 * marker's frame, in the same bytes: the original's two tables overlap by
 * one, and the start frames are read (and the tenth enemy's is the byte past
 * the markers') when the battle opens, before any marker is drawn. */
static void set_start_frame(GameState *g, unsigned slot, uint8_t frame)
{
    if (slot + 1 < BATTLE_COMBATANTS)
        set_battle_marker_frame(g, slot + 1, frame);
    else
        set_battle_marker_frame_unchecked(g, slot + 1, frame);
}

void setup_place_combatant(GameState *g, unsigned slot)
{
    set_battle_object_x(g, battle_object_of(slot), start_x(g, slot));
    set_battle_object_y(g, battle_object_of(slot), start_y(g, slot));
    set_start_frame(g, slot, battle_start_positions_frame(g, slot));
    set_battle_object_facing(g, slot, 0);
}

/* A character draws with its own graphics set and tile lists (three bytes a
 * character) and its animation set (a word a character). How far apart its
 * facings are in its frame lists is its frame lists' length, a quarter of the
 * way to the next set's. */
enum { POINTER_BYTES = 3, FACINGS = 4 };

static void load_member_graphics(GameState *g, unsigned member)
{
    uint8_t character = party_members(g, member);
    uint8_t set = (uint8_t)((uint8_t)(character << 1) + character);
    set_battle_object_graphics(g, member, setup_character_graphics(g, set));
    set_battle_object_tile_lists(g, member, setup_tile_lists(g, set));
    uint8_t animation = (uint8_t)(character << 1), next = (uint8_t)(((animation >> 1) + 1) << 1);
    uint16_t frames = setup_frame_lists(g, animation);
    set_battle_object_frame_lists(g, member, (uint32_t)SETUP_ANIMATION_BANK << 16 | frames);
    set_battle_object_delay_lists(g, member, (uint32_t)SETUP_ANIMATION_BANK << 16 | setup_delay_lists(g, animation));
    uint16_t stride = (uint16_t)(setup_frame_lists(g, next) - frames) / FACINGS;
    set_battle_facing_stride_low(g, member, (uint8_t)stride);
    set_battle_facing_stride_high(g, member, (uint8_t)(stride >> 8));
}

// $CC:E933
void setup_party_objects(GameState *g)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        set_battle_start_positions_tile_x(g, member, battle_party_placement_tile_x(g, member));
        set_battle_start_positions_tile_y(g, member, battle_party_placement_tile_y(g, member));
        set_battle_start_positions_x_offset(g, member, battle_party_placement_x_offset(g, member));
        set_battle_start_positions_y_offset(g, member, battle_party_placement_y_offset(g, member));
        set_battle_start_positions_frame(g, member, battle_party_placement_frame(g, member));
    }
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        setup_place_combatant(g, member);
        set_battle_object_part_count(g, battle_object_of(member), MEMBER_SPRITES);
    }
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        load_member_graphics(g, member);
}
