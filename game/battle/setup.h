/* Battle setup: the tables a battle builds once before it starts. */
#ifndef CT_GAME_BATTLE_SETUP_H
#define CT_GAME_BATTLE_SETUP_H

#include "snes/system.h"

/* battle/setup.c */
void battle_setup_state(Snes *s);
void battle_setup_graphics(Snes *s);

#endif
