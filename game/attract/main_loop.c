/* Code the game decompresses to $7E:3000 and runs from RAM during the attract
 * sequence and title screen. Every entry here has a guard that checks, by
 * hash, that RAM holds the code the port was written for: the same addresses
 * hold other code and data at other times. */
#include "attract/attract.h"

#include "port/machine.h"

/* Guard for the frame-wait loop at $7E:3023: the loop's 7 bytes, and an RTS
 * at $7E:3176, the routine the loop calls. */
bool attract_wait_loop_present(Snes *s)
{
    return snes_bus_peek(&s->bus, 0x7E3176) == 0x60 && m_code_matches(s, 0x7E3023, 7, 0x8FB1B63AAE3F69D0ull);
}

/* $7E:3023 (attract/title main loop)
 * Waits for the NMI handler to set the 16-bit flag at direct page $00, calling
 * an empty routine on each pass. Idle entry: one pass of the loop, entered
 * with 16-bit A. Exits to $7E:302A. */
void attract_wait_loop(Snes *s)
{
    m_push16(s, 0x3025);   /* JSR $3176 ... */
    m_rts(s);              /* ... which is an RTS */
    uint16_t flag = m_lda16(s, m_read16(s, m_dp(s, 0x00)));
    s->cpu.pc = flag ? 0x302A : 0x3023;
}

/* Guard for the vblank wait at $7E:31B0-$7E:31BE. */
bool attract_vblank_wait_present(Snes *s)
{
    return m_code_matches(s, 0x7E31B0, 15, 0x04AE9A24245E978Dull);
}

/* $7E:31B0: wait while the display is in vblank ($4212 bit 7 set). Idle entry,
 * 8-bit A. Continues to the second loop at $7E:31B7. */
void attract_vblank_end_loop(Snes *s)
{
    uint8_t status = m_lda8(s, m_read8(s, m_abs(s, 0x4212)));
    m_bit_imm8(s, 0x80);
    if (!(status & 0x80))
        s->cpu.pc = 0x31B7;
}

/* $7E:31B7: wait until vblank starts, then return. Idle entry, 8-bit A. */
void attract_vblank_start_loop(Snes *s)
{
    uint8_t status = m_lda8(s, m_read8(s, m_abs(s, 0x4212)));
    m_bit_imm8(s, 0x80);
    if (status & 0x80) {
        s->cpu.pc = 0x31BE;
        m_rts(s);
    }
}
