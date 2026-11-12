/* The tracks: stepping through their streams, playing notes, and the events
 * that shape them.
 *
 * A track's stream is bytes. One below $A8 is a note: its pitch, one of the
 * twelve semitones of the track's octave, and its length, one of fourteen,
 * packed together (pitch x 14 + length). $A8-$B5 is a tie, which holds the
 * note sounding for another length, and $B6-$C3 a rest; $C4 and up are events
 * (the table at the end), each with its operands. A track waits out a note's
 * length in ticks of the song's tempo, then reads events up to its next note.
 *
 * Before a note is released the driver looks ahead in the stream: if what
 * comes next is a tie, or the note is slurred or rolled into the next one, it
 * is held and not released before the next starts. The look-ahead follows the
 * stream's jumps and loops without taking them.
 *
 * The music's tracks step when the tempo's beat comes round, the effects'
 * at a fixed rate of their own. Each step also moves the track's volume, pan
 * and pitch slides and its pan sweep on, and counts down the delays before a
 * vibrato or tremolo starts. */
#include "core/sound_data.h"
#include "sound/driver.h"

enum {
    FIRST_TIE = 0xA8,
    FIRST_REST = 0xB6,
    FIRST_EVENT = 0xC4,
    NOTE_LENGTHS = 14,
    RELEASE_TICKS = 2,          /* a note not held is released this many ticks before it ends */
    NOTE_BELOW_OCTAVE = 10,     /* the first note of an octave is this many semitones into it */
    EFFECT_BEAT = 0x81          /* the effects' tempo */
};

typedef enum { TRACK_GOES_ON, TRACK_ENDED } TrackFlow;

static SoundTrack *track_of(SoundDriver *d, uint8_t track) { return &d->song.tracks[track]; }

// SPC $0753
static uint8_t next_byte(SoundDriver *d, SoundTrack *t)
{
    uint8_t b = sound_memory_byte(d, t->at);
    t->at++;
    return b;
}

/* An address in the song's stream: relocated to where the song is. */
static uint16_t in_song(const SoundDriver *d, uint16_t address) { return (uint16_t)(address + d->song.relocation); }

/* A track's vibrato takes its depth from its note: until the first, this. */
enum { DEPTH_UNTIL_A_NOTE = 0x1000 };

// SPC $0CF6
void reset_track(SoundTrack *t)
{
    t->loop_level = 0;
    t->slide_semitones = 0;
    t->slide_step = 0;
    t->vibrato.depth = 0;
    t->tremolo.depth = 0;
    t->pan_sweep = 0;
    t->pan_sweep_phase = 0;
    t->detune = 0;
    t->transpose = 0;
    t->vibrato_depth = DEPTH_UNTIL_A_NOTE;
    t->ticks_left = 1;
}

/* ---- Pitch -------------------------------------------------------------------------------- */

/* A note's pitch: its semitone's pitch in the note's octave, tuned by the
 * instrument. The tuning's low byte (with the track's detune) is a fraction
 * the pitch is raised by, or with its top bit set the fraction it is taken to;
 * the high byte raises it further, by 1/65536ths. Octave 4 is the table's. */
enum { FRACTION_TAKEN_TO = 0x80, SLIDE_STEP_BITS = 0xFFFFFF };

// SPC $06BD
uint16_t pitch_of_note(const SoundDriver *d, const SoundTrack *t, uint8_t note)
{
    uint8_t octave = note / 12;
    uint16_t base = d->semitone_pitch[note % 12];
    uint8_t fraction = (uint8_t)(t->tuning + t->detune);
    uint16_t pitch = (uint16_t)(((uint32_t)base * fraction) >> 8);
    uint8_t fine = (uint8_t)(t->tuning >> 8);
    if (fine)
        pitch = (uint16_t)(pitch + (((pitch >> 8) * fine) >> 8));
    if (!(fraction & FRACTION_TAKEN_TO))
        pitch = (uint16_t)(pitch + base);
    if (octave >= 4)
        return octave - 4 >= 16 ? 0 : (uint16_t)(pitch << (octave - 4));
    return (uint16_t)(pitch >> (4 - octave));
}

// SPC $064B
/* A pitch slide set up by event $C8 runs from the note just started to the
 * one `slide_semitones` away, over the ticks it gave, as a 16.8 step. */
static void begin_pitch_slide(SoundDriver *d, SoundTrack *t)
{
    t->note = (uint8_t)(t->note + t->slide_semitones);
    uint16_t target = pitch_of_note(d, t, t->note);
    bool up = target >= t->pitch;
    uint16_t distance = up ? (uint16_t)(target - t->pitch) : (uint16_t)(t->pitch - target);
    uint32_t step = t->slide_ticks ? ((uint32_t)distance << 8) / t->slide_ticks : distance;
    t->slide_step = (up ? step : (uint32_t)-step) & SLIDE_STEP_BITS;
    t->slide_semitones = 0;
    t->pitch_fraction = 0;
}

/* ---- The look-ahead -------------------------------------------------------------------------- */

/* Where the look-ahead is: a place in the stream, and a copy of the track's
 * loop level, which it moves without touching the loops. */
typedef struct {
    uint16_t at;
    uint8_t loop_level;
} Cursor;

typedef enum {
    LOOK_PAST,              /* step over the event and its operands */
    LOOK_ARTICULATION,      /* slur, roll or instrument: the note is not carried into the next */
    LOOK_LOOP_END,
    LOOK_LOOP_EXIT,
    LOOK_JUMP,
    LOOK_CONDITION,
    LOOK_EFFECT_TRACK
} LookAhead;

static uint8_t previous_level(uint8_t level) { return (uint8_t)((level - 1) & (SOUND_LOOP_LEVELS - 1)); }
static uint8_t next_level(uint8_t level) { return (uint8_t)((level + 1) & (SOUND_LOOP_LEVELS - 1)); }

/* A loop is on its last pass when one pass is left of a counted loop. */
static bool last_pass(const SoundLoop *l) { return l->counts_left == 1; }

/* Where a loop's end leads: back to its start, unless the pass was its last
 * (its level is then left). */
static bool loop_goes_back(const SoundLoop *l) { return l->counts_left == 0 || !last_pass(l); }

// SPC $1BBC
/* One of an effect's two tracks, by the effect set's table; 0 if it has none
 * (its high byte is 0). */
static uint16_t effect_track_address(SoundDriver *d, uint8_t effect, uint8_t which)
{
    return sound_memory_word(d, (uint16_t)(SOUND_EFFECTS + 4 * effect + 2 * which));
}

