/* The menus' music: the gate's screen's song (game/menu/sound.c; on the
 * original's machine, game/menu/sound_bridge.c). */
#ifndef CT_GAME_MENU_SOUND_H
#define CT_GAME_MENU_SOUND_H

#include <stdbool.h>

#include "platform/audio.h"
#include "snes/system.h"
#include "state.h"

/* The gate's own song; the story's note (story_game_notes) that the place's
 * song plays on in the gate's screen. */
enum { MENU_GATE_SONG = 0x3E, MENU_PLACE_SONG_PLAYS_ON = 0x40 };

/* Whether the gate's screen changes the song to its own: not where the
 * story lets the place's song play on, nor where that song is the gate's.
 * The song playing is noted either way, and whether it changes; and whether
 * it did, as the screen closes. */
bool menu_gate_song_wanted(GameState *g);
bool menu_gate_song_played(const GameState *g);

/* What the screen sends the driver: the song playing faded out; the gate's
 * song started, the driver saving the place's, and brought straight up;
 * the place's song started again, which the driver resumes where it was,
 * and faded back in to the place's volume. */
AudioCommand menu_fade_the_song_out(void);
AudioCommand menu_start_the_gate_song(void);
AudioCommand menu_bring_the_song_up(void);
AudioCommand menu_start_the_place_song_again(const GameState *g);
AudioCommand menu_fade_the_place_song_in(const GameState *g);

/* The entries: the change to the gate's song ($C2:CB8B), the task that
 * starts it ($C2:CBBF) and the wait for it to play ($C2:CBE0), and the
 * change back ($C2:CC0E). */
void menu_switch_to_the_gate_song(Snes *s);
void menu_play_the_gate_song(Snes *s);
void menu_wait_for_menu_music_playing_loop(Snes *s);
void menu_switch_back_to_the_field_song(Snes *s);

#endif
