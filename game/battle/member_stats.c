/* Battle: the party members' stats with their equipment, as the battle sets
 * them up.
 *
 * A member's stat block holds its seven stats twice: as the member has them
 * (from STAT_BASE_STATS) and as it fights with them (from STAT_POWER). As the
 * battle starts, and again where a member's equipment changes in it, each is
 * set to the member's own, and then each piece the member wears -- its weapon,
 * armor and helmet -- adds what its record says: a boost to some of the stats,
 * up to each stat's most, and something to each of the four elements'
 * affinities ($C1:CE3A). The equipment's other records -- the weapon's attack,
 * the armor's and helmet's defence, the accessory's -- are copied into the
 * blocks whole ($C1:CF15), a piece at a time for the whole party.
 *
 * The records are bank CC's (battle/equipment_data.h). The entries are at the
 * end. */
#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/fight.h"
#include "battle/effect_parts.h"
#include "battle/entries.h"
#include "battle/equipment_data.h"
#include "battle/rules_data.h"
#include "original_effects.h"

/* The direct page's cells the setup works in. The stat block's place, the
 * record's and what the last boost or affinity added are cells later work
 * reads as it finds them (battle.h), kept plainly; the others are
 * scaffolding. */
enum {
    BLOCK_AT = BATTLE_CLOCK_READS, MEMBER_AT = 0x02, BITS_LEFT = 0x04, COPIED = 0x04, RECORD_AT = BATTLE_NUMBER_TESTED,
    ADDED = BATTLE_STAT_OFFSET_LOW, BOOST_AT = 0x0C, TABLE_AT = BATTLE_TECH_HITS_LEFT,
};

/* ---- The boosts ------------------------------------------------------------------------------ */

/* The stats a boost can raise, by its bits from the top, and each one's
 * most. */
enum { BYTE_MOST = 0xFF };

static const struct {
    uint8_t stat, most;
} boostable[] = {
    { STAT_POWER, 99 }, { STAT_SPEED_RATING, 16 }, { STAT_HIT, 99 }, { STAT_STAMINA, 99 },
    { STAT_EVADE, BYTE_MOST }, { STAT_MAGIC, 99 }, { STAT_MAGIC_DEFENCE, 99 },
};

enum { BOOSTABLE = sizeof boostable / sizeof boostable[0] };

/* A stat with `amount` more, at most `most`. The original takes the sum in a
 * byte, so one past 255 comes round below the cap (it is kept so), but for
 * the stat whose most is the byte's. */
static uint8_t raised(uint8_t stat, uint8_t amount, uint8_t most)
{
    if (most == BYTE_MOST)
        return stat + amount > BYTE_MOST ? BYTE_MOST : (uint8_t)(stat + amount);
    uint8_t sum = (uint8_t)(stat + amount);
    return sum < most ? sum : most;
}

// $FD:B14D
/* A boost added to the member's stats: each stat it names, raised by its
 * amount. */
void battle_apply_boost(GameState *g, uint8_t member, uint8_t which)
{
    StatBoost boost = stat_boost(g, which);
    battle_keep_byte(g, ADDED, boost.amount);
    for (unsigned i = 0; i < BOOSTABLE; i++) {
        if (boost.stats & (0x80 >> i)) {
            uint8_t stat = boostable[i].stat;
            set_combatant_stat_byte(g, member, stat, raised(combatant_stat_byte(g, member, stat), boost.amount, boostable[i].most));
        }
    }
    ORIGINAL_EFFECT(battle_leave_word(g, BOOST_AT, stat_boost_place(which)));
    ORIGINAL_EFFECT(battle_leave_byte(g, BITS_LEFT, (uint8_t)(boost.stats << (BOOSTABLE - 1))));
}

// $FD:B141
/* The boost a piece's record names, if any, added to the member's stats. */
static void apply_boost(GameState *g, uint8_t member, uint16_t record)
{
    uint8_t which = equipment_bank_byte(g, (uint16_t)(record + BOOST_BYTE));
    if (which != NO_BOOST)
        battle_apply_boost(g, member, which);
}

// $C1:CF69
/* What a piece's record adds to the member's elemental affinities: its low
 * four bits, to each of the four elements its high four name (the first from
 * the top). An affinity is a byte, and the sum comes round past 255. */
enum { ELEMENTS = 4 };

