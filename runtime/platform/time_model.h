/* The frame-level time model (docs/REIMPLEMENTATION.md, "The frame-level time
 * model"): the original's time for work that a host does another way, or
 * not at all, in the original machine's master clocks.
 *
 * A port on the machine is timed by its estimate (its bus clocks at its
 * calibrated rate, the timing gate's measure). What no estimate can see is
 * work the port hands to the host instead of doing: the C sound driver takes
 * the CPU half's boot and commands as calls, where the original uploaded the
 * driver, a song's samples and its sequence through the ports a few bytes at
 * a time, waiting for the sound processor's echo at each. These are that
 * time, measured on the machine (the CPU half's port with the original's
 * driver, over the library's movies) and charged where the host takes the
 * work: on the machine as time the port stands waiting (the SNES backend), in
 * a build with no machine as frames the main thread doesn't run (the host
 * timing backend's lag frames, platform/native/timing.c). Nothing here reads
 * a clock or keeps state: the same work costs the same time on every host. */
#ifndef CT_PLATFORM_TIME_MODEL_H
#define CT_PLATFORM_TIME_MODEL_H

#include <stdbool.h>
#include <stdint.h>

enum {
    TIME_FRAME_CLOCKS = 357368,   /* a frame: 262 lines of 1364 master clocks */
    TIME_MASTER_HZ = 21477272,
    TIME_SPC_HZ = 1024000         /* the sound processor's cycles */
};

/* The sound processor's cycles (the C driver's time) in master clocks. */
static inline uint32_t time_of_spc_cycles(uint32_t cycles)
{
    return (uint32_t)((uint64_t)cycles * TIME_MASTER_HZ / TIME_SPC_HZ);
}

/* Master clocks in the sound processor's cycles. */
static inline uint32_t time_in_spc_cycles(uint32_t clocks)
{
    return (uint32_t)((uint64_t)clocks * TIME_SPC_HZ / TIME_MASTER_HZ);
}

/* The sound driver's boot: the boot ROM made ready, the driver's program and
 * data uploaded through it a byte an echo, and the countdown that gives the
 * driver time to start. One cost: the upload is the same every time. The
 * driver starts at its end, so a host's driver, booted at once, starts its
 * time (its first echo's wait) that much later. */
enum { TIME_SOUND_BOOT = 7360649 };

/* A command the CPU half takes, by the wall, under a light vblank handler (a
 * song loads in about twice the time under the menus' busy one, which this
 * doesn't follow: docs/TODO.md). Measured as the original's time per command against what
 * the C driver loads for it, over eight movies' 70 songs (gato-battle,
 * battle-magus, battle-guardian, events-keepers-dome, events-prison-escape,
 * late-omen-battles, menus-save-point, menus-shop-equip):
 *
 * - a command passed on as it is (an effect, a fade, the volume) is put on
 *   the ports and echoed: TIME_SOUND_MESSAGE;
 * - a song's start ($10, $11, $14, $15) is its own exchange and the
 *   tables that go with every song (the directory, the instruments): about 94
 *   echoes, TIME_SOUND_SONG_START, whether or not anything else loads;
 * - each byte the CPU half uploads (the song's missing samples, its sequence
 *   and percussion, the effects it brings back) goes three a message, an echo
 *   a message, with the music stopped: TIME_SOUND_BYTE (400 to 425 a byte
 *   over the songs loaded under a light handler);
 * - the samples a song keeps move down in sound memory before the missing
 *   ones load, the sound processor's copy, which the CPU half waits out:
 *   TIME_SOUND_BYTE_MOVED (what is left of the songs that move, 350 to 430 a
 *   byte);
 * - command $70's effects and $71's sample go up with the music playing,
 *   the sound processor answering between its ticks: TIME_SOUND_BYTE_PLAYING
 *   (750 to 1110 a byte over the switches measured, the least under the
 *   lightest handler). */
enum {
    TIME_SOUND_MESSAGE = 3200,
    TIME_SOUND_SONG_START = 95000,
    TIME_SOUND_BYTE = 405,
    TIME_SOUND_BYTE_PLAYING = 750,
    TIME_SOUND_BYTE_MOVED = 380
};

/* The commands whose uploads go with the music playing, and the songs'
 * starts ($12, $13, $16 and $17 do nothing). */
enum { TIME_SOUND_SONGS = 0x10, TIME_SOUND_AFTER_SONGS = 0x18, TIME_SOUND_NOT_A_START = 0x02,
       TIME_SOUND_SWITCH_EFFECTS = 0x70, TIME_SOUND_LOAD_INSTRUMENT = 0x71 };

static inline bool time_sound_song_start(uint8_t command)
{
    return command >= TIME_SOUND_SONGS && command < TIME_SOUND_AFTER_SONGS && !(command & TIME_SOUND_NOT_A_START);
}

/* The original's time for the game's command `command`, for which the C
 * driver loaded `sent` bytes and moved `moved`. */
static inline uint32_t time_of_sound_command(uint8_t command, uint32_t sent, uint32_t moved)
{
    bool playing = command == TIME_SOUND_SWITCH_EFFECTS || command == TIME_SOUND_LOAD_INSTRUMENT;
    uint32_t base = time_sound_song_start(command) ? TIME_SOUND_SONG_START : TIME_SOUND_MESSAGE;
    return base + sent * (playing ? TIME_SOUND_BYTE_PLAYING : TIME_SOUND_BYTE) + moved * TIME_SOUND_BYTE_MOVED;
}

/* ---- Loads --------------------------------------------------------------------------------
 *
 * What a load spends its frames on, as the game's work charges it (charge.h)
 * and the video service sends it. A frame's own work is taken to fit its
 * frame: only these are charged.
 *
 * - The unpacker ($C3:0002), most of a load's work. Its charge counts the
 *   accesses the original makes to its own cells as it goes (where the port
 *   keeps locals), which follow the stream: its segments, control bytes,
 *   references and items. Over new-game's 60 streams the original took
 *   29.0 M clocks (the shadow run's totals for decompress_direct, which
 *   every stream goes through) for 451,831 accesses charged: 64 an access,
 *   the whole of its time.
 * - The other charges are a frame's work, not a load's: nothing.
 * - A send to video memory from the main thread (in vblank the handlers'
 *   sends are the frame's own): TIME_DMA_BYTE a byte, DMA's rate. */
enum { TIME_UNPACKER = 0xC30002, TIME_UNPACK_ACCESS = 64, TIME_DMA_BYTE = 8 };

static inline uint32_t time_of_charge(uint32_t routine, uint32_t accesses)
{
    return routine == TIME_UNPACKER ? accesses * TIME_UNPACK_ACCESS : 0;
}

static inline uint32_t time_of_transfer(uint32_t bytes) { return bytes * TIME_DMA_BYTE; }

#endif