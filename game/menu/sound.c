/* The menus' music: the song of the gate's screen at the End of Time.
 *
 * The gate's screen (the party exchange, menu/exchange.c) plays a song of
 * its own over whatever the field or the world map was playing, unless the
 * story lets the place's song play on or that song is already the gate's.
 * Opening, the screen fades the music out and starts a task that waits for
 * the fade to run out, starts the gate's song with the driver saving the one
 * it replaces, brings the music straight up, and waits for the song to be
 * playing; the screen itself waits for that task before it fades in.
 * Closing, it fades the gate's song out, waits for the fade, and starts the
 * song it replaced again, which the driver resumes where it was, fading it
 * back in to the place's volume.
 *
 * The waits ask the sound driver how it is getting on (platform/audio.h):
 * how many steps its music's fade has left, and whether a song's tracks are
 * playing. The waits for a fade let a frame go by between looks; the wait
 * for the song to be playing doesn't, and spins until the driver says so.
 * This file decides which song and what to send; the sending and the
 * waiting, on the original's machine, are menu/sound_bridge.c. */
#include "menu/sound.h"

#include "vars.h"

/* The menus opened on one of the field's locations rather than the world
 * map (menu_place_items' first, noted as they open), whose song is the
 * field's; the world map's is the one its scripts asked for. */
enum { NOT_ON_A_LOCATION = 0 };

static uint8_t song_playing(const GameState *g)
{
    return menu_place_items(g, 0) != NOT_ON_A_LOCATION ? current_song(g) : menu_song_playing(g);
}

// $C2:CB8B
bool menu_gate_song_wanted(GameState *g)
{
    set_menu_exchange_song_changed(g, false);
    if (story_game_notes(g) & MENU_PLACE_SONG_PLAYS_ON)
        return false;
    uint8_t song = song_playing(g);
    set_menu_exchange_song_before(g, song);
    if (song == MENU_GATE_SONG)
        return false;
    set_menu_exchange_song_changed(g, true);
    return true;
}

bool menu_gate_song_played(const GameState *g) { return menu_exchange_song_changed(g) != 0; }

/* The fades and the starts: a song faded out over eight steps; the gate's
 * song started at the middle volume, then brought straight up to full; the
 * place's song started again and faded back in over sixteen steps to the
 * volume it plays at (full where none is noted). */
enum { FADE_OUT_STEPS = 8, FADED = 1, AT_ONCE = 1, FADE_IN_STEPS = 16, MIDDLE = 0x80 };

static AudioCommand fade_music(uint8_t steps, uint8_t volume)
{
    return (AudioCommand){ .command = AU_FADE_MUSIC, .p1 = steps, .p2 = volume, .p3 = volume, .count = 3 };
}

static AudioCommand start_song(uint8_t command, uint8_t song)
{
    return (AudioCommand){ .command = command, .p1 = song, .p2 = MIDDLE, .p3 = MIDDLE, .count = 3 };
}

AudioCommand menu_fade_the_song_out(void) { return fade_music(FADE_OUT_STEPS, FADED); }

AudioCommand menu_start_the_gate_song(void) { return start_song(AU_START_SONG_SAVING, MENU_GATE_SONG); }

AudioCommand menu_bring_the_song_up(void) { return fade_music(AT_ONCE, AU_FULL_VOLUME); }

AudioCommand menu_start_the_place_song_again(const GameState *g)
{
    return start_song(AU_START_SONG_SECOND_SET, menu_exchange_song_before(g));
}

AudioCommand menu_fade_the_place_song_in(const GameState *g)
{
    uint8_t volume = menu_field_song_volume(g);
    return fade_music(FADE_IN_STEPS, volume != 0 ? volume : AU_FULL_VOLUME);
}
