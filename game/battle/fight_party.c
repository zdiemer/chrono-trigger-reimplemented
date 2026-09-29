/* Battle: the party as the fight starts.
 *
 * Each member fights with a copy of its character's record (character_
 * records) as its stat block: its HP and MP, its base stats, its equipment.
 * The setup then works out what the equipment does to it: its elemental
 * affinities, its stats with the equipment's boosts (battle/member_stats.c),
 * its attack and defence, what its armor, helmet and accessory add (a
 * modifier, a status it starts with, more HP, a better critical rate, a
 * status it can't be given), and which weapon it carries. Ayla fights with
 * her fists, which grow with her level.
 *
 * A demo battle (the attract mode's) fights with the characters as a new game
 * starts them, the HP and MP the demo gives each, and an inventory of every
 * item. */
#include "battle/fight.h"

#include "battle/arith.h"
#include "battle/battle.h"
#include "battle/combatant.h"
#include "battle/effect_data.h"
#include "battle/effect_parts.h"
#include "battle/equipment_data.h"
#include "battle/rules_data.h"
#include "battle/setup_data.h"
#include "original_effects.h"
#include "vars.h"

enum { CHARACTERS = 7, RECORD_BYTES = 0x50, AYLA = 5 };

/* ---- A demo battle's party ---------------------------------------------------------------------- */

/* The demo gives HP and MP to eight records, one more than there are
 * characters: the eighth lands on what the party has learned after the
 * seventh (character_records' unchecked). */
enum { DEMO_RECORDS = 8, DEMO_STAT = 35, DEMO_OTHER_STAT = 0x80, INVENTORY_PLACES = 256 };

static void set_record_byte(GameState *g, unsigned character, unsigned at, uint8_t v)
{
    if (character < CHARACTER_RECORDS_COUNT)
        set_character_records_record_byte(g, character, at, v);
    else
        set_character_records_record_byte_unchecked(g, character, at, v);
}

static void set_record_word(GameState *g, unsigned character, unsigned at, uint16_t v)
{
    set_record_byte(g, character, at, (uint8_t)v);
    set_record_byte(g, character, at + 1, (uint8_t)(v >> 8));
}

// $FD:B33F
/* The characters as a new game starts them, and an inventory of every item,
 * as many of each as its number. */
void fight_start_demo_characters(GameState *g)
{
    ORIGINAL_EFFECT(set_battle_demo_spare(g, 0));
    set_battle_party_fighting_count(g, BATTLE_PARTY_SIZE);
    for (unsigned at = 0; at < CHARACTERS * RECORD_BYTES; at++)
        set_character_records_record_byte(g, at / RECORD_BYTES, at % RECORD_BYTES, character_start_record_byte(g, (uint16_t)at));
    for (unsigned place = 0; place < INVENTORY_PLACES; place++) {
        set_inventory_items_unchecked(g, place, (uint8_t)place);
        set_inventory_counts_unchecked(g, place, (uint8_t)place);
    }
}

// $FD:B2DE
/* A demo battle's party: the characters as a new game starts them, and each
 * one's HP and MP as the demo gives them, with two of its base stats at 35 and
 * another at 128. */
void fight_load_demo_party(GameState *g)
{
    if (!battle_is_demo(g))
        return;
    fight_start_demo_characters(g);
    uint8_t demo = battle_demo(g);
    for (unsigned character = 0; character < DEMO_RECORDS; character++) {
        DemoMember d = demo_member(g, demo, character);
        set_record_byte(g, character, STAT_BASE_STATS, DEMO_STAT);
        set_record_byte(g, character, STAT_BASE_STATS + 3, DEMO_STAT);
        set_record_byte(g, character, STAT_BASE_STATS + 1, DEMO_OTHER_STAT);
        set_record_word(g, character, STAT_HP, d.hp);
        set_record_word(g, character, STAT_MAX_HP, d.max_hp);
        set_record_word(g, character, STAT_MP, d.mp);
        set_record_word(g, character, STAT_MAX_MP, d.max_mp);
    }
}

/* ---- Who fights, and with what ------------------------------------------------------------------ */

/* The cells the record's copy leaves: its count, kept (battle.h), and the
 * character. */
enum { COPIED_AT = BATTLE_CLOCK_READS, CHARACTER_AT = 0x02, COPY_ENDS_AT = 0x80 };

