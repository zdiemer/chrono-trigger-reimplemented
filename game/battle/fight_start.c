/* Battle: the fight's start, from the battle's first step, before the first
 * turn ($C1:FA8B).
 *
 * In order:
 *   - a demo battle's party put in place (battle/fight_party.c);
 *   - who is in the fight (battle/fight_enemies.c), and every enemy's stat
 *     block from its record;
 *   - every member's stat block from its character's record, Ayla's fists,
 *     the party listed again leaving out a member sitting the fight out, each
 *     member's affinities and accessory (battle/fight_party.c);
 *   - the members' tech records (battle/tech_records.c) and the battle's item
 *     list (battle/inventory.c), and the weapons the members carry;
 *   - the members' choices, targets and numbers cleared, and the timers'
 *     order, counts and cursors;
 *   - the members' stats with their equipment (battle/member_stats.c), their
 *     defence and attack;
 *   - the timers (battle/fight_timers.c), what the accessories and armor do,
 *     the turns' and the rewards' state cleared (below), the damage table,
 *     each member's low HP noted, its most HP kept for after the fight, the
 *     accessories' bonuses;
 *   - what each enemy carries to steal, the combatants' places, and the
 *     accessory that wards off confusion.
 *
 * All of it is C; battle/fight_entries.c is its machine side.
 *
 * The original works in the direct page's cells throughout and hands several
 * pieces their member or their numbers there. Most of what it leaves there is
 * overwritten before the start ends, and the pieces only it calls leave that
 * as scaffolding. Five cells hold, when it ends, what later work reads as it
 * finds it (battle.h), and each is written plainly by its last writer: $00 the
 * enemies counted as their steal marks are set (battle/fight_enemies.c), $06
 * and $08 the accessory's record's copy (below), $0A the same copy's length
 * or, where a member's accessory boosts it, the boost's amount
 * (battle/member_stats.c), and $0E the stats' spare table or the boosts'
 * (battle/member_stats.c, battle/fight_party.c). */
#include "battle/fight.h"

#include "battle/battle.h"
#include "battle/combatant.h"
#include "battle/inventory.h"
#include "battle/party_records.h"
#include "battle/rules_data.h"
#include "battle/timers.h"
#include "original_effects.h"
#include "vars.h"

/* ---- What the start clears ---------------------------------------------------------------------- */

/* A member's choice cleared: where it is in choosing, its choice, its tech's
 * aim and its target. The first two members' partners and what they chose
 * are cleared too; the third's keep what they had (the original clears
 * eighteen bytes of the records, the third's first four), until the turns'
 * state below gives every member no partners. */
static void clear_member_choice(GameState *g, unsigned member)
{
    set_battle_member_actions_state(g, member, 0);
    set_battle_member_actions_choice_flags(g, member, 0);
    set_battle_member_actions_command(g, member, 0);
    set_battle_member_actions_target(g, member, 0);
}

static void clear_member_choices(GameState *g)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        clear_member_choice(g, member);
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE - 1; member++) {
        set_battle_member_actions_spare(g, member, 0);
        set_battle_member_actions_partners(g, member, 0);
        set_battle_member_actions_what(g, member, 0);
    }
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        set_battle_member_ready(g, member, 0);
    set_battle_action_target_count(g, 0);
    for (unsigned n = 0; n < BATTLE_ACTION_TARGETS_COUNT; n++)
        set_battle_action_targets(g, n, COMBATANT_NONE);
    set_battle_action_targets_end(g, COMBATANT_NONE);
}

/* The timers' order and counts: nobody in the order, every count and state
 * clear, the turn's waits and counts none yet. */
enum { TIMER_KIND_CURSORS = 13 };

static void clear_timer_order(GameState *g)
{
    set_battle_foe_kept(g, 0);
    for (unsigned n = 0; n < BATTLE_FIGHT_START_SPARES_COUNT; n++)
        ORIGINAL_EFFECT(set_battle_fight_start_spares(g, n, 0));
    set_battle_timer_kind_bit(g, 0);
    set_battle_timer_kinds_due(g, 0);
    for (unsigned kind = 0; kind < BATTLE_TIMER_KINDS; kind++)
        for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++) {
            set_battle_timer_counts_byte(g, kind, slot, 0);
            set_battle_timer_flags_byte(g, kind, slot, 0);
        }
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++) {
        set_battle_timer_order(g, slot, COMBATANT_NONE);
        set_battle_foes(g, slot, COMBATANT_NONE);
        set_battle_turn_wait(g, slot, COMBATANT_NONE);
        set_battle_timer_counts_byte(g, BATTLE_TIMER_TURN, slot, COMBATANT_NONE);
    }
    for (unsigned n = 0; n < TIMER_KIND_CURSORS; n++)
        set_battle_timer_kind_cursors(g, n, 0);
}

