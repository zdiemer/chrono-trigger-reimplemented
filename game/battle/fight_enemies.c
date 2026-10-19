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
#include "battle/setup.h"
#include "battle/setup_data.h"
#include "original_effects.h"
#include "vars.h"

/* ---- Who is in the fight ---------------------------------------------------------------------- */

/* A placement names no enemy where its word's top bit is set. */
static bool placement_empty(uint16_t placed) { return (placed & 0x8000) != 0; }

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
        combatant_list_nobody(g, slot);
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++)
        set_battle_enemy_standing(g, enemy, 0);
    uint8_t members = 0;
    for (unsigned slot = 0; slot < BATTLE_PARTY_SIZE; slot++) {
        uint8_t character = party_members(g, slot);
        if (!(character & BATTLE_EMPTY_SLOT)) {
            combatant_list_as(g, slot, character);
            members++;
        }
    }
    set_battle_party_fighting_count(g, members);
    set_battle_party_defeated(g, 0);
    for (unsigned character = 0; character < SETUP_CHARACTERS; character++)
        set_battle_character_slot(g, character, COMBATANT_NONE);
    for (uint8_t slot = 0; slot < BATTLE_PARTY_SIZE; slot++) {
        uint8_t character = battle_combatant_fighting(g, slot);
        if (character != COMBATANT_NONE)
            set_battle_character_slot(g, character, slot);
    }
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++) {
        uint16_t placed = battle_enemy_placement_enemy(g, enemy);
        if (placement_empty(placed))
            continue;
        unsigned slot = BATTLE_FIRST_ENEMY + enemy;
        combatant_list_as(g, slot, (uint8_t)placed);
        if (battle_enemy_placement_kind(g, enemy) & ENEMY_IN_RESERVE) {
            set_battle_combatant_fighting(g, slot, COMBATANT_NONE);
            set_battle_enemy_standing(g, enemy, battle_enemy_standing(g, enemy) | ENEMY_IN_RESERVE);
        }
    }
    set_battle_enemy_count(g, BATTLE_ENEMIES);
}

/* ---- An enemy's stat block --------------------------------------------------------------------- */

/* The cells the original leaves: the id of the enemy it loaded, and its id
 * times eight with the count of the record's last part over its low byte
 * (kept, as later work reads it as it finds it: battle.h; the enemies'
 * loader is also called when one comes into a fight already going). */
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
    EnemyStatsRecord record = enemy_stats_record(g, id);
    fight_clear_enemy_block(g, enemy);
    set_combatant_stat_byte(g, slot, STAT_ID, id);
    set_battle_stats_hp(g, slot, record.hp);
    set_battle_stats_max_hp(g, slot, record.hp);
    set_combatant_stat_byte(g, slot, STAT_LEVEL, record.level);
    for (unsigned n = 0; n < ENEMY_DEFENCES; n++)
        set_combatant_stat_byte(g, slot, (uint16_t)(STAT_SHRUGS_OFF + n), record.defences[n]);
    /* it starts with the statuses it keeps */
    set_combatant_stat_byte(g, slot, STAT_STATUS_3, combatant_stat_byte(g, slot, STAT_KEPT_STATUS_3));
    set_combatant_stat_byte(g, slot, STAT_STATUS_4, combatant_stat_byte(g, slot, STAT_KEPT_STATUS_4));
    for (unsigned n = 0; n < ENEMY_STATS; n++)
        set_combatant_stat_byte(g, slot, (uint16_t)(STAT_STAMINA + n), record.stats[n]);
    for (unsigned n = 0; n < ENEMY_KIND_BYTES; n++)
        set_combatant_stat_byte(g, slot, (uint16_t)(STAT_KIND + n), record.kind[n]);
    ORIGINAL_EFFECT(battle_leave_word(g, ID_AT, id));
    battle_keep_word(g, COUNT_AT, (uint16_t)((id * 8) & 0xFF00) | ENEMY_KIND_BYTES);
}

// $FD:B121
/* The low two bits of each enemy's family, noted: the first has a beaten
 * enemy change the battlefield as it leaves (BATTLE_FAMILY_CHANGES_THE_BATTLEFIELD,
 * battle.h). */
enum { FAMILY_BITS = 0x03 };

static void note_family(GameState *g, unsigned enemy)
{
    set_battle_enemy_family_bits(g, enemy, combatant_stat_byte(g, (uint8_t)(BATTLE_FIRST_ENEMY + enemy), STAT_FAMILY) & FAMILY_BITS);
}

void fight_note_enemy_families(GameState *g)
{
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++)
        note_family(g, enemy);
}

/* ---- What the enemies carry -------------------------------------------------------------------- */

/* The cells the original counts the enemies in and finds each one's rewards
 * at. The count is the last the fight's start writes to its cell, which later
 * work reads as it finds it (battle.h), so it is kept plainly. */
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
        uint16_t rewards = (uint16_t)battle_multiply_words(g, id, ENEMY_REWARD_BYTES);
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
