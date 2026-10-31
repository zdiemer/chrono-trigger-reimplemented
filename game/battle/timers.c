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
#include "battle/main_loop.h"
#include "battle/status_timers.h"
#include "charge.h"
#include "original_effects.h"
#include "vars.h"

enum { TIMER_RUNNING = 0x01, TIMER_RUN_OUT = 0x80, TIMER_STOPPED = 0 };

/* A status that wears off does so on the tenth run of its timer. */
enum { RUNS_A_CYCLE = 10 };

/* The status bits the kinds time: the first status byte in the low byte, the
 * fourth in the high (battle/combatant.h). Kind 11 follows the third byte's
 * $02, whose meaning isn't worked out. */
static const Statuses timed_status[BATTLE_TIMER_KINDS] = {
    [BATTLE_TIMER_STOP] = STATUS_STOP,
    [BATTLE_TIMER_POISON] = STATUS_POISON,
    [BATTLE_TIMER_SLOW] = STATUS_SLOW,
    [BATTLE_TIMER_HASTE] = (Statuses)STATUS3_HASTE << STATUS_BYTE_3_SHIFT,
    [BATTLE_TIMER_EVASION] = (Statuses)STATUS3_EVADES_MUCH_BETTER << STATUS_BYTE_3_SHIFT,
    [BATTLE_TIMER_SHIELD] = (Statuses)STATUS4_SHIELD << STATUS_BYTE_4_SHIFT,
    [BATTLE_TIMER_HP_LOSS] = STATUS_LOSES_HP,
    [BATTLE_TIMER_MP_REGAIN] = (Statuses)STATUS4_REGAINS_MP << STATUS_BYTE_4_SHIFT,
    [BATTLE_TIMER_BARRIER] = (Statuses)STATUS4_BARRIER << STATUS_BYTE_4_SHIFT,
    [BATTLE_TIMER_11] = (Statuses)STATUS3_TIMER_11 << STATUS_BYTE_3_SHIFT,
};

Statuses battle_timed_status(BattleTimer kind) { return timed_status[kind]; }

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

uint8_t battle_timer_length(const GameState *g, BattleTimer timer, unsigned slot)
{
    return battle_timer_lengths_byte(g, timer, slot);
}

void battle_set_timer_count(GameState *g, BattleTimer timer, unsigned slot, uint8_t count)
{
    set_timer_count(g, timer, slot, count);
}

