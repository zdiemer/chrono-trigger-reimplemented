/* The sound driver's parts, as they call each other (game/sound/sound.h says
 * what the driver is).
 *
 * Each function that stands for code of the original carries its address on
 * the sound CPU, `// SPC $xxxx`, several where it stands for more than one
 * routine (docs/SOUND.md, "The driver in a paragraph", and the listing
 * tools/spc700dis.py prints). The command and event tables name each row's
 * handler the same way. */
#ifndef CT_GAME_SOUND_DRIVER_H
#define CT_GAME_SOUND_DRIVER_H

#include "sound/sound.h"

/* ---- Tracks and voices ---------------------------------------------------------------- */

/* The music's tracks drive the voices of their number, the effects' tracks
 * 12-15 voices 4-7: a track's voice is its number's low three bits. */
static inline uint8_t voice_of(uint8_t track) { return track & 7; }
static inline AuVoiceSet voice_bit(uint8_t track) { return (AuVoiceSet)(1u << voice_of(track)); }
static inline bool is_music(uint8_t track) { return track < SOUND_MUSIC_TRACKS; }
static inline uint8_t effect_track_of(uint8_t voice) { return (uint8_t)(SOUND_FIRST_EFFECT_TRACK - 4 + voice); }

/* No song: what the song playing, the song saved and the last song started
 * are when there is none. */
enum { NO_SONG = 0xFF };

enum {
    ALL_VOICES = 0xFF,
    EFFECT_PAIR_67 = 0xC0,    /* the voices the effects of command $18 play on */
    EFFECT_PAIR_45 = 0x30     /* ... of command $19 */
};

/* The flags of the kind of track this is. */
static inline SoundVoiceFlags *flags_of(SoundDriver *d, uint8_t track)
{
    return is_music(track) ? &d->song.music : &d->song.effects;
}

/* The fades: the music's, and the effects' on each pair of voices. */
enum { FADE_MUSIC, FADE_EFFECTS_67, FADE_EFFECTS_45 };

/* ---- Slides --------------------------------------------------------------------------- */

static inline uint8_t slide_whole(const SoundSlide *s) { return (uint8_t)(s->level >> 8); }
static inline void slide_set_whole(SoundSlide *s, uint8_t whole) { s->level = (uint16_t)(whole << 8 | (s->level & 0xFF)); }

/* A slide's step, as 8.8, for `steps` steps from one whole value to another;
 * which way it goes depends on whether the values are signed. */
typedef enum { UNSIGNED, SIGNED } Signedness;
uint16_t slide_step(uint8_t from, uint8_t to, uint8_t steps, Signedness values);

/* Advance a slide a step if it has one left; true if its whole part moved. */
bool slide_advance(SoundSlide *s);

/* ---- The sound memory ----------------------------------------------------------------- */

enum { END_OF_TRACK = 0xEB };

uint8_t sound_memory_byte(SoundDriver *d, uint16_t at);
uint16_t sound_memory_word(SoundDriver *d, uint16_t at);

/* The one-byte track the effects are sent to when they stop: it ends them.
 * It is in the driver's program, which the memory above doesn't hold. */
enum { SILENT_TRACK = 0x1DC1 };

/* ---- songs.c: songs, effects and what they load ---------------------------------------- */

/* A song's two sets of tracks. */
enum { FIRST_TRACK_SET = 0, SECOND_TRACK_SET = 1 };
bool announce_song(SoundDriver *d, uint8_t song, uint8_t volume, uint8_t echo_delay, bool save_first);
void start_song(SoundDriver *d, uint8_t track_set);
void start_effect(SoundDriver *d, uint8_t effect, uint8_t pan, AuVoiceSet pair);
void stop_music(SoundDriver *d);
void stop_effects(SoundDriver *d, AuVoiceSet pair);
void change_echo_delay(SoundDriver *d, uint8_t delay);
void take_song_voices(SoundDriver *d);
void load_boot_data(SoundDriver *d);

/* What a track leaves when its stream ends, or its effect is cut off. */
void release_track(SoundDriver *d, uint8_t track);

/* The track's instrument: its sample, tuning and envelope from the
 * instrument tables; and put on its voice. */
void set_instrument(SoundDriver *d, uint8_t track, uint8_t instrument);
void store_instrument(SoundDriver *d, uint8_t instrument, uint16_t tuning, uint16_t envelope);
AuEnvelope instrument_envelope(SoundDriver *d, uint8_t instrument);
void put_instrument_on_voice(SoundDriver *d, uint8_t track);
void put_envelope_on_voice(SoundDriver *d, uint8_t track);

/* The echo, noise and pitch modulation voices, from the music's and the
 * effects' flags. */
void update_echo_voices(SoundDriver *d);
void update_noise(SoundDriver *d);
void update_pitch_modulation(SoundDriver *d);

/* ---- sequencer.c: the tracks ----------------------------------------------------------- */

/* Step the music (by the tempo) and the effects (at their own rate). While
 * loading, only the music, and the settings slide with it. */
void sequence(SoundDriver *d);
void sequence_music_while_loading(SoundDriver *d);

void reset_track(SoundTrack *t);
uint16_t pitch_of_note(const SoundDriver *d, const SoundTrack *t, uint8_t note);

/* ---- voices.c: what each voice sounds like, and the mix ---------------------------------- */

void hear_echo_volume(SoundDriver *d);
void flush_mix(SoundDriver *d);
void update_voices(SoundDriver *d);
void update_music_voices_while_loading(SoundDriver *d);
void slide_song_settings(SoundDriver *d);
void put_pitch_on_voice(SoundDriver *d, uint8_t track);
void restart_vibrato(SoundDriver *d, uint8_t track);
void restart_tremolo(SoundDriver *d, uint8_t track);
uint16_t wave_base_step(uint8_t rate, uint8_t depth);
void meter_status(SoundDriver *d);

#endif
