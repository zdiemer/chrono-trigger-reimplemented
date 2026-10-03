/* The voices' types (platform/audio.h, "The voices"), apart from the rest of
 * the audio service, for what implements the voices and needs nothing else
 * of it: platform/voices.h and the native mixer. */
#ifndef CT_PLATFORM_VOICE_TYPES_H
#define CT_PLATFORM_VOICE_TYPES_H

#include <stdbool.h>
#include <stdint.h>

typedef struct AuVoices AuVoices;

enum { AU_VOICES = 8 };
typedef uint8_t AuVoiceSet;   /* a bit per voice, voice 0 the lowest */

/* An ADSR envelope: attack rate 0-15, decay rate 0-7, the level the decay
 * stops at 0-7 (in eighths, less one), and the rate it falls at from there
 * 0-31. `adsr` false plays the voice at its gain instead. */
typedef struct {
    bool adsr;
    uint8_t attack, decay, sustain_level, sustain_rate;
} AuEnvelope;

/* A gain, what a voice plays at when its ADSR is off: a level held
 * (AU_GAIN_DIRECT, 0-127), or a rate 0-31 it moves at (0 holds it still). */
typedef enum {
    AU_GAIN_DIRECT,
    AU_GAIN_DECREASE_LINEAR,
    AU_GAIN_DECREASE_EXPONENTIAL,
    AU_GAIN_INCREASE_LINEAR,
    AU_GAIN_INCREASE_BENT
} AuGainMode;
typedef struct {
    AuGainMode mode;
    uint8_t value;
} AuGain;

#endif
