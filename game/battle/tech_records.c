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
#include "battle/effect_parts.h"

#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/entries.h"
#include "battle/fight.h"
#include "battle/rules_data.h"
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
static bool accessory_test_carry(const GameState *g, uint8_t character)
{
    return battle_member_accessory(g, slot_paying(battle_character_slot(g, character))) > QUARTERS_COST;
}

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

/* The direct page's cells the search works in. Three are cells later work
 * reads as it finds them (battle.h): the first, the number tested, and the
 * one an enemy tech's hits are noted from; the search keeps its counts there
 * plainly. */
enum {
    LOOKED_PAST = BATTLE_CLOCK_READS, TECH_TWICE = 0x02, RECORDS_AT = 0x04, TECH_AT = BATTLE_NUMBER_TESTED,
    LEARNED_READ = 0x0C, TECHS_END = BATTLE_TECH_HITS_LEFT, PER_BYTE = 0x10, SPARE = 0x14,
};

/* Whether all three of a tech's members are in the battle: each a character
 * whose slot hasn't its top bit (none). */
enum { NO_MEMBER_SLOT = 0x80 };

static bool members_in_battle(const GameState *g)
{
    for (unsigned part = 0; part < TECH_MEMBERS; part++) {
        uint8_t character = battle_tech_members(g, part);
        if (character == NO_MEMBER || (battle_character_slot(g, character) & NO_MEMBER_SLOT))
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
    (void)effect_times(g, tech, PARTY_TECH_BYTES);
    ORIGINAL_EFFECT(battle_leave_multiply_carry(g, accessory_test_carry(g, battle_tech_members(g, 1))));
    uint8_t high = (uint8_t)(partner_slot(g, 2, member) << 4);
    /* the original works the partners out in the search's first cell */
    battle_keep_byte(g, LOOKED_PAST, high);
    set_battle_tech_records_partners(g, record, (uint8_t)(high | partner_slot(g, 1, member)));
}

static uint8_t learned_byte(const GameState *g, unsigned triple)
{
    return triple < PARTY_TRIPLE_TECHS_LEARNED_COUNT ? party_triple_techs_learned(g, triple)
                                                     : party_triple_techs_learned_unchecked(g, triple);
}

/* What the search leaves in the direct page's cells: the tech it stopped at,
 * where the triples end and how many it looked past, kept; and as
 * scaffolding twice the tech, the learned byte it read last, its count and
 * where the member's records are. A member out of the fight leaves them
 * clear. */
static void keep_counts(GameState *g, uint8_t tech, uint8_t end, uint16_t looked_past)
{
    battle_keep_word(g, TECH_AT, tech);
    battle_keep_word(g, TECHS_END, end);
    battle_keep_word(g, LOOKED_PAST, looked_past);
}

static void leave_search(GameState *g, uint8_t member, uint8_t tech)
{
    battle_leave_word(g, PER_BYTE, 0);
    battle_leave_word(g, SPARE, 0);
    battle_leave_word(g, TECH_TWICE, (uint16_t)(tech * 2));
    battle_leave_word(g, RECORDS_AT, battle_member_tech_records_place(g, member));
    battle_leave_byte(g, LEARNED_READ, learned_byte(g, (uint8_t)(tech - FIRST_TRIPLE_TECH)));
}

static void leave_nothing_searched(GameState *g)
{
    battle_leave_word(g, PER_BYTE, 0);
    battle_leave_word(g, SPARE, 0);
}

// $C1:CCCB
/* The member's triple tech: the first learned one all of whose members are in
 * the battle (the tech's members are left as the last learned one looked
 * at). A member out of the fight has none set. */
static void set_up_triple_tech(GameState *g, uint8_t member)
{
    if (!combatant_in_fight(g, member)) {
        keep_counts(g, 0, 0, 0);
        ORIGINAL_EFFECT(leave_nothing_searched(g));
        return;
    }
    for (uint8_t tech = FIRST_TRIPLE_TECH; tech < TRIPLE_TECHS_END; tech++) {
        uint8_t triple = (uint8_t)(tech - FIRST_TRIPLE_TECH);
        if (!(learned_byte(g, triple) & LEARNED))
            continue;
        effect_take_tech_members(g, tech);
        if (members_in_battle(g)) {
            keep_counts(g, tech, TRIPLE_TECHS_END, triple);
            ORIGINAL_EFFECT(leave_search(g, member, tech));
            fill_triple(g, member, tech);
            return;
        }
    }
    keep_counts(g, TRIPLE_TECHS_END, TRIPLE_TECHS_END, TRIPLE_TECHS_END - FIRST_TRIPLE_TECH);
    ORIGINAL_EFFECT(leave_search(g, member, TRIPLE_TECHS_END));
}

void battle_set_up_triple_tech_of(GameState *g, uint8_t member) { set_up_triple_tech(g, member); }

/* ---- The single and double techs -------------------------------------------------------------- */

/* A part's cost: what its effect costs in MP. The original finds the tech's
 * record by a multiply, entered with the carry its search for the tech's
 * members left, or the accessory's test before it (scaffolding). */
static uint8_t part_cost(GameState *g, uint8_t tech, unsigned part, bool carry)
{
    (void)effect_times(g, tech, PARTY_TECH_BYTES);
    ORIGINAL_EFFECT(battle_leave_multiply_carry(g, carry));
    return party_effect_mp_cost(g, party_tech(g, tech).effects[part]);
}

/* The carry the search for a tech's members ($C1:CB93) leaves: its members'
 * number times three past a byte. */
static bool members_search_carry(const GameState *g, uint8_t tech)
{
    uint8_t members = party_tech(g, tech).members;
    return (uint8_t)(members * 2) + members > 0xFF;
}

/* The cells the builders keep their places in, as scaffolding, and the three
 * of them later work reads as it finds them (battle.h), kept plainly: the
 * character or the learned byte's number, the tech, and where the techs end;
 * and the word the double techs' builder steps without starting it. */
enum { COUNTED_AT = BATTLE_CLOCK_READS, TECH_TWICE_AT = 0x02, RECORD_PLACE_AT = 0x04, CURRENT_TECH = BATTLE_NUMBER_TESTED,
       STEPPED = BATTLE_STAT_OFFSET_LOW, LEARNED_AT = 0x0C, TECHS_END_AT = BATTLE_TECH_HITS_LEFT, BIT_IN_BYTE = 0x10,
       DOUBLES_FILLED = 0x14 };

static void leave_record_place(GameState *g, uint8_t member, unsigned records)
{
    battle_leave_word(g, RECORD_PLACE_AT, (uint16_t)(battle_member_tech_records_place(g, member) + records * TECH_RECORD_BYTES));
}

enum { SINGLE_TECHS = 8 };

// $C1:C96A
/* The member's single techs: its character's eight in its first records, in
 * order, each the character has learned given its cost to the member's slot;
 * no partners. A member out of the fight has none. */
void battle_set_up_single_techs(GameState *g, uint8_t member)
{
    battle_keep_word(g, COUNTED_AT, 0);
    battle_keep_word(g, CURRENT_TECH, 0);
    battle_keep_word(g, TECHS_END_AT, 0);
    if (!combatant_in_fight(g, member))
        return;
    uint8_t character = battle_combatant_fighting(g, member);
    uint8_t first = character_first_single_tech(g, character), end = character_first_single_tech(g, (uint8_t)(character + 1));
    uint8_t learned = party_single_techs_learned(g, character);
    unsigned record = member_record(g, member, 0);
    uint8_t tech = first;
    do {
        if (learned & 0x80) {
            effect_take_tech_members(g, tech);
            start_record(g, record, tech);
            set_slot_cost(g, record, battle_tech_members(g, 0), part_cost(g, tech, 0, members_search_carry(g, tech)));
            set_battle_tech_records_partners(g, record, NO_PARTNERS);
        }
        learned = (uint8_t)(learned << 1);
        record++;
        tech++;
    } while (tech < end);
    battle_keep_word(g, COUNTED_AT, character);
    battle_keep_word(g, CURRENT_TECH, tech);
    battle_keep_word(g, TECHS_END_AT, end);
    ORIGINAL_EFFECT(battle_leave_word(g, TECH_TWICE_AT, (uint16_t)(tech * 2)));
    ORIGINAL_EFFECT(leave_record_place(g, member, (unsigned)(tech - first)));
    ORIGINAL_EFFECT(battle_leave_byte(g, LEARNED_AT, learned));
}

/* The double techs, three to each byte of what the party has learned. */
enum { FIRST_DOUBLE_TECH = 0x39, DOUBLE_TECHS_END = 0x66, DOUBLES_A_BYTE = 3, MOST_DOUBLES = 6 };

static uint8_t doubles_learned_byte(const GameState *g, unsigned n)
{
    return n < PARTY_DOUBLE_TECHS_LEARNED_COUNT ? party_double_techs_learned(g, n) : party_double_techs_learned_unchecked(g, n);
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
        if (slot[part] & NO_MEMBER_SLOT)
            return false;
    }
    return slot[0] == member || slot[1] == member;
}

