/* The voices as S-DSP registers (platform/voices_dsp.h). */
#include "platform/voices_dsp.h"

#include "apu/dsp.h"

static uint8_t voice_reg(uint8_t voice, uint8_t reg) { return (uint8_t)((voice & 7) << 4 | reg); }

static void put(AuVoices *v, uint8_t reg, uint8_t value) { v->write(v->ctx, reg, value); }

void au_voices_start(AuVoices *v) { put(v, DSP_DIR, VOICES_DSP_DIRECTORY >> 8); }

void au_voice_sample(AuVoices *v, uint8_t voice, uint8_t sample) { put(v, voice_reg(voice, DSP_SRCN), sample); }

void au_voice_envelope(AuVoices *v, uint8_t voice, AuEnvelope e)
{
    put(v, voice_reg(voice, DSP_ADSR0), (uint8_t)((e.adsr ? 0x80 : 0) | (e.decay & 7) << 4 | (e.attack & 0x0F)));
    put(v, voice_reg(voice, DSP_ADSR1), (uint8_t)((e.sustain_level & 7) << 5 | (e.sustain_rate & 0x1F)));
}

void au_voice_gain(AuVoices *v, uint8_t voice, AuGain g)
{
    uint8_t value = g.mode == AU_GAIN_DIRECT ? (uint8_t)(g.value & 0x7F)
                                             : (uint8_t)(0x80 | (g.mode - AU_GAIN_DECREASE_LINEAR) << 5 | (g.value & 0x1F));
    put(v, voice_reg(voice, DSP_GAIN), value);
}

void au_voice_pitch(AuVoices *v, uint8_t voice, uint16_t pitch)
{
    put(v, voice_reg(voice, DSP_PITCHL), (uint8_t)pitch);
    put(v, voice_reg(voice, DSP_PITCHH), (uint8_t)(pitch >> 8));
}

void au_voice_volume(AuVoices *v, uint8_t voice, int8_t left, int8_t right)
{
    put(v, voice_reg(voice, DSP_VOLL), (uint8_t)left);
    put(v, voice_reg(voice, DSP_VOLR), (uint8_t)right);
}

void au_voices_key_on(AuVoices *v, AuVoiceSet voices) { put(v, DSP_KON, voices); }
void au_voices_key_off(AuVoices *v, AuVoiceSet voices) { put(v, DSP_KOFF, voices); }

enum { ADSR_ON = 0x80, ECHO_WRITES_OFF = 0x20 };

void au_voices_hold(AuVoices *v, bool hold)
{
    for (uint8_t voice = 0; voice < AU_VOICES; voice++) {
        uint8_t reg = voice_reg(voice, DSP_ADSR0);
        uint8_t adsr = v->read(v->ctx, reg);
        put(v, reg, hold ? (uint8_t)(adsr & ~ADSR_ON) : (uint8_t)(adsr | ADSR_ON));
    }
}

int8_t au_voice_output(AuVoices *v, uint8_t voice) { return (int8_t)v->read(v->ctx, voice_reg(voice, DSP_OUTX)); }

void au_master_volume(AuVoices *v, int8_t left, int8_t right)
{
    put(v, DSP_MVOLL, (uint8_t)left);
    put(v, DSP_MVOLR, (uint8_t)right);
}

void au_mixer(AuVoices *v, AuMixer m)
{
    put(v, DSP_FLG, (uint8_t)((m.reset ? 0x80 : 0) | (m.muted ? 0x40 : 0) | (m.echo_writes_off ? ECHO_WRITES_OFF : 0) |
                              (m.noise_clock & 0x1F)));
}

void au_noise_voices(AuVoices *v, AuVoiceSet voices) { put(v, DSP_NON, voices); }
void au_pitch_modulation(AuVoices *v, AuVoiceSet voices) { put(v, DSP_PMON, voices); }
void au_echo_voices(AuVoices *v, AuVoiceSet voices) { put(v, DSP_EON, voices); }

void au_echo_volume(AuVoices *v, int8_t left, int8_t right)
{
    put(v, DSP_EVOLL, (uint8_t)left);
    put(v, DSP_EVOLR, (uint8_t)right);
}

void au_echo_feedback(AuVoices *v, int8_t feedback) { put(v, DSP_EFB, (uint8_t)feedback); }

void au_echo_filter(AuVoices *v, const int8_t taps[8])
{
    for (uint8_t tap = 0; tap < 8; tap++)
        put(v, (uint8_t)(tap << 4 | DSP_FIR), (uint8_t)taps[tap]);
}

/* The buffer ends where the driver's tables begin, and a unit of delay is
 * 2 KiB, eight pages. */
void au_echo_delay(AuVoices *v, uint8_t delay)
{
    put(v, DSP_EDL, delay);
    put(v, DSP_ESA, (uint8_t)(VOICES_DSP_ECHO_END_PAGE - 8 * delay));
}

void au_echo_writes(AuVoices *v, bool on)
{
    uint8_t flags = v->read(v->ctx, DSP_FLG);
    put(v, DSP_FLG, on ? (uint8_t)(flags & ~ECHO_WRITES_OFF) : (uint8_t)(flags | ECHO_WRITES_OFF));
}

/* The directory holds each sample's start and loop addresses. */
static void directory_entry(AuVoices *v, uint8_t sample, uint16_t start, uint16_t loop)
{
    uint16_t at = (uint16_t)(VOICES_DSP_DIRECTORY + 4 * sample);
    v->memory_write(v->ctx, at, (uint8_t)start);
    v->memory_write(v->ctx, (uint16_t)(at + 1), (uint8_t)(start >> 8));
    v->memory_write(v->ctx, (uint16_t)(at + 2), (uint8_t)loop);
    v->memory_write(v->ctx, (uint16_t)(at + 3), (uint8_t)(loop >> 8));
}

void au_sample_define(AuVoices *v, uint8_t sample, const uint8_t *brr, uint16_t length, uint16_t loop)
{
    for (uint16_t i = 0; i < length && i < VOICES_DSP_OWN_SAMPLE_BYTES; i++)
        v->memory_write(v->ctx, (uint16_t)(VOICES_DSP_OWN_SAMPLE + i), brr[i]);
    directory_entry(v, sample, VOICES_DSP_OWN_SAMPLE, (uint16_t)(VOICES_DSP_OWN_SAMPLE + loop));
}

void au_sample_write(AuVoices *v, uint8_t sample, uint16_t at, uint8_t byte)
{
    (void)sample;
    if (at < VOICES_DSP_OWN_SAMPLE_BYTES)
        v->memory_write(v->ctx, (uint16_t)(VOICES_DSP_OWN_SAMPLE + at), byte);
}

void au_voices_snes_register(AuVoices *v, uint8_t reg, uint8_t value) { put(v, reg, value); }
