/* Field mode: installing its interrupt handlers. */
#include "field/field.h"

#include "port/machine.h"

/* Writes a JML opcode and a 24-bit target into one of the RAM interrupt stubs
 * the ROM vectors jump through ($0500 NMI, $0504 IRQ), the way the originals
 * do: A = $5C, X = target address (16-bit), A = target bank. */
static void install_vector_stub(Snes *s, uint16_t stub, uint32_t target)
{
    m_write8(s, m_abs(s, stub), 0x5C);                       /* JML */
    s->cpu.x = target & 0xFFFF;
    m_write16(s, m_abs(s, stub + 1), s->cpu.x);
    m_set_a8(s, target >> 16);
    m_write8(s, m_abs(s, stub + 3), target >> 16);
    m_set_nz8(s, target >> 16);
}

/* $C0:0B64
 * Point the NMI stub at the field NMI handler (field_nmi, $C0:EA63).
 * Entry: 8-bit A, 16-bit X. */
void field_install_nmi(Snes *s)
{
    install_vector_stub(s, 0x0500, 0xC0EA63);
    m_rts(s);
}

/* $C0:0B75
 * Point the IRQ stub at the field IRQ handler ($C0:ECCC).
 * Entry: 8-bit A, 16-bit X. */
void field_install_irq(Snes *s)
{
    install_vector_stub(s, 0x0504, 0xC0ECCC);
    m_rts(s);
}
