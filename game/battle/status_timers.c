/* Battle: what happens when a combatant's status timer runs out.
 *
 * A status that lasts runs a timer of its own (battle/timers.h). Each time the
 * main loop finds one run out it sees to it by the timer's kind: the timer
 * starts again, and the status does what it does every so often -- poison
 * hurts, one status takes a point of HP, another brings back some MP -- or
 * counts towards wearing off, which it does after the tenth time (the count
 * then starts again at 10 for the next time the status comes). What a status
 * does shows over the combatant as a number, through the action's showing,
 * which is still the original's (battle/enemy_turn.c's start of it).
 *
 * The statuses by their timer: stop, poison, slow, haste, evading much better,
 * the shield, the point of HP lost, the MP regained and the barrier; the
 * other three timers' kinds do nothing when they run out. */
#include "battle/status_timers.h"

#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/effect_work.h"
#include "battle/entries.h"
#include "battle/arith.h"
#include "battle/timers.h"
#include "charge.h"
#include "original_effects.h"
#include "port/native.h"
#include "vars.h"

/* What a kind of timer running out does. */
typedef enum {
    NOTHING_HAPPENS,
    WEARS_OFF,         /* the tenth time, the status (if it names one) goes and the timer stops */
    POISON_HURTS,      /* poison takes a share of the combatant's most HP, shown */
    LOSES_A_POINT,     /* a point of HP lost, not shown as a number */
    REGAINS_MAGIC,     /* the tenth time, 5 MP back, shown */
} TimerEffect;

static const struct {
    uint16_t handler;  /* the original's handler, in bank $C1 */
    TimerEffect effect;
    uint16_t status;   /* the status byte a status that wears off is in, and its bit */
    uint8_t bit;
} timed_statuses[BATTLE_TIMER_TURN] = {
    [BATTLE_TIMER_STOP] = { 0x8876, WEARS_OFF, STAT_STATUS_1, STATUS_STOP },
    [BATTLE_TIMER_STATUS_1] = { 0x88C5, POISON_HURTS, STAT_STATUS_1, STATUS_POISON },
    /* the slow's timer runs out ten times and stops, and the slow stays on:
     * the original's, which clears no bit here */
    [BATTLE_TIMER_STATUS_2] = { 0x8975, WEARS_OFF, 0, 0 },
    [BATTLE_TIMER_STATUS_3] = { 0x89B9, WEARS_OFF, STAT_STATUS_3, STATUS3_HASTE },
    [BATTLE_TIMER_STATUS_4] = { 0x8A05, WEARS_OFF, STAT_STATUS_3, STATUS3_EVADES_MUCH_BETTER },
    [BATTLE_TIMER_STATUS_5] = { 0x8A51, WEARS_OFF, STAT_STATUS_4, STATUS4_SHIELD },
    [BATTLE_TIMER_STATUS_6] = { 0x8A9D, NOTHING_HAPPENS, 0, 0 },
    [BATTLE_TIMER_STATUS_7] = { 0x8A9E, NOTHING_HAPPENS, 0, 0 },
    [BATTLE_TIMER_STATUS_8] = { 0x8A9F, LOSES_A_POINT, STAT_STATUS_1, STATUS_LOSES_HP },
    [BATTLE_TIMER_STATUS_9] = { 0x8B10, REGAINS_MAGIC, STAT_STATUS_4, STATUS4_REGAINS_MP },
    [BATTLE_TIMER_STATUS_10] = { 0x8BB9, WEARS_OFF, STAT_STATUS_4, STATUS4_BARRIER },
    [BATTLE_TIMER_STATUS_11] = { 0x8C08, NOTHING_HAPPENS, 0, 0 },
};

enum { RUNS_BEFORE_WEARING_OFF = 10, MAGIC_REGAINED = 5, POINT_OF_HP = 1 };

static uint8_t stat(const GameState *g, uint8_t slot, uint16_t offset) { return combatant_stat_byte(g, slot, offset); }

