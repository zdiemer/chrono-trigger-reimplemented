/* Songs and sound effects: what they load, how they start, and how they stop.
 *
 * A song is a header and its tracks' streams. The header gives the address
 * the song was built for (its streams' jumps are addresses in it), a marker
 * for "no track", and two sets of eight track addresses: commands $10 and
 * $14 play the first set, $11 and $15 the second. Starting a song keys every
 * voice off, loads it, and starts its tracks; starting the same song again
 * does nothing (the game restarts the music without a break that way). A
 * song can be saved as it plays and resumed later where it was, fading back
 * in: the field's music, left for a battle and taken up again after it.
 *
 * A sound effect is two tracks named by an entry of the loaded effect set,
 * played on a pair of voices (6-7 or 4-5) that it takes from the music for
 * as long as it plays. Starting one cuts off what the pair was playing.
 *
 * The data all of this reads is sound memory, loaded as the CPU half would
 * upload it: the instruments' tables at $1F00, the song at $2000 with the
 * effect set after it at $2F00 (a song long enough runs over the effects,
 * as in the original, until the set is loaded again), and the song's
 * percussion at $F120. */
#include <string.h>

#include "core/sound_data.h"
#include "sound/driver.h"

/* ---- The sound memory ----------------------------------------------------------------- */

uint8_t sound_memory_byte(SoundDriver *d, uint16_t at)
{
    if (at >= SOUND_INSTRUMENT_TABLES && at < SOUND_SEQUENCES)
        return d->instrument_tables[at - SOUND_INSTRUMENT_TABLES];
    if (at >= SOUND_SEQUENCES && at < SOUND_SEQUENCES_END)
        return d->sequences[at - SOUND_SEQUENCES];
    if (at >= SOUND_PERCUSSION && at < SOUND_PERCUSSION + sizeof d->percussion)
        return d->percussion[at - SOUND_PERCUSSION];
    if (at == SILENT_TRACK)
        return END_OF_TRACK;
    if (!d->stray_reads++)
        d->first_stray_read = at;
    return 0;
}

uint16_t sound_memory_word(SoundDriver *d, uint16_t at)
{
    return (uint16_t)(sound_memory_byte(d, at) | sound_memory_byte(d, (uint16_t)(at + 1)) << 8);
}

/* ---- Loading ---------------------------------------------------------------------------- */

// SPC $10EC
static void load_effect_set(SoundDriver *d, uint8_t set)
{
    uint16_t length = sound_effect_set_length(d->game, set);
    for (uint16_t i = 0; i < length && SOUND_EFFECTS + i < SOUND_SEQUENCES_END; i++)
        d->sequences[SOUND_EFFECTS - SOUND_SEQUENCES + i] = sound_effect_set_byte(d->game, set, i);
}

// SPC $1044, $10EC
/* The set loaded is the one a song that runs over it brings back (loads.c). */
void sound_load_effects(SoundDriver *d, uint8_t set)
{
    stop_effects(d, EFFECT_PAIR_67);
    stop_effects(d, EFFECT_PAIR_45);
    load_effect_set(d, set);
    d->samples.effect_set = set;
    d->samples.effects_overwritten = 0;
}

// SPC $107C
/* A song's sequence goes up three bytes at a time and one message more than
 * it needs, so the bytes after it in the cartridge come with it. */
static void load_song(SoundDriver *d, uint8_t song)
{
    uint16_t length = sound_song_length(d->game, song);
    uint16_t loaded = (uint16_t)(3 * (length / 3 + 1));
    for (uint16_t i = 0; i < loaded && SOUND_SEQUENCES + i < SOUND_SEQUENCES_END; i++)
        d->sequences[i] = sound_song_byte(d->game, song, i);
    for (uint8_t i = 0; i < sizeof d->percussion; i++)
        d->percussion[i] = sound_song_percussion_byte(d->game, song, i);
    bool listed = true;
    for (uint8_t i = 0; i < SOUND_SONG_INSTRUMENTS; i++) {
        uint16_t block = listed ? sound_song_instrument_block(d->game, song, i) : 0;
        listed = block != 0;
        sound_load_instrument(d, (uint8_t)(SOUND_FIRST_SONG_INSTRUMENT + i), block);
    }
}

