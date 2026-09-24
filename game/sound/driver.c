/* The sound driver's tick, its commands and what it shows the game.
 *
 * A tick sets the mix the last one left (the voices keyed on and off, the
 * echo, noise and pitch modulation), shows the game the status, moves the
 * fades on (every seventh tick), steps the tracks, and then turns every
 * voice's state into what it sounds like. Commands the game sends while a
 * tick runs wait in a queue for one of the tick's two points that take them
 * (sound_queue_command); one sent between ticks is carried out at once. A
 * pause stops everything but the mix and the status.
 *
 * The fades are three volumes the game moves toward a level over a number of
 * steps: the music's and the two effect pairs'. A fade that ends in silence
 * stops what it faded. The tempo and pitch modifiers are two more such
 * values, for the whole song (the game slows the music, or bends it). */
#include <string.h>

#include "core/sound_data.h"
#include "sound/driver.h"

enum { TICKS_PER_FADE_STEP = 7 };

/* ---- Slides --------------------------------------------------------------------------- */

uint16_t slide_step(uint8_t from, uint8_t to, uint8_t steps, Signedness values)
{
    bool up = values == SIGNED ? (int8_t)to >= (int8_t)from : to >= from;
    uint8_t distance = up ? (uint8_t)(to - from) : (uint8_t)(from - to);
    uint16_t step = (uint16_t)(((uint16_t)distance << 8) / steps);
    return up ? step : (uint16_t)-step;
}

bool slide_advance(SoundSlide *s)
{
    if (!s->steps_left)
        return false;
    uint8_t before = slide_whole(s);
    s->steps_left--;
    s->level = (uint16_t)(s->level + s->step);
    return slide_whole(s) != before;
}

/* Move a fade or a modifier from its whole value to `target` over `steps`
 * steps, or there at once. */
static void slide_to(SoundSlide *s, uint8_t steps, uint8_t target, Signedness values)
{
    uint8_t from = slide_whole(s);
    if (steps == 0 || target == from) {
        *s = (SoundSlide){ .level = (uint16_t)(target << 8) };
        return;
    }
    s->steps_left = steps;
    s->step = slide_step(from, target, steps, values);
    s->level = (uint16_t)(from << 8);
}

/* ---- Fades and modifiers ----------------------------------------------------------------- */

static const AuVoiceSet fade_voices[SOUND_FADES] = { 0, EFFECT_PAIR_67, EFFECT_PAIR_45 };

static AuVoiceSet voices_faded(const SoundDriver *d, uint8_t fade)
{
    return fade == FADE_MUSIC ? d->song.music_tracks : fade_voices[fade];
}

/* A fade of the music marks voices 0-6 changed and not 7, as the original
 * does (the voice's volume catches up at its next change). */
enum { MUSIC_FADE_MARKS = 0x7F };

static void fade(SoundDriver *d, uint8_t which, uint8_t steps, uint8_t volume)
{
    d->volume_changed |= which == FADE_MUSIC ? MUSIC_FADE_MARKS : fade_voices[which];
    slide_to(&d->fades[which].volume, steps, volume, UNSIGNED);
}

/* A step of each fade; one that has just reached silence stops what it
 * faded. The music's keeps the song the save holds, to resume. */
static void advance_fades(SoundDriver *d)
{
    for (uint8_t which = 0; which < SOUND_FADES; which++) {
        SoundSlide *f = &d->fades[which].volume;
        if (!f->steps_left)
            continue;
        if (slide_advance(f))
            d->volume_changed |= voices_faded(d, which);
        if (f->steps_left || slide_whole(f))
            continue;
        if (which == FADE_MUSIC) {
            uint8_t song = d->song_id, saved = d->saved_song_id;
            stop_music(d);
            d->song_id = song;
            d->saved_song_id = saved;
        } else {
            stop_effects(d, fade_voices[which]);
        }
    }
    slide_advance(&d->tempo_modifier);
    if (slide_advance(&d->pitch_modifier))
        d->pitch_changed |= d->song.music_tracks;
}

