/* Audio service (docs/PLATFORM.md): commands to the sound driver, the status
 * the game polls, and the host's samples. */
#ifndef CT_PLATFORM_AUDIO_H
#define CT_PLATFORM_AUDIO_H

#include "platform/platform.h"

/* A sound driver command ($C7:0004's interface) with up to three parameters.
 * `count` is how many of p1-p3 the caller sets; the driver sends the others as
 * the previous command left them (event $E8 sets two, the scene song three). */
typedef struct {
    uint8_t command, p1, p2, p3;
    uint8_t count;
} AudioCommand;

/* Send a command. SNES backend: the parameters and command into $1E01-$1E03 and
 * $1E00, then a call-out to $C7:0004 from o.site (o.target), so the driver's
 * echo waits and uploads run as before and frames can pass. Returns when the
 * original's JSL would. The WRAM writes are made in one order (parameters, then
 * the command) whatever order the original used: nothing runs between them. */
PF_API void au_command(Platform *pf, PfOrigin o, AudioCommand c);

static inline void au_play_song(Platform *pf, PfOrigin o, uint8_t song)
{
    au_command(pf, o, (AudioCommand){ .command = 0x10, .p1 = song, .count = 1 });
}

/* Driver status the game polls: the byte the driver shows on port 1-3. Named
 * by port until its meaning is known (event $ED: port 1 zero; $EE: port 3's low
 * nibble). */
PF_API uint8_t au_status(Platform *pf, PfOrigin o, uint8_t port);

/* Host side: up to max_frames stereo frames at 32000 Hz; returns how many. */
PF_API size_t au_take_samples(Platform *pf, int16_t *out, size_t max_frames);

#ifdef CT_PLATFORM_SNES
#include "platform/snes/audio.h"
#endif

#endif
