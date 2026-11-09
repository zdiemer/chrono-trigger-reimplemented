/* Audio service, host backend (runtime/platform/native/host.h): the game's
 * side of the sound driver, answered by the player the driver supplies
 * (Platform.audio), which plays the C sound driver in the SPC700's place.
 *
 * The game hands the player the boot and its commands as calls
 * (core/sound_commands.h); nothing is uploaded. What the
 * scenes write to the ports themselves, and the status they read there, go to
 * the player's ports, and a wait on a port passes the player's time until it
 * answers. With no player, nothing plays and every port reads 0. */
#include <stdio.h>
#include <stdlib.h>

#include "platform/native/host.h"
#include "platform/time_model.h"

static bool playing(const Platform *pf) { return pf->audio.ctx != NULL; }

void au_command(Platform *pf, PfOrigin o, AudioCommand c)
{
    (void)o;
    if (playing(pf))
        pf->audio.send(pf->audio.ctx, c);
}

static uint8_t port(Platform *pf, uint8_t which) { return playing(pf) ? pf->audio.read(pf->audio.ctx, which & 3) : 0; }

uint8_t au_status(Platform *pf, PfOrigin o, uint8_t which)
{
    (void)o;
    return port(pf, which);
}

uint8_t au_wait_music_start(Platform *pf, PfOrigin o)
{
    uint8_t tracks;
    while (!(tracks = au_music_playing(pf, PF_AT(o.site))))
        tm_wait_frame(pf, PF_AT(o.site));
    return tracks;
}

size_t au_take_samples(Platform *pf, int16_t *out, size_t max_frames)
{
    (void)pf, (void)out, (void)max_frames;
    return 0;
}

/* ---- The driver's side: a host plays it --------------------------------------------------------- */

bool au_driver_hosted(Platform *pf) { return playing(pf); }

/* The time the original's uploads took belongs to the caller: a handler's
 * share of its frame, or the main thread's work to sit out. A
 * song's start comes after what the main thread owes so far, as the
 * original's CPU half reached the ports after that work: the player counts
 * what is left of the echo's wait from there (SNES backend: native_sync). */
bool au_driver_boot_taken(Platform *pf)
{
    if (!playing(pf))
        return false;
    tm_host_charge(pf, pf->audio.boot(pf->audio.ctx));
    return true;
}

bool au_driver_command_taken(Platform *pf, AudioCommand c, uint8_t effects_asked)
{
    if (!playing(pf))
        return false;
    if (time_sound_song_start(c.command))
        tm_host_sit_out_owed(pf);
    tm_host_charge(pf, pf->audio.command(pf->audio.ctx, c, effects_asked));
    return true;
}

/* ---- The ports, and the waits on them ----------------------------------------------------------- */

void au_driver_write(Platform *pf, PfOrigin o, uint8_t which, uint8_t v)
{
    (void)o;
    if (playing(pf))
        pf->audio.write(pf->audio.ctx, which & 3, v);
}

uint8_t au_driver_read(Platform *pf, PfOrigin o, uint8_t which)
{
    (void)o;
    return port(pf, which);
}

/* How long a wait gives the player to answer, in its time (the SPC700's
 * cycles): a step a little over a tick's, and a second in all, which no
 * answer the game waits for takes. A wait the player never answers ends the
 * program, saying where. */
enum { WAIT_STEP_CYCLES = 0x2000, WAIT_LIMIT_CYCLES = 1024000 };

static bool echoed(Platform *pf, uint8_t value)
{
    for (uint32_t waited = 0; port(pf, 0) != value; waited += WAIT_STEP_CYCLES) {
        if (!playing(pf) || waited >= WAIT_LIMIT_CYCLES)
            return false;
        pf->audio.pass(pf->audio.ctx, WAIT_STEP_CYCLES);
    }
    return true;
}

void au_driver_wait_echo(Platform *pf, PfOrigin o, uint8_t value)
{
    if (echoed(pf, value))
        return;
    fprintf(stderr, "ct_native: the sound driver never answered the wait at $%02X:%04X\n",
            (unsigned)(o.site >> 16), (unsigned)(o.site & 0xFFFF));
    exit(1);
}

/* Scenes send directly to the C driver's host ports and retry when the
 * bounded wait ends. No cartridge CPU-half code runs on this path. */
bool au_driver_wait_echo_or_give_up(Platform *pf, PfOrigin o, uint32_t given_up, uint8_t value, uint16_t tries)
{
    (void)o, (void)given_up;
    for (unsigned poll = 0; poll < tries; poll++) {
        if (port(pf, 0) == value)
            return true;
        if (!playing(pf))
            return false;
        pf->audio.pass(pf->audio.ctx, WAIT_STEP_CYCLES);
    }
    return port(pf, 0) == value;
}

/* A call into the original's routine that sends over the ports, which only
 * the machine has. */
void au_driver_call(Platform *pf, PfOrigin o, uint16_t dont_care)
{
    (void)pf, (void)dont_care;
    fprintf(stderr, "ct_native: the call at $%02X:%04X into the original's send to the sound driver needs the machine\n",
            (unsigned)(o.site >> 16), (unsigned)(o.site & 0xFFFF));
    exit(1);
}
