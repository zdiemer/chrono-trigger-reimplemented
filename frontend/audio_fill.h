/* Filling an audio device from the player (docs/PLAYING.md): the APU produces
 * 32000 Hz as the emulation runs, paced by video, and a device plays at its
 * own rate from its own clock. Samples are resampled into a ring buffer the
 * device drains, and the resampling ratio is nudged (at most 0.5%) to keep the
 * buffer near its target fill instead of dropping or repeating anything.
 *
 * Host-neutral: the driver owns the device. It calls audio_fill_frame once a
 * frame and starts or stops the device as it says, and audio_fill_drain from
 * the device's callback. The ring is shared with that callback, so the driver
 * supplies `lock`, which audio_fill_frame holds around each touch of it (the
 * callback itself runs with the lock held). */
#ifndef CT_FRONTEND_AUDIO_FILL_H
#define CT_FRONTEND_AUDIO_FILL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "apu/dsp.h"
#include "player.h"

#define AUDIO_RING_FRAMES 16384
#define AUDIO_HISTORY 4096   /* input frames kept for the resampler (> one video frame) */

typedef struct AudioFill {
    int rate;        /* the device's sample rate */
    size_t target;   /* ring fill aimed for, in frames */
    bool playing;    /* whether the device should be playing */

    /* Ring buffer shared with the device callback (under `lock`). */
    int16_t ring[AUDIO_RING_FRAMES * 2];
    size_t head, count;

    /* Resampler input: frames from the APU not yet passed, and the read position. */
    int16_t in[(AUDIO_HISTORY + DSP_OUT_FRAMES) * 2];
    size_t in_count;
    double pos;
    double fill_avg;

    void (*lock)(void *ctx, bool locked);
    void *lock_ctx;
} AudioFill;

/* For a device playing stereo int16 at `rate` Hz that asks for `buffer_frames`
 * frames at a time. */
void audio_fill_init(AudioFill *a, int rate, int buffer_frames, void (*lock)(void *ctx, bool locked), void *lock_ctx);

/* Once a frame: move the samples the last frame produced toward the device.
 * While `paused` the samples are dropped and the ring emptied. Returns whether
 * the device should now be playing: false while paused, true once the ring
 * first holds its target after that. */
bool audio_fill_frame(AudioFill *a, Player *pl, bool paused);

/* The device callback: `frames` stereo frames into out, silence where the ring
 * runs dry. The caller holds the lock. */
void audio_fill_drain(AudioFill *a, int16_t *out, size_t frames);

#endif
