/* Unit checks for the S-DSP (runtime/apu/dsp.c): the envelope/noise rate
 * counter, BRR decoding, and the key-on, envelope, ENDX and echo timing of a
 * voice; and the native mixer (runtime/platform/native/mixer.c) against it,
 * frame for frame. Prints failures and exits nonzero if any. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apu/dsp.h"
#include "platform/native/mixer.h"
#include "platform/voices_dsp.h"

static int failures;

#define CHECK(cond, ...)                                                                   \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            failures++;                                                                    \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                    \
            printf(__VA_ARGS__);                                                           \
            printf("\n");                                                                  \
        }                                                                                  \
    } while (0)

static void test_rates(void)
{
    static const int period[32] = {
        0, 2048, 1536, 1280, 1024, 768, 640, 512, 384, 320, 256, 192, 160, 128, 96, 80,
        64, 48, 40, 32, 24, 20, 16, 12, 10, 8, 6, 5, 4, 3, 2, 1,
    };
    for (unsigned rate = 0; rate < 32; rate++) {
        int fires = 0, last = -1, uneven = 0;
        /* The counter counts down through 30720 values; walk it the same way. */
        for (int step = 0; step < 2 * 30720; step++) {
            int32_t counter = (30720 - step % 30720) % 30720;
            if (!dsp_rate_fires(counter, rate))
                continue;
            if (step < 30720)
                fires++;
            if (last >= 0 && step - last != period[rate])
                uneven++;
            last = step;
        }
        CHECK(fires == (rate ? 30720 / period[rate] : 0), "rate %u fires %d times per cycle", rate, fires);
        CHECK(!uneven, "rate %u steps unevenly", rate);
    }
    /* Phase groups: periods 1<<n fire at counter 0, 3<<n and 5<<n are offset. */
    CHECK(dsp_rate_fires(0, 1) && dsp_rate_fires(0, 4) && dsp_rate_fires(0, 31), "1<<n rates at counter 0");
    CHECK(!dsp_rate_fires(0, 2) && dsp_rate_fires(1536 - 1040, 2), "rate 2 phase");
    CHECK(!dsp_rate_fires(0, 3) && dsp_rate_fires(1280 - 536, 3), "rate 3 phase");
}

static void test_brr(void)
{
    int16_t out[16], p1 = 0, p2 = 0;

    /* Filter 0: (nybble << shift) >> 1. */
    uint8_t block[9] = { 0xC0, 0x78, 0x10, 0xF0, 0, 0, 0, 0, 0 };
    dsp_decode_brr_block(block, out, &p1, &p2);
    CHECK(out[0] == 14336 && out[1] == -16384 && out[2] == 2048 && out[3] == 0 && out[4] == -2048,
          "filter 0 shift 12: %d %d %d %d %d", out[0], out[1], out[2], out[3], out[4]);
    CHECK(p1 == 0 && p2 == 0, "history after a block ending in zeros");

    /* Shift 0 drops the low bit; shifts 13-15 give 0 or -2048. */
    block[0] = 0x00;
    block[1] = 0x1F;
    p1 = p2 = 0;
    dsp_decode_brr_block(block, out, &p1, &p2);
    CHECK(out[0] == 0 && out[1] == -1, "shift 0: %d %d", out[0], out[1]);
    block[0] = 0xF0;
    block[1] = 0x7F;
    dsp_decode_brr_block(block, out, &p1, &p2);
    CHECK(out[0] == 0 && out[1] == -2048, "shift 15: %d %d", out[0], out[1]);

    /* Filters, with nybble 0 so only the prediction remains. */
    memset(block + 1, 0, 8);
    block[0] = 0x04;   /* filter 1: p1 * 15/16 */
    p1 = 1000;
    p2 = 0;
    dsp_decode_brr_block(block, out, &p1, &p2);
    CHECK(out[0] == 1000 + (-1000 >> 4), "filter 1: %d", out[0]);
    block[0] = 0x08;   /* filter 2: p1 * 61/32 - p2 * 15/16 */
    p1 = 1000;
    p2 = 500;
    dsp_decode_brr_block(block, out, &p1, &p2);
    CHECK(out[0] == 2000 + (-3000 >> 5) - 500 + (500 >> 4), "filter 2: %d", out[0]);
    CHECK(out[1] == 2 * out[0] + ((-3 * out[0]) >> 5) - 1000 + (1000 >> 4), "filter 2 second sample: %d", out[1]);
    block[0] = 0x0C;   /* filter 3: p1 * 115/64 - p2 * 13/16 */
    p1 = -1000;
    p2 = 500;
    dsp_decode_brr_block(block, out, &p1, &p2);
    CHECK(out[0] == -2000 + (13000 >> 6) - 500 + (1500 >> 4), "filter 3: %d", out[0]);

    /* Overflow: saturate to 16 bits, then the 15-bit result loses the sign. */
    block[0] = 0xC8;
    block[1] = 0x70;
    p1 = 16000;
    p2 = 0;
    dsp_decode_brr_block(block, out, &p1, &p2);
    CHECK(out[0] == -1, "saturated +32767 wraps to -1 in 15 bits: %d", out[0]);
    block[1] = 0x80;
    p1 = -16384;
    p2 = 16383;
    dsp_decode_brr_block(block, out, &p1, &p2);
    CHECK(out[0] == 0, "saturated -32768 wraps to 0 in 15 bits: %d", out[0]);
}

