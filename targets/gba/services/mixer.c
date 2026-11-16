/* The GBA's mixer (targets/gba/services/mixer.h).
 *
 * The voices' calls keep what the driver reads back as the native mixer
 * (runtime/platform/native/mixer.c) keeps it, and set up what the voices
 * play. Where the S-DSP's own detail costs more than the GBA can give, it is
 * approximated (docs/PLATFORM.md, "The GBA build, as built", "The sound"):
 * a voice takes the nearest sample rather than the Gaussian four, its
 * envelope steps once a mix by as many of its rate's steps as fell in it,
 * key ons and offs take effect at the next mix in the order they came, and
 * the echo and the pitch modulation aren't heard. */
#include "services/mixer.h"

#include <string.h>

#include "services/direct_sound.h"
#include "system/debug_log.h"

static const MixerSamples *packed;

void mixer_use_samples(const MixerSamples *samples) { packed = samples; }

static const MixerSampleBlock *packed_block(uint16_t k) { return (const MixerSampleBlock *)(packed + 1) + k; }

static const MixerSample *packed_sample(uint16_t k)
{
    return (const MixerSample *)(packed_block(packed->blocks)) + k;
}

static const int8_t *packed_pcm(uint32_t at) { return (const int8_t *)packed + at; }

/* ---- Time --------------------------------------------------------------------------------- */

/* The mix is made as the GBA plays it, a chunk whenever the ring has room
 * for one (mixer_fill), so its time is the GBA's: 224 samples a GBA frame,
 * 448 output samples every 1,065 of the S-DSP's frames, a chunk's envelopes
 * stepped by the S-DSP's frames its samples last. The driver's time, which
 * passes with the game's frames, only changes what the voices play. Where
 * the game keeps its time the two are the same; where its frames take
 * longer, the voices go on sounding as the driver last set them rather than
 * the sound stopping, and the music's own steps come as the game's frames
 * do. */
enum { OUT_PER = 448, FRAMES_PER = 1065, CHUNK = DIRECT_SOUND_FILL };

enum { NOISE_PEAK = 0x4000 };   /* the noise's 15 bits at their loudest */

/* Every chunk's cycles, added from the interrupt; the report takes what was
 * added since it last looked. */
static volatile uint32_t mixing_cycles;

uint32_t mixer_take_cycles(void)
{
    static uint32_t taken;
    uint32_t total = mixing_cycles, c = total - taken;
    taken = total;
    return c;
}

/* ---- The S-DSP's envelopes -------------------------------------------------------------- */

/* Each rate's frames between steps (runtime/platform/native/mixer.c's). */
static const uint16_t rate_period[32] = {
    0, 2048, 1536, 1280, 1024, 768, 640, 512, 384, 320, 256, 192, 160, 128, 96, 80,
    64, 48, 40, 32, 24, 20, 16, 12, 10, 8, 6, 5, 4, 3, 2, 1,
};

enum {
    LEVEL_MAX = 0x7FF, ATTACK_STEP = 0x20, ATTACK_STEP_FASTEST = 0x400, FASTEST_RATE = 31, LINEAR_STEP = 0x20,
    BENT_KNEE = 0x600, BENT_STEP = 0x08, RELEASE_STEP = 8, DIRECT_GAIN_SCALE = 0x10
};

__attribute__((target("arm"), section(".iwram"))) static uint8_t envelope_rate(const AuEnvelope *e, const MixerVoice *v)
{
    if (!e->adsr)
        return v->gain_value & 0x1F;
    switch (v->phase) {
    case MIXER_ATTACK: return (uint8_t)((e->attack & 0x0F) * 2 + 1);
    case MIXER_DECAY: return (uint8_t)((e->decay & 7) * 2 + 16);
    default: return e->sustain_rate & 0x1F;
    }
}

