/* Battle: the party's techs in its members' tech records, as the fight's
 * start sets them up (battle/fight_start.c).
 *
 * Each member has twenty tech records (battle_tech_records, battle/
 * setup_party.c lists them): its character's eight single techs from record
 * 0, the double techs it can do with the others in the fight from record 10,
 * and a triple tech in record 18. Every record starts blank ($FF) and a tech
 * the party hasn't learned leaves its record so. A tech's record says, beside
 * the tech, what each of the party's three slots pays for it in MP -- its own
 * part's cost, cut by that member's accessory -- and in its partners byte the
 * slots of the others who perform it with the member.
 *
 * The single techs are the character's, learned one by one ($C1:C96A). The
 * double techs are the first six learned whose two members are both in the
 * fight and one of them the member ($C1:CA1A), and the triple tech the first
 * learned one whose three members are all in it ($C1:CCCB). The entries are
 * at the end. */
#include "charge.h"
#include "battle/effect_parts.h"

#include "battle/arith.h"
#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/entries.h"
#include "battle/party_records.h"
#include "battle/rules_data.h"
#include "battle/setup.h"
#include "battle/setup_data.h"

/* The records' places: a member's records from where its start (setup_data.h),
 * seven bytes each, the first double tech's at 10 and the triple tech's at
 * 18. */
enum { TECH_RECORD_BYTES = 7, FIRST_DOUBLE_RECORD = 10, TRIPLE_RECORD = 18, NO_COST = 0xFF, QUARTERS_COST = 0xA3 };

/* The triple techs, and the byte of each that says the party has learned it
 * (its bit 7). */
enum { FIRST_TRIPLE_TECH = 0x66, TRIPLE_TECHS_END = 0x75, LEARNED = 0x80 };

static unsigned member_record(const GameState *g, uint8_t member, uint8_t entry)
{
    return setup_member_tech_records_at(g, member) / TECH_RECORD_BYTES + entry;
}

/* ---- A combination tech's costs --------------------------------------------------------------- */

/* The party's accessories that cut what a part costs in MP (party_work.c's
 * rule): the original asks the one of the party's slot a member is in, and
 * past the second slot the third's. */
static uint8_t slot_paying(uint8_t slot) { return slot < BATTLE_PARTY_SIZE - 1 ? slot : BATTLE_PARTY_SIZE - 1; }

/* The carry the accessory's test leaves ($C1:CBF6): set for an accessory
 * past the one that quarters the cost. */

// $C1:CC0B, $C1:CC3E
/* A combination tech's record takes what the member of the character
 * `character` pays for its part, `cost` cut by the member's accessory, in the
 * field of the member's slot. */
static uint8_t set_slot_cost(GameState *g, unsigned record, uint8_t character, uint8_t cost)
{
    uint8_t slot = slot_paying(battle_character_slot(g, character));
    uint8_t cut = effect_mp_cost_cut(cost, battle_member_accessory(g, slot));
    if (slot == 0)
        set_battle_tech_records_cost(g, record, cut);
    else if (slot == 1)
        set_battle_tech_records_second_cost(g, record, cut);
    else
        set_battle_tech_records_third_cost(g, record, cut);
    return cut;
}

/* ---- The triple tech ---------------------------------------------------------------------------- */

/* The direct page's cells the search works in, which the rest of the fight's
 * start overwrites before anything reads them (scaffolding). */
enum {
    LOOKED_PAST = BATTLE_CLOCK_READS, TECH_TWICE = 0x02, RECORDS_AT = 0x04, TECH_AT = BATTLE_NUMBER_TESTED,
    LEARNED_READ = 0x0C, TECHS_END = BATTLE_TECH_HITS_LEFT, PER_BYTE = 0x10, SPARE = 0x14,
};

/* Whether all three of a tech's members are in the battle: each a character
 * in a party slot. */
