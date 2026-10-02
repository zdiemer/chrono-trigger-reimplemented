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
 * $1E00, then a call-out to $C7:0004 from o.site (o.target), so the driver's
 * echo waits and uploads run as before and frames can pass. Returns when the
 * original's JSL would. The WRAM writes are made in one order (parameters, then
 * the command) whatever order the original used: nothing runs between them.
 * APU-direct: the same, and the driver's registry entry runs the command
 * without main-CPU time passing. */
PF_API void au_command(Platform *pf, PfOrigin o, AudioCommand c);

/* The driver's commands the game sends (docs/SOUND.md, "Commands from the CPU"). */
enum {
    AU_START_SONG = 0x10,       /* p1 the song */
    AU_START_EFFECT = 0x18,     /* p1 the effect, p2 where it is panned (0x80 the middle) */
    AU_START_OTHER_EFFECT = 0x19, /* ... on the other pair of effect voices (4-5) */
    AU_FADE_ALL = 0x80,         /* p1 steps, p2 the volume faded to: everything */
    AU_FADE_MUSIC = 0x81,       /* ... the music */
    AU_FADE_EFFECTS = 0x82,     /* ... the effects on voices 6-7 */
    AU_FADE_MORE_EFFECTS = 0x83 /* ... the effects on voices 4-5 */
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

/* Fade `what` (AU_FADE_*) to `volume` over `steps` steps (0: at once). */
static inline void au_fade(Platform *pf, PfOrigin o, uint8_t what, uint8_t steps, uint8_t volume)
{
    au_command(pf, o, (AudioCommand){ .command = what, .p1 = steps, .p2 = volume, .count = 2 });
}

/* Driver status the game polls: the byte the driver shows on port 1-3. Doesn't
 * sync: when a poll happens is the caller's business. */
PF_API uint8_t au_status(Platform *pf, PfOrigin o, uint8_t port);

/* What the status says (docs/SOUND.md, "What the game reads back"): the steps
 * the music's fade has left, and which of the music's tracks are playing,
 * folded into four bits (none once the song has ended). */
static inline uint8_t au_music_fade_left(Platform *pf, PfOrigin o) { return au_status(pf, o, 1); }
static inline uint8_t au_music_playing(Platform *pf, PfOrigin o) { return au_status(pf, o, 3) & 0x0F; }

/* Host side: up to max_frames stereo frames at 32000 Hz; returns how many. */
PF_API size_t au_take_samples(Platform *pf, int16_t *out, size_t max_frames);

/* Below the command interface: Square's protocol. These are for the CPU half of
 * the driver (core/sound_driver.c: the upload at $C7:0000, the commands at
 * $C7:0004 and their uploads) and for the scene code that sends the driver
 * messages the same way (scene/program.c); tests/test_platform_lint.py allows
 * them nowhere else. A backend that replaces the driver never sees them.
 *
 * au_driver_write and au_driver_read access port 0-3. SNES backend: after
 * tm_sync, since the SPC700 watches the timing (the driver writes ports 1-3
 * before port 0, and waits for echoes). APU-direct, inside a run of the driver:
 * the time the code has taken passes on the APU instead. */
PF_API void au_driver_write(Platform *pf, PfOrigin o, uint8_t port, uint8_t v);
PF_API uint8_t au_driver_read(Platform *pf, PfOrigin o, uint8_t port);

/* The first thing the registry entries that start an exchange do (the upload,
 * a command, the scene's send): true if APU-direct ran the exchange, from the
 * entry's PC until PC leaves [lo, hi], without main-CPU time passing; the entry
 * then returns at once. SNES backend: false. */
PF_API bool au_driver_run_direct(Platform *pf, uint32_t lo, uint32_t hi);

/* A JSR the driver makes to a routine that waits for the SPC700 (o: the JSR's
 * site and target). SNES backend: a call-out, so frames pass while it waits.
 * APU-direct, inside a run: the callee runs synchronously. `dont_care` names
 * the caller's registers and flags the callee doesn't read (DC_* bits,
 * port/native.h), which shadow mode doesn't compare at the call: a port written
 * as logic apart from the registers sets only what the callee takes. */
PF_API void au_driver_call(Platform *pf, PfOrigin o, uint16_t dont_care);

/* A host that plays its own driver in the SPC700's place (the C sound driver,
 * frontend/sound_host.h) takes the CPU half's boot and commands as calls:
 * nothing is uploaded and nothing waits on the ports. The CPU half's two
 * entries ask it first: true if it took the boot, or the command in the
 * buffer au_command fills (with the set of effects the game asks its next
 * song to bring, which it keeps beside the buffer). False with the original
 * driver. */
PF_API bool au_driver_boot_taken(Platform *pf);
PF_API bool au_driver_command_taken(Platform *pf);

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
 * No register numbers and no write order: a call says what changes. The first
 * implementation (platform/voices_dsp.h) makes each change as the S-DSP
 * register writes that make it, so the C driver can be checked register for
 * register against the original (ct_dev --sound-lockstep) and can play through
 * the emulated S-DSP; a native mixer is the second (docs/PLAN.md, Phase 4a).
 * An AuVoices is whichever of them the driver was given. */
typedef struct AuVoices AuVoices;

enum { AU_VOICES = 8 };
typedef uint8_t AuVoiceSet;   /* a bit per voice, voice 0 the lowest */

/* An ADSR envelope: attack rate 0-15, decay rate 0-7, the level the decay
 * stops at 0-7 (in eighths, less one), and the rate it falls at from there
 * 0-31. `adsr` false plays the voice at its gain instead. */
typedef struct {
    bool adsr;
    uint8_t attack, decay, sustain_level, sustain_rate;
} AuEnvelope;

/* A gain, what a voice plays at when its ADSR is off: a level held
 * (AU_GAIN_DIRECT, 0-127), or a rate 0-31 it moves at (0 holds it still). */
typedef enum {
    AU_GAIN_DIRECT,
    AU_GAIN_DECREASE_LINEAR,
    AU_GAIN_DECREASE_EXPONENTIAL,
    AU_GAIN_INCREASE_LINEAR,
    AU_GAIN_INCREASE_BENT
} AuGainMode;
typedef struct {
    AuGainMode mode;
    uint8_t value;
} AuGain;

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
/* The whole output stopped (true): nothing is heard, and every voice is keyed
 * off and held there until it is started again (false). */
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

/* The SNES escape: an S-DSP register written as it is, for the driver's
 * command that asks for exactly that ($FD). */
void au_voices_snes_register(AuVoices *v, uint8_t reg, uint8_t value);

/* Backends selectable at run time on the development system (host side).
 * APU-direct runs the SPC700 outside the machine's timeline
 * (platform/direct/audio.h), so it can't keep the original's timing and is
 * never the default. */
typedef enum { AU_BACKEND_SNES, AU_BACKEND_DIRECT } AuBackend;
PF_API void au_select_backend(Platform *pf, AuBackend backend);

#ifdef CT_PLATFORM_SNES
#include "platform/snes/audio.h"
#endif

#endif