// $FD:B732
/* The turns' and the rewards' state as a fight starts it: no action running
 * nor its steps, no candidates, no enemy acting nor beaten, no chosen actions,
 * nothing won, the spare turn's member none (3), and no partners. */
enum { NO_SPARE_TURN = 3 };

/* Six words of the direct page's cells the turns' work leaves behind
 * (scaffolding). */
static void clear_work_cells(GameState *g)
{
    battle_leave_word(g, 0x16, 0);
    battle_leave_word(g, 0x18, 0);
    battle_leave_word(g, 0x1A, 0);
    battle_leave_word(g, 0x1C, 0);
    battle_leave_word(g, 0x1E, 0);
    battle_leave_word(g, 0x20, 0);
}

void fight_clear_turn_state(GameState *g)
{
    ORIGINAL_EFFECT(clear_work_cells(g));
    set_battle_action_running(g, 0);
    set_battle_action_state(g, 0);
    ORIGINAL_EFFECT(set_battle_enemy_turn_spare(g, 0));
    set_battle_candidate_count(g, 0);
    set_battle_action_hit_enemy(g, 0);
    set_battle_action_hit_party(g, 0);
    set_battle_experience_won(g, 0);
    set_battle_gold_won(g, 0);
    set_battle_tech_points_won_low(g, 0);
    set_battle_tech_points_won_high(g, 0);
    set_battle_reward_flags(g, 0);
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        set_battle_rewards_levels_noted(g, member, 0);
        set_battle_rewards_techs_noted(g, member, 0);
    }
    set_battle_action_actor_bits(g, 0);
    set_battle_action_tech_hits(g, 0);
    set_battle_action_elements(g, 0);
    set_battle_action_step(g, 0);
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++)
        set_battle_chosen_actions(g, slot, 0);
    for (unsigned n = 0; n < BATTLE_ITEMS_WON_COUNT; n++)
        set_battle_items_won(g, n, 0);
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++)
        set_battle_candidates(g, slot, COMBATANT_NONE);
    set_battle_condition_target(g, COMBATANT_NONE);
    for (unsigned n = 0; n < BATTLE_CONDITION_TARGET_SPARES_COUNT; n++)
        ORIGINAL_EFFECT(set_battle_condition_target_spares(g, n, COMBATANT_NONE));
    set_battle_enemy_beaten_any(g, 0);
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++) {
        set_battle_enemy_action_chosen(g, enemy, 0);
        set_battle_enemy_beaten(g, enemy, 0);
        set_battle_enemy_beaten_counted(g, enemy, 0);
        set_battle_enemy_script_counter(g, enemy, 0);
        set_battle_status_script_seen(g, BATTLE_FIRST_ENEMY + enemy, 0);
        set_battle_enemy_script_block(g, enemy, COMBATANT_NONE);
        set_battle_enemy_actions_block(g, enemy, COMBATANT_NONE);
    }
    set_battle_damage_flags_given(g, 0);
    for (unsigned n = 0; n < BATTLE_REWARDS_TECHS_COUNT; n++)
        set_battle_rewards_techs(g, n, COMBATANT_NONE);
    set_battle_rewards_double_techs_listed(g, 0);
    set_battle_rewards_triple_tech(g, 0);
    set_battle_spare_turn_member(g, NO_SPARE_TURN);
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        set_battle_member_actions_partners(g, member, NO_PARTNERS);
}

// $FD:B223
/* Each combatant in its own place. */
void fight_number_places(GameState *g)
{
    for (uint8_t slot = 0; slot < BATTLE_COMBATANTS; slot++)
        set_battle_places(g, slot, slot);
}

/* ---- The whole of it ---------------------------------------------------------------------------- */

/* The cells the original counts in and hands its pieces their numbers in:
 * the enemy or the member it is at, the member whose techs are built, and the
 * member whose stats are added up, twice over (scaffolding); and the
 * equipment records' copies' three numbers, the last of which survive the
 * start (above). */
enum { COUNTING_AT = 0x02, TECHS_MEMBER_AT = BATTLE_MEMBER_CELL, WORN_AT = BATTLE_MEMBER_CELL,
       COPY_TO_AT = BATTLE_NUMBER_TESTED, BYTES_AT = BATTLE_STAT_OFFSET_LOW, ADDING_UP_AT = 0x10 };