static void add_affinities(GameState *g, uint8_t member, uint16_t record)
{
    uint8_t adds = equipment_bank_byte(g, (uint16_t)(record + AFFINITY_BYTE));
    if (adds == 0)
        return;
    uint8_t amount = adds & 0x0F;
    battle_keep_byte(g, ADDED, amount);
    for (unsigned e = 0; e < ELEMENTS; e++) {
        if (adds & (0x80 >> e)) {
            uint16_t at = (uint16_t)(STAT_ELEMENT_AFFINITY + e);
            set_combatant_stat_byte(g, member, at, (uint8_t)(combatant_stat_byte(g, member, at) + amount));
        }
    }
    ORIGINAL_EFFECT(battle_leave_byte(g, BITS_LEFT, (uint8_t)(adds << (ELEMENTS - 1))));
}

/* ---- A member's stats ------------------------------------------------------------------------- */

/* The pieces whose second records boost the stats, in the order the setup
 * adds them: where the member's block names each, and the first of its
 * range. */
static const struct {
    EquipmentKind kind;
    uint8_t worn_at, first;
} worn_pieces[] = {
    { WEAPON_BOOSTS, STAT_WEAPON, 0 },
    { ARMOR_BOOSTS, STAT_ARMOR, FIRST_ARMOR },
    { HELMET_BOOSTS, STAT_HELMET, FIRST_HELMET },
};

// $C1:CE3A
/* The member's stats as it fights: its own, with what its weapon, armor and
 * helmet add. A member out of the fight has nothing set. */
enum { STATS = 7 };

static void set_up_member_stats(GameState *g, uint8_t member)
{
    battle_keep_word(g, ADDED, 0);
    if (!combatant_in_fight(g, member))
        return;
    battle_keep_word(g, BLOCK_AT, battle_stat_block_place(g, member));
    for (uint16_t i = 0; i < STATS; i++)
        set_combatant_stat_byte(g, member, (uint16_t)(STAT_POWER + i), combatant_stat_byte(g, member, (uint16_t)(STAT_BASE_STATS + i)));
    for (unsigned p = 0; p < sizeof worn_pieces / sizeof worn_pieces[0]; p++) {
        uint8_t piece = combatant_stat_byte(g, member, worn_pieces[p].worn_at);
        uint8_t number = (uint8_t)(piece - worn_pieces[p].first);
        uint16_t record = (uint16_t)(boost_records_place(worn_pieces[p].kind) + effect_times(g, number, BOOST_RECORD_BYTES));
        /* the original finds the armor's and the helmet's record with the
         * carry of its range's test in (scaffolding) */
        ORIGINAL_EFFECT(battle_leave_multiply_carry(g, worn_pieces[p].first != 0 && piece >= worn_pieces[p].first));
        /* the original hands the boost the record's place and the boosts'
         * table's in cells, and names another table before the affinities,
         * which don't read it: kept, as later work reads the cells */
        battle_keep_word(g, RECORD_AT, record);
        battle_keep_word(g, TABLE_AT, STAT_BOOSTS);
        apply_boost(g, member, record);
        battle_keep_word(g, TABLE_AT, STAT_SETUP_SPARE_TABLE);
        add_affinities(g, member, record);
    }
}

/* ---- The equipment's records ------------------------------------------------------------------ */

// $FD:B033
/* Where an item's record is in bank CC: the place of its number in its
 * range; an item past the recorded ones has none, and is given `none`. */
static uint16_t item_record_place(GameState *g, uint8_t item, uint16_t none)
{
    ItemRecords records = item_records_of(item);
    if (records.bytes == 0)
        return none;
    uint16_t place = (uint16_t)(records.place + effect_times(g, (uint8_t)(item - records.first), records.bytes));
    /* the original multiplies with the carry of its range's test in: set
     * past the weapons (scaffolding) */
    ORIGINAL_EFFECT(battle_leave_multiply_carry(g, item >= FIRST_ARMOR));
    return place;
}

// $C1:CF15
/* For each member of the party, `bytes` of the record of the item its block
 * names at `worn_at` copied to its block at `copy_to` (at least one). A
 * member whose item has no record copies from bank CC's start, at twice its
 * slot. */
static void copy_equipment_records(GameState *g, uint8_t worn_at, uint8_t copy_to, uint8_t bytes)
{
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++) {
        battle_keep_word(g, BLOCK_AT, battle_stat_block_place(g, member));
        uint16_t from = item_record_place(g, combatant_stat_byte(g, member, worn_at), (uint16_t)(member * 2));
        uint8_t n = 0;
        do {
            set_combatant_stat_byte(g, member, (uint16_t)(copy_to + n), equipment_bank_byte(g, (uint16_t)(from + n)));
            n++;
        } while (n < bytes);
    }
    ORIGINAL_EFFECT(battle_leave_word(g, MEMBER_AT, BATTLE_PARTY_SIZE));
    ORIGINAL_EFFECT(battle_leave_word(g, COPIED, 0));
}

