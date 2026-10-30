/* The sound driver: Chrono Trigger's music and sound effects.
 *
 * Square's driver, which ran on the sound CPU, reimplemented in C (docs/SOUND.md).
 * It plays one song on eight tracks, one per voice, and sound effects on four
 * more tracks that take voices 4-7 from the music while they play. A track is
 * a stream of notes and of events that shape them (volume, pan, vibrato,
 * tremolo, slides, loops, echo, noise); the driver steps the tracks in ticks
 * of about 190 a second, and each tick turns every voice's state into the
 * voice calls of the audio service (platform/audio.h, "The voices").
 *
 * The game drives it with commands (sound_run_command: start a song or an effect,
 * fade, pause, change the echo) and reads back three things: how far the
 * music's fade has to go, what is playing, and the marker the song last
 * passed (sound_status). The data it plays -- songs, sound effects,
 * instruments -- is loaded from the game's resources (core/sound_data.h) by
 * the loads below, which is what the CPU half's uploads become.
 *
 * The host owns the driver and calls it: sound_boot once, sound_tick at the
 * driver's rate (SOUND_TICKS_PER_SECOND), commands as they come. The pieces:
 * driver.c (the tick, the commands and the status), songs.c (songs and
 * effects starting and stopping, and the data they load), sequencer.c (the
 * tracks and their events), voices.c (what each voice sounds like, and the
 * mix). */
#ifndef CT_GAME_SOUND_SOUND_H
#define CT_GAME_SOUND_SOUND_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/audio.h"
#include "state.h"

/* The commands the game sends most often (driver.c's table has them all):
 * start a song on its first set of tracks, or a sound effect on the pair of
 * voices 6-7 or 4-5 that the effects take from the music. */
enum { SOUND_PLAY_SONG = 0x10 };
typedef enum { SOUND_EFFECT_ON_VOICES_6_7 = 0x18, SOUND_EFFECT_ON_VOICES_4_5 = 0x19 } SoundEffectVoices;

/* The driver's tick: its timer runs at 8 kHz and ticks every 42nd count; fast
 * forward ticks at every count. */
enum { SOUND_TIMER_HZ = 8000, SOUND_TIMER_COUNTS_PER_TICK = 42 };

enum {
    SOUND_TRACKS = 16,       /* 0-7 the music, one per voice; 12-15 the effects, on voices 4-7 */
    SOUND_MUSIC_TRACKS = 8,
    SOUND_FIRST_EFFECT_TRACK = 12,
    SOUND_LOOP_LEVELS = 4,
    SOUND_QUEUE = 16
};

/* A value that moves toward another in equal steps: an 8.8 level, the step,
 * and how many steps are left. */
typedef struct {
    uint16_t level;
    uint16_t step;
    uint8_t steps_left;
} SoundSlide;

/* A wave a voice's pitch or volume follows (vibrato, tremolo): a phase that
 * advances by `step` each tick and turns back every `rate` ticks, and a
 * `ramp` of half waves over which it grows to its full depth. */
typedef struct {
    uint8_t depth;        /* 0 is none; the low six bits are how deep, the top two the wave's shape */
    uint8_t rate, ticks_left;
    uint8_t delay, delay_left;
    uint8_t ramp;
    uint16_t base_step, step;
    uint16_t phase;
} SoundWave;

typedef struct {
    uint8_t passes, counts_left;
    uint16_t start;
} SoundLoop;

