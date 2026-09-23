/* Scenes: the scene program's messages to the sound driver.
 *
 * A scene tells the sound driver what to do the way the rest of the game does
 * -- a command and its parameters in the sound command buffer, sent through the
 * driver's entry (the frame's song, a script's command: scene/frame.c,
 * scene/script.c) -- and, for its own sounds, it sends the buffer to the
 * driver itself and waits for the driver to echo the command. It also waits on
 * the song: the driver shows a marker the song sets (docs/SOUND.md, "What the
 * game reads back"), which the program waits for to keep a scene in time with
 * its music. The race's engine is its own message (scene/race.c). */
#include "scene/scene.h"

#include "platform/audio.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* The driver's status on port 3: its low nibble is the song's marker. */
enum { MARKER_PORT = 3, MARKER = 0x0F };

/* The guard also checks that absolute addressing reaches the APU ports, which
 * the audio service addresses in bank $00. */
bool scene_wait_for_song_marker_present(Snes *s)
{
    return m_bank_reaches_io(s) && m_code_matches(s, 0x7E5AAA, 7, 0x441A7AC228BA6C77ull);
}

// $7E:5AAA
/* One pass of the wait for the song to set its marker, then return. Idle
 * entry, 8-bit A holding the marker. */
void scene_wait_for_song_marker_loop(Snes *s)
{
    uint8_t marker = au_status(m_platform(s), PF_AT(0x7E5AAA), MARKER_PORT) & MARKER;
    m_set_a8(s, marker);
    m_set_nz8(s, marker);
    if (marker != 0)
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
    static const uint32_t sites[4] = { 0x7E3786, 0x7E378C, 0x7E3792, 0x7E3798 };
    for (uint8_t port = 3; port > 0; port--)
        au_driver_write(pf, PF_AT(sites[3 - port]), port, sound_command_parameters(g, port - 1u));
    uint8_t command = sound_command(g);
    au_driver_write(pf, PF_AT(sites[3]), 0, command);
    return command;
}

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
    uint8_t echo = au_driver_read(m_platform(s), PF_AT(0x7E379B), 0);
    m_set_flag(s, CPU_FLAG_C, command >= echo);
    m_set_nz8(s, (uint8_t)(command - echo));
    if (command == echo) {
        s->cpu.pc = 0x37A5;
        return;
    }
    uint16_t passes = (uint16_t)(s->cpu.x - 1);
    s->cpu.x = passes;
    m_set_nz16(s, passes);
    if (passes == 0)
        s->cpu.pc = 0x37A3;
}