static bool no_track(uint16_t address) { return (address >> 8) == 0; }

// SPC $1B86
static void clear_articulation(SoundDriver *d, uint8_t track)
{
    SoundVoiceFlags *f = flags_of(d, track);
    f->slur &= (AuVoiceSet)~voice_bit(track);
    f->roll &= (AuVoiceSet)~voice_bit(track);
}

typedef struct EventRow EventRow;
static const EventRow *event_row(uint8_t event);
static uint8_t event_operands(const EventRow *row);
static LookAhead event_look_ahead(const EventRow *row);
static uint8_t event_effect_track(const EventRow *row);

// SPC $075E, $1B86, $1BD9, $1BDD, $1BFC, $1C39, $1CC9, $1D2A
/* What comes after the note: the next note, tie or rest, or END_OF_TRACK. */
static uint8_t look_ahead(SoundDriver *d, uint8_t track)
{
    SoundTrack *t = track_of(d, track);
    Cursor c = { t->at, t->loop_level };
    for (;;) {
        uint8_t b = sound_memory_byte(d, c.at);
        if (b < FIRST_EVENT)
            return b;
        c.at++;
        if (b == END_OF_TRACK)
            return b;
        const EventRow *row = event_row(b);
        SoundLoop *l = &t->loops[c.loop_level];
        switch (event_look_ahead(row)) {
        case LOOK_PAST:
            c.at = (uint16_t)(c.at + event_operands(row));
            break;
        case LOOK_ARTICULATION:
            /* its operand, if it has one, is then read as the next note */
            clear_articulation(d, track);
            break;
        case LOOK_LOOP_END:
            if (loop_goes_back(l))
                c.at = l->start;
            else
                c.loop_level = previous_level(c.loop_level);
            break;
        case LOOK_LOOP_EXIT:
            if (l->passes != sound_memory_byte(d, c.at)) {
                c.at = (uint16_t)(c.at + 3);
                break;
            }
            if (last_pass(l))
                c.loop_level = previous_level(c.loop_level);
            c.at = in_song(d, sound_memory_word(d, (uint16_t)(c.at + 1)));
            break;
        case LOOK_JUMP:
            c.at = in_song(d, sound_memory_word(d, c.at));
            break;
        case LOOK_CONDITION:
            /* the byte as it is: the branch itself masks it to a nibble */
            if (d->condition >= sound_memory_byte(d, c.at))
                c.at = in_song(d, sound_memory_word(d, (uint16_t)(c.at + 1)));
            else
                c.at = (uint16_t)(c.at + 3);
            break;
        case LOOK_EFFECT_TRACK: {
            uint16_t address = effect_track_address(d, sound_memory_byte(d, c.at), event_effect_track(row));
            if (no_track(address))
                return END_OF_TRACK;
            c.at = address;
            break;
        }
        }
    }
}

/* ---- Notes ------------------------------------------------------------------------------ */

/* A percussion entry: an instrument, the note it plays at, and a pan unless
 * its top bit says to keep the track's. */
enum { KEEP_PAN = 0x80 };

// SPC $0720
static void play_percussion(SoundDriver *d, uint8_t track, uint8_t entry, uint8_t *note)
{
    SoundTrack *t = track_of(d, track);
    uint16_t at = (uint16_t)(SOUND_PERCUSSION + (uint8_t)(3 * entry));
    uint8_t pan = sound_memory_byte(d, (uint16_t)(at + 2));
    if (!(pan & KEEP_PAN))
        slide_set_whole(&t->pan, (uint8_t)(pan << 1));
    *note = sound_memory_byte(d, (uint16_t)(at + 1));
    set_instrument(d, track, sound_memory_byte(d, at));
}

// SPC $05C1
/* Whether the note keys its voice on: a roll always does; a slur only for
 * its first note. */
static bool keys_on(SoundVoiceFlags *f, AuVoiceSet voice)
{
    if (f->roll & voice)
        return true;
    if (!(f->slur & voice))
        return true;
    if (f->slurring & voice)
        return false;
    f->slurring |= voice;
    return true;
}

// SPC $052F
static void start_note(SoundDriver *d, uint8_t track, uint8_t semitone)
{
    SoundTrack *t = track_of(d, track);
    AuVoiceSet voice = voice_bit(track);
    uint8_t note = (uint8_t)(t->octave + semitone);
    if (is_music(track) && (d->song.percussion & voice))
        play_percussion(d, track, semitone, &note);
    t->note = (uint8_t)(note + t->transpose - NOTE_BELOW_OCTAVE);
    t->pitch = pitch_of_note(d, t, t->note);

    if (t->vibrato.depth) {
        restart_vibrato(d, track);
        uint8_t scale = d->vibrato_scale[is_music(track) ? 0 : t->vibrato_scale];
        t->vibrato_depth = scale ? (uint16_t)(((uint32_t)t->pitch * scale) >> 8) : t->pitch;
    }
    if (t->tremolo.depth)
        restart_tremolo(d, track);
    t->vibrato_offset = 0;
    t->tremolo.phase = 0;
    t->slide_step = 0;

    SoundVoiceFlags *f = flags_of(d, track);
    f->sounding |= voice;
    if (is_music(track) && (d->effect_voices & voice))
        return;
    if (keys_on(f, voice))
        d->song.key_on |= voice;
    d->volume_changed |= voice;
    d->pitch_changed |= voice;
    put_instrument_on_voice(d, track);
}

// SPC $0601
/* After a note or tie: whether it is held into what comes next, and a pitch
 * slide set up for it. */
static void follow_note(SoundDriver *d, uint8_t track)
{
    SoundTrack *t = track_of(d, track);
    SoundVoiceFlags *f = flags_of(d, track);
    AuVoiceSet voice = voice_bit(track);
    uint8_t next = look_ahead(d, track);
    if (next >= FIRST_REST) {
        f->slur &= (AuVoiceSet)~voice;
        f->roll &= (AuVoiceSet)~voice;
        f->held &= (AuVoiceSet)~voice;
    } else if (next >= FIRST_TIE) {
        f->held |= voice;
    } else {
        f->held = (AuVoiceSet)((f->held & ~voice) | ((f->slur | f->roll) & voice));
    }
    if (t->slide_semitones)
        begin_pitch_slide(d, t);
}

// SPC $0513
/* A note, tie or rest: its length (unless event $E8 gave the next one), and
 * what it does. */
static void play(SoundDriver *d, uint8_t track, uint8_t b)
{
    SoundTrack *t = track_of(d, track);
    if (!t->ticks_left)
        t->ticks_left = d->note_length[b % NOTE_LENGTHS];
    if (b >= FIRST_REST)
        return;
    if (b < FIRST_TIE)
        start_note(d, track, b / NOTE_LENGTHS);
    follow_note(d, track);
}

