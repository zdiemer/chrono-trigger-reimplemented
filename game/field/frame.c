/* Field mode: synchronizing the main loop with the display. */
#include "field/field.h"

#include "port/machine.h"

/* $C0:EC71 (loop head inside the wait-for-next-frame routine at $C0:EC60)
 * Spins until the NMI handler clears the pending-frame flag at $0152, then
 * returns. Idle entry: one pass of the loop. */
void field_wait_for_frame_loop(Snes *s)
{
    uint8_t pending = m_read8(s, m_abs(s, 0x0152));
    m_set_a8(s, pending);
    m_set_nz8(s, pending);
    if (pending != 0)
        return;              /* still waiting: PC stays at the loop head */
    s->cpu.pc = 0xEC76;      /* the RTS after the loop */
    m_rts(s);
}
