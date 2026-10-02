/* The voices as S-DSP registers (platform/voices_dsp.h). */
#include "platform/voices_dsp.h"

#include "apu/dsp.h"

static uint8_t voice_reg(uint8_t voice, uint8_t reg) { return (uint8_t)((voice & 7) << 4 | reg); }

static VoicesDsp *dsp(AuVoices *v) { return (VoicesDsp *)v; }

static void put(AuVoices *v, uint8_t reg, uint8_t value) { dsp(v)->write(dsp(v)->ctx, reg, value); }
static uint8_t get(AuVoices *v, uint8_t reg) { return dsp(v)->read(dsp(v)->ctx, reg); }
static void memory_write(AuVoices *v, uint16_t at, uint8_t value) { dsp(v)->memory_write(dsp(v)->ctx, at, value); }
static uint8_t memory_read(AuVoices *v, uint16_t at) { return dsp(v)->memory_read(dsp(v)->ctx, at); }

static void dsp_voices_start(AuVoices *v) { put(v, DSP_DIR, VOICES_DIRECTORY >> 8); }

static void dsp_voice_sample(AuVoices *v, uint8_t voice, uint8_t sample) { put(v, voice_reg(voice, DSP_SRCN), sample); }

static void dsp_voice_envelope(AuVoices *v, uint8_t voice, AuEnvelope e)
{
    put(v, voice_reg(voice, DSP_ADSR0), (uint8_t)((e.adsr ? 0x80 : 0) | (e.decay & 7) << 4 | (e.attack & 0x0F)));
    put(v, voice_reg(voice, DSP_ADSR1), (uint8_t)((e.sustain_level & 7) << 5 | (e.sustain_rate & 0x1F)));
}

static void dsp_voice_gain(AuVoices *v, uint8_t voice, AuGain g)
{
    uint8_t value = g.mode == AU_GAIN_DIRECT ? (uint8_t)(g.value & 0x7F)
                                             : (uint8_t)(0x80 | (g.mode - AU_GAIN_DECREASE_LINEAR) << 5 | (g.value & 0x1F));
    put(v, voice_reg(voice, DSP_GAIN), value);
}

static void dsp_voice_pitch(AuVoices *v, uint8_t voice, uint16_t pitch)
{
    put(v, voice_reg(voice, DSP_PITCHL), (uint8_t)pitch);
    put(v, voice_reg(voice, DSP_PITCHH), (uint8_t)(pitch >> 8));
}

static void dsp_voice_volume(AuVoices *v, uint8_t voice, int8_t left, int8_t right)
{
    put(v, voice_reg(voice, DSP_VOLL), (uint8_t)left);
    put(v, voice_reg(voice, DSP_VOLR), (uint8_t)right);
}

static void dsp_voices_key_on(AuVoices *v, AuVoiceSet voices) { put(v, DSP_KON, voices); }
static void dsp_voices_key_off(AuVoices *v, AuVoiceSet voices) { put(v, DSP_KOFF, voices); }

enum { ADSR_ON = 0x80, RESET = 0x80, MUTED = 0x40, ECHO_WRITES_OFF = 0x20, NOISE_CLOCK = 0x1F };

static void dsp_voices_hold(AuVoices *v, bool hold)
{
    for (uint8_t voice = 0; voice < AU_VOICES; voice++) {
        uint8_t reg = voice_reg(voice, DSP_ADSR0);
        uint8_t adsr = get(v, reg);
        put(v, reg, hold ? (uint8_t)(adsr & ~ADSR_ON) : (uint8_t)(adsr | ADSR_ON));
    }
}

static int8_t dsp_voice_output(AuVoices *v, uint8_t voice) { return (int8_t)get(v, voice_reg(voice, DSP_OUTX)); }

static void dsp_master_volume(AuVoices *v, int8_t left, int8_t right)
{
    put(v, DSP_MVOLL, (uint8_t)left);
    put(v, DSP_MVOLR, (uint8_t)right);
}

/* The S-DSP keeps the noise clock, the echo's writes and the output's two
 * switches in one register (FLG), so each of their calls changes its own bits
 * and keeps the others'. */
static void set_flags(AuVoices *v, uint8_t bits, uint8_t value)
{
    put(v, DSP_FLG, (uint8_t)((get(v, DSP_FLG) & ~bits) | (value & bits)));
}

static void dsp_noise_clock(AuVoices *v, uint8_t clock) { set_flags(v, NOISE_CLOCK, clock); }

static void dsp_output_stopped(AuVoices *v, bool stopped) { set_flags(v, RESET | MUTED, stopped ? RESET | MUTED : 0); }

static void dsp_noise_voices(AuVoices *v, AuVoiceSet voices) { put(v, DSP_NON, voices); }
static void dsp_pitch_modulation(AuVoices *v, AuVoiceSet voices) { put(v, DSP_PMON, voices); }
static void dsp_echo_voices(AuVoices *v, AuVoiceSet voices) { put(v, DSP_EON, voices); }

