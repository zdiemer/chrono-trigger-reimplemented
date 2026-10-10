/* Timing service (docs/PLATFORM.md, "Timing without a beam"): which of the
 * frame's handlers run, the frame waits, and the budget of the work that can
 * wait a frame.
 *
 * The game's frame is a vblank handler, the main thread's work up to its wait
 * for the next frame, and a line handler that ends the picture (all three of
 * the game's line handlers blank the screen after their line and use the time
 * left for uploads). A backend with a beam (the SNES) runs them where the beam
 * puts them; a host runs them in that order, once a frame, with the main
 * thread as a coroutine that gives the frame up at each wait.
 */
#ifndef CT_PLATFORM_TIMING_H
#define CT_PLATFORM_TIMING_H

#include "platform/platform.h"

/* Interrupt sources for tm_enable_interrupts. */
enum {
    TM_INT_VBLANK = 0x80,       /* the vblank handler */
    TM_INT_LINE = 0x20,         /* the line handler, at its line */
    TM_INT_DOT = 0x10,          /* the line handler, at its dot (the game never enables it) */
    TM_INT_AUTO_JOYPAD = 0x01,  /* controllers read at vblank (input.h) */
};

/* Bits of tm_ack_vblank's result. */
enum { TM_VBLANK_PENDING = 0x80 };
/* Bits of tm_ack_line_irq's result. */
enum { TM_LINE_IRQ_PENDING = 0x80 };
/* Bits of tm_reset_beam_latch's result. */
enum { TM_BEAM_LATCHED = 0x80 };


/* Which handlers the frame runs (TM_INT_* bits; others as given, SNES only). */
PF_API void tm_enable_interrupts(Platform *pf, PfOrigin o, uint8_t sources);
/* The line the line handler (TM_INT_LINE) runs at, from the top of the frame:
 * where the mode's picture ends. */
PF_API void tm_set_line(Platform *pf, PfOrigin o, uint16_t line);
/* Both timers' places (the dot and the line) back to the start of the frame,
 * as the game does where it starts a mode: nothing enables them after, so a
 * backend without them does nothing. */
PF_API void tm_reset_timers(Platform *pf, PfOrigin o);
/* Whether the cartridge's ROM is read at its fast speed: a matter of the SNES
 * backend's timing alone. */
PF_API void tm_fast_rom(Platform *pf, PfOrigin o, bool fast);
/* Acknowledge the vblank interrupt; returns its status: pending inside the
 * vblank handler. */
PF_API uint8_t tm_ack_vblank(Platform *pf, PfOrigin o);
/* Acknowledge the line interrupt; returns its status: pending inside the
 * line handler. */
PF_API uint8_t tm_ack_line_irq(Platform *pf, PfOrigin o);
/* Whether this frame is the odd one, which the scenes use to move something
 * every other frame. */
PF_API bool tm_odd_frame(Platform *pf, PfOrigin o);
/* Restart the beam position latch (the battle's vblank handler does this
 * every frame); returns the status it had. */
PF_API uint8_t tm_reset_beam_latch(Platform *pf, PfOrigin o);
/* Before I/O another processor watches (the APU): let the time the native
 * routine has taken so far pass first. */
PF_API void tm_sync(Platform *pf);

/* The main thread's wait for the next frame: it gives the frame up, and goes
 * on once the next frame's vblank handler has run. `o` names the call of the
 * mode's own wait routine it stands for (PF_CALL_DC: the site, the routine,
 * and what the routine doesn't read), which the SNES backend makes. */
PF_API void tm_wait_frame(Platform *pf, PfOrigin o);
/* Whether the frame still has time for work that can wait for the next one
 * (the field's object scripts and new animation frames): the original stops
 * such work once the picture has reached `line`, until the frame's end. A
 * host has time for all of it. */
PF_API bool tm_frame_time_left(Platform *pf, PfOrigin o, uint16_t line);
/* Wait until the picture has reached `line`. `o` names the call of the wait
 * routine it stands for, as tm_wait_frame's does. A host draws a frame at a
 * time, so it has nothing to wait for. */
PF_API void tm_wait_line(Platform *pf, PfOrigin o, uint16_t line);
/* A random word, for effects that look random (the scenes' noise): the
 * original takes it from where the beam is. A host takes it from a generator
 * the driver seeds, so a replay draws the same words. */
PF_API uint16_t tm_entropy(Platform *pf, PfOrigin o);

#ifdef CT_PLATFORM_SNES
#include "platform/snes/timing.h"
#endif

#endif
