/* The GBA's interrupts, each source's to its own handler: the picture's
 * line interrupts (services/video.c) and the sound's timer
 * (services/direct_sound.c) share the one vector. A handler acknowledges its
 * own sources; whoever sets a handler enables its sources. A handler can hand
 * longer work on to run with the interrupts on (gba_interrupt_nested), so
 * that the lines' interrupts don't wait behind it. */
#ifndef CT_GBA_INTERRUPTS_H
#define CT_GBA_INTERRUPTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { GBA_IRQ_HBLANK = 0x0002, GBA_IRQ_VCOUNT = 0x0004, GBA_IRQ_TIMER1 = 0x0010 };

/* The handler for the sources in `sources` (GBA_IRQ_*). */
void gba_interrupt_handler(uint16_t sources, void (*handler)(void));

/* From a handler, with its source acknowledged: `work` run with the
 * interrupts on, on a stack of its own (system/nested.s), the handler going
 * on once it returns. One at a time: the caller makes sure no other is
 * running. */
void gba_interrupt_nested(void (*work)(void));

/* Whether the interrupts' and the nested work's stacks kept within their
 * room (their canaries, which main.c checks each frame), and their size and
 * deepest use (its report). */
bool gba_interrupt_stacks_whole(void);
size_t gba_interrupts_stack_used(void);
size_t gba_interrupts_stack_size(void);
size_t gba_nested_stack_used(void);
size_t gba_nested_stack_size(void);

#endif