static bool members_in_battle(const GameState *g)
{
    for (unsigned part = 0; part < TECH_MEMBERS; part++) {
        uint8_t character = battle_tech_members(g, part);
        if (character == NO_MEMBER || (battle_character_slot(g, character) & BATTLE_EMPTY_SLOT))
            return false;
    }
    return true;
}

/* A partner's slot, as the partners byte keeps it: the slot of the member of
 * that part, or the first member's where that is the member itself. */
static uint8_t partner_slot(const GameState *g, unsigned part, uint8_t member)
{
    uint8_t slot = battle_character_slot(g, battle_tech_members(g, part));
    return slot == member ? battle_character_slot(g, battle_tech_members(g, 0)) : slot;
}

/* A record given a tech: its menu bytes and the tech, and no costs yet. */
static void start_record(GameState *g, unsigned record, uint8_t tech)
{
    TechMenuBytes menu = tech_menu_bytes(g, tech);
    set_battle_tech_records_flags(g, record, menu.flags);
    set_battle_tech_records_menu_detail(g, record, menu.detail);
    set_battle_tech_records_id(g, record, tech);
    set_battle_tech_records_cost(g, record, NO_COST);
    set_battle_tech_records_second_cost(g, record, NO_COST);
    set_battle_tech_records_third_cost(g, record, NO_COST);
}

/* The member's triple tech record filled with the tech: its menu bytes, what
 * each slot pays, and its partners. */
static void fill_triple(GameState *g, uint8_t member, uint8_t tech)
{
    unsigned record = member_record(g, member, TRIPLE_RECORD);
    start_record(g, record, tech);
    PartyTech t = party_tech(g, tech);
    for (unsigned part = 0; part < TECH_MEMBERS; part++)
        set_slot_cost(g, record, battle_tech_members(g, part), party_effect_mp_cost(g, t.effects[part]));
    /* the original finds each part's cost by a multiply (the tech's record's
     * place), the last entered with the carry the second member's
     * accessory's test left (scaffolding) */
    battle_leave_multiply(g, tech, PARTY_TECH_BYTES);
    charge_machine_accesses(g, 4);
    uint8_t high = (uint8_t)(partner_slot(g, 2, member) << 4);
    /* the original works the partners out in the search's first cell */
    charge_machine_accesses(g, 1);
    set_battle_tech_records_partners(g, record, (uint8_t)(high | partner_slot(g, 1, member)));
}

static uint8_t learned_byte(const GameState *g, unsigned triple)
{
    return triple < PARTY_TRIPLE_TECHS_LEARNED_COUNT ? party_triple_techs_learned(g, triple)
                                                     : party_triple_techs_learned_unchecked(g, triple);
}

/* What the search leaves in the direct page's cells: the tech it stopped at,
 * where the triples end and how many it looked past, twice the tech, the
 * learned byte it read last, its count and where the member's records are. A
 * member out of the fight leaves them clear. */

// $C1:CCCB
/* The member's triple tech: the first learned one all of whose members are in
 * the battle (the tech's members are left as the last learned one looked
 * at). A member out of the fight has none set. */
static void set_up_triple_tech(GameState *g, uint8_t member)
{
    if (!combatant_in_fight(g, member)) {
        charge_machine_accesses(g, 10);
        return;
    }
    for (uint8_t tech = FIRST_TRIPLE_TECH; tech < TRIPLE_TECHS_END; tech++) {
        uint8_t triple = (uint8_t)(tech - FIRST_TRIPLE_TECH);
        if (!(learned_byte(g, triple) & LEARNED))
            continue;
        effect_take_tech_members(g, tech);
        if (members_in_battle(g)) {
            (charge_machine_accesses(g, 16), charge_machine_rom_accesses(g, 2));
            fill_triple(g, member, tech);
            return;
        }
    }
    (charge_machine_accesses(g, 16), charge_machine_rom_accesses(g, 2));
}

