/* Unit checks for the S-DSP (runtime/apu/dsp.c): the envelope/noise rate
 * counter, BRR decoding, and the key-on, envelope, ENDX and echo timing of a
 * voice. Prints failures and exits nonzero if any. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apu/dsp.h"

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

int main(void)
{
    test_rates();
    test_brr();
    test_voice();
    test_echo();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("dsp_test: all checks passed\n");
    return 0;
}