/* The member's record filled with a double tech: what each of the two pays
 * for its part, and as the partner the other one's slot. */
static void fill_double(GameState *g, uint8_t member, unsigned record, uint8_t tech)
{
    start_record(g, record, tech);
    uint8_t first = battle_tech_members(g, 0), second = battle_tech_members(g, 1);
    set_slot_cost(g, record, first, part_cost(g, tech, 0, members_search_carry(g, tech)));
    set_slot_cost(g, record, second, part_cost(g, tech, 1, accessory_test_carry(g, first)));
    uint8_t partner = battle_character_slot(g, second);
    if (partner == member)
        partner = battle_character_slot(g, first);
    set_battle_tech_records_partners(g, record, (uint8_t)(partner | 0xF0));
}

// $C1:CA1A
/* The member's double techs: the first six the party has learned whose two
 * members are both in the fight, one of them the member, in the records from
 * the tenth. A member out of the fight has none. */
void battle_set_up_double_techs(GameState *g, uint8_t member)
{
    battle_keep_word(g, CURRENT_TECH, 0);
    battle_keep_word(g, COUNTED_AT, 0);
    ORIGINAL_EFFECT(battle_leave_word(g, BIT_IN_BYTE, 0));
    battle_keep_word(g, TECHS_END_AT, 0);
    ORIGINAL_EFFECT(battle_leave_word(g, DOUBLES_FILLED, 0));
    if (!combatant_in_fight(g, member))
        return;
    uint16_t stepped = battle_cell_word(g, STEPPED);
    unsigned byte = 0, bit = 0, filled = 0;
    uint8_t learned = doubles_learned_byte(g, 0);
    uint8_t tech = FIRST_DOUBLE_TECH;
    for (;;) {
        if (learned & 0x80) {
            effect_take_tech_members(g, tech);
            if (double_for(g, member)) {
                fill_double(g, member, member_record(g, member, (uint8_t)(FIRST_DOUBLE_RECORD + filled)), tech);
                if (++filled == MOST_DOUBLES)
                    break;
            }
        }
        learned = (uint8_t)(learned << 1);
        tech++;
        if (++bit == DOUBLES_A_BYTE) {
            learned = doubles_learned_byte(g, ++byte);
            bit = 0;
        }
        stepped = (uint16_t)(stepped + 12);
        if (tech >= DOUBLE_TECHS_END)
            break;
    }
    battle_keep_word(g, CURRENT_TECH, tech);
    battle_keep_word(g, COUNTED_AT, (uint16_t)byte);
    battle_keep_word(g, TECHS_END_AT, DOUBLE_TECHS_END);
    battle_keep_word(g, STEPPED, stepped);
    ORIGINAL_EFFECT(battle_leave_word(g, TECH_TWICE_AT, (uint16_t)(tech * 2)));
    /* the place it steps is the member's records', the tenth's offset in the
     * original's stores rather than in the cell */
    ORIGINAL_EFFECT(leave_record_place(g, member, filled == MOST_DOUBLES ? filled - 1 : filled));
    ORIGINAL_EFFECT(battle_leave_byte(g, LEARNED_AT, learned));
    ORIGINAL_EFFECT(battle_leave_word(g, BIT_IN_BYTE, (uint16_t)bit));
    ORIGINAL_EFFECT(battle_leave_word(g, DOUBLES_FILLED, (uint16_t)filled));
}

