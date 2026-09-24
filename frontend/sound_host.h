/* The C sound driver (game/sound/) playing in place of the original's on the
 * SPC700: ct_play --sound-driver c (docs/PLAYING.md; docs/SOUND.md, "The port
 * as built").
 *
 * The CPU half is unchanged: it uploads the original driver through the boot
 * ROM, then talks to it through the four ports. Once the boot upload starts a
 * program, the SPC700 stays idle and this answers the ports in its place, as
 * the original's main loop and upload receivers would: it echoes commands and
 * carries them out, takes uploads (their data lands in sound memory, where the
 * CPU half puts its samples and their directory; the driver itself loads what
 * it reads from the cartridge through its accessors), runs the driver's ticks
 * on its timer, and shows the driver's status. The driver plays through the
 * voices (platform/audio.h) as S-DSP register writes (platform/voices_dsp.h),
 * into the emulated S-DSP, which makes the sound as ever.
 *
 * The answers come at once rather than after the original's instructions, so
 * the CPU half waits less and the game's timing is not the original's: this
 * can't record or replay a movie. The driver's state is not in a saved state
 * either, so the player doesn't save or load one in this mode. */
#ifndef CT_FRONTEND_SOUND_HOST_H
#define CT_FRONTEND_SOUND_HOST_H

#include "snes/system.h"

typedef struct SoundHost SoundHost;

/* Play the C driver on the machine's APU from its next boot on. */
SoundHost *sound_host_attach(Snes *s);
void sound_host_detach(SoundHost *h);

#endif
