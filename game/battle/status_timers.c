/* Battle: what happens when a combatant's status timer runs out.
 *
 * A status that lasts runs a timer of its own (battle/timers.h). Each time the
 * main loop finds one run out it sees to it by the timer's kind: the timer
 * starts again, and the status does what it does every so often -- poison
 * hurts, one status takes a point of HP, another brings back some MP -- or
 * counts towards wearing off, which it does on the tenth run (the count then
 * starts again at 10 for the next time the status comes). What a status does
 * shows over the combatant as a number, through the action's showing, which
 * is still the original's (battle/enemy_turn.c's start of it).
 *
 * The search can take the order's empty place for a combatant (battle/
 * timers.h): the timer it saw to is then nobody's, past its kind's row, and
 * so is everything the status's handler touches, a stat block far past the
 * combatants' included. The original works on it as on any slot, and so does
 * the port: that is what the game does with a timer of nobody's. */
#include "battle/status_timers.h"

#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/effect_work.h"
#include "battle/enemy_actions.h"
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
    WEARS_OFF,         /* on the tenth run the status goes and the timer stops */
    POISON_HURTS,      /* poison takes a share of the combatant's most HP, shown */
    LOSES_A_POINT,     /* a point of HP lost, not shown as a number */
    REGAINS_MAGIC,     /* on the tenth run, 5 MP back, shown */
} TimerEffect;

/* Each kind: what it does, and the status it times (its byte and bit), which
 * the status's own doing asks after and wearing off takes away -- but the
 * slow's, whose wearing off leaves the slow on (the original's). */
static const struct {
    TimerEffect effect;
    uint16_t status;
    uint8_t bit;
    bool wearing_off_clears;
} timed_statuses[BATTLE_TIMER_TURN] = {
    [BATTLE_TIMER_STOP] = { WEARS_OFF, STAT_STATUS_1, STATUS_STOP, true },
    [BATTLE_TIMER_POISON] = { POISON_HURTS, STAT_STATUS_1, STATUS_POISON, false },
    [BATTLE_TIMER_SLOW] = { WEARS_OFF, STAT_STATUS_1, STATUS_SLOW, false },
    [BATTLE_TIMER_HASTE] = { WEARS_OFF, STAT_STATUS_3, STATUS3_HASTE, true },
    [BATTLE_TIMER_EVASION] = { WEARS_OFF, STAT_STATUS_3, STATUS3_EVADES_MUCH_BETTER, true },
    [BATTLE_TIMER_SHIELD] = { WEARS_OFF, STAT_STATUS_4, STATUS4_SHIELD, true },
    [BATTLE_TIMER_6] = { NOTHING_HAPPENS, 0, 0, false },
    [BATTLE_TIMER_7] = { NOTHING_HAPPENS, 0, 0, false },
    [BATTLE_TIMER_HP_LOSS] = { LOSES_A_POINT, STAT_STATUS_1, STATUS_LOSES_HP, false },
    [BATTLE_TIMER_MP_REGAIN] = { REGAINS_MAGIC, STAT_STATUS_4, STATUS4_REGAINS_MP, false },
    [BATTLE_TIMER_BARRIER] = { WEARS_OFF, STAT_STATUS_4, STATUS4_BARRIER, true },
    [BATTLE_TIMER_11] = { NOTHING_HAPPENS, 0, 0, false },
};

enum { RUNS_PER_CYCLE = 10, MAGIC_REGAINED = 5, POINT_OF_HP = 1 };

static uint8_t stat(const GameState *g, uint8_t slot, uint16_t offset) { return combatant_stat_byte(g, slot, offset); }

static bool knocked_out(const GameState *g, uint8_t slot)
{
    return (stat(g, slot, STAT_CONDITION) & CONDITION_KNOCKED_OUT) != 0;
}

/* Whether the combatant has the status its timer times: its own bit, and for
 * the third and fourth bytes the ones it keeps whatever happens. */
static bool has_timed_status(const GameState *g, BattleTimer kind, uint8_t slot)
{
    uint16_t at = timed_statuses[kind].status;
    uint8_t bits = stat(g, slot, at);
    if (at == STAT_STATUS_3)
        bits |= stat(g, slot, STAT_KEPT_STATUS_3);
    else if (at == STAT_STATUS_4)
        bits |= stat(g, slot, STAT_KEPT_STATUS_4);
    return (bits & timed_statuses[kind].bit) != 0;
}

