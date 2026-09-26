/* Battle: keeping the combatants' state in step with their stat blocks.
 *
 * Once a turn the main loop updates every combatant (battle/main_loop.c):
 * each one in a filled slot is put back in the fight -- a combatant revived
 * since stands up again -- and one whose HP is gone is knocked out: out of the
 * fight with its statuses gone, an enemy beaten. A member that comes back once
 * when knocked out is still the original's code (docs/TODO.md). Then an enemy out of the fight
 * whose other part is out too loses its turn's script, and the party all out
 * is a defeat. Between turns the loop keeps each combatant's status timers
 * running while it has the status they time (combatant.h, battle/timers.h),
 * and notes whose HP is low. */
#include "battle/combatant.h"

#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/rules_data.h"
#include "battle/timers.h"
#include "original_effects.h"
#include "port/native.h"
#include "vars.h"
#include "wram.h"

/* ---- Asking about a combatant ----------------------------------------------------------- */

/* The kept statuses are ORed into the third and fourth status bytes. */
enum { KEPT_STATUSES_SHIFT = 16 };

Statuses combatant_statuses(const GameState *g, unsigned slot)
{
    return battle_stats_status(g, slot) | (Statuses)battle_stats_status_kept(g, slot) << KEPT_STATUSES_SHIFT;
}

/* Across the blocks, as one table: an index past the last reads on after it
 * (into the next bank, as the original's indexed reads carry). */
static uint32_t stray_stats_place(const GameState *g, uint32_t at) { return 0x7E0000u + battle_stat_block_place(g, 0) + at; }

static uint8_t stats_byte_at(const GameState *g, uint32_t at)
{
    unsigned block = at / BATTLE_STAT_BLOCK_BYTES;
    if (block < BATTLE_STATS_COUNT)
        return battle_stats_block_byte(g, block, at % BATTLE_STAT_BLOCK_BYTES);
    return wram_read8(g, stray_stats_place(g, at));
}
static void set_stats_byte_at(GameState *g, uint32_t at, uint8_t v)
{
    unsigned block = at / BATTLE_STAT_BLOCK_BYTES;
    if (block < BATTLE_STATS_COUNT)
        set_battle_stats_block_byte(g, block, at % BATTLE_STAT_BLOCK_BYTES, v);
    else
        wram_write8(g, stray_stats_place(g, at), v);
}

uint8_t combatant_stat_byte_at_block(const GameState *g, uint16_t block, uint16_t offset)
{
    return stats_byte_at(g, (uint32_t)block + offset);
}

uint8_t combatant_stat_byte(const GameState *g, uint8_t slot, uint16_t offset)
{
    return stats_byte_at(g, (uint32_t)slot * BATTLE_STAT_BLOCK_BYTES + offset);
}

void set_combatant_stat_byte(GameState *g, uint8_t slot, uint16_t offset, uint8_t v)
{
    set_stats_byte_at(g, (uint32_t)slot * BATTLE_STAT_BLOCK_BYTES + offset, v);
}

/* Where the table of the blocks' places takes a slot past the combatants:
 * wherever in work RAM its word points. Most of the original's code doubles
 * the slot in a byte, so the high slots index from the table's start; some
 * doubles it in a word (`wide`). */
static uint32_t stray_place(const GameState *g, uint8_t slot, uint16_t offset, bool wide)
{
    uint16_t index = wide ? (uint16_t)(slot * 2) : (uint8_t)(slot * 2);
    return 0x7E0000u + battle_stat_block_place_stray(g, index) + offset;
}

static uint8_t stat_byte_by_place(const GameState *g, uint8_t slot, uint16_t offset, bool wide)
{
    if (slot < BATTLE_COMBATANTS)
        return combatant_stat_byte(g, slot, offset);
    return wram_read8(g, stray_place(g, slot, offset, wide));
}

uint8_t combatant_stat_byte_by_place(const GameState *g, uint8_t slot, uint16_t offset)
{
    return stat_byte_by_place(g, slot, offset, false);
}

