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

#include "platform/audio.h"
#include "port/machine.h"
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

/* The guard also checks that absolute addressing reaches the APU ports, which
 * the audio service addresses in bank $00. */
bool scene_wait_for_music_present(Snes *s)
{
    return m_bank_reaches_io(s) && m_code_matches(s, 0x7E5AAA, 7, 0x441A7AC228BA6C77ull);
}

// $7E:5AAA
/* One pass of the wait for the music to start playing, then return. Idle
 * entry, 8-bit A holding the tracks playing. */
void scene_wait_for_music_loop(Snes *s)
{
    uint8_t playing = music_playing(m_platform(s));
    m_set_a8(s, playing);
    m_set_nz8(s, playing);
    if (playing != 0)
        m_rts(s);
}

/* The routine at $7E:377E, whose absolute addressing must reach the sound
 * command buffer and the APU ports. */
bool scene_send_sound_command_present(Snes *s)
{
    return m_bank_reaches_io(s) && m_code_matches(s, 0x7E377E, 40, 0x4A45A38F11AEF81Bull);
}

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

/* The driver echoes a command on port 0 once it has taken it. */
static uint8_t echo(Platform *pf) { return au_driver_read(pf, PF_AT(0x7E379B), 0); }

/* How many passes the echo wait makes before it sends the command again. */
enum { ECHO_PASSES = 0x0400 };

/* Stops at the echo wait at $7E:379B, with 8-bit A the command and X the
 * passes left before it sends again. */
void scene_send_sound_command_again(Snes *s)
{
    uint8_t command = send_sound_command(state_of(s), m_platform(s));
    m_set_a8(s, command);
    m_set_nz8(s, command);
    s->cpu.x = ECHO_PASSES;
    s->cpu.pc = 0x379B;
}

// $7E:377E
/* The scene's own send of the command in the buffer (the pause's sounds, the
 * race's). With the APU run directly, the send and its echo wait run as one. */
void scene_send_sound_command(Snes *s)
{
    if (au_driver_run_direct(m_platform(s), 0x7E377E, 0x7E37A4))
        return;
    m_set_flag(s, CPU_FLAG_M, true);
    scene_send_sound_command_again(s);
}

// $7E:379B
/* One pass of the echo wait: on to the return at $7E:37A5 when the driver
 * echoes the command, or after the passes run out to $7E:37A3, which sends it
 * again. Idle entry, 8-bit A and 16-bit X. */
void scene_wait_sound_command_echo_loop(Snes *s)
{
    uint8_t command = (uint8_t)s->cpu.a;
    uint8_t echoed = echo(m_platform(s));
    m_set_flag(s, CPU_FLAG_C, command >= echoed);
    m_set_nz8(s, (uint8_t)(command - echoed));
    if (command == echoed) {
        m_rts(s);
        return;
    }
    uint16_t passes = (uint16_t)(s->cpu.x - 1);
    s->cpu.x = passes;
    m_set_nz16(s, passes);
    if (passes == 0)
        s->cpu.pc = 0x3780;
}

/* ---- The sound put back to normal ------------------------------------------------------------ */

/* What a scene leaves of the sound driver's state, put back as the next scene
 * starts and as the program ends: the tempo and the pitch unbent at once, the
 * effects' pitch unbent, both pairs of effect voices stopped, and the music at
 * full volume at once. Each command goes through the program's own send,
 * which waits for the driver to take it; the last is the routine's own jump to
 * the send, which returns for it, where the port stops. A scene that sends no
 * songs leaves the sound alone. The send reads the buffer and none of the
 * registers. */
enum { SCENE_SEND = 0x7E377E, SEND_READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };
/* The last send returns to the routine's caller, which reads what the routine
 * was called with of A's high byte, Y and the overflow. */
enum { THE_LAST_SEND_RETURNS_A_HIGH_Y_AND_THE_OVERFLOW = DC_AL | DC_X | DC_N | DC_Z | DC_C };

static const struct {
    uint8_t command, p1, p2;
    uint32_t site;   /* the routine's call of the send; 0 for the jump to it */
} back_to_normal[] = {
    { AU_BEND_BOTH, 0, 0, 0x7E3187 },
    { AU_BEND_EFFECTS_PITCH, 0, 0, 0x7E318F },
    { AU_STOP_EFFECTS, 0, 0, 0x7E3197 },
    { AU_STOP_EFFECTS, 1, 0, 0x7E319F },
    { AU_FADE_MUSIC, 0, AU_FULL_VOLUME, 0 },
};
enum { BACK_TO_NORMAL = sizeof back_to_normal / sizeof back_to_normal[0] };

static void into_the_buffer(GameState *g, unsigned n)
{
    set_sound_command(g, back_to_normal[n].command);
    set_sound_command_parameters(g, 0, back_to_normal[n].p1);
    set_sound_command_parameters(g, 1, back_to_normal[n].p2);
}

// $7E:3177
void scene_sound_back_to_normal(Snes *s)
{
    GameState *g = state_of(s);
    if (scene_flags(g) & SCENE_FLAG_NO_SONGS) {
        m_rts(s);
        return;
    }
    m_set_flag(s, CPU_FLAG_M, true);
    for (unsigned n = 0; n + 1 < BACK_TO_NORMAL; n++) {
        into_the_buffer(g, n);
        au_driver_call(m_platform(s), PF_CALL(back_to_normal[n].site, SCENE_SEND), SEND_READS_NOTHING);
    }
    into_the_buffer(g, BACK_TO_NORMAL - 1);
    m_stop_at(s, SCENE_SEND, THE_LAST_SEND_RETURNS_A_HIGH_Y_AND_THE_OVERFLOW);
}

/* The routine, whose stores reach the sound command buffer through the data
 * bank. */
bool scene_sound_back_to_normal_present(Snes *s)
{
    return m_bank_reaches_io(s) && m_code_matches(s, 0x7E3177, 0x39, 0x39E5E647149652CAull);
}
