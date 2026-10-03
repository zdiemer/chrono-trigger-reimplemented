/* The console: how the game starts at power-on and after a reset, and how the
 * two interrupts reach the running mode's handlers (core/console.c). */
#ifndef CT_GAME_CORE_CONSOLE_H
#define CT_GAME_CORE_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/platform.h"
#include "snes/system.h"
#include "state.h"

/* Where the game's stack starts, as the boot and every restart set it. */
enum { CONSOLE_STACK_TOP = 0x06FF };

/* The console's own registers and the picture's as the game starts: no
 * interrupts or transfers, and the picture blanked with every setting at its
 * start. */
void console_starts(GameState *g, Platform *pf);

/* Each game mode sends the two interrupts to its own handlers: the vblank's,
 * which sends the frame's work to the screen, and the line's, which ends the
 * picture where the mode wants it. `handler` is the handler's address. */
void console_install_vblank_handler(GameState *g, uint32_t handler);
void console_install_line_handler(GameState *g, uint32_t handler);

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
