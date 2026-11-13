/* Audio service (docs/PLATFORM.md): commands to the sound driver, the status
 * the game polls, and the host's samples. */
#ifndef CT_PLATFORM_AUDIO_H
#define CT_PLATFORM_AUDIO_H

#include "platform/platform.h"

/* A sound driver command ($C7:0004's interface) with up to three parameters.
 * `count` is how many of p1-p3 the caller sets; the driver sends the others as
 * the previous command left them (event $E8 sets two, the scene song three). */
typedef struct {
    uint8_t command, p1, p2, p3;
    uint8_t count;
} AudioCommand;

/* Send a command. SNES backend: the parameters and command into $1E01-$1E03 and
 * $1E00 (in one order, parameters then command, whatever order the original
 * used: nothing runs between them), then handed to the C sound driver the host
 * plays as a call (au_driver_hosted; core/sound_commands.h). Where the
 * console's sound processor runs the cartridge's own driver -- the reference
 * play, which is the cartridge's program on the emulated console -- a call-out
 * to $C7:0004 from o.site (o.target) instead, the cartridge's CPU half carrying
 * it to the sound processor, and it returns when the original's JSL would. */
PF_API void au_command(Platform *pf, PfOrigin o, AudioCommand c);

/* The driver's commands the game sends (docs/SOUND.md, "Commands from the CPU"). */
enum {
    AU_START_SONG = 0x10,       /* p1 the song */
    AU_START_SONG_SECOND_SET = 0x11, /* ... from its second set of tracks; a song the driver saved resumes where it was */
    AU_START_SONG_SAVING = 0x14, /* ... saving the song playing first, to resume */
    AU_START_EFFECT = 0x18,     /* p1 the effect, p2 where it is panned (0x80 the middle) */
    AU_START_OTHER_EFFECT = 0x19, /* ... on the other pair of effect voices (4-5) */
    AU_FADE_ALL = 0x80,         /* p1 steps, p2 the volume faded to: everything */
    AU_FADE_MUSIC = 0x81,       /* ... the music */
    AU_FADE_EFFECTS = 0x82,     /* ... the effects on voices 6-7 */
    AU_FADE_MORE_EFFECTS = 0x83, /* ... the effects on voices 4-5 */
    AU_BEND_BOTH = 0x85,        /* p1 steps, p2 the tempo's and the pitch's modifiers both */
    AU_BEND_TEMPO = 0x86,       /* p1 steps, p2 the tempo's modifier: 0 as written, 1-127 faster, 128-255 slower */
    AU_BEND_EFFECTS_PITCH = 0x89, /* p1 steps, p2 the pitch modifier of the effect voices */
    AU_SWITCH_EFFECTS = 0x70,   /* p1 nonzero: the battle's set of effects, at once; 0: the field's */
    AU_STOP_ALL = 0xF0,         /* the music and every effect */
    AU_STOP_EFFECTS = 0xF2,     /* p1 which pair of effect voices: 0 voices 6-7, 1 voices 4-5 */
    AU_FAST_FORWARD = 0xF6      /* p1 1: everything played fast (the driver's timer at 8 kHz); 0: back at its pace */
};
enum { AU_CENTRE = 0x80, AU_FULL_VOLUME = 0xFF };

static inline void au_play_song(Platform *pf, PfOrigin o, uint8_t song)
{
    au_command(pf, o, (AudioCommand){ .command = AU_START_SONG, .p1 = song, .count = 1 });
}

static inline void au_play_effect(Platform *pf, PfOrigin o, uint8_t effect, uint8_t pan)
{
    au_command(pf, o, (AudioCommand){ .command = AU_START_EFFECT, .p1 = effect, .p2 = pan, .count = 2 });
}

/* Switch the set of sound effects the driver plays at once: the battle's, or
 * the field's. */
static inline void au_switch_effects(Platform *pf, PfOrigin o, bool battles)
{
    au_command(pf, o, (AudioCommand){ .command = AU_SWITCH_EFFECTS, .p1 = battles ? 1 : 0, .count = 1 });
}

/* Fade `what` (AU_FADE_*) to `volume` over `steps` steps (0: at once). */
static inline void au_fade(Platform *pf, PfOrigin o, uint8_t what, uint8_t steps, uint8_t volume)
{
    au_command(pf, o, (AudioCommand){ .command = what, .p1 = steps, .p2 = volume, .count = 2 });
}