// SPC $0200
void load_boot_data(SoundDriver *d)
{
    const GameState *g = d->game;
    for (uint8_t i = 0; i < SOUND_SEMITONES; i++)
        d->semitone_pitch[i] = sound_semitone_pitch(g, i);
    for (uint8_t i = 0; i < SOUND_NOTE_LENGTHS; i++)
        d->note_length[i] = sound_note_length(g, i);
    for (uint8_t i = 0; i < SOUND_VIBRATO_SCALES; i++)
        d->vibrato_scale[i] = sound_vibrato_scale(g, i);
    for (uint8_t filter = 0; filter < SOUND_ECHO_FILTERS; filter++)
        for (uint8_t tap = 0; tap < SOUND_ECHO_TAPS; tap++)
            d->echo_filter[filter][tap] = sound_echo_filter_tap(g, filter, tap);
    for (uint8_t i = 0; i < SOUND_OWN_SAMPLE_BYTES; i++)
        d->own_sample[i] = sound_own_sample_byte(g, i);
    uint8_t instruments = sound_boot_instruments(g);
    for (uint8_t i = 0; i < instruments; i++)
        store_instrument(d, i, sound_boot_instrument_tuning(g, i), sound_boot_instrument_envelope(g, i));
    load_effect_set(d, 0);
}

/* ---- Instruments ------------------------------------------------------------------------ */

/* An instrument's tuning and envelope are a word each in the two tables, by
 * twice its number. There are 64 of each, and the index wraps at 256 bytes:
 * the tunings of the instruments past 64 are the envelopes, and their
 * envelopes run on into the song. The envelope is the S-DSP's two ADSR bytes,
 * the first in the low byte. */
enum { INSTRUMENT_TUNINGS = SOUND_INSTRUMENT_TABLES, INSTRUMENT_ENVELOPES = SOUND_INSTRUMENT_TABLES + 0x80 };

// SPC $0276
void store_instrument(SoundDriver *d, uint8_t instrument, uint16_t tuning, uint16_t envelope)
{
    uint8_t at = (uint8_t)(2 * instrument);
    uint8_t *tunings = &d->instrument_tables[INSTRUMENT_TUNINGS - SOUND_INSTRUMENT_TABLES];
    uint8_t *envelopes = &d->instrument_tables[INSTRUMENT_ENVELOPES - SOUND_INSTRUMENT_TABLES];
    tunings[at] = (uint8_t)tuning;
    tunings[at + 1] = (uint8_t)(tuning >> 8);
    envelopes[at] = (uint8_t)envelope;
    envelopes[at + 1] = (uint8_t)(envelope >> 8);
}

// SPC $10EC
void sound_load_instrument(SoundDriver *d, uint8_t instrument, uint16_t block)
{
    store_instrument(d, instrument, sound_instrument_tuning(d->game, block), sound_instrument_envelope(d->game, block));
}

// SPC $1ACF
static AuEnvelope envelope_of(uint16_t adsr)
{
    return (AuEnvelope){ .adsr = adsr & 0x80,
                         .attack = adsr & 0x0F,
                         .decay = (adsr >> 4) & 7,
                         .sustain_level = (adsr >> 13) & 7,
                         .sustain_rate = (adsr >> 8) & 0x1F };
}

// SPC $1B5F
AuEnvelope instrument_envelope(SoundDriver *d, uint8_t instrument)
{
    return envelope_of(sound_memory_word(d, (uint16_t)(INSTRUMENT_ENVELOPES + (uint8_t)(2 * instrument))));
}

// SPC $1ACF
void set_instrument(SoundDriver *d, uint8_t track, uint8_t instrument)
{
    SoundTrack *t = &d->song.tracks[track];
    t->instrument = instrument;
    t->tuning = sound_memory_word(d, (uint16_t)(INSTRUMENT_TUNINGS + (uint8_t)(2 * instrument)));
    t->envelope = instrument_envelope(d, instrument);
}

// SPC $1B0C
/* The envelope goes on the voice unless an effect has taken it from the
 * music. */
void put_envelope_on_voice(SoundDriver *d, uint8_t track)
{
    if (is_music(track) && (d->effect_voices & voice_bit(track)))
        return;
    au_voice_envelope(d->voices, voice_of(track), d->song.tracks[track].envelope);
}

// SPC $1AED
void put_instrument_on_voice(SoundDriver *d, uint8_t track)
{
    au_voice_sample(d->voices, voice_of(track), d->song.tracks[track].instrument);
    put_envelope_on_voice(d, track);
}

/* ---- Echo, noise and pitch modulation voices -------------------------------------------------- */

// SPC $1A21, $1A48, $1AAA
/* The music's voices the effects haven't taken, and the effects' own. */
static AuVoiceSet music_and_effects(const SoundDriver *d, AuVoiceSet music, AuVoiceSet effects)
{
    return (AuVoiceSet)((music & ~d->effect_voices) | effects);
}