/* A looping one-block sample in voice 0, keyed on at sample 0. */
static void setup_voice(Dsp *d, uint8_t *ram, uint8_t adsr0, uint8_t adsr1, uint8_t gain)
{
    dsp_init(d);
    memset(ram, 0, 0x10000);
    ram[0x0200] = 0x00;   /* directory at $0200: start $0300, loop $0300 */
    ram[0x0201] = 0x03;
    ram[0x0202] = 0x00;
    ram[0x0203] = 0x03;
    ram[0x0300] = 0xB3;   /* shift 11, filter 0, end + loop */
    memset(ram + 0x0301, 0x77, 8);
    dsp_write(d, DSP_DIR, 0x02);
    dsp_write(d, DSP_VOLL, 0x40);
    dsp_write(d, DSP_VOLR, 0x40);
    dsp_write(d, DSP_PITCHL, 0x00);
    dsp_write(d, DSP_PITCHH, 0x10);   /* one BRR sample per output sample */
    dsp_write(d, DSP_ADSR0, adsr0);
    dsp_write(d, DSP_ADSR1, adsr1);
    dsp_write(d, DSP_GAIN, gain);
    dsp_write(d, DSP_MVOLL, 0x7F);
    dsp_write(d, DSP_MVOLR, 0x7F);
    dsp_write(d, DSP_FLG, 0x20);   /* out of reset, unmuted, echo writes off, noise stopped */
    dsp_write(d, DSP_ENDX, 0);
    dsp_write(d, DSP_KON, 0x01);
}