static bool knocked_out(const GameState *g, uint8_t slot)
{
    return (stat(g, slot, STAT_CONDITION) & CONDITION_KNOCKED_OUT) != 0;
}

static bool hasted(const GameState *g, uint8_t slot)
{
    return ((stat(g, slot, STAT_STATUS_3) | stat(g, slot, STAT_KEPT_STATUS_3)) & STATUS3_HASTE) != 0;
}

static bool slowed(const GameState *g, uint8_t slot) { return (stat(g, slot, STAT_STATUS_1) & STATUS_SLOW) != 0; }

static uint8_t length_of(const GameState *g, BattleTimer kind, uint8_t slot)
{
    return battle_timer_lengths(g, (unsigned)kind * BATTLE_COMBATANTS + slot);
}

/* The timer starts again, for its length. */
static void run_again(GameState *g, BattleTimer kind, uint8_t slot, uint8_t length)
{
    battle_restart_timer(g, kind, slot);
    set_battle_timer_counts_byte(g, kind, slot, length);
}

/* A timer that runs as long as a turn's wait: half as long hasted, twice
 * slowed (the slow's doubling of the whole length, in a byte). The MP's timer
 * asks only whether the combatant keeps haste whatever happens, not whether it
 * has it: the original loads the status byte and drops it. */
static uint8_t paced_length(const GameState *g, BattleTimer kind, uint8_t slot, bool hasted_now)
{
    uint8_t length = length_of(g, kind, slot);
    uint8_t paced = hasted_now ? (uint8_t)(length >> 1) : length;
    return slowed(g, slot) ? (uint8_t)(length << 1) : paced;
}

/* The carry the original records a point of HP with, which the multiply
 * leaves in its cell (scaffolding): the bit its pacing shifted out of the
 * length last (slowed, the top; hasted, the bottom), or none, which finding
 * the stat block's place leaves. */
static bool paced_carry(const GameState *g, BattleTimer kind, uint8_t slot)
{
    uint8_t length = length_of(g, kind, slot);
    if (slowed(g, slot))
        return (length & 0x80) != 0;
    return hasted(g, slot) && (length & 0x01) != 0;
}

/* One more run towards wearing off: the tenth ends it. */
static bool wears_off_now(GameState *g, BattleTimer kind, uint8_t slot)
{
    uint8_t left = (uint8_t)(battle_timer_runs_left_byte(g, kind, slot) - 1);
    set_battle_timer_runs_left_byte(g, kind, slot, left);
    if (left != 0)
        return false;
    set_battle_timer_runs_left_byte(g, kind, slot, RUNS_BEFORE_WEARING_OFF);
    return true;
}

static void lose_status(GameState *g, BattleTimer kind, uint8_t slot)
{
    uint16_t at = timed_statuses[kind].status;
    if (at != 0)
        set_combatant_stat_byte(g, slot, at, (uint8_t)(stat(g, slot, at) & (uint8_t)~timed_statuses[kind].bit));
    battle_stop_timer(g, kind, slot);
}

// $C1:88C5
/* Poison takes the combatant's most HP over half its hit stat and 8 (in a
 * byte). The original divides the long arithmetic's leftover high word with
 * the most HP, and by a divisor whose high byte is whatever that arithmetic
 * left in its cell: both as the game plays them. */
static uint16_t poison_damage(GameState *g, uint8_t slot)
{
    uint8_t hit = stat(g, slot, STAT_HIT);
    uint16_t divisor = (uint16_t)(battle_wide_math_second_high(g) << 8 | (uint8_t)((hit >> 1) + 8));
    return (uint16_t)battle_divide_leftover(g, battle_stats_max_hp(g, slot), divisor).quotient;
}

/* What the combatant's status did that the showing shows. */
typedef enum { NOTHING_SHOWN, NUMBER_SHOWN, EFFECT_APPLIED } Shown;

