/* Battle: the pause and the waits for the sound driver (battle/sound.c). */
#ifndef CT_GAME_BATTLE_SOUND_H
#define CT_GAME_BATTLE_SOUND_H

#include <stdbool.h>

#include "battle/bridge.h"
#include "platform/platform.h"
#include <stdint.h>

/* The pause's turn ($CD:3E44): a paused battle sits the pause out a frame
 * at a time. */
void battle_sit_out_the_pause(GameState *g, Platform *pf);
enum { BATTLE_PAUSE_ENTRY = 0xCD3E44 };
/* A sound a screen script asked for, played once ($CD:3B5F). */
void battle_play_the_script_sound(GameState *g, Platform *pf);
enum { BATTLE_SCRIPT_SOUND_ENTRY = 0xCD3B5F };
/* A sound effect played, panned where the caller says ($CD:3B71). */
void battle_play_a_sound(Platform *pf, uint8_t effect, uint8_t pan);
enum { BATTLE_PLAY_A_SOUND_ENTRY = 0xCD3B71 };

#endif
