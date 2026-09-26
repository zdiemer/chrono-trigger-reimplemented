#include "sound_audio.h"

#include <stdlib.h>
#include <string.h>

#include "apu/dsp.h"
#include "media.h"

enum {
    WRITES = 8192,          /* a tick writes about a hundred; a song's start a few hundred */
    RING = 1 << 16,         /* the original's samples kept, about two seconds */
    LAG_LIMIT = 4096,       /* samples the second S-DSP may fall behind when no tick comes (an upload) */
    LAG_KEPT = 1024,
    ECHO_UNIT = 0x800
};

typedef struct {
    uint64_t at;            /* the sample it lands before */
    uint8_t reg, value;
    bool matched;           /* EXACT: placed at the original's same write */
} Write;

typedef struct {
    int16_t frame[2];
    uint8_t envx[8], outx[8];
    bool seen;
} Sample;

struct SoundAudio {
    SoundAudioTiming timing;
    bool started;
    Dsp dsp;
    uint8_t ram[0x10000];
    uint8_t echo[0x10000];

    Write writes[WRITES];           /* the C driver's not landed yet, in order of `at` */
    unsigned write_count;
    Write originals[WRITES];        /* EXACT: the original's this tick, `at` its sample */
    unsigned original_count;
    unsigned long long writes_lost, writes_matched, writes_unmatched;

    Sample *ring;
    uint64_t seen;                  /* the original's samples taken so far */
    uint64_t tick_start;
    bool in_tick;

    uint64_t compared, unseen, output_differs, first_output_difference;
    uint64_t envelope_differs[8], level_differs[8], first_voice_difference[8];
    int max_difference;
    double sum_original, sum_difference;

    ct_wav c_wav, original_wav;
    bool wavs;
};

SoundAudio *sound_audio_create(SoundAudioTiming timing)
{
    SoundAudio *a = calloc(1, sizeof *a);
    if (!a)
        return NULL;
    a->ring = calloc(RING, sizeof *a->ring);
    if (!a->ring) {
        free(a);
        return NULL;
    }
    a->timing = timing;
    a->first_output_difference = UINT64_MAX;
    for (int v = 0; v < 8; v++)
        a->first_voice_difference[v] = UINT64_MAX;
    return a;
}

void sound_audio_destroy(SoundAudio *a)
{
    if (!a)
        return;
    if (a->wavs) {
        ct_wav_close(&a->c_wav);
        ct_wav_close(&a->original_wav);
    }
    free(a->ring);
    free(a);
}

bool sound_audio_wav(SoundAudio *a, const char *c_path, const char *original_path)
{
    if (ct_wav_open(&a->c_wav, c_path, DSP_SAMPLE_RATE) != 0)
        return false;
    if (ct_wav_open(&a->original_wav, original_path, DSP_SAMPLE_RATE) != 0) {
        ct_wav_close(&a->c_wav);
        return false;
    }
    a->wavs = true;
    return true;
}

void sound_audio_boot(SoundAudio *a, const Apu *apu)
{
    if (a->started)
        return;
    a->dsp = apu->dsp;
    a->dsp.out_count = 0;
    memcpy(a->ram, apu->ram, sizeof a->ram);
    a->seen = apu->dsp.samples;
    a->tick_start = apu->dsp.samples;
    a->started = true;
}

/* ---- The second S-DSP ---------------------------------------------------------------------- */

/* The echo buffer the second S-DSP writes: the one it is writing and the
 * one its registers name, which differ while the delay changes. */
static void echo_range(const SoundAudio *a, int which, uint16_t *start, uint32_t *length)
{
    if (which == 0) {
        *start = (uint16_t)(a->dsp.echo_esa << 8);
        *length = a->dsp.echo_length ? a->dsp.echo_length : 4;
    } else {
        *start = (uint16_t)(a->dsp.regs[DSP_ESA] << 8);
        uint32_t units = a->dsp.regs[DSP_EDL] & 0x0F;
        *length = units ? units * ECHO_UNIT : 4;
    }
}

