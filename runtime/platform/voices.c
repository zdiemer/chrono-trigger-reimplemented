/* The voice calls (platform/audio.h), made by whichever implementation the
 * AuVoices is (platform/voices.h); and a pair of implementations played as one. */
#include "platform/voices.h"

void au_voices_start(AuVoices *v) { v->ops->start(v); }
void au_voice_sample(AuVoices *v, uint8_t voice, uint8_t sample) { v->ops->sample(v, voice, sample); }
void au_voice_envelope(AuVoices *v, uint8_t voice, AuEnvelope e) { v->ops->envelope(v, voice, e); }
void au_voice_gain(AuVoices *v, uint8_t voice, AuGain g) { v->ops->gain(v, voice, g); }
void au_voice_pitch(AuVoices *v, uint8_t voice, uint16_t pitch) { v->ops->pitch(v, voice, pitch); }
void au_voice_volume(AuVoices *v, uint8_t voice, int8_t left, int8_t right) { v->ops->volume(v, voice, left, right); }
void au_voices_key_on(AuVoices *v, AuVoiceSet voices) { v->ops->key_on(v, voices); }
void au_voices_key_off(AuVoices *v, AuVoiceSet voices) { v->ops->key_off(v, voices); }
void au_voices_hold(AuVoices *v, bool hold) { v->ops->hold(v, hold); }
int8_t au_voice_output(AuVoices *v, uint8_t voice) { return v->ops->output(v, voice); }
void au_master_volume(AuVoices *v, int8_t left, int8_t right) { v->ops->master_volume(v, left, right); }
void au_noise_clock(AuVoices *v, uint8_t clock) { v->ops->noise_clock(v, clock); }
void au_output_stopped(AuVoices *v, bool stopped) { v->ops->output_stopped(v, stopped); }
void au_noise_voices(AuVoices *v, AuVoiceSet voices) { v->ops->noise_voices(v, voices); }
void au_pitch_modulation(AuVoices *v, AuVoiceSet voices) { v->ops->pitch_modulation(v, voices); }
void au_echo_voices(AuVoices *v, AuVoiceSet voices) { v->ops->echo_voices(v, voices); }
void au_echo_volume(AuVoices *v, int8_t left, int8_t right) { v->ops->echo_volume(v, left, right); }
void au_echo_feedback(AuVoices *v, int8_t feedback) { v->ops->echo_feedback(v, feedback); }
void au_echo_filter(AuVoices *v, const int8_t taps[8]) { v->ops->echo_filter(v, taps); }
void au_echo_delay(AuVoices *v, uint8_t delay) { v->ops->echo_delay(v, delay); }
void au_echo_writes(AuVoices *v, bool on) { v->ops->echo_writes(v, on); }

void au_sample_define(AuVoices *v, uint8_t sample, const uint8_t *brr, uint16_t length, uint16_t loop)
{
    v->ops->sample_define(v, sample, brr, length, loop);
}

void au_sample_write(AuVoices *v, uint8_t sample, uint16_t at, uint8_t byte) { v->ops->sample_write(v, sample, at, byte); }
void au_sample_memory_write(AuVoices *v, uint16_t at, uint8_t byte) { v->ops->sample_memory_write(v, at, byte); }

void au_sample_memory_move(AuVoices *v, uint16_t to, uint16_t from, uint16_t length)
{
    v->ops->sample_memory_move(v, to, from, length);
}

void au_sample_place(AuVoices *v, uint8_t sample, uint16_t start, uint16_t loop)
{
    v->ops->sample_place(v, sample, start, loop);
}

void au_voices_snes_register(AuVoices *v, uint8_t reg, uint8_t value) { v->ops->snes_register(v, reg, value); }

/* ---- A pair ------------------------------------------------------------------------------ */

static AuVoices *first(AuVoices *v) { return ((AuVoicesPair *)v)->first; }
static AuVoices *second(AuVoices *v) { return ((AuVoicesPair *)v)->second; }

#define BOTH(call, ...) do { au_##call(first(v), __VA_ARGS__); au_##call(second(v), __VA_ARGS__); } while (0)

