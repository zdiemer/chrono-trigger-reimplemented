/* The platform services' host backends: what a build with no emulator passes
 * the game as its Platform (docs/PLATFORM.md, "Timing without a beam"). Such a
 * services' headers declare the calls
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
 * returns (it waits for frames instead), and its two handlers; and, if the
 * game's state keeps it, the original's time of the game's work since the
 * last call (its own work at the rates the machine calibrated, and what it
 * was charged: runtime/port/state_host.c), taken as the main thread waits and
 * as the main thread starts its part of a frame, where it is the handlers'. */
typedef struct {
    void (*main_thread)(Platform *pf, void *ctx);
    void (*vblank)(Platform *pf, void *ctx);
    void (*line)(Platform *pf, void *ctx);
    void *ctx;
    uint32_t (*owed)(void *ctx);
    /* an access the game made through a service rather than its own memory
     * (the cartridge's save RAM), of `clocks` master clocks on the machine,
     * which the game's state times as its own (TmHostProgram.owed) */
    void (*accessed)(void *ctx, uint32_t clocks);
    /* optional: the main thread's wait at `frame` lasts `frames` more, for
     * `owed` master clocks of work (ct_native --lag-log) */
    void (*lagged)(void *ctx, uint32_t frame, uint32_t frames, uint32_t owed);
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
    uint32_t owed;          /* the original's time charged to the main thread since it last waited (master clocks) */
    uint32_t handler_charges; /* service work in the line/vblank handlers, taken with their accesses */
    uint32_t handlers;      /* the handlers' time in the frame the main thread runs in (master clocks) */
    uint32_t lagging;       /* frames the main thread sits out, its wait lasting through them */
    uint32_t lag_frames;    /* ... in all */
} TmHost;

/* The picture: every video call folded into its state by the observer
 * (platform/observe.h), which keeps VRAM, the palette, the sprite table and
 * the settings as the calls leave them, reading what a call names of the
 * game's memory through the driver's reader; a driver draws the frame it
 * makes (vid_host_picture) with the native renderer. */
enum { VID_HOST_LAST_LINE = 224 };
typedef struct {
    VidObserver *observer;
    VidDisplay display;          /* the picture's from its top line */
    uint8_t line_effects;        /* vid_enable_line_effects' slots, from the top line */
    /* What the line handler changed of the display and the line effects
     * (where the field, the battles and the scenes end their picture: the
     * rest blanked, the line effects off), or a vid_raster_display at a line:
     * the picture from each change's own line down this frame, and the whole of it
     * from the next frame on, until a call sets it again. Taken as the whole
     * frame's, the picture's end would blank all of it, and its line effects
     * (the layers' and windows' settings line by line) would be lost above. */
    uint16_t display_from, effects_from; /* independent first lines for the two changes */
    bool below_display, below_line_effects;
    VidDisplay display_below;
    uint8_t line_effects_below;
    uint32_t screens_applied;
    VidView view;                /* vid_host_set_view's; zero: the original's screen */
    int16_t layer_left[4], layer_right[4];   /* vid_show_a_layer_across's this frame */
    uint8_t layout_count;                    /* vid_lay_out_a_map_across's this frame */
    uint16_t layout_map[VID_MAP_LAYOUTS];
    VidMapLayout layouts[VID_MAP_LAYOUTS];
    bool parted;                             /* vid_part_the_windows's this frame */
    VidWindowParting parting;
} VidHost;

/* The sound: the C sound driver (game/sound/) played in the SPC700's place,
 * which the driver supplies (frontend/sound_native.h), as frontend/sound_host.h
 * plays it beside the machine. The game's CPU half hands it the boot and the
 * commands (au_driver_boot_taken, au_driver_command_taken); a command the
 * game sends through the service (au_command) goes to the CPU half, which the
 * driver calls as C (`send`); the scenes write commands to the ports and read
 * the driver's answers there; and while the game waits on a port, the
 * driver's time passes (`pass`, in the SPC700's cycles). The boot and the
 * commands answer the time the original's uploads took, charged to the
 * caller's share of the frame (tm_host_charge). With no player the sound is silent and
 * its ports read 0. */
typedef struct {
    void *ctx;
    void (*send)(void *ctx, AudioCommand c);
    uint32_t (*boot)(void *ctx);   /* each answers the original's time for what it took (platform/time_model.h) */
    uint32_t (*command)(void *ctx, AudioCommand c, uint8_t effects_asked);
    void (*write)(void *ctx, uint8_t port, uint8_t v);
    uint8_t (*read)(void *ctx, uint8_t port);
    void (*pass)(void *ctx, uint32_t cycles);
} AuHost;

/* The cartridge's save memory, which the driver hands over and keeps. */
typedef struct {
    uint8_t *bytes;              /* SAVE_SIZE of them */
    bool dirty;                  /* written since sv_host_bytes last said */
    void *ctx;
    void (*written)(void *ctx, uint16_t offset); /* optional driver observer, after a write */
} SvHost;

struct Platform {
    TmHost timing;
    PadBits pads[4];        /* in_host_set's, which in_read reads */
    VidHost video;
    AuHost audio;
    SvHost save;
};

/* The picture's state from the game's memory, read through the driver's
 * callback; and the frame it makes, its line effects run down the picture. */
void vid_host_start(Platform *pf, uint8_t (*read)(void *ctx, uint32_t addr), void *ctx);
void vid_host_stop(Platform *pf);
/* How much of the picture the game shows (vid_view): the driver's to choose,
 * the original's screen until it does. A snapshot leaves it as it is. */
void vid_host_set_view(Platform *pf, VidView view);
const VidFrame *vid_host_picture(Platform *pf);
/* A new frame begins: what the last one changed below a line holds from its
 * top (tm_host_frame calls it). */
void vid_host_frame_begins(Platform *pf);

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
/* The frame-level time model (platform/time_model.h): the original's time,
 * in master clocks, for work the caller did another way (the sound
 * driver's uploads, a load's sends to video memory), beside the game's own
 * work its state keeps. A handler's charge belongs to that frame's handler
 * budget, including on lag frames; it never builds up main-thread debt.
 * Where the main thread waits for the next frame having filled whole
 * frames (a frame's time less the handlers'), the wait lasts as many more:
 * lag frames, in which the handlers run and the main thread doesn't, as the
 * original's frames went by while it worked. What is left fits in the frame,
 * and goes. */
void tm_host_charge(Platform *pf, uint32_t clocks);
/* The main thread sits out what it owes now, rather than at its next wait,
 * where what it does next changes the frames that go by: another processor's
 * time runs on from it (a song's start, whose echo the sound driver waits for
 * meanwhile), or the handlers change (tm_enable_interrupts: a setup the
 * original ran with its vblank handler off passes with it off). The whole
 * frames filled pass with the handlers as they are, and the main thread goes
 * on where it stood, what is left over still owed. */
void tm_host_sit_out_owed(Platform *pf);
/* The game's access through a service (TmHostProgram.accessed). */
void tm_host_game_access(Platform *pf, uint32_t clocks);
/* Lag frames so far. */
uint32_t tm_host_lag_frames(const Platform *pf);

/* The services' state as bytes for a snapshot, taken at a frame's end, and
 * put back from them (platform/native/snapshot.c); writes when `room` is
 * enough and answers the bytes it needs either way. What binds the services
 * to the run -- callbacks, the observer, the sound and save hosts -- stays. */
size_t pf_host_snapshot(const Platform *pf, uint8_t *out, size_t room);
bool pf_host_restore(Platform *pf, const uint8_t *in, size_t len);

#endif
