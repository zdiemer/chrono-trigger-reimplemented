/* The world map's music and the sprite-object mode's sound request
 * (game/overworld/music.c). */
#ifndef CT_GAME_OVERWORLD_MUSIC_H
#define CT_GAME_OVERWORLD_MUSIC_H

#include <stdbool.h>

#include "overworld/overworld.h"
#include "platform/audio.h"

/* The request a script or the music leaves (menu_sound_request) is passed on
 * to the driver's command cells when the mailbox lets it; the main loop sends
 * a command waiting there, the mailbox marked as sending meanwhile. */
bool overworld_pass_on_sound(GameState *g);
bool overworld_sound_waits(const GameState *g);
bool overworld_sound_is_being_sent(const GameState *g);
void overworld_mark_sound_sending(GameState *g, bool sending);
/* The sound command a request left waiting, to send to the driver: false
 * where none waits; the mailbox is marked as being sent until
 * overworld_waiting_sound_sent. */
bool overworld_take_the_waiting_sound(GameState *g, AudioCommand *waiting);
void overworld_waiting_sound_sent(GameState *g);

/* Play a song: the request passed on at once if the mailbox lets it, and the
 * song noted as playing where it was. */
void overworld_play_song(GameState *g, uint8_t song);
void overworld_play_the_song_in_view(GameState *g);
bool overworld_restore_the_tempo(GameState *g);

/* The song of where the party is on the world map, as a scene's script
 * starts it (straight to the driver, not through the request): the song a
 * screen keeps, or the song of the region the location's place is in, which
 * comes back too; it is noted as the song playing. And the command that
 * starts a song as the map's music does. */
typedef struct {
    uint8_t song;
    bool of_a_region;
    uint8_t region;
} LocationSong;
LocationSong overworld_location_song(GameState *g);
AudioCommand overworld_song_command(uint8_t song);

/* The guard's: whether the music's object is on one of its steps, and where
 * it looks up the party's region, the party is on the region map. */
bool overworld_region_song_can_play(const GameState *g, MenuObject self);

#endif
