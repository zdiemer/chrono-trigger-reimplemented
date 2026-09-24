/* Display and interrupt control shared by every game mode (core/display.c). */
#ifndef CT_GAME_CORE_DISPLAY_H
#define CT_GAME_CORE_DISPLAY_H

#include "snes/system.h"

void display_blank_and_stop_interrupts(Snes *s);
void display_init_registers(Snes *s);

#endif