static void copy_echo(uint8_t *to, const uint8_t *from, const SoundAudio *a)
{
    for (int which = 0; which < 2; which++) {
        uint16_t start;
        uint32_t length;
        echo_range(a, which, &start, &length);
        for (uint32_t i = 0; i < length; i++)
            to[(uint16_t)(start + i)] = from[(uint16_t)(start + i)];
    }
}

/* Sound memory as the original's is now, but for the echo buffer, which each
 * S-DSP keeps for itself. */
static void take_memory(SoundAudio *a, const Apu *apu)
{
    copy_echo(a->echo, a->ram, a);
    memcpy(a->ram, apu->ram, sizeof a->ram);
    copy_echo(a->ram, a->echo, a);
}

static void compare(SoundAudio *a, uint64_t index, const int16_t frame[2])
{
    const Sample *o = &a->ring[index % RING];
    if (a->wavs) {
        static const int16_t silence[2] = { 0, 0 };
        ct_wav_write(&a->c_wav, frame, 1);
        ct_wav_write(&a->original_wav, o->seen ? o->frame : silence, 1);
    }
    if (!o->seen) {
        a->unseen++;
        return;
    }
    a->compared++;
    for (int ch = 0; ch < 2; ch++) {
        int d = frame[ch] - o->frame[ch];
        if (d < 0)
            d = -d;
        if (d > a->max_difference)
            a->max_difference = d;
        a->sum_difference += (double)d * d;
        a->sum_original += (double)o->frame[ch] * o->frame[ch];
    }
    if (frame[0] != o->frame[0] || frame[1] != o->frame[1]) {
        a->output_differs++;
        if (a->first_output_difference == UINT64_MAX)
            a->first_output_difference = index;
    }
    for (int v = 0; v < 8; v++) {
        bool envelope = a->dsp.regs[v << 4 | DSP_ENVX] != o->envx[v];
        bool level = a->dsp.regs[v << 4 | DSP_OUTX] != o->outx[v];
        a->envelope_differs[v] += envelope;
        a->level_differs[v] += level;
        if ((envelope || level) && a->first_voice_difference[v] == UINT64_MAX)
            a->first_voice_difference[v] = index;
    }
}

/* The second S-DSP up to sample `to`, each write landing before the sample
 * it is placed at. */
static void advance(SoundAudio *a, uint64_t to)
{
    while (a->dsp.samples < to) {
        unsigned due = 0;
        while (due < a->write_count && a->writes[due].at <= a->dsp.samples) {
            dsp_write(&a->dsp, a->writes[due].reg, a->writes[due].value);
            due++;
        }
        if (due) {
            a->write_count -= due;
            memmove(a->writes, a->writes + due, a->write_count * sizeof *a->writes);
        }
        uint64_t index = a->dsp.samples;
        dsp_sample(&a->dsp, a->ram);
        int16_t frame[2];
        dsp_take_samples(&a->dsp, frame, 1);
        compare(a, index, frame);
    }
}

void sound_audio_watch(SoundAudio *a, Apu *apu)
{
    if (!a->started)
        return;
    uint64_t now = apu->dsp.samples;
    if (now > a->seen) {
        /* An instruction makes a sample at most: the newest is the one just
         * made. Taking it empties the buffer, so none is missed. */
        for (uint64_t k = a->seen; k + 1 < now; k++)
            a->ring[k % RING].seen = false;
        Sample *s = &a->ring[(now - 1) % RING];
        s->seen = apu->dsp.out_count > 0;
        if (s->seen) {
            s->frame[0] = apu->dsp.out[(apu->dsp.out_count - 1) * 2];
            s->frame[1] = apu->dsp.out[(apu->dsp.out_count - 1) * 2 + 1];
        }
        apu->dsp.out_count = 0;
        for (int v = 0; v < 8; v++) {
            s->envx[v] = apu->dsp.regs[v << 4 | DSP_ENVX];
            s->outx[v] = apu->dsp.regs[v << 4 | DSP_OUTX];
        }
        a->seen = now;
    }
    if (!a->in_tick && now - a->dsp.samples > LAG_LIMIT) {
        take_memory(a, apu);
        advance(a, now - LAG_KEPT);
    }
}

void sound_audio_tick(SoundAudio *a, const Apu *apu)
{
    if (!a->started)
        return;
    take_memory(a, apu);
    advance(a, a->tick_start);
    a->tick_start = apu->dsp.samples;
    a->original_count = 0;
}