/* ---- Stepping ----------------------------------------------------------------------------- */

static TrackFlow run_event(SoundDriver *d, uint8_t track, uint8_t event);

// SPC $0507
/* Read events up to the track's next note, and play it. */
static TrackFlow step_track(SoundDriver *d, uint8_t track)
{
    for (;;) {
        uint8_t b = next_byte(d, track_of(d, track));
        if (b < FIRST_EVENT) {
            play(d, track, b);
            return TRACK_GOES_ON;
        }
        if (run_event(d, track, b) == TRACK_ENDED)
            return TRACK_ENDED;
    }
}

// SPC $07AF
/* The slides a step moves on. */
static void slide_track(SoundDriver *d, uint8_t track)
{
    SoundTrack *t = track_of(d, track);
    AuVoiceSet voice = voice_bit(track);
    if (t->vibrato.delay_left)
        t->vibrato.delay_left--;
    if (t->tremolo.delay_left)
        t->tremolo.delay_left--;
    if (slide_advance(&t->volume))
        d->volume_changed |= voice;
    if (slide_advance(&t->pan))
        d->volume_changed |= voice;
    if (t->slide_step) {
        uint32_t step = t->slide_step;
        if (--t->slide_ticks == 0)
            t->slide_step = 0;
        uint32_t pitch = ((uint32_t)t->pitch << 8 | t->pitch_fraction) + step;
        t->pitch = (uint16_t)(pitch >> 8);
        t->pitch_fraction = (uint8_t)pitch;
        d->pitch_changed |= voice;
    }
    if (t->pan_sweep) {
        uint8_t before = (uint8_t)(t->pan_sweep_phase >> 8);
        t->pan_sweep_phase = (uint16_t)(t->pan_sweep_phase + t->pan_sweep_step);
        if ((uint8_t)(t->pan_sweep_phase >> 8) != before)
            d->volume_changed |= voice;
        if (--t->pan_sweep_ticks_left == 0) {
            t->pan_sweep_step = (uint16_t)-t->pan_sweep_step;
            t->pan_sweep_ticks_left = t->pan_sweep_period;
        }
    }
}

// SPC $03A3
/* A track's tick when its part of the song steps: a note that isn't held is
 * released a little before its end, and one that has ended is followed by
 * the next. */
static void advance_track(SoundDriver *d, uint8_t track)
{
    SoundTrack *t = track_of(d, track);
    if (--t->ticks_left == 0) {
        if (step_track(d, track) == TRACK_ENDED)
            return;
    } else if (t->ticks_left == RELEASE_TICKS) {
        SoundVoiceFlags *f = flags_of(d, track);
        AuVoiceSet voice = voice_bit(track);
        if (!(f->held & voice)) {
            f->sounding &= (AuVoiceSet)~voice;
            if (!is_music(track) || !(d->effect_voices & voice))
                d->song.key_off |= voice;
        }
    }
    slide_track(d, track);
}

// SPC $0377
/* The music's beat: the tempo, bent by the tempo modifier. */
static uint8_t music_beat(const SoundDriver *d)
{
    uint8_t tempo = slide_whole(&d->song.tempo);
    uint8_t modifier = slide_whole(&d->tempo_modifier);
    if (!modifier)
        return tempo;
    uint8_t part = (uint8_t)((tempo * modifier) >> 8);
    if (modifier & 0x80)
        return part ? part : 1;
    uint16_t faster = (uint16_t)(2 * part + tempo);
    return faster > 0xFF ? 0xFF : (uint8_t)faster;
}

// SPC $0377
static bool music_steps(SoundDriver *d)
{
    uint16_t beat = (uint16_t)(d->song.music_beat + music_beat(d));
    d->song.music_beat = (uint8_t)beat;
    return beat > 0xFF;
}

// SPC $0399
static void step_music_tracks(SoundDriver *d)
{
    AuVoiceSet playing = d->song.music_tracks;
    for (uint8_t track = 0; track < SOUND_MUSIC_TRACKS; track++)
        if (playing & voice_bit(track))
            advance_track(d, track);
}

// SPC $0377
void sequence(SoundDriver *d)
{
    if (d->stepping_every_tick || music_steps(d)) {
        step_music_tracks(d);
        slide_song_settings(d);
        d->volume_changed &= (AuVoiceSet)~d->effect_voices;
        d->pitch_changed &= (AuVoiceSet)~d->effect_voices;
    }
    if (!d->stepping_every_tick) {
        uint16_t beat = (uint16_t)(d->song.effect_beat + EFFECT_BEAT);
        d->song.effect_beat = (uint8_t)beat;
        if (beat <= 0xFF)
            return;
    }
    AuVoiceSet playing = d->effect_voices;
    for (uint8_t voice = 4; voice < AU_VOICES; voice++)
        if (playing & 1u << voice)
            advance_track(d, effect_track_of(voice));
}

// SPC $0464
void sequence_music_while_loading(SoundDriver *d)
{
    if (!music_steps(d))
        return;
    step_music_tracks(d);
    slide_song_settings(d);
}

/* ---- Events ----------------------------------------------------------------------------- */

/* Envelope parts events $DD-$E0 set. */
typedef enum { ENVELOPE_ATTACK, ENVELOPE_DECAY, ENVELOPE_SUSTAIN_LEVEL, ENVELOPE_SUSTAIN_RATE } EnvelopePart;

// SPC $0E5F, called from $1732, $1770, $179A, $17CE, $1805, $1826
/* Head for `target` over `steps` steps from where the slide is, its
 * fraction and all; a slide already there stops. */
static void slide_toward(SoundSlide *s, uint8_t steps, uint8_t target, Signedness values)
{
    s->steps_left = target == slide_whole(s) ? 0 : steps;
    if (s->steps_left)
        s->step = slide_step(slide_whole(s), target, steps, values);
}

enum { LOUDEST = 0x7F };

// SPC $175F, $1770
/* Volume, 0-127: now, or sliding there over `steps` steps. */
static TrackFlow volume(SoundDriver *d, uint8_t track, uint8_t steps, uint8_t level)
{
    SoundTrack *t = track_of(d, track);
    level &= LOUDEST;
    if (steps) {
        slide_toward(&t->volume, steps, level, UNSIGNED);
        return TRACK_GOES_ON;
    }
    t->volume = (SoundSlide){ .level = (uint16_t)(level << 8), .step = t->volume.step };
    d->volume_changed |= voice_bit(track);
    return TRACK_GOES_ON;
}

