/* Battle: the main loop in bank C1 ($C1:8000-$C1:883C). */
#ifndef CT_GAME_BATTLE_MAIN_LOOP_H
#define CT_GAME_BATTLE_MAIN_LOOP_H

#include <stdbool.h>

#include "platform/platform.h"
#include "snes/system.h"
#include "state.h"

/* $FD:A982: the battle's work on its turns cleared, as a battle starts. */
void battle_clear_the_turn_work(GameState *g);
void battle_clear_the_turn_work_entry(Snes *s);

void battle_main_loop_start(Snes *s);

/* The battle from its start ($C1:8000) to its end, a pass a frame, as C:
 * false where it went no further, true once it leaves for the field. */
bool battle_main_loop_runs(GameState *g, Platform *pf);
/* The turn's handler ($C1:8461), as C, for the timers'. */
bool battle_turn_given(GameState *g, Platform *pf);
void battle_main_loop(Snes *s);
void battle_main_loop_body(Snes *s);
void battle_main_loop_tail(Snes *s);
void battle_give_turn(Snes *s);

/* The turn's handler, from the call at `site` (the loop's, or the timers'
 * dispatch): the port where its guard holds, else the original's. */
bool battle_take_turn(Snes *s, uint32_t site);
bool battle_turn_names_combatant(Snes *s);
bool battle_pass_ends_plainly(Snes *s);

#endif