/* The equipment's records each member's block takes: the weapon's attack and
 * what goes with it, the helmet's and the armor's defence and modifier, and
 * the accessory's record. */
static const struct {
    uint8_t worn_at, copy_to, bytes;
} records_taken[] = {
    { STAT_WEAPON, STAT_WEAPON_ATTACK, 6 },
    { STAT_HELMET, STAT_HELMET_DEFENCE, 3 },
    { STAT_ARMOR, STAT_ARMOR_DEFENCE, 3 },
    { STAT_ACCESSORY, STAT_ACCESSORY_RECORD, 4 },
};

enum { RECORDS_TAKEN = sizeof records_taken / sizeof records_taken[0] };

/* The original hands each copy its three numbers in the cells; only the last
 * copy's survive the start, which later work reads as it finds them
 * (above). */
static void hand_over_record(GameState *g, unsigned n)
{
    battle_leave_word(g, WORN_AT, records_taken[n].worn_at);
    battle_leave_word(g, COPY_TO_AT, records_taken[n].copy_to);
    battle_leave_word(g, BYTES_AT, records_taken[n].bytes);
}

static void keep_last_record_handed_over(GameState *g)
{
    battle_keep_word(g, WORN_AT, records_taken[RECORDS_TAKEN - 1].worn_at);
    battle_keep_word(g, COPY_TO_AT, records_taken[RECORDS_TAKEN - 1].copy_to);
    battle_keep_word(g, BYTES_AT, records_taken[RECORDS_TAKEN - 1].bytes);
}

void battle_start_fight(GameState *g)
{
    fight_load_demo_party(g);
    fight_list_combatants(g);
    for (uint8_t enemy = 0; enemy < battle_enemy_count(g); enemy++)
        fight_load_enemy(g, enemy);
    ORIGINAL_EFFECT(battle_leave_word(g, COUNTING_AT, battle_enemy_count(g)));
    fight_note_enemy_families(g);
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++) {
        fight_take_member_record(g, member);
        if (fight_member_is_ayla(g, member))
            set_combatant_stat_byte(g, member, STAT_WEAPON, fight_aylas_fists(g, combatant_stat_byte(g, member, STAT_LEVEL)));
    }
    fight_list_party(g);
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        fight_set_up_affinities(g, member);
    fight_note_accessories(g);

    battle_blank_tech_records(g);
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++) {
        ORIGINAL_EFFECT(battle_leave_word(g, TECHS_MEMBER_AT, member));
        battle_set_up_single_techs(g, member);
        battle_set_up_double_techs(g, member);
        battle_find_triple_tech(g, member);
    }
    battle_list_usable_items(g);
    fight_note_weapons(g);

    clear_member_choices(g);
    battle_clear_numbers_shown(g);
    clear_timer_order(g);

    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++) {
        ORIGINAL_EFFECT(battle_leave_word(g, COUNTING_AT, member));
        battle_equip_member(g, member);
    }
    for (unsigned n = 0; n < RECORDS_TAKEN; n++) {
        ORIGINAL_EFFECT(hand_over_record(g, n));
        battle_take_equipment_records(g, records_taken[n].worn_at, records_taken[n].copy_to, records_taken[n].bytes);
    }
    keep_last_record_handed_over(g);
    ORIGINAL_EFFECT(set_battle_fight_start_mark(g, 0));
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        fight_add_up_member_stats(g, member);
    ORIGINAL_EFFECT(battle_leave_word(g, ADDING_UP_AT, BATTLE_PARTY_SIZE * 2));

    fight_clear_timers(g);
    fight_set_timer_lengths(g);
    fight_set_first_turns(g);
    fight_clear_enemy_animations(g);
    fight_apply_equipment_effects(g);
    fight_clear_turn_state(g);
    battle_clear_damage_table(g);
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        combatant_note_low_hp(g, member);
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        set_battle_party_max_hp_kept(g, member, battle_stats_max_hp(g, member));
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++)
        fight_apply_member_bonuses(g, member);
    set_battle_timer_tick_slot(g, 0);
    ORIGINAL_EFFECT(set_battle_timer_tick_spare(g, 0));
    set_battle_timer_tick_timer(g, 0);
    fight_mark_what_enemies_carry(g);
    fight_number_places(g);
    fight_ward_off_confusion(g);
}
