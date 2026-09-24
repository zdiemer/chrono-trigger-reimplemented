/* The C sound driver's state against the original's sound RAM
 * (dev/sound_state.h). The original's layout is written here and nowhere in
 * game/: a track's arrays are words at $F200 + $20k indexed by twice the
 * track, its vibrato and tremolo settings bytes in page 1, its loops four
 * levels of three arrays at $F560, and the song's and driver's variables in
 * the direct page (docs/SOUND.md, "Sound RAM"). */
#include "sound_state.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    char *out;
    size_t size, used;
    unsigned count;
} Report;

static void differs(Report *r, const char *what, int track, unsigned port, unsigned original)
{
    if (port == original)
        return;
    r->count++;
    if (r->used + 1 >= r->size)
        return;
    int n = track < 0 ? snprintf(r->out + r->used, r->size - r->used, " %s %X/%X;", what, port, original)
                      : snprintf(r->out + r->used, r->size - r->used, " t%d.%s %X/%X;", track, what, port, original);
    if (n > 0)
        r->used += (size_t)n < r->size - r->used ? (size_t)n : r->size - r->used - 1;
}

static unsigned word(const uint8_t *ram, unsigned at) { return ram[at & 0xFFFF] | ram[(at + 1) & 0xFFFF] << 8; }

static unsigned adsr(AuEnvelope e)
{
    return (unsigned)((e.adsr ? 0x80 : 0) | (e.decay & 7) << 4 | (e.attack & 15) | ((e.sustain_level & 7) << 5 | (e.sustain_rate & 0x1F)) << 8);
}

static void compare_wave(Report *r, int t, const char *name, const SoundWave *w, const uint8_t *ram, unsigned depth,
                         unsigned rate, unsigned delay, unsigned delay_left, unsigned base, unsigned step, unsigned phase,
                         unsigned ramp)
{
    char what[32];
#define WAVE(field, port, original)                                                                \
    do {                                                                                           \
        snprintf(what, sizeof what, "%s.%s", name, field);                                         \
        differs(r, what, t, (port), (original));                                                   \
    } while (0)
    unsigned x = 2u * (unsigned)t;
    WAVE("depth", w->depth, ram[depth + x]);
    WAVE("rate", w->rate, ram[rate + x]);
    WAVE("ticks", w->ticks_left, ram[rate + x + 1]);
    if (delay)
        WAVE("delay", w->delay, ram[delay + x]);
    WAVE("delay_left", w->delay_left, ram[delay_left + x]);
    WAVE("base", w->base_step, word(ram, base + x));
    WAVE("step", w->step, word(ram, step + x));
    WAVE("phase", w->phase, word(ram, phase + x));
    WAVE("ramp", w->ramp, ramp);
#undef WAVE
}

