/* Battle: who is in the fight, and the enemies as it starts.
 *
 * The fight has the party's three slots and the eight enemy slots the
 * battle's placement lists (battle_enemy_placement): an empty party slot or
 * an unlisted enemy is nobody ($FF), and an enemy placed in reserve is in the
 * battle but not yet in the fight, to come in later. Each enemy's stat block
 * is filled from its record in the ROM, and the enemies that have something
 * to steal are marked. */
#include "battle/fight.h"

#include "battle/arith.h"
#include "battle/battle.h"
#include "battle/combatant.h"
#include "battle/effect_data.h"
#include "battle/effect_parts.h"
#include "battle/rules_data.h"
#include "battle/setup_data.h"
#include "original_effects.h"
#include "vars.h"

/* ---- Who is in the fight ---------------------------------------------------------------------- */

enum { EMPTY_SLOT = 0x80, NO_ENEMY = 0x80, CHARACTER_SLOTS = 7 };

static void no_one_in(GameState *g, unsigned slot)
{
    set_battle_combatant_fighting(g, slot, COMBATANT_NONE);
    set_battle_combatant_id(g, slot, COMBATANT_NONE);
}

static void in_fight_as(GameState *g, unsigned slot, uint8_t id)
{
    set_battle_combatant_fighting(g, slot, id);
    set_battle_combatant_id(g, slot, id);
}

// $FD:B22E
/* Every stat block cleared and every gauge empty; the party's members in
 * their slots, each character's slot noted; and the enemies the placement
 * lists, those placed in reserve out of the fight for now. */
void fight_list_combatants(GameState *g)
{
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++)
        for (unsigned at = 0; at < BATTLE_STAT_BLOCK_BYTES; at++)
            set_combatant_stat_byte(g, (uint8_t)slot, (uint16_t)at, 0);
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        set_battle_gauge_wait(g, member, COMBATANT_NONE);
        set_battle_gauge_wait_full(g, member, COMBATANT_NONE);
    }
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++)
        no_one_in(g, slot);
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++)
        set_battle_enemy_standing(g, enemy, 0);
    uint8_t members = 0;
    for (unsigned slot = 0; slot < BATTLE_PARTY_SIZE; slot++) {
        uint8_t character = party_members(g, slot);
        if (!(character & EMPTY_SLOT)) {
            in_fight_as(g, slot, character);
            members++;
        }
    }
    set_battle_party_fighting_count(g, members);
    set_battle_party_defeated(g, 0);
    for (unsigned character = 0; character < CHARACTER_SLOTS; character++)
        set_battle_character_slot(g, character, COMBATANT_NONE);
    for (uint8_t slot = 0; slot < BATTLE_PARTY_SIZE; slot++) {
        uint8_t character = battle_combatant_fighting(g, slot);
        if (character != COMBATANT_NONE)
            set_battle_character_slot(g, character, slot);
    }
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++) {
        uint16_t placed = battle_enemy_placement_enemy(g, enemy);
        if (placed >> 8 & NO_ENEMY)
            continue;
        unsigned slot = BATTLE_FIRST_ENEMY + enemy;
        in_fight_as(g, slot, (uint8_t)placed);
        if (battle_enemy_placement_kind(g, enemy) & ENEMY_IN_RESERVE) {
            set_battle_combatant_fighting(g, slot, COMBATANT_NONE);
            set_battle_enemy_standing(g, enemy, battle_enemy_standing(g, enemy) | ENEMY_IN_RESERVE);
        }
    }
    set_battle_enemy_count(g, BATTLE_ENEMIES);
}

/* ---- An enemy's stat block --------------------------------------------------------------------- */

/* Where each part of an enemy's record goes in its stat block: its HP (as
 * its HP and its most), its level, what it shrugs off and keeps, its stats
 * from stamina on, and its kind, family and attack. */
enum {
    RECORD_HP = 0, RECORD_LEVEL = 2, RECORD_SHRUGS_OFF = 3, SHRUGS_OFF_BYTES = 5, RECORD_STATS = 8, STATS_BYTES = 12,
    RECORD_KIND = 20, KIND_BYTES = 3, FIRST_STAT = 0x37,
};

/* The cells the original leaves: the id of the enemy it loaded, and its id
 * times eight with the count of the record's last part over its low byte
 * (kept, as later work reads it as it finds it: battle.h). */
enum { COUNT_AT = BATTLE_CLOCK_READS, ID_AT = 0x04 };

// $FD:B4D6
/* An enemy's stat block cleared. */
void fight_clear_enemy_block(GameState *g, uint8_t enemy)
{
    for (unsigned at = 0; at < BATTLE_STAT_BLOCK_BYTES; at++)
        set_combatant_stat_byte(g, (uint8_t)(BATTLE_FIRST_ENEMY + enemy), (uint16_t)at, 0);
}

