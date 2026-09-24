/* Battle: keeping the combatants' state in step with their stat blocks.
 *
 * Once a turn the main loop updates every combatant (battle/main_loop.c):
 * each one in a filled slot is put back in the fight -- a combatant revived
 * since stands up again -- and one whose HP is gone is knocked out, which is
 * still the original's code (docs/TODO.md). Then an enemy out of the fight
 * whose other part is out too loses its turn's script, and the party all out
 * is a defeat. Between turns the loop keeps each combatant's status timers
 * running while it has the status they time (combatant.h, battle/timers.h),
 * and notes whose HP is low. */
#include "battle/combatant.h"

#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/timers.h"
#include "original_effects.h"
#include "vars.h"

/* ---- Asking about a combatant ----------------------------------------------------------- */

/* The kept statuses are ORed into the third and fourth status bytes. */
enum { KEPT_STATUSES_SHIFT = 16 };

Statuses combatant_statuses(const GameState *g, unsigned slot)
{
    return battle_stats_status(g, slot) | (Statuses)battle_stats_status_kept(g, slot) << KEPT_STATUSES_SHIFT;
}

/* The condition byte's knocked-out bit. */
enum { CONDITION_KNOCKED_OUT = 0x80 };

bool combatant_knocked_out(const GameState *g, unsigned slot)
{
    return (battle_stats_condition(g, slot) & CONDITION_KNOCKED_OUT) != 0;
}

bool combatant_in_fight(const GameState *g, unsigned slot) { return battle_combatant_fighting(g, slot) != COMBATANT_NONE; }

/* The low HP flag is bit 0 of the stat block's first byte. */
enum { LOW_HP = 0x01, LOW_HP_FRACTION = 8 };

bool combatant_hp_low(const GameState *g, unsigned slot)
{
    return battle_stats_hp(g, slot) <= battle_stats_max_hp(g, slot) / LOW_HP_FRACTION;
}

void combatant_note_low_hp(GameState *g, unsigned slot)
{
    uint8_t flags = battle_stats_low_hp(g, slot) & (uint8_t)~LOW_HP;
    set_battle_stats_low_hp(g, slot, combatant_hp_low(g, slot) ? flags | LOW_HP : flags);
}

/* ---- The statuses that run on timers ------------------------------------------------------ */

/* Each timed status, and the bit of the combatant's statuses its timer
 * follows (kinds 6 and 7 time no status, and 12 is the turn). */
static const struct {
    BattleTimer timer;
    Statuses status;
} timed_statuses[] = {
    { BATTLE_TIMER_STATUS_10, 0x04000000 }, { BATTLE_TIMER_STATUS_1, 0x00000040 },
    { BATTLE_TIMER_STATUS_8, 0x00000010 },  { BATTLE_TIMER_STATUS_9, 0x20000000 },
    { BATTLE_TIMER_STATUS_11, 0x00020000 }, { BATTLE_TIMER_STATUS_2, 0x00000020 },
    { BATTLE_TIMER_STATUS_5, 0x40000000 },  { BATTLE_TIMER_STATUS_3, 0x00800000 },
    { BATTLE_TIMER_STATUS_4, 0x00400000 },  { BATTLE_TIMER_STOP, STATUS_STOP },
};

/* A combatant whose status bit restarts its script comes back to its script's
 * start when the bit comes or goes. The table the original marks is the
 * enemies' (the block they last started from, battle_enemy_script_block_seen,
 * by slot); for a party member the same index runs back into the last three
 * enemies' block counts, which the original marks too. */
enum { SCRIPT_START_OVER = 0xFF, PARTY_MARK_LANDS_ON_ENEMY = BATTLE_ENEMIES - BATTLE_PARTY_SIZE };

static void restart_script(GameState *g, unsigned slot)
{
    if (battle_slot_is_party(slot))
        set_battle_enemy_script_blocks(g, PARTY_MARK_LANDS_ON_ENEMY + slot, SCRIPT_START_OVER);
    else
        set_battle_enemy_script_block_seen(g, slot - BATTLE_FIRST_ENEMY, SCRIPT_START_OVER);
}

/* A stopped combatant shows the stopped animation. Only the enemies have
 * one; for a party member the original's index runs back into the waits of
 * the last three script channels, which it sets alike (battle/scripts.c). */
enum { PARTY_ANIMATION_LANDS_ON_CHANNEL = 13 };