// SPC $1A21
void update_echo_voices(SoundDriver *d)
{
    d->echo_voices = music_and_effects(d, d->song.music.echo, d->song.effects.echo);
}

// SPC $1AAA
void update_pitch_modulation(SoundDriver *d)
{
    d->pitch_modulation_voices = music_and_effects(d, d->song.music.pitch_modulation, d->song.effects.pitch_modulation);
}

// SPC $1A48
/* One noise clock plays: the effects' while any effect plays noise. */
void update_noise(SoundDriver *d)
{
    const SoundSong *s = &d->song;
    d->noise_clock = s->effects.noise ? s->effect_noise_clock : s->music_noise_clock;
    d->noise_voices = music_and_effects(d, s->music.noise, s->effects.noise);
}

// SPC $1D4C
/* A track that ends lets go of what it set: the song forgets it started once
 * its last track ends (so it can be started again), and an effect gives its
 * voice back to the music. */
void release_track(SoundDriver *d, uint8_t track)
{
    AuVoiceSet voice = voice_bit(track);
    SoundSong *s = &d->song;
    if (is_music(track)) {
        if (s->music_tracks == voice)
            d->last_song_started = NO_SONG;
        s->music_tracks &= (AuVoiceSet)~voice;
    } else {
        d->effect_voices &= (AuVoiceSet)~voice;
        s->music.slurring &= (AuVoiceSet)~voice;
    }
    SoundVoiceFlags *f = flags_of(d, track);
    f->noise &= (AuVoiceSet)~voice;
    update_noise(d);
    f->pitch_modulation &= (AuVoiceSet)~voice;
    update_pitch_modulation(d);
    f->echo &= (AuVoiceSet)~voice;
    update_echo_voices(d);
}

/* ---- Stopping ----------------------------------------------------------------------------- */

// SPC $0F17
void stop_music(SoundDriver *d)
{
    SoundSong *s = &d->song;
    AuVoiceSet music = (AuVoiceSet)~d->effect_voices;
    s->key_off |= music;
    s->key_on &= (AuVoiceSet)~music;
    d->echo_voices &= (AuVoiceSet)~music;
    d->pitch_modulation_voices &= (AuVoiceSet)~music;
    d->noise_voices &= (AuVoiceSet)~music;
    s->music_tracks = 0;
    s->music.echo = s->music.pitch_modulation = s->music.noise = 0;
    d->last_song_started = d->song_id = d->saved_song_id = NO_SONG;
}

// SPC $0F3F, $0F71
/* The pair's voices are released at once; their tracks end at their next
 * step, which gives the voices back. */
void stop_effects(SoundDriver *d, AuVoiceSet pair)
{
    SoundSong *s = &d->song;
    AuVoiceSet playing = d->effect_voices & pair;
    s->key_off |= playing;
    s->key_on &= (AuVoiceSet)~playing;
    s->effects.sounding &= (AuVoiceSet)~playing;
    d->volume_changed &= (AuVoiceSet)~playing;
    d->pitch_changed &= (AuVoiceSet)~playing;
    for (uint8_t voice = 0; voice < AU_VOICES; voice++) {
        if (!(playing & 1u << voice))
            continue;
        SoundTrack *t = &s->tracks[effect_track_of(voice)];
        t->at = SILENT_TRACK;
        t->ticks_left = 2;
    }
}

/* ---- The echo's delay ------------------------------------------------------------------------ */

// SPC $1352
/* The echo is silenced and its writes stopped while its buffer changes size:
 * the S-DSP goes on with the old delay until its buffer wraps, and writing it
 * from the new start would run past the buffer's end. The original then
 * waits, the old delay's time for that, and lets the echo write again; then
 * the new delay's for the buffer to fill with what is playing, before a song
 * takes its echo. A host that waits too (echo_waits) says when with
 * sound_echo_drained and sound_echo_ready; otherwise the echo writes again at
 * once. */
void change_echo_delay(SoundDriver *d, uint8_t delay)
{
    au_echo_writes(d->voices, false);
    au_echo_voices(d->voices, 0);
    au_echo_feedback(d->voices, 0);
    au_echo_volume(d->voices, 0, 0);
    au_echo_delay(d->voices, delay);
    d->echo_draining = d->echo_waits;
    if (!d->echo_draining)
        au_echo_writes(d->voices, true);
}

/* ---- Songs ------------------------------------------------------------------------------ */

