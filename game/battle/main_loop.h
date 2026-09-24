/* Battle: the main loop in bank C1 ($C1:8000-$C1:883C). */
#ifndef CT_GAME_BATTLE_MAIN_LOOP_H
#define CT_GAME_BATTLE_MAIN_LOOP_H

#include "snes/system.h"

void battle_main_loop_start(Snes *s);
void battle_main_loop(Snes *s);
void battle_main_loop_body(Snes *s);
void battle_main_loop_tail(Snes *s);
void battle_run_queued_scripts(Snes *s);
bool battle_pass_ends_plainly(Snes *s);

#endif
