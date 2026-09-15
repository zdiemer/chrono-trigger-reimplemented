/* Menu: loops around sound driver commands $11 and $14 ($C2:CBBF, $C2:CC0E)
 * that wait for the driver's status on APU ports 1 and 3, through the audio
 * service (platform/audio.h). No movie reaches them. */
#include "menu/sound.h"

#include "platform/audio.h"
#include "port/machine.h"

/* LDA $002141 : BEQ done -- one pass of a loop that calls $C2:821E until port 1
 * reads zero: continues at `done`, or at the call. */
static void wait_port1(Snes *s, uint32_t at, uint16_t call, uint16_t done)
{
    uint8_t v = m_lda8(s, au_status(m_platform(s), PF_AT(at), 1));
    s->cpu.pc = v == 0 ? done : call;
}

// $C2:CBC1
/* Before command $14 in $C2:CBBF: continues at $C2:CBCC once port 1 is zero,
 * else at the call at $C2:CBC7. */
void menu_wait_sound_port1_cbc1(Snes *s) { wait_port1(s, 0xC2CBC1, 0xCBC7, 0xCBCC); }

// $C2:CC28
/* Before command $11 in $C2:CC0E: continues at $C2:CC33 once port 1 is zero,
 * else at the call at $C2:CC2E. */
void menu_wait_sound_port1_cc28(Snes *s) { wait_port1(s, 0xC2CC28, 0xCC2E, 0xCC33); }

// $C2:CBE0
/* LDA $002143 : AND #$0F : BEQ loop -- wait for the low nibble of port 3 to be
 * nonzero after command $14. Idle entry, 8-bit A; exits to the JMP at
 * $C2:CBE8. */
void menu_wait_sound_port3_loop(Snes *s)
{
    uint8_t v = au_status(m_platform(s), PF_AT(0xC2CBE0), 3);
    if (m_lda8(s, v & 0x0F) != 0)
        s->cpu.pc = 0xCBE8;
}
