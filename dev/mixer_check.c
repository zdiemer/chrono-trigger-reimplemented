#include "mixer_check.h"

#include <stdlib.h>

#include "apu/dsp.h"

/* Frames, and their power in bands of the spectrum, a block of frames at a
 * time. */
enum { CHECK_BLOCK = 1024, CHECK_BANDS = 7 };

struct MixerCheck {
    unsigned long long compared, sounding, differing, first_difference;
    int largest;
    double power[2];                    /* the S-DSP's, and the difference's */
    double band_power[CHECK_BANDS][2];
    float block[2][CHECK_BLOCK];        /* the S-DSP's frames, and the differences, as mono */
    unsigned filled;
};

/* The power in each band of a block of frames, an octave wide from 250 Hz up
 * to the 16 kHz the output reaches and everything below 250 Hz, by a Haar
 * wavelet's split of the block: each level halves the band, and the
 * differences of a level are the power in the upper half (the bands' edges
 * are soft, which is enough to say where a difference is). */
static void band_powers(float *x, double power[CHECK_BANDS])
{
    const float root_half = 0.70710678f;
    unsigned n = CHECK_BLOCK;
    for (int band = CHECK_BANDS - 1; band > 0; band--) {
        n /= 2;
        double upper = 0;
        for (unsigned i = 0; i < n; i++) {
            float a = x[2 * i], b = x[2 * i + 1];
            float d = (a - b) * root_half;
            x[i] = (a + b) * root_half;
            upper += (double)d * d;
        }
        power[band] += upper;
    }
    for (unsigned i = 0; i < n; i++)
        power[0] += (double)x[i] * x[i];
}

static void check_block(MixerCheck *c)
{
    double power[2][CHECK_BANDS] = { { 0 } };
    for (int which = 0; which < 2; which++)
        band_powers(c->block[which], power[which]);
    for (int b = 0; b < CHECK_BANDS; b++)
        for (int which = 0; which < 2; which++)
            c->band_power[b][which] += power[which][b];
    c->filled = 0;
}

/* The S-DSP's frame and the mixer's, compared. */
static void check_frame(void *ctx, const int16_t dsp[2], const int16_t mixed[2])
{
    MixerCheck *c = ctx;
    c->compared++;
    c->sounding += dsp[0] || dsp[1];
    int largest = 0;
    for (int ch = 0; ch < 2; ch++) {
        int d = mixed[ch] - dsp[ch];
        if (d < 0)
            d = -d;
        if (d > largest)
            largest = d;
        c->power[0] += (double)dsp[ch] * dsp[ch];
        c->power[1] += (double)d * d;
    }
    if (largest) {
        if (!c->differing)
            c->first_difference = c->compared - 1;
        c->differing++;
        if (largest > c->largest)
            c->largest = largest;
    }
    c->block[0][c->filled] = (float)(dsp[0] + dsp[1]) / 2;
    c->block[1][c->filled] = (float)((mixed[0] - dsp[0]) + (mixed[1] - dsp[1])) / 2;
    if (++c->filled == CHECK_BLOCK)
        check_block(c);
}

MixerCheck *mixer_check_attach(SoundHost *h)
{
    MixerCheck *c = calloc(1, sizeof *c);
    if (c)
        sound_host_compare_mixer(h, check_frame, c);
    return c;
}

void mixer_check_free(MixerCheck *c) { free(c); }

/* `of` as a percentage of `to`. */
static double percent(double of, double to) { return to > 0 ? 100 * of / to : 0; }

bool mixer_check_report(const MixerCheck *c, FILE *out)
{
    fprintf(out, "mixer check: %llu frames compared (%llu sounding)", c->compared, c->sounding);
    if (!c->differing) {
        fprintf(out, ": identical\n");
        return true;
    }
    fprintf(out, ": %llu differ (%.3f%%), the first at %.3f s; largest difference %d; the difference's power "
                 "%.4f%% of the signal's; by band (Hz), the same:",
            c->differing, 100.0 * (double)c->differing / (double)c->compared,
            (double)c->first_difference / DSP_SAMPLE_RATE, c->largest, percent(c->power[1], c->power[0]));
    for (int b = 0; b < CHECK_BANDS; b++) {
        if (b)
            fprintf(out, " %d-%d %.4f%%", 125 << b, 250 << b, percent(c->band_power[b][1], c->band_power[b][0]));
        else
            fprintf(out, " <250 %.4f%%", percent(c->band_power[b][1], c->band_power[b][0]));
    }
    fprintf(out, "\n");
    return false;
}