void battle_find_triple_tech(GameState *g, uint8_t member) { set_up_triple_tech(g, member); }

/* The double techs, three to each byte of what the party has learned. */
enum { FIRST_DOUBLE_TECH = 0x39, DOUBLE_TECHS_END = 0x66, DOUBLES_A_BYTE = 3, MOST_DOUBLES = 6 };

static uint8_t doubles_learned_byte(const GameState *g, unsigned n)
{
    return n < PARTY_DOUBLE_TECHS_LEARNED_COUNT ? party_double_techs_learned(g, n) : party_double_techs_learned_unchecked(g, n);
}

/* ---- What the builders leave in the direct page's cells ---------------------------------------- */

/* The original builds the records in the direct page's cells, which the rest
 * of the fight's start overwrites before anything reads them: scaffolding,
 * worked out here from where each builder ended. The cells: the character or
 * the learned byte's number, twice the tech, the records' place, the tech,
 * the learned byte it was at, where the techs end, the bit in the byte, the
 * doubles filled, and a word the double techs' builder steps by twelve a tech
 * without starting it. */
enum { COUNTED_AT = 0x00, TECH_TWICE_AT = 0x02, RECORD_PLACE_AT = 0x04, CURRENT_TECH = 0x08, STEPPED = 0x0A,
       LEARNED_AT = 0x0C, TECHS_END_AT = 0x0E, BIT_IN_BYTE = 0x10, DOUBLES_FILLED = 0x14, STEP_BYTES = 12 };

/* The carry the search for a tech's members ($C1:CB93) leaves, its members'
 * number times three past a byte, which the next multiply is entered with. */

/* The second part's multiply is entered with the carry of the first member's
 * accessory's test. */

/* The double techs' builder stops at the tech it filled the sixth record
 * with, or past the last; the place it steps is the member's records', the
 * tenth's offset in its stores rather than in the cell, and it doesn't step
 * past the sixth. */

/* ---- The single and double techs ---------------------------------------------------------------- */

/* A part's cost: what its effect costs the member in MP. The original finds
 * the tech's record by a multiply, whose cells it leaves as they are. */
static uint8_t part_cost(GameState *g, uint8_t tech, unsigned part)
{
    battle_leave_multiply(g, tech, PARTY_TECH_BYTES);
    return party_effect_mp_cost(g, party_tech(g, tech).effects[part]);
}

enum { LEARNED_FIRST = 0x80, NO_PARTNER_BYTE = 0xFF };

// $C1:C96A
/* The member's single techs: its character's eight in its first records, in
 * order, each the character has learned given its cost to the member's slot;
 * no partners. A member out of the fight has none. */
void battle_set_up_single_techs(GameState *g, uint8_t member)
{
    if (!combatant_in_fight(g, member)) {
        charge_machine_accesses(g, 6);
        return;
    }
    uint8_t character = battle_combatant_fighting(g, member);
    uint8_t first = character_first_single_tech(g, character), end = character_first_single_tech(g, (uint8_t)(character + 1));
    uint8_t learned = party_single_techs_learned(g, character);
    unsigned record = member_record(g, member, 0);
    uint8_t tech = first;
    do {
        if (learned & LEARNED_FIRST) {
            effect_take_tech_members(g, tech);
            start_record(g, record, tech);
            set_slot_cost(g, record, battle_tech_members(g, 0), part_cost(g, tech, 0));
            (charge_machine_accesses(g, 1), charge_machine_rom_accesses(g, 4 + 2 * PARTY_TECH_PARTS));
            set_battle_tech_records_partners(g, record, NO_PARTNER_BYTE);
        }
        learned = (uint8_t)(learned << 1);
        record++;
        tech++;
    } while (tech < end);
    (charge_machine_accesses(g, 12), charge_machine_rom_accesses(g, 2));
}

