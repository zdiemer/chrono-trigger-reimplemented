/* Bank C0 system routines: display, interrupts and interrupt vectors. */
#include "natives.h"

#include "port/machine.h"

/* $C0:0B4E
 * Blank the display and stop interrupts, DMA and HDMA: force blank on, NMI and
 * timer IRQs and auto-joypad off. Leaves the data bank at 0.
 * Entry: 8-bit A. Exit: A low byte 0, I set, N clear, Z set. */
void c0_disable_display_and_interrupts(Snes *s)
{
    m_set_flag(s, CPU_FLAG_I, true);

    /* The data bank is set by pushing 0 and pulling it, which leaves the 0 in
     * the byte just below the stack pointer. */
    m_push8(s, 0x00);
    s->cpu.db = m_pull8(s);

    m_write8(s, m_abs(s, 0x2100), 0x80);   /* INIDISP: force blank */
    m_write8(s, m_abs(s, 0x4200), 0x00);   /* NMITIMEN */
    m_write8(s, m_abs(s, 0x420B), 0x00);   /* MDMAEN */
    m_write8(s, m_abs(s, 0x420C), 0x00);   /* HDMAEN */

    m_set_a8(s, 0x00);
    m_set_nz8(s, 0x00);
    m_rts(s);
}

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
 * Point the NMI stub at the field NMI handler ($C0:EA63).
 * Entry: 8-bit A, 16-bit X. */
void c0_install_field_nmi(Snes *s)
{
    install_vector_stub(s, 0x0500, 0xC0EA63);
    m_rts(s);
}

/* $C0:0B75
 * Point the IRQ stub at the field IRQ handler ($C0:ECCC).
 * Entry: 8-bit A, 16-bit X. */
void c0_install_field_irq(Snes *s)
{
    install_vector_stub(s, 0x0504, 0xC0ECCC);
    m_rts(s);
}