static void test_voice(void)
{
    static Dsp d;
    static uint8_t ram[0x10000];

    /* Attack rate 15 steps +$400 every sample: $400, then clipped to $7FF. */
    setup_voice(&d, ram, 0x8F, 0xE0, 0);
    int first_env = -1, endx_at = -1, out_at = -1;
    uint8_t envs[40];
    for (int i = 0; i < 40; i++) {
        dsp_sample(&d, ram);
        envs[i] = dsp_read(&d, DSP_ENVX);
        if (first_env < 0 && envs[i])
            first_env = i;
        if (endx_at < 0 && (dsp_read(&d, DSP_ENDX) & 1))
            endx_at = i;
        if (out_at < 0 && d.out[i * 2] != 0)
            out_at = i;
    }
    /* KON is polled on the second sample, the voice sits out 5 samples, the first
     * envelope step lands at the end of the next, and ENVX shows it a sample later. */
    CHECK(first_env == 8, "first nonzero ENVX at sample %d", first_env);
    CHECK(first_env >= 0 && envs[first_env] == 0x40 && envs[first_env + 1] == 0x7F,
          "attack 15 levels %02X %02X", envs[first_env], envs[first_env + 1]);
    CHECK(out_at == first_env, "first output at sample %d", out_at);
    /* ENDX sets when the end block's last byte pair is decoded: three decodes
     * during key-on, the fourth once the pitch counter has passed four samples. */
    CHECK(endx_at == first_env + 4, "ENDX set at sample %d", endx_at);
    CHECK((dsp_read(&d, DSP_OUTX) & 0x80) == 0 && dsp_read(&d, DSP_OUTX) != 0, "OUTX %02X", dsp_read(&d, DSP_OUTX));

    /* Key-off releases by 8 per sample. */
    int before = d.voice[0].env;
    dsp_write(&d, DSP_KOFF, 0x01);
    for (int i = 0; i < 4; i++)
        dsp_sample(&d, ram);
    CHECK(d.voice[0].env_mode == DSP_ENV_RELEASE, "KOFF enters release");
    CHECK(d.voice[0].env < before && (before - d.voice[0].env) % 8 == 0, "release steps of 8: %d -> %d", before,
          d.voice[0].env);

    /* Direct GAIN sets the level on the next envelope step. */
    setup_voice(&d, ram, 0x00, 0x00, 0x40);
    for (int i = 0; i < 12; i++)
        dsp_sample(&d, ram);
    CHECK(d.voice[0].env == 0x400, "direct gain $40 gives level $400: %X", d.voice[0].env);

    /* Soft reset silences and releases every voice. */
    dsp_write(&d, DSP_FLG, 0xA0);
    dsp_sample(&d, ram);
    CHECK(d.voice[0].env == 0 && d.voice[0].env_mode == DSP_ENV_RELEASE, "FLG reset");

    /* Writing ENDX clears it, whatever the value. */
    d.regs[DSP_ENDX] = 0xFF;
    dsp_write(&d, DSP_ENDX, 0x5A);
    CHECK(dsp_read(&d, DSP_ENDX) == 0, "ENDX write clears");
}

static void test_echo(void)
{
    static Dsp d;
    static uint8_t ram[0x10000];

    /* An impulse in the echo ring with FIR7 = $40 (x1/2 after the 15-bit read)
     * comes out through EVOL once per pass of a 4-byte ring (EDL 0). */
    setup_voice(&d, ram, 0x00, 0x00, 0x00);   /* voice silent */
    dsp_write(&d, DSP_ESA, 0x40);
    dsp_write(&d, DSP_EDL, 0);
    dsp_write(&d, 0x7F, 0x40);
    dsp_write(&d, DSP_EVOLL, 0x7F);
    dsp_write(&d, DSP_EVOLR, 0x7F);
    ram[0x4000] = 0x00;
    ram[0x4001] = 0x40;   /* left sample $4000 */
    dsp_sample(&d, ram);   /* ESA is sampled a sample before it's used */
    dsp_sample(&d, ram);
    /* 15-bit $2000 * $40 >> 6 = $2000; * $7F >> 7 = $1FC0. */
    CHECK(d.out[0] == 0 && d.out[2] == 0x1FC0 && d.out[3] == 0, "echo output %d %d %d", d.out[0], d.out[2],
          d.out[3]);
    CHECK(ram[0x4001] == 0x40, "echo writes disabled by FLG keep the ring");

    dsp_write(&d, DSP_FLG, 0x00);
    dsp_sample(&d, ram);   /* left write still sees the old FLG */
    dsp_sample(&d, ram);
    CHECK(ram[0x4000] == 0 && ram[0x4001] == 0, "echo write stores the (silent) mix");

    size_t before = d.out_count;
    int16_t buf[8];
    CHECK(dsp_take_samples(&d, buf, 2) == 2 && d.out_count == before - 2, "take_samples");
    CHECK(buf[0] == 0 && buf[2] == 0x1FC0, "take_samples returns oldest first");
}

/* ---- The native mixer against the S-DSP ---------------------------------------------------- */

/* The voices played through both implementations at once (a pair): the
 * S-DSP's registers (platform/voices_dsp.h) into an S-DSP over its sound
 * memory, and the native mixer (platform/native/mixer.h). Random calls of
 * every kind the driver makes, at random frames, over random BRR samples
 * placed where the echo's buffer runs over some of them; every frame of the
 * two outputs, and every voice's output level, must be the same. */
typedef struct {
    Dsp dsp;
    uint8_t ram[0x10000];
    VoicesDsp registers;
    Mixer mixer;
    AuVoicesPair both;
    uint64_t rng;
    long frame, echo_settled;   /* the echo's writes may come on again from echo_settled */
} Pair;