/* Where the decay ends: the sustain level, or with the gain on, its top bits. */
__attribute__((target("arm"), section(".iwram"))) static int decay_end(const AuEnvelope *e, const MixerVoice *v)
{
    if (e->adsr)
        return e->sustain_level;
    return v->gain_mode == AU_GAIN_DIRECT ? (v->gain_value & 0x7F) >> 5 : 3 + v->gain_mode;
}

/* One step of the envelope at its rate, as the S-DSP takes it: in the
 * cartridge, as Thumb, since it runs only at the envelope's steps and fast
 * RAM's room does more as the picture's code (toolchain/fast_code.ld). */
__attribute__((noinline)) static void envelope_step(const AuEnvelope *e, MixerVoice *v)
{
    int level = v->level;
    if (e->adsr) {
        if (v->phase == MIXER_ATTACK) {
            level += envelope_rate(e, v) < FASTEST_RATE ? ATTACK_STEP : ATTACK_STEP_FASTEST;
        } else {
            level -= 1;
            level -= level >> 8;
        }
    } else {
        switch (v->gain_mode) {
        case AU_GAIN_DECREASE_LINEAR: level -= LINEAR_STEP; break;
        case AU_GAIN_DECREASE_EXPONENTIAL: level -= 1; level -= level >> 8; break;
        case AU_GAIN_INCREASE_LINEAR: level += LINEAR_STEP; break;
        default: level += level >= BENT_KNEE ? BENT_STEP : LINEAR_STEP; break;
        }
    }
    if (v->phase == MIXER_DECAY && (level >> 8) == decay_end(e, v))
        v->phase = MIXER_SUSTAIN;
    if (level < 0 || level > LEVEL_MAX) {
        level = level < 0 ? 0 : LEVEL_MAX;
        if (v->phase == MIXER_ATTACK)
            v->phase = MIXER_DECAY;
    }
    v->level = (int16_t)level;
}

/* The envelope over `frames` of the S-DSP's: a released note falls every
 * frame, a direct gain holds its level, and otherwise each of its rate's
 * steps that fell in them is taken. */
__attribute__((target("arm"), section(".iwram"), noinline)) static void run_envelope(const AuEnvelope *e, MixerVoice *v, uint32_t frames)
{
    if (v->phase == MIXER_RELEASE) {
        int level = v->level - (int)(RELEASE_STEP * frames);
        v->level = (int16_t)(level < 0 ? 0 : level);
        return;
    }
    if (!e->adsr && v->gain_mode == AU_GAIN_DIRECT) {
        v->level = (int16_t)((v->gain_value & 0x7F) * DIRECT_GAIN_SCALE);
        return;
    }
    for (;;) {
        uint8_t rate = envelope_rate(e, v);
        if (!rate)
            return;
        uint16_t period = rate_period[rate];
        if (v->until > period || !v->until)
            v->until = period;
        if (v->until > frames) {
            v->until = (uint16_t)(v->until - frames);
            return;
        }
        frames -= v->until;
        uint8_t phase = v->phase;
        envelope_step(e, v);
        v->until = phase == v->phase ? period : 0;
    }
}

/* ---- BRR, for the driver's own sample ---------------------------------------------------- */

enum { BLOCK_BYTES = 9, BLOCK_SAMPLES = 16, NO_LOOP_END = 1, LAST_BLOCK = 1 };

static int clamp16(int v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }

static int brr_sample(int nibble, uint8_t header, int last, int before_last)
{
    int shift = header >> 4;
    int s = shift <= 12 ? (nibble * (1 << shift)) >> 1 : (nibble < 0 ? -2048 : 0);
    switch ((header >> 2) & 3) {
    case 1: s += last + ((-last) >> 4); break;
    case 2: s += 2 * last + ((-3 * last) >> 5) - before_last + (before_last >> 4); break;
    case 3: s += 2 * last + ((-13 * last) >> 6) - before_last + ((3 * before_last) >> 4); break;
    }
    return (int16_t)(uint16_t)(clamp16(s) * 2) >> 1;
}