static void compare_track(Report *r, const SoundTrack *tr, int t, const uint8_t *ram)
{
    unsigned x = 2u * (unsigned)t;
    differs(r, "at", t, tr->at, word(ram, 0x02 + x));
    differs(r, "ticks", t, tr->ticks_left, ram[0x26 + x]);
    differs(r, "loop_level", t, tr->loop_level, (ram[0x27 + x] - 2 * x) & 3);
    for (unsigned level = 0; level < SOUND_LOOP_LEVELS; level++) {
        unsigned y = 2 * x + level;
        char what[24];
        snprintf(what, sizeof what, "loop%u.passes", level);
        if (t < SOUND_MUSIC_TRACKS)
            differs(r, what, t, tr->loops[level].passes, ram[0xF560 + y]);
        snprintf(what, sizeof what, "loop%u.counts", level);
        differs(r, what, t, tr->loops[level].counts_left, ram[0xF580 + y]);
        snprintf(what, sizeof what, "loop%u.start", level);
        differs(r, what, t, tr->loops[level].start, word(ram, 0xF5C0 + 2 * y));
    }
    differs(r, "octave", t, tr->octave, ram[0xF200 + x]);
    differs(r, "instrument", t, tr->instrument, ram[0xF201 + x]);
    differs(r, "volume_scale", t, tr->volume_scale, ram[0xF220 + x]);
    differs(r, "volume", t, tr->volume.level, word(ram, 0xF240 + x));
    differs(r, "volume.step", t, tr->volume.step, word(ram, 0xF260 + x));
    differs(r, "volume.steps", t, tr->volume.steps_left, ram[0xF2C0 + x]);
    differs(r, "pan", t, tr->pan.level, word(ram, 0xF280 + x));
    differs(r, "pan.step", t, tr->pan.step, word(ram, 0xF2A0 + x));
    differs(r, "pan.steps", t, tr->pan.steps_left, ram[0xF2C1 + x]);
    differs(r, "vibrato_depth", t, tr->vibrato_depth, word(ram, 0xF300 + x));
    differs(r, "slide_ticks", t, tr->slide_ticks, ram[0xF360 + x]);
    differs(r, "transpose", t, (uint8_t)tr->transpose, ram[0xF361 + x]);
    differs(r, "tuning", t, tr->tuning, word(ram, 0xF380 + x));
    differs(r, "detune", t, tr->detune, ram[0xF3A0 + x]);
    differs(r, "note", t, tr->note, ram[0xF3A1 + x]);
    differs(r, "slide_step", t, tr->slide_step, ram[0xF4E0 + x] | ram[0xF3C0 + x] << 8 | (unsigned)ram[0xF3C1 + x] << 16);
    differs(r, "pitch_fraction", t, tr->pitch_fraction, ram[0xF4E1 + x]);
    differs(r, "vibrato_offset", t, (uint16_t)tr->vibrato_offset, word(ram, 0xF500 + x));
    differs(r, "pitch", t, tr->pitch, word(ram, 0xF520 + x));
    differs(r, "envelope", t, adsr(tr->envelope), word(ram, 0xF540 + x));
    differs(r, "slide_semitones", t, (uint8_t)tr->slide_semitones, ram[0x0150 + x]);
    differs(r, "pan_sweep", t, tr->pan_sweep, ram[0x0171 + x]);
    differs(r, "pan_sweep.period", t, tr->pan_sweep_period, ram[0xF340 + x]);
    differs(r, "pan_sweep.ticks", t, tr->pan_sweep_ticks_left, ram[0xF341 + x]);
    differs(r, "pan_sweep.step", t, tr->pan_sweep_step, word(ram, 0xF3E0 + x));
    differs(r, "pan_sweep.phase", t, tr->pan_sweep_phase, word(ram, 0xF4C0 + x));
    unsigned ramps = ram[0xF221 + x];
    bool music = t < SOUND_MUSIC_TRACKS;
    compare_wave(r, t, "vibrato", &tr->vibrato, ram, 0x0151, 0xF2E0, music ? 0x0110 : 0, 0x0111, 0xF400, 0xF420, 0xF480,
                 ramps & 7);
    if (!music)
        differs(r, "vibrato_scale", t, tr->vibrato_scale, ram[0x0110 + x]);
    compare_wave(r, t, "tremolo", &tr->tremolo, ram, 0x0170, 0xF320, 0x0130, 0x0131, 0xF440, 0xF460, 0xF4A0,
                 ramps >> 4 & 7);
}

static void compare_flags(Report *r, const char *name, const SoundVoiceFlags *f, const uint8_t *ram, unsigned kind)
{
    char what[32];
    const struct {
        const char *field;
        AuVoiceSet value;
        unsigned at;
    } fields[] = {
        { "echo", f->echo, 0x54 },        { "noise", f->noise, 0x56 },   { "pitch_mod", f->pitch_modulation, 0x58 },
        { "held", f->held, 0x5A },        { "slur", f->slur, 0x5C },     { "slurring", f->slurring, 0x5E },
        { "roll", f->roll, 0x60 },        { "sounding", f->sounding, 0x24 },
    };
    for (unsigned i = 0; i < sizeof fields / sizeof fields[0]; i++) {
        snprintf(what, sizeof what, "%s.%s", name, fields[i].field);
        differs(r, what, -1, fields[i].value, ram[fields[i].at + kind]);
    }
}

