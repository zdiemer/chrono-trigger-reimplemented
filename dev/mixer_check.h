/* The native mixer checked against the S-DSP (frontend/sound_host.h,
 * SOUND_MIXER_CHECK), for ct_dev, ct_drive and ct_sound_fuzz: the host hands
 * each frame the S-DSP makes over with the mixer's beside it, and this counts
 * how many differ, by how much, and where in the spectrum the difference's
 * power lies. It works in floating point, which is why it is here and not in
 * the host the player ships with, which stays integer-only. */
#ifndef CT_DEV_MIXER_CHECK_H
#define CT_DEV_MIXER_CHECK_H

#include <stdbool.h>
#include <stdio.h>

#include "sound_host.h"

typedef struct MixerCheck MixerCheck;

/* A comparison of the host's frames from now on; NULL if out of memory. */
MixerCheck *mixer_check_attach(SoundHost *h);
void mixer_check_free(MixerCheck *c);

/* The frames compared so far, on one line to `out` ("mixer check: ..."); true
 * if none differ. */
bool mixer_check_report(const MixerCheck *c, FILE *out);

#endif
