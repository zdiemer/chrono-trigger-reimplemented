/* Battle: the pause and the waits for the sound driver (battle/sound.c). */
#ifndef CT_GAME_BATTLE_SOUND_H
#define CT_GAME_BATTLE_SOUND_H

#include <stdbool.h>

#include "snes/system.h"

void battle_sit_out_the_pause(Snes *s);
void battle_sit_out_the_pause_long(Snes *s);
void battle_play_the_script_sound(Snes *s);
void battle_wait_start_music_fade_loop(Snes *s);
void battle_wait_music_fade_loop(Snes *s);
void battle_wait_music_fade_for_gradient_loop(Snes *s);

#endif
