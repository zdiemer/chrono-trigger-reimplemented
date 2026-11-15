/* The GBA's mixer: the sound driver's voices (platform/voices.h) played on
 * the GBA, in the native mixer's place (frontend/sound_native.c takes it
 * through SOUND_NATIVE_MIXER, and SOUND_NATIVE_MIXER_PLAYS hands it the
 * driver's time a pass at a time). docs/PLATFORM.md, "The GBA build, as
 * built", "The sound", says what it plays, what it approximates and what it
 * costs.
 *
 * The driver plays as it does natively, and what it reads back of the voices
 * and the mix (their settings) is what it set, masked as the native mixer
 * masks it. The samples it writes into sound memory are known by their
 * bytes: the packing step (targets/gba/host/samples.py) lists each block the
 * driver can write with a hash of it, and the samples in it already
 * converted to 8-bit PCM, which the voices play from the cartridge. Only
 * the driver's own sample, which it rewrites as it plays, is decoded here.
 *
 * Eight voices are mixed 32 output samples at a time, at the GBA's own rate
 * (services/direct_sound.h), each voice at its pitch and envelope, scaled by
 * its volume on each side and the master volume. */
#ifndef CT_GBA_MIXER_H
#define CT_GBA_MIXER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "platform/voices.h"

/* The packed samples (samples.py's table): its head, then the blocks
 * shortest first, then the samples, then the PCM and the noise. */
typedef struct {
    uint16_t blocks, samples;
    uint32_t noise_at;             /* the noise generator's sequence, MIXER_NOISE_LENGTH samples */
} MixerSamples;
typedef struct {
    uint32_t length, hash;         /* the block's bytes as the driver writes them */
    uint16_t first, count;         /* its samples */
} MixerSampleBlock;
typedef struct {
    uint16_t start, unused;        /* where in its block it starts */
    uint32_t loop;                 /* the PCM sample it loops from, or MIXER_NO_LOOP */
    uint32_t pcm_at, length;       /* its PCM, from the table's start */
    uint16_t peak, unused2;        /* its loudest sample, 15 bits, which the PCM's 127 stands for */
} MixerSample;
enum { MIXER_NO_LOOP = 0xFFFFFFFFu, MIXER_NOISE_LENGTH = 0x7FFF };

enum {
    MIXER_DIRECTORY = 48,          /* the samples a voice can name: the boot's, the driver's own ($10), a song's ($20-$2F) */
    MIXER_REGIONS = 20,            /* the blocks sound memory holds at once: a song's 16 and the boot's */
    MIXER_OWN_SAMPLES = 80,        /* the driver's own sample, decoded: five blocks */
    MIXER_KEY_EVENTS = 8           /* key ons and offs kept in order between two mixes */
};

typedef enum { MIXER_RELEASE, MIXER_ATTACK, MIXER_DECAY, MIXER_SUSTAIN } MixerPhase;

/* A sample as a voice plays it: its PCM, its length and where it loops from
 * (MIXER_NO_LOOP: it ends), in samples; NULL: silence. */
typedef struct {
    const int8_t *pcm;
    uint32_t length, loop;
} MixerWave;

/* What a voice plays, and where its note is. */
typedef struct {
    uint8_t sample;
    uint8_t gain_mode, gain_value;
    uint8_t phase;                 /* MixerPhase */
    int16_t level;                 /* the envelope, 0-0x7FF */
    uint16_t until;                /* frames of the S-DSP's to the envelope's next step */
    MixerWave wave;
    uint16_t peak;                 /* the wave's loudest, 15 bits */
    uint32_t position;             /* in 4096ths of a sample */
} MixerVoice;

/* A block the driver wrote into sound memory, known. */
typedef struct {
    uint16_t at, length;
    uint8_t block;
} MixerRegion;

typedef struct Mixer {
    AuVoices voices;              /* first, as the voices' calls expect */
    AuVoiceSettings voice[AU_VOICES];
    AuMixSettings mix;
    /* who learns of a write to sound memory (the native mixer's snapshots' watch) */
    void (*watch)(void *ctx, uint16_t at);
    void *watch_ctx;

    MixerVoice play[AU_VOICES];
    AuVoiceSet noise_voices;
    uint8_t noise_clock;
    bool silenced, resetting;
    struct { uint8_t on; AuVoiceSet voices; } keys[MIXER_KEY_EVENTS];
    uint8_t key_events;

    struct { uint16_t start, loop; } directory[MIXER_DIRECTORY];
    MixerRegion region[MIXER_REGIONS];
    uint8_t regions;

    /* the bytes being written, not yet known as a block */
    uint32_t run_next;            /* the address that continues them */
    uint16_t run_at, run_cursor;
    uint32_t run_length, run_hash;

    uint8_t own_brr[VOICES_OWN_SAMPLE_BYTES];
    uint16_t own_loop;
    bool own_changed;
    int8_t own_pcm[MIXER_OWN_SAMPLES];
    uint16_t own_peak;

    uint32_t owed;                /* at the chunk's start, toward its first output sample: 448 a frame of the S-DSP's, 1,065 a sample */
    uint16_t due;                 /* the S-DSP's frames until the chunk's last sample is owed */
    uint16_t frames;              /* the S-DSP's frames since the last mix */
} Mixer;

void mixer_init(Mixer *m);
AuVoices *mixer_voices(Mixer *m);
/* The packed samples the voices play (main.c hands them over). */
void mixer_use_samples(const MixerSamples *samples);
/* `frames` of the S-DSP's 32,000 a second passed: the sound they make is mixed
 * and queued for the GBA to play. */
void mixer_pass(Mixer *m, uint32_t frames);
uint8_t mixer_echo_delay(const Mixer *m);
/* Cycles spent mixing since the last call (main.c's report). */
uint32_t mixer_take_cycles(void);

#endif
