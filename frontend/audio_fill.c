#include "audio_fill.h"

#include <string.h>

/* How far the resampling ratio may move to hold the target fill. */
#define AUDIO_MAX_ADJUST 0.005

static void lock(AudioFill *a, bool locked)
{
    if (a->lock)
        a->lock(a->lock_ctx, locked);
}

void audio_fill_init(AudioFill *a, int rate, int buffer_frames, void (*lock_fn)(void *ctx, bool locked),
                     void *lock_ctx)
{
    memset(a, 0, sizeof *a);
    a->rate = rate;
    a->lock = lock_fn;
    a->lock_ctx = lock_ctx;
    /* Two device buffers plus about two video frames. */
    a->target = (size_t)buffer_frames * 2 + (size_t)rate / 30;
    if (a->target > AUDIO_RING_FRAMES / 4)
        a->target = AUDIO_RING_FRAMES / 4;
    a->fill_avg = (double)a->target;
    a->pos = 1.0;
}

void audio_fill_drain(AudioFill *a, int16_t *out, size_t frames)
{
    size_t n = frames < a->count ? frames : a->count;
    size_t tail = (a->head + AUDIO_RING_FRAMES - a->count) % AUDIO_RING_FRAMES;
    for (size_t i = 0; i < n; i++) {
        size_t at = (tail + i) % AUDIO_RING_FRAMES;
        out[i * 2] = a->ring[at * 2];
        out[i * 2 + 1] = a->ring[at * 2 + 1];
    }
    a->count -= n;
    memset(out + n * 2, 0, (frames - n) * 4);   /* underrun: silence */
}

static void ring_clear(AudioFill *a)
{
    lock(a, true);
    a->count = 0;
    lock(a, false);
    a->in_count = 0;
    a->pos = 1.0;
    a->fill_avg = (double)a->target;
}

bool audio_fill_frame(AudioFill *a, Player *pl, bool paused)
{
    if (paused) {
        player_discard_audio(pl);
        a->playing = false;
        ring_clear(a);
        return false;
    }

    a->in_count += player_take_audio(pl, a->in + a->in_count * 2, sizeof a->in / sizeof a->in[0] / 2 - a->in_count);

    lock(a, true);
    size_t fill = a->count;
    lock(a, false);
    a->fill_avg += (fill - a->fill_avg) * 0.05;
    double err = (a->fill_avg - (double)a->target) / (double)a->target;
    err = err < -1 ? -1 : err > 1 ? 1 : err;
    double step = (double)DSP_SAMPLE_RATE / a->rate * (1.0 + AUDIO_MAX_ADJUST * err);

    /* Cubic Hermite over in[i-1..i+2] at pos = i + f. */
    int16_t out[4096 * 2];
    size_t n = 0;
    size_t i;
    while ((i = (size_t)a->pos) + 2 < a->in_count && n < 4096) {
        double f = a->pos - (double)i;
        for (int ch = 0; ch < 2; ch++) {
            double y0 = a->in[(i - 1) * 2 + ch], y1 = a->in[i * 2 + ch];
            double y2 = a->in[(i + 1) * 2 + ch], y3 = a->in[(i + 2) * 2 + ch];
            double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3;
            double c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
            double v = ((c3 * f + c2) * f + c1) * f + y1;
            out[n * 2 + ch] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        n++;
        a->pos += step;
    }
    /* Keep in[i-1] onward. */
    i = (size_t)a->pos;
    size_t drop = i > 1 ? i - 1 : 0;
    if (drop > a->in_count)
        drop = a->in_count;
    memmove(a->in, a->in + drop * 2, (a->in_count - drop) * 2 * sizeof a->in[0]);
    a->in_count -= drop;
    a->pos -= (double)drop;

    lock(a, true);
    /* Past four times the target (a device far slower than its rate), drop
     * rather than let latency grow. */
    for (size_t k = 0; k < n && a->count < 4 * a->target; k++) {
        a->ring[a->head * 2] = out[k * 2];
        a->ring[a->head * 2 + 1] = out[k * 2 + 1];
        a->head = (a->head + 1) % AUDIO_RING_FRAMES;
        a->count++;
    }
    fill = a->count;
    lock(a, false);

    if (!a->playing && fill >= a->target)   /* start once there's a cushion */
        a->playing = true;
    return a->playing;
}
