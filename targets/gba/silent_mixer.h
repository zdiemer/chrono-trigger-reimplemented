/* The sound's voices on the GBA until it plays them (issue #368): the native
 * mixer's interface (runtime/platform/native/mixer.h), which
 * frontend/sound_native.c drives, keeping what the driver sets and reads back
 * of the voices and the mix, and making no sound (targets/gba/main.c).
 *
 * What it can't answer is a voice's output, which the native mixer computes
 * as it plays: it answers 0, so the sound driver's meter (its status mode
 * that shows the voices' levels, command $FF) reads silence here. Nothing
 * else the driver does depends on the mix. Its samples aren't kept: the
 * native mixer's 82 KiB is the GBA's work RAM it hasn't got (docs/PLATFORM.md,
 * "The GBA build, as built"). */
#ifndef CT_GBA_SILENT_MIXER_H
#define CT_GBA_SILENT_MIXER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "platform/voices.h"

typedef struct Mixer {
    AuVoices voices;              /* first, as the voices' calls expect */
    AuVoiceSettings voice[AU_VOICES];
    AuMixSettings mix;
    /* who learns of a write to sound memory (the native mixer's snapshots' watch) */
    void (*watch)(void *ctx, uint16_t at);
    void *watch_ctx;
} Mixer;

void mixer_init(Mixer *m);
AuVoices *mixer_voices(Mixer *m);
/* A frame of sound: silence. */
void mixer_frame(Mixer *m, int16_t frame[2]);
uint8_t mixer_echo_delay(const Mixer *m);

#endif