/* A block of the own sample decoded into `out`, after the two samples in `history`. */
static void decode_block(const uint8_t *block, int16_t *out, int history[2])
{
    for (int i = 0; i < BLOCK_SAMPLES; i++) {
        int nibble = (int)(((block[1 + i / 2] >> (i & 1 ? 0 : 4)) & 0x0F) ^ 8) - 8;
        int s = brr_sample(nibble, block[0], history[0], history[1]);
        out[i] = (int16_t)s;
        history[1] = history[0];
        history[0] = s;
    }
}

/* The driver's own sample as it plays (samples.py's decode, here because the
 * driver rewrites it as it plays), scaled to 8 bits. */
static void decode_own_sample(Mixer *m)
{
    int16_t decoded[MIXER_OWN_SAMPLES];
    int history[2] = { 0, 0 };
    unsigned blocks = VOICES_OWN_SAMPLE_BYTES / BLOCK_BYTES, loop_block = m->own_loop / BLOCK_BYTES;
    for (unsigned b = 0; b < blocks; b++)
        decode_block(&m->own_brr[b * BLOCK_BYTES], &decoded[b * BLOCK_SAMPLES], history);
    for (unsigned b = loop_block; b < blocks; b++)   /* the loop as it sounds coming round */
        decode_block(&m->own_brr[b * BLOCK_BYTES], &decoded[b * BLOCK_SAMPLES], history);
    int peak = 1;
    for (unsigned i = 0; i < MIXER_OWN_SAMPLES; i++)
        if (decoded[i] > peak || -decoded[i] > peak)
            peak = decoded[i] < 0 ? -decoded[i] : decoded[i];
    for (unsigned i = 0; i < MIXER_OWN_SAMPLES; i++)
        m->own_pcm[i] = (int8_t)(decoded[i] * 127 / peak);
    m->own_peak = (uint16_t)peak;
    m->own_changed = false;
}

/* ---- The mix kept from what the driver is writing ---------------------------------------- */

/* While a call of the driver's changes what the mix reads, the playback's
 * interrupt doesn't mix (mixer_fill). The barriers keep the compiler from
 * moving the call's writes out from between the two. */
static void hold_the_mix(Mixer *m)
{
    m->writing++;
    __asm__ volatile("" ::: "memory");
}

static void release_the_mix(Mixer *m)
{
    __asm__ volatile("" ::: "memory");
    m->writing--;
}

/* ---- What sound memory holds ------------------------------------------------------------- */

enum { FNV_OFFSET = 0x811C9DC5u, FNV_PRIME = 0x01000193u, NOT_WRITING = 0xFFFFFFFFu };

static void forget_overlapping(Mixer *m, uint16_t at, uint16_t length)
{
    uint8_t kept = 0;
    for (uint8_t r = 0; r < m->regions; r++) {
        const MixerRegion *g = &m->region[r];
        bool apart = (uint16_t)(g->at - at) >= length && (uint16_t)(at - g->at) >= g->length;
        if (apart)
            m->region[kept++] = *g;
    }
    m->regions = kept;
}

static void know_block(Mixer *m, uint16_t at, uint16_t block)
{
    uint16_t length = (uint16_t)packed_block(block)->length;
    hold_the_mix(m);   /* the regions a voice's start looks in (mixer_fill) */
    forget_overlapping(m, at, length);
    if (m->regions < MIXER_REGIONS)
        m->region[m->regions++] = (MixerRegion){ at, length, (uint8_t)block };
    release_the_mix(m);
}

static void start_run(Mixer *m, uint32_t at)
{
    m->run_next = at;
    m->run_at = (uint16_t)at;
    m->run_cursor = 0;
    m->run_length = 0;
    m->run_hash = FNV_OFFSET;
}

/* A byte the driver writes: the bytes written one after another since the
 * last known block, once they are as long as a block and hash as it does,
 * are that block. */
