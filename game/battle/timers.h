/* Battle: the combatants' timers.
 *
 * Every combatant has thirteen timers, one of each kind: twelve for timed
 * statuses (how long a status lasts, or how often it acts, as poison does),
 * of which ten follow a status bit (battle/combatants.c), and the last its
 * turn, which fills its gauge and makes it ready to act. A timer is stopped,
 * running, or run out; while the combatant is stopped (combatant.h,
 * STATUS_STOP) only its stop timer counts. The main loop counts them all down
 * once a pass, then looks for run-out timers, at most one of each kind a pass
 * and the combatants in an order drawn at the start of the battle, and runs
 * each kind's handler for them (the turn's is battle/main_loop.c's
 * battle_run_queued_scripts; the statuses' are still the original's). */
#ifndef CT_GAME_BATTLE_TIMERS_H
#define CT_GAME_BATTLE_TIMERS_H

#include <stdbool.h>

#include "state.h"

typedef enum {
    BATTLE_TIMER_STOP = 0,
    BATTLE_TIMER_STATUS_1, BATTLE_TIMER_STATUS_2, BATTLE_TIMER_STATUS_3, BATTLE_TIMER_STATUS_4,
    BATTLE_TIMER_STATUS_5, BATTLE_TIMER_STATUS_6, BATTLE_TIMER_STATUS_7, BATTLE_TIMER_STATUS_8,
    BATTLE_TIMER_STATUS_9, BATTLE_TIMER_STATUS_10, BATTLE_TIMER_STATUS_11,
    BATTLE_TIMER_TURN,
    BATTLE_TIMER_KINDS
} BattleTimer;

/* Start a combatant's timer running, stop it, or have it run while
 * `status_on` (the timer of a status the combatant has). */
void battle_run_timer(GameState *g, BattleTimer timer, unsigned slot);
void battle_stop_timer(GameState *g, BattleTimer timer, unsigned slot);
void battle_follow_status(GameState *g, BattleTimer timer, unsigned slot, bool status_on);

/* Count every running timer down a step. */
void battle_count_timers_down(GameState *g);

/* Look for run-out timers: the party's turn timers shown on their gauges,
 * then, on the passes that look, a run-out timer of each kind marked due. */
void battle_find_run_out_timers(GameState *g, bool look);

#endif