/* One track: where it is in its stream and everything its events have set. */
typedef struct {
    uint16_t at;             /* the next byte of its stream, a sound memory address */
    uint8_t ticks_left;      /* until its next note */
    uint8_t loop_level;
    SoundLoop loops[SOUND_LOOP_LEVELS];

    uint8_t instrument;
    uint16_t tuning;         /* low byte a fraction of the pitch, high byte a finer one (sequencer.c) */
    AuEnvelope envelope;
    uint8_t octave;          /* in semitones: 12 x the octave */
    int8_t transpose;
    uint8_t detune;
    uint8_t note;            /* the note playing, in semitones */
    uint16_t pitch;          /* its pitch, before vibrato */

    uint8_t volume_scale;
    SoundSlide volume, pan;

    int8_t slide_semitones;  /* a pitch slide set up for the next note */
    uint8_t slide_ticks;
    uint32_t slide_step;     /* 16.8, 24 bits */
    uint8_t pitch_fraction;

    SoundWave vibrato, tremolo;
    uint8_t vibrato_scale;   /* effects: which depth scale (sound_vibrato_scale) */
    uint16_t vibrato_depth;
    int16_t vibrato_offset;

    uint8_t pan_sweep;       /* the event's depth byte: 0 is none */
    uint8_t pan_sweep_period, pan_sweep_ticks_left;
    uint16_t pan_sweep_step, pan_sweep_phase;
} SoundTrack;

/* Voice sets kept apart for the music and for the effects. */
typedef struct {
    AuVoiceSet echo, noise, pitch_modulation;
    AuVoiceSet sounding;     /* a note is sounding: released before its end unless held */
    AuVoiceSet held;         /* the note runs into the next: no release before it */
    AuVoiceSet slur, slurring, roll;
} SoundVoiceFlags;

/* Everything a song is while it plays: what the song save keeps
 * (command $14) to resume it later where it was. */
typedef struct {
    SoundTrack tracks[SOUND_TRACKS];
    uint16_t relocation;     /* added to the song's own addresses */
    AuVoiceSet key_on, key_off;
    SoundSlide tempo;
    uint8_t music_beat, effect_beat;
    uint8_t volume;
    SoundSlide echo_volume, echo_feedback;
    SoundSlide filter[8];
    uint8_t filter_steps_left;
    uint8_t music_tracks;    /* the music tracks still playing */
    SoundVoiceFlags music, effects;
    uint8_t music_noise_clock, effect_noise_clock;
    uint8_t marker;
    AuVoiceSet percussion;
} SoundSong;

typedef struct {
    SoundSlide volume;
} SoundFade;

enum { SOUND_FADES = 3 };   /* the music, effects on voices 6-7, effects on voices 4-5 */

typedef struct {
    uint8_t command, p1, p2, p3;
} SoundCommand;

/* What the driver showed the game at its last tick: see sound_status. */
typedef struct {
    uint8_t port[4];
} SoundStatus;

/* The sound memory the driver reads its data from, as the CPU half loads it:
 * the instruments' tunings and envelopes, the song with the sound effects
 * after it, and the song's percussion. The data's own pointers are addresses
 * in it, which is why it is kept by address. */
enum {
    SOUND_INSTRUMENT_TABLES = 0x1F00,
    SOUND_SEQUENCES = 0x2000,
    SOUND_EFFECTS = 0x2F00,
    SOUND_SEQUENCES_END = 0x4700,
    SOUND_PERCUSSION = 0xF120
};

/* The game's samples in sound memory (loads.c): sixteen places after the
 * boot's own samples, each holding one sample block of the cartridge, laid
 * end to end from the first. The echo's buffer takes the top of sound memory,
 * and what lies past its start once a song has loaded is forgotten. */
enum { SOUND_SAMPLE_PLACES = 16 };

typedef struct {
    uint16_t block[SOUND_SAMPLE_PLACES];        /* the block each place holds, 0 for none */
    uint16_t length[SOUND_SAMPLE_PLACES];
    uint16_t start[SOUND_SAMPLE_PLACES + 1];    /* each place's start; a place ends where the next starts */
    uint16_t song_start[SOUND_SAMPLE_PLACES];   /* where the song's instruments' samples were found, */
    uint16_t song_loop[SOUND_SAMPLE_PLACES];    /* ... and loop from, as the song that last loaded left them */
    uint8_t echo_delay;                          /* the echo whose buffer the places keep below */
    uint8_t echo_page;                           /* ... and the page that buffer starts at */
    uint8_t instrument_place;                    /* command $71's: the place of the song's first instrument */
    uint8_t effect_set;                          /* the set of effects loaded */
    uint8_t effects_overwritten;                 /* songs that ran over the effects since they were loaded */
} SoundSamples;