static uint8_t pair_read(void *ctx, uint8_t reg) { return dsp_read(&((Pair *)ctx)->dsp, reg); }
static void pair_write(void *ctx, uint8_t reg, uint8_t value) { dsp_write(&((Pair *)ctx)->dsp, reg, value); }
static void pair_memory_write(void *ctx, uint16_t at, uint8_t value) { ((Pair *)ctx)->ram[at] = value; }
static uint8_t pair_memory_read(void *ctx, uint16_t at) { return ((Pair *)ctx)->ram[at]; }

static uint32_t pair_next(Pair *p)
{
    p->rng ^= p->rng << 13;
    p->rng ^= p->rng >> 7;
    p->rng ^= p->rng << 17;
    return (uint32_t)(p->rng >> 16);
}
static uint32_t pair_below(Pair *p, uint32_t n) { return pair_next(p) % n; }

enum { SAMPLE_COUNT = 24, SAMPLE_BASE = 0x2400 };

/* A random sample of 2-40 blocks, its last one ending or looping. */
static uint16_t place_sample(Pair *p, AuVoices *v, uint8_t sample, uint16_t at)
{
    uint16_t blocks = (uint16_t)(2 + pair_below(p, 39));
    bool loops = pair_below(p, 4) != 0;
    for (uint16_t b = 0; b < blocks; b++) {
        uint8_t header = (uint8_t)(pair_below(p, 13) << 4 | pair_below(p, 4) << 2);
        if (pair_below(p, 50) == 0)
            header = (uint8_t)(0xD0 | pair_below(p, 4) << 2);   /* the ranges past 12 */
        if (b == blocks - 1)
            header |= loops ? 3 : 1;
        au_sample_memory_write(v, (uint16_t)(at + 9 * b), header);
        for (int i = 1; i < 9; i++)
            au_sample_memory_write(v, (uint16_t)(at + 9 * b + i), (uint8_t)pair_next(p));
    }
    au_sample_place(v, sample, at, (uint16_t)(at + 9 * pair_below(p, blocks)));
    return (uint16_t)(at + 9 * blocks);
}

static void random_call(Pair *p, AuVoices *v)
{
    uint8_t voice = (uint8_t)pair_below(p, 8);
    switch (pair_below(p, 24)) {
    case 0: au_voice_sample(v, voice, (uint8_t)pair_below(p, SAMPLE_COUNT + 1)); break;
    case 1: case 2: au_voice_pitch(v, voice, (uint16_t)pair_next(p)); break;
    case 3: au_voice_volume(v, voice, (int8_t)pair_next(p), (int8_t)pair_next(p)); break;
    case 4:
        au_voice_envelope(v, voice, (AuEnvelope){ pair_below(p, 4) != 0, (uint8_t)pair_below(p, 16),
                                                  (uint8_t)pair_below(p, 8), (uint8_t)pair_below(p, 8),
                                                  (uint8_t)pair_below(p, 32) });
        break;
    case 5: au_voice_gain(v, voice, (AuGain){ (AuGainMode)pair_below(p, 5), (uint8_t)pair_next(p) }); break;
    case 6: case 7: case 8: au_voices_key_on(v, (AuVoiceSet)pair_next(p)); break;
    case 9: case 10: au_voices_key_off(v, (AuVoiceSet)(pair_below(p, 2) ? 0 : pair_next(p))); break;
    case 11: au_master_volume(v, (int8_t)pair_next(p), (int8_t)pair_next(p)); break;
    case 12: au_echo_volume(v, (int8_t)pair_next(p), (int8_t)pair_next(p)); break;
    case 13: au_echo_feedback(v, (int8_t)pair_next(p)); break;
    case 14: {
        int8_t taps[8];
        for (int t = 0; t < 8; t++)
            taps[t] = (int8_t)pair_next(p);
        au_echo_filter(v, taps);
        break;
    }
    case 15:
        /* as the driver changes it: with the writes off until the old
         * buffer's time and the new one's have passed (platform/audio.h) */
        au_echo_writes(v, false);
        au_echo_delay(v, (uint8_t)pair_below(p, 16));
        p->echo_settled = p->frame + 2 * 0x7800 / 4;
        break;
    case 16: au_echo_writes(v, p->frame >= p->echo_settled && pair_below(p, 3) != 0); break;
    case 17: au_echo_voices(v, (AuVoiceSet)pair_next(p)); break;
    case 18:
        au_noise_clock(v, (uint8_t)pair_below(p, 32));
        au_noise_voices(v, (AuVoiceSet)(pair_below(p, 3) ? 0 : pair_next(p)));
        break;
    case 19: au_pitch_modulation(v, (AuVoiceSet)pair_next(p)); break;
    case 20: au_voices_hold(v, pair_below(p, 2)); break;
    case 21: au_output_stopped(v, pair_below(p, 8) == 0); break;
    case 22: au_sample_write(v, 0x10, (uint16_t)pair_below(p, 45), (uint8_t)pair_next(p)); break;
    default: {
        /* the escape, but for the registers the S-DSP reports through and
         * the directory's page, which the mixer keeps where the original
         * driver does (the game never moves it) */
        uint8_t reg;
        do
            reg = (uint8_t)pair_below(p, 0x80);
        while ((reg & 0x0F) == DSP_ENVX || (reg & 0x0F) == DSP_OUTX || reg == DSP_DIR || reg == DSP_ENDX);
        au_voices_snes_register(v, reg, (uint8_t)pair_next(p));
        break;
    }
    }
}