static void set_combatant_animation(GameState *g, unsigned slot, uint8_t animation)
{
    if (battle_slot_is_party(slot))
        set_battle_script_waits(g, PARTY_ANIMATION_LANDS_ON_CHANNEL + slot, animation);
    else
        set_battle_enemy_animation(g, slot - BATTLE_FIRST_ENEMY, animation);
}

// $C1:B0B6
/* Bring the combatant's status timers in step with its statuses: a timer runs
 * while the combatant has its status, and a combatant that gets no turns has
 * its turn timer stopped (an enemy's animation back to idle). A knocked-out
 * combatant's timers stay as they are. Then what its statuses decide besides:
 * a party member that can't use techs is marked so (and nothing else is
 * looked at); the combatant's script starts over when its status bit for that
 * came or went; and a stopped combatant shows so. */
void combatant_follow_statuses(GameState *g, unsigned slot)
{
    Statuses statuses = combatant_statuses(g, slot);
    bool knocked_out = combatant_knocked_out(g, slot);
    if (!knocked_out) {
        if (statuses & STATUS_NO_TURNS) {
            battle_stop_timer(g, BATTLE_TIMER_TURN, slot);
            if (!battle_slot_is_party(slot))
                set_battle_enemy_animation(g, slot - BATTLE_FIRST_ENEMY, ENEMY_ANIMATION_IDLE);
        }
        for (unsigned i = 0; i < sizeof timed_statuses / sizeof timed_statuses[0]; i++)
            battle_follow_status(g, timed_statuses[i].timer, slot, (statuses & timed_statuses[i].status) != 0);
    }
    if (battle_slot_is_party(slot)) {
        bool no_techs = !knocked_out && (statuses & STATUS_NO_TECHS);
        set_battle_member_disabled(g, slot, no_techs);
        if (no_techs)
            return;
    }
    bool restarts = (battle_stats_status(g, slot) & STATUS_RESTARTS_SCRIPT) != 0;
    uint8_t seen = battle_status_script_seen(g, slot);
    if (restarts ? seen != STATUS_RESTARTS_SCRIPT : (seen & STATUS_RESTARTS_SCRIPT) != 0) {
        set_battle_status_script_seen(g, slot, restarts ? STATUS_RESTARTS_SCRIPT : 0);
        restart_script(g, slot);
    }
    if (battle_stats_status(g, slot) & STATUS_STOP)
        set_combatant_animation(g, slot, ENEMY_ANIMATION_STOPPED);
}

/* ---- The update each turn ----------------------------------------------------------------- */

/* An enemy waiting off the battlefield to be brought in (battle_enemy_standing). */
enum { ENEMY_IN_RESERVE = 0x80 };

static bool enemy_in_reserve(const GameState *g, unsigned slot)
{
    return !battle_slot_is_party(slot) && (battle_enemy_standing(g, slot - BATTLE_FIRST_ENEMY) & ENEMY_IN_RESERVE);
}

/* How a combatant's update ends: it stays in the fight, or it leaves it,
 * knocked out (its HP gone) or taken off the battlefield (an enemy in
 * reserve); the original's code does the last two. */
typedef enum { STAYS_IN_FIGHT, KNOCKED_OUT, TAKEN_OFF } UpdateEnd;

// $C1:B279
/* The combatant's part of the turn's update: in a filled slot it is back in
 * the fight, standing (its knocked-out bit cleared), with its turn timer
 * running, and the party isn't defeated; then an enemy in reserve is taken off
 * the battlefield, and a combatant with no HP is knocked out. */
static UpdateEnd update_combatant(GameState *g, unsigned slot)
{
    uint8_t id = battle_combatant_id(g, slot);
    if (id == COMBATANT_NONE)
        return STAYS_IN_FIGHT;
    set_battle_stats_condition(g, slot, battle_stats_condition(g, slot) & (uint8_t)~CONDITION_KNOCKED_OUT);
    set_battle_combatant_fighting(g, slot, id);
    set_battle_party_defeated(g, false);
    battle_run_timer(g, BATTLE_TIMER_TURN, slot);
    if (enemy_in_reserve(g, slot))
        return TAKEN_OFF;
    return battle_stats_hp(g, slot) != 0 ? STAYS_IN_FIGHT : KNOCKED_OUT;
}

bool combatant_update(GameState *g, unsigned slot) { return update_combatant(g, slot) == STAYS_IN_FIGHT; }

/* Whether an enemy's other part is in the fight. An enemy with no other part
 * has $FF for its link, and the original reads the byte that far past the
 * table (which isn't $FF as the battle goes), so such an enemy keeps its
 * script. */