// $FD:AD09
/* Every member's tech records blank. */
enum { TECH_RECORDS = 60 };

void battle_blank_tech_records(GameState *g)
{
    for (unsigned record = 0; record < TECH_RECORDS; record++) {
        set_battle_tech_records_id(g, record, NO_COST);
        set_battle_tech_records_menu_detail(g, record, NO_COST);
        set_battle_tech_records_flags(g, record, NO_COST);
        set_battle_tech_records_cost(g, record, NO_COST);
        set_battle_tech_records_partners(g, record, NO_COST);
        set_battle_tech_records_second_cost(g, record, NO_COST);
        set_battle_tech_records_third_cost(g, record, NO_COST);
    }
}

/* ---- The entries ------------------------------------------------------------------------------ */

/* The member in the direct page's cell the setup hands it in ($06); a
 * member in the party's slots. */
enum { MEMBER_CELL = 0x06 };

bool battle_set_up_triple_tech_ready(Snes *s)
{
    vars_peek(true);
    bool member = battle_low_scratch(state_of(s), MEMBER_CELL) < BATTLE_PARTY_SIZE;
    vars_peek(false);
    return battle_on_page(s) && member;
}

void battle_set_up_triple_tech(Snes *s)
{
    set_up_triple_tech(state_of(s), battle_low_scratch(state_of(s), MEMBER_CELL));
    m_rts(s);
}