// SPC $17BA
/* Pan, 0 left to 127 right in the stream (0-254 as the driver keeps it):
 * the music's tracks only, since an effect's pan is where the game started
 * it. */
static TrackFlow set_pan(SoundDriver *d, uint8_t track, uint8_t pan)
{
    if (!is_music(track))
        return TRACK_GOES_ON;
    SoundTrack *t = track_of(d, track);
    t->pan = (SoundSlide){ .level = (uint16_t)((uint8_t)(pan << 1) << 8), .step = t->pan.step };
    d->volume_changed |= voice_bit(track);
    return TRACK_GOES_ON;
}

// SPC $17CE
/* A pan slide. On an effect's track it only sets how many steps the slide it
 * already had takes (the original's). */
static TrackFlow slide_pan(SoundDriver *d, uint8_t track, uint8_t steps, uint8_t pan)
{
    SoundTrack *t = track_of(d, track);
    if (!is_music(track)) {
        t->pan.steps_left = steps;
        return TRACK_GOES_ON;
    }
    if (!steps)
        return set_pan(d, track, pan);
    slide_toward(&t->pan, steps, (uint8_t)(pan << 1), UNSIGNED);
    return TRACK_GOES_ON;
}

// SPC $17F2
/* A pitch slide for the next note, to `semitones` away over `ticks`. */
static TrackFlow slide_pitch(SoundDriver *d, uint8_t track, uint8_t ticks, int8_t semitones)
{
    SoundTrack *t = track_of(d, track);
    t->slide_ticks = (uint8_t)(ticks + 1);
    t->slide_semitones = semitones;
    return TRACK_GOES_ON;
}

// SPC $186E
/* Vibrato: after `delay` steps (music), a wave `depth` deep at `rate` ticks a
 * half wave; an effect's first operand chooses its depth's scale instead. */
static TrackFlow vibrato(SoundDriver *d, uint8_t track, uint8_t delay, uint8_t rate, uint8_t depth)
{
    SoundTrack *t = track_of(d, track);
    if (is_music(track))
        t->vibrato.delay = delay;
    else
        t->vibrato_scale = delay % SOUND_VIBRATO_SCALES;
    t->vibrato.rate = rate;
    t->vibrato.depth = depth;
    t->vibrato.base_step = wave_base_step(rate, depth);
    t->vibrato_offset = 0;
    restart_vibrato(d, track);
    return TRACK_GOES_ON;
}

// SPC $192E
static TrackFlow vibrato_off(SoundDriver *d, uint8_t track)
{
    SoundTrack *t = track_of(d, track);
    t->vibrato.depth = 0;
    t->vibrato_offset = 0;
    return TRACK_GOES_ON;
}

// SPC $1938
static TrackFlow tremolo(SoundDriver *d, uint8_t track, uint8_t delay, uint8_t rate, uint8_t depth)
{
    SoundTrack *t = track_of(d, track);
    t->tremolo.delay = delay;
    t->tremolo.rate = rate;
    t->tremolo.depth = depth;
    t->tremolo.base_step = wave_base_step(rate, depth);
    t->tremolo.phase = 0;
    restart_tremolo(d, track);
    return TRACK_GOES_ON;
}

// SPC $198E
static TrackFlow tremolo_off(SoundDriver *d, uint8_t track)
{
    SoundTrack *t = track_of(d, track);
    t->tremolo.depth = 0;
    t->tremolo.phase = 0;
    return TRACK_GOES_ON;
}

// SPC $1998
/* Pan sweep: the pan swings back and forth, turning every `period` steps (a
 * half of it the first time); `depth`'s top bit starts it to the left. */
static TrackFlow pan_sweep(SoundDriver *d, uint8_t track, uint8_t period, uint8_t depth)
{
    SoundTrack *t = track_of(d, track);
    uint8_t whole = (uint8_t)((period + 1) & 0xFE);
    bool longest = period == 0xFF;
    if (!longest && !whole)
        whole = 2;
    t->pan_sweep_period = whole;
    uint8_t half = longest ? 0x80 : whole >> 1;
    t->pan_sweep_ticks_left = half;
    bool left = depth & 0x80;
    uint8_t swing = depth & 0x7F;
    if (left)
        swing ^= 0x7F;
    uint16_t step = swing;
    if (!(half & 0x80)) {
        step = (uint16_t)(((uint16_t)swing << 8) / half);
        if (!step)
            step = 1;
    }
    t->pan_sweep_step = left ? (uint16_t)-step : step;
    t->pan_sweep = depth;
    t->pan_sweep_phase = 0;
    return TRACK_GOES_ON;
}

// SPC $19EE
static TrackFlow pan_sweep_off(SoundDriver *d, uint8_t track)
{
    SoundTrack *t = track_of(d, track);
    t->pan_sweep = 0;
    t->pan_sweep_phase = 0;
    return TRACK_GOES_ON;
}

// SPC $1A78
/* Noise's clock: set, or (an operand past 31) moved by its low five bits. */
static TrackFlow noise_clock(SoundDriver *d, uint8_t track, uint8_t clock)
{
    uint8_t *own = is_music(track) ? &d->song.music_noise_clock : &d->song.effect_noise_clock;
    uint8_t value = clock & 0x1F;
    if (value != clock)
        value = (uint8_t)((*own + value) & 0x1F);
    *own = value;
    update_noise(d);
    return TRACK_GOES_ON;
}

// SPC $1A3C, $1A68
static TrackFlow noise(SoundDriver *d, uint8_t track, bool on)
{
    SoundVoiceFlags *f = flags_of(d, track);
    f->noise = on ? (AuVoiceSet)(f->noise | voice_bit(track)) : (AuVoiceSet)(f->noise & ~voice_bit(track));
    update_noise(d);
    return TRACK_GOES_ON;
}

/* Pitch modulation: an effect on voice 4 or 6 can't have it. */
enum { EFFECT_VOICES_NOT_MODULATED = 1 << 4 | 1 << 6 };

// SPC $1A98, $1AB5
static TrackFlow pitch_modulation(SoundDriver *d, uint8_t track, bool on)
{
    SoundVoiceFlags *f = flags_of(d, track);
    AuVoiceSet voice = voice_bit(track);
    if (!on)
        f->pitch_modulation &= (AuVoiceSet)~voice;
    else if (is_music(track) || !(voice & EFFECT_VOICES_NOT_MODULATED))
        f->pitch_modulation |= voice;
    update_pitch_modulation(d);
    return TRACK_GOES_ON;
}