static void written(Mixer *m, uint16_t at, uint8_t byte)
{
    if (!packed)
        return;
    if (at != m->run_next)
        start_run(m, at);
    m->run_hash = (m->run_hash ^ byte) * FNV_PRIME;
    m->run_length++;
    m->run_next = (uint16_t)(at + 1);
    while (m->run_cursor < packed->blocks && packed_block(m->run_cursor)->length < m->run_length)
        m->run_cursor++;
    for (uint16_t k = m->run_cursor; k < packed->blocks && packed_block(k)->length == m->run_length; k++)
        if (packed_block(k)->hash == m->run_hash) {
            know_block(m, m->run_at, k);
            start_run(m, (uint16_t)(at + 1));
            return;
        }
}

/* What a voice starting sample `sample` plays: the driver's own, or the
 * packed sample the directory points into a known block at. */
static void find_sample(Mixer *m, MixerVoice *v)
{
    v->wave.pcm = NULL;
    if (v->sample >= MIXER_DIRECTORY)
        return;
    uint16_t start = m->directory[v->sample].start;
    if (start == VOICES_OWN_SAMPLE) {
        if (m->own_changed)
            decode_own_sample(m);
        uint16_t loop = (uint16_t)(m->directory[v->sample].loop - start);
        v->wave = (MixerWave){ m->own_pcm, MIXER_OWN_SAMPLES,
                               loop < VOICES_OWN_SAMPLE_BYTES ? loop / BLOCK_BYTES * BLOCK_SAMPLES : MIXER_NO_LOOP };
        v->peak = m->own_peak;
        return;
    }
    if (!packed)
        return;
    for (uint8_t r = 0; r < m->regions; r++) {
        const MixerRegion *g = &m->region[r];
        uint16_t offset = (uint16_t)(start - g->at);
        if (offset >= g->length)
            continue;
        const MixerSampleBlock *b = packed_block(g->block);
        for (uint16_t k = b->first; k < b->first + b->count; k++) {
            const MixerSample *s = packed_sample(k);
            if (s->start == offset && s->length) {
                v->wave = (MixerWave){ packed_pcm(s->pcm_at), s->length, s->loop };
                v->peak = s->peak;
                return;
            }
        }
        return;
    }
}

/* ---- Mixing ----------------------------------------------------------------------------- */

/* The mix is kept a word an output sample, both sides in it: a voice's
 * sample times its gains, the left's in the top half and the right's in the
 * bottom, adds to both sides at once, one multiply rather than two. Each
 * side's gain is at most 127, so the sides keep apart until the mix is twice
 * as loud as 8 bits can say (to_8_bits). */
static int32_t gains_of(int left, int right) { return (int32_t)((uint32_t)left << 16) + right; }

/* The mixer's loops (services/mix.s: ARM, in fast RAM). A wave's `count`
 * samples added into `mix` at `gains`, from `position` in 4096ths of a
 * sample stepping `step`; false where the wave ended. */
__attribute__((long_call)) bool mix_wave(int32_t *mix, unsigned count, const MixerWave *w, uint32_t *position,
                                         uint32_t step, int32_t gains);
/* The mix to 8 bits, each side, `count` a multiple of 4. */
__attribute__((long_call)) void to_8_bits(const int32_t *mix, int8_t *left, int8_t *right, unsigned count);

static void key_on(Mixer *m, MixerVoice *v)
{
    find_sample(m, v);
    v->phase = MIXER_ATTACK;
    v->level = 0;
    v->until = 0;
    v->position = 0;
}

static void take_keys(Mixer *m)
{
    for (uint8_t e = 0; e < m->key_events; e++)
        for (uint8_t n = 0; n < AU_VOICES; n++)
            if (m->keys[e].voices & (1u << n)) {
                if (m->keys[e].on)
                    key_on(m, &m->play[n]);
                else
                    m->play[n].phase = MIXER_RELEASE;
            }
    m->key_events = 0;
}

