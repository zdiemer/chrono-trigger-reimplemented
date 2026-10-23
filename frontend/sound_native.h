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

#include "platform/native/host.h"
#include "state.h"

typedef struct SoundNative SoundNative;

/* The player for the game `g` on `pf`, its hooks put in pf->audio. */
SoundNative *sound_native_create(GameState *g, Platform *pf);
void sound_native_destroy(SoundNative *n);

/* A displayed frame's worth of the driver's time. */
void sound_native_frame(SoundNative *n);

#endif