static bool hasted(const GameState *g, uint8_t slot)
{
    return ((stat(g, slot, STAT_STATUS_3) | stat(g, slot, STAT_KEPT_STATUS_3)) & STATUS3_HASTE) != 0;
}

static bool slowed(const GameState *g, uint8_t slot) { return (stat(g, slot, STAT_STATUS_1) & STATUS_SLOW) != 0; }

/* A timer that runs as long as a turn's wait: half as long hasted, twice
 * slowed (the slow's doubling of the whole length, in a byte). The MP's timer
 * asks only whether the combatant keeps haste whatever happens, not whether it
 * has it: the original loads the status byte and drops it. */
static uint8_t paced_length(const GameState *g, BattleTimer kind, uint8_t slot, bool hasted_now)
{
    uint8_t length = battle_timer_length(g, kind, slot);
    uint8_t paced = hasted_now ? (uint8_t)(length >> 1) : length;
    return slowed(g, slot) ? (uint8_t)(length << 1) : paced;
}

/* The carry the original records a point of HP with, which the multiply
 * leaves in its cell (scaffolding): the bit its pacing shifted out of the
 * length last (slowed, the top; hasted, the bottom), or none, which finding
 * the stat block's place leaves. */
static bool paced_carry(const GameState *g, BattleTimer kind, uint8_t slot)
{
    uint8_t length = battle_timer_length(g, kind, slot);
    if (slowed(g, slot))
        return (length & 0x80) != 0;
    return hasted(g, slot) && (length & 0x01) != 0;
}

/* The runs of a timer count in cycles of ten: true on the tenth, when the
 * count starts again. */
static uint8_t runs_left(const GameState *g, BattleTimer kind, uint8_t slot)
{
    return slot < BATTLE_COMBATANTS ? battle_timer_runs_left_byte(g, kind, slot)
                                    : battle_timer_runs_left_byte_unchecked(g, kind, slot);
}
static void set_runs_left(GameState *g, BattleTimer kind, uint8_t slot, uint8_t left)
{
    if (slot < BATTLE_COMBATANTS)
        set_battle_timer_runs_left_byte(g, kind, slot, left);
    else
        set_battle_timer_runs_left_byte_unchecked(g, kind, slot, left);
}

static bool tenth_run(GameState *g, BattleTimer kind, uint8_t slot)
{
    uint8_t left = (uint8_t)(runs_left(g, kind, slot) - 1);
    bool tenth = left == 0;
    set_runs_left(g, kind, slot, tenth ? RUNS_PER_CYCLE : left);
    return tenth;
}

// $C1:88C5
/* Poison takes the combatant's most HP over half its hit stat and 8 (in a
 * byte). The original divides the long arithmetic's leftover high word with
 * the most HP, and by a divisor whose high byte is what that arithmetic left
 * in its cell, as the game plays them: both are 0 after the small multiplies
 * the battle does between, but a large product left there makes poison's
 * damage the low word of a huge quotient, and a divisor left there, almost
 * nothing. */
static uint16_t poison_damage(GameState *g, uint8_t slot)
{
    uint8_t hit = stat(g, slot, STAT_HIT);
    uint16_t divisor = (uint16_t)(battle_wide_math_second_high(g) << 8 | (uint8_t)((hit >> 1) + 8));
    uint16_t most = (uint16_t)(stat(g, slot, STAT_MAX_HP) | stat(g, slot, STAT_MAX_HP + 1) << 8);
    return (uint16_t)battle_divide_leftover(g, most, divisor).quotient;
}

/* What the status's doing leaves to show. */
typedef enum { NOTHING_TO_SHOW, NUMBER_TO_SHOW, DAMAGE_TO_APPLY } TimerOutcome;

/* The handlers of the statuses that act leave the combatant's stat block's
 * place in $10, as the original's do: scaffolding. */
enum { STAT_BLOCK_PLACE_CELL = 0x10 };

/* The timer run out seen to, up to what the showing shows. Poison is seen to
 * after its showing (see_to_run_out_timer), as the original's is: the
 * showing's code runs with its due bit still set. */
