/* The C sound driver (game/sound/) playing in place of the original's on the
 * SPC700: ct_play's default (docs/PLAYING.md; docs/SOUND.md, "The port as
 * built").
 *
 * The game's CPU half of the driver hands its boot and its commands to the
 * host as calls (platform/audio.h, au_driver_boot_taken): the driver boots,
 * and each command is carried out as the CPU half took it, a song with its
 * samples placed in sound memory, so nothing is uploaded and nothing waits
 * on the ports. The SPC700 stays idle. The host runs the driver's ticks on
 * the APU's time, waits out the echo's buffer after a change of its delay as
 * the original does (holding the commands that come meanwhile), shows the
 * driver's status on the ports, and answers the commands the game writes to
 * the ports itself (the scene's). The driver plays through the voices
 * (platform/audio.h) as S-DSP register writes (platform/voices_dsp.h), into
 * the emulated S-DSP, which makes the sound as ever.
 *
 * A command takes no time, where the original's took frames over its uploads,
 * so the game's timing is not the original's: this can't record or replay a
 * movie. Saved states carry the host's and the driver's fields
 * (sound_host_state). */
#ifndef CT_FRONTEND_SOUND_HOST_H
#define CT_FRONTEND_SOUND_HOST_H

#include "snes/system.h"

typedef struct SoundHost SoundHost;

/* Play the C driver on the machine's APU from the game's next boot of the
 * driver on. */
SoundHost *sound_host_attach(Snes *s);
void sound_host_detach(SoundHost *h);

/* Bytes of samples placed where the S-DSP was writing its echo, below where
 * the samples' places end for the echo: the one thing the original never
 * lets happen to a song's samples (it stops the echo writing, and moves its
 * buffer, before they arrive), since the echo would overwrite them. (A sample
 * placed past that is the original's too, which forgets it once it is in.)
 * Nonzero is a fault; ct_dev and ct_sound_fuzz check it. */
unsigned long sound_host_samples_into_echo(const SoundHost *h);

/* The song the driver has saved to resume (command $14), for ct_sound_fuzz,
 * which mustn't make up a song in its place. */
uint8_t sound_host_saved_song(const SoundHost *h);

/* The host's and the driver's fields for a saved state
 * (snes_state_write_with, snes_state_read_with); NULL if out of memory. */
const SnesStateExtra *sound_host_state(SoundHost *h);

/* For tests: every field a state holds, as snes_state_fields lists the
 * machine's, and where the driver is in the host. */
void sound_host_driver_fields(SoundHost *h, SnesStateField fn, void *ctx);
size_t sound_host_driver_offset(void);

#endif
