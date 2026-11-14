/* The voices kept and not played (targets/gba/silent_mixer.h): each call
 * keeps what runtime/platform/native/mixer.c's keeps of the settings the
 * driver reads back, masked as it masks them. */
#include "silent_mixer.h"

#include <string.h>

static Mixer *mixer(AuVoices *v) { return (Mixer *)v; }
static AuVoiceSettings *voice(AuVoices *v, uint8_t n) { return &mixer(v)->voice[n & 7]; }

static void start(AuVoices *v) { (void)v; }
static void set_sample(AuVoices *v, uint8_t n, uint8_t sample) { (void)v, (void)n, (void)sample; }

static void set_envelope(AuVoices *v, uint8_t n, AuEnvelope e)
{
    voice(v, n)->envelope = (AuEnvelope){ e.adsr, (uint8_t)(e.attack & 0x0F), (uint8_t)(e.decay & 7),
                                          (uint8_t)(e.sustain_level & 7), (uint8_t)(e.sustain_rate & 0x1F) };
}

static void set_gain(AuVoices *v, uint8_t n, AuGain g) { (void)v, (void)n, (void)g; }
static void set_pitch(AuVoices *v, uint8_t n, uint16_t pitch) { voice(v, n)->pitch = pitch; }

static void set_volume(AuVoices *v, uint8_t n, int8_t left, int8_t right)
{
    voice(v, n)->volume_left = left;
    voice(v, n)->volume_right = right;
}

static void key_on(AuVoices *v, AuVoiceSet voices) { (void)v, (void)voices; }
static void key_off(AuVoices *v, AuVoiceSet voices) { (void)v, (void)voices; }

static void hold(AuVoices *v, bool held)
{
    for (uint8_t n = 0; n < AU_VOICES; n++)
        voice(v, n)->envelope.adsr = !held;
}

static int8_t output(AuVoices *v, uint8_t n) { (void)v, (void)n; return 0; }

static void master_volume(AuVoices *v, int8_t left, int8_t right)
{
    mixer(v)->mix.master_left = left;
    mixer(v)->mix.master_right = right;
}

static void noise_clock(AuVoices *v, uint8_t clock) { (void)v, (void)clock; }
static void muted(AuVoices *v, bool on) { (void)v, (void)on; }
static void reset(AuVoices *v, bool on) { (void)v, (void)on; }
static void noise_voices(AuVoices *v, AuVoiceSet voices) { (void)v, (void)voices; }
static void pitch_modulation(AuVoices *v, AuVoiceSet voices) { (void)v, (void)voices; }
static void echo_voices(AuVoices *v, AuVoiceSet voices) { (void)v, (void)voices; }

static void echo_volume(AuVoices *v, int8_t left, int8_t right)
{
    mixer(v)->mix.echo_left = left;
    mixer(v)->mix.echo_right = right;
}

static void echo_feedback(AuVoices *v, int8_t feedback) { (void)v, (void)feedback; }
static void echo_filter(AuVoices *v, const int8_t taps[8]) { memcpy(mixer(v)->mix.taps, taps, sizeof mixer(v)->mix.taps); }

static void echo_delay(AuVoices *v, uint8_t delay)
{
    mixer(v)->mix.echo_delay = delay;
    mixer(v)->mix.echo_page = voices_echo_page(delay);
}

static void echo_buffer(AuVoices *v, uint8_t page, uint8_t delay)
{
    mixer(v)->mix.echo_page = page;
    mixer(v)->mix.echo_delay = delay;
}

static void echo_writes(AuVoices *v, bool on) { (void)v, (void)on; }

/* Sound memory isn't kept; who watches it still learns of each write. */
static void put(Mixer *m, uint16_t at)
{
    if (m->watch)
        m->watch(m->watch_ctx, at);
}

static void sample_define(AuVoices *v, uint8_t sample, const uint8_t *brr, uint16_t length, uint16_t loop)
{
    (void)sample, (void)brr, (void)loop;
    for (uint16_t i = 0; i < length && i < VOICES_OWN_SAMPLE_BYTES; i++)
        put(mixer(v), (uint16_t)(VOICES_OWN_SAMPLE + i));
}

static void sample_write(AuVoices *v, uint8_t sample, uint16_t at, uint8_t byte)
{
    (void)sample, (void)byte;
    if (at < VOICES_OWN_SAMPLE_BYTES)
        put(mixer(v), (uint16_t)(VOICES_OWN_SAMPLE + at));
}

static void sample_memory_write(AuVoices *v, uint16_t at, uint8_t byte) { (void)byte; put(mixer(v), at); }

static void sample_memory_move(AuVoices *v, uint16_t to, uint16_t from, uint16_t length)
{
    (void)from;
    for (uint32_t i = 0; i < length; i++)
        put(mixer(v), (uint16_t)(to + i));
}

static void sample_place(AuVoices *v, uint8_t sample, uint16_t start_at, uint16_t loop)
{
    (void)v, (void)sample, (void)start_at, (void)loop;
}

static AuVoiceSettings voice_settings(AuVoices *v, uint8_t n) { return *voice(v, n); }
static AuMixSettings mix_settings(AuVoices *v) { return mixer(v)->mix; }

static const AuVoicesOps silent_ops = {
    start, set_sample, set_envelope, set_gain, set_pitch, set_volume, key_on, key_off, hold, output,
    master_volume, noise_clock, muted, reset, noise_voices, pitch_modulation, echo_voices, echo_volume,
    echo_feedback, echo_filter, echo_delay, echo_buffer, echo_writes, sample_define, sample_write,
    sample_memory_write, sample_memory_move, sample_place, voice_settings, mix_settings
};

AuVoices *mixer_voices(Mixer *m)
{
    m->voices.ops = &silent_ops;
    return &m->voices;
}

void mixer_init(Mixer *m)
{
    memset(m, 0, sizeof *m);
    m->voices.ops = &silent_ops;
}

void mixer_frame(Mixer *m, int16_t frame[2])
{
    (void)m;
    frame[0] = frame[1] = 0;
}

uint8_t mixer_echo_delay(const Mixer *m) { return m->mix.echo_delay; }