static TimerOutcome timer_ran_out(GameState *g, BattleTimer kind, uint8_t slot)
{
    const TimerEffect effect = timed_statuses[kind].effect;
    if (effect == POISON_HURTS || effect == LOSES_A_POINT || effect == REGAINS_MAGIC)
        ORIGINAL_EFFECT(battle_leave_word(g, STAT_BLOCK_PLACE_CELL, (uint16_t)(slot * BATTLE_STAT_BLOCK_BYTES)));
    switch (effect) {
    case NOTHING_HAPPENS:
        return NOTHING_TO_SHOW;
    case WEARS_OFF: {
        uint8_t length = battle_timer_length(g, kind, slot);
        battle_timer_seen_to(g, kind);
        if (!tenth_run(g, kind, slot)) {
            battle_start_timer(g, kind, slot, length);
            return NOTHING_TO_SHOW;
        }
        battle_set_timer_count(g, kind, slot, length);
        if (timed_statuses[kind].wearing_off_clears)
            set_combatant_stat_byte(g, slot, timed_statuses[kind].status,
                                    (uint8_t)(stat(g, slot, timed_statuses[kind].status) & (uint8_t)~timed_statuses[kind].bit));
        battle_stop_timer(g, kind, slot);
        return NOTHING_TO_SHOW;
    }
    case POISON_HURTS:
        if (knocked_out(g, slot) || !has_timed_status(g, kind, slot))
            return NOTHING_TO_SHOW;
        battle_start_timer(g, kind, slot, (uint8_t)(battle_timer_length(g, kind, slot) + stat(g, slot, STAT_HIT)));
        effect_show_status_damage(g, slot, poison_damage(g, slot));
        return NUMBER_TO_SHOW;
    case LOSES_A_POINT:
        battle_start_timer(g, kind, slot, paced_length(g, kind, slot, hasted(g, slot)));
        battle_timer_seen_to(g, kind);
        if (!has_timed_status(g, kind, slot))
            return NOTHING_TO_SHOW;
        effect_record_status_damage(g, slot, POINT_OF_HP);
        ORIGINAL_EFFECT(battle_leave_multiply_carry(g, paced_carry(g, kind, slot)));
        return DAMAGE_TO_APPLY;
    case REGAINS_MAGIC:
    default:
        battle_start_timer(g, kind, slot,
                           paced_length(g, kind, slot, (stat(g, slot, STAT_KEPT_STATUS_3) & STATUS3_HASTE) != 0));
        battle_timer_seen_to(g, kind);
        if (!tenth_run(g, kind, slot) || knocked_out(g, slot) || !has_timed_status(g, kind, slot))
            return NOTHING_TO_SHOW;
        effect_show_magic_regained(g, slot, MAGIC_REGAINED);
        return NUMBER_TO_SHOW;
    }
}

// $C1:895B
/* A status's number shown as an action of its own: the showing numbered
 * `showing`, as a tech of the first slot's, aimed at the first slot's bit. */
enum { FIRST_SLOT_BIT = 0x8000, STATUS_NUMBER_SHOWING = 0x7F };

static void set_up_status_showing(GameState *g, uint8_t showing)
{
    set_battle_action_what(g, showing);
    set_battle_action_actor(g, 0);
    set_battle_action_type(g, ACTION_TECH);
    set_battle_action_extra(g, 0);
    set_battle_action_target_bits(g, FIRST_SLOT_BIT);
}

/* ---- The entries ----------------------------------------------------------------------- */

/* The showing a status's number asks for ($C1:AC57, a sibling), from
 * $C1:895B's call; the effect's start for a point of HP ($C1:EC7F, the
 * original's; it reads nothing of the registers). */
enum { SHOWING_CALL = 0xC18971, EFFECT_START_CALL = 0xC18B0C, EFFECT_START = 0xC1EC7F };

void battle_show_status_number(Snes *s)
{
    set_up_status_showing(state_of(s), (uint8_t)s->cpu.a);
    if (battle_run(s, SHOWING_CALL, 0xC1AC57, battle_start_action_showing))
        m_rts(s);
}

/* The handlers' calls of $C1:895B, by the kind whose number they show. */
enum { POISON_SHOWS_CALL = 0xC18945, MAGIC_SHOWS_CALL = 0xC18BB1, SHOW_NUMBER = 0xC1895B };