enum {
    SONG_HEADER_BASE = 0,       /* the address the song was built for, less the header */
    SONG_HEADER_NO_TRACK = 2,
    SONG_HEADER_TRACKS = 4,     /* two sets of eight */
    SONG_HEADER_BYTES = 0x24
};

// SPC $13A9
static void save_song(SoundDriver *d)
{
    d->saved_song_id = d->song_id;
    d->saved = d->song;
}

/* A resumed song fades back in over 16 steps. */
enum { RESUME_FADE_STEPS = 16, RESUME_FADE_FROM = 0x10, RESUME_FADE_STEP = 0x0EF0 };

// SPC $13E5
static void restore_song(SoundDriver *d)
{
    d->saved_song_id = NO_SONG;
    d->song = d->saved;
    d->fades[FADE_MUSIC].volume = (SoundSlide){ .level = RESUME_FADE_FROM << 8, .step = RESUME_FADE_STEP, .steps_left = RESUME_FADE_STEPS };
}

// SPC $0C1B
/* Take the song up where it was saved: every music voice is set up again,
 * silent, with its instrument and pitch, and the notes that were sounding are
 * keyed on at the next tick. */
static void resume_song(SoundDriver *d)
{
    SoundSong *s = &d->song;
    d->noise_clock = s->music_noise_clock;
    for (uint8_t track = 0; track < SOUND_MUSIC_TRACKS; track++) {
        s->tracks[track].ticks_left++;
        put_instrument_on_voice(d, track);
        au_voice_volume(d->voices, voice_of(track), 0, 0);
        put_pitch_on_voice(d, track);
    }
    s->effects.echo = s->effects.noise = s->effects.pitch_modulation = 0;
    s->key_on = s->music.sounding;
    s->key_off = 0;
    s->effects.sounding = 0;
    d->volume_changed = s->music_tracks;
}

/* Start the song's tracks of one set from their beginnings; the others keep
 * what they had but don't play. */
/* A song starts at the slowest tempo (its first event sets its own) with its
 * first beat due, so its tracks step at the first tick; each at its full
 * volume scale. */
enum { SLOWEST_TEMPO = 1, BEAT_DUE = 0xFF, FULL_SCALE = 0xFF };

// SPC $0C5E
static void begin_song(SoundDriver *d, uint8_t track_set)
{
    SoundSong *s = &d->song;
    s->music_tracks = 0;
    s->music.echo = s->effects.echo = s->music.noise = s->effects.noise = 0;
    s->music.pitch_modulation = s->effects.pitch_modulation = 0;
    s->key_on = s->key_off = 0;
    s->music.sounding = s->effects.sounding = 0;
    s->music.held = s->music.slur = s->music.roll = 0;
    s->percussion = 0;
    s->marker = 0;
    slide_set_whole(&s->tempo, SLOWEST_TEMPO);
    s->music_beat = BEAT_DUE;

    uint16_t header = SOUND_SEQUENCES;
    for (uint8_t track = 0; track < SOUND_MUSIC_TRACKS; track++)
        s->tracks[track].at =
            sound_memory_word(d, (uint16_t)(header + SONG_HEADER_TRACKS + 16 * track_set + 2 * track));
    s->relocation = (uint16_t)(header + SONG_HEADER_BYTES - sound_memory_word(d, header + SONG_HEADER_BASE));
    uint16_t no_track = sound_memory_word(d, header + SONG_HEADER_NO_TRACK);
    for (uint8_t track = SOUND_MUSIC_TRACKS; track-- > 0;) {
        SoundTrack *t = &s->tracks[track];
        if (t->at == no_track)
            continue;
        s->music_tracks |= voice_bit(track);
        t->at = (uint16_t)(t->at + s->relocation);
        reset_track(t);
        t->volume_scale = FULL_SCALE;
    }
    d->volume_changed = 0;
}

// SPC $0D29, $0BD8-$0C05
/* A song's command comes before its data: the song playing is saved if the
 * command asks, the music stops, and a new echo delay moves the echo's buffer
 * at once, with its writes stopped, since the song's samples are loaded next
 * and may lie where the old, longer buffer was. The song the driver started
 * last is refused (false). */
bool announce_song(SoundDriver *d, uint8_t song, uint8_t volume, uint8_t echo_delay, bool save_first)
{
    bool again = song == d->last_song_started;
    d->last_song_started = song;
    if (again)
        return false;
    if (save_first)
        save_song(d);
    au_voices_key_off(d->voices, ALL_VOICES);
    d->song_id = song;
    d->song.volume = volume;
    echo_delay &= 0x0F;
    d->song_echo_changed = echo_delay != d->echo_delay;
    if (d->song_echo_changed) {
        d->echo_delay = echo_delay;
        change_echo_delay(d, echo_delay);
    }
    return true;
}