static void dsp_echo_volume(AuVoices *v, int8_t left, int8_t right)
{
    put(v, DSP_EVOLL, (uint8_t)left);
    put(v, DSP_EVOLR, (uint8_t)right);
}

static void dsp_echo_feedback(AuVoices *v, int8_t feedback) { put(v, DSP_EFB, (uint8_t)feedback); }

static void dsp_echo_filter(AuVoices *v, const int8_t taps[8])
{
    for (uint8_t tap = 0; tap < 8; tap++)
        put(v, (uint8_t)(tap << 4 | DSP_FIR), (uint8_t)taps[tap]);
}

/* The buffer ends where the driver's tables begin (platform/voices.h). */
static void dsp_echo_delay(AuVoices *v, uint8_t delay)
{
    put(v, DSP_EDL, delay);
    put(v, DSP_ESA, voices_echo_page(delay));
}

static void dsp_echo_writes(AuVoices *v, bool on) { set_flags(v, ECHO_WRITES_OFF, on ? 0 : ECHO_WRITES_OFF); }

/* The directory holds each sample's start and loop addresses. */
static void directory_entry(AuVoices *v, uint8_t sample, uint16_t start, uint16_t loop)
{
    uint16_t at = (uint16_t)(VOICES_DIRECTORY + 4 * sample);
    memory_write(v, at, (uint8_t)start);
    memory_write(v, (uint16_t)(at + 1), (uint8_t)(start >> 8));
    memory_write(v, (uint16_t)(at + 2), (uint8_t)loop);
    memory_write(v, (uint16_t)(at + 3), (uint8_t)(loop >> 8));
}

static void dsp_sample_define(AuVoices *v, uint8_t sample, const uint8_t *brr, uint16_t length, uint16_t loop)
{
    for (uint16_t i = 0; i < length && i < VOICES_OWN_SAMPLE_BYTES; i++)
        memory_write(v, (uint16_t)(VOICES_OWN_SAMPLE + i), brr[i]);
    directory_entry(v, sample, VOICES_OWN_SAMPLE, (uint16_t)(VOICES_OWN_SAMPLE + loop));
}

static void dsp_sample_write(AuVoices *v, uint8_t sample, uint16_t at, uint8_t byte)
{
    (void)sample;
    if (at < VOICES_OWN_SAMPLE_BYTES)
        memory_write(v, (uint16_t)(VOICES_OWN_SAMPLE + at), byte);
}

static void dsp_sample_memory_write(AuVoices *v, uint16_t at, uint8_t byte) { memory_write(v, at, byte); }

/* The original's receiver ($1206) copies whole pages up from the first, then
 * the rest down from its last byte. */
static void dsp_sample_memory_move(AuVoices *v, uint16_t to, uint16_t from, uint16_t length)
{
    for (uint8_t pages = (uint8_t)(length >> 8); pages; pages--) {
        for (uint16_t i = 0; i < 0x100; i++)
            memory_write(v, (uint16_t)(to + i), memory_read(v, (uint16_t)(from + i)));
        from = (uint16_t)(from + 0x100);
        to = (uint16_t)(to + 0x100);
    }
    for (uint8_t i = (uint8_t)length; i; i--)
        memory_write(v, (uint16_t)(to + i - 1), memory_read(v, (uint16_t)(from + i - 1)));
}

static void dsp_sample_place(AuVoices *v, uint8_t sample, uint16_t start, uint16_t loop)
{
    directory_entry(v, sample, start, loop);
}

static void dsp_voices_snes_register(AuVoices *v, uint8_t reg, uint8_t value) { put(v, reg, value); }

static const AuVoicesOps dsp_ops = {
    dsp_voices_start, dsp_voice_sample, dsp_voice_envelope, dsp_voice_gain, dsp_voice_pitch, dsp_voice_volume,
    dsp_voices_key_on, dsp_voices_key_off, dsp_voices_hold, dsp_voice_output, dsp_master_volume, dsp_noise_clock,
    dsp_output_stopped, dsp_noise_voices, dsp_pitch_modulation, dsp_echo_voices, dsp_echo_volume, dsp_echo_feedback,
    dsp_echo_filter, dsp_echo_delay, dsp_echo_writes, dsp_sample_define, dsp_sample_write, dsp_sample_memory_write,
    dsp_sample_memory_move, dsp_sample_place, dsp_voices_snes_register
};

void voices_dsp_init(VoicesDsp *v, void *ctx, uint8_t (*read)(void *ctx, uint8_t reg),
                     void (*write)(void *ctx, uint8_t reg, uint8_t value),
                     void (*memory_write)(void *ctx, uint16_t at, uint8_t value),
                     uint8_t (*memory_read)(void *ctx, uint16_t at))
{
    *v = (VoicesDsp){ { &dsp_ops }, ctx, read, write, memory_write, memory_read };
}