uint16_t combatant_stat_word_by_place(const GameState *g, uint8_t slot, uint16_t offset)
{
    return (uint16_t)(stat_byte_by_place(g, slot, offset, false) | stat_byte_by_place(g, slot, (uint16_t)(offset + 1), false) << 8);
}

uint8_t combatant_stat_byte_by_wide_place(const GameState *g, uint8_t slot, uint16_t offset)
{
    return stat_byte_by_place(g, slot, offset, true);
}

uint16_t combatant_stat_word_by_wide_place(const GameState *g, uint8_t slot, uint16_t offset)
{
    return (uint16_t)(stat_byte_by_place(g, slot, offset, true) | stat_byte_by_place(g, slot, (uint16_t)(offset + 1), true) << 8);
}

void set_combatant_stat_byte_by_place(GameState *g, uint8_t slot, uint16_t offset, uint8_t v)
{
    if (slot < BATTLE_COMBATANTS)
        set_stats_byte_at(g, (uint32_t)slot * BATTLE_STAT_BLOCK_BYTES + offset, v);
    else
        wram_write8(g, stray_place(g, slot, offset, false), v);
}

bool combatant_knocked_out(const GameState *g, unsigned slot)
{
    return (battle_stats_condition(g, slot) & CONDITION_KNOCKED_OUT) != 0;
}

bool combatant_in_fight(const GameState *g, unsigned slot) { return combatant_fighting_as(g, slot) != COMBATANT_NONE; }

uint8_t combatant_fighting_as(const GameState *g, unsigned slot)
{
    return slot < BATTLE_COMBATANTS ? battle_combatant_fighting(g, slot) : battle_combatant_fighting_unchecked(g, slot);
}

uint8_t combatant_id(const GameState *g, unsigned slot)
{
    return slot < BATTLE_COMBATANTS ? battle_combatant_id(g, slot) : battle_combatant_id_unchecked(g, slot);
}

uint8_t combatant_turn_wait(const GameState *g, unsigned slot)
{
    return slot < BATTLE_COMBATANTS ? battle_turn_wait(g, slot) : battle_turn_wait_unchecked(g, slot);
}

void set_combatant_turn_wait(GameState *g, unsigned slot, uint8_t wait)
{
    if (slot < BATTLE_COMBATANTS)
        set_battle_turn_wait(g, slot, wait);
    else
        set_battle_turn_wait_unchecked(g, slot, wait);
}

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

/* A combatant whose status bit restarts its script chooses afresh when the
 * bit comes or goes: its script forgets the block whose actions it was going
 * through (battle_enemy_actions_block, by slot). For a party member the same
 * index runs back into the last three enemies' places in their scripts'
 * testing, which the original marks too. */
enum { SCRIPT_START_OVER = 0xFF, PARTY_MARK_LANDS_ON_ENEMY = BATTLE_ENEMIES - BATTLE_PARTY_SIZE };