void sound_audio_in_tick(SoundAudio *a, bool in_tick) { a->in_tick = in_tick; }

/* A write into the queue by its sample, after those at the same one. */
static void queue(SoundAudio *a, Write w)
{
    if (a->write_count == WRITES) {
        a->writes_lost++;
        return;
    }
    unsigned i = a->write_count;
    while (i && a->writes[i - 1].at > w.at)
        i--;
    memmove(a->writes + i + 1, a->writes + i, (a->write_count - i) * sizeof *a->writes);
    a->writes[i] = w;
    a->write_count++;
}

/* EXACT pairs each of the C driver's writes with the original's same write
 * (register and value) in the same tick, whichever came first: a tick's
 * writes come after the original's, but a command's before, at the point the
 * lockstep hands it over. A write the original didn't make stays where it
 * was made. */
void sound_audio_original_write(SoundAudio *a, const Apu *apu, uint8_t reg, uint8_t value)
{
    if (!a->started || a->timing != SOUND_AUDIO_EXACT)
        return;
    reg &= 0x7F;
    for (unsigned i = 0; i < a->write_count; i++) {
        Write w = a->writes[i];
        if (w.matched || w.reg != reg || w.value != value || w.at < a->tick_start)
            continue;
        a->write_count--;
        memmove(a->writes + i, a->writes + i + 1, (a->write_count - i) * sizeof *a->writes);
        w.at = apu->dsp.samples;
        w.matched = true;
        a->writes_matched++;
        a->writes_unmatched--;
        queue(a, w);
        return;
    }
    if (a->original_count < WRITES)
        a->originals[a->original_count++] = (Write){ apu->dsp.samples, reg, value, false };
}

void sound_audio_write(SoundAudio *a, const Apu *apu, uint8_t reg, uint8_t value)
{
    if (!a->started)
        return;
    reg &= 0x7F;
    Write w = { apu->dsp.samples, reg, value, false };
    if (a->timing == SOUND_AUDIO_HOST) {
        if (a->in_tick)
            w.at = a->tick_start;
        queue(a, w);
        return;
    }
    for (unsigned i = 0; i < a->original_count; i++) {
        Write *o = &a->originals[i];
        if (o->matched || o->reg != reg || o->value != value)
            continue;
        o->matched = true;
        w.at = o->at;
        w.matched = true;
        a->writes_matched++;
        queue(a, w);
        return;
    }
    a->writes_unmatched++;
    queue(a, w);
}

void sound_audio_report(SoundAudio *a, FILE *out)
{
    static const char *timings[] = { "exact", "host" };
    double power = a->sum_original > 0 ? 100.0 * a->sum_difference / a->sum_original : 0;
    fprintf(out, "sound audio (%s timing): %llu samples compared", timings[a->timing], (unsigned long long)a->compared);
    if (a->unseen)
        fprintf(out, " (%llu not taken)", (unsigned long long)a->unseen);
    if (a->writes_lost)
        fprintf(out, ", %llu writes lost", a->writes_lost);
    if (a->timing == SOUND_AUDIO_EXACT)
        fprintf(out, ", %llu writes at the original's sample and %llu without one", a->writes_matched,
                a->writes_unmatched);
    if (!a->output_differs) {
        fprintf(out, ": identical\n");
        return;
    }
    fprintf(out, ": %llu differ (%.2f%%), the first at %.3f s; largest difference %d, the difference's power %.3f%% of the signal's\n",
            (unsigned long long)a->output_differs, 100.0 * (double)a->output_differs / (double)(a->compared ? a->compared : 1),
            (double)a->first_output_difference / DSP_SAMPLE_RATE, a->max_difference, power);
    for (int v = 0; v < 8; v++) {
        if (a->first_voice_difference[v] == UINT64_MAX)
            continue;
        fprintf(out, "  voice %d: envelope differs at %llu samples, output level at %llu, the first at %.3f s\n", v,
                (unsigned long long)a->envelope_differs[v], (unsigned long long)a->level_differs[v],
                (double)a->first_voice_difference[v] / DSP_SAMPLE_RATE);
    }
}
