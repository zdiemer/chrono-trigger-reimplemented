/* The C sound driver (game/sound/) played for a build with no machine
 * (ct_native; docs/PLAYING.md, "The native program"): the player behind the
 * audio service's host backend (runtime/platform/native/host.h, AuHost), as
 * frontend/sound_host.h is the one beside the machine's APU.
 *
 * The game's CPU half hands it the boot and its commands as calls; the
 * game's sends through the service go to the CPU half as C
 * (sound_driver_send); the scenes' own commands come on the ports, and the
 * driver's status shows there. Its time is the driver's own: the driver
 * ticks on it as on the original's timer, a change of the echo's delay waits
 * out the buffer as the original's does, holding the commands that come
 * meanwhile, and the voices play through the native mixer. The driver moves
 * it on a frame at a time, and a wait on a port moves it on further. */
#ifndef CT_FRONTEND_SOUND_NATIVE_H
#define CT_FRONTEND_SOUND_NATIVE_H

#include <stddef.h>

#include "platform/native/host.h"
#include "state.h"

typedef struct SoundNative SoundNative;

/* The player for the game `g` on `pf`, its hooks put in pf->audio. */
SoundNative *sound_native_create(GameState *g, Platform *pf);
void sound_native_destroy(SoundNative *n);
/* The driver, its held commands and ports, the mixer and the samples not yet
 * taken, as bytes for a snapshot, and put back from them; a restore keeps
 * what binds the player to the run (its game, the platform, the voices). */
size_t sound_native_snapshot_bytes(void);
void sound_native_snapshot(const SoundNative *n, uint8_t *out);
void sound_native_restore(SoundNative *n, const uint8_t *in);

/* A displayed frame's worth of the driver's time. */
void sound_native_frame(SoundNative *n);

/* Buffered 32000 Hz stereo samples for the driver, oldest first. Taking fewer
 * than are ready leaves the rest for the next call. A driver that doesn't
 * play sound discards it after each frame. If a driver falls behind, the
 * buffer keeps the newest 8192 frames rather than growing without bound. */
size_t sound_native_take_audio(SoundNative *n, int16_t *out, size_t frames);
void sound_native_discard_audio(SoundNative *n);

#endif