static void restart_script(GameState *g, unsigned slot)
{
    if (battle_slot_is_party(slot))
        set_battle_enemy_script_block(g, PARTY_MARK_LANDS_ON_ENEMY + slot, SCRIPT_START_OVER);
    else
        set_battle_enemy_actions_block(g, slot - BATTLE_FIRST_ENEMY, SCRIPT_START_OVER);
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
        if (statuses & STATUS_ASLEEP) {
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
    bool restarts = (battle_stats_status(g, slot) & STATUS_CONFUSED) != 0;
    uint8_t seen = battle_status_script_seen(g, slot);
    if (restarts ? seen != STATUS_CONFUSED : (seen & STATUS_CONFUSED) != 0) {
        set_battle_status_script_seen(g, slot, restarts ? STATUS_CONFUSED : 0);
        restart_script(g, slot);
    }
    if (battle_stats_status(g, slot) & STATUS_STOP)
        set_combatant_animation(g, slot, ENEMY_ANIMATION_STOPPED);
}

/* ---- The update each turn ----------------------------------------------------------------- */

static bool enemy_in_reserve(const GameState *g, unsigned slot)
{
    return !battle_slot_is_party(slot) && (battle_enemy_standing(g, slot - BATTLE_FIRST_ENEMY) & ENEMY_IN_RESERVE);
}

/* A party member knocked out drops the choice bit 4 of its action record
 * marks; the original makes a call there that does nothing, having named the
 * member the actor first, which stays. */
enum { CHOICE_DROPPED_ON_KNOCK_OUT = 0x10 };

static void drop_choice(GameState *g, unsigned member)
{
    uint8_t flags = battle_member_actions_choice_flags(g, member);
    if (!(flags & CHOICE_DROPPED_ON_KNOCK_OUT))
        return;
    set_battle_actor(g, (uint8_t)member);
    set_battle_member_actions_choice_flags(g, member, flags & (uint8_t)~CHOICE_DROPPED_ON_KNOCK_OUT);
}

/* The original reaches a member's action record through its place, which it
 * leaves in a cell ($06). */
static void leave_record_place(GameState *g, unsigned member)
{
    uint16_t place = battle_member_record_place(g, (uint8_t)member);
    set_battle_low_scratch(g, 6, (uint8_t)place);
    set_battle_low_scratch(g, 7, (uint8_t)(place >> 8));
}

/* An enemy leaving the fight is beaten, where it hadn't been counted beaten
 * already and wasn't waiting in reserve. */
static void count_enemy_beaten(GameState *g, unsigned enemy)
{
    if (battle_enemy_beaten_counted(g, enemy) || (battle_enemy_standing(g, enemy) & ENEMY_IN_RESERVE))
        return;
    set_battle_enemy_beaten(g, enemy, true);
    set_battle_enemy_beaten_any(g, true);
}

/* A combatant that comes back once when knocked out (bit 7 of its stat
 * block's revival byte, used up as it does). */
enum { COMES_BACK = 0x80 };

bool combatant_comes_back(const GameState *g, unsigned slot) { return (battle_stats_revival(g, slot) & COMES_BACK) != 0; }

/* The combatant leaves the fight, knocked out or taken off: out of the fight
 * with its turn timer stopped and its statuses and low HP gone. An enemy is
 * beaten; a party member's gauge starts over from its turn wait -- unless it
 * comes back, which is the original's code still (docs/TODO.md). */
static UpdateEnd leave_fight(GameState *g, unsigned slot)
{
    battle_stop_timer(g, BATTLE_TIMER_TURN, slot);
    set_battle_combatant_fighting(g, slot, COMBATANT_NONE);
    set_battle_stats_status(g, slot, 0);
    set_battle_stats_low_hp(g, slot, battle_stats_low_hp(g, slot) & (uint8_t)~LOW_HP);
    if (!battle_slot_is_party(slot)) {
        count_enemy_beaten(g, slot - BATTLE_FIRST_ENEMY);
        return UPDATED;
    }
    ORIGINAL_EFFECT(leave_record_place(g, slot));
    drop_choice(g, slot);
    if (combatant_comes_back(g, slot))
        return COMES_BACK_ONCE;
    uint8_t wait = battle_turn_wait(g, slot);
    set_battle_gauge_wait_full(g, slot, wait);
    set_battle_gauge_wait(g, slot, wait);
    set_battle_timer_counts_byte(g, BATTLE_TIMER_TURN, slot, wait);
    return UPDATED;
}

// $C1:B279
/* The combatant's part of the turn's update: in a filled slot it is back in
 * the fight, standing (its knocked-out bit cleared), with its turn timer
 * running, and the party isn't defeated; then an enemy in reserve is taken off
 * the battlefield again, and a combatant with no HP is knocked out. */
UpdateEnd combatant_update(GameState *g, unsigned slot)
{
    uint8_t id = battle_combatant_id(g, slot);
    if (id == COMBATANT_NONE)
        return UPDATED;
    set_battle_stats_condition(g, slot, battle_stats_condition(g, slot) & (uint8_t)~CONDITION_KNOCKED_OUT);
    set_battle_combatant_fighting(g, slot, id);
    set_battle_party_defeated(g, false);
    battle_run_timer(g, BATTLE_TIMER_TURN, slot);
    if (enemy_in_reserve(g, slot))
        return leave_fight(g, slot);
    if (battle_stats_hp(g, slot) != 0)
        return UPDATED;
    set_battle_stats_condition(g, slot, CONDITION_KNOCKED_OUT);
    return leave_fight(g, slot);
}

bool combatant_update_comes_back(const GameState *g, unsigned slot)
{
    return battle_slot_is_party(slot) && battle_combatant_id(g, slot) != COMBATANT_NONE && battle_stats_hp(g, slot) == 0
           && combatant_comes_back(g, slot);
}

/* Whether an enemy's target is in the fight. An enemy with no target has $FF
 * for it, and the original reads the byte that far past the table (which
 * isn't $FF as the battle goes), so such an enemy keeps what it chose. */
static bool target_in_fight(const GameState *g, uint8_t target)
{
    uint8_t fighting = battle_names_combatant(target) ? battle_combatant_fighting(g, target)
                                                      : battle_combatant_fighting_unchecked(g, target);
    return fighting != COMBATANT_NONE;
}

/* An enemy out of the fight whose target is out too forgets what its script
 * chose: no condition held, and its script chooses again when it is back. */
static void drop_script_if_gone(GameState *g, unsigned slot)
{
    if (battle_slot_is_party(slot) || combatant_in_fight(g, slot))
        return;
    unsigned enemy = slot - BATTLE_FIRST_ENEMY;
    if (target_in_fight(g, battle_enemy_target(g, enemy)))
        return;
    set_battle_enemy_last_condition(g, enemy, NO_CONDITION);
    set_battle_enemy_action_chosen(g, enemy, false);
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

/* A member that comes back is the original's from its test of the revival
 * byte, with the byte in A (its high byte clear), the carry clear and X and Y
 * as they came. The update leaves A clear otherwise. */
enum { COMES_BACK_TEST = 0xC1B316 };

static void finish_update(Snes *s, unsigned slot, UpdateEnd end)
{
    if (end == COMES_BACK_ONCE) {
        s->cpu.a = battle_stats_revival(state_of(s), slot);
        m_set_flag(s, CPU_FLAG_C, false);
        m_stop_at_own(s, COMES_BACK_TEST, DC_N | DC_V | DC_Z);
        return;
    }
    s->cpu.a = 0;
    m_rts(s);
}

void battle_update_combatant(Snes *s)
{
    unsigned slot = combatant_in_x(s);
    finish_update(s, slot, combatant_update(state_of(s), slot));
}

// $C1:B223
/* Update every combatant for the turn, a combatant at a time with its place
 * in the loop in its cell. A member that comes back is updated by the
 * original's code, called as its loop calls it with the combatant in X and Y;
 * the rest is C. What the callers read back is the carry of the last compare
 * of the search for a party member in the fight: clear when it found the
 * second or the third. */
void battle_update_all_combatants(Snes *s)
{
    GameState *g = state_of(s);
    for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++) {
        ORIGINAL_EFFECT(set_battle_combatant_updating(g, (uint8_t)slot));
        if (combatant_update_comes_back(g, slot)) {
            s->cpu.y = (uint16_t)slot;
            s->cpu.x = (uint16_t)(slot * BATTLE_STAT_BLOCK_BYTES);
            s->cpu.a = 0;
            if (!battle_call(s, 0xC1B234, 0xC1B279, DC_A | DC_FLAGS))
                return;
        } else {
            NATIVE_TIME_AS(s, 0xC1B279, combatant_update(g, slot));
        }
        combatant_after_update(g, slot);
    }
    ORIGINAL_EFFECT(set_battle_combatant_updating(g, BATTLE_COMBATANTS));
    combatants_after_update(g);
    unsigned member = first_member_fighting(g);
    m_set_flag(s, CPU_FLAG_C, member == 0 || member == BATTLE_PARTY_SIZE);
    m_rts(s);
}