void battle_start_timer(GameState *g, BattleTimer timer, unsigned slot, uint8_t count)
{
    set_timer_count(g, timer, slot, count);
    battle_restart_timer(g, timer, slot);
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

void battle_restart_timer(GameState *g, BattleTimer timer, unsigned slot) { set_timer_state(g, timer, slot, TIMER_RUNNING); }

bool battle_timer_running(const GameState *g, BattleTimer timer, unsigned slot)
{
    return timer_state(g, timer, slot) != TIMER_STOPPED;
}

bool battle_timer_run_out(const GameState *g, BattleTimer timer, unsigned slot)
{
    return (timer_state(g, timer, slot) & TIMER_RUN_OUT) != 0;
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
uint8_t battle_timer_order_at(const GameState *g, uint8_t place)
{
    return place < BATTLE_TIMER_ORDER_COUNT ? battle_timer_order(g, place) : battle_timer_order_unchecked(g, place);
}

TimerOwner battle_timer_found_run_out(const GameState *g, BattleTimer kind)
{
    uint8_t place = battle_timer_kind_cursors(g, kind);
    if (place == 0)
        place = battle_timer_order_length(g);
    uint8_t slot = battle_timer_order_at(g, (uint8_t)(place - 1));
    return (TimerOwner){ slot >= BATTLE_COMBATANTS, slot };
}

/* Whether the timer at a place of the order has run out. The order's empty
 * place names nobody, whose state the original reads past the kind's row:
 * only the original's memory has it, and this is the one place the port
 * reads it (docs/TODO.md). */
static bool has_run_out(const GameState *g, BattleTimer timer, uint8_t slot)
{
    if (slot < BATTLE_COMBATANTS)
        return battle_timer_run_out(g, timer, slot);
    return (battle_timer_flags_byte_unchecked(g, timer, slot) & TIMER_RUN_OUT) != 0;
}

// $C1:8876, $C1:8975, $C1:89B9, $C1:8A05, $C1:8A51, $C1:8BB9
/* The original's handlers for a status that wears off take the timer at the
 * empty place's slot as a combatant's: they start it again from its length,
 * count a run of the ten off, and on the tenth stop it and clear the status in
 * the stat block at that slot. The cells are past the kind's rows, in the
 * battle's other tables (the evading much better's state is the poison's
 * length for the third slot), which the game goes on reading: this is the one
 * place the port writes them (docs/TODO.md). */
void battle_timer_wears_off_past_the_rows(GameState *g, BattleTimer kind, uint8_t nobody, Statuses clears)
{
    uint8_t length = battle_timer_lengths_byte_unchecked(g, kind, nobody);
    set_battle_timer_flags_byte_unchecked(g, kind, nobody, TIMER_RUNNING);
    set_battle_timer_counts_byte_unchecked(g, kind, nobody, length);
    uint8_t left = (uint8_t)(battle_timer_runs_left_byte_unchecked(g, kind, nobody) - 1);
    set_battle_timer_runs_left_byte_unchecked(g, kind, nobody, left == 0 ? RUNS_A_CYCLE : left);
    if (left != 0)
        return;
    combatant_lose_statuses(g, nobody, clears);
    set_battle_timer_flags_byte_unchecked(g, kind, nobody, TIMER_STOPPED);
}

/* The due bits run from kind 0 at bit 12 down to the turn at bit 0. Seeing
 * to a kind clears the bits above them too, which nothing sets: the
 * original's masks keep the thirteen alone. */
enum { DUE_BITS = 0x1FFF };

static uint16_t due_bit(BattleTimer kind) { return (uint16_t)(1u << (BATTLE_TIMER_TURN - kind)); }

void battle_timer_seen_to(GameState *g, BattleTimer kind)
{
    set_battle_timer_kinds_due(g, battle_timer_kinds_due(g) & (uint16_t)(DUE_BITS & ~due_bit(kind)));
}

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
            slot = battle_timer_order_at(g, place);
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

/* The main loop counts its passes; the search looks on the odd ones. */
static bool pass_looks(const GameState *g) { return battle_loop_count(g) & 0x01; }

void battle_tick_timers(Snes *s)
{
    battle_count_timers_down(state_of(s));
    m_sep(s, CPU_FLAG_M);
    m_stop_at(s, SEARCH, DC_A | DC_X | DC_Y | DC_FLAGS);
}

/* Each due kind's handler run: the timed statuses' (battle/status_timers.c),
 * the turn's the main loop's (battle/main_loop.c), each a routine of its own
 * that the program calls as C and the machine's entry from the original's
 * loop. */
typedef struct {
    void (*handlers_begin)(void *ctx);
    bool (*see_to)(void *ctx, BattleTimer kind);
} DueCalls;

static bool run_due_handlers(GameState *g, const DueCalls *calls, void *ctx)
{
    calls->handlers_begin(ctx);
    for (unsigned kind = 0; kind < BATTLE_TIMER_KINDS; kind++) {
        ORIGINAL_EFFECT(set_battle_timer_kind_bit(g, due_bit(kind)));
        if (!(battle_timer_kinds_due(g) & due_bit(kind)))
            continue;
        if (!calls->see_to(ctx, (BattleTimer)kind))
            return false;
    }
    ORIGINAL_EFFECT(set_battle_timer_kind_bit(g, 0));
    return true;
}

// $C1:8290
/* The search, and the handlers of the kinds due on the passes that look.
 * False where a handler went no further. */
static bool see_to_the_timers(GameState *g, const DueCalls *calls, void *ctx)
{
    bool look = pass_looks(g);
    battle_find_run_out_timers(g, look);
    return !look || run_due_handlers(g, calls, ctx);
}

/* ---- As C ------------------------------------------------------------------------------------ */

typedef struct {
    GameState *game;
    Platform *pf;
} TimersContext;

static void handlers_begin(void *ctx) { (void)ctx; }

static bool see_to(void *ctx, BattleTimer kind)
{
    TimersContext *c = ctx;
    if (kind == BATTLE_TIMER_TURN)
        return battle_turn_given(c->game, c->pf);
    return battle_run_out_timer_seen_to(c->game, c->pf, kind);
}

bool battle_timers_seen_to(GameState *g, Platform *pf)
{
    static const DueCalls in_the_program = { handlers_begin, see_to };
    TimersContext c = { g, pf };
    bool done = false;
    TIMED_AS(g, SEARCH, done = see_to_the_timers(g, &in_the_program, &c));
    return done;
}

/* ---- On the machine ------------------------------------------------------------------------ */

/* Each handler is called as the original's loop calls it: the kind on the
 * stack under the call and its bit in its cell. The timed statuses'
 * handlers are run as the loop's JSR calls them; where the turn's is the
 * original's (a turn that names no combatant), it reads Y, which the search
 * left at the last kind and a handler before it leaves as it likes, and the
 * overflow flag, which the test of the due bits sets from their bit 14. */
enum { DUE_TEST_OVERFLOW = 0x4000 };

static void handlers_begin_on_the_machine(void *ctx) { ((Snes *)ctx)->cpu.y = BATTLE_TIMER_KINDS; }

static bool see_to_on_the_machine(void *ctx, BattleTimer kind)
{
    Snes *s = ctx;
    m_push16(s, (uint16_t)kind);
    s->cpu.x = (uint16_t)(kind * 2);
    s->cpu.a = 0;
    bool back;
    if (kind != BATTLE_TIMER_TURN) {
        back = battle_see_to_timer_as_called(s, THE_HANDLERS_CALL, kind);
    } else {
        m_set_flag(s, CPU_FLAG_V, battle_timer_kinds_due(state_of(s)) & DUE_TEST_OVERFLOW);
        back = battle_take_turn(s, THE_HANDLERS_CALL);
    }
    if (!back)
        return false;
    m_pull16(s);
    return true;
}

void battle_dispatch_expired_timers(Snes *s)
{
    static const DueCalls on_the_machine = { handlers_begin_on_the_machine, see_to_on_the_machine };
    if (see_to_the_timers(state_of(s), &on_the_machine, s))
        m_stop_at(s, REST_OF_PASS, DC_A | DC_X | DC_Y | DC_FLAGS);
}