/* ---- Commands ------------------------------------------------------------------------- */

/* What a command does is its row's: the operation and what the command fixes
 * of it (which set of a song's tracks, which pair of voices, which fade). The
 * operations name the command's parameters as they take them. */
typedef struct CommandRow CommandRow;
typedef void (*CommandRun)(SoundDriver *d, const CommandRow *row, SoundCommand c);

typedef enum { MUTE_ONLY, MUTE_ALSO, MUTE_NO_LONGER } MuteHow;
enum { ALL_FADES = SOUND_FADES };

struct CommandRow {
    uint8_t command;
    CommandRun run;
    uint8_t track_set;       /* play_song: the song's first or second set of tracks */
    bool save_first;         /* play_song: the song playing is saved, to resume later */
    AuVoiceSet pair;         /* play_effect: the voices the effect plays on */
    uint8_t fade;            /* fade: which (FADE_*), or ALL_FADES */
    bool tempo, pitch;       /* bend: what the modifier bends */
    bool music, effects;     /* stop: all of either */
    MuteHow mute;
};

static void play_song(SoundDriver *d, const CommandRow *row, SoundCommand c)
{
    uint8_t song = c.p1, volume = c.p2, echo_delay = c.p3;
    start_song(d, song, volume, echo_delay, row->track_set, row->save_first);
}

static void play_effect(SoundDriver *d, const CommandRow *row, SoundCommand c)
{
    uint8_t effect = c.p1, pan = c.p2;
    start_effect(d, effect, pan, row->pair);
}

static void fade_to(SoundDriver *d, const CommandRow *row, SoundCommand c)
{
    uint8_t steps = c.p1, volume = c.p2;
    if (row->fade != ALL_FADES) {
        fade(d, row->fade, steps, volume);
        return;
    }
    for (uint8_t which = 0; which < SOUND_FADES; which++)
        fade(d, which, steps, volume);
}

/* Slow the music down or speed it up, bend its pitch, or both, reaching the
 * modifier over `steps` steps: 1-127 up to twice as fast or high, 128-255
 * down to a fraction, 0 as written. */
static void bend(SoundDriver *d, const CommandRow *row, SoundCommand c)
{
    uint8_t steps = c.p1, modifier = c.p2;
    if (row->pitch) {
        slide_to(&d->pitch_modifier, steps, modifier, SIGNED);
        d->pitch_changed = ALL_VOICES;
    }
    if (row->tempo)
        slide_to(&d->tempo_modifier, steps, modifier, SIGNED);
}

/* The value a song's conditional branches look at (event $FA). */
static void set_condition(SoundDriver *d, const CommandRow *row, SoundCommand c)
{
    (void)row;
    d->condition = c.p1 & 0x0F;
}

/* Bend the pitch of the effects on voices 6-7. */
static void bend_effects(SoundDriver *d, const CommandRow *row, SoundCommand c)
{
    (void)row;
    uint8_t modifier = c.p1;
    if (modifier == d->effect_pitch_modifier)
        return;
    d->effect_pitch_modifier = modifier;
    d->pitch_changed |= d->effect_voices & EFFECT_PAIR_67;
}

enum { HALF_MASTER_VOLUME = 0x40, FULL_MASTER_VOLUME = 0x7F };

static void set_master_volume(SoundDriver *d, const CommandRow *row, SoundCommand c)
{
    (void)row;
    d->full_volume = c.p1 != 0;
    int8_t volume = d->full_volume ? FULL_MASTER_VOLUME : HALF_MASTER_VOLUME;
    au_master_volume(d->voices, volume, volume);
}

/* Stop the music, the effects, or both; or one pair of effects, the one
 * the parameter's low bit names. */