/* A double or triple tech's cost for a member, from the original's builders
 * of the records (the double techs' is still the original's): the character
 * in A, the cost in Y, the member's records' place in its cell ($04), whose
 * record, the first double tech's or the triple tech's, takes it. The cut
 * cost comes back in A's low byte, with the carry of the accessory's test. */
enum { FIRST_RECORDS_PLACE = 0x1A80, RECORDS = 60 };

static bool record_place_ready(Snes *s, uint8_t entry)
{
    vars_peek(true);
    uint16_t place = battle_cell_word(state_of(s), RECORDS_AT);
    vars_peek(false);
    unsigned first = (unsigned)(place - FIRST_RECORDS_PLACE) / TECH_RECORD_BYTES;
    return battle_on_page(s) && place >= FIRST_RECORDS_PLACE && (place - FIRST_RECORDS_PLACE) % TECH_RECORD_BYTES == 0
           && first + entry < RECORDS && (uint8_t)s->cpu.a < BATTLE_CHARACTER_SLOT_COUNT;
}

static void cost_entry(Snes *s, uint8_t entry)
{
    GameState *g = state_of(s);
    uint16_t place = battle_cell_word(g, RECORDS_AT);
    uint8_t character = (uint8_t)s->cpu.a;
    uint8_t cut = set_slot_cost(g, (unsigned)(place - FIRST_RECORDS_PLACE) / TECH_RECORD_BYTES + entry, character,
                                (uint8_t)s->cpu.y);
    s->cpu.y = (uint16_t)((s->cpu.y & 0xFF00) | cut);
    m_set_a8(s, cut);
    m_set_flag(s, CPU_FLAG_C, accessory_test_carry(g, character));
    m_rts(s);
}

bool battle_set_double_tech_cost_ready(Snes *s) { return record_place_ready(s, FIRST_DOUBLE_RECORD); }
void battle_set_double_tech_cost(Snes *s) { cost_entry(s, FIRST_DOUBLE_RECORD); }
bool battle_set_triple_tech_cost_ready(Snes *s) { return record_place_ready(s, TRIPLE_RECORD); }
void battle_set_triple_tech_cost(Snes *s) { cost_entry(s, TRIPLE_RECORD); }

/* The single and double techs' builders take the member as the triple
 * tech's does, and the character each is fighting as must be one of the
 * seven, whose techs the ROM lists. */
enum { CHARACTERS = 7 };

bool battle_set_up_member_techs_ready(Snes *s)
{
    vars_peek(true);
    GameState *g = state_of(s);
    uint8_t member = battle_low_scratch(g, MEMBER_CELL);
    bool ready = member < BATTLE_PARTY_SIZE
                 && (!combatant_in_fight(g, member) || battle_combatant_fighting(g, member) < CHARACTERS);
    vars_peek(false);
    return battle_on_page(s) && ready;
}

void battle_set_up_single_techs_entry(Snes *s)
{
    battle_set_up_single_techs(state_of(s), battle_low_scratch(state_of(s), MEMBER_CELL));
    m_rts(s);
}

void battle_set_up_double_techs_entry(Snes *s)
{
    battle_set_up_double_techs(state_of(s), battle_low_scratch(state_of(s), MEMBER_CELL));
    m_rts(s);
}

void battle_blank_tech_records_entry(Snes *s)
{
    battle_blank_tech_records(state_of(s));
    m_rtl(s);
}
