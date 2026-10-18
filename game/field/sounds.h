/* Field: the sounds the field plays of its own, and the music round a battle
 * (field/sounds.c). */
#ifndef CT_GAME_FIELD_SOUNDS_H
#define CT_GAME_FIELD_SOUNDS_H

#include <stdbool.h>

#include "platform/platform.h"
#include "snes/system.h"
#include "state.h"

/* A switch pressed ($C0:1B90) and a chest opened ($C0:1BA7), panned on the
 * party's leader. */
void field_play_the_switch_sound(GameState *g, Platform *pf);
void field_play_the_chest_sound(GameState *g, Platform *pf);
enum { FIELD_SWITCH_SOUND_ENTRY = 0xC01B90, FIELD_CHEST_SOUND_ENTRY = 0xC01BA7 };

void field_play_the_switch_sound_entry(Snes *s);
void field_play_the_chest_sound_entry(Snes *s);
bool field_sound_at_the_leader_can_play(Snes *s);

/* The battle's song as it starts ($C0:0008), and the field's music back once
 * it is over ($C0:000B): the entries the battle runs, as the original calls
 * them. */
enum { FIELD_BATTLE_SONG_ENTRY = 0xC00008, FIELD_MUSIC_BACK_ENTRY = 0xC0000B };
void field_start_the_battle_song(Snes *s);
void field_bring_the_music_back(Snes *s);

#endif
