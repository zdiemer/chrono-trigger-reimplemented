/* Timing service (docs/PLATFORM.md): interrupts, their acknowledgement, the
 * video status the game polls, and syncing with other processors.
 *
 * The status reads return the byte as the SNES reads it, so ports keep the
 * register and flag effects of the original's LDA/CMP/BIT. Only the named bits
 * have a meaning for other backends.
 */
#ifndef CT_PLATFORM_TIMING_H
#define CT_PLATFORM_TIMING_H

#include "platform/platform.h"

/* Interrupt sources for tm_enable_interrupts. */
enum {
    TM_INT_VBLANK = 0x80,       /* the vblank handler */
    TM_INT_LINE = 0x20,         /* the line handler, at its line */
    TM_INT_DOT = 0x10,          /* the line handler, at its dot */
    TM_INT_AUTO_JOYPAD = 0x01,  /* controllers read at vblank (input.h) */
};

/* Bits of tm_ack_vblank's result. */
enum { TM_VBLANK_PENDING = 0x80 };
/* Bits of tm_ack_line_irq's result. */
enum { TM_LINE_IRQ_PENDING = 0x80 };
/* Bits of tm_video_status's result. */
enum { TM_STATUS_VBLANK = 0x80, TM_STATUS_HBLANK = 0x40, TM_STATUS_JOYPAD_BUSY = 0x01 };
/* Bits of tm_reset_beam_latch's result. */
enum { TM_BEAM_LATCHED = 0x80 };

/* Which interrupts are delivered (TM_INT_* bits; others as given, SNES only). */
PF_API void tm_enable_interrupts(Platform *pf, PfOrigin o, uint8_t sources);
/* Acknowledge the vblank interrupt; returns its status. */
PF_API uint8_t tm_ack_vblank(Platform *pf, PfOrigin o);
/* Acknowledge the line interrupt; returns its status. */
PF_API uint8_t tm_ack_line_irq(Platform *pf, PfOrigin o);
/* Blanking and controller-read status now. */
PF_API uint8_t tm_video_status(Platform *pf, PfOrigin o);
/* Restart the beam position latch (the battle NMI does this every frame);
 * returns the status it had. */
PF_API uint8_t tm_reset_beam_latch(Platform *pf, PfOrigin o);
/* Before I/O another processor watches (the APU): let the time the native
 * routine has taken so far pass first. */
PF_API void tm_sync(Platform *pf);

#ifdef CT_PLATFORM_SNES
#include "platform/snes/timing.h"
#endif

#endif
