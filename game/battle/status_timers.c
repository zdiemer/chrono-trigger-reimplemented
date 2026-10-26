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
 * The search can find a timer of nobody's run out (battle/timers.h), which
 * the original's handlers see to as any slot's, past every table they touch.
 * Nobody has a status: in the game nothing happens but the kind being seen
 * to, and for a status that wears off the original's writes past the tables
 * are made in one place (battle_timer_wears_off_past_the_rows); the other
 * kinds of nobody's the entries' guards leave to the original, and the game
 * makes none of them (docs/TODO.md). */
#include "battle/status_timers.h"

#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/combatant.h"
#include "battle/effect_work.h"
#include "battle/enemy_actions.h"
#include "battle/entries.h"
#include "battle/arith.h"
#include "battle/timers.h"
#include "battle/program.h"
#include "battle/steps.h"
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

/* Each kind: what it does, and whether wearing off takes away the status it
 * times (battle_timed_status), which the status's own doing asks after too --
 * all but the slow's, whose wearing off leaves the slow on (the original's). */
static const struct {
    TimerEffect effect;
    bool wearing_off_clears;
} timed_statuses[BATTLE_TIMER_TURN] = {
    [BATTLE_TIMER_STOP] = { WEARS_OFF, true },
    [BATTLE_TIMER_POISON] = { POISON_HURTS, false },
    [BATTLE_TIMER_SLOW] = { WEARS_OFF, false },
    [BATTLE_TIMER_HASTE] = { WEARS_OFF, true },
    [BATTLE_TIMER_EVASION] = { WEARS_OFF, true },
    [BATTLE_TIMER_SHIELD] = { WEARS_OFF, true },
    [BATTLE_TIMER_6] = { NOTHING_HAPPENS, false },
    [BATTLE_TIMER_7] = { NOTHING_HAPPENS, false },
    [BATTLE_TIMER_HP_LOSS] = { LOSES_A_POINT, false },
    [BATTLE_TIMER_MP_REGAIN] = { REGAINS_MAGIC, false },
    [BATTLE_TIMER_BARRIER] = { WEARS_OFF, true },
    [BATTLE_TIMER_11] = { NOTHING_HAPPENS, false },
};

enum { RUNS_PER_CYCLE = 10, MAGIC_REGAINED = 5, POINT_OF_HP = 1 };

static uint8_t stat(const GameState *g, uint8_t slot, uint16_t offset) { return combatant_stat_byte(g, slot, offset); }

static bool knocked_out(const GameState *g, uint8_t slot)
{
    return (stat(g, slot, STAT_CONDITION) & CONDITION_KNOCKED_OUT) != 0;
}

/* Whether the combatant has the status its timer times, the ones it keeps
 * whatever happens included. */