static void stop(SoundDriver *d, const CommandRow *row, SoundCommand c)
{
    if (row->music)
        stop_music(d);
    if (row->effects) {
        stop_effects(d, EFFECT_PAIR_67);
        stop_effects(d, EFFECT_PAIR_45);
    } else if (!row->music) {
        stop_effects(d, c.p1 & 1 ? EFFECT_PAIR_45 : EFFECT_PAIR_67);
    }
}

static void set_mono(SoundDriver *d, const CommandRow *row, SoundCommand c)
{
    (void)row;
    d->mono = c.p1 != 0;
    d->volume_changed = ALL_VOICES;
}

/* Silence some of the music's voices: only these, these as well, or these
 * no longer. */
static void mute(SoundDriver *d, const CommandRow *row, SoundCommand c)
{
    AuVoiceSet voices = c.p1;
    if (row->mute == MUTE_ONLY) {
        d->muted = voices;
        d->volume_changed = ALL_VOICES;
        return;
    }
    d->muted = row->mute == MUTE_ALSO ? (AuVoiceSet)(d->muted | voices) : (AuVoiceSet)(d->muted & ~voices);
    d->volume_changed |= voices;
}

/* Pause: every voice holds its envelope where it is and falls silent, and
 * the ticks do nothing but the mix and the status until it resumes. */
static void pause_or_resume(SoundDriver *d, const CommandRow *row, SoundCommand c)
{
    (void)row;
    if (c.p1) {
        au_voices_hold(d->voices, true);
        for (uint8_t voice = 0; voice < AU_VOICES; voice++)
            au_voice_volume(d->voices, voice, 0, 0);
        d->paused = true;
        d->volume_changed = 0;
        d->song.key_on = 0;
        memset(d->meter_volume, 0, sizeof d->meter_volume);
    } else {
        au_voices_hold(d->voices, false);
        d->paused = false;
        d->volume_changed = ALL_VOICES;
    }
}

/* Fast forward: the timer ticks at every count, and every tick steps the
 * tracks whatever the tempo. A song's start ends the second but not the
 * first. */
static void fast_forward(SoundDriver *d, const CommandRow *row, SoundCommand c)
{
    (void)row;
    d->fast = c.p1 != 0;
    d->stepping_every_tick = d->fast;
}

/* A new delay for the echo; with no music playing once its buffer is ready,
 * the echo falls silent. */
static void set_echo_delay(SoundDriver *d, uint8_t delay)
{
    delay &= 0x0F;
    if (delay == d->echo_delay)
        return;
    d->echo_delay = delay;
    change_echo_delay(d, delay);
    d->echo_silences_waiting++;
    if (!d->echo_waits)
        sound_echo_ready(d);
}

static void echo_delay(SoundDriver *d, const CommandRow *row, SoundCommand c)
{
    (void)row;
    set_echo_delay(d, c.p1);
}

void sound_echo_drained(SoundDriver *d)
{
    if (d->echo_draining)
        au_echo_writes(d->voices, true);
    d->echo_draining = false;
}

void sound_echo_ready(SoundDriver *d)
{
    sound_echo_drained(d);
    if (d->song_voices_waiting) {
        take_song_voices(d);
        d->song_voices_waiting = false;
    }
    if (d->echo_silences_waiting) {
        d->echo_silences_waiting--;
        if (!d->song.music_tracks)
            slide_set_whole(&d->song.echo_volume, 0);
    }
}

static void write_register(SoundDriver *d, const CommandRow *row, SoundCommand c)
{
    (void)row;
    uint8_t reg = c.p1, value = c.p2;
    au_voices_snes_register(d->voices, reg, value);
}

/* $FF: the voices' levels on the status in place of the status (p1 1, on
 * unless p2 is 0), or the driver stopping for the CPU half to load it again
 * (p1 $F0). */
enum { METER = 0x01, RELOAD = 0xF0 };

static void meter_or_reload(SoundDriver *d, const CommandRow *row, SoundCommand c)
{
    (void)row;
    if (c.p1 == METER) {
        d->meter = c.p2 != 0;
    } else if (c.p1 == RELOAD) {
        au_mixer(d->voices, (AuMixer){ .reset = true, .muted = true, .echo_writes_off = true });
        d->stopped = true;
    }
}

