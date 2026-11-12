/* The voice calls (platform/audio.h), made by whichever implementation the
 * AuVoices is (platform/voices.h). */
#include "platform/voices.h"

#include "platform/audio.h"

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
void au_output_muted(AuVoices *v, bool muted) { v->ops->muted(v, muted); }
void au_voices_reset(AuVoices *v, bool reset) { v->ops->reset(v, reset); }

void au_output_stopped(AuVoices *v, bool stopped)
{
    au_voices_reset(v, stopped);
    au_output_muted(v, stopped);
}

void au_noise_voices(AuVoices *v, AuVoiceSet voices) { v->ops->noise_voices(v, voices); }
void au_pitch_modulation(AuVoices *v, AuVoiceSet voices) { v->ops->pitch_modulation(v, voices); }
void au_echo_voices(AuVoices *v, AuVoiceSet voices) { v->ops->echo_voices(v, voices); }
void au_echo_volume(AuVoices *v, int8_t left, int8_t right) { v->ops->echo_volume(v, left, right); }
void au_echo_feedback(AuVoices *v, int8_t feedback) { v->ops->echo_feedback(v, feedback); }
void au_echo_filter(AuVoices *v, const int8_t taps[8]) { v->ops->echo_filter(v, taps); }
void au_echo_delay(AuVoices *v, uint8_t delay) { v->ops->echo_delay(v, delay); }
void au_echo_buffer(AuVoices *v, uint8_t page, uint8_t delay) { v->ops->echo_buffer(v, page, delay); }
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

AuVoiceSettings au_voice_settings(AuVoices *v, uint8_t voice) { return v->ops->voice_settings(v, voice); }
AuMixSettings au_mix_settings(AuVoices *v) { return v->ops->mix_settings(v); }
