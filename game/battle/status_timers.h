/* Battle: what happens when a combatant's status timer runs out
 * (battle/status_timers.c): the timer starts again, and the status acts --
 * poison hurts, a point of HP goes, MP comes back -- or counts towards
 * wearing off. The main loop's search for run-out timers (battle/timers.c)
 * calls it for each kind it found due. */
#ifndef CT_GAME_BATTLE_STATUS_TIMERS_H
#define CT_GAME_BATTLE_STATUS_TIMERS_H

#include <stdbool.h>
#include <stdint.h>

#include "battle/timers.h"
#include "snes/system.h"

/* See to a kind's run-out timer, for the combatant the search found. The
 * showing of a number is the original's, called out to: false where it didn't
 * come back. */
bool battle_see_to_run_out_timer(Snes *s, BattleTimer kind);

/* Each kind's handler as the original's table names it ($C1:B92D), and its
 * registered entry. */
uint32_t battle_status_timer_handler(BattleTimer kind);
extern void (*const battle_status_timer_entries[BATTLE_TIMER_TURN])(Snes *);

#endif
