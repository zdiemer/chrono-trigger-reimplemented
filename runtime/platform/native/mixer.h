/* The native mixer: the voices (platform/audio.h, "The voices") made into
 * sound on the host, with no S-DSP under them (docs/PLATFORM.md, "The native
 * mixer, as built").
 *
 * Eight voices play BRR samples from the mixer's sound memory at their
 * pitches, through the four-point Gaussian interpolation, shaped by their
 * ADSR envelopes or gains, at their volumes on each side, one of them
 * modulating the pitch of the one above or played as noise; the voices that
 * send to the echo are delayed through a buffer in the same memory, filtered
 * by eight taps and fed back; and the main and echo mixes are heard at their
 * volumes, 32000 stereo frames a second.
 *
 * The arithmetic is the S-DSP's, to the bit, and so is the timing of what the
 * driver asks for: a key on or off takes effect at the next of every other
 * frame, a note starts five frames after it, a block of samples is decoded
 * four at a time when the voice reaches it, and the echo takes a change of
 * its place when its offset wraps. Each of these is kept because it is
 * audible, or because the game relies on it (the echo's buffer runs over
 * samples placed under it, and the driver rewrites its own sample while it
 * plays), and together they keep the mixer checkable frame for frame against
 * the S-DSP (frontend/sound_host.h, SOUND_MIXER_CHECK; dsp_test). There is no
 * register file and no register polling: each call sets what it names.
 *
 * The samples lie where the driver places them in `memory`, a sound memory of
 * the mixer's own laid out as the original driver lays out the SPC700's
 * (platform/voices.h), since where they lie decides what the echo overwrites.
 * The host advances the mixer a frame at a time on the sound CPU's clock,
 * which is when the driver's calls take effect, and takes the frames it made. */
#ifndef CT_PLATFORM_NATIVE_MIXER_H
#define CT_PLATFORM_NATIVE_MIXER_H

#include <stddef.h>
#include <stdint.h>

#include "platform/voices.h"

enum {
    MIXER_RATE = 32000,
    MIXER_OUT_FRAMES = 4096      /* frames kept until the host takes them */
};

typedef enum { MIXER_RELEASE, MIXER_ATTACK, MIXER_DECAY, MIXER_SUSTAIN } MixerPhase;

/* Every field of the voices and the mixer below is a fixed-size integer, so a
 * saved state can hold them as they are (mixer_fields). */
typedef struct {
    /* what the driver set */
    uint8_t sample;
    uint16_t pitch;               /* 4096ths of the sample's rate; 14 bits heard */
    int8_t volume[2];
    uint8_t adsr;                 /* 1: the ADSR envelope shapes the note; 0: the gain */
    uint8_t attack, decay, sustain_level, sustain_rate;
    uint8_t gain_mode, gain_value;   /* AuGainMode and its value */

    /* the note */
    uint8_t starting;             /* frames of the note's start left */
    uint8_t phase;                /* MixerPhase */
    int16_t level;                /* the envelope, 0-0x7FF */
    int16_t level_unclipped;      /* the last level computed, before it was clipped */
    uint16_t block;               /* the BRR block playing */
    uint8_t block_byte;           /* the next pair of the block's bytes to decode (1, 3, 5, 7) */
    uint8_t decoded_at;           /* where the next four decoded samples go in `decoded` */
    int16_t decoded[12];          /* the last twelve samples decoded, 15 bits each */
    uint16_t position;            /* how far into the samples ahead, in 4096ths of a sample */
    int16_t output;               /* what the voice put out last frame */
} MixerVoice;

typedef struct Mixer {
    AuVoices voices;              /* the handle the driver plays through */

    MixerVoice voice[AU_VOICES];
    int8_t master[2];
    int8_t echo_volume[2];
    int8_t echo_feedback;
    int8_t taps[8];
    uint8_t noise_clock;
    AuVoiceSet noise_voices, modulated_voices, echo_voices;
    uint8_t silenced;             /* the output stopped: nothing heard ... */
    uint8_t resetting;            /* ... and every voice released and held silent */
    uint8_t echo_writes;          /* the echo writing its buffer */
    uint8_t echo_delay;           /* in 16 ms units */
    uint8_t echo_page;            /* where its buffer starts */

    AuVoiceSet keys_asked;        /* key ons asked for since the last poll */
    AuVoiceSet keys_on;           /* ... taken at the last poll, started this frame */
    AuVoiceSet keys_released;     /* the voices the driver keeps released */
    AuVoiceSet keys_off;          /* ... as the last poll found them */
    uint8_t polling;              /* 1 on the frames that act on the keys */
    uint16_t rate_counter;        /* the clock every envelope and the noise step on */
    uint16_t noise;               /* the noise generator's 15 bits */

    uint8_t echo_page_now;        /* the buffer the echo is in, and */
    uint8_t echo_writes_now;      /* ... whether it writes, as it last looked */
    uint16_t echo_offset, echo_length;
    uint8_t echo_newest;          /* where in `echo_history` the newest frame is */
    int16_t echo_history[8][2];

    uint8_t memory[0x10000];      /* the samples, their directory and the echo's buffer */

    /* not part of a state: the frames made, and the host's watch on memory */
    uint32_t out_count;
    uint32_t out_dropped;
    int16_t out[MIXER_OUT_FRAMES * 2];
    void (*watch)(void *ctx, uint16_t at);   /* before each write to sound memory, or NULL */
    void *watch_ctx;
} Mixer;

/* Silent, as the S-DSP is at power-on: the output stopped, the echo not
 * writing, and memory clear. MIXER_RATE frames a second follow. */
void mixer_init(Mixer *m);

/* The handle the driver plays through (platform/audio.h), made good again
 * after the mixer was copied in from a saved state. */
AuVoices *mixer_voices(Mixer *m);

/* One frame of output, now, returned rather than kept. */
void mixer_frame(Mixer *m, int16_t frame[2]);
/* `frames` frames, kept for mixer_take (frames past MIXER_OUT_FRAMES are
 * dropped and counted). */
void mixer_run(Mixer *m, unsigned frames);
size_t mixer_take(Mixer *m, int16_t *out, size_t max_frames);

/* The echo's delay as last set, and whether the echo writes over a byte of
 * sound memory: in the buffer it is writing now or in the one it moves to. */
uint8_t mixer_echo_delay(const Mixer *m);
bool mixer_echo_writes_over(const Mixer *m, uint16_t at);

/* Every field of a saved state, by name ("voice[3].level"), with its size in
 * bytes and its count; the host's saved states call this. */
typedef void (*MixerField)(void *ctx, const char *name, void *field, unsigned size, uint32_t count);
void mixer_fields(Mixer *m, MixerField fn, void *ctx);

#endif