// SPC $1A15, $1A2C
static TrackFlow echo(SoundDriver *d, uint8_t track, bool on)
{
    SoundVoiceFlags *f = flags_of(d, track);
    f->echo = on ? (AuVoiceSet)(f->echo | voice_bit(track)) : (AuVoiceSet)(f->echo & ~voice_bit(track));
    update_echo_voices(d);
    return TRACK_GOES_ON;
}

// SPC $1A0E
static TrackFlow set_octave(SoundDriver *d, uint8_t track, uint8_t octave)
{
    track_of(d, track)->octave = (uint8_t)(octave * 12);
    return TRACK_GOES_ON;
}

// SPC $19FA, $1A04
static TrackFlow shift_octave(SoundDriver *d, uint8_t track, int8_t octaves)
{
    SoundTrack *t = track_of(d, track);
    t->octave = (uint8_t)(t->octave + 12 * octaves);
    return TRACK_GOES_ON;
}

// SPC $1801
static TrackFlow set_transpose(SoundDriver *d, uint8_t track, int8_t semitones)
{
    track_of(d, track)->transpose = semitones;
    return TRACK_GOES_ON;
}

// SPC $17FD
static TrackFlow add_transpose(SoundDriver *d, uint8_t track, int8_t semitones)
{
    SoundTrack *t = track_of(d, track);
    t->transpose = (int8_t)(t->transpose + semitones);
    return TRACK_GOES_ON;
}

// SPC $1CF2
static TrackFlow detune(SoundDriver *d, uint8_t track, uint8_t amount)
{
    track_of(d, track)->detune = amount;
    return TRACK_GOES_ON;
}

// SPC $1ACF
static TrackFlow instrument(SoundDriver *d, uint8_t track, uint8_t number)
{
    set_instrument(d, track, number);
    return TRACK_GOES_ON;
}

// SPC $1AFC, $1B2A, $1B3D, $1B4F
/* A part of the envelope: an attack or decay rate turns the ADSR on. */
static TrackFlow envelope_part(SoundDriver *d, uint8_t track, EnvelopePart part, uint8_t value)
{
    AuEnvelope *e = &track_of(d, track)->envelope;
    switch (part) {
    case ENVELOPE_ATTACK:
        e->attack = value & 0x0F;
        e->adsr = true;
        break;
    case ENVELOPE_DECAY:
        e->decay = value & 7;
        e->adsr = true;
        break;
    case ENVELOPE_SUSTAIN_LEVEL:
        e->sustain_level = value & 7;
        break;
    case ENVELOPE_SUSTAIN_RATE:
        e->sustain_rate = value & 0x1F;
        break;
    }
    put_envelope_on_voice(d, track);
    return TRACK_GOES_ON;
}

// SPC $1B5F
/* Back to the instrument's own envelope. */
static TrackFlow instrument_envelope_again(SoundDriver *d, uint8_t track)
{
    SoundTrack *t = track_of(d, track);
    t->envelope = instrument_envelope(d, t->instrument);
    put_envelope_on_voice(d, track);
    return TRACK_GOES_ON;
}

// SPC $1C64
/* A loop around what follows, `count` more times (0 for ever), on the
 * track's next level: there are four, reused in turn. */
static TrackFlow loop_start(SoundDriver *d, uint8_t track, uint8_t count)
{
    SoundTrack *t = track_of(d, track);
    t->loop_level = next_level(t->loop_level);
    SoundLoop *l = &t->loops[t->loop_level];
    l->counts_left = count ? (uint8_t)(count + 1) : 0;
    if (is_music(track))
        l->passes = 1;
    l->start = t->at;
    return TRACK_GOES_ON;
}

// SPC $1C94
static TrackFlow loop_end(SoundDriver *d, uint8_t track)
{
    SoundTrack *t = track_of(d, track);
    SoundLoop *l = &t->loops[t->loop_level];
    if (is_music(track))
        l->passes++;
    if (l->counts_left && --l->counts_left == 0) {
        l->counts_left = 1;
        t->loop_level = previous_level(t->loop_level);
        return TRACK_GOES_ON;
    }
    t->at = l->start;
    return TRACK_GOES_ON;
}

// SPC $1C09
/* Leave for `address` on the loop's pass `pass`, and the loop with it if the
 * pass is its last. */
static TrackFlow loop_exit(SoundDriver *d, uint8_t track, uint8_t pass, uint16_t address)
{
    SoundTrack *t = track_of(d, track);
    SoundLoop *l = &t->loops[t->loop_level];
    if (l->passes != pass)
        return TRACK_GOES_ON;
    if (last_pass(l))
        t->loop_level = previous_level(t->loop_level);
    t->at = in_song(d, address);
    return TRACK_GOES_ON;
}

// SPC $1B72
static TrackFlow slur(SoundDriver *d, uint8_t track)
{
    SoundVoiceFlags *f = flags_of(d, track);
    f->slur |= voice_bit(track);
    f->slurring &= (AuVoiceSet)~voice_bit(track);
    return TRACK_GOES_ON;
}

// SPC $1B9A
static TrackFlow roll(SoundDriver *d, uint8_t track)
{
    flags_of(d, track)->roll |= voice_bit(track);
    return TRACK_GOES_ON;
}

// SPC $12DD, $1B86
/* Slur and roll end by the look-ahead's meeting them (LOOK_ARTICULATION). */
static TrackFlow articulation_ends(SoundDriver *d, uint8_t track)
{
    (void)d;
    (void)track;
    return TRACK_GOES_ON;
}

// SPC $1CEF
static TrackFlow next_length(SoundDriver *d, uint8_t track, uint8_t ticks)
{
    track_of(d, track)->ticks_left = ticks;
    return TRACK_GOES_ON;
}

// SPC $1BA8, $1BAC
/* Carry on in one of an effect's tracks; the track ends if it has none. */
static TrackFlow effect_track(SoundDriver *d, uint8_t track, uint8_t effect, uint8_t which)
{
    SoundTrack *t = track_of(d, track);
    t->at = effect_track_address(d, effect, which);
    if (!no_track(t->at))
        return TRACK_GOES_ON;
    release_track(d, track);
    return TRACK_ENDED;
}

// SPC $1D4A
static TrackFlow end_track(SoundDriver *d, uint8_t track)
{
    release_track(d, track);
    return TRACK_ENDED;
}

/* The tempo: the song's number scaled by 1 + 20/256, the beat's step a
 * tick; set, or reached over `steps` steps. */