/* The CPU half's loads ($FE) leave nothing for the driver to do: what they
 * load comes through sound_load_*. */
static void loading(SoundDriver *d, const CommandRow *row, SoundCommand c)
{
    (void)d;
    (void)row;
    (void)c;
}

#define RUN(op) .run = op

static const CommandRow commands[] = {
    { 0x10, RUN(play_song), .track_set = FIRST_TRACK_SET },
    { 0x11, RUN(play_song), .track_set = SECOND_TRACK_SET },
    { 0x14, RUN(play_song), .track_set = FIRST_TRACK_SET, .save_first = true },
    { 0x15, RUN(play_song), .track_set = SECOND_TRACK_SET, .save_first = true },
    { 0x18, RUN(play_effect), .pair = EFFECT_PAIR_67 },
    { 0x19, RUN(play_effect), .pair = EFFECT_PAIR_45 },
    { 0x80, RUN(fade_to), .fade = ALL_FADES },
    { 0x81, RUN(fade_to), .fade = FADE_MUSIC },
    { 0x82, RUN(fade_to), .fade = FADE_EFFECTS_67 },
    { 0x83, RUN(fade_to), .fade = FADE_EFFECTS_45 },
    { 0x85, RUN(bend), .pitch = true, .tempo = true },
    { 0x86, RUN(bend), .tempo = true },
    { 0x87, RUN(bend), .pitch = true },
    { 0x88, RUN(set_condition) },
    { 0x89, RUN(bend_effects) },
    { 0x8E, RUN(set_master_volume) },
    { 0xF0, RUN(stop), .music = true, .effects = true },
    { 0xF1, RUN(stop), .music = true },
    { 0xF2, RUN(stop) },
    { 0xF3, RUN(set_mono) },
    { 0xF4, RUN(mute), .mute = MUTE_ONLY },
    { 0xF5, RUN(pause_or_resume) },
    { 0xF6, RUN(fast_forward) },
    { 0xF8, RUN(mute), .mute = MUTE_NO_LONGER },
    { 0xF9, RUN(mute), .mute = MUTE_ALSO },
    { 0xFC, RUN(echo_delay) },
    { 0xFD, RUN(write_register) },
    { 0xFE, RUN(loading) },
    { 0xFF, RUN(meter_or_reload) },
};

void sound_run_command(SoundDriver *d, SoundCommand c)
{
    for (unsigned i = 0; i < sizeof commands / sizeof commands[0]; i++) {
        if (commands[i].command == c.command) {
            commands[i].run(d, &commands[i], c);
            return;
        }
    }
}

/* A full queue takes a new command in place of its last. */
void sound_queue_command(SoundDriver *d, SoundCommand c)
{
    if (!c.command)
        return;
    d->queue[d->queued < SOUND_QUEUE ? d->queued++ : SOUND_QUEUE - 1] = c;
}

static void run_queued_commands(SoundDriver *d)
{
    for (uint8_t i = 0; i < d->queued; i++)
        sound_run_command(d, d->queue[i]);
    d->queued = 0;
}

/* ---- The status ------------------------------------------------------------------------- */

/* The status: port 1 the steps the music's fade has left; port 2 the song's
 * marker (low nibble) and the highest condition a branch has looked for (high
 * nibble); port 3 the music's tracks playing, folded into a nibble (track 0 or
 * 4, 1 or 5, ...), and the effects' voices. Port 0 is the driver's own. */
enum { MUSIC_FADE_PORT = 1, MARKER_PORT = 2, PLAYING_PORT = 3 };

static void show_status(SoundDriver *d)
{
    if (d->meter) {
        meter_status(d);
        return;
    }
    uint8_t music = d->song.music_tracks;
    d->status.port[0] = 0;
    d->status.port[MUSIC_FADE_PORT] = d->fades[FADE_MUSIC].volume.steps_left;
    d->status.port[MARKER_PORT] = (uint8_t)(d->highest_condition << 4 | d->song.marker);
    d->status.port[PLAYING_PORT] = (uint8_t)(((music | music >> 4) & 0x0F) | d->effect_voices);
}