/* A side's gain for a voice, -127 to 127: its sample's peak at its
 * envelope's level, its volume and the master's, so that the PCM's 127 times
 * it is that much of 16 bits over 2^7 (the S-DSP's voice puts out twice a
 * 15-bit sample at full level). */
static int gain(const MixerVoice *v, int8_t volume, int8_t master)
{
    int full = (2 * v->peak * v->level) >> 11;
    return ((((full * volume) >> 7) * master >> 7) + 128) >> 8;
}

static int32_t gains(const MixerVoice *v, const AuVoiceSettings *set, const AuMixSettings *mix)
{
    return gains_of(gain(v, set->volume_left, mix->master_left), gain(v, set->volume_right, mix->master_right));
}

static int32_t mix[CHUNK] __attribute__((section(".iwram_bss")));

__attribute__((target("arm"), section(".iwram"), long_call, noinline)) static void mix_chunk(Mixer *m, unsigned count, uint32_t frames)
{
    uint32_t began = cycles_now();
    take_keys(m);
    memset(mix, 0, sizeof mix[0] * count);
    for (uint8_t n = 0; n < AU_VOICES; n++) {
        MixerVoice *v = &m->play[n];
        const AuEnvelope *e = &m->voice[n].envelope;
        if (m->resetting)
            v->phase = MIXER_RELEASE, v->level = 0;
        run_envelope(e, v, frames);
        bool noisy = m->noise_voices & (1u << n);
        if ((!v->wave.pcm && !noisy) || !v->level)
            continue;
        const AuVoiceSettings *set = &m->voice[n];
        if (noisy) {
            /* the noise generator, stepping at its clock, played as a wave */
            uint16_t period = rate_period[m->noise_clock & 0x1F];
            if (!period)
                continue;
            MixerVoice noise = *v;
            noise.wave = (MixerWave){ packed_pcm(packed->noise_at), MIXER_NOISE_LENGTH, 0 };
            noise.peak = NOISE_PEAK;
            mix_wave(mix, count, &noise.wave, &v->position, 4096u * FRAMES_PER / OUT_PER / period,
                     gains(&noise, set, &m->mix));
            continue;
        }
        uint32_t step = (uint32_t)(set->pitch & 0x3FFF) * FRAMES_PER / OUT_PER;
        if (!mix_wave(mix, count, &v->wave, &v->position, step, gains(v, set, &m->mix)))
            v->wave.pcm = NULL, v->level = 0, v->phase = MIXER_RELEASE;
    }
    int8_t *left, *right;
    if (m->silenced)
        memset(mix, 0, sizeof mix[0] * count);
    direct_sound_next(&left, &right);
    to_8_bits(mix, left, right, count);
    direct_sound_queued();
    mixing_cycles += cycles_now() - began;
}

/* The S-DSP's frames from a chunk's start, `owed` toward its first sample,
 * to the one its last sample falls due at. */
static uint16_t frames_to_chunk(uint32_t owed)
{
    return (uint16_t)((CHUNK * FRAMES_PER - owed + OUT_PER - 1) / OUT_PER);
}

/* The driver's time makes no sound of its own here (the playback asks for
 * it, mixer_fill). */
void mixer_pass(Mixer *m, uint32_t frames) { (void)m, (void)frames; }

/* The mixer the playback fills the ring from: the one made last. */
static Mixer *played;

/* Chunks mixed until the ring is full, from the playback's interrupt with
 * the other interrupts on (services/direct_sound.h). The driver's calls run
 * between the game's frames and in its waits, never inside this; where the
 * interrupt came in the middle of one (`writing`), the voices may be half
 * set, and the ring is filled at the next interrupt instead, 16 samples on. */
__attribute__((target("arm"), section(".iwram"), noinline)) void mixer_fill(void)
{
    Mixer *m = played;
    if (!m || m->writing)
        return;
    while (direct_sound_room() >= CHUNK) {
        uint32_t frames = frames_to_chunk(m->owed);
        mix_chunk(m, CHUNK, frames);
        m->owed = m->owed + frames * OUT_PER - CHUNK * FRAMES_PER;
    }
}

