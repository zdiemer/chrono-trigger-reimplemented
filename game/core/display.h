/* Display and interrupt control shared by every game mode (core/display.c). */
#ifndef CT_GAME_CORE_DISPLAY_H
#define CT_GAME_CORE_DISPLAY_H

#include "platform/platform.h"
#include "snes/system.h"

/* The screen blank and every interrupt, transfer and per-line effect stopped,
 * as a mode does before it changes what the screen shows. */
void display_blank_and_stop(Platform *pf);

enum { DISPLAY_BLANK_AND_STOP_ENTRY = 0xC00B4E };
void display_blank_and_stop_interrupts(Snes *s);

/* The same, as a port runs it where the original calls it: the CPU's
 * interrupts masked too and the data bank $00, as the routine leaves the
 * machine, and timed as the routine. */
void display_go_dark(Snes *s);
/* What it leaves of the machine alone, for a port that stops the screen
 * itself. */
void display_leaves_the_machine_dark(Snes *s);

#endif
