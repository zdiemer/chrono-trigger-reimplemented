/* The native mixer (platform/native/mixer.h). */
#include "platform/native/mixer.h"

#include <stdio.h>
#include <string.h>

/* ---- The S-DSP's constants -------------------------------------------------------------- */

/* The interpolation's kernel, 512 weights: four of them, picked by where
 * between two samples the voice is, weigh the four samples around it (each
 * four sum to 2047-2049). */
static const int16_t gauss[512] = {
       0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
       1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    1,    2,    2,    2,    2,    2,
       2,    2,    3,    3,    3,    3,    3,    4,    4,    4,    4,    4,    5,    5,    5,    5,
       6,    6,    6,    6,    7,    7,    7,    8,    8,    8,    9,    9,    9,   10,   10,   10,
      11,   11,   11,   12,   12,   13,   13,   14,   14,   15,   15,   15,   16,   16,   17,   17,
      18,   19,   19,   20,   20,   21,   21,   22,   23,   23,   24,   24,   25,   26,   27,   27,
      28,   29,   29,   30,   31,   32,   32,   33,   34,   35,   36,   36,   37,   38,   39,   40,
      41,   42,   43,   44,   45,   46,   47,   48,   49,   50,   51,   52,   53,   54,   55,   56,
      58,   59,   60,   61,   62,   64,   65,   66,   67,   69,   70,   71,   73,   74,   76,   77,
      78,   80,   81,   83,   84,   86,   87,   89,   90,   92,   94,   95,   97,   99,  100,  102,
     104,  106,  107,  109,  111,  113,  115,  117,  118,  120,  122,  124,  126,  128,  130,  132,
     134,  137,  139,  141,  143,  145,  147,  150,  152,  154,  156,  159,  161,  163,  166,  168,
     171,  173,  175,  178,  180,  183,  186,  188,  191,  193,  196,  199,  201,  204,  207,  210,
     212,  215,  218,  221,  224,  227,  230,  233,  236,  239,  242,  245,  248,  251,  254,  257,
     260,  263,  267,  270,  273,  276,  280,  283,  286,  290,  293,  297,  300,  304,  307,  311,
     314,  318,  321,  325,  328,  332,  336,  339,  343,  347,  351,  354,  358,  362,  366,  370,
     374,  378,  381,  385,  389,  393,  397,  401,  405,  410,  414,  418,  422,  426,  430,  434,
     439,  443,  447,  451,  456,  460,  464,  469,  473,  477,  482,  486,  491,  495,  499,  504,
     508,  513,  517,  522,  527,  531,  536,  540,  545,  550,  554,  559,  563,  568,  573,  577,
     582,  587,  592,  596,  601,  606,  611,  615,  620,  625,  630,  635,  640,  644,  649,  654,
     659,  664,  669,  674,  678,  683,  688,  693,  698,  703,  708,  713,  718,  723,  728,  732,
     737,  742,  747,  752,  757,  762,  767,  772,  777,  782,  787,  792,  797,  802,  806,  811,
     816,  821,  826,  831,  836,  841,  846,  851,  855,  860,  865,  870,  875,  880,  884,  889,
     894,  899,  904,  908,  913,  918,  923,  927,  932,  937,  941,  946,  951,  955,  960,  965,
     969,  974,  978,  983,  988,  992,  997, 1001, 1005, 1010, 1014, 1019, 1023, 1027, 1032, 1036,
    1040, 1045, 1049, 1053, 1057, 1061, 1066, 1070, 1074, 1078, 1082, 1086, 1090, 1094, 1098, 1102,
    1106, 1109, 1113, 1117, 1121, 1125, 1128, 1132, 1136, 1139, 1143, 1146, 1150, 1153, 1157, 1160,
    1164, 1167, 1170, 1174, 1177, 1180, 1183, 1186, 1190, 1193, 1196, 1199, 1202, 1205, 1207, 1210,
    1213, 1216, 1219, 1221, 1224, 1227, 1229, 1232, 1234, 1237, 1239, 1241, 1244, 1246, 1248, 1251,
    1253, 1255, 1257, 1259, 1261, 1263, 1265, 1267, 1269, 1270, 1272, 1274, 1275, 1277, 1279, 1280,
    1282, 1283, 1284, 1286, 1287, 1288, 1290, 1291, 1292, 1293, 1294, 1295, 1296, 1297, 1297, 1298,
    1299, 1300, 1300, 1301, 1302, 1302, 1303, 1303, 1303, 1304, 1304, 1304, 1304, 1304, 1305, 1305,
};

