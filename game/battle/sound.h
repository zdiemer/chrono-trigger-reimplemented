/* Battle: pausing the sound and waiting for the sound driver (battle/sound.c). */
#ifndef CT_GAME_BATTLE_SOUND_H
#define CT_GAME_BATTLE_SOUND_H

#include <stdbool.h>

#include "snes/system.h"

bool battle_sound_buffer_reachable(Snes *s);
void battle_sound_pause(Snes *s);
void battle_sound_resume(Snes *s);
void battle_wait_start_music_fade_loop(Snes *s);
void battle_wait_music_fade_loop(Snes *s);
void battle_wait_music_fade_for_gradient_loop(Snes *s);

#endif