static bool learned_double(const GameState *g, uint8_t tech)
{
    unsigned n = (unsigned)(tech - FIRST_DOUBLE_TECH);
    return (doubles_learned_byte(g, n / DOUBLES_A_BYTE) << (n % DOUBLES_A_BYTE)) & LEARNED_FIRST;
}

/* Whether both members of a double tech are in the fight and one of them is
 * the member. */
static bool double_for(const GameState *g, uint8_t member)
{
    uint8_t slot[2];
    for (unsigned part = 0; part < 2; part++) {
        uint8_t character = battle_tech_members(g, part);
        if (character == NO_MEMBER)
            return false;
        slot[part] = battle_character_slot(g, character);
        if (slot[part] & BATTLE_EMPTY_SLOT)
            return false;
    }
    return slot[0] == member || slot[1] == member;
}

/* The member's record filled with a double tech: what each of the two pays
 * for its part, and as the partner the other one's slot, with no second
 * partner. */
static void fill_double(GameState *g, uint8_t member, unsigned record, uint8_t tech)
{
    start_record(g, record, tech);
    uint8_t first = battle_tech_members(g, 0), second = battle_tech_members(g, 1);
    set_slot_cost(g, record, first, part_cost(g, tech, 0));
    set_slot_cost(g, record, second, part_cost(g, tech, 1));
    charge_machine_accesses(g, 3);
    uint8_t partner = battle_character_slot(g, second);
    if (partner == member)
        partner = battle_character_slot(g, first);
    set_battle_tech_records_partners(g, record, (uint8_t)(NO_PARTNER << 4 | partner));
}

// $C1:CA1A
/* The member's double techs: the first six the party has learned whose two
 * members are both in the fight, one of them the member, in the records from
 * the tenth. A member out of the fight has none. */
void battle_set_up_double_techs(GameState *g, uint8_t member)
{
    if (!combatant_in_fight(g, member)) {
        charge_machine_accesses(g, 10);
        return;
    }
    unsigned filled = 0;
    uint8_t tech;
    for (tech = FIRST_DOUBLE_TECH; tech < DOUBLE_TECHS_END; tech++) {
        if (!learned_double(g, tech))
            continue;
        effect_take_tech_members(g, tech);
        if (!double_for(g, member))
            continue;
        fill_double(g, member, member_record(g, member, (uint8_t)(FIRST_DOUBLE_RECORD + filled)), tech);
        if (++filled == MOST_DOUBLES)
            break;
    }
    (charge_machine_accesses(g, 20), charge_machine_rom_accesses(g, 2));
}

// $FD:AD09
/* Every member's tech records blank. */
enum { TECH_RECORDS = 60, BLANK = 0xFF };

void battle_blank_tech_records(GameState *g)
{
    for (unsigned record = 0; record < TECH_RECORDS; record++) {
        set_battle_tech_records_id(g, record, BLANK);
        set_battle_tech_records_menu_detail(g, record, BLANK);
        set_battle_tech_records_flags(g, record, BLANK);
        set_battle_tech_records_cost(g, record, BLANK);
        set_battle_tech_records_partners(g, record, BLANK);
        set_battle_tech_records_second_cost(g, record, BLANK);
        set_battle_tech_records_third_cost(g, record, BLANK);
    }
}

/* ---- The entries ------------------------------------------------------------------------------ */

/* The member in the direct page's cell the fight's start hands it in; a
 * member in the party's slots. */

/* A double or triple tech's cost for a member, from the original's builders
 * of the records (the double techs' is still the original's): the character
 * in A, the cost in Y, the member's records' place in its cell ($04), whose
 * record, the first double tech's or the triple tech's, takes it. The cut
 * cost comes back in A's low byte, with the carry of the accessory's test. */
enum { FIRST_RECORDS_PLACE = 0x1A80 };

/* The single and double techs' builders take the member as the triple
 * tech's does, and the character each is fighting as must be one of the
 * seven, whose techs the ROM lists. */