// $FD:B438
/* The enemy's stat block, cleared, filled from its record: its id, its HP, its
 * level, what it shrugs off and the statuses it keeps (also as its third and
 * fourth), its stats, and its kind. */
void fight_load_enemy(GameState *g, uint8_t enemy)
{
    uint8_t slot = (uint8_t)(BATTLE_FIRST_ENEMY + enemy), id = battle_combatant_id(g, slot);
    fight_clear_enemy_block(g, enemy);
    set_combatant_stat_byte(g, slot, 0, id);
    uint8_t hp_low = enemy_record_byte(g, id, RECORD_HP), hp_high = enemy_record_byte(g, id, RECORD_HP + 1);
    set_combatant_stat_byte(g, slot, STAT_HP, hp_low);
    set_combatant_stat_byte(g, slot, STAT_MAX_HP, hp_low);
    set_combatant_stat_byte(g, slot, STAT_HP + 1, hp_high);
    set_combatant_stat_byte(g, slot, STAT_MAX_HP + 1, hp_high);
    set_combatant_stat_byte(g, slot, STAT_LEVEL, enemy_record_byte(g, id, RECORD_LEVEL));
    for (unsigned n = 0; n < SHRUGS_OFF_BYTES; n++)
        set_combatant_stat_byte(g, slot, (uint16_t)(STAT_SHRUGS_OFF + n), enemy_record_byte(g, id, RECORD_SHRUGS_OFF + n));
    set_combatant_stat_byte(g, slot, STAT_STATUS_3, combatant_stat_byte(g, slot, STAT_KEPT_STATUS_3));
    set_combatant_stat_byte(g, slot, STAT_STATUS_4, combatant_stat_byte(g, slot, STAT_KEPT_STATUS_4));
    for (unsigned n = 0; n < STATS_BYTES; n++)
        set_combatant_stat_byte(g, slot, (uint16_t)(FIRST_STAT + n), enemy_record_byte(g, id, RECORD_STATS + n));
    for (unsigned n = 0; n < KIND_BYTES; n++)
        set_combatant_stat_byte(g, slot, (uint16_t)(STAT_KIND + n), enemy_record_byte(g, id, RECORD_KIND + n));
    ORIGINAL_EFFECT(battle_leave_word(g, ID_AT, id));
    battle_keep_word(g, COUNT_AT, (uint16_t)((id * 8) & 0xFF00) | KIND_BYTES);
}

// $FD:B121
/* The low two bits of each enemy's family, noted (nothing reads them:
 * scaffolding). */
enum { FAMILY_BITS = 0x03 };

static void note_family(GameState *g, unsigned enemy)
{
    set_battle_enemy_family_bits(g, enemy, combatant_stat_byte(g, (uint8_t)(BATTLE_FIRST_ENEMY + enemy), STAT_FAMILY) & FAMILY_BITS);
}

void fight_note_enemy_families(GameState *g)
{
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++)
        ORIGINAL_EFFECT(note_family(g, enemy));
}

/* ---- What the enemies carry -------------------------------------------------------------------- */

/* The cells the original counts the enemies in (kept: battle.h) and finds
 * each one's rewards at. */
enum { ENEMY_AT = BATTLE_CLOCK_READS, REWARDS_AT = 0x02 };

enum { CARRIES_CHARM = 0x80, CARRIES_ITEM = 0x40 };

// $C1:FD60
/* Each enemy in the fight marked with what a steal can take from it: its
 * charm, and the item it drops. */
void fight_mark_what_enemies_carry(GameState *g)
{
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++) {
        unsigned slot = BATTLE_FIRST_ENEMY + enemy;
        set_battle_steal_marks(g, slot, 0);
        uint8_t id = battle_combatant_fighting(g, slot);
        if (id == COMBATANT_NONE)
            continue;
        /* the original finds the enemy's rewards by a multiply, and keeps
         * their place in a cell (scaffolding) */
        uint16_t rewards = effect_times(g, id, ENEMY_REWARD_BYTES);
        ORIGINAL_EFFECT(battle_leave_word(g, REWARDS_AT, rewards));
        EnemyItems items = enemy_items(g, id);
        uint8_t marks = 0;
        if (items.charm)
            marks = CARRIES_CHARM;
        if (items.dropped)
            marks |= CARRIES_ITEM;
        set_battle_steal_marks(g, slot, marks);
    }
    battle_keep_word(g, ENEMY_AT, BATTLE_ENEMIES);
}
