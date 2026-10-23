/* The platform services' host backends: what a build with no emulator passes
 * the game as its Platform (docs/PLATFORM.md, "Timing without a beam"). Such a
 * build defines CT_PLATFORM_EXTERN, so the services' headers declare the calls
 * and these files define them.
 *
 * Timing, input, video, audio and saves. The frame is the driver's: it calls
 * tm_host_frame once per displayed frame, and that runs, in order,
 *
 *   1. the vblank handler, if the game has it enabled;
 *   2. the game's main thread, resumed where it last waited, until it waits
 *      for the next frame (tm_wait_frame);
 *   3. the line handler, if the game has it enabled, which ends the picture.
 *
 * The main thread runs as a coroutine (runtime/port/fiber.h): its waits are
 * deep in its call stack (event commands, fades, battle scripts), and a wait
 * gives the rest of the frame back to the driver. A coroutine the main thread
 * runs in turn (the menus' tasks) may wait too: the next frame resumes
 * whichever fiber waited. Nothing here sleeps or reads a clock; a frame takes
 * as long as its work. */
#ifndef CT_PLATFORM_NATIVE_HOST_H
#define CT_PLATFORM_NATIVE_HOST_H

#include "platform/audio.h"
#include "platform/input.h"
#include "platform/save.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/fiber.h"

/* The game, as the driver runs it: its main thread from power-on, which never
 * returns (it waits for frames instead), and its two handlers. */
typedef struct {
    void (*main_thread)(Platform *pf, void *ctx);
    void (*vblank)(Platform *pf, void *ctx);
    void (*line)(Platform *pf, void *ctx);
    void *ctx;
} TmHostProgram;

/* Where the frame is: which of its three parts runs. */
typedef enum { TM_HOST_IDLE, TM_HOST_IN_VBLANK, TM_HOST_IN_MAIN, TM_HOST_IN_LINE } TmHostPart;

typedef struct {
    TmHostProgram program;
    Fiber *driver;          /* the driver's stack, which tm_host_frame runs on */
    Fiber *main;            /* the main thread's */
    Fiber *waiting;         /* the fiber that waited for this frame, which it resumes */
    TmHostPart part;
    uint8_t interrupts;     /* tm_enable_interrupts' */
    uint16_t line;          /* tm_set_line's */
    uint32_t frame;         /* frames run */
    uint32_t entropy;       /* tm_entropy's generator */
} TmHost;

/* The picture: every video call folded into its state by the observer
 * (platform/observe.h), which keeps VRAM, the palette, the sprite table and
 * the settings as the calls leave them, reading what a call names of the
 * game's memory through the driver's reader. Nothing draws it yet
 * (docs/TODO.md, "The native program"). The SNES's escapes a host has no
 * meaning for are counted, and the first one's site kept. */
enum { VID_HOST_LAST_LINE = 224 };
typedef struct {
    VidObserver *observer;
    void (*write)(void *ctx, uint32_t addr, uint8_t v);   /* the game's memory, for the one escape that writes it */
    void *ctx;
    VidDisplay display;          /* the latest a call set */
    uint8_t line_effects;        /* vid_enable_line_effects' slots */
    uint32_t screens_applied;
    int16_t multiplicand;        /* the Mode 7 matrix's a, and b's high byte: the multiplier's operands */
    int8_t multiplier;
    uint32_t escapes;
    uint32_t first_escape;
} VidHost;

/* The sound: the C sound driver (game/sound/) played in the SPC700's place,
 * which the driver supplies (frontend/sound_native.h), as frontend/sound_host.h
 * plays it beside the machine. The game's CPU half hands it the boot and the
 * commands (au_driver_boot_taken, au_driver_command_taken); a command the
 * game sends through the service (au_command) goes to the CPU half, which the
 * driver calls as C (`send`); the scenes write commands to the ports and read
 * the driver's answers there; and while the game waits on a port, the
 * driver's time passes (`pass`, in the SPC700's cycles). With no player the
 * sound is silent and its ports read 0. */
typedef struct {
    void *ctx;
    void (*send)(void *ctx, AudioCommand c);
    void (*boot)(void *ctx);
    void (*command)(void *ctx, AudioCommand c, uint8_t effects_asked);
    void (*write)(void *ctx, uint8_t port, uint8_t v);
    uint8_t (*read)(void *ctx, uint8_t port);
    void (*pass)(void *ctx, uint32_t cycles);
} AuHost;

/* The cartridge's save memory, which the driver hands over and keeps. */
typedef struct {
    uint8_t *bytes;              /* SAVE_SIZE of them */
    bool dirty;                  /* written since sv_host_bytes last said */
} SvHost;

struct Platform {
    TmHost timing;
    PadBits pads[4];        /* in_host_set's, which in_read reads */
    VidHost video;
    AuHost audio;
    SvHost save;
};

/* The picture's state from the game's memory, read and (for the escape that
 * clears it) written through the driver's callbacks; and the frame it makes,
 * its line effects run down the picture. */
void vid_host_start(Platform *pf, uint8_t (*read)(void *ctx, uint32_t addr),
                    void (*write)(void *ctx, uint32_t addr, uint8_t v), void *ctx);
void vid_host_stop(Platform *pf);
const VidFrame *vid_host_picture(Platform *pf);

/* Start the game: its main thread is made, to run from the first frame, and
 * tm_entropy's generator seeded (a replay seeds it the same). */
void tm_host_start(Platform *pf, const TmHostProgram *program, uint32_t seed);
/* One displayed frame, as above. */
void tm_host_frame(Platform *pf);
/* Where the picture ends, as the game left it this frame: the line handler's
 * line and the lines above it, where the handler blanks the rest (a mode's
 * fact, which a widescreen or GBA build can use); 0 when no line handler
 * runs and the whole picture shows. */
uint16_t tm_host_picture_lines(const Platform *pf);
/* Frames run so far. */
uint32_t tm_host_frames(const Platform *pf);

#endif
