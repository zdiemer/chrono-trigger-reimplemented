/* ct_dev --sound-lockstep-audio: what the C sound driver sounds like, sample
 * for sample, beside the original (docs/SOUND.md, "Validating the port").
 *
 * The lockstep (dev/sound_lockstep.h) compares the drivers' registers at the
 * tick boundary, which says nothing of when inside a tick a write lands or of
 * what the S-DSP makes of it. Here a second S-DSP plays the C driver's writes
 * over the same sound memory as the original's (its own echo buffer apart),
 * started from the original's S-DSP as the driver boots, and each of its
 * samples is compared with the original's: the output and every voice's
 * envelope and output level.
 *
 * The writes land in one of two ways:
 *
 * - EXACT: at the sample where the original made the same write in the same
 *   tick (or, if it made none, where the lockstep hands the C driver its
 *   input), so that drivers that write the same land the same;
 * - HOST: everything a tick writes at the sample the tick starts, as
 *   frontend/sound_host.c runs the driver in the SPC700's place (a tick at
 *   once, and a command as it comes).
 *
 * The second S-DSP runs a tick behind the original, since a tick's writes are
 * known only once it has run. */
#ifndef CT_DEV_SOUND_AUDIO_H
#define CT_DEV_SOUND_AUDIO_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "apu/apu.h"

typedef enum { SOUND_AUDIO_EXACT, SOUND_AUDIO_HOST } SoundAudioTiming;

typedef struct SoundAudio SoundAudio;

SoundAudio *sound_audio_create(SoundAudioTiming timing);
void sound_audio_destroy(SoundAudio *a);

/* The driver boots: the second S-DSP starts as the original's is (the first
 * time only; a reload keeps what it has). */
void sound_audio_boot(SoundAudio *a, const Apu *apu);
/* Before each of the original's instructions: takes the sample it made, if it
 * made one (from the S-DSP's output buffer, which it empties), and keeps the
 * second S-DSP within reach of it. */
void sound_audio_watch(SoundAudio *a, Apu *apu);
/* A tick starts: the second S-DSP catches up to the last tick's start. */
void sound_audio_tick(SoundAudio *a, const Apu *apu);
/* Whether the lockstep is inside a tick (HOST batches what it writes). */
void sound_audio_in_tick(SoundAudio *a, bool in_tick);
/* One of the original's register writes, now. */
void sound_audio_original_write(SoundAudio *a, const Apu *apu, uint8_t reg, uint8_t value);
/* One of the C driver's register writes, now. */
void sound_audio_write(SoundAudio *a, const Apu *apu, uint8_t reg, uint8_t value);

/* Writes the second S-DSP's output as a WAV (from the first sample
 * compared), and the original's alongside, for listening to. */
bool sound_audio_wav(SoundAudio *a, const char *c_path, const char *original_path);

/* The summary: samples compared, how many differ and where first, by voice. */
void sound_audio_report(SoundAudio *a, FILE *out);

#endif
