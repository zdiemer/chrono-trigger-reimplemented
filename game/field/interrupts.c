#include "charge.h"
/* Field mode: taking over the interrupts.
 *
 * Each mode sends the two interrupts to its own handlers (core/program.c).
 * Entering the field sends them to the field's vblank handler (field/nmi.c)
 * and its line interrupt (field/display.c). */
#include "field/field.h"

#include "core/program.h"
#include "port/vars_ram.h"

// $C0:0B64
void field_take_the_vblank_interrupt(GameState *g) { program_hand_the_vblank_to(g, field_vblank);
    charge_accesses(g, 4); }

// $C0:0B75
void field_take_the_line_interrupt(GameState *g) { program_install_line_handler(g, field_line);
    charge_accesses(g, 4); }