/* The handlers of the statuses that act leave the combatant's stat block's
 * place in $10, as the original's do: scaffolding. */
enum { STAT_BLOCK_PLACE_CELL = 0x10 };

/* The part of the handler before any showing; what it leaves to show. */
static Shown timer_ran_out(GameState *g, BattleTimer kind, uint8_t slot)
{
    const TimerEffect effect = timed_statuses[kind].effect;
    if (effect == POISON_HURTS || effect == LOSES_A_POINT || effect == REGAINS_MAGIC)
        ORIGINAL_EFFECT(battle_leave_word(g, STAT_BLOCK_PLACE_CELL, (uint16_t)(slot * BATTLE_STAT_BLOCK_BYTES)));
    switch (effect) {
    case NOTHING_HAPPENS:
        return NOTHING_SHOWN;
    case WEARS_OFF:
        run_again(g, kind, slot, length_of(g, kind, slot));
        battle_timer_seen_to(g, kind);
        if (wears_off_now(g, kind, slot))
            lose_status(g, kind, slot);
        return NOTHING_SHOWN;
    case POISON_HURTS:
        /* seen to after the showing, which the showing's code may read */
        if (knocked_out(g, slot) || !(stat(g, slot, STAT_STATUS_1) & STATUS_POISON))
            return NOTHING_SHOWN;
        run_again(g, kind, slot, (uint8_t)(length_of(g, kind, slot) + stat(g, slot, STAT_HIT)));
        effect_show_status_damage(g, slot, poison_damage(g, slot));
        return NUMBER_SHOWN;
    case LOSES_A_POINT:
        run_again(g, kind, slot, paced_length(g, kind, slot, hasted(g, slot)));
        battle_timer_seen_to(g, kind);
        if (!(stat(g, slot, STAT_STATUS_1) & STATUS_LOSES_HP))
            return NOTHING_SHOWN;
        effect_record_status_damage(g, slot, POINT_OF_HP);
        ORIGINAL_EFFECT(battle_leave_multiply_carry(g, paced_carry(g, kind, slot)));
        return EFFECT_APPLIED;
    case REGAINS_MAGIC:
    default:
        run_again(g, kind, slot,
                  paced_length(g, kind, slot, (stat(g, slot, STAT_KEPT_STATUS_3) & STATUS3_HASTE) != 0));
        battle_timer_seen_to(g, kind);
        if (!wears_off_now(g, kind, slot) || knocked_out(g, slot))
            return NOTHING_SHOWN;
        if (!((stat(g, slot, STAT_STATUS_4) | stat(g, slot, STAT_KEPT_STATUS_4)) & STATUS4_REGAINS_MP))
            return NOTHING_SHOWN;
        effect_show_magic_regained(g, slot, MAGIC_REGAINED);
        return NUMBER_SHOWN;
    }
}

// $C1:895B
/* A status's number shown as an action of its own: the showing's number $7F,
 * as a tech (kind 2) of nobody's, aimed at the first battle slot's bit. */
enum { STATUS_SHOWING = 0x7F, SHOWN_AS_TECH = 2, FIRST_SLOT_BIT = 0x8000 };

static void set_up_status_showing(GameState *g)
{
    set_battle_action_what(g, STATUS_SHOWING);
    set_battle_action_actor(g, 0);
    set_battle_action_type(g, SHOWN_AS_TECH);
    set_battle_action_extra(g, 0);
    set_battle_action_target_bits(g, FIRST_SLOT_BIT);
}

/* ---- The entries ----------------------------------------------------------------------- */

/* The action's showing, run from $C1:895B's call ($C1:AC57, a sibling), and
 * the effect's start for a point of HP ($C1:EC7F, the original's; it reads
 * nothing of the registers). */