// $FD:AE52
/* A member's stat block, its character's record copied in and the rest of it
 * clear; a member out of the fight keeps its block as it is. */
void fight_take_member_record(GameState *g, uint8_t member)
{
    uint8_t character = battle_combatant_fighting(g, member);
    if (character & 0x80)
        return;
    /* the original finds the record by a multiply */
    (void)effect_times(g, RECORD_BYTES, character);
    for (unsigned at = 0; at < BATTLE_STAT_BLOCK_BYTES; at++)
        set_combatant_stat_byte(g, member, (uint16_t)at, at < RECORD_BYTES ? character_records_record_byte(g, character, at) : 0);
    battle_keep_word(g, COPIED_AT, COPY_ENDS_AT);
    ORIGINAL_EFFECT(battle_leave_word(g, CHARACTER_AT, character));
}

// $C1:FDAE
/* Ayla's fists, the weapon she fights with: a better one every 24 levels, from
 * the first ($44). The original divides with the carry its caller's test for
 * Ayla left set. */
enum { FIRST_FISTS = 0x44, LEVELS_A_FIST = 24 };

uint8_t fight_aylas_fists(GameState *g, uint8_t level)
{
    return (uint8_t)(battle_divide_leftover_marked(g, level, LEVELS_A_FIST).quotient + FIRST_FISTS);
}

/* Whether the member's block is Ayla's: its first byte, the character. */
bool fight_member_is_ayla(const GameState *g, uint8_t member) { return combatant_stat_byte(g, member, 0) == AYLA; }

// $C1:FB06
/* The party in the fight: each member not sitting it out, counted; with the
 * enemies, the number of combatants the timers' order holds. */
void fight_list_party(GameState *g)
{
    uint8_t members = 0;
    for (unsigned slot = 0; slot < BATTLE_PARTY_SIZE; slot++) {
        set_battle_combatant_fighting(g, slot, COMBATANT_NONE);
        set_battle_combatant_id(g, slot, COMBATANT_NONE);
        uint8_t character = party_members(g, slot);
        if (battle_member_sitting_out(g, slot) || (character & 0x80))
            continue;
        set_battle_combatant_fighting(g, slot, character);
        set_battle_combatant_id(g, slot, character);
        members++;
    }
    set_battle_party_fighting_count(g, members);
    set_battle_timer_order_length(g, (uint8_t)(members + battle_enemy_count(g)));
}

// $FD:B3FE
/* A member's elemental affinities as they start: each element's at the
 * middle of the scale, but the one its attack is made of (the first of its
 * attack's four elements), a step better. A member out of the fight has none
 * set. */
enum { ELEMENTS = 4, AFFINITY_NEUTRAL = 4, AFFINITY_OWN_ELEMENT = 5 };

void fight_set_up_affinities(GameState *g, uint8_t member)
{
    if (!combatant_in_fight(g, member))
        return;
    for (unsigned e = 0; e < ELEMENTS; e++)
        set_combatant_stat_byte(g, member, (uint16_t)(STAT_ELEMENT_AFFINITY + e), AFFINITY_NEUTRAL);
    uint8_t elements = combatant_stat_byte(g, member, STAT_ELEMENTS);
    for (unsigned e = 0; e < ELEMENTS; e++) {
        if (elements & (0x80 >> e)) {
            set_combatant_stat_byte(g, member, (uint16_t)(STAT_ELEMENT_AFFINITY + e), AFFINITY_OWN_ELEMENT);
            return;
        }
    }
}

/* ---- The accessories ---------------------------------------------------------------------------- */

/* The accessories whose effects the setup applies: one turns the experience
 * the battle gives into gold (battle_members_turning_experience_to_gold),
 * two raise the most HP by a quarter and a half, one makes two of the weapons
 * strike critically more often, and one wards off confusion. */
enum {
    EXPERIENCE_TO_GOLD = 0xA9, QUARTER_MORE_HP = 0xA0, HALF_MORE_HP = 0xA1, SHARPER_WEAPONS = 0xB3,
    WARDS_OFF_CONFUSION = 0xAB,
};

// $FD:AE99
/* Each member's accessory noted, and how many wear the one that turns the
 * experience into gold. */