unsigned sound_state_differences(const SoundDriver *d, const uint8_t *ram, char *out, size_t size)
{
    Report r = { out, size, 0, 0 };
    if (size)
        out[0] = 0;
    const SoundSong *s = &d->song;
    for (int t = 0; t < SOUND_TRACKS; t++) {
        bool playing = t < SOUND_MUSIC_TRACKS ? (s->music_tracks >> t & 1) : t >= SOUND_FIRST_EFFECT_TRACK && (d->effect_voices >> (t & 7) & 1);
        if (playing)
            compare_track(&r, &s->tracks[t], t, ram);
    }
    differs(&r, "relocation", -1, s->relocation, word(ram, 0x00));
    differs(&r, "key_on", -1, s->key_on, ram[0x22]);
    differs(&r, "key_off", -1, s->key_off, ram[0x23]);
    differs(&r, "tempo", -1, s->tempo.level, word(ram, 0x46));
    differs(&r, "tempo.steps", -1, s->tempo.steps_left, ram[0x4A]);
    differs(&r, "tempo.step", -1, s->tempo.step, word(ram, 0x4B));
    differs(&r, "music_beat", -1, s->music_beat, ram[0x48]);
    differs(&r, "effect_beat", -1, s->effect_beat, ram[0x49]);
    differs(&r, "volume", -1, s->volume, ram[0x4D]);
    differs(&r, "echo_volume", -1, s->echo_volume.level, word(ram, 0x4E));
    differs(&r, "echo_volume.step", -1, s->echo_volume.step, word(ram, 0x50));
    differs(&r, "echo_volume.steps", -1, s->echo_volume.steps_left, ram[0x52]);
    differs(&r, "music_tracks", -1, s->music_tracks, ram[0x53]);
    compare_flags(&r, "music", &s->music, ram, 0);
    compare_flags(&r, "effects", &s->effects, ram, 1);
    differs(&r, "music_noise_clock", -1, s->music_noise_clock, ram[0x62]);
    differs(&r, "effect_noise_clock", -1, s->effect_noise_clock, ram[0x63]);
    for (unsigned tap = 0; tap < 8; tap++) {
        char what[24];
        snprintf(what, sizeof what, "filter%u", tap);
        differs(&r, what, -1, s->filter[tap].level, word(ram, 0x65 + 2 * tap));
        snprintf(what, sizeof what, "filter%u.step", tap);
        differs(&r, what, -1, s->filter[tap].step, word(ram, 0x0100 + 2 * tap));
    }
    differs(&r, "filter.steps", -1, s->filter_steps_left, ram[0x77]);
    differs(&r, "echo_feedback", -1, s->echo_feedback.level, word(ram, 0x75));
    differs(&r, "echo_feedback.steps", -1, s->echo_feedback.steps_left, ram[0x78]);
    differs(&r, "echo_feedback.step", -1, s->echo_feedback.step, word(ram, 0x79));
    differs(&r, "marker", -1, s->marker, ram[0x7B]);
    differs(&r, "percussion", -1, s->percussion, ram[0x7C]);

    differs(&r, "last_song_started", -1, d->last_song_started, ram[0x80]);
    differs(&r, "echo_delay", -1, d->echo_delay, ram[0x81]);
    differs(&r, "effect_voices", -1, d->effect_voices, ram[0x86]);
    differs(&r, "echo_voices", -1, d->echo_voices, ram[0x89]);
    differs(&r, "noise_voices", -1, d->noise_voices, ram[0x8A]);
    differs(&r, "pitch_mod_voices", -1, d->pitch_modulation_voices, ram[0x8B]);
    differs(&r, "noise_clock", -1, d->noise_clock, ram[0x8C]);
    differs(&r, "muted", -1, d->muted, ram[0xA7]);
    differs(&r, "tempo_modifier", -1, d->tempo_modifier.level, word(ram, 0xA9));
    differs(&r, "pitch_modifier", -1, d->pitch_modifier.level, word(ram, 0xAE));
    differs(&r, "effect_pitch_modifier", -1, d->effect_pitch_modifier, ram[0xB1]);
    differs(&r, "saved_song", -1, d->saved_song_id, ram[0xBB]);
    differs(&r, "song", -1, d->song_id, ram[0xBC]);
    differs(&r, "condition", -1, d->condition, ram[0xD2]);
    differs(&r, "highest_condition", -1, d->highest_condition, ram[0xD3]);
    differs(&r, "own_sample_nibble", -1, d->own_sample_nibble, ram[0xD8]);
    for (unsigned f = 0; f < SOUND_FADES; f++) {
        char what[24];
        snprintf(what, sizeof what, "fade%u", f);
        differs(&r, what, -1, d->fades[f].volume.level, word(ram, 0xF160 + 2 * f));
        snprintf(what, sizeof what, "fade%u.steps", f);
        differs(&r, what, -1, d->fades[f].volume.steps_left, ram[0xF166 + 2 * f]);
    }
    return r.count;
}
