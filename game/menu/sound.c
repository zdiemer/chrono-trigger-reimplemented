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
 * This file decides which song and what to send, and sends it. */
#include "menu/sound.h"

#include "charge.h"
#include "menu/fades.h"
#include "menu/menu.h"
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

/* ---- The changes and the task --------------------------------------------------------------------- */

/* The driver's calls, from the song's start's and the fade's routines. */
enum { SOUND_DRIVER = 0xC70004, SEND_THE_SONG = 0xC2CBF3, SEND_THE_FADE = 0xC2CC09 };

static void send_the_fade(Platform *pf, AudioCommand fade) { au_command(pf, PF_CALL(SEND_THE_FADE, SOUND_DRIVER), fade); }

static void send_the_song_and_fade(Platform *pf, AudioCommand song, AudioCommand fade)
{
    au_command(pf, PF_CALL(SEND_THE_SONG, SOUND_DRIVER), song);
    send_the_fade(pf, fade);
}

/* The music's fade run out, a frame at a time, the driver asked from the
 * gate's wait or the place's. */
enum { THE_GATE_S_FADE_ASKED = 0xC2CBC1, THE_PLACE_S_FADE_ASKED = 0xC2CC28 };

static void fade_waited_out(GameState *g, Platform *pf, uint32_t asked_at)
{
    while (au_music_fade_left(pf, PF_AT(asked_at)) != 0)
        menu_task_waits(g, 1);
}

// $C2:CB8B
/* The place's song faded out, and the task started that brings the gate's
 * in. */
void menu_switch_to_the_gate_song(GameState *g, Platform *pf)
{
    if (menu_gate_song_wanted(g)) {
        send_the_fade(pf, menu_fade_the_song_out());
        menu_start_task(g, MENU_DESCRIPTION_TASK, MENU_GATE_SONG_TASK_START);
    }
}

// $C2:CBBF, $C2:CBE0
/* The task: once the fade has run out, the gate's song, its volume brought
 * up at once; then the wait for it to be playing, which ends the task. The
 * original spins on the driver's status without letting a frame go by, so
 * it waits as long as the driver takes to start the song. Nothing here may
 * spin on a device: the driver a host plays passes its time with the
 * frames, so the task looks again once a frame, until the song's tracks are
 * playing. */
void menu_play_the_gate_song(GameState *g, Platform *pf)
{
    fade_waited_out(g, pf, THE_GATE_S_FADE_ASKED);
    send_the_song_and_fade(pf, menu_start_the_gate_song(), menu_bring_the_song_up());
    while (au_music_playing(pf, PF_AT(0xC2CBE0)) == 0)
        menu_task_waits(g, 1);
}

// $C2:CC0E
/* The gate's song faded out with the screen, and the place's back from
 * where the driver saved it, fading in. */
void menu_switch_back_to_the_field_song(GameState *g, Platform *pf)
{
    bool played = menu_gate_song_played(g);
    if (played)
        send_the_fade(pf, menu_fade_the_song_out());
    menu_fade_to_black(g);
    if (!played)
        return;
    fade_waited_out(g, pf, THE_PLACE_S_FADE_ASKED);
    send_the_song_and_fade(pf, menu_start_the_place_song_again(g), menu_fade_the_place_song_in(g));
}
