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
void display_init_registers(Snes *s);

#endif
