/* Field mode: taking over the interrupts.
 *
 * Each mode sends the two interrupts to its own handlers (core/program.c).
 * Entering the field sends them to the field's vblank handler (field/nmi.c)
 * and its line interrupt (field/display.c). */
#include "field/field.h"

#include "core/program.h"
#include "port/machine.h"
#include "port/vars_ram.h"

enum { FIELD_VBLANK_HANDLER = 0xC0EA63, FIELD_LINE_HANDLER = 0xC0ECCC };

// $C0:0B64
void field_take_the_vblank_interrupt(GameState *g) { program_install_vblank_handler(g, FIELD_VBLANK_HANDLER); }

// $C0:0B75
void field_take_the_line_interrupt(GameState *g) { program_install_line_handler(g, FIELD_LINE_HANDLER); }

/* Nothing they leave in the registers is read after (the entries' don't-care
 * sets). */
void field_install_nmi(Snes *s)
{
    field_take_the_vblank_interrupt(state_of(s));
    m_rts(s);
}

void field_install_irq(Snes *s)
{
    field_take_the_line_interrupt(state_of(s));
    m_rts(s);
}
