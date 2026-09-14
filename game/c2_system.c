/* Bank C2 system routines. */
#include "natives.h"

#include "port/machine.h"

/* $C2:0348 (loop head inside $C2:0346, "wait for the next frame")
 * Spins until the NMI handler changes the frame counter at $4B from the value
 * loaded into A, then returns. Idle entry: one pass of the loop. */
void c2_wait_frame_loop(Snes *s)
{
    uint8_t counter = m_read8(s, m_dp(s, 0x4B));
    m_cmp8(s, (uint8_t)s->cpu.a, counter);
    if (counter == (uint8_t)s->cpu.a)
        return;
    s->cpu.pc = 0x034C;
    m_rts(s);
}

/* $C2:81CA (loop inside the routine starting the C2 mode loop)
 * Spins until the NMI handler sets $0D01, then continues at $C2:81CF.
 * Idle entry: one pass of the loop. */
void c2_wait_ready_loop(Snes *s)
{
    uint8_t ready = m_read8(s, m_abs(s, 0x0D01));
    m_set_a8(s, ready);
    m_set_nz8(s, ready);
    if (ready != 0)
        s->cpu.pc = 0x81CF;
}