// SPC $0C08
/* The song announced, once its data is in: its tracks start from their
 * beginnings, or from where it was saved. */
void start_song(SoundDriver *d, uint8_t track_set)
{
    bool echo_changed = d->song_echo_changed;
    d->song_echo_changed = false;
    load_song(d, d->song_id);

    d->effect_voices = 0;
    d->stepping_every_tick = false;
    d->condition = d->highest_condition = 0;
    d->pitch_changed = 0;
    bool resuming = false;
    if (d->saved_song_id == d->song_id) {
        restore_song(d);
        resuming = d->song.music_tracks != 0;
    }
    if (resuming)
        resume_song(d);
    else
        begin_song(d, track_set);
    if (echo_changed && d->echo_waits)
        d->song_voices_waiting = true;
    else
        take_song_voices(d);
}

// SPC $0CE5
/* The song's echo, noise and pitch modulation voices: the music's, whatever
 * the effects have (they have none when a song starts). */
void take_song_voices(SoundDriver *d)
{
    d->echo_voices = d->song.music.echo;
    d->noise_voices = d->song.music.noise;
    d->pitch_modulation_voices = d->song.music.pitch_modulation;
}

/* ---- Sound effects ---------------------------------------------------------------------- */

/* An effect starts on its tracks at the volume, octave and instrument every
 * effect starts with, panned where the game asks. */
enum { EFFECT_VOLUME = 0x46, EFFECT_OCTAVE = 6 * 12, EFFECT_INSTRUMENT = 2 };

// SPC $0D7E
static void begin_effect_track(SoundDriver *d, uint8_t track, uint16_t at, uint8_t pan)
{
    SoundTrack *t = &d->song.tracks[track];
    t->at = at;
    reset_track(t);
    t->ticks_left++;
    slide_set_whole(&t->volume, EFFECT_VOLUME);
    slide_set_whole(&t->pan, pan);
    t->octave = EFFECT_OCTAVE;
    t->volume.steps_left = t->pan.steps_left = 0;
    set_instrument(d, track, EFFECT_INSTRUMENT);
}

/* The effect's entry holds two track addresses, high byte last; they are
 * read from the end, and a high byte of 0 is passed over, so an effect with
 * one track gives it in either half. */
enum { EFFECT_ENTRY_BYTES = 4 };

// SPC $0D5B
void start_effect(SoundDriver *d, uint8_t effect, uint8_t pan, AuVoiceSet pair)
{
    if (d->paused)
        return;
    SoundSong *s = &d->song;
    uint16_t entry = (uint16_t)(SOUND_EFFECTS + EFFECT_ENTRY_BYTES * effect);
    uint8_t track = effect_track_of(pair == EFFECT_PAIR_67 ? 7 : 5);
    AuVoiceSet started = 0;
    /* The byte index counts down and stops once it wraps past 0; a high byte
     * found at 0 takes its low byte from 255 bytes on, as the original's does. */
    for (uint8_t i = EFFECT_ENTRY_BYTES - 1; i < 0x80; i--) {
        uint8_t high = sound_memory_byte(d, (uint16_t)(entry + i));
        if (!high)
            continue;
        i--;
        uint8_t low = sound_memory_byte(d, (uint16_t)(entry + i));
        begin_effect_track(d, track, (uint16_t)(high << 8 | low), pan);
        started |= voice_bit(track);
        track--;
    }

    AuVoiceSet cut_off = (AuVoiceSet)((d->effect_voices & pair) | started);
    s->key_on &= (AuVoiceSet)~cut_off;
    s->effects.held &= (AuVoiceSet)~cut_off;
    s->effects.slur &= (AuVoiceSet)~cut_off;
    s->effects.roll &= (AuVoiceSet)~cut_off;
    d->volume_changed &= (AuVoiceSet)~cut_off;
    d->pitch_changed &= (AuVoiceSet)~cut_off;
    s->key_off |= cut_off;
    s->effects.sounding &= (AuVoiceSet)~cut_off;
    for (uint8_t voice = AU_VOICES; voice-- > 4;)
        if (cut_off & 1u << voice)
            release_track(d, effect_track_of(voice));

    d->effect_voices |= started;
    d->echo_voices &= (AuVoiceSet)~started;
    d->pitch_modulation_voices &= (AuVoiceSet)~started;
    d->noise_voices &= (AuVoiceSet)~started;
    s->music.sounding &= (AuVoiceSet)~started;
}
