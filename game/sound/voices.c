/* What each voice sounds like, tick by tick, and the mix.
 *
 * A voice's volume on each side is its track's volume, moved by its tremolo,
 * scaled by the fade over it (and for the music by the track's scale and the
 * song's volume), then split between left and right by its pan and pan sweep.
 * Its pitch is its note's, moved by its vibrato and slide, and bent by the
 * pitch modifier of the music or of the effects on voices 6-7. A voice's
 * volume and pitch go out only on the ticks that change them.
 *
 * Vibrato and tremolo are waves (SoundWave): a phase that moves by a step
 * each tick, the step turning round every `rate` ticks, and growing over a
 * ramp of half waves from a quarter of its size when the wave waits for a
 * delay first. The vibrato's phase is a fraction of the note's pitch; the
 * tremolo's is the volume's.
 *
 * The mix -- keys on and off, the echo, noise and pitch modulation -- is set
 * once a tick, at its start, from what the last tick left. */
#include "sound/driver.h"

/* ---- Waves -------------------------------------------------------------------------------- */

/* A wave's depth byte: its low six bits are how deep, the top two how the
 * step grows over the ramp. */
enum { WAVE_DEPTH = 0x3F, WAVE_RAMP = 7 };
typedef enum { GROWS_RISING, GROWS_FALLING, GROWS_BOTH_WAYS } WaveShape;

// SPC $0ADE, $1908
static WaveShape shape_of(uint8_t depth)
{
    if (depth >= 0xC0)
        return GROWS_BOTH_WAYS;
    return depth >= 0x80 ? GROWS_FALLING : GROWS_RISING;
}

static bool falling(uint16_t step) { return step & 0x8000; }

// SPC $18DE
/* The step a wave `depth` deep takes to cover it in `rate` ticks, in 256ths,
 * rounded down to a multiple of 4 and at least 4. */
uint16_t wave_base_step(uint8_t rate, uint8_t depth)
{
    uint16_t size = (uint16_t)((depth & WAVE_DEPTH) + 1);
    uint16_t step = rate ? (uint16_t)((size << 8) / rate) : size;
    step >>= 2;
    if (!step)
        step = 1;
    return (uint16_t)(step << 2);
}

// SPC $1908
/* Start a wave over: a delayed one starts at a quarter of its step and grows
 * to it over its ramp. The falling shape starts downward. */
static void start_wave(SoundWave *w, bool delayed)
{
    w->ticks_left = w->rate;
    uint16_t step = w->base_step;
    if (delayed)
        step >>= 2;
    if (shape_of(w->depth) == GROWS_FALLING)
        step = (uint16_t)-step;
    w->step = step;
    w->ramp = delayed ? WAVE_RAMP : 0;
}

// SPC $18A2
void restart_vibrato(SoundDriver *d, uint8_t track)
{
    SoundTrack *t = &d->song.tracks[track];
    t->vibrato.phase = 0;
    t->vibrato.delay_left = is_music(track) ? t->vibrato.delay : 0;
    start_wave(&t->vibrato, t->vibrato.delay_left != 0);
}

// SPC $1960
void restart_tremolo(SoundDriver *d, uint8_t track)
{
    SoundTrack *t = &d->song.tracks[track];
    t->tremolo.delay_left = t->tremolo.delay;
    start_wave(&t->tremolo, t->tremolo.delay_left != 0);
}

// SPC $0ADE
/* The end of a half wave: the step turns round, and while the ramp lasts it
 * grows by a quarter of the base step, on each turn upward, each turn
 * downward, or every turn, as the shape says. */
static void turn(SoundWave *w)
{
    uint16_t quarter = (uint16_t)(w->base_step >> 2);
    bool ramping = w->ramp != 0;
    if (ramping)
        w->ramp--;
    uint16_t step = w->step;
    switch (shape_of(w->depth)) {
    case GROWS_RISING:
        step = (uint16_t)-step;
        if (ramping && !falling(step))
            step = (uint16_t)(step + quarter);
        break;
    case GROWS_FALLING:
        step = (uint16_t)-step;
        if (ramping && falling(step))
            step = (uint16_t)(step - quarter);
        break;
    case GROWS_BOTH_WAYS:
        if (!ramping) {
            step = (uint16_t)-step;
        } else {
            uint16_t size = (uint16_t)((falling(step) ? (uint16_t)-step : step) + quarter);
            step = falling(w->step) ? size : (uint16_t)-size;
        }
        break;
    }
    w->step = step;
}