static bool has_timed_status(const GameState *g, BattleTimer kind, uint8_t slot)
{
    return (combatant_statuses(g, slot) & battle_timed_status(kind)) != 0;
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
static bool tenth_run(GameState *g, BattleTimer kind, uint8_t slot)
{
    uint8_t left = (uint8_t)(battle_timer_runs_left_byte(g, kind, slot) - 1);
    bool tenth = left == 0;
    set_battle_timer_runs_left_byte(g, kind, slot, tenth ? RUNS_PER_CYCLE : left);
    return tenth;
}

// $C1:88C5
/* Poison takes the combatant's most HP over half its stamina and 8 (in a
 * byte). The original divides the long arithmetic's leftover high word with
 * the most HP, and by a divisor whose high byte is what that arithmetic left
 * in its cell, as the game plays them: both are 0 after the small multiplies
 * the battle does between, but a large product left there makes poison's
 * damage the low word of a huge quotient, and a divisor left there, almost
 * nothing. */
static uint16_t poison_damage(GameState *g, uint8_t slot)
{
    uint8_t stamina = stat(g, slot, STAT_STAMINA);
    uint16_t divisor = (uint16_t)(battle_wide_math_second_high(g) << 8 | (uint8_t)((stamina >> 1) + 8));
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
            combatant_lose_statuses(g, slot, battle_timed_status(kind));
        battle_stop_timer(g, kind, slot);
        return NOTHING_TO_SHOW;
    }
    case POISON_HURTS:
        if (knocked_out(g, slot) || !has_timed_status(g, kind, slot))
            return NOTHING_TO_SHOW;
        /* each run the next is longer, by the combatant's stamina (in a
         * byte, so a long one runs round to a short one) */
        battle_start_timer(g, kind, slot, (uint8_t)(battle_timer_length(g, kind, slot) + stat(g, slot, STAT_STAMINA)));
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

/* A timer of nobody's run out: there is no status to wear off, and nothing
 * to show. */
static TimerOutcome nobodys_timer_ran_out(GameState *g, BattleTimer kind, uint8_t nobody)
{
    battle_timer_seen_to(g, kind);
    battle_timer_wears_off_past_the_rows(g, kind, nobody,
                                         timed_statuses[kind].wearing_off_clears ? battle_timed_status(kind) : 0);
    return NOTHING_TO_SHOW;
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
 * $C1:895B's call. */
enum { SHOWING_CALL = 0xC18971 };

void battle_show_status_number_entry(Snes *s)
{
    set_up_status_showing(state_of(s), (uint8_t)s->cpu.a);
    if (battle_run(s, SHOWING_CALL, 0xC1AC57, battle_start_action_showing))
        m_rts(s);
}

bool battle_show_status_number(GameState *g, Platform *pf, uint8_t showing)
{
    set_up_status_showing(g, showing);
    bool done = false;
    TIMED_AS(g, 0xC1AC57, done = battle_action_shown(g, pf));
    return done;
}

/* The handlers' calls of $C1:895B, by the kind whose number they show. */
enum { POISON_SHOWS_CALL = 0xC18945, MAGIC_SHOWS_CALL = 0xC18BB1, SHOW_NUMBER = 0xC1895B };

// $C1:8876, $C1:88C5, $C1:8975, $C1:89B9, $C1:8A05, $C1:8A51, $C1:8A9D-$C1:8A9F, $C1:8B10, $C1:8BB9, $C1:8C08
/* A run-out timer seen to, the number its status shows shown as an action
 * of its own (a routine of its own, `show`); poison is seen to after its
 * showing. False where the showing went no further. */
static bool see_to_the_timer(GameState *g, BattleTimer kind, bool (*show)(void *ctx, BattleTimer kind), void *ctx)
{
    TimerOwner owner = battle_timer_found_run_out(g, kind);
    TimerOutcome outcome = owner.nobody ? nobodys_timer_ran_out(g, kind, owner.slot) : timer_ran_out(g, kind, owner.slot);
    if (outcome == NUMBER_TO_SHOW) {
        if (!show(ctx, kind))
            return false;
        TIMED_AS(g, 0xFDACEE, battle_clear_numbers_shown(g));
    } else if (outcome == DAMAGE_TO_APPLY) {
        TIMED_AS(g, 0xC1EC7F, battle_apply_damage(g));
    }
    if (timed_statuses[kind].effect == POISON_HURTS)
        battle_timer_seen_to(g, kind);
    return true;
}

/* On the machine the number is shown from the handler's own call. */
static bool show_on_the_machine(void *ctx, BattleTimer kind)
{
    Snes *s = ctx;
    m_set_a8(s, STATUS_NUMBER_SHOWING);
    uint32_t site = kind == BATTLE_TIMER_POISON ? POISON_SHOWS_CALL : MAGIC_SHOWS_CALL;
    return battle_run(s, site, SHOW_NUMBER, battle_show_status_number_entry);
}

static void handler_entry(Snes *s, BattleTimer kind)
{
    if (see_to_the_timer(state_of(s), kind, show_on_the_machine, s))
        m_rts(s);
}

/* The guard's state half: a timer of nobody's that does anything but wear
 * off is the original's: its handler works past every table it touches
 * (battle/timers.h). */
static bool handler_takes(const GameState *g, BattleTimer kind)
{
    return !battle_timer_found_run_out(g, kind).nobody || timed_statuses[kind].effect == WEARS_OFF;
}

static bool handler_ready(Snes *s, BattleTimer kind)
{
    vars_peek(true);
    bool ours = handler_takes(state_of(s), kind);
    vars_peek(false);
    return battle_on_page(s) && ours;
}

#define HANDLER(name, kind) \
    void name(Snes *s) { handler_entry(s, kind); } \
    bool name##_ready(Snes *s) { return handler_ready(s, kind); }
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
    uint32_t routine;
    void (*entry)(Snes *);
} handlers[BATTLE_TIMER_TURN] = {
    [BATTLE_TIMER_STOP] = { 0xC18876, battle_stop_timer_ran_out },
    [BATTLE_TIMER_POISON] = { 0xC188C5, battle_poison_timer_ran_out },
    [BATTLE_TIMER_SLOW] = { 0xC18975, battle_slow_timer_ran_out },
    [BATTLE_TIMER_HASTE] = { 0xC189B9, battle_haste_timer_ran_out },
    [BATTLE_TIMER_EVASION] = { 0xC18A05, battle_evasion_timer_ran_out },
    [BATTLE_TIMER_SHIELD] = { 0xC18A51, battle_shield_timer_ran_out },
    [BATTLE_TIMER_6] = { 0xC18A9D, battle_timer_6_ran_out },
    [BATTLE_TIMER_7] = { 0xC18A9E, battle_timer_7_ran_out },
    [BATTLE_TIMER_HP_LOSS] = { 0xC18A9F, battle_hp_loss_timer_ran_out },
    [BATTLE_TIMER_MP_REGAIN] = { 0xC18B10, battle_mp_regain_timer_ran_out },
    [BATTLE_TIMER_BARRIER] = { 0xC18BB9, battle_barrier_timer_ran_out },
    [BATTLE_TIMER_11] = { 0xC18C08, battle_timer_11_ran_out },
};

bool battle_see_to_timer_as_called(Snes *s, uint32_t site, BattleTimer kind)
{
    return battle_run(s, site, handlers[kind].routine, handlers[kind].entry);
}

/* ---- As C ------------------------------------------------------------------------------------ */

typedef struct {
    GameState *game;
    Platform *pf;
} TimerAsC;

static bool show_as_c(void *ctx, BattleTimer kind)
{
    (void)kind;
    TimerAsC *c = ctx;
    bool done = false;
    TIMED_AS(c->game, SHOW_NUMBER, done = battle_show_status_number(c->game, c->pf, STATUS_NUMBER_SHOWING));
    return done;
}

bool battle_run_out_timer_seen_to(GameState *g, Platform *pf, BattleTimer kind)
{
    if (!handler_takes(g, kind))
        return battle_needs_the_machine(g, handlers[kind].routine, "a timer of nobody's the port doesn't see to");
    TimerAsC c = { g, pf };
    bool done = false;
    TIMED_AS(g, handlers[kind].routine, done = see_to_the_timer(g, kind, show_as_c, &c));
    return done;
}
