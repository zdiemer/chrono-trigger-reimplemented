/* The GBA's interrupts, each source's to its own handler: the picture's
 * line interrupts (services/video.c) and the sound's timer
 * (services/direct_sound.c) share the one vector. A handler acknowledges its
 * own sources; whoever sets a handler enables its sources. */
#ifndef CT_GBA_INTERRUPTS_H
#define CT_GBA_INTERRUPTS_H

#include <stdint.h>

enum { GBA_IRQ_HBLANK = 0x0002, GBA_IRQ_VCOUNT = 0x0004, GBA_IRQ_TIMER1 = 0x0010 };

/* The handler for the sources in `sources` (GBA_IRQ_*). */
void gba_interrupt_handler(uint16_t sources, void (*handler)(void));

#endif
