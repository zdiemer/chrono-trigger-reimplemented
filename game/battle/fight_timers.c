/* Battle: the timers as the fight starts (battle/timers.h says what they
 * are).
 *
 * Every timer starts stopped, each status's at its usual length, and each
 * combatant's turn timer at its first wait: shorter the faster it is, and on
 * the player's battle speed setting. The combatant whose first turn comes
 * soonest has it one tick away: every other's wait is shortened by as much,
 * so the fight starts at once. The party's gauges show their waits. */
#include "battle/fight.h"

#include "battle/arith.h"
#include "battle/battle.h"
#include "battle/combatant.h"
#include "battle/effect_parts.h"
#include "battle/rules_data.h"
#include "battle/setup.h"
#include "battle/setup_data.h"
#include "battle/timers.h"
#include "original_effects.h"
#include "vars.h"

// $FD:B0D5
/* Every timer stopped, each with ten runs left; and the enemies whose HP the
 * target cursor may show: those not of the families it hides. */
enum { RUNS = 10, HIDES_HP = 0x02 };

void fight_clear_timers(GameState *g)
{
    for (unsigned kind = 0; kind < BATTLE_TIMER_KINDS; kind++)
        for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++)
            set_battle_timer_flags_byte(g, kind, slot, 0);
    for (unsigned kind = 0; kind < BATTLE_TIMER_KINDS; kind++)
        for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++)
            set_battle_timer_runs_left_byte(g, kind, slot, RUNS);
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++)
        set_battle_enemy_hp_visible(g, enemy, 0);
    set_battle_enemy_hp_seen(g, 0);
    for (unsigned enemy = 0; enemy < BATTLE_ENEMIES; enemy++) {
        uint8_t slot = (uint8_t)(BATTLE_FIRST_ENEMY + enemy);
        if (battle_combatant_id(g, slot) != COMBATANT_NONE)
            set_battle_enemy_hp_visible(g, enemy, (combatant_stat_byte(g, slot, STAT_FAMILY) & HIDES_HP) ^ HIDES_HP);
    }
}

// $FD:B4E7
/* How long each status's timer runs, for every combatant, and each one's turn
 * wait before its speed is taken into it. */
static const uint8_t status_lengths[BATTLE_TIMER_TURN] = {
    [BATTLE_TIMER_STOP] = 60,    [BATTLE_TIMER_POISON] = 150, [BATTLE_TIMER_SLOW] = 90,     [BATTLE_TIMER_HASTE] = 110,
    [BATTLE_TIMER_EVASION] = 90, [BATTLE_TIMER_SHIELD] = 60,  [BATTLE_TIMER_6] = 60,        [BATTLE_TIMER_7] = 60,
    [BATTLE_TIMER_HP_LOSS] = 30, [BATTLE_TIMER_MP_REGAIN] = 25, [BATTLE_TIMER_BARRIER] = 60, [BATTLE_TIMER_11] = 120,
};

enum { SLOWEST_TURN = 0x69 };

void fight_set_timer_lengths(GameState *g)
{
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++) {
        for (unsigned kind = 0; kind < BATTLE_TIMER_TURN; kind++) {
            set_battle_timer_lengths_byte(g, kind, slot, status_lengths[kind]);
            set_battle_timer_counts_byte(g, kind, slot, status_lengths[kind]);
        }
        set_battle_turn_wait(g, slot, SLOWEST_TURN);
    }
}

// $FD:B7EB
/* A combatant's first turn: its speed, at most 16, takes six ticks a point off
 * the slowest wait, and the table gives it back a part by its speed and the
 * battle speed setting. The original multiplies each through its cells, the
 * speed's with the carry of the sum before it (scaffolding). */
enum { FASTEST_SPEED = 16, SETTINGS_SPEED_BITS = 0x07, SPEEDS_A_SETTING = 16, TICKS_A_SPEED_POINT = 6 };

