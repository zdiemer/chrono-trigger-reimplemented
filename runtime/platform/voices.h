/* The voices' implementations (host side of platform/audio.h, "The voices").
 *
 * An AuVoices is a table of the calls' implementations, and each
 * implementation's own struct starts with one: platform/voices_dsp.h makes
 * each call as the S-DSP register writes that make it, platform/native/mixer.h
 * makes the sound itself. The au_voice* calls dispatch through the table
 * (voices.c), so the driver plays through whichever it was handed.
 *
 * A pair plays every call through two implementations at once, answering what
 * the driver asks (au_voice_output) from the first: ct_dev and ct_sound_fuzz
 * use it to hear the mixer beside the S-DSP (frontend/sound_host.h,
 * SOUND_MIXER_CHECK). Game code sees none of this. */
#ifndef CT_PLATFORM_VOICES_H
#define CT_PLATFORM_VOICES_H

#include "platform/audio.h"

typedef struct AuVoicesOps {
    void (*start)(AuVoices *v);
    void (*sample)(AuVoices *v, uint8_t voice, uint8_t sample);
    void (*envelope)(AuVoices *v, uint8_t voice, AuEnvelope e);
    void (*gain)(AuVoices *v, uint8_t voice, AuGain g);
    void (*pitch)(AuVoices *v, uint8_t voice, uint16_t pitch);
    void (*volume)(AuVoices *v, uint8_t voice, int8_t left, int8_t right);
    void (*key_on)(AuVoices *v, AuVoiceSet voices);
    void (*key_off)(AuVoices *v, AuVoiceSet voices);
    void (*hold)(AuVoices *v, bool hold);
    int8_t (*output)(AuVoices *v, uint8_t voice);
    void (*master_volume)(AuVoices *v, int8_t left, int8_t right);
    void (*noise_clock)(AuVoices *v, uint8_t clock);
    void (*output_stopped)(AuVoices *v, bool stopped);
    void (*noise_voices)(AuVoices *v, AuVoiceSet voices);
    void (*pitch_modulation)(AuVoices *v, AuVoiceSet voices);
    void (*echo_voices)(AuVoices *v, AuVoiceSet voices);
    void (*echo_volume)(AuVoices *v, int8_t left, int8_t right);
    void (*echo_feedback)(AuVoices *v, int8_t feedback);
    void (*echo_filter)(AuVoices *v, const int8_t taps[8]);
    void (*echo_delay)(AuVoices *v, uint8_t delay);
    void (*echo_writes)(AuVoices *v, bool on);
    void (*sample_define)(AuVoices *v, uint8_t sample, const uint8_t *brr, uint16_t length, uint16_t loop);
    void (*sample_write)(AuVoices *v, uint8_t sample, uint16_t at, uint8_t byte);
    void (*sample_memory_write)(AuVoices *v, uint16_t at, uint8_t byte);
    void (*sample_memory_move)(AuVoices *v, uint16_t to, uint16_t from, uint16_t length);
    void (*sample_place)(AuVoices *v, uint8_t sample, uint16_t start, uint16_t loop);
    void (*snes_register)(AuVoices *v, uint8_t reg, uint8_t value);
} AuVoicesOps;

struct AuVoices {
    const AuVoicesOps *ops;
};

/* Where both implementations keep things in sound memory, which is where the
 * original driver keeps them: the sample directory, the echo buffer, which
 * ends where the driver's tables begin, and the driver's own sample, below
 * the directory. The game's samples are placed where the original's CPU half
 * uploaded them, so what the echo's buffer runs over is the original's. */
enum {
    VOICES_DIRECTORY = 0x1E00,      /* each sample's start and loop, four bytes a sample */
    VOICES_ECHO_END_PAGE = 0xF1,
    VOICES_ECHO_UNIT_PAGES = 8,     /* a unit of delay, 16 ms, is 2 KiB of buffer */
    VOICES_OWN_SAMPLE = 0x1DC2,
    VOICES_OWN_SAMPLE_BYTES = 45
};

/* The page the echo's buffer starts at for a delay. */
static inline uint8_t voices_echo_page(uint8_t delay)
{
    return (uint8_t)(VOICES_ECHO_END_PAGE - VOICES_ECHO_UNIT_PAGES * delay);
}

typedef struct {
    AuVoices voices;
    AuVoices *first, *second;
} AuVoicesPair;

void au_voices_pair(AuVoicesPair *p, AuVoices *first, AuVoices *second);

#endif
