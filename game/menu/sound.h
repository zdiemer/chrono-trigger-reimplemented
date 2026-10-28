/* The menus' music: the gate's screen's song (game/menu/sound.c; on the
 * original's machine, game/menu/sound_bridge.c). */
#ifndef CT_GAME_MENU_SOUND_H
#define CT_GAME_MENU_SOUND_H

#include <stdbool.h>

#include "platform/audio.h"
#include "platform/platform.h"
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

/* The change to the gate's song ($C2:CB8B), the task that starts it
 * ($C2:CBBF, with the wait for it to play, $C2:CBE0), and the change back
 * ($C2:CC0E), each one sequence for the machine and for C: what is sent and
 * from where, the waits for a fade a frame at a time, the machine's widths
 * as the original's code sets them, the screen's fade out, the wait for the
 * song to be playing, and what the decision leaves (scaffolding). */
typedef enum { GATE_SONG_FADED_OUT, GATE_SONG_STARTED, GATE_SONG_FADED_OUT_AGAIN, PLACE_SONG_STARTED_AGAIN } GateSongSend;
typedef enum { GATE_SONG_AT_THE_TASK_S_START, GATE_SONG_AT_THE_GATE_S_FADE, GATE_SONG_AT_THE_PLACE_S_FADE } GateSongPlace;
typedef struct {
    bool (*send)(void *ctx, GateSongSend at, AudioCommand c);
    bool (*send_song_and_fade)(void *ctx, GateSongSend at, AudioCommand song, AudioCommand fade);
    bool (*wait)(void *ctx, GateSongPlace at);
    void (*registers_at)(void *ctx, GateSongPlace at);
    bool (*fade_the_screen_out)(void *ctx);
    bool (*wait_for_the_music)(void *ctx);
    void (*decided)(void *ctx);
} GateSongSteps;
bool menu_switch_to_the_gate_song_with(GameState *g, Platform *pf, const GateSongSteps *n, void *ctx);
bool menu_play_the_gate_song_with(GameState *g, Platform *pf, const GateSongSteps *n, void *ctx);
bool menu_switch_back_to_the_field_song_with(GameState *g, Platform *pf, const GateSongSteps *n, void *ctx);

/* As C: the changes, and the task, which the menus' tasks run (menu/tasks.c,
 * by its start). */
bool menu_switch_to_the_gate_song(GameState *g, Platform *pf);
bool menu_switch_back_to_the_field_song(GameState *g, Platform *pf);
enum { MENU_GATE_SONG_TASK_START = 0xCBBF };
void menu_play_the_gate_song(GameState *g, Platform *pf);

/* The entries. */
void menu_switch_to_the_gate_song_entry(Snes *s);
void menu_play_the_gate_song_entry(Snes *s);
void menu_wait_for_menu_music_playing_loop(Snes *s);
void menu_switch_back_to_the_field_song_entry(Snes *s);

#endif