/* ---- The voices' calls ------------------------------------------------------------------- */

static Mixer *mixer(AuVoices *v) { return (Mixer *)v; }
static AuVoiceSettings *voice(AuVoices *v, uint8_t n) { return &mixer(v)->voice[n & 7]; }

/* A call that changes what the mix reads says so while it does, so that the
 * playback's interrupt doesn't mix from voices half set (mixer_fill). */
static Mixer *begin_write(AuVoices *v)
{
    Mixer *m = mixer(v);
    hold_the_mix(m);
    return m;
}
static void end_write(Mixer *m) { release_the_mix(m); }

static void start(AuVoices *v) { (void)v; }
static void set_sample(AuVoices *v, uint8_t n, uint8_t sample)
{
    Mixer *w = begin_write(v);
    mixer(v)->play[n & 7].sample = sample;
    end_write(w);
}

static void set_envelope(AuVoices *v, uint8_t n, AuEnvelope e)
{
    Mixer *w = begin_write(v);
    voice(v, n)->envelope = (AuEnvelope){ e.adsr, (uint8_t)(e.attack & 0x0F), (uint8_t)(e.decay & 7),
                                          (uint8_t)(e.sustain_level & 7), (uint8_t)(e.sustain_rate & 0x1F) };
    end_write(w);
}

static void set_gain(AuVoices *v, uint8_t n, AuGain g)
{
    Mixer *w = begin_write(v);
    MixerVoice *p = &mixer(v)->play[n & 7];
    p->gain_mode = (uint8_t)g.mode;
    p->gain_value = g.mode == AU_GAIN_DIRECT ? (uint8_t)(g.value & 0x7F) : (uint8_t)(g.value & 0x1F);
    end_write(w);
}

static void set_pitch(AuVoices *v, uint8_t n, uint16_t pitch)
{
    Mixer *w = begin_write(v);
    voice(v, n)->pitch = pitch;
    end_write(w);
}

static void set_volume(AuVoices *v, uint8_t n, int8_t left, int8_t right)
{
    Mixer *w = begin_write(v);
    voice(v, n)->volume_left = left;
    voice(v, n)->volume_right = right;
    end_write(w);
}

static void key_event(Mixer *m, bool on, AuVoiceSet voices)
{
    if (!voices)
        return;
    if (m->key_events == MIXER_KEY_EVENTS) {
        take_keys(m);   /* more than a mix's worth: taken now */
    }
    m->keys[m->key_events].on = on;
    m->keys[m->key_events++].voices = voices;
}

static void keys_on(AuVoices *v, AuVoiceSet voices)
{
    Mixer *w = begin_write(v);
    key_event(mixer(v), true, voices);
    end_write(w);
}
static void keys_off(AuVoices *v, AuVoiceSet voices)
{
    Mixer *w = begin_write(v);
    key_event(mixer(v), false, voices);
    end_write(w);
}

static void hold(AuVoices *v, bool held)
{
    Mixer *w = begin_write(v);
    for (uint8_t n = 0; n < AU_VOICES; n++)
        voice(v, n)->envelope.adsr = !held;
    end_write(w);
}

/* The voice's output as the meter reads it: its sample where it is, at its
 * level. The game never turns the meter on (docs/SOUND.md). */
static int8_t output(AuVoices *v, uint8_t n)
{
    const MixerVoice *p = &mixer(v)->play[n & 7];
    if (!p->wave.pcm || (p->position >> 12) >= p->wave.length)
        return 0;
    return (int8_t)((p->wave.pcm[p->position >> 12] * ((2 * p->peak * p->level) >> 11)) >> 15);
}

static void master_volume(AuVoices *v, int8_t left, int8_t right)
{
    Mixer *w = begin_write(v);
    mixer(v)->mix.master_left = left;
    mixer(v)->mix.master_right = right;
    end_write(w);
}

