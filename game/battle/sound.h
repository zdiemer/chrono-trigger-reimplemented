/* Battle: the pause and the waits for the sound driver (battle/sound.c). */
#ifndef CT_GAME_BATTLE_SOUND_H
#define CT_GAME_BATTLE_SOUND_H

#include <stdbool.h>

#include "battle/bridge.h"
#include "platform/platform.h"
#include "snes/system.h"

void battle_sit_out_the_pause_entry(Snes *s);
/* The pause's turn ($CD:3E44): a paused battle sits the pause out a frame
 * at a time. */
void battle_sit_out_the_pause(GameState *g, Platform *pf);
enum { BATTLE_PAUSE_ENTRY = 0xCD3E44 };
void battle_sit_out_the_pause_long(Snes *s);
void battle_play_the_script_sound_entry(Snes *s);
/* A sound a screen script asked for, played once ($CD:3B5F). */
void battle_play_the_script_sound(GameState *g, Platform *pf);
enum { BATTLE_SCRIPT_SOUND_ENTRY = 0xCD3B5F };
void battle_play_a_sound_entry(Snes *s);
/* A sound effect played, panned where the caller says ($CD:3B71). */
void battle_play_a_sound(Platform *pf, uint8_t effect, uint8_t pan);
enum { BATTLE_PLAY_A_SOUND_ENTRY = 0xCD3B71 };

/* Play a sound effect from the frame work (the window's, a battle message's),
 * panned at `pan`, as the original's JSR at `site` calls $CD:3B71 with what it
 * keeps (`kept`): the work as C, under the call's return, where it makes its
 * call to the sound driver; where the entry's own preconditions don't hold
 * (the battle's page, its widths), the original's call instead. Leaves the
 * registers as the original's routine does: the pan in A's low byte and in X.
 * False where the call stopped and the original has the machine. */
bool battle_play_a_sound_from(Snes *s, uint32_t site, uint8_t effect, uint8_t pan, BattleFrame kept);
void battle_wait_music_fade_loop(Snes *s);

#endif