void fight_note_accessories(GameState *g)
{
    uint8_t turning = 0;
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        uint8_t accessory = combatant_stat_byte(g, (uint8_t)member, STAT_ACCESSORY);
        set_battle_member_accessory(g, member, accessory);
        if (accessory == EXPERIENCE_TO_GOLD)
            turning++;
    }
    set_battle_members_turning_experience_to_gold(g, turning);
}

// $FD:B3EB
/* The two weapons the accessory sharpens strike critically half the time. */
enum { SHARPENED_A = 0x3D, SHARPENED_B = 0x42, SHARPENED_CRITICAL_RATE = 50 };

void fight_raise_critical_rate(GameState *g, uint8_t member)
{
    uint8_t weapon = combatant_stat_byte(g, member, STAT_WEAPON);
    if (weapon == SHARPENED_A || weapon == SHARPENED_B)
        set_combatant_stat_byte(g, member, STAT_CRITICAL_RATE, SHARPENED_CRITICAL_RATE);
}

/* Where the armor's and the helmet's records put their modifier in the block
 * (battle/member_stats.c copies them): the modifier, and whether it applies. */
enum { ARMOR_MODIFIER = 0x49, ARMOR_MODIFIER_ON = 0x4A, HELMET_MODIFIER = 0x71, HELMET_MODIFIER_ON = 0x72,
       MODIFIER_APPLIES = 1 };

/* A modifier a piece of armor gives: the bits its record names set in the
 * byte of the member's block it names (a block's offset past its end reaches
 * the next). */
static void give_modifier(GameState *g, uint8_t member, uint8_t modifier)
{
    ModifierRecord r = modifier_record_at(g, (uint8_t)(modifier * 3));
    set_combatant_stat_byte(g, member, r.routine, combatant_stat_byte(g, member, r.routine) | r.first);
}

// $FD:B555
/* The action and the enemies' animations start clear; then what the
 * accessories and the armor do: the sharpened weapons, and each armor's and
 * helmet's modifier. */
enum { ENEMY_ANIMATION_WORK = 16 };

void fight_apply_accessory_effects(GameState *g)
{
    for (unsigned n = 0; n < BATTLE_EFFECT_RECORD_COUNT; n++)
        set_battle_effect_record(g, n, 0);
    for (unsigned n = 0; n < BATTLE_EFFECT_RECORD_SPARES_COUNT; n++)
        ORIGINAL_EFFECT(set_battle_effect_record_spares(g, n, 0));
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++) {
        set_battle_chosen_actions(g, BATTLE_FIRST_ENEMY + enemy, 0);
        set_battle_enemy_animation(g, enemy, 0);
        set_battle_enemy_target(g, enemy, 0);
    }
    for (unsigned n = 0; n < ENEMY_ANIMATION_WORK; n++)
        set_battle_enemy_animation_work(g, n, 0);
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        if (battle_member_accessory(g, member) == SHARPER_WEAPONS)
            fight_raise_critical_rate(g, member);
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        if (combatant_stat_byte(g, member, ARMOR_MODIFIER_ON) == MODIFIER_APPLIES)
            give_modifier(g, member, combatant_stat_byte(g, member, ARMOR_MODIFIER));
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        if (combatant_stat_byte(g, member, HELMET_MODIFIER_ON) == MODIFIER_APPLIES)
            give_modifier(g, member, combatant_stat_byte(g, member, HELMET_MODIFIER));
}

/* An accessory's record in the block (battle/member_stats.c copies it): its
 * flags, and two bytes they say how to read -- a status the member starts
 * with (which status byte from the condition, and its bits), or a boost to
 * its stats. */
enum { ACCESSORY_FLAGS = 0x4C, ACCESSORY_FIRST = 0x4D, ACCESSORY_SECOND = 0x4E, GIVES_STATUS = 0x80, GIVES_BOOST = 0x40 };

/* The cells the original works in: the status's bits (kept, battle.h), where
 * the block is, and which status byte. */
enum { STATUS_BITS_AT = BATTLE_CLOCK_READS, BLOCK_OFFSET_AT = 0x02, STATUS_BYTE_AT = 0x04, MOST_HP = 999 };