enum { TEMPO_SCALE = 20 };
// SPC $1722, $1732
static TrackFlow tempo(SoundDriver *d, uint8_t track, uint8_t steps, uint8_t value)
{
    (void)track;
    SoundSlide *s = &d->song.tempo;
    uint8_t scaled = (uint8_t)(((value * TEMPO_SCALE) >> 8) + value);
    if (steps)
        slide_toward(s, steps, scaled, UNSIGNED);
    else
        *s = (SoundSlide){ .level = (uint16_t)(scaled << 8), .step = s->step };
    return TRACK_GOES_ON;
}

// SPC $1791, $179A
/* The echo's volume, signed: set, or reached over `steps` steps. */
static TrackFlow echo_volume(SoundDriver *d, uint8_t track, uint8_t steps, uint8_t level)
{
    (void)track;
    SoundSlide *s = &d->song.echo_volume;
    if (steps)
        slide_toward(s, steps, level, SIGNED);
    else
        *s = (SoundSlide){ .level = (uint16_t)(level << 8), .step = s->step };
    return TRACK_GOES_ON;
}

// SPC $1751
static TrackFlow song_volume(SoundDriver *d, uint8_t track, uint8_t level)
{
    (void)track;
    d->song.volume = level;
    d->volume_changed |= d->song.music_tracks;
    return TRACK_GOES_ON;
}

// SPC $1BEE
static TrackFlow jump(SoundDriver *d, uint8_t track, uint16_t address)
{
    track_of(d, track)->at = in_song(d, address);
    return TRACK_GOES_ON;
}

// SPC $1805
/* Echo feedback: set (its fraction kept), or reached over `steps` steps. */
static TrackFlow echo_feedback(SoundDriver *d, uint8_t track, uint8_t steps, uint8_t level)
{
    (void)track;
    SoundSlide *s = &d->song.echo_feedback;
    s->steps_left = steps;
    if (steps)
        s->step = slide_step(slide_whole(s), level, steps, SIGNED);
    else
        slide_set_whole(s, level);
    return TRACK_GOES_ON;
}

// SPC $1826
/* One of the four echo filters: set (the fractions kept), or reached over
 * `steps` steps, tap by tap. */
static TrackFlow echo_filter(SoundDriver *d, uint8_t track, uint8_t steps, uint8_t which)
{
    (void)track;
    const int8_t *taps = d->echo_filter[which % SOUND_ECHO_FILTERS];
    d->song.filter_steps_left = steps;
    for (uint8_t tap = 0; tap < SOUND_ECHO_TAPS; tap++) {
        SoundSlide *s = &d->song.filter[tap];
        uint8_t target = (uint8_t)taps[tap];
        if (!steps) {
            slide_set_whole(s, target);
            continue;
        }
        s->level = (uint16_t)(slide_whole(s) << 8);
        s->step = slide_step(slide_whole(s), target, steps, SIGNED);
    }
    return TRACK_GOES_ON;
}

// SPC $1CF6
/* The marker the game waits on to keep a scene in time with the music. */
static TrackFlow marker(SoundDriver *d, uint8_t track, uint8_t value)
{
    (void)track;
    d->song.marker = value & 0x0F;
    return TRACK_GOES_ON;
}

// SPC $1CFB
/* Branch to `address` if the value the game set (command $88) has reached
 * `condition`; the highest condition met or not is shown to the game. */
static TrackFlow branch_on_condition(SoundDriver *d, uint8_t track, uint8_t condition, uint16_t address)
{
    condition &= 0x0F;
    if (condition >= d->highest_condition)
        d->highest_condition = condition;
    if (d->condition >= condition)
        track_of(d, track)->at = in_song(d, address);
    return TRACK_GOES_ON;
}

// SPC $1AC5, $1AC9
static TrackFlow percussion(SoundDriver *d, uint8_t track, bool on)
{
    AuVoiceSet voice = voice_bit(track);
    d->song.percussion = on ? (AuVoiceSet)(d->song.percussion | voice) : (AuVoiceSet)(d->song.percussion & ~voice);
    return TRACK_GOES_ON;
}

// SPC $1757
static TrackFlow volume_scale(SoundDriver *d, uint8_t track, uint8_t scale)
{
    track_of(d, track)->volume_scale = (uint8_t)(scale << 1);
    d->volume_changed |= voice_bit(track);
    return TRACK_GOES_ON;
}

/* ---- The event table ------------------------------------------------------------------------ */

/* An event's parameters, decoded from its operands by its row. */
typedef struct {
    uint8_t steps, level, pan, ticks, delay, rate, depth, period, clock, octave, amount, number, count, effect,
        which, value, pass, condition, scale;
    int8_t semitones, octaves;
    bool on;
    EnvelopePart part;
    uint16_t address;
} EventArgs;

typedef TrackFlow (*EventRun)(SoundDriver *d, uint8_t track, const EventArgs *a);

/* EVENT(op, parameter...): the form of `op` a row calls, which hands it the
 * decoded parameters it names, in that order. */
#define EVENT(op, ...) \
    static TrackFlow op##_event(SoundDriver *d, uint8_t track, const EventArgs *a) { return op(d, track, __VA_ARGS__); }
#define EVENT_NOTHING(op) \
    static TrackFlow op##_event(SoundDriver *d, uint8_t track, const EventArgs *a) { (void)a; return op(d, track); }

EVENT(volume, a->steps, a->level)
EVENT(set_pan, a->pan)
EVENT(slide_pan, a->steps, a->pan)
EVENT(slide_pitch, a->ticks, a->semitones)
EVENT(vibrato, a->delay, a->rate, a->depth)
EVENT_NOTHING(vibrato_off)
EVENT(tremolo, a->delay, a->rate, a->depth)
EVENT_NOTHING(tremolo_off)
EVENT(pan_sweep, a->period, a->depth)
EVENT_NOTHING(pan_sweep_off)
EVENT(noise_clock, a->clock)
EVENT(noise, a->on)
EVENT(pitch_modulation, a->on)
EVENT(echo, a->on)
EVENT(set_octave, a->octave)
EVENT(shift_octave, a->octaves)
EVENT(set_transpose, a->semitones)
EVENT(add_transpose, a->semitones)
EVENT(detune, a->amount)
EVENT(instrument, a->number)
EVENT(envelope_part, a->part, a->value)
EVENT_NOTHING(instrument_envelope_again)
EVENT(loop_start, a->count)
EVENT_NOTHING(loop_end)
EVENT_NOTHING(slur)
EVENT_NOTHING(roll)
EVENT_NOTHING(articulation_ends)
EVENT(next_length, a->ticks)
EVENT(effect_track, a->effect, a->which)
EVENT_NOTHING(end_track)
EVENT(tempo, a->steps, a->value)
EVENT(echo_volume, a->steps, a->level)
EVENT(song_volume, a->level)
EVENT(loop_exit, a->pass, a->address)
EVENT(jump, a->address)
EVENT(echo_feedback, a->steps, a->level)
EVENT(echo_filter, a->steps, a->which)
EVENT(marker, a->value)
EVENT(branch_on_condition, a->condition, a->address)
EVENT(percussion, a->on)
EVENT(volume_scale, a->scale)