static void noise_clock(AuVoices *v, uint8_t clock)
{
    Mixer *w = begin_write(v);
    mixer(v)->noise_clock = clock & 0x1F;
    end_write(w);
}
static void muted(AuVoices *v, bool on)
{
    Mixer *w = begin_write(v);
    mixer(v)->silenced = on;
    end_write(w);
}
static void reset(AuVoices *v, bool on)
{
    Mixer *w = begin_write(v);
    mixer(v)->resetting = on;
    end_write(w);
}
static void noise_voices(AuVoices *v, AuVoiceSet voices)
{
    Mixer *w = begin_write(v);
    mixer(v)->noise_voices = voices;
    end_write(w);
}
static void pitch_modulation(AuVoices *v, AuVoiceSet voices) { (void)v, (void)voices; }
static void echo_voices(AuVoices *v, AuVoiceSet voices) { (void)v, (void)voices; }

static void echo_volume(AuVoices *v, int8_t left, int8_t right)
{
    Mixer *w = begin_write(v);
    mixer(v)->mix.echo_left = left;
    mixer(v)->mix.echo_right = right;
    end_write(w);
}

static void echo_feedback(AuVoices *v, int8_t feedback) { (void)v, (void)feedback; }
static void echo_filter(AuVoices *v, const int8_t taps[8])
{
    Mixer *w = begin_write(v);
    memcpy(mixer(v)->mix.taps, taps, sizeof mixer(v)->mix.taps);
    end_write(w);
}

static void echo_delay(AuVoices *v, uint8_t delay)
{
    Mixer *w = begin_write(v);
    mixer(v)->mix.echo_delay = delay;
    mixer(v)->mix.echo_page = voices_echo_page(delay);
    end_write(w);
}

static void echo_buffer(AuVoices *v, uint8_t page, uint8_t delay)
{
    Mixer *w = begin_write(v);
    mixer(v)->mix.echo_page = page;
    mixer(v)->mix.echo_delay = delay;
    end_write(w);
}

static void echo_writes(AuVoices *v, bool on) { (void)v, (void)on; }

/* Sound memory isn't kept; who watches it still learns of each write. */
static void put(Mixer *m, uint16_t at)
{
    if (m->watch)
        m->watch(m->watch_ctx, at);
}

static void place(Mixer *m, uint8_t sample, uint16_t start_at, uint16_t loop)
{
    for (uint16_t i = 0; i < 4; i++)
        put(m, (uint16_t)(VOICES_DIRECTORY + 4 * sample + i));
    if (sample < MIXER_DIRECTORY)
        m->directory[sample].start = start_at, m->directory[sample].loop = loop;
}

static void sample_define(AuVoices *v, uint8_t sample, const uint8_t *brr, uint16_t length, uint16_t loop)
{
    Mixer *w = begin_write(v);
    Mixer *m = mixer(v);
    for (uint16_t i = 0; i < length && i < VOICES_OWN_SAMPLE_BYTES; i++) {
        put(m, (uint16_t)(VOICES_OWN_SAMPLE + i));
        m->own_brr[i] = brr[i];
    }
    m->own_loop = loop;
    m->own_changed = true;
    place(m, sample, VOICES_OWN_SAMPLE, (uint16_t)(VOICES_OWN_SAMPLE + loop));
    end_write(w);
}

static void sample_write(AuVoices *v, uint8_t sample, uint16_t at, uint8_t byte)
{
    Mixer *w = begin_write(v);
    (void)sample;
    if (at < VOICES_OWN_SAMPLE_BYTES) {
        put(mixer(v), (uint16_t)(VOICES_OWN_SAMPLE + at));
        mixer(v)->own_brr[at] = byte;
        mixer(v)->own_changed = true;
    }
    end_write(w);
}

/* A song's load is thousands of these, which the mix reads nothing of until
 * a block is known (know_block): only that is kept from the mix. */
