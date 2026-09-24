/* Battle: the combatants' timers (battle/timers.h), counted down and looked
 * for once a pass of the main loop.
 *
 * A timer's state byte is 0 while it is stopped and nonzero while it runs,
 * with bit 7 once it has run out; a status's timer keeps bit 0 as the status
 * (battle_follow_status). Its count is a byte that runs down to 0. */
#include "battle/timers.h"

#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "original_effects.h"
#include "vars.h"

enum { TIMER_RUNNING = 0x01, TIMER_RUN_OUT = 0x80, TIMER_STOPPED = 0 };

static uint8_t timer_state(const GameState *g, BattleTimer timer, unsigned slot)
{
    return battle_timer_flags_byte(g, timer, slot);
}
static void set_timer_state(GameState *g, BattleTimer timer, unsigned slot, uint8_t state)
{
    set_battle_timer_flags_byte(g, timer, slot, state);
}
static uint8_t timer_count(const GameState *g, BattleTimer timer, unsigned slot)
{
    return battle_timer_counts_byte(g, timer, slot);
}
static void set_timer_count(GameState *g, BattleTimer timer, unsigned slot, uint8_t count)
{
    set_battle_timer_counts_byte(g, timer, slot, count);
}

static bool timer_counting(uint8_t state) { return state != TIMER_STOPPED && !(state & TIMER_RUN_OUT); }

void battle_run_timer(GameState *g, BattleTimer timer, unsigned slot)
{
    set_timer_state(g, timer, slot, timer_state(g, timer, slot) | TIMER_RUNNING);
}

void battle_stop_timer(GameState *g, BattleTimer timer, unsigned slot) { set_timer_state(g, timer, slot, TIMER_STOPPED); }

void battle_follow_status(GameState *g, BattleTimer timer, unsigned slot, bool status_on)
{
    uint8_t state = timer_state(g, timer, slot) & (uint8_t)~TIMER_RUNNING;
    set_timer_state(g, timer, slot, status_on ? state | TIMER_RUNNING : state);
}

static void run_out(GameState *g, BattleTimer timer, unsigned slot)
{
    set_timer_state(g, timer, slot, timer_state(g, timer, slot) | TIMER_RUN_OUT);
}

// $C1:8231
/* Count every running timer down a step, kind after kind. A stopped
 * combatant's timers stand still, and each of them steps its stop timer down
 * instead, which runs out when it reaches 0 (from 0 it goes round to 255). A
 * timer at 0 already runs out without counting. */
void battle_count_timers_down(GameState *g)
{
    for (unsigned kind = 0; kind < BATTLE_TIMER_KINDS; kind++) {
        for (unsigned slot = 0; slot < BATTLE_COMBATANTS; slot++) {
            if (!timer_counting(timer_state(g, kind, slot)))
                continue;
            if (timer_state(g, BATTLE_TIMER_STOP, slot) != TIMER_STOPPED) {
                uint8_t left = (uint8_t)(timer_count(g, BATTLE_TIMER_STOP, slot) - 1);
                set_timer_count(g, BATTLE_TIMER_STOP, slot, left);
                if (left == 0)
                    run_out(g, BATTLE_TIMER_STOP, slot);
                continue;
            }
            uint8_t left = timer_count(g, kind, slot);
            if (left != 0)
                set_timer_count(g, kind, slot, --left);
            if (left == 0)
                run_out(g, kind, slot);
        }
    }
    ORIGINAL_EFFECT(set_battle_timer_tick_slot(g, 0));
    ORIGINAL_EFFECT(set_battle_timer_tick_timer(g, BATTLE_TIMER_KINDS * BATTLE_COMBATANTS));
}

/* The combatant at a place in the order the timers are looked for in. A
 * kind's cursor that found a run-out timer at the order's last place goes on
 * past it, and the next search reads on into the list after the order, as the
 * original's indexed read does. And a place past the combatants is empty
 * ($FF), whose timer is past the kind's row: that byte is read as a state
 * too. */
static uint8_t combatant_in_order(const GameState *g, uint8_t place)
{
    return place < BATTLE_TIMER_ORDER_COUNT ? battle_timer_order(g, place) : battle_timer_order_unchecked(g, place);
}

static bool has_run_out(const GameState *g, BattleTimer timer, uint8_t slot)
{
    return (battle_timer_flags_byte_unchecked(g, timer, slot) & TIMER_RUN_OUT) != 0;
}

/* The due bits run from kind 0 at bit 12 down to the turn at bit 0. */
static uint16_t due_bit(BattleTimer kind) { return (uint16_t)(1u << (BATTLE_TIMER_TURN - kind)); }

/* What the search leaves in its cells: the kind after the last (a word), the
 * battle slot it looked at last, and the due bit shifted past kind 12. */