/* Driver status the game polls: the byte the driver shows on port 1-3. Doesn't
 * sync: when a poll happens is the caller's business. */
PF_API uint8_t au_status(Platform *pf, PfOrigin o, uint8_t port);
/* Wait until the new song has at least one playing track. Returns its mask. */
PF_API uint8_t au_wait_music_start(Platform *pf, PfOrigin o);

/* What the status says (docs/SOUND.md, "What the game reads back"): the steps
 * the music's fade has left, and which of the music's tracks are playing,
 * folded into four bits (none once the song has ended). */
static inline uint8_t au_music_fade_left(Platform *pf, PfOrigin o) { return au_status(pf, o, 1); }
static inline uint8_t au_music_playing(Platform *pf, PfOrigin o) { return au_status(pf, o, 3) & 0x0F; }

/* The rate of everything the game's sound produces, in stereo frames a second. */
enum { AU_SAMPLE_RATE = 32000 };

/* Host side: up to max_frames stereo frames at AU_SAMPLE_RATE; returns how many. */
PF_API size_t au_take_samples(Platform *pf, int16_t *out, size_t max_frames);

/* Below the command interface: the ports, for the scene code that sends the
 * driver messages on them itself (scene/sound.c, scene/race.c);
 * tests/test_platform_lint.py allows them nowhere else. The C driver's host
 * answers them as the original's driver does.
 *
 * au_driver_write and au_driver_read access port 0-3. SNES backend: after
 * tm_sync, since the sound processor watches the timing (a message is written
 * on ports 1-3 before port 0, and its echo waited for). */
PF_API void au_driver_write(Platform *pf, PfOrigin o, uint8_t port, uint8_t v);
PF_API uint8_t au_driver_read(Platform *pf, PfOrigin o, uint8_t port);

/* A JSR to a routine of the original's that sends over the ports (o: the
 * JSR's site and target). SNES backend: a call-out, so frames pass while it
 * waits. `dont_care` names
 * the caller's registers and flags the callee doesn't read (DC_* bits,
 * port/native.h), which shadow mode doesn't compare at the call: a port written
 * as logic apart from the registers sets only what the callee takes. */
PF_API void au_driver_call(Platform *pf, PfOrigin o, uint16_t dont_care);

/* The wait for the driver's answer on port 0: it echoes every value put there
 * once it has taken the message the value ends. au_driver_wait_echo stands
 * for a loop of the caller's own routine (PF_LOOP(head, exit)) that waits for
 * port 0 to show `value` (native_wait_loop). */
PF_API void au_driver_wait_echo(Platform *pf, PfOrigin o, uint8_t value);
/* The scene's bounded echo wait: retry the buffered command after `tries`
 * failed polls. `given_up` is the original's retry branch (SNES only). */
PF_API bool au_driver_wait_echo_or_give_up(Platform *pf, PfOrigin o, uint32_t given_up, uint8_t value, uint16_t tries);


/* The host plays the C sound driver in the sound processor's place
 * (frontend/sound_host.h) and takes the game's boot and commands as calls
 * (core/sound_commands.h): nothing is uploaded and nothing waits on the
 * ports. au_driver_hosted says whether one plays (not in the reference play,
 * where the console's sound processor runs the cartridge's driver); true if
 * it took the boot, or the command `c` (the command and its three parameters,
 * as au_command left them in the buffer) with `effects_asked`, the set of
 * effects the game asks its next song to bring. */
PF_API bool au_driver_hosted(Platform *pf);
PF_API bool au_driver_boot_taken(Platform *pf);
PF_API bool au_driver_command_taken(Platform *pf, AudioCommand c, uint8_t effects_asked);

/* ---- The voices ----------------------------------------------------------------------
 *
 * What the sound driver (game/sound/) plays through, the S-DSP's semantics
 * without its mechanics. Eight voices, each playing a sample from the sample
 * directory at a pitch, shaped by an envelope and heard at a volume on each
 * side; a master volume; an echo (a delay line whose output is filtered by
 * eight taps, fed back and mixed in at its own volume, for the voices that
 * send to it); noise at a clock in place of a voice's sample; and pitch
 * modulation of a voice by the one below it. A key on starts the voices' notes
 * with what they are set to, a key off releases them.
 *
 * No register numbers and no write order: a call says what changes. The
 * native mixer (platform/native/mixer.h) is the implementation; an AuVoices
 * is whichever one the driver was given. */