static void pair_start(AuVoices *v) { au_voices_start(first(v)); au_voices_start(second(v)); }
static void pair_sample(AuVoices *v, uint8_t voice, uint8_t sample) { BOTH(voice_sample, voice, sample); }
static void pair_envelope(AuVoices *v, uint8_t voice, AuEnvelope e) { BOTH(voice_envelope, voice, e); }
static void pair_gain(AuVoices *v, uint8_t voice, AuGain g) { BOTH(voice_gain, voice, g); }
static void pair_pitch(AuVoices *v, uint8_t voice, uint16_t pitch) { BOTH(voice_pitch, voice, pitch); }
static void pair_volume(AuVoices *v, uint8_t voice, int8_t l, int8_t r) { BOTH(voice_volume, voice, l, r); }
static void pair_key_on(AuVoices *v, AuVoiceSet voices) { BOTH(voices_key_on, voices); }
static void pair_key_off(AuVoices *v, AuVoiceSet voices) { BOTH(voices_key_off, voices); }
static void pair_hold(AuVoices *v, bool hold) { BOTH(voices_hold, hold); }

static int8_t pair_output(AuVoices *v, uint8_t voice) { return au_voice_output(first(v), voice); }

static void pair_master_volume(AuVoices *v, int8_t l, int8_t r) { BOTH(master_volume, l, r); }
static void pair_noise_clock(AuVoices *v, uint8_t clock) { BOTH(noise_clock, clock); }
static void pair_output_stopped(AuVoices *v, bool stopped) { BOTH(output_stopped, stopped); }
static void pair_noise_voices(AuVoices *v, AuVoiceSet voices) { BOTH(noise_voices, voices); }
static void pair_pitch_modulation(AuVoices *v, AuVoiceSet voices) { BOTH(pitch_modulation, voices); }
static void pair_echo_voices(AuVoices *v, AuVoiceSet voices) { BOTH(echo_voices, voices); }
static void pair_echo_volume(AuVoices *v, int8_t l, int8_t r) { BOTH(echo_volume, l, r); }
static void pair_echo_feedback(AuVoices *v, int8_t feedback) { BOTH(echo_feedback, feedback); }
static void pair_echo_filter(AuVoices *v, const int8_t taps[8]) { BOTH(echo_filter, taps); }
static void pair_echo_delay(AuVoices *v, uint8_t delay) { BOTH(echo_delay, delay); }
static void pair_echo_writes(AuVoices *v, bool on) { BOTH(echo_writes, on); }

static void pair_sample_define(AuVoices *v, uint8_t sample, const uint8_t *brr, uint16_t length, uint16_t loop)
{
    BOTH(sample_define, sample, brr, length, loop);
}

static void pair_sample_write(AuVoices *v, uint8_t sample, uint16_t at, uint8_t byte) { BOTH(sample_write, sample, at, byte); }
static void pair_sample_memory_write(AuVoices *v, uint16_t at, uint8_t byte) { BOTH(sample_memory_write, at, byte); }

static void pair_sample_memory_move(AuVoices *v, uint16_t to, uint16_t from, uint16_t length)
{
    BOTH(sample_memory_move, to, from, length);
}

static void pair_sample_place(AuVoices *v, uint8_t sample, uint16_t start, uint16_t loop)
{
    BOTH(sample_place, sample, start, loop);
}

static void pair_snes_register(AuVoices *v, uint8_t reg, uint8_t value) { BOTH(voices_snes_register, reg, value); }

static const AuVoicesOps pair_ops = {
    pair_start, pair_sample, pair_envelope, pair_gain, pair_pitch, pair_volume, pair_key_on, pair_key_off,
    pair_hold, pair_output, pair_master_volume, pair_noise_clock, pair_output_stopped, pair_noise_voices,
    pair_pitch_modulation, pair_echo_voices, pair_echo_volume, pair_echo_feedback, pair_echo_filter,
    pair_echo_delay, pair_echo_writes, pair_sample_define, pair_sample_write, pair_sample_memory_write,
    pair_sample_memory_move, pair_sample_place, pair_snes_register
};

void au_voices_pair(AuVoicesPair *p, AuVoices *a, AuVoices *b)
{
    p->voices.ops = &pair_ops;
    p->first = a;
    p->second = b;
}