typedef struct {
    const GameState *game;   /* where songs, effects and instruments are loaded from */
    AuVoices *voices;

    uint8_t instrument_tables[SOUND_SEQUENCES - SOUND_INSTRUMENT_TABLES];
    uint8_t sequences[SOUND_SEQUENCES_END - SOUND_SEQUENCES];
    uint8_t percussion[36];
    uint16_t semitone_pitch[12];
    uint8_t note_length[14];
    uint8_t vibrato_scale[12];
    int8_t echo_filter[4][8];
    uint8_t own_sample[45];
    uint8_t own_sample_nibble;

    SoundSong song, saved;
    uint8_t song_id, saved_song_id, last_song_started;
    bool song_announced;               /* a song's command came, and its data is awaited */
    uint8_t announced_track_set;
    bool song_echo_changed;            /* ... with a new echo delay */
    uint8_t echo_delay;
    AuVoiceSet effect_voices;          /* the voices the effects have taken */
    AuVoiceSet echo_voices, noise_voices, pitch_modulation_voices;
    uint8_t noise_clock;
    int8_t echo_volume_heard;
    AuVoiceSet muted;
    AuVoiceSet volume_changed, pitch_changed;
    bool mono, full_volume, paused, stepping_every_tick, fast, meter, meter_upper, stopped;
    SoundSlide tempo_modifier, pitch_modifier;
    uint8_t effect_pitch_modifier;
    uint8_t condition, highest_condition;
    SoundFade fades[SOUND_FADES];
    uint8_t fade_divider;
    uint8_t meter_volume[16];         /* each voice's last left and right volumes */
    SoundCommand queue[SOUND_QUEUE];
    uint8_t queued;
    bool echo_waits;                   /* the host says when the echo's buffer is ready */
    bool song_voices_waiting;          /* for the buffer: a song's echo, noise and pitch modulation */
    uint8_t echo_silences_waiting;     /* ... delay changes that silence the echo if no music plays */
    bool echo_draining;                /* the echo's writes wait for the old buffer to drain */
    SoundStatus status;
    SoundSamples samples;

    uint32_t stray_reads;              /* reads outside the sound memory kept here, */
    uint16_t first_stray_read;         /* ... the first of them at this address */

    /* The work the original's loads were, counted since the boot, for a host
     * that keeps the original's time without its uploads (the time model,
     * docs/REIMPLEMENTATION.md): the bytes its CPU half sent the sound
     * processor (samples, songs, effects), and the bytes the sound processor
     * moved down in its memory meanwhile. */
    uint32_t bytes_sent, bytes_moved;
} SoundDriver;

/* Start the driver: its tables and the data the boot uploads (the first set
 * of sound effects, the first instruments and their samples), its own
 * sample, and the voices. The host zeroes the driver first, as power-on leaves sound memory;
 * booting it again reloads it, keeping what the original keeps (driver.c). */
void sound_boot(SoundDriver *d, const GameState *game, AuVoices *voices);

/* A command from the game ($10 play a song, $18 an effect, $80-$83 fades,
 * ... docs/SOUND.md, "Commands from the CPU"), carried out now. */
void sound_run_command(SoundDriver *d, SoundCommand c);
/* A song's start in its two parts, for a host that uploads the song's data
 * between them as the original's CPU half does: the command as it comes (the
 * music stops, and a new echo delay moves the echo's buffer out of the way of
 * the samples about to arrive; false if the song is refused as the one
 * playing), and the start once the data is in. sound_run_command does both
 * at once. */
bool sound_announce_song(SoundDriver *d, SoundCommand c);
void sound_start_announced_song(SoundDriver *d);
/* A command that arrives while a tick runs: it waits for the tick's next
 * point that takes commands. */
