/* Battle: the party's combination techs in its members' tech records, as the
 * battle sets them up.
 *
 * Each member has twenty tech records (battle_tech_records, battle/
 * setup_party.c lists them): its single techs, its double techs from record
 * 10, and a triple tech in record 18. A combination tech's record says, beside
 * the tech, what each of the party's three slots pays for it in MP -- its own
 * part's cost, cut by that member's accessory -- and in its partners byte the
 * slots of the other two who perform it with the member.
 *
 * The triple tech a member shows is the first of the party's learned triple
 * techs ($66 to $74) whose three members are all in the battle
 * ($C1:CCCB); the double techs' records (the original's $C1:CA1A) have their
 * costs set the same way ($C1:CC0B). The entries are at the end. */
#include "battle/effect_parts.h"

#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/entries.h"
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

/* The member's triple tech record filled with the tech: its menu bytes, what
 * each slot pays, and its partners. */
static void fill_triple(GameState *g, uint8_t member, uint8_t tech)
{
    unsigned record = member_record(g, member, TRIPLE_RECORD);
    TechMenuBytes menu = tech_menu_bytes(g, tech);
    set_battle_tech_records_flags(g, record, menu.flags);
    set_battle_tech_records_menu_detail(g, record, menu.detail);
    set_battle_tech_records_id(g, record, tech);
    set_battle_tech_records_cost(g, record, NO_COST);
    set_battle_tech_records_second_cost(g, record, NO_COST);
    set_battle_tech_records_third_cost(g, record, NO_COST);
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