/* How a row fills a parameter: from an operand (1 the first), from the word
 * at an operand, or a constant of the event's. */
typedef enum { FROM_NONE, FROM_OPERAND, FROM_WORD, FROM_CONSTANT } ArgFrom;
typedef struct {
    ArgFrom from;
    int16_t value;   /* the operand, or the constant */
} ArgSpec;
#define OPERAND(n) { FROM_OPERAND, (n) }
#define WORD(n) { FROM_WORD, (n) }
#define CONSTANT(k) { FROM_CONSTANT, (k) }

struct EventRow {
    EventRun run;
    uint8_t operands;
    LookAhead look;
    struct {
        ArgSpec steps, level, pan, ticks, delay, rate, depth, period, clock, octave, amount, number, count, effect,
            which, value, pass, condition, scale, semitones, octaves, on, part, address;
    };
};
#define RUN(op) .run = op##_event

/* A row for each event, $C4 on, in order, with the address of its handler in the
 * original (and of what its look-ahead runs, where that is its own). */
static const EventRow rows[] = {
    /* $C4 */ { RUN(volume), 1, LOOK_PAST, { .steps = CONSTANT(0), .level = OPERAND(1) } }, // SPC $175F
    /* $C5 */ { RUN(volume), 2, LOOK_PAST, { .steps = OPERAND(1), .level = OPERAND(2) } }, // SPC $1770
    /* $C6 */ { RUN(set_pan), 1, LOOK_PAST, { .pan = OPERAND(1) } }, // SPC $17BA
    /* $C7 */ { RUN(slide_pan), 2, LOOK_PAST, { .steps = OPERAND(1), .pan = OPERAND(2) } }, // SPC $17CE
    /* $C8 */ { RUN(slide_pitch), 2, LOOK_PAST, { .ticks = OPERAND(1), .semitones = OPERAND(2) } }, // SPC $17F2
    /* $C9 */ { RUN(vibrato), 3, LOOK_PAST, { .delay = OPERAND(1), .rate = OPERAND(2), .depth = OPERAND(3) } }, // SPC $186E
    /* $CA */ { RUN(vibrato_off), 0, LOOK_PAST }, // SPC $192E
    /* $CB */ { RUN(tremolo), 3, LOOK_PAST, { .delay = OPERAND(1), .rate = OPERAND(2), .depth = OPERAND(3) } }, // SPC $1938
    /* $CC */ { RUN(tremolo_off), 0, LOOK_PAST }, // SPC $198E
    /* $CD */ { RUN(pan_sweep), 2, LOOK_PAST, { .period = OPERAND(1), .depth = OPERAND(2) } }, // SPC $1998
    /* $CE */ { RUN(pan_sweep_off), 0, LOOK_PAST }, // SPC $19EE
    /* $CF */ { RUN(noise_clock), 1, LOOK_PAST, { .clock = OPERAND(1) } }, // SPC $1A78
    /* $D0 */ { RUN(noise), 0, LOOK_PAST, { .on = CONSTANT(true) } }, // SPC $1A3C
    /* $D1 */ { RUN(noise), 0, LOOK_PAST, { .on = CONSTANT(false) } }, // SPC $1A68
    /* $D2 */ { RUN(pitch_modulation), 0, LOOK_PAST, { .on = CONSTANT(true) } }, // SPC $1A98
    /* $D3 */ { RUN(pitch_modulation), 0, LOOK_PAST, { .on = CONSTANT(false) } }, // SPC $1AB5
    /* $D4 */ { RUN(echo), 0, LOOK_PAST, { .on = CONSTANT(true) } }, // SPC $1A15
    /* $D5 */ { RUN(echo), 0, LOOK_PAST, { .on = CONSTANT(false) } }, // SPC $1A2C
    /* $D6 */ { RUN(set_octave), 1, LOOK_PAST, { .octave = OPERAND(1) } }, // SPC $1A0E
    /* $D7 */ { RUN(shift_octave), 0, LOOK_PAST, { .octaves = CONSTANT(1) } }, // SPC $19FA
    /* $D8 */ { RUN(shift_octave), 0, LOOK_PAST, { .octaves = CONSTANT(-1) } }, // SPC $1A04
    /* $D9 */ { RUN(set_transpose), 1, LOOK_PAST, { .semitones = OPERAND(1) } }, // SPC $1801
    /* $DA */ { RUN(add_transpose), 1, LOOK_PAST, { .semitones = OPERAND(1) } }, // SPC $17FD
    /* $DB */ { RUN(detune), 1, LOOK_PAST, { .amount = OPERAND(1) } }, // SPC $1CF2
    /* $DC */ { RUN(instrument), 1, LOOK_ARTICULATION, { .number = OPERAND(1) } }, // SPC $1ACF, look-ahead $1B86
    /* $DD */ { RUN(envelope_part), 1, LOOK_PAST, { .part = CONSTANT(ENVELOPE_ATTACK), .value = OPERAND(1) } }, // SPC $1AFC
    /* $DE */ { RUN(envelope_part), 1, LOOK_PAST, { .part = CONSTANT(ENVELOPE_DECAY), .value = OPERAND(1) } }, // SPC $1B2A
    /* $DF */ { RUN(envelope_part), 1, LOOK_PAST, { .part = CONSTANT(ENVELOPE_SUSTAIN_LEVEL), .value = OPERAND(1) } }, // SPC $1B3D
    /* $E0 */ { RUN(envelope_part), 1, LOOK_PAST, { .part = CONSTANT(ENVELOPE_SUSTAIN_RATE), .value = OPERAND(1) } }, // SPC $1B4F
    /* $E1 */ { RUN(instrument_envelope_again), 0, LOOK_PAST }, // SPC $1B5F
    /* $E2 */ { RUN(loop_start), 1, LOOK_PAST, { .count = OPERAND(1) } }, // SPC $1C64
    /* $E3 */ { RUN(loop_end), 0, LOOK_LOOP_END }, // SPC $1C94, look-ahead $1CC9
    /* $E4 */ { RUN(slur), 0, LOOK_ARTICULATION }, // SPC $1B72, look-ahead $1B86
    /* $E5 */ { RUN(articulation_ends), 0, LOOK_ARTICULATION }, // SPC $12DD, look-ahead $1B86
    /* $E6 */ { RUN(roll), 0, LOOK_ARTICULATION }, // SPC $1B9A, look-ahead $1B86
    /* $E7 */ { RUN(articulation_ends), 0, LOOK_ARTICULATION }, // SPC $12DD, look-ahead $1B86
    /* $E8 */ { RUN(next_length), 1, LOOK_PAST, { .ticks = OPERAND(1) } }, // SPC $1CEF
    /* $E9 */ { RUN(effect_track), 1, LOOK_EFFECT_TRACK, { .effect = OPERAND(1), .which = CONSTANT(0) } }, // SPC $1BA8, look-ahead $1BD9
    /* $EA */ { RUN(effect_track), 1, LOOK_EFFECT_TRACK, { .effect = OPERAND(1), .which = CONSTANT(1) } }, // SPC $1BAC, look-ahead $1BDD
    /* $EB */ { RUN(end_track), 0, LOOK_PAST }, // SPC $1D4A
    /* $EC */ { RUN(end_track), 0, LOOK_PAST }, // SPC $1D4A
    /* $ED */ { RUN(end_track), 0, LOOK_PAST }, // SPC $1D4A
    /* $EE */ { RUN(end_track), 0, LOOK_PAST }, // SPC $1D4A
    /* $EF */ { RUN(end_track), 0, LOOK_PAST }, // SPC $1D4A
    /* $F0 */ { RUN(tempo), 1, LOOK_PAST, { .steps = CONSTANT(0), .value = OPERAND(1) } }, // SPC $1722
    /* $F1 */ { RUN(tempo), 2, LOOK_PAST, { .steps = OPERAND(1), .value = OPERAND(2) } }, // SPC $1732
    /* $F2 */ { RUN(echo_volume), 1, LOOK_PAST, { .steps = CONSTANT(0), .level = OPERAND(1) } }, // SPC $1791
    /* $F3 */ { RUN(echo_volume), 2, LOOK_PAST, { .steps = OPERAND(1), .level = OPERAND(2) } }, // SPC $179A
    /* $F4 */ { RUN(song_volume), 1, LOOK_PAST, { .level = OPERAND(1) } }, // SPC $1751
    /* $F5 */ { RUN(loop_exit), 3, LOOK_LOOP_EXIT, { .pass = OPERAND(1), .address = WORD(2) } }, // SPC $1C09, look-ahead $1C39
    /* $F6 */ { RUN(jump), 2, LOOK_JUMP, { .address = WORD(1) } }, // SPC $1BEE, look-ahead $1BFC
    /* $F7 */ { RUN(echo_feedback), 2, LOOK_PAST, { .steps = OPERAND(1), .level = OPERAND(2) } }, // SPC $1805
    /* $F8 */ { RUN(echo_filter), 2, LOOK_PAST, { .steps = OPERAND(1), .which = OPERAND(2) } }, // SPC $1826
    /* $F9 */ { RUN(marker), 1, LOOK_PAST, { .value = OPERAND(1) } }, // SPC $1CF6
    /* $FA */ { RUN(branch_on_condition), 3, LOOK_CONDITION, { .condition = OPERAND(1), .address = WORD(2) } }, // SPC $1CFB, look-ahead $1D2A
    /* $FB */ { RUN(percussion), 0, LOOK_PAST, { .on = CONSTANT(true) } }, // SPC $1AC5
    /* $FC */ { RUN(percussion), 0, LOOK_PAST, { .on = CONSTANT(false) } }, // SPC $1AC9
    /* $FD */ { RUN(volume_scale), 1, LOOK_PAST, { .scale = OPERAND(1) } }, // SPC $1757
    /* $FE */ { RUN(end_track), 0, LOOK_PAST }, // SPC $1D4A
    /* $FF */ { RUN(end_track), 0, LOOK_PAST }, // SPC $1D4A
};