/* As the fight's start calls them (battle/fight.h). */
void battle_set_up_member_stats_of(GameState *g, uint8_t member) { set_up_member_stats(g, member); }

void battle_copy_equipment_records_for(GameState *g, uint8_t worn_at, uint8_t copy_to, uint8_t bytes)
{
    copy_equipment_records(g, worn_at, copy_to, bytes);
}

/* ---- The entries ------------------------------------------------------------------------------ */

/* The stat block whose place is in the cell `at`: the combatant's slot, or
 * none where the place is no block's. */
static bool block_in_cell(const GameState *g, unsigned at, uint8_t *slot)
{
    uint16_t offset = (uint16_t)(battle_cell_word(g, at) - battle_stat_block_place(g, 0));
    *slot = (uint8_t)(offset / BATTLE_STAT_BLOCK_BYTES);
    return offset % BATTLE_STAT_BLOCK_BYTES == 0 && offset / BATTLE_STAT_BLOCK_BYTES < BATTLE_PARTY_SIZE;
}

/* The member in its cell ($02), a member of the party's. */
bool battle_set_up_member_stats_ready(Snes *s)
{
    vars_peek(true);
    bool member = battle_cell_word(state_of(s), MEMBER_AT) < BATTLE_PARTY_SIZE;
    vars_peek(false);
    return battle_on_page(s) && member;
}

void battle_set_up_member_stats(Snes *s)
{
    set_up_member_stats(state_of(s), (uint8_t)battle_cell_word(state_of(s), MEMBER_AT));
    m_rts(s);
}

/* A piece's boost and affinities, for the member whose block's place is in
 * its cell ($00), from the record whose place is in its ($08); the boosts'
 * table's place in its cell ($0E). The record is read within the bank. */
enum { RECORD_LAST_PLACE = 0xFFFA };

static bool piece_ready(Snes *s)
{
    uint8_t slot;
    vars_peek(true);
    GameState *g = state_of(s);
    bool ready = block_in_cell(g, BLOCK_AT, &slot) && battle_cell_word(g, RECORD_AT) <= RECORD_LAST_PLACE;
    vars_peek(false);
    return battle_on_page(s) && ready;
}

bool battle_apply_stat_boost_ready(Snes *s)
{
    vars_peek(true);
    bool table = battle_cell_word(state_of(s), TABLE_AT) == STAT_BOOSTS;
    vars_peek(false);
    return piece_ready(s) && table;
}

void battle_apply_stat_boost(Snes *s)
{
    uint8_t member;
    GameState *g = state_of(s);
    block_in_cell(g, BLOCK_AT, &member);
    apply_boost(g, member, battle_cell_word(g, RECORD_AT));
    m_rtl(s);
}

bool battle_add_affinities_ready(Snes *s) { return piece_ready(s); }

void battle_add_affinities(Snes *s)
{
    uint8_t member;
    GameState *g = state_of(s);
    block_in_cell(g, BLOCK_AT, &member);
    add_affinities(g, member, battle_cell_word(g, RECORD_AT));
    m_rts(s);
}

/* An item's record's place: the item in A, the place in X (A clear where it
 * has one; where not, X and A are left as they came). */
bool battle_item_record_place_ready(Snes *s) { return battle_on_page_a_high_clear(s); }

void battle_item_record_place(Snes *s)
{
    uint8_t item = (uint8_t)s->cpu.a;
    uint16_t place = item_record_place(state_of(s), item, s->cpu.x);
    if (item_records_of(item).bytes != 0) {
        s->cpu.x = place;
        s->cpu.a = 0;
    }
    m_rtl(s);
}

/* The records to copy: where the item is in a block, in its cell ($06); where
 * they go, in its ($08); how many bytes, in its ($0A). */
enum { WORN_AT = BATTLE_MEMBER_CELL, COPY_TO = BATTLE_NUMBER_TESTED, BYTES = BATTLE_STAT_OFFSET_LOW, BLOCK_OFFSETS = 0x100 };

bool battle_copy_equipment_records_ready(Snes *s)
{
    vars_peek(true);
    GameState *g = state_of(s);
    bool offsets = battle_cell_word(g, WORN_AT) < BLOCK_OFFSETS && battle_cell_word(g, COPY_TO) < BLOCK_OFFSETS;
    vars_peek(false);
    return battle_on_page(s) && offsets;
}

void battle_copy_equipment_records(Snes *s)
{
    GameState *g = state_of(s);
    copy_equipment_records(g, battle_low_scratch(g, WORN_AT), battle_low_scratch(g, COPY_TO), battle_low_scratch(g, BYTES));
    m_rts(s);
}
