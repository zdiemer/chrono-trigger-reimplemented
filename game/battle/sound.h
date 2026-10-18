/* Battle: the pause and the waits for the sound driver (battle/sound.c). */
#ifndef CT_GAME_BATTLE_SOUND_H
#define CT_GAME_BATTLE_SOUND_H

#include <stdbool.h>

#include "battle/bridge.h"
#include "snes/system.h"

void battle_sit_out_the_pause(Snes *s);
void battle_sit_out_the_pause_long(Snes *s);
void battle_play_the_script_sound(Snes *s);
void battle_play_a_sound(Snes *s);

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