// SPC $0916, $09A1
/* A tick of a wave; true if its phase's whole part moved. */
static bool move_wave(SoundWave *w)
{
    uint16_t phase = (uint16_t)(w->phase + w->step);
    bool moved = (phase >> 8) != (w->phase >> 8);
    w->phase = phase;
    if (--w->ticks_left == 0) {
        turn(w);
        w->ticks_left = w->rate;
    }
    return moved;
}

// SPC $0936
/* `swing`/256 of `depth`, rounded down. */
static int16_t swing_of(int8_t swing, uint16_t depth)
{
    int32_t product = (int32_t)swing * depth;
    return (int16_t)(product >= 0 ? product / 256 : -((-product + 255) / 256));
}

// SPC $0903
/* The vibrato waits out its delay on the music; the tremolo on both. */
static void move_waves(SoundDriver *d, uint8_t track)
{
    SoundTrack *t = &d->song.tracks[track];
    AuVoiceSet voice = voice_bit(track);
    if (t->vibrato.depth && !(is_music(track) && t->vibrato.delay_left)) {
        if (move_wave(&t->vibrato)) {
            int8_t swing = (int8_t)(uint8_t)(t->vibrato.phase >> 7);
            t->vibrato_offset = swing_of(swing, t->vibrato_depth);
            d->pitch_changed |= voice;
        }
    }
    if (t->tremolo.depth && !t->tremolo.delay_left) {
        if (move_wave(&t->tremolo))
            d->volume_changed |= voice;
    }
}

/* ---- A voice's volume and pitch ----------------------------------------------------------- */

static uint8_t scaled(uint8_t level, uint8_t by) { return (uint8_t)((level * by) >> 8); }

// SPC $0A0A
/* The pan, 0 left to 255 right, with the pan sweep's swing, held to that range. */
static uint8_t pan_of(const SoundTrack *t)
{
    int16_t pan = (int16_t)(slide_whole(&t->pan) + (int8_t)(t->pan_sweep_phase >> 8));
    return pan < 0 ? 0 : pan > 0xFF ? 0xFF : (uint8_t)pan;
}

/* The volume's share on the left: in mono, half on each side. */
enum { MONO_LEFT_SHARE = 0x80 };

// SPC $09FB
static void put_volume(SoundDriver *d, uint8_t track, uint8_t fade)
{
    SoundTrack *t = &d->song.tracks[track];
    uint8_t voice = voice_of(track);
    uint8_t left_share = d->mono ? MONO_LEFT_SHARE : (uint8_t)~pan_of(t);
    uint8_t volume = slide_whole(&t->volume);
    int8_t tremolo = (int8_t)(t->tremolo.phase >> 8);
    if (tremolo)
        volume = (uint8_t)((volume * (256 + 2 * tremolo)) >> 8);
    uint8_t level = scaled(fade, volume);
    if (is_music(track))
        level = (d->muted & voice_bit(track)) ? 0 : scaled(scaled(level, t->volume_scale), d->song.volume);
    uint8_t left = scaled(level, left_share), right = scaled(level, (uint8_t)~left_share);
    d->meter_volume[2 * voice] = left;
    d->meter_volume[2 * voice + 1] = right;
    au_voice_volume(d->voices, voice, (int8_t)left, (int8_t)right);
}

// SPC $0A78
/* A pitch bent by a modifier: up to twice as high (0-127), or down to a
 * fraction of it (128-255, the music's only). */
static uint16_t bent(uint16_t pitch, uint8_t modifier, bool can_lower)
{
    uint16_t part = (uint16_t)(((uint32_t)pitch * modifier) >> 8);
    if (can_lower && (modifier & 0x80))
        return part;
    return (uint16_t)(pitch + 2 * part);
}

// SPC $0A70
void put_pitch_on_voice(SoundDriver *d, uint8_t track)
{
    SoundTrack *t = &d->song.tracks[track];
    uint16_t pitch = (uint16_t)(t->pitch + t->vibrato_offset);
    if (is_music(track))
        pitch = bent(pitch, slide_whole(&d->pitch_modifier), true);
    else if ((EFFECT_PAIR_67 & voice_bit(track)) && d->effect_pitch_modifier)
        pitch = bent(pitch, d->effect_pitch_modifier, false);
    au_voice_pitch(d->voices, voice_of(track), pitch);
}

// SPC $09F6
static void put_voice(SoundDriver *d, uint8_t track, uint8_t fade)
{
    AuVoiceSet voice = voice_bit(track);
    if (d->volume_changed & voice)
        put_volume(d, track, fade);
    if (d->pitch_changed & voice)
        put_pitch_on_voice(d, track);
}

/* ---- Every voice, each tick --------------------------------------------------------------- */

static AuVoiceSet music_voices_heard(const SoundDriver *d)
{
    return (AuVoiceSet)(~d->effect_voices & d->song.music_tracks & d->song.music.sounding);
}

