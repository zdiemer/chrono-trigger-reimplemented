/* Battle: what happens when a combatant's status timer runs out
 * (battle/status_timers.c): the timer starts again, and the status acts --
 * poison hurts, a point of HP goes, MP comes back -- or counts towards
 * wearing off. The main loop's search for run-out timers (battle/timers.c)
 * runs it for each kind it found due. */
#ifndef CT_GAME_BATTLE_STATUS_TIMERS_H
#define CT_GAME_BATTLE_STATUS_TIMERS_H

#include <stdbool.h>
#include <stdint.h>

#include "battle/timers.h"
#include "platform/platform.h"
#include "state.h"

/* A kind's run-out timer seen to: false where it went no further. */
bool battle_run_out_timer_seen_to(GameState *g, Platform *pf, BattleTimer kind);

#endif