static void keep_bonus_cells(GameState *g, uint16_t bits, uint16_t block, uint16_t byte)
{
    battle_keep_word(g, STATUS_BITS_AT, bits);
    ORIGINAL_EFFECT(battle_leave_word(g, BLOCK_OFFSET_AT, block));
    ORIGINAL_EFFECT(battle_leave_word(g, STATUS_BYTE_AT, byte));
}

// $FD:B363
/* What a member's accessory gives it from the start: the status its record
 * names, and the accessories that raise its most HP by a quarter or by half,
 * up to 999. A member out of the fight gets nothing. */
void fight_apply_member_bonuses(GameState *g, uint8_t member)
{
    keep_bonus_cells(g, 0, 0, 0);
    if (!combatant_in_fight(g, member))
        return;
    uint16_t block = (uint16_t)(member * BATTLE_STAT_BLOCK_BYTES);
    keep_bonus_cells(g, 0, block, 0);
    if (combatant_stat_byte(g, member, ACCESSORY_FLAGS) & GIVES_STATUS) {
        uint8_t byte = combatant_stat_byte(g, member, ACCESSORY_FIRST), bits = combatant_stat_byte(g, member, ACCESSORY_SECOND);
        uint16_t at = (uint16_t)(STAT_CONDITION + byte);
        set_combatant_stat_byte(g, member, at, combatant_stat_byte(g, member, at) | bits);
        keep_bonus_cells(g, bits, block, byte);
    }
    uint8_t accessory = combatant_stat_byte(g, member, STAT_ACCESSORY);
    if (accessory != QUARTER_MORE_HP && accessory != HALF_MORE_HP)
        return;
    uint16_t most = battle_stats_max_hp(g, member);
    uint16_t raised = (uint16_t)(most + (accessory == QUARTER_MORE_HP ? most / 4 : most / 2));
    set_battle_stats_max_hp(g, member, raised < MOST_HP ? raised : MOST_HP);
}

// $FD:AEC4
/* The accessory that wards off confusion: the member shrugs it off. */
void fight_ward_off_confusion(GameState *g)
{
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        if (combatant_stat_byte(g, member, STAT_ACCESSORY) == WARDS_OFF_CONFUSION)
            set_combatant_stat_byte(g, member, STAT_STATUS_IGNORED, combatant_stat_byte(g, member, STAT_STATUS_IGNORED) | STATUS_CONFUSED);
}

/* ---- The weapons -------------------------------------------------------------------------------- */

/* A member's weapon noted as the battle carries it, with its marks: $80, 1,
 * and the party-slot bit of the character it is made for, where that
 * character is in the party (nothing reads the marks: scaffolding). The
 * character is the first of the weapon's bits from the top; a weapon made for
 * none reads past the characters' slots, as the original does. */
enum { CARRIED_BYTES = 5, NO_CHARACTER_SLOT = 0x80, WEAPON_RECORD_BYTES = 6 };

static uint8_t owners_slot_bit(const GameState *g, uint16_t record)
{
    uint8_t made_for = weapon_made_for(g, record);
    unsigned character = 0;
    while (character < 8 && !(made_for & 0x80)) {
        made_for = (uint8_t)(made_for << 1);
        character++;
    }
    uint8_t slot = character < BATTLE_CHARACTER_SLOT_COUNT ? battle_character_slot(g, character)
                                                            : battle_character_slot_unchecked(g, character);
    return slot & NO_CHARACTER_SLOT ? 0 : (uint8_t)(slot < 8 ? 0x80 >> slot : 0);
}

/* The cells the original counts in: each member's weapon (kept: battle.h),
 * its record's place, the member, and the carried bytes' place (kept). */
enum { WEAPON_AT = BATTLE_MEMBER_CELL, RECORD_AT = 0x04, MEMBER_AT = 0x02, CARRIED_AT = BATTLE_STAT_OFFSET_LOW };