/* The rates an envelope or the noise steps at, 1-31, as frames between steps
 * (0 never steps). They count on one clock shared by every voice and the
 * noise, which runs down through 30720 frames (the periods' least common
 * multiple); the periods of 1, 3 and 5 times a power of two step at different
 * points of it. */
static const uint16_t rate_period[32] = {
    0, 2048, 1536, 1280, 1024, 768, 640, 512, 384, 320, 256, 192, 160, 128, 96, 80,
    64, 48, 40, 32, 24, 20, 16, 12, 10, 8, 6, 5, 4, 3, 2, 1,
};
static const uint16_t rate_phase[3] = { 536, 0, 1040 };
enum { RATE_CLOCK = 30720 };

static bool rate_steps(const Mixer *m, uint8_t rate)
{
    rate &= 0x1F;
    return rate && (m->rate_counter + rate_phase[rate % 3]) % rate_period[rate] == 0;
}

enum {
    LEVEL_MAX = 0x7FF,
    START_FRAMES = 5,            /* from a key on to the note's first sound */
    NO_LOOP = 1,                 /* a block's flags: the sample ends here without looping ... */
    LAST_BLOCK = 1,              /* ... or this is its last block (with loop, it loops) */
    BLOCK_BYTES = 9,
    ECHO_UNIT_BYTES = 0x800,
    ECHO_FRAME_BYTES = 4
};