void sound_queue_command(SoundDriver *d, SoundCommand c);

/* A tick: the mix the last tick left, the status, the fades and the tracks
 * stepped; then what each voice sounds like; then the driver's own sample.
 * Commands queued meanwhile are carried out after the first two parts. The
 * parts are for a host that carries out each command itself at the point it
 * comes (ct_dev --sound-lockstep): a paused driver does only the first, up to
 * the status, and a tick paused by its first commands stops there. */
void sound_tick(SoundDriver *d);
void sound_tick_begin(SoundDriver *d);
void sound_tick_voices(SoundDriver *d);
void sound_tick_end(SoundDriver *d);

/* The tick the driver runs while data is being loaded for it: the music goes
 * on, but the effects, the commands and the status wait. */
void sound_tick_while_loading(SoundDriver *d);

/* The echo's buffer changes size with its delay, which the S-DSP takes time
 * over: the original waits, carrying out commands meanwhile, first for the
 * old buffer to drain before the echo writes again, then for the new one to
 * fill before a song's voices take the song's echo, noise and pitch
 * modulation and before a delay changed with no music playing silences the
 * echo. A host that waits the same way sets `echo_waits` and calls
 * sound_echo_drained and then sound_echo_ready (which implies the first);
 * otherwise those follow at once. */
void sound_echo_drained(SoundDriver *d);
void sound_echo_ready(SoundDriver *d);

/* Loads: a set of sound effects replaces the one there (and the effects
 * playing stop), and one block's instrument replaces the song's first
 * (command $71's upload). A song's own data is loaded when it starts. */
void sound_load_effects(SoundDriver *d, uint8_t set);
void sound_load_instrument(SoundDriver *d, uint8_t instrument, uint16_t block);

/* ---- The game's side (loads.c) -------------------------------------------------------------
 *
 * What the original's CPU half did with its uploads, as calls: the game's
 * command as it took it ($C7:0004), which starts a song with its samples
 * placed in sound memory, switches the set of effects, loads an instrument,
 * and sends the rest to the driver as they are. Beside the command the game
 * keeps the set of effects it wants its next song to bring (`effects_asked`,
 * nonzero for the second), which the field asks for before some songs. A
 * host that plays the C driver in the original's place gives it the game's
 * commands here. */
void sound_game_command(SoundDriver *d, SoundCommand c, uint8_t effects_asked);

/* Its parts, for a host that takes the CPU half's work from the original as
 * it happens (ct_dev --sound-lockstep): a song's start with the parameters it
 * comes with (a new echo delay keeps its buffer free of samples first); the
 * set of effects a song announced by sound_announce_song brings; the rest of
 * that song once the original's upload is in (its samples placed, the effects
 * loaded again if they changed or a song ran over them, the song started);
 * the room the echo keeps at the top after a change of its delay; and command
 * $71's instrument, false if its sample doesn't fit where the song's first
 * lies. */
void sound_play_song(SoundDriver *d, SoundCommand c, uint8_t effects_asked);
void sound_ask_for_effects(SoundDriver *d, uint8_t effects_asked);
void sound_load_announced_song(SoundDriver *d);
void sound_keep_echo_free(SoundDriver *d, uint8_t echo_delay);
bool sound_load_song_instrument(SoundDriver *d, uint16_t block);

/* The status the game reads (docs/SOUND.md, "What the game reads back"), as
 * the driver showed it at its last tick. */
uint8_t sound_music_fade_left(const SoundDriver *d);
uint8_t sound_playing(const SoundDriver *d);   /* the music's tracks (low nibble) and the effects' voices */
uint8_t sound_marker(const SoundDriver *d);    /* low nibble the song's marker, high the highest condition met */
SoundStatus sound_status(const SoundDriver *d);

/* Whether the driver's timer runs fast (command $F6). */
bool sound_ticking_fast(const SoundDriver *d);

#endif