enum {
    SHOWING_CALL = 0xC18971, EFFECT_START_CALL = 0xC18B0C, EFFECT_START = 0xC1EC7F,
    POISON_SHOWS_RETURN = 0x8947, MAGIC_SHOWS_RETURN = 0x8BB3,   /* the calls of $C1:895B, their last bytes */
};

bool battle_see_to_run_out_timer(Snes *s, BattleTimer kind)
{
    GameState *g = state_of(s);
    uint8_t slot = battle_timer_found_run_out(g, kind);
    Shown shown = timer_ran_out(g, kind, slot);
    if (shown == NUMBER_SHOWN) {
        set_up_status_showing(g);
        /* the showing is called from $C1:895B, a subroutine of the handler's,
         * whose return is on the stack under its call */
        m_push16(s, timed_statuses[kind].effect == POISON_HURTS ? POISON_SHOWS_RETURN : MAGIC_SHOWS_RETURN);
        if (!battle_run(s, SHOWING_CALL, 0xC1AC57, battle_start_action_showing))
            return false;
        m_pull16(s);
        TIMED_AS(g, 0xFDACEE, battle_clear_numbers_shown(g));
    } else if (shown == EFFECT_APPLIED) {
        if (!battle_call(s, EFFECT_START_CALL, EFFECT_START, DC_A | DC_X | DC_Y | DC_FLAGS))
            return false;
    }
    if (timed_statuses[kind].effect == POISON_HURTS)
        battle_timer_seen_to(g, kind);
    return true;
}


/* The handlers work on the combatant the search found, which must be one. */
static bool handler_ready(Snes *s, BattleTimer kind)
{
    vars_peek(true);
    bool names = battle_names_combatant(battle_timer_found_run_out(state_of(s), kind));
    vars_peek(false);
    return battle_on_page(s) && names;
}

static void handler_entry(Snes *s, BattleTimer kind)
{
    if (battle_see_to_run_out_timer(s, kind))
        m_rts(s);
}

#define HANDLER(name, kind) \
    void name(Snes *s) { handler_entry(s, kind); } \
    bool name##_ready(Snes *s) { return handler_ready(s, kind); }
HANDLER(battle_stop_timer_ran_out, BATTLE_TIMER_STOP)
HANDLER(battle_poison_timer_ran_out, BATTLE_TIMER_STATUS_1)
HANDLER(battle_slow_timer_ran_out, BATTLE_TIMER_STATUS_2)
HANDLER(battle_haste_timer_ran_out, BATTLE_TIMER_STATUS_3)
HANDLER(battle_evasion_timer_ran_out, BATTLE_TIMER_STATUS_4)
HANDLER(battle_shield_timer_ran_out, BATTLE_TIMER_STATUS_5)
HANDLER(battle_timer_6_ran_out, BATTLE_TIMER_STATUS_6)
HANDLER(battle_timer_7_ran_out, BATTLE_TIMER_STATUS_7)
HANDLER(battle_hp_loss_timer_ran_out, BATTLE_TIMER_STATUS_8)
HANDLER(battle_mp_regain_timer_ran_out, BATTLE_TIMER_STATUS_9)
HANDLER(battle_barrier_timer_ran_out, BATTLE_TIMER_STATUS_10)
HANDLER(battle_timer_11_ran_out, BATTLE_TIMER_STATUS_11)

void (*const battle_status_timer_entries[BATTLE_TIMER_TURN])(Snes *) = {
    battle_stop_timer_ran_out, battle_poison_timer_ran_out, battle_slow_timer_ran_out, battle_haste_timer_ran_out,
    battle_evasion_timer_ran_out, battle_shield_timer_ran_out, battle_timer_6_ran_out, battle_timer_7_ran_out,
    battle_hp_loss_timer_ran_out, battle_mp_regain_timer_ran_out, battle_barrier_timer_ran_out,
    battle_timer_11_ran_out,
};

uint32_t battle_status_timer_handler(BattleTimer kind) { return 0xC10000u | timed_statuses[kind].handler; }
