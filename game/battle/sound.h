/* Battle: pausing the sound and waiting for the sound driver (battle/sound.c). */
#ifndef CT_GAME_BATTLE_SOUND_H
#define CT_GAME_BATTLE_SOUND_H

#include <stdbool.h>

#include "snes/system.h"

bool battle_sound_bank_reaches_1e00(Snes *s);
void battle_sound_pause(Snes *s);
void battle_sound_resume(Snes *s);
void battle_wait_sound_port1_or_2a1f(Snes *s);
void battle_wait_sound_port1_0d9c(Snes *s);
void battle_wait_sound_port1_0db7(Snes *s);

#endif