static bool link_in_fight(const GameState *g, uint8_t link)
{
    uint8_t fighting = battle_names_combatant(link) ? battle_combatant_fighting(g, link)
                                                    : battle_combatant_fighting_unchecked(g, link);
    return fighting != COMBATANT_NONE;
}

/* An enemy out of the fight, whose other part is out too, loses its turn's
 * script: no condition tested, and its script found again when it is back. */
enum { NO_CONDITION = 0xFF };

static void drop_script_if_gone(GameState *g, unsigned slot)
{
    if (battle_slot_is_party(slot) || combatant_in_fight(g, slot))
        return;
    unsigned enemy = slot - BATTLE_FIRST_ENEMY;
    if (link_in_fight(g, battle_enemy_link(g, enemy)))
        return;
    set_battle_enemy_last_condition(g, enemy, NO_CONDITION);
    set_battle_enemy_script_started(g, enemy, false);
}

/* The first party member still in the fight, or the party's size if none is:
 * the party all out is a defeat. The party's ids are small, and the test here
 * is the byte's sign, which of a member's values only "out" ($FF) has; an
 * enemy's id can have its top bit set, so elsewhere "out" is the whole byte. */
enum { OUT_OF_FIGHT_SIGN = 0x80 };

static unsigned first_member_fighting(const GameState *g)
{
    unsigned member = 0;
    while (member < BATTLE_PARTY_SIZE && (battle_combatant_fighting(g, member) & OUT_OF_FIGHT_SIGN))
        member++;
    return member;
}

void combatant_after_update(GameState *g, unsigned slot) { drop_script_if_gone(g, slot); }

void combatants_after_update(GameState *g)
{
    if (first_member_fighting(g) == BATTLE_PARTY_SIZE)
        set_battle_party_defeated(g, true);
}

/* ---- The entries ------------------------------------------------------------------------ */

/* Each takes the combatant as the original's callers give it: its stat block
 * in X, and its battle slot in Y where the routine reads both. */
static unsigned combatant_in_x(Snes *s) { return battle_slot_of_stat_block(s->cpu.x); }

// $C1:B093
void battle_update_low_hp_flag(Snes *s)
{
    combatant_note_low_hp(state_of(s), combatant_in_x(s));
    m_rts(s);
}

void battle_update_status_flags(Snes *s)
{
    combatant_follow_statuses(state_of(s), combatant_in_x(s));
    m_rts(s);
}

/* Where the original goes on for a combatant that leaves the fight: it knocks
 * it out at $C1:B2B1 (in 16-bit A, as the HP test left it), or takes it off
 * at $C1:B2B9. */
enum { KNOCK_OUT = 0xC1B2B1, TAKE_OFF = 0xC1B2B9 };

void battle_update_combatant(Snes *s)
{
    switch (update_combatant(state_of(s), combatant_in_x(s))) {
    case STAYS_IN_FIGHT:
        m_rts(s);
        return;
    case KNOCKED_OUT:
        m_rep(s, CPU_FLAG_M);
        m_stop_at(s, KNOCK_OUT, 0);
        return;
    case TAKEN_OFF:
        m_stop_at(s, TAKE_OFF, 0);
        return;
    }
}

// $C1:B223
/* Update every combatant for the turn. Each one's update runs through its own
 * routine ($C1:B279, battle_update_combatant above), since the original's code
 * finishes a combatant that leaves the fight; the port's call is the
 * original's, with the combatant in X and Y and its place in the loop in its
 * cell. What the callers read back is the carry of the last compare of the
 * search for a party member in the fight: clear when it found the second or
 * the third. */
void battle_update_all_combatants(Snes *s)
{
    GameState *g = state_of(s);
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++) {
        ORIGINAL_EFFECT(set_battle_combatant_updating(g, (uint8_t)slot));
        s->cpu.y = (uint16_t)slot;
        s->cpu.x = (uint16_t)(slot * BATTLE_STAT_BLOCK_BYTES);
        s->cpu.a = 0;
        if (!battle_call(s, 0xC1B234, 0xC1B279, DC_A | DC_FLAGS))
            return;
        combatant_after_update(g, slot);
    }
    ORIGINAL_EFFECT(set_battle_combatant_updating(g, BATTLE_COMBATANTS));
    combatants_after_update(g);
    unsigned member = first_member_fighting(g);
    m_set_flag(s, CPU_FLAG_C, member == 0 || member == BATTLE_PARTY_SIZE);
    m_rts(s);
}
