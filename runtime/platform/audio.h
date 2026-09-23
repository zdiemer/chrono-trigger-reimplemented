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
 * the music's fade has left, and the marker the playing song last set (its
 * sequence's event $F9), which a scene waits on to keep in time with it. */
static inline uint8_t au_music_fade_left(Platform *pf, PfOrigin o) { return au_status(pf, o, 1); }
static inline uint8_t au_song_marker(Platform *pf, PfOrigin o) { return au_status(pf, o, 3) & 0x0F; }

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
