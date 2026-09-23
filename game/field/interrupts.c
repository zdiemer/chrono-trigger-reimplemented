/* Field mode: taking over the interrupts.
 *
 * The ROM's interrupt vectors jump through two stubs in work RAM (nmi_vector
 * and irq_vector), each a long jump the running mode points at its own
 * handler. Entering the field points them at the field's vblank handler
 * (field/nmi.c) and its line interrupt (field/display.c). */
#include "field/field.h"

#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

enum { LONG_JUMP = 0x5C };
enum { FIELD_VBLANK_HANDLER = 0xC0EA63, FIELD_LINE_HANDLER = 0xC0ECCC };

static uint32_t jump_to(uint32_t handler) { return LONG_JUMP | handler << 8; }

// $C0:0B64
static void install_nmi(GameState *g) { set_nmi_vector(g, jump_to(FIELD_VBLANK_HANDLER)); }

// $C0:0B75
static void install_irq(GameState *g) { set_irq_vector(g, jump_to(FIELD_LINE_HANDLER)); }

/* Nothing they leave in the registers is read after (the entries' don't-care
 * sets). */
void field_install_nmi(Snes *s)
{
    install_nmi(state_of(s));
    m_rts(s);
}

void field_install_irq(Snes *s)
{
    install_irq(state_of(s));
    m_rts(s);
}