#include "platform/voice_types.h"

/* Ready the voices for a driver: they find their samples in the directory
 * the driver's data was loaded with. */
void au_voices_start(AuVoices *v);

void au_voice_sample(AuVoices *v, uint8_t voice, uint8_t sample);
void au_voice_envelope(AuVoices *v, uint8_t voice, AuEnvelope e);
void au_voice_gain(AuVoices *v, uint8_t voice, AuGain g);
/* The pitch in 4096ths of the sample's own rate; the S-DSP hears 14 bits. */
void au_voice_pitch(AuVoices *v, uint8_t voice, uint16_t pitch);
void au_voice_volume(AuVoices *v, uint8_t voice, int8_t left, int8_t right);
void au_voices_key_on(AuVoices *v, AuVoiceSet voices);
void au_voices_key_off(AuVoices *v, AuVoiceSet voices);
/* Every voice's envelope stops where it is (true: each plays at its gain),
 * or goes on with its ADSR (false): the driver's pause. */
void au_voices_hold(AuVoices *v, bool hold);
/* The level a voice is putting out now, its top 8 bits (signed). */
int8_t au_voice_output(AuVoices *v, uint8_t voice);

void au_master_volume(AuVoices *v, int8_t left, int8_t right);
/* The clock the noise voices play at, 0-31. */
void au_noise_clock(AuVoices *v, uint8_t clock);
/* The output muted (true: nothing is heard, the voices play on unheard). */
void au_output_muted(AuVoices *v, bool muted);
/* Every voice keyed off and held there (true) until let go again (false). */
void au_voices_reset(AuVoices *v, bool reset);
/* The whole output stopped (true): muted and the voices reset, until it is
 * started again (false). */
void au_output_stopped(AuVoices *v, bool stopped);
void au_noise_voices(AuVoices *v, AuVoiceSet voices);
void au_pitch_modulation(AuVoices *v, AuVoiceSet voices);

void au_echo_voices(AuVoices *v, AuVoiceSet voices);
void au_echo_volume(AuVoices *v, int8_t left, int8_t right);
void au_echo_feedback(AuVoices *v, int8_t feedback);
void au_echo_filter(AuVoices *v, const int8_t taps[8]);
/* The echo's delay, in 16 ms units (up to 15); its buffer is where the S-DSP
 * implementation's sound memory keeps it. Change it with the echo's writes
 * off (au_echo_writes), or the old delay's writes land where the new buffer
 * isn't. */
void au_echo_delay(AuVoices *v, uint8_t delay);
/* The echo's buffer placed by hand: starting at a page of sound memory, with
 * a delay, wherever that leaves it. */
void au_echo_buffer(AuVoices *v, uint8_t page, uint8_t delay);
void au_echo_writes(AuVoices *v, bool on);

/* A sample of the driver's own: its BRR bytes, which loop from `loop` on, as
 * sample `sample` of the directory; and one byte of it changed. */
void au_sample_define(AuVoices *v, uint8_t sample, const uint8_t *brr, uint16_t length, uint16_t loop);
void au_sample_write(AuVoices *v, uint8_t sample, uint16_t at, uint8_t byte);

/* The game's samples: BRR bytes in the voices' sound memory, which the
 * driver places itself (game/sound/loads.c), where the original's CPU half
 * uploaded them, since where a sample lies decides whether the echo's buffer
 * runs over it. A byte of one put in place; the samples between two addresses
 * moved down to another, whole 256-byte pages first from the start and then
 * what is left from its end down, as the original's receiver copies them; and
 * sample `sample` of the directory found at `start`, looping from `loop`. */
void au_sample_memory_write(AuVoices *v, uint16_t at, uint8_t byte);
void au_sample_memory_move(AuVoices *v, uint16_t to, uint16_t from, uint16_t length);
void au_sample_place(AuVoices *v, uint8_t sample, uint16_t start, uint16_t loop);

/* What a voice and the mix are set to now (platform/voice_types.h): for a
 * change to part of a setting, which keeps the rest. */
AuVoiceSettings au_voice_settings(AuVoices *v, uint8_t voice);
AuMixSettings au_mix_settings(AuVoices *v);


#endif
