/* Display and interrupt control shared by every game mode. */
#include "core/display.h"

#include "port/machine.h"

/* $C0:0B4E
 * Blank the display and stop interrupts, DMA and HDMA: force blank on, NMI and
 * timer IRQs and auto-joypad off. Leaves the data bank at 0.
 * Entry: 8-bit A. Exit: A low byte 0, I set, N clear, Z set. */
void display_blank_and_stop_interrupts(Snes *s)
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