static uint8_t first_wait(GameState *g, uint8_t slot)
{
    uint8_t speed = combatant_stat_byte(g, slot, STAT_SPEED_RATING);
    if (speed >= FASTEST_SPEED) {
        speed = FASTEST_SPEED;
        set_combatant_stat_byte(g, slot, STAT_SPEED_RATING, speed);
    }
    uint8_t setting = (uint8_t)effect_times(g, game_settings(g, SETTINGS_TIME_AND_INFO) & SETTINGS_SPEED_BITS, SPEEDS_A_SETTING);
    uint8_t at = (uint8_t)(speed - 1 + setting);
    uint8_t given_back = first_turn_wait(g, at);
    uint8_t taken = (uint8_t)effect_times(g, speed, TICKS_A_SPEED_POINT);
    ORIGINAL_EFFECT(battle_leave_multiply_carry(g, (uint8_t)(speed - 1) + setting > 0xFF));
    return (uint8_t)(SLOWEST_TURN - taken + given_back);
}

/* An enemy whose block has this bit is marked in its standing (what the mark
 * is for isn't worked out: docs/TODO.md). */
enum { MAX_MP_HIGH = STAT_MAX_MP + 1, STANDING_MARK_BIT = 0x01, STANDING_MARK = 0x40 };

/* The cells the original counts in: the combatant and its block's place, and
 * the wait the soonest turn takes off the others (kept: battle.h). */
enum { SLOT_AT = 0x02, PLACE_AT = 0x04, TAKEN_OFF_AT = BATTLE_CLOCK_READS };

static void start_turn(GameState *g, uint8_t slot, uint8_t wait)
{
    set_battle_turn_wait(g, slot, wait);
    battle_set_timer_count(g, BATTLE_TIMER_TURN, slot, wait);
}

void fight_set_first_turns(GameState *g)
{
    uint16_t place = 0;
    bool any = false;
    for (uint8_t member = 0; member < BATTLE_PARTY_SIZE; member++) {
        if (!combatant_in_fight(g, member))
            continue;
        place = battle_stat_block_place(g, member);
        any = true;
        start_turn(g, member, first_wait(g, member));
        battle_restart_timer(g, BATTLE_TIMER_TURN, member);
    }
    for (uint8_t enemy = 0; enemy < battle_enemy_count(g); enemy++) {
        uint8_t slot = (uint8_t)(BATTLE_FIRST_ENEMY + enemy);
        if (battle_combatant_id(g, slot) == COMBATANT_NONE)
            continue;
        place = battle_stat_block_place(g, slot);
        any = true;
        uint8_t wait = first_wait(g, slot);
        if (combatant_stat_byte(g, slot, MAX_MP_HIGH) & STANDING_MARK_BIT)
            set_battle_enemy_standing(g, enemy, battle_enemy_standing(g, enemy) | STANDING_MARK);
        start_turn(g, slot, wait);
        if (combatant_in_fight(g, slot))
            battle_restart_timer(g, BATTLE_TIMER_TURN, slot);
    }
    ORIGINAL_EFFECT(battle_leave_word(g, SLOT_AT, battle_enemy_count(g)));
    if (any)
        ORIGINAL_EFFECT(battle_leave_word(g, PLACE_AT, place));

    /* the soonest turn: the original compares down the slots, keeping the
     * first it finds of the least nonzero wait of anyone in the fight, and
     * the first slot's when none is less */
    uint8_t soonest = 0, least = battle_timer_counts_byte(g, BATTLE_TIMER_TURN, 0);
    for (uint8_t slot = 1; slot < BATTLE_COMBATANTS; slot++) {
        uint8_t wait = battle_timer_counts_byte(g, BATTLE_TIMER_TURN, slot);
        if (least < wait)
            continue;
        if (wait != 0 && combatant_in_fight(g, slot))
            soonest = slot;
        least = battle_timer_counts_byte(g, BATTLE_TIMER_TURN, soonest);
    }
    uint8_t taken_off = (uint8_t)(battle_timer_counts_byte(g, BATTLE_TIMER_TURN, soonest) - 1);
    battle_keep_word(g, TAKEN_OFF_AT, taken_off);
    for (uint8_t slot = 0; slot < BATTLE_COMBATANTS; slot++) {
        uint8_t wait = battle_timer_counts_byte(g, BATTLE_TIMER_TURN, slot);
        if (combatant_in_fight(g, slot) && wait != 0)
            battle_set_timer_count(g, BATTLE_TIMER_TURN, slot, (uint8_t)(wait - taken_off));
    }
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++) {
        uint8_t wait = battle_timer_counts_byte(g, BATTLE_TIMER_TURN, member);
        set_battle_gauge_wait(g, member, wait);
        set_battle_gauge_wait_full(g, member, wait);
    }
}