static int clamp16(int v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
static int wrap16(int v) { return (int16_t)(uint16_t)v; }

static uint16_t memory16(const Mixer *m, uint16_t at)
{
    return (uint16_t)(m->memory[at] | m->memory[(uint16_t)(at + 1)] << 8);
}

/* ---- Samples ---------------------------------------------------------------------------- */

/* Where a sample starts, and where it loops from at its end, from the
 * directory. The directory lies in sound memory, where the original driver
 * keeps it, because the echo can reach it: a buffer whose size changes while
 * it plays runs on at the old size from the new place until it wraps, past
 * the top of memory and round into the bottom, and what it reads there is
 * heard. */
static uint16_t sample_start(const Mixer *m, uint8_t sample)
{
    return memory16(m, (uint16_t)(VOICES_DIRECTORY + 4 * sample));
}

static uint16_t sample_loop(const Mixer *m, uint8_t sample)
{
    return memory16(m, (uint16_t)(VOICES_DIRECTORY + 4 * sample + 2));
}

/* One sample of BRR: a nibble scaled by the block's range, plus the block's
 * filter of the two before it. The S-DSP keeps 15 bits, saturating to 16
 * first and then losing the top one, which some samples rely on. */
static int brr_sample(int nibble, uint8_t header, int older, int old)
{
    int shift = header >> 4;
    int s = shift <= 12 ? (nibble * (1 << shift)) >> 1 : (nibble < 0 ? -2048 : 0);
    switch ((header >> 2) & 3) {
    case 1: s += older + ((-older) >> 4); break;
    case 2: s += 2 * older + ((-3 * older) >> 5) - old + (old >> 4); break;
    case 3: s += 2 * older + ((-13 * older) >> 6) - old + ((3 * old) >> 4); break;
    }
    return wrap16(clamp16(s) * 2) >> 1;
}

/* The next four samples of the voice's block, from the pair of bytes it has
 * reached. Four at a time, when the voice needs them, as the S-DSP does: a
 * byte that changes under a playing voice (the echo's buffer running over a
 * sample, the driver rewriting its own) is heard from the next four on. */
static void decode_four(Mixer *m, MixerVoice *v, uint8_t header)
{
    int newest = v->decoded[(v->decoded_at + 11) % 12];
    int before = v->decoded[(v->decoded_at + 10) % 12];
    uint16_t at = (uint16_t)(v->block + v->block_byte);
    unsigned bytes = (unsigned)m->memory[at] << 8 | m->memory[(uint16_t)(at + 1)];
    for (int k = 0; k < 4; k++) {
        int nibble = (int)(((bytes >> (12 - 4 * k)) & 0x0F) ^ 8) - 8;
        int s = brr_sample(nibble, header, newest, before);
        v->decoded[(v->decoded_at + k) % 12] = (int16_t)s;
        before = newest;
        newest = s;
    }
    v->decoded_at = (uint8_t)((v->decoded_at + 4) % 12);
}

/* The voice's sample where it is now, from the four decoded around it. The
 * first three products' sum wraps at 16 bits before the fourth is added: an
 * overflow the S-DSP has, kept so the loudest samples sound as they did. */
static int interpolate(const MixerVoice *v)
{
    int between = (v->position >> 4) & 0xFF;
    int first = v->decoded_at + (v->position >> 12);
    int out = (gauss[255 - between] * v->decoded[first % 12]) >> 10;
    out += (gauss[511 - between] * v->decoded[(first + 1) % 12]) >> 10;
    out += (gauss[256 + between] * v->decoded[(first + 2) % 12]) >> 10;
    out = wrap16(out);
    out += (gauss[between] * v->decoded[(first + 3) % 12]) >> 10;
    return clamp16(out) & ~1;
}

/* ---- Envelopes -------------------------------------------------------------------------- */

/* What the decay compares its level with to find the sustain: the ADSR's
 * sustain level, or with the voice at its gain the top bits of the gain as the
 * S-DSP encodes it (the driver's pause switches a voice to its gain mid-decay). */
static int decay_end(const MixerVoice *v)
{
    if (v->adsr)
        return v->sustain_level;
    return v->gain_mode == AU_GAIN_DIRECT ? (v->gain_value & 0x7F) >> 5 : 3 + v->gain_mode;
}

/* One frame of the voice's envelope: the level it is heading for, and the
 * rate at which it gets there (a step is taken only when the rate's turn
 * comes round on the shared clock). A released note falls every frame. */
static void run_envelope(const Mixer *m, MixerVoice *v)
{
    int level = v->level;
    if (v->phase == MIXER_RELEASE) {
        level -= 8;
        v->level = (int16_t)(level < 0 ? 0 : level);
        return;
    }

    int rate;
    if (v->adsr) {
        if (v->phase == MIXER_ATTACK) {
            rate = (v->attack & 0x0F) * 2 + 1;
            level += rate < 31 ? 0x20 : 0x400;
        } else {
            level -= 1;
            level -= level >> 8;
            rate = v->phase == MIXER_DECAY ? (v->decay & 7) * 2 + 16 : v->sustain_rate & 0x1F;
        }
    } else if (v->gain_mode == AU_GAIN_DIRECT) {
        level = (v->gain_value & 0x7F) * 0x10;
        rate = 31;
    } else {
        rate = v->gain_value & 0x1F;
        switch (v->gain_mode) {
        case AU_GAIN_DECREASE_LINEAR: level -= 0x20; break;
        case AU_GAIN_DECREASE_EXPONENTIAL: level -= 1; level -= level >> 8; break;
        case AU_GAIN_INCREASE_LINEAR: level += 0x20; break;
        default:   /* bent: slower over three quarters */
            level += v->level_unclipped < 0 || v->level_unclipped >= 0x600 ? 0x08 : 0x20;
            break;
        }
    }

    /* the decay ends, and the attack, without waiting for the rate */
    if (v->phase == MIXER_DECAY && (level >> 8) == decay_end(v))
        v->phase = MIXER_SUSTAIN;
    v->level_unclipped = (int16_t)level;
    if (level < 0 || level > LEVEL_MAX) {
        level = level < 0 ? 0 : LEVEL_MAX;
        if (v->phase == MIXER_ATTACK)
            v->phase = MIXER_DECAY;
    }
    if (rate_steps(m, (uint8_t)rate))
        v->level = (int16_t)level;
}

/* ---- A frame ---------------------------------------------------------------------------- */

/* The voice's part of a frame: its output (0 while it is silent), with
 * `below` the output of the voice under it, which modulates its pitch. */
static int play_voice(Mixer *m, uint8_t n, int below)
{
    MixerVoice *v = &m->voice[n];
    const AuVoiceSet bit = (AuVoiceSet)(1 << n);
    uint8_t header = m->memory[v->block];

    int pitch = v->pitch & 0x3FFF;
    if (m->modulated_voices & bit & 0xFE)
        pitch += ((below >> 5) * pitch) >> 10;

    /* A note's start: back to the sample's first block, and the frames it
     * takes to decode three lots of four, silent and still. */
    if (v->starting) {
        if (v->starting == START_FRAMES) {
            v->block = sample_start(m, v->sample);
            v->block_byte = 1;
            v->decoded_at = 0;
            header = 0;
        }
        v->level = 0;
        v->level_unclipped = 0;
        v->starting--;
        v->position = (v->starting & 3) ? 0x4000 : 0;
        pitch = 0;
    }

    int out = 0;
    if (v->level) {
        int s = (m->noise_voices & bit) ? wrap16(m->noise * 2) : interpolate(v);
        out = ((s * v->level) >> 11) & ~1;
    }

    /* A sample's end without a loop silences it at once, as does the reset. */
    if (m->resetting || (header & 3) == NO_LOOP) {
        v->phase = MIXER_RELEASE;
        v->level = 0;
    }
    if (m->polling) {
        if (m->keys_off & bit)
            v->phase = MIXER_RELEASE;
        if (m->keys_on & bit) {
            v->starting = START_FRAMES;
            v->phase = MIXER_ATTACK;
        }
    }
    if (!v->starting)
        run_envelope(m, v);

    if (v->position >= 0x4000) {
        decode_four(m, v, header);
        v->block_byte += 2;
        if (v->block_byte >= BLOCK_BYTES) {
            v->block = (uint16_t)(v->block + BLOCK_BYTES);
            if (header & LAST_BLOCK)
                v->block = sample_loop(m, v->sample);
            v->block_byte = 1;
        }
    }
    int position = (v->position & 0x3FFF) + pitch;
    v->position = (uint16_t)(position > 0x7FFF ? 0x7FFF : position);
    v->output = (int16_t)out;
    return out;
}

/* The echo: the frame its buffer gives back, through the filter's eight taps
 * over the last eight frames, heard at the echo's volume (into `heard`), and
 * what goes back into the buffer in its place, the voices sent to it with
 * the filtered echo fed back. The buffer's place and size are taken up as the
 * echo comes round to them: a new size when it wraps, a new place the next
 * frame, its writes on or off a frame late on the left. */
static void run_echo(Mixer *m, const int sent[2], int heard[2])
{
    const uint16_t at = (uint16_t)((m->echo_page_now << 8) + m->echo_offset);
    m->echo_newest = (uint8_t)((m->echo_newest + 1) & 7);
    int16_t *newest = m->echo_history[m->echo_newest];
    newest[0] = (int16_t)((int16_t)memory16(m, at) >> 1);
    newest[1] = (int16_t)((int16_t)memory16(m, (uint16_t)(at + 2)) >> 1);

    int back[2];
    for (int ch = 0; ch < 2; ch++) {
        /* the first tap weighs the oldest frame; the sum wraps before the last */
        int sum = 0;
        for (int t = 0; t < 7; t++)
            sum += (m->echo_history[(m->echo_newest + 1 + t) & 7][ch] * m->taps[t]) >> 6;
        sum = wrap16(sum) + wrap16((newest[ch] * m->taps[7]) >> 6);
        const int filtered = clamp16(sum) & ~1;
        heard[ch] = wrap16((filtered * m->echo_volume[ch]) >> 7);
        back[ch] = clamp16(sent[ch] + wrap16((filtered * m->echo_feedback) >> 7)) & ~1;
    }

    m->echo_page_now = m->echo_page;
    if (m->echo_offset == 0)
        m->echo_length = (uint16_t)((m->echo_delay & 0x0F) * ECHO_UNIT_BYTES);
    m->echo_offset = (uint16_t)(m->echo_offset + ECHO_FRAME_BYTES);
    if (m->echo_offset >= m->echo_length)
        m->echo_offset = 0;
    for (int ch = 0; ch < 2; ch++) {
        if (ch == 1)
            m->echo_writes_now = m->echo_writes;
        if (m->echo_writes_now) {
            uint16_t to = (uint16_t)(at + 2 * ch);
            m->memory[to] = (uint8_t)back[ch];
            m->memory[(uint16_t)(to + 1)] = (uint8_t)(back[ch] >> 8);
        }
    }
}

void mixer_frame(Mixer *m, int16_t frame[2])
{
    int main_mix[2] = { 0, 0 }, sent[2] = { 0, 0 };
    int below = 0;
    for (uint8_t n = 0; n < AU_VOICES; n++) {
        int out = play_voice(m, n, below);
        for (int ch = 0; ch < 2; ch++) {
            int heard = (out * m->voice[n].volume[ch]) >> 7;
            main_mix[ch] = clamp16(main_mix[ch] + heard);
            if (m->echo_voices & (1 << n))
                sent[ch] = clamp16(sent[ch] + heard);
        }
        below = out;
    }

    int echo[2];
    run_echo(m, sent, echo);
    for (int ch = 0; ch < 2; ch++)
        frame[ch] = (int16_t)(m->silenced ? 0 : clamp16(wrap16((main_mix[ch] * m->master[ch]) >> 7) + echo[ch]));

    /* Key ons and offs are taken every other frame, a key on once. */
    m->polling ^= 1;
    if (m->polling) {
        m->keys_asked &= (AuVoiceSet)~m->keys_on;
        m->keys_on = m->keys_asked;
        m->keys_off = m->keys_released;
    }
    m->rate_counter = (uint16_t)(m->rate_counter ? m->rate_counter - 1 : RATE_CLOCK - 1);
    if (rate_steps(m, m->noise_clock))
        m->noise = (uint16_t)((m->noise >> 1) | ((m->noise ^ (m->noise >> 1)) & 1) << 14);
}

void mixer_run(Mixer *m, unsigned frames)
{
    while (frames--) {
        int16_t frame[2];
        mixer_frame(m, frame);
        if (m->out_count == MIXER_OUT_FRAMES) {
            m->out_dropped++;
            continue;
        }
        m->out[m->out_count * 2] = frame[0];
        m->out[m->out_count * 2 + 1] = frame[1];
        m->out_count++;
    }
}

size_t mixer_take(Mixer *m, int16_t *out, size_t max_frames)
{
    size_t n = m->out_count < max_frames ? m->out_count : max_frames;
    if (!n)
        return 0;
    memcpy(out, m->out, n * 2 * sizeof *out);
    m->out_count -= (uint32_t)n;
    if (m->out_count)
        memmove(m->out, m->out + n * 2, m->out_count * 2 * sizeof *m->out);
    return n;
}

uint8_t mixer_echo_delay(const Mixer *m) { return m->echo_delay & 0x0F; }

bool mixer_echo_writes_over(const Mixer *m, uint16_t at)
{
    if (!m->echo_writes)
        return false;
    uint16_t now = (uint16_t)(m->echo_page_now << 8), next = (uint16_t)(m->echo_page << 8);
    uint32_t now_length = m->echo_length ? m->echo_length : ECHO_FRAME_BYTES;
    uint32_t next_length = mixer_echo_delay(m) ? mixer_echo_delay(m) * (uint32_t)ECHO_UNIT_BYTES : ECHO_FRAME_BYTES;
    return (uint16_t)(at - now) < now_length || (uint16_t)(at - next) < next_length;
}

/* ---- The voice calls -------------------------------------------------------------------- */

static Mixer *mixer(AuVoices *v) { return (Mixer *)v; }
static MixerVoice *voice(AuVoices *v, uint8_t n) { return &mixer(v)->voice[n & 7]; }

static void put(Mixer *m, uint16_t at, uint8_t byte)
{
    if (m->watch)
        m->watch(m->watch_ctx, at);
    m->memory[at] = byte;
}

static void start(AuVoices *v) { (void)v; }
static void set_sample(AuVoices *v, uint8_t n, uint8_t sample) { voice(v, n)->sample = sample; }

static void set_envelope(AuVoices *v, uint8_t n, AuEnvelope e)
{
    MixerVoice *mv = voice(v, n);
    mv->adsr = e.adsr;
    mv->attack = e.attack & 0x0F;
    mv->decay = e.decay & 7;
    mv->sustain_level = e.sustain_level & 7;
    mv->sustain_rate = e.sustain_rate & 0x1F;
}

static void set_gain(AuVoices *v, uint8_t n, AuGain g)
{
    voice(v, n)->gain_mode = (uint8_t)g.mode;
    voice(v, n)->gain_value = g.mode == AU_GAIN_DIRECT ? (uint8_t)(g.value & 0x7F) : (uint8_t)(g.value & 0x1F);
}

static void set_pitch(AuVoices *v, uint8_t n, uint16_t pitch) { voice(v, n)->pitch = pitch; }

static void set_volume(AuVoices *v, uint8_t n, int8_t left, int8_t right)
{
    voice(v, n)->volume[0] = left;
    voice(v, n)->volume[1] = right;
}

static void key_on(AuVoices *v, AuVoiceSet voices) { mixer(v)->keys_asked = voices; }
static void key_off(AuVoices *v, AuVoiceSet voices) { mixer(v)->keys_released = voices; }

static void hold(AuVoices *v, bool held)
{
    for (uint8_t n = 0; n < AU_VOICES; n++)
        voice(v, n)->adsr = !held;
}

static int8_t output(AuVoices *v, uint8_t n) { return (int8_t)(voice(v, n)->output >> 8); }

static void master_volume(AuVoices *v, int8_t left, int8_t right)
{
    mixer(v)->master[0] = left;
    mixer(v)->master[1] = right;
}

static void noise_clock(AuVoices *v, uint8_t clock) { mixer(v)->noise_clock = clock & 0x1F; }

static void output_stopped(AuVoices *v, bool stopped)
{
    mixer(v)->silenced = stopped;
    mixer(v)->resetting = stopped;
}

static void noise_voices(AuVoices *v, AuVoiceSet voices) { mixer(v)->noise_voices = voices; }
static void pitch_modulation(AuVoices *v, AuVoiceSet voices) { mixer(v)->modulated_voices = voices; }
static void echo_voices(AuVoices *v, AuVoiceSet voices) { mixer(v)->echo_voices = voices; }

static void echo_volume(AuVoices *v, int8_t left, int8_t right)
{
    mixer(v)->echo_volume[0] = left;
    mixer(v)->echo_volume[1] = right;
}

static void echo_feedback(AuVoices *v, int8_t feedback) { mixer(v)->echo_feedback = feedback; }
static void echo_filter(AuVoices *v, const int8_t taps[8]) { memcpy(mixer(v)->taps, taps, sizeof mixer(v)->taps); }

static void echo_delay(AuVoices *v, uint8_t delay)
{
    mixer(v)->echo_delay = delay;
    mixer(v)->echo_page = voices_echo_page(delay);
}

static void echo_writes(AuVoices *v, bool on) { mixer(v)->echo_writes = on; }

static void place(Mixer *m, uint8_t sample, uint16_t start_at, uint16_t loop)
{
    uint16_t at = (uint16_t)(VOICES_DIRECTORY + 4 * sample);
    put(m, at, (uint8_t)start_at);
    put(m, (uint16_t)(at + 1), (uint8_t)(start_at >> 8));
    put(m, (uint16_t)(at + 2), (uint8_t)loop);
    put(m, (uint16_t)(at + 3), (uint8_t)(loop >> 8));
}

static void sample_define(AuVoices *v, uint8_t sample, const uint8_t *brr, uint16_t length, uint16_t loop)
{
    for (uint16_t i = 0; i < length && i < VOICES_OWN_SAMPLE_BYTES; i++)
        put(mixer(v), (uint16_t)(VOICES_OWN_SAMPLE + i), brr[i]);
    place(mixer(v), sample, VOICES_OWN_SAMPLE, (uint16_t)(VOICES_OWN_SAMPLE + loop));
}

static void sample_write(AuVoices *v, uint8_t sample, uint16_t at, uint8_t byte)
{
    (void)sample;
    if (at < VOICES_OWN_SAMPLE_BYTES)
        put(mixer(v), (uint16_t)(VOICES_OWN_SAMPLE + at), byte);
}

static void sample_memory_write(AuVoices *v, uint16_t at, uint8_t byte) { put(mixer(v), at, byte); }

/* Whole pages up from the first, then the rest down from its last byte, as
 * the original's receiver moves them: which order decides what an overlapping
 * move leaves. */
static void sample_memory_move(AuVoices *v, uint16_t to, uint16_t from, uint16_t length)
{
    Mixer *m = mixer(v);
    for (uint8_t pages = (uint8_t)(length >> 8); pages; pages--) {
        for (uint16_t i = 0; i < 0x100; i++)
            put(m, (uint16_t)(to + i), m->memory[(uint16_t)(from + i)]);
        from = (uint16_t)(from + 0x100);
        to = (uint16_t)(to + 0x100);
    }
    for (uint8_t i = (uint8_t)length; i; i--)
        put(m, (uint16_t)(to + i - 1), m->memory[(uint16_t)(from + i - 1)]);
}

static void sample_place(AuVoices *v, uint8_t sample, uint16_t start_at, uint16_t loop)
{
    place(mixer(v), sample, start_at, loop);
}

/* The SNES escape: an S-DSP register's meaning, set as the call that means it.
 * The registers the S-DSP reports through (the envelopes, the outputs, the
 * ends) take nothing, and nor does the directory's page: this mixer's
 * directory is a table, and the game never moves it. */
enum {
    REG_VOLUME_LEFT, REG_VOLUME_RIGHT, REG_PITCH_LOW, REG_PITCH_HIGH, REG_SAMPLE, REG_ADSR1, REG_ADSR2, REG_GAIN,
    REG_TAP = 0x0F,
    REG_MASTER_LEFT = 0x0C, REG_MASTER_RIGHT = 0x1C, REG_ECHO_LEFT = 0x2C, REG_ECHO_RIGHT = 0x3C,
    REG_KEY_ON = 0x4C, REG_KEY_OFF = 0x5C, REG_FLAGS = 0x6C,
    REG_FEEDBACK = 0x0D, REG_MODULATION = 0x2D, REG_NOISE = 0x3D, REG_ECHO_VOICES = 0x4D,
    REG_ECHO_PAGE = 0x6D, REG_ECHO_DELAY = 0x7D
};

static void escape_register(AuVoices *v, uint8_t reg, uint8_t value)
{
    Mixer *m = mixer(v);
    MixerVoice *mv = voice(v, (uint8_t)(reg >> 4));
    reg &= 0x7F;
    switch (reg & 0x0F) {
    case REG_VOLUME_LEFT: mv->volume[0] = (int8_t)value; return;
    case REG_VOLUME_RIGHT: mv->volume[1] = (int8_t)value; return;
    case REG_PITCH_LOW: mv->pitch = (uint16_t)((mv->pitch & 0xFF00) | value); return;
    case REG_PITCH_HIGH: mv->pitch = (uint16_t)((mv->pitch & 0x00FF) | value << 8); return;
    case REG_SAMPLE: mv->sample = value; return;
    case REG_ADSR1:
        mv->adsr = value >> 7;
        mv->decay = (value >> 4) & 7;
        mv->attack = value & 0x0F;
        return;
    case REG_ADSR2:
        mv->sustain_level = value >> 5;
        mv->sustain_rate = value & 0x1F;
        return;
    case REG_GAIN:
        if (value & 0x80)
            set_gain(v, (uint8_t)(reg >> 4), (AuGain){ (AuGainMode)(AU_GAIN_DECREASE_LINEAR + ((value >> 5) & 3)), value });
        else
            set_gain(v, (uint8_t)(reg >> 4), (AuGain){ AU_GAIN_DIRECT, value });
        return;
    case REG_TAP: m->taps[reg >> 4] = (int8_t)value; return;
    }
    switch (reg) {
    case REG_MASTER_LEFT: m->master[0] = (int8_t)value; break;
    case REG_MASTER_RIGHT: m->master[1] = (int8_t)value; break;
    case REG_ECHO_LEFT: m->echo_volume[0] = (int8_t)value; break;
    case REG_ECHO_RIGHT: m->echo_volume[1] = (int8_t)value; break;
    case REG_KEY_ON: m->keys_asked = value; break;
    case REG_KEY_OFF: m->keys_released = value; break;
    case REG_FLAGS:
        m->resetting = value >> 7;
        m->silenced = (value >> 6) & 1;
        m->echo_writes = !(value & 0x20);
        m->noise_clock = value & 0x1F;
        break;
    case REG_FEEDBACK: m->echo_feedback = (int8_t)value; break;
    case REG_MODULATION: m->modulated_voices = value; break;
    case REG_NOISE: m->noise_voices = value; break;
    case REG_ECHO_VOICES: m->echo_voices = value; break;
    case REG_ECHO_PAGE: m->echo_page = value; break;
    case REG_ECHO_DELAY: m->echo_delay = value; break;
    }
}

static const AuVoicesOps mixer_ops = {
    start, set_sample, set_envelope, set_gain, set_pitch, set_volume, key_on, key_off, hold, output,
    master_volume, noise_clock, output_stopped, noise_voices, pitch_modulation, echo_voices, echo_volume,
    echo_feedback, echo_filter, echo_delay, echo_writes, sample_define, sample_write, sample_memory_write,
    sample_memory_move, sample_place, escape_register
};

AuVoices *mixer_voices(Mixer *m)
{
    m->voices.ops = &mixer_ops;
    return &m->voices;
}

void mixer_init(Mixer *m)
{
    memset(m, 0, sizeof *m);
    m->voices.ops = &mixer_ops;
    m->silenced = 1;
    m->resetting = 1;
    m->polling = 1;
    m->noise = 0x4000;
    for (int n = 0; n < AU_VOICES; n++)
        m->voice[n].block_byte = 1;
}

/* ---- Saved states ----------------------------------------------------------------------- */

#define FIELD(name, x) fn(ctx, (name), &(x), sizeof(x), 1)
#define ARRAY(name, a) fn(ctx, (name), (a), sizeof((a)[0]), sizeof(a) / sizeof((a)[0]))

void mixer_fields(Mixer *m, MixerField fn, void *ctx)
{
    char name[48];
    for (int n = 0; n < AU_VOICES; n++) {
        MixerVoice *v = &m->voice[n];
#define VFIELD(f) (snprintf(name, sizeof name, "voice[%d]." #f, n), FIELD(name, v->f))
#define VARRAY(f) (snprintf(name, sizeof name, "voice[%d]." #f, n), ARRAY(name, v->f))
        VFIELD(sample);
        VFIELD(pitch);
        VARRAY(volume);
        VFIELD(adsr);
        VFIELD(attack);
        VFIELD(decay);
        VFIELD(sustain_level);
        VFIELD(sustain_rate);
        VFIELD(gain_mode);
        VFIELD(gain_value);
        VFIELD(starting);
        VFIELD(phase);
        VFIELD(level);
        VFIELD(level_unclipped);
        VFIELD(block);
        VFIELD(block_byte);
        VFIELD(decoded_at);
        VARRAY(decoded);
        VFIELD(position);
        VFIELD(output);
#undef VFIELD
#undef VARRAY
    }
    ARRAY("master", m->master);
    ARRAY("echo_volume", m->echo_volume);
    FIELD("echo_feedback", m->echo_feedback);
    ARRAY("taps", m->taps);
    FIELD("noise_clock", m->noise_clock);
    FIELD("noise_voices", m->noise_voices);
    FIELD("modulated_voices", m->modulated_voices);
    FIELD("echo_voices", m->echo_voices);
    FIELD("silenced", m->silenced);
    FIELD("resetting", m->resetting);
    FIELD("echo_writes", m->echo_writes);
    FIELD("echo_delay", m->echo_delay);
    FIELD("echo_page", m->echo_page);
    FIELD("keys_asked", m->keys_asked);
    FIELD("keys_on", m->keys_on);
    FIELD("keys_released", m->keys_released);
    FIELD("keys_off", m->keys_off);
    FIELD("polling", m->polling);
    FIELD("rate_counter", m->rate_counter);
    FIELD("noise", m->noise);
    FIELD("echo_page_now", m->echo_page_now);
    FIELD("echo_writes_now", m->echo_writes_now);
    FIELD("echo_offset", m->echo_offset);
    FIELD("echo_length", m->echo_length);
    FIELD("echo_newest", m->echo_newest);
    fn(ctx, "echo_history", &m->echo_history[0][0], sizeof m->echo_history[0][0], 16);
    ARRAY("memory", m->memory);
}
