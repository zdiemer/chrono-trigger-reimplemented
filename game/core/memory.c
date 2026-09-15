/* A block move between banks chosen at run time. */
#include "core/memory.h"

#include "port/machine.h"

/* Guard: the stub's MVN opcode and RTL are in place. Its two bank operands
 * vary; the callers write them. */
bool memory_move_stub_present(Snes *s)
{
    return snes_bus_peek(&s->bus, 0x7E9690) == 0x54 && snes_bus_peek(&s->bus, 0x7E9693) == 0x6B;
}

// $7E:9690
/* MVN dst,src : RTL, which the menu code ($C2:8635, $C2:F710) and the save
 * code ($FF:FAA3) write to RAM with the banks they need: copies A+1 bytes from
 * src:X to dst:Y, leaving A = $FFFF and the data bank = dst. Entered with
 * 16-bit X. */
void memory_move_stub(Snes *s)
{
    uint8_t dst = m_read8(s, 0x7E9691);
    uint8_t src = m_read8(s, 0x7E9692);
    m_mvn(s, dst, src);
    m_rtl(s);
}
