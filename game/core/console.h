/* The console: how the machine reaches the program at power-on and after a
 * reset, and how the two interrupts reach the running mode's handlers
 * (core/console.c; the program itself is core/program.h). */
#ifndef CT_GAME_CORE_CONSOLE_H
#define CT_GAME_CORE_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

#include "snes/system.h"

/* Where the game's stack starts, as the boot and every restart set it. */
enum { CONSOLE_STACK_TOP = 0x06FF };

/* The entries: the reset, the boot, and the interrupts' way through the
 * cartridge's vectors and the stubs in work RAM, with the stubs' guards. */
void console_reset(Snes *s);
void console_boot(Snes *s);
void console_vblank_vector(Snes *s);
void console_line_vector(Snes *s);
void console_vblank_stub(Snes *s);
void console_line_stub(Snes *s);
bool console_vblank_stub_is_set(Snes *s);
bool console_line_stub_is_set(Snes *s);

#endif