static bool see_to_run_out_timer(Snes *s, BattleTimer kind)
{
    GameState *g = state_of(s);
    uint8_t slot = battle_timer_found_run_out(g, kind);
    TimerOutcome outcome = timer_ran_out(g, kind, slot);
    if (outcome == NUMBER_TO_SHOW) {
        m_set_a8(s, STATUS_NUMBER_SHOWING);
        uint32_t site = kind == BATTLE_TIMER_POISON ? POISON_SHOWS_CALL : MAGIC_SHOWS_CALL;
        if (!battle_run(s, site, SHOW_NUMBER, battle_show_status_number))
            return false;
        TIMED_AS(g, 0xFDACEE, battle_clear_numbers_shown(g));
    } else if (outcome == DAMAGE_TO_APPLY) {
        if (!battle_call(s, EFFECT_START_CALL, EFFECT_START, DC_A | DC_X | DC_Y | DC_FLAGS))
            return false;
    }
    if (timed_statuses[kind].effect == POISON_HURTS)
        battle_timer_seen_to(g, kind);
    return true;
}

static void handler_entry(Snes *s, BattleTimer kind)
{
    if (see_to_run_out_timer(s, kind))
        m_rts(s);
}

#define HANDLER(name, kind) \
    void name(Snes *s) { handler_entry(s, kind); }
HANDLER(battle_stop_timer_ran_out, BATTLE_TIMER_STOP)
HANDLER(battle_poison_timer_ran_out, BATTLE_TIMER_POISON)
HANDLER(battle_slow_timer_ran_out, BATTLE_TIMER_SLOW)
HANDLER(battle_haste_timer_ran_out, BATTLE_TIMER_HASTE)
HANDLER(battle_evasion_timer_ran_out, BATTLE_TIMER_EVASION)
HANDLER(battle_shield_timer_ran_out, BATTLE_TIMER_SHIELD)
HANDLER(battle_timer_6_ran_out, BATTLE_TIMER_6)
HANDLER(battle_timer_7_ran_out, BATTLE_TIMER_7)
HANDLER(battle_hp_loss_timer_ran_out, BATTLE_TIMER_HP_LOSS)
HANDLER(battle_mp_regain_timer_ran_out, BATTLE_TIMER_MP_REGAIN)
HANDLER(battle_barrier_timer_ran_out, BATTLE_TIMER_BARRIER)
HANDLER(battle_timer_11_ran_out, BATTLE_TIMER_11)

/* The original's handlers (its table at $C1:B92D), by kind, and their entries. */
static const struct {
    uint16_t routine;
    void (*entry)(Snes *);
} handlers[BATTLE_TIMER_TURN] = {
    [BATTLE_TIMER_STOP] = { 0x8876, battle_stop_timer_ran_out },
    [BATTLE_TIMER_POISON] = { 0x88C5, battle_poison_timer_ran_out },
    [BATTLE_TIMER_SLOW] = { 0x8975, battle_slow_timer_ran_out },
    [BATTLE_TIMER_HASTE] = { 0x89B9, battle_haste_timer_ran_out },
    [BATTLE_TIMER_EVASION] = { 0x8A05, battle_evasion_timer_ran_out },
    [BATTLE_TIMER_SHIELD] = { 0x8A51, battle_shield_timer_ran_out },
    [BATTLE_TIMER_6] = { 0x8A9D, battle_timer_6_ran_out },
    [BATTLE_TIMER_7] = { 0x8A9E, battle_timer_7_ran_out },
    [BATTLE_TIMER_HP_LOSS] = { 0x8A9F, battle_hp_loss_timer_ran_out },
    [BATTLE_TIMER_MP_REGAIN] = { 0x8B10, battle_mp_regain_timer_ran_out },
    [BATTLE_TIMER_BARRIER] = { 0x8BB9, battle_barrier_timer_ran_out },
    [BATTLE_TIMER_11] = { 0x8C08, battle_timer_11_ran_out },
};

bool battle_see_to_timer_as_called(Snes *s, uint32_t site, BattleTimer kind)
{
    return battle_run(s, site, 0xC10000u | handlers[kind].routine, handlers[kind].entry);
}
