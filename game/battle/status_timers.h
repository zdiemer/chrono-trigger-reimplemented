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
#include "snes/system.h"

/* See to a kind's run-out timer as the original's call at `site` would run
 * its handler (a sibling, battle/bridge.h's battle_run): false where the
 * handler didn't come back (a showing the original's code stopped in). */
bool battle_see_to_timer_as_called(Snes *s, uint32_t site, BattleTimer kind);

#endif