// $FD:AEF2
/* The weapon each member fights with, noted as the battle carries it. */
void fight_note_weapons(GameState *g)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        set_battle_member_carried_weapon(g, member, 0);
        set_battle_member_carried_marks(g, member, 0);
        ORIGINAL_EFFECT(set_battle_member_carried_spare(g, member, 0));
    }
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++) {
        uint8_t weapon = combatant_stat_byte(g, member, STAT_WEAPON);
        battle_keep_byte(g, WEAPON_AT, weapon);
        if (weapon == 0)
            continue;
        uint16_t record = (uint16_t)effect_times(g, weapon, WEAPON_RECORD_BYTES);
        ORIGINAL_EFFECT(battle_leave_word(g, RECORD_AT, record));
        set_battle_member_carried_weapon(g, member, weapon);
        uint8_t owner = owners_slot_bit(g, record);
        ORIGINAL_EFFECT(set_battle_member_carried_marks(g, member, (uint32_t)0x80 | 1u << 8 | (uint32_t)owner << 16));
    }
    ORIGINAL_EFFECT(battle_leave_word(g, MEMBER_AT, BATTLE_PARTY_SIZE));
    battle_keep_word(g, CARRIED_AT, BATTLE_PARTY_SIZE * CARRIED_BYTES);
}

/* ---- Attack and defence ------------------------------------------------------------------------- */

/* The stats the setup builds the member's attack and defence from.
 * combatant.h's names for three of them disagree with what they are built
 * from here (docs/TODO.md): its defence is the stat it calls STAT_HIT with the
 * armor's and the helmet's, and Marle's and Lucca's attack the one it calls
 * STAT_STAMINA where the others' is their power; the attack is the byte it
 * calls STAT_SPEED. */
enum { OWN_DEFENCE = STAT_HIT, MARKSMAN_POWER = STAT_STAMINA, ATTACK = STAT_SPEED, DEFENCE = 0x3E,
       ARMOR_DEFENCE = 0x48, HELMET_DEFENCE = 0x70, MOST_DEFENCE = 0xFF, MARLE = 1, LUCCA = 2 };

/* The cells the original adds in: the member's block's place, and what the
 * boost was handed (kept: battle.h). */
enum { BLOCK_PLACE_AT = 0x12, BOOST_BLOCK_AT = BATTLE_CLOCK_READS, BOOST_TABLE_AT = BATTLE_TECH_HITS_LEFT };

/* The original adds the two in the arithmetic's cells, which are game state
 * (battle/arith.h): the next multiply's or division's caller finds them. */
static void keep_sum_cells(GameState *g, uint8_t first, uint8_t second)
{
    set_battle_wide_math(g, 0, first);
    set_battle_wide_math(g, 1, 0);
    set_battle_wide_math(g, 2, second);
    set_battle_wide_math(g, 3, 0);
}

// $C1:FC4B
/* A member's defence and attack: its own defence with its armor's and its
 * helmet's, at most 255; the boost its accessory gives, if any; and its
 * attack, its power (Marle's and Lucca's, the other stat) with a byte the
 * original reads from bank CC at the block's place where it means the
 * weapon's record, taken in a byte. */
void fight_add_up_member_stats(GameState *g, uint8_t member)
{
    uint16_t place = battle_stat_block_place(g, member);
    ORIGINAL_EFFECT(battle_leave_word(g, BLOCK_PLACE_AT, place));
    uint16_t defence = (uint16_t)(combatant_stat_byte(g, member, OWN_DEFENCE) + combatant_stat_byte(g, member, ARMOR_DEFENCE)
                                  + combatant_stat_byte(g, member, HELMET_DEFENCE));
    set_combatant_stat_byte(g, member, DEFENCE, defence < MOST_DEFENCE ? (uint8_t)defence : MOST_DEFENCE);
    uint8_t boost = combatant_stat_byte(g, member, ACCESSORY_FIRST);
    if ((combatant_stat_byte(g, member, ACCESSORY_FLAGS) & GIVES_BOOST) && boost) {
        battle_keep_word(g, BOOST_BLOCK_AT, place);
        battle_keep_word(g, BOOST_TABLE_AT, STAT_BOOSTS);
        battle_apply_boost(g, member, boost);
    }
    /* the original finds the weapon's record by a multiply, then reads the
     * wrong place for it */
    (void)effect_times(g, combatant_stat_byte(g, member, STAT_WEAPON), WEAPON_RECORD_BYTES);
    uint8_t character = battle_combatant_fighting(g, member);
    uint8_t power = combatant_stat_byte(g, member, character == MARLE || character == LUCCA ? MARKSMAN_POWER : STAT_POWER);
    uint8_t stray = equipment_bank_byte(g, place);
    set_combatant_stat_byte(g, member, ATTACK, (uint8_t)(power + stray));
    keep_sum_cells(g, power, stray);
}