uint8_t sound_music_fade_left(const SoundDriver *d) { return d->status.port[MUSIC_FADE_PORT]; }
uint8_t sound_playing(const SoundDriver *d) { return d->status.port[PLAYING_PORT]; }
uint8_t sound_marker(const SoundDriver *d) { return d->status.port[MARKER_PORT]; }
SoundStatus sound_status(const SoundDriver *d) { return d->status; }
bool sound_ticking_fast(const SoundDriver *d) { return d->fast; }

/* ---- The driver's own sample --------------------------------------------------------------- */

/* The driver's own sample is a waveform it keeps changing: each tick one
 * nibble of the looped part's first two blocks flips between 7 and 0, going
 * round them (and past their headers), so the sample never repeats the same
 * way. The nibble after the round's last flips the first byte's top nibble as
 * well. Nibbles are counted from the loop's start, two a byte, high first. */
enum {
    FIRST_NIBBLE = 2,
    LAST_NIBBLE = 0x23,
    AFTER_LAST_NIBBLE = 0x24,
    ROUND_START = 3,
    BRR_BLOCK = 9,
    OWN_SAMPLE = 0x10,        /* its number in the directory, and its instrument's */
    OWN_SAMPLE_TUNING = 0x00E1,
    OWN_SAMPLE_ENVELOPE = 0xE0FF
};

static void flip_high_nibble(SoundDriver *d, uint8_t at)
{
    d->own_sample[at] ^= 0x70;
    au_sample_write(d->voices, OWN_SAMPLE, at, d->own_sample[at]);
}

static void flip_nibble(SoundDriver *d, uint8_t nibble)
{
    uint8_t at = (uint8_t)(SOUND_OWN_SAMPLE_LOOP + nibble / 2);
    d->own_sample[at] ^= nibble & 1 ? 0x07 : 0x70;
    au_sample_write(d->voices, OWN_SAMPLE, at, d->own_sample[at]);
}

static void animate_own_sample(SoundDriver *d)
{
    uint8_t nibble = d->own_sample_nibble;
    if (nibble == LAST_NIBBLE)
        flip_high_nibble(d, SOUND_OWN_SAMPLE_LOOP + 1);
    flip_nibble(d, nibble);
    uint8_t next = (uint8_t)(nibble + 1);
    if (next == AFTER_LAST_NIBBLE)
        next = ROUND_START;
    else if ((next / 2) % BRR_BLOCK == 0)
        next += 2;   /* a block's header */
    d->own_sample_nibble = next;
}

/* ---- Boot and the tick ---------------------------------------------------------------------- */

enum { INITIAL_ECHO_DELAY = 5 };

/* The boot starts the driver's variables afresh. What the driver keeps in its
 * tables it leaves: the tracks' settings and their loops, the filter's steps,
 * the song saved, the data loaded, and the fades' fractions and steps. So a
 * reload of the driver (command $FF $F0, then the CPU half's boot upload)
 * carries those over, as the original's does. */