static void test_mixer(void)
{
    static Pair p;
    for (uint64_t seed = 1; seed <= 6; seed++) {
        memset(&p, 0, sizeof p);
        p.rng = seed * 0x9E3779B97F4A7C15ull | 1;
        dsp_init(&p.dsp);
        mixer_init(&p.mixer);
        voices_dsp_init(&p.registers, &p, pair_read, pair_write, pair_memory_write, pair_memory_read);
        au_voices_pair(&p.both, &p.registers.voices, mixer_voices(&p.mixer));
        AuVoices *v = &p.both.voices;

        au_voices_start(v);
        uint16_t at = SAMPLE_BASE;
        for (uint8_t s = 0; s < SAMPLE_COUNT; s++)
            at = place_sample(&p, v, s, at);
        /* some samples under where the echo's longest buffers lie */
        at = 0x9000;
        for (uint8_t s = SAMPLE_COUNT - 4; s < SAMPLE_COUNT; s++)
            at = place_sample(&p, v, s, at);
        static const uint8_t own[45] = { 0xB3, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0 };
        au_sample_define(v, 0x10, own, sizeof own, 0);
        au_output_stopped(v, false);

        long frames = 0, differ = 0, levels_differ = 0;
        for (int burst = 0; burst < 1500; burst++) {
            for (uint32_t calls = pair_below(&p, 6); calls; calls--)
                random_call(&p, v);
            for (uint32_t n = 1 + pair_below(&p, pair_below(&p, 8) ? 64 : 2000); n; n--, p.frame = ++frames) {
                int16_t a[2], b[2];
                dsp_sample(&p.dsp, p.ram);
                dsp_take_samples(&p.dsp, a, 1);
                mixer_frame(&p.mixer, b);
                if (a[0] != b[0] || a[1] != b[1]) {
                    if (!differ)
                        printf("  seed %llu: the first difference at frame %ld: %d %d against %d %d\n",
                               (unsigned long long)seed, frames, b[0], b[1], a[0], a[1]);
                    differ++;
                }
            }
            for (uint8_t n = 0; n < 8; n++)
                levels_differ += au_voice_output(&p.registers.voices, n) != au_voice_output(&p.mixer.voices, n);
        }
        /* the echo's writes, into the samples under it too */
        long memory_differs = 0;
        for (uint32_t a = 0; a < 0x10000; a++)
            memory_differs += p.ram[a] != p.mixer.memory[a];
        CHECK(!differ && !levels_differ && !memory_differs,
              "mixer, seed %llu: %ld of %ld frames differ from the S-DSP's, %ld voice levels, %ld bytes of memory",
              (unsigned long long)seed, differ, frames, levels_differ, memory_differs);
    }
}

int main(void)
{
    test_rates();
    test_brr();
    test_voice();
    test_echo();
    test_mixer();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("dsp_test: all checks passed\n");
    return 0;
}
