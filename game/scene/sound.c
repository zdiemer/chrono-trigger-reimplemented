/* Scenes: the scene program's messages to the sound driver.
 *
 * A scene tells the sound driver what to do the way the rest of the game does
 * -- a command and its parameters in the sound command buffer, sent through the
 * driver's entry (the frame's song, a script's command: scene/frame.c,
 * scene/script.c) -- and, for its own sounds, it sends the buffer to the
 * driver itself and waits for the driver to echo the command. It also waits on
 * the song: the driver shows a marker the song sets as it plays (docs/SOUND.md,
 * "What the game reads back"), which the program waits for to keep a scene in
 * time with its music, and whether the music is playing. The race's engine is its own message (scene/race.c). */
#include "scene/scene.h"

#include "charge.h"
#include "platform/audio.h"
#include "port/vars_ram.h"
#include "vars.h"

/* The driver's status on port 2: its low nibble is the marker the song sets,
 * how far it has got. And on port 3, whose low nibble is which of the music's
 * tracks are playing (docs/SOUND.md, "What the game reads back"). */
enum { MARKER_PORT = 2, PROGRESS = 0x0F, PLAYING_PORT = 3, MUSIC_TRACKS = 0x0F };

MusicStatus scene_music_status(Platform *pf)
{
    uint8_t first = au_status(pf, PF_AT(0xC30C2C), MARKER_PORT);
    uint8_t second = au_status(pf, PF_AT(0xC30C30), MARKER_PORT);
    return (MusicStatus){ first, second, first == second };
}

uint8_t scene_music_progress(MusicStatus music) { return music.first & PROGRESS; }

/* The music is at `level` or past it (compared by the sign of the difference,
 * as the original compares it). */
bool scene_music_reached(MusicStatus music, uint8_t level)
{
    return music.steady && (int8_t)(scene_music_progress(music) - level) >= 0;
}

/* Which of the music's tracks are playing: nonzero once a song has started. */
static uint8_t music_playing(Platform *pf) { return au_status(pf, PF_AT(0x7E5AAA), PLAYING_PORT) & MUSIC_TRACKS; }

// $7E:3780
/* The command in the sound command buffer to the driver, parameters first and
 * the command last, the way the driver's entry sends it but without its
 * dispatch. Returns the command, which the echo wait compares with what the
 * driver sends back. */
static uint8_t send_sound_command(const GameState *g, Platform *pf)
{
    au_driver_write(pf, PF_AT(0x7E3786), 3, sound_command_parameters(g, 2));
    au_driver_write(pf, PF_AT(0x7E378C), 2, sound_command_parameters(g, 1));
    au_driver_write(pf, PF_AT(0x7E3792), 1, sound_command_parameters(g, 0));
    uint8_t command = sound_command(g);
    au_driver_write(pf, PF_AT(0x7E3798), 0, command);
    return command;
}

/* How many passes the echo wait makes before it sends the command again. */
enum { ECHO_PASSES = 0x0400 };

static uint8_t send_and_wait_for_the_echo(GameState *g, Platform *pf);

/* ---- The sound put back to normal ------------------------------------------------------------ */

// $7E:3177
/* What a scene leaves of the sound driver's state, put back as the next scene
 * starts and as the program ends: the tempo and the pitch unbent at once, the
 * effects' pitch unbent, both pairs of effect voices stopped, and the music at
 * full volume at once. Each command goes through the program's own send,
 * which waits for the driver to take it. A scene that sends no songs leaves
 * the sound alone. */
enum { SCENE_SEND = 0x7E377E };

static const struct {
    uint8_t command, p1, p2;
} back_to_normal[] = {
    { AU_BEND_BOTH, 0, 0 },
    { AU_BEND_EFFECTS_PITCH, 0, 0 },
    { AU_STOP_EFFECTS, 0, 0 },
    { AU_STOP_EFFECTS, 1, 0 },
    { AU_FADE_MUSIC, 0, AU_FULL_VOLUME },
};
enum { BACK_TO_NORMAL = sizeof back_to_normal / sizeof back_to_normal[0] };

static void into_the_buffer(GameState *g, unsigned n)
{
    set_sound_command(g, back_to_normal[n].command);
    set_sound_command_parameters(g, 0, back_to_normal[n].p1);
    set_sound_command_parameters(g, 1, back_to_normal[n].p2);
}

/* The command in the buffer sent, and the driver's echo waited for. */
static uint8_t send_and_wait_for_the_echo(GameState *g, Platform *pf)
{
    for (;;) {
        uint8_t command = send_sound_command(g, pf);
        if (au_driver_wait_echo_or_give_up(pf, PF_LOOP(0x7E379B, 0x7E37A5), 0x7E37A3, command, ECHO_PASSES))
            return command;
    }
}

void scene_sound_back_to_normal(GameState *g, Platform *pf)
{
    if (scene_flags(g) & SCENE_FLAG_NO_SONGS)
        return;
    for (unsigned n = 0; n < BACK_TO_NORMAL; n++) {
        into_the_buffer(g, n);
        TIMED_AS(g, SCENE_SEND, send_and_wait_for_the_echo(g, pf));
    }
}

void scene_sends_the_command(GameState *g, Platform *pf) { TIMED_AS(g, SCENE_SEND, send_and_wait_for_the_echo(g, pf)); }

bool scene_music_is_playing(Platform *pf) { return music_playing(pf) != 0; }
