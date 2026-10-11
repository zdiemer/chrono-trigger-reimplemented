/* Code the game decompresses to $7E:3000 and runs from RAM during the attract
 * sequence and title screen: the scene program, whose ports are in game/scene/
 * (scene/scene.h says where; these belong with them). Every entry here has a guard that checks, by
 * hash, that RAM holds the code the port was written for: the same addresses
 * hold other code and data at other times. */
#include "attract/attract.h"

#include "port/machine.h"

/* The frame wait at $7E:3023 calls a routine each pass, whose address the
 * program keeps in the call's own operand ($7E:3024): the title's is an empty
 * one (an RTS, $7E:3176), and the attract demo sets its own frame work there
 * ($7E:51D1), which is the attract program's still. */
enum { WAIT_LOOP = 0x7E3023, FRAME_ROUTINE_OPERAND = 0x7E3024, CODE_BANK = 0x7E0000, RTS = 0x60 };
enum { JSR = 0x20 };

static uint16_t frame_routine(Snes *s)
{
    return (uint16_t)(snes_bus_peek(&s->bus, FRAME_ROUTINE_OPERAND) | snes_bus_peek(&s->bus, FRAME_ROUTINE_OPERAND + 1) << 8);
}

/* Guard for the frame-wait loop: the call, then the loop's test (its 4 bytes
 * after the call's 3), whatever routine the call names. */
bool attract_wait_loop_present(Snes *s)
{
    return snes_bus_peek(&s->bus, WAIT_LOOP) == JSR && m_code_matches(s, WAIT_LOOP + 3, 4, 0x2B0A967642BA802Bull);
}

/* $7E:3023 (attract/title main loop)
 * Waits for a sequence script to stop, which sets the 16-bit flag at direct
 * page $00 (scene_program_over; scene/script.c), calling the program's frame
 * routine on each pass. Idle entry: one pass of the loop, entered with 16-bit
 * A. Exits to $7E:302A. A frame routine that does anything runs as the
 * attract program's, and the loop goes on after the call at $7E:3026. */
void attract_wait_loop(Snes *s)
{
    uint16_t routine = frame_routine(s);
    m_push16(s, 0x3025);   /* JSR (routine) */
    if (snes_bus_peek(&s->bus, CODE_BANK | routine) != RTS) {
        s->cpu.pc = routine;
        return;
    }
    m_rts(s);
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
