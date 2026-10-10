/* The platform services' host backends: what a build with no emulator passes
 * the game as its Platform (docs/PLATFORM.md, "Timing without a beam"). Such a
 * build defines CT_PLATFORM_EXTERN, so the services' headers declare the calls
 * and these files define them.
 *
 * Timing and input exist so far. The frame is the driver's: it calls
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

#include "platform/input.h"
#include "platform/timing.h"
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

struct Platform {
    TmHost timing;
    PadBits pads[4];        /* in_host_set's, which in_read reads */
};

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