static void leave_search_cells(GameState *g, uint8_t last_slot)
{
    set_battle_low_scratch(g, 0, BATTLE_TIMER_KINDS);
    set_battle_low_scratch(g, 1, 0);
    set_battle_low_scratch(g, 2, last_slot);
    set_battle_low_scratch(g, 3, 0);
    set_battle_timer_kind_bit(g, 0);
}

// $C1:8290
/* The party's turn timers go on their gauges. Then, on the passes that look,
 * each kind of timer is searched for one that has run out, from where its
 * last search stopped in the order, to the order's end: the first found marks
 * the kind due and the search moves past it; none found starts the kind's
 * next search at the top. */
void battle_find_run_out_timers(GameState *g, bool look)
{
    for (unsigned member = 0; member < BATTLE_PARTY_SIZE; member++)
        set_battle_gauge_wait(g, member, timer_count(g, BATTLE_TIMER_TURN, member));
    if (!look)
        return;
    uint8_t slot = 0;
    for (unsigned kind = 0; kind < BATTLE_TIMER_KINDS; kind++) {
        for (;;) {
            uint8_t place = battle_timer_kind_cursors(g, kind);
            slot = combatant_in_order(g, place);
            set_battle_timer_kind_cursors(g, kind, ++place);
            if (has_run_out(g, kind, slot)) {
                set_battle_timer_kinds_due(g, battle_timer_kinds_due(g) | due_bit(kind));
                break;
            }
            if (place >= BATTLE_TIMER_ORDER_COUNT) {
                set_battle_timer_kind_cursors(g, kind, 0);
                break;
            }
        }
    }
    ORIGINAL_EFFECT(leave_search_cells(g, slot));
}

/* ---- The entries ------------------------------------------------------------------------ */

/* Both are stretches of the main loop, which each hands on to the next: the
 * count-down to the search ($C1:8290), and the search, with the handlers of
 * the kinds due, to the rest of the pass ($C1:8345). */
enum { SEARCH = 0xC18290, REST_OF_PASS = 0xC18345, THE_HANDLERS_CALL = 0xC18333 };

/* The timed statuses' handlers, as the original's table at $C1:B92D names
 * them, all still the original's; the turn's is the turn itself
 * (battle/main_loop.c, battle_run_queued_scripts at $C1:8461). */
static const uint16_t kind_handlers[BATTLE_TIMER_TURN] = {
    0x8876, 0x88C5, 0x8975, 0x89B9, 0x8A05, 0x8A51, 0x8A9D, 0x8A9E, 0x8A9F, 0x8B10, 0x8BB9, 0x8C08,
};

/* The main loop counts its passes; the search looks on the odd ones. */
static bool pass_looks(const GameState *g) { return battle_loop_count(g) & 0x01; }

void battle_tick_timers(Snes *s)
{
    battle_count_timers_down(state_of(s));
    m_sep(s, CPU_FLAG_M);
    m_stop_at(s, SEARCH, DC_A | DC_X | DC_Y | DC_FLAGS);
}

/* Run each due kind's handler, as the original's loop calls them: the kind on
 * the stack under the call and its bit in its cell. The turn's handler also
 * reads Y, which the search left at the last kind and a handler before it
 * leaves as it likes, and the overflow flag, which the test of the due bits
 * sets from their bit 14. */
enum { DUE_TEST_OVERFLOW = 0x4000 };

static bool call_handler(Snes *s, BattleTimer kind)
{
    s->cpu.x = (uint16_t)(kind * 2);
    s->cpu.a = 0;
    if (kind != BATTLE_TIMER_TURN)
        return battle_call(s, THE_HANDLERS_CALL, 0xC10000u | kind_handlers[kind], DC_A | DC_X | DC_Y | DC_FLAGS);
    m_set_flag(s, CPU_FLAG_V, battle_timer_kinds_due(state_of(s)) & DUE_TEST_OVERFLOW);
    return battle_call(s, THE_HANDLERS_CALL, 0xC18461, DC_A | DC_X | DC_N | DC_Z | DC_C);
}

static bool run_due_handlers(Snes *s)
{
    GameState *g = state_of(s);
    s->cpu.y = BATTLE_TIMER_KINDS;
    for (unsigned kind = 0; kind < BATTLE_TIMER_KINDS; kind++) {
        ORIGINAL_EFFECT(set_battle_timer_kind_bit(g, due_bit(kind)));
        if (!(battle_timer_kinds_due(g) & due_bit(kind)))
            continue;
        m_push16(s, (uint16_t)kind);
        if (!call_handler(s, kind))
            return false;
        m_pull16(s);
    }
    ORIGINAL_EFFECT(set_battle_timer_kind_bit(g, 0));
    return true;
}

void battle_dispatch_expired_timers(Snes *s)
{
    GameState *g = state_of(s);
    bool look = pass_looks(g);
    battle_find_run_out_timers(g, look);
    if (look && !run_due_handlers(s))
        return;
    m_stop_at(s, REST_OF_PASS, DC_A | DC_X | DC_Y | DC_FLAGS);
}