_Static_assert(sizeof rows / sizeof rows[0] == 0x100 - FIRST_EVENT, "a row for every event");

static const EventRow *event_row(uint8_t event) { return &rows[event - FIRST_EVENT]; }
static uint8_t event_operands(const EventRow *row) { return row->operands; }
static LookAhead event_look_ahead(const EventRow *row) { return row->look; }
static uint8_t event_effect_track(const EventRow *row) { return (uint8_t)row->which.value; }

static int16_t decode(ArgSpec spec, const uint8_t *operands)
{
    switch (spec.from) {
    case FROM_OPERAND:
        return operands[spec.value - 1];
    case FROM_WORD:
        return (int16_t)(operands[spec.value - 1] | operands[spec.value] << 8);
    case FROM_CONSTANT:
        return spec.value;
    default:
        return 0;
    }
}

// SPC $073D
static TrackFlow run_event(SoundDriver *d, uint8_t track, uint8_t event)
{
    const EventRow *row = event_row(event);
    uint8_t operands[3] = { 0 };
    for (uint8_t i = 0; i < row->operands; i++)
        operands[i] = next_byte(d, track_of(d, track));
    EventArgs a = {
        .steps = (uint8_t)decode(row->steps, operands),
        .level = (uint8_t)decode(row->level, operands),
        .pan = (uint8_t)decode(row->pan, operands),
        .ticks = (uint8_t)decode(row->ticks, operands),
        .delay = (uint8_t)decode(row->delay, operands),
        .rate = (uint8_t)decode(row->rate, operands),
        .depth = (uint8_t)decode(row->depth, operands),
        .period = (uint8_t)decode(row->period, operands),
        .clock = (uint8_t)decode(row->clock, operands),
        .octave = (uint8_t)decode(row->octave, operands),
        .amount = (uint8_t)decode(row->amount, operands),
        .number = (uint8_t)decode(row->number, operands),
        .count = (uint8_t)decode(row->count, operands),
        .effect = (uint8_t)decode(row->effect, operands),
        .which = (uint8_t)decode(row->which, operands),
        .value = (uint8_t)decode(row->value, operands),
        .pass = (uint8_t)decode(row->pass, operands),
        .condition = (uint8_t)decode(row->condition, operands),
        .scale = (uint8_t)decode(row->scale, operands),
        .semitones = (int8_t)decode(row->semitones, operands),
        .octaves = (int8_t)decode(row->octaves, operands),
        .on = decode(row->on, operands) != 0,
        .part = (EnvelopePart)decode(row->part, operands),
        .address = (uint16_t)decode(row->address, operands),
    };
    return row->run(d, track, &a);
}