static void forget_variables(SoundDriver *d)
{
    SoundSong *s = &d->song;
    for (uint8_t track = 0; track < SOUND_TRACKS; track++) {
        s->tracks[track].at = 0;
        s->tracks[track].ticks_left = 0;
        s->tracks[track].loop_level = 0;
    }
    s->relocation = 0;
    s->key_on = s->key_off = 0;
    s->tempo = s->echo_volume = s->echo_feedback = (SoundSlide){ 0 };
    s->music_beat = s->effect_beat = s->volume = 0;
    for (uint8_t tap = 0; tap < SOUND_ECHO_TAPS; tap++)
        s->filter[tap].level = 0;
    s->filter_steps_left = 0;
    s->music_tracks = 0;
    s->music = s->effects = (SoundVoiceFlags){ 0 };
    s->music_noise_clock = s->effect_noise_clock = s->marker = s->percussion = 0;

    d->echo_delay = 0;
    d->effect_voices = d->echo_voices = d->noise_voices = d->pitch_modulation_voices = 0;
    d->noise_clock = 0;
    d->echo_volume_heard = 0;
    d->muted = d->volume_changed = d->pitch_changed = 0;
    d->mono = d->full_volume = d->paused = d->stepping_every_tick = d->fast = false;
    d->meter = d->meter_upper = d->stopped = false;
    d->tempo_modifier = d->pitch_modifier = (SoundSlide){ 0 };
    d->effect_pitch_modifier = d->condition = d->highest_condition = 0;
    memset(d->meter_volume, 0, sizeof d->meter_volume);
    d->queued = 0;
    d->song_voices_waiting = false;
    d->echo_silences_waiting = 0;
    d->echo_draining = false;
    for (uint8_t which = 0; which < SOUND_FADES; which++) {
        slide_set_whole(&d->fades[which].volume, 0xFF);
        d->fades[which].volume.steps_left = 0;
    }
}

/* The host zeroes the driver before its first boot, as power-on leaves sound
 * memory. */
void sound_boot(SoundDriver *d, const GameState *game, AuVoices *voices)
{
    d->game = game;
    d->voices = voices;
    forget_variables(d);
    load_boot_data(d);
    d->last_song_started = d->song_id = d->saved_song_id = NO_SONG;

    au_echo_volume(voices, 0, 0);
    au_voices_start(voices);
    for (uint8_t voice = 0; voice < AU_VOICES; voice++)
        au_voice_gain(voices, voice, (AuGain){ AU_GAIN_DECREASE_EXPONENTIAL, 0 });
    set_echo_delay(d, INITIAL_ECHO_DELAY);
    d->song.key_off = ALL_VOICES;
    d->fade_divider = TICKS_PER_FADE_STEP;

    au_sample_define(voices, OWN_SAMPLE, d->own_sample, SOUND_OWN_SAMPLE_BYTES, SOUND_OWN_SAMPLE_LOOP);
    store_instrument(d, OWN_SAMPLE, OWN_SAMPLE_TUNING, OWN_SAMPLE_ENVELOPE);
    au_master_volume(voices, HALF_MASTER_VOLUME, HALF_MASTER_VOLUME);
    d->own_sample_nibble = FIRST_NIBBLE;
}

void sound_tick_begin(SoundDriver *d)
{
    if (d->stopped)
        return;
    hear_echo_volume(d);
    flush_mix(d);
    show_status(d);
    if (d->paused)
        return;
    if (--d->fade_divider == 0) {
        d->fade_divider = TICKS_PER_FADE_STEP;
        advance_fades(d);
    }
    sequence(d);
}

void sound_tick_voices(SoundDriver *d)
{
    if (d->stopped || d->paused)
        return;
    update_voices(d);
    d->volume_changed = d->pitch_changed = 0;
}

/* The end of a tick that got past its voices: a pause that came with the
 * last commands doesn't stop it. */
void sound_tick_end(SoundDriver *d)
{
    if (d->stopped)
        return;
    animate_own_sample(d);
}

void sound_tick(SoundDriver *d)
{
    sound_tick_begin(d);
    if (d->stopped || d->paused)
        return;
    run_queued_commands(d);
    if (d->paused)
        return;
    sound_tick_voices(d);
    run_queued_commands(d);
    sound_tick_end(d);
}

void sound_tick_while_loading(SoundDriver *d)
{
    if (d->stopped || d->paused)
        return;
    flush_mix(d);
    if (--d->fade_divider == 0) {
        d->fade_divider = TICKS_PER_FADE_STEP;
        advance_fades(d);
    }
    sequence_music_while_loading(d);
    update_music_voices_while_loading(d);
    d->volume_changed = d->pitch_changed = 0;
}
