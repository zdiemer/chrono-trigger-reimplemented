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
#include "charge.h"
#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/party_records.h"
#include "battle/effect_parts.h"
#include "battle/entries.h"
#include "battle/equipment_data.h"
#include "battle/rules_data.h"

/* The work cells the setup works in. The stat block's place, the
 * record's and what the last boost or affinity added are cells later work
 * reads as it finds them (battle.h); the others keep the original's
 * values. */
enum {
    BLOCK_AT = BATTLE_CLOCK_READS,
    MEMBER_AT = 0x02,
    BITS_LEFT = 0x04,
    COPIED = 0x04,
    RECORD_AT = BATTLE_NUMBER_TESTED,
    ADDED = BATTLE_STAT_OFFSET_LOW,
    TABLE_AT = BATTLE_TECH_HITS_LEFT,
};

/* ---- The boosts ------------------------------------------------------------------------------ */

/* The stats a boost can raise, by its bits from the top, and each one's
 * most. */
enum { BYTE_MOST = 0xFF };

static const struct {
    uint8_t stat, most;
} boostable[] = {
    { STAT_POWER, 99 }, { STAT_SPEED_RATING, 16 }, { STAT_STAMINA, 99 }, { STAT_HIT, 99 },
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
}

/* As the fight's start calls them (battle/fight.h). */
void battle_equip_member(GameState *g, uint8_t member) { set_up_member_stats(g, member); }

void battle_take_equipment_records(GameState *g, uint8_t worn_at, uint8_t copy_to, uint8_t bytes)
{
    copy_equipment_records(g, worn_at, copy_to, bytes);
}

/* ---- The equipment records copied -------------------------------------------------------------- */

/* The records to copy: where the item is in a block, in its cell ($06); where
 * they go, in its ($08); how many bytes, in its ($0A). */
enum {
    WORN_AT = BATTLE_MEMBER_CELL,
    COPY_TO = BATTLE_NUMBER_TESTED,
    BYTES = BATTLE_STAT_OFFSET_LOW,
    BLOCK_OFFSETS = 0x100,
};