static void sample_memory_write(AuVoices *v, uint16_t at, uint8_t byte)
{
    put(mixer(v), at);
    written(mixer(v), at, byte);
}

/* The packed block the cartridge holds at `source`, `length` bytes long, or
 * the blocks' count where none is. */
static uint16_t block_from(uint32_t source, uint16_t length)
{
    for (uint16_t k = 0; k < packed->blocks; k++)
        if (packed_block(k)->source == source && packed_block(k)->length == length)
            return k;
    return packed->blocks;
}

/* A song's sample block, sent whole from the cartridge: known by where it
 * comes from, which the packing step listed with it, with none of its bytes
 * read. A run the packing step didn't list is taken a byte at a time. */
static void sample_memory_send(AuVoices *v, uint16_t at, PfData bytes, uint16_t length)
{
    Mixer *m = mixer(v);
    if (m->watch)
        for (uint16_t i = 0; i < length; i++)
            put(m, (uint16_t)(at + i));
    if (!packed)
        return;
    uint16_t k = block_from(bytes.addr, length);
    if (k < packed->blocks) {
        know_block(m, at, k);
        start_run(m, (uint16_t)(at + length));
        return;
    }
    for (uint16_t i = 0; i < length; i++) {
        const uint8_t *byte = pf_data_pointer(bytes, i);
        written(m, (uint16_t)(at + i), byte ? *byte : 0);
    }
}

/* The known blocks moved go where they are moved to, and what was there is
 * forgotten. */
static void sample_memory_move(AuVoices *v, uint16_t to, uint16_t from, uint16_t length)
{
    Mixer *w = begin_write(v);
    Mixer *m = mixer(v);
    for (uint32_t i = 0; i < length; i++)
        put(m, (uint16_t)(to + i));
    MixerRegion moved[MIXER_REGIONS];
    uint8_t count = 0, kept = 0;
    for (uint8_t r = 0; r < m->regions; r++) {
        MixerRegion g = m->region[r];
        if ((uint16_t)(g.at - from) < length && (uint16_t)(g.at - from) + g.length <= length) {
            g.at = (uint16_t)(g.at - from + to);
            moved[count++] = g;
        } else {
            m->region[kept++] = g;
        }
    }
    m->regions = kept;
    forget_overlapping(m, to, length);
    for (uint8_t r = 0; r < count && m->regions < MIXER_REGIONS; r++)
        m->region[m->regions++] = moved[r];
    m->run_next = NOT_WRITING;
    end_write(w);
}

static void sample_place(AuVoices *v, uint8_t sample, uint16_t start_at, uint16_t loop)
{
    Mixer *w = begin_write(v);
    place(mixer(v), sample, start_at, loop);
    end_write(w);
}

static AuVoiceSettings voice_settings(AuVoices *v, uint8_t n) { return *voice(v, n); }
static AuMixSettings mix_settings(AuVoices *v) { return mixer(v)->mix; }

static const AuVoicesOps gba_ops = {
    start, set_sample, set_envelope, set_gain, set_pitch, set_volume, keys_on, keys_off, hold, output,
    master_volume, noise_clock, muted, reset, noise_voices, pitch_modulation, echo_voices, echo_volume,
    echo_feedback, echo_filter, echo_delay, echo_buffer, echo_writes, sample_define, sample_write,
    sample_memory_write, sample_memory_send, sample_memory_move, sample_place, voice_settings, mix_settings
};

AuVoices *mixer_voices(Mixer *m)
{
    m->voices.ops = &gba_ops;
    return &m->voices;
}

/* Silent, as the S-DSP is at power-on: the output stopped and the voices held. */
void mixer_init(Mixer *m)
{
    memset(m, 0, sizeof *m);
    m->voices.ops = &gba_ops;
    m->silenced = m->resetting = true;
    m->run_next = NOT_WRITING;
    played = m;
}

uint8_t mixer_echo_delay(const Mixer *m) { return m->mix.echo_delay; }
