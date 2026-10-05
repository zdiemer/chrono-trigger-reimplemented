/* The world map's music and the sprite-object mode's sound request
 * (game/overworld/music.c). */
#ifndef CT_GAME_OVERWORLD_MUSIC_H
#define CT_GAME_OVERWORLD_MUSIC_H

#include <stdbool.h>

#include "overworld/overworld.h"

/* The request a script or the music leaves (menu_sound_request) is passed on
 * to the driver's command cells when the mailbox lets it; the main loop sends
 * a command waiting there, the mailbox marked as sending meanwhile. */
bool overworld_pass_on_sound(GameState *g);
bool overworld_sound_waits(const GameState *g);
bool overworld_sound_is_being_sent(const GameState *g);
void overworld_mark_sound_sending(GameState *g, bool sending);

/* The guard's: whether the music's object is on one of its steps, and where
 * it looks up the party's region, the party is on the region map. */
bool overworld_region_song_can_play(const GameState *g, MenuObject self);

#endif
