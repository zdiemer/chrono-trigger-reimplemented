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

#include "charge.h"
#include "menu/fades.h"
#include "menu/menu.h"
#include "port/native.h"
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

/* ---- The changes and the task, one sequence each ------------------------------------------------- */

/* Where the waits for a fade ask the driver how many steps it has left. */
static const uint32_t FADE_ASKED_AT[] = { [GATE_SONG_AT_THE_GATE_S_FADE] = 0xC2CBC1, [GATE_SONG_AT_THE_PLACE_S_FADE] = 0xC2CC28 };

/* The music's fade run out, a frame at a time. */
static bool fade_waited_out(Platform *pf, GateSongPlace at, const GateSongSteps *n, void *ctx)
{
    for (;;) {
        n->registers_at(ctx, at);
        if (au_music_fade_left(pf, PF_AT(FADE_ASKED_AT[at])) == 0)
            return true;
        if (!n->wait(ctx, at))
            return false;
    }
}

// $C2:CB8B
/* The place's song faded out, and the task started that brings the gate's
 * in. */
bool menu_switch_to_the_gate_song_with(GameState *g, Platform *pf, const GateSongSteps *n, void *ctx)
{
    (void)pf;
    if (menu_gate_song_wanted(g)) {
        if (!n->send(ctx, GATE_SONG_FADED_OUT, menu_fade_the_song_out()))
            return false;
        n->registers_at(ctx, GATE_SONG_AT_THE_TASK_S_START);
        menu_start_task(g, MENU_DESCRIPTION_TASK, MENU_GATE_SONG_TASK_START);
    }
    n->decided(ctx);
    return true;
}

// $C2:CBBF, $C2:CBE0
/* The task: once the fade has run out, the gate's song, its volume brought
 * up at once; then the wait for it to be playing, which ends the task. */
bool menu_play_the_gate_song_with(GameState *g, Platform *pf, const GateSongSteps *n, void *ctx)
{
    (void)g;
    return fade_waited_out(pf, GATE_SONG_AT_THE_GATE_S_FADE, n, ctx)
           && n->send_song_and_fade(ctx, GATE_SONG_STARTED, menu_start_the_gate_song(), menu_bring_the_song_up())
           && n->wait_for_the_music(ctx);
}

// $C2:CC0E
/* The gate's song faded out with the screen, and the place's back from
 * where the driver saved it, fading in. */
bool menu_switch_back_to_the_field_song_with(GameState *g, Platform *pf, const GateSongSteps *n, void *ctx)
{
    bool played = menu_gate_song_played(g);
    if (played && !n->send(ctx, GATE_SONG_FADED_OUT_AGAIN, menu_fade_the_song_out()))
        return false;
    if (!n->fade_the_screen_out(ctx))
        return false;
    return !played
           || (fade_waited_out(pf, GATE_SONG_AT_THE_PLACE_S_FADE, n, ctx)
               && n->send_song_and_fade(ctx, PLACE_SONG_STARTED_AGAIN, menu_start_the_place_song_again(g),
                                        menu_fade_the_place_song_in(g)));
}

/* ---- As C ---- */

/* The driver's calls, from the song's start's and the fade's routines. */
enum { SOUND_DRIVER = 0xC70004, SEND_THE_SONG = 0xC2CBF3, SEND_THE_FADE = 0xC2CC09 };
enum { THE_DRIVER_READS_A_HIGH = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

typedef struct {
    GameState *game;
    Platform *pf;
} SongInC;

static bool sent(void *ctx, GateSongSend at, AudioCommand c)
{
    (void)at;
    SongInC *n = ctx;
    au_command(n->pf, PF_CALL_DC(SEND_THE_FADE, SOUND_DRIVER, THE_DRIVER_READS_A_HIGH), c);
    return true;
}

static bool song_and_fade_sent(void *ctx, GateSongSend at, AudioCommand song, AudioCommand fade)
{
    (void)at;
    SongInC *n = ctx;
    au_command(n->pf, PF_CALL_DC(SEND_THE_SONG, SOUND_DRIVER, THE_DRIVER_READS_A_HIGH), song);
    au_command(n->pf, PF_CALL_DC(SEND_THE_FADE, SOUND_DRIVER, THE_DRIVER_READS_A_HIGH), fade);
    return true;
}

static bool a_frame_waited(void *ctx, GateSongPlace at)
{
    (void)at;
    menu_task_waits(((SongInC *)ctx)->game, 1);
    return true;
}

static void no_registers(void *ctx, GateSongPlace at) { (void)ctx, (void)at; }

static bool screen_faded_out(void *ctx)
{
    menu_fade_to_black(((SongInC *)ctx)->game);
    return true;
}

/* The original spins on the driver's status without letting a frame go by
 * (an idle loop on the machine, sound_bridge.c), so it waits as long as the
 * driver takes to start the song. Nothing here may spin on a device: the
 * driver a host plays passes its time with the frames, so as C the task
 * looks again once a frame, until the song's tracks are playing. */
static bool music_waited_for(void *ctx)
{
    SongInC *n = ctx;
    while (au_music_playing(n->pf, PF_AT(0xC2CBE0)) == 0)
        menu_task_waits(n->game, 1);
    return true;
}

static void nothing_left(void *ctx) { (void)ctx; }

static const GateSongSteps song_in_c = {
    sent, song_and_fade_sent, a_frame_waited, no_registers, screen_faded_out, music_waited_for, nothing_left,
};

bool menu_switch_to_the_gate_song(GameState *g, Platform *pf)
{
    SongInC c = { g, pf };
    return menu_switch_to_the_gate_song_with(g, pf, &song_in_c, &c);
}

bool menu_switch_back_to_the_field_song(GameState *g, Platform *pf)
{
    SongInC c = { g, pf };
    return menu_switch_back_to_the_field_song_with(g, pf, &song_in_c, &c);
}

void menu_play_the_gate_song(GameState *g, Platform *pf)
{
    SongInC c = { g, pf };
    (void)menu_play_the_gate_song_with(g, pf, &song_in_c, &c);
}