// SPC $02F5
/* The music's waves stand still while any effect plays. */
void update_voices(SoundDriver *d)
{
    AuVoiceSet music = music_voices_heard(d);
    uint8_t fade = slide_whole(&d->fades[FADE_MUSIC].volume);
    for (uint8_t track = 0; track < SOUND_MUSIC_TRACKS; track++) {
        if (!(music & voice_bit(track)))
            continue;
        if (!d->effect_voices)
            move_waves(d, track);
        put_voice(d, track, fade);
    }
    AuVoiceSet effects = d->effect_voices & d->song.effects.sounding;
    for (uint8_t voice = AU_VOICES; voice-- > 4;) {
        if (!(effects & 1u << voice))
            continue;
        uint8_t track = effect_track_of(voice);
        move_waves(d, track);
        put_voice(d, track, slide_whole(&d->fades[voice >= 6 ? FADE_EFFECTS_67 : FADE_EFFECTS_45].volume));
    }
}

// SPC $0439
void update_music_voices_while_loading(SoundDriver *d)
{
    AuVoiceSet music = music_voices_heard(d);
    uint8_t fade = slide_whole(&d->fades[FADE_MUSIC].volume);
    for (uint8_t track = 0; track < SOUND_MUSIC_TRACKS; track++) {
        if (!(music & voice_bit(track)))
            continue;
        move_waves(d, track);
        put_voice(d, track, fade);
    }
}

/* ---- The song's settings and the mix ------------------------------------------------------- */

// SPC $04C7
/* The tempo, the echo's volume and feedback, and its filter slide with the
 * music's steps. */
void slide_song_settings(SoundDriver *d)
{
    SoundSong *s = &d->song;
    slide_advance(&s->tempo);
    slide_advance(&s->echo_volume);
    slide_advance(&s->echo_feedback);
    if (s->filter_steps_left) {
        s->filter_steps_left--;
        for (uint8_t tap = 0; tap < 8; tap++)
            s->filter[tap].level = (uint16_t)(s->filter[tap].level + s->filter[tap].step);
    }
}

// SPC $02A2
/* The echo's volume as the mix hears it, worked out at the start of each
 * tick: at full master volume twice as loud, and a negative one at its
 * loudest. The ticks while data loads mix with the last tick's. */
void hear_echo_volume(SoundDriver *d)
{
    uint8_t volume = slide_whole(&d->song.echo_volume);
    if (d->full_volume)
        volume = volume & 0x80 ? 0x7F : (uint8_t)(volume << 1);
    d->echo_volume_heard = (int8_t)volume;
}

// SPC $02A0
void flush_mix(SoundDriver *d)
{
    SoundSong *s = &d->song;
    int8_t taps[8];
    for (uint8_t tap = 0; tap < 8; tap++)
        taps[tap] = (int8_t)slide_whole(&s->filter[tap]);
    au_echo_filter(d->voices, taps);
    au_echo_feedback(d->voices, (int8_t)slide_whole(&s->echo_feedback));
    au_echo_volume(d->voices, d->echo_volume_heard, d->echo_volume_heard);
    au_echo_voices(d->voices, d->echo_voices);
    au_voices_key_off(d->voices, s->key_off);
    /* The mixer's switches are set whole every tick, as the original writes
     * them from its shadow: the noise at its clock, the echo writing and the
     * output on, whatever changed them since (a `$FD` register write). */
    au_noise_clock(d->voices, d->noise_clock);
    au_echo_writes(d->voices, true);
    au_output_stopped(d->voices, false);
    au_noise_voices(d->voices, d->noise_voices);
    au_pitch_modulation(d->voices, d->pitch_modulation_voices);
    au_voices_key_on(d->voices, s->key_on);
    s->key_off = s->key_on = 0;
}

// SPC $1438
/* The meter: in place of the status, four voices' levels a tick, the lower
 * four then the upper: the output's level scaled by the left volume in the
 * high nibble, by the right in the low, and the top bit for the upper four. */
void meter_status(SoundDriver *d)
{
    uint8_t first = d->meter_upper ? 4 : 0;
    for (uint8_t i = 0; i < 4; i++) {
        uint8_t voice = (uint8_t)(first + i);
        uint8_t level = (uint8_t)au_voice_output(d->voices, voice);
        uint8_t left = scaled(level, (uint8_t)(d->meter_volume[2 * voice] << 1));
        uint8_t right = scaled(level, (uint8_t)(d->meter_volume[2 * voice + 1] << 1));
        d->status.port[i] = (uint8_t)((left & 0x70) | ((right >> 4) & 7) | (d->meter_upper ? 0x80 : 0));
    }
    d->meter_upper = !d->meter_upper;
}
