/* Battle: the combatants' timers.
 *
 * Every combatant has thirteen timers, one of each kind: twelve for timed
 * statuses (how long a status lasts, or how often it acts, as poison does),
 * of which ten follow a status bit (battle/combatants.c), and the last its
 * turn, which fills its gauge and makes it ready to act. A timer is stopped,
 * running, or run out; while the combatant is stopped (combatant.h,
 * STATUS_STOP) only its stop timer counts. The main loop counts them all down
 * once a pass, then looks for run-out timers, at most one of each kind a pass
 * and the combatants in an order drawn at the start of the battle, and sees
 * to each: the statuses' in battle/status_timers.c, the turn's in
 * battle/main_loop.c (battle_run_queued_scripts).
 *
 * A timer's slot is a battle slot, or, where the search took the order's
 * empty place for one, $FF: its timer then lies past its kind's row, in the
 * tables after, as the original's indexed accesses have it. */
#ifndef CT_GAME_BATTLE_TIMERS_H
#define CT_GAME_BATTLE_TIMERS_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"

/* The kinds, by the status each times: stop, poison, slow, haste, evading
 * much better, the shield, the point of HP lost, the MP regained, the barrier.
 * Kinds 6 and 7 time no status the battle sets, and 11 follows the third
 * status byte's $02, whose meaning isn't worked out (docs/TODO.md). */
typedef enum {
    BATTLE_TIMER_STOP = 0,
    BATTLE_TIMER_POISON, BATTLE_TIMER_SLOW, BATTLE_TIMER_HASTE, BATTLE_TIMER_EVASION,
    BATTLE_TIMER_SHIELD, BATTLE_TIMER_6, BATTLE_TIMER_7, BATTLE_TIMER_HP_LOSS,
    BATTLE_TIMER_MP_REGAIN, BATTLE_TIMER_BARRIER, BATTLE_TIMER_11,
    BATTLE_TIMER_TURN,
    BATTLE_TIMER_KINDS
} BattleTimer;

/* Start a combatant's timer running, stop it, or have it run while
 * `status_on` (the timer of a status the combatant has). */
void battle_run_timer(GameState *g, BattleTimer timer, unsigned slot);
void battle_stop_timer(GameState *g, BattleTimer timer, unsigned slot);
void battle_follow_status(GameState *g, BattleTimer timer, unsigned slot, bool status_on);
/* Start it running afresh, past having run out; or from a count. */
void battle_restart_timer(GameState *g, BattleTimer timer, unsigned slot);
void battle_start_timer(GameState *g, BattleTimer timer, unsigned slot, uint8_t count);
void battle_set_timer_count(GameState *g, BattleTimer timer, unsigned slot, uint8_t count);
/* How long a status's timer runs for the combatant, once the status starts it. */
uint8_t battle_timer_length(const GameState *g, BattleTimer timer, unsigned slot);
bool battle_timer_run_out(const GameState *g, BattleTimer timer, unsigned slot);

/* The combatant whose timer of a kind the last search found run out: the one
 * before where the kind's search goes on in the order (the order's last where
 * it starts again at the top). */
uint8_t battle_timer_found_run_out(const GameState *g, BattleTimer kind);

/* A kind's run-out timer seen to: it is no longer due. */
void battle_timer_seen_to(GameState *g, BattleTimer kind);

/* The combatant at a place in the order the timers are looked for in (a
 * place past the order reads on, as the original does: battle/timers.c). */
uint8_t battle_timer_order_at(const GameState *g, uint8_t place);

/* Count every running timer down a step. */
void battle_count_timers_down(GameState *g);

/* Look for run-out timers: the party's turn timers shown on their gauges,
 * then, on the passes that look, a run-out timer of each kind marked due. */
void battle_find_run_out_timers(GameState *g, bool look);

#endif
