/* Scenes: the bank C3 scene script command that waits on the sound driver's
 * status, through the audio service (platform/audio.h). Reached only with the
 * original timing, in an ending sequence. */
#include "scene/sound.h"

#include "platform/audio.h"
#include "port/machine.h"

// $C3:0C2A
/* SEP #$20 : LDA $002142 : CMP $002142 : BNE loop -- read port 2 until two reads
 * agree; then AND #$0F : CMP ($20) : BPL: while its low nibble is at least the
 * script's operand byte, back to the script loop at $C3:0C28; otherwise REP #$20
 * : DEC $20 (the script stays on this command) : RTS. Idle entry. */
void scene_script_wait_sound_port2(Snes *s)
{
    Platform *pf = m_platform(s);
    m_sep(s, CPU_FLAG_M);
    uint8_t first = m_lda8(s, au_status(pf, PF_AT(0xC30C2C), 2));
    uint8_t second = au_status(pf, PF_AT(0xC30C30), 2);
    m_cmp8(s, first, second);
    if (first != second)
        return;
    uint8_t nibble = m_lda8(s, first & 0x0F);
    uint16_t ptr = m_read16(s, m_dp(s, 0x20));
    m_cmp8(s, nibble, m_read8(s, m_abs(s, ptr)));
    if (!m_flag(s, CPU_FLAG_N)) {
        s->cpu.pc = 0x0C28;
        return;
    }
    m_rep(s, CPU_FLAG_M);
    uint16_t v = (uint16_t)(ptr - 1);
    m_write16(s, m_dp(s, 0x20), v);
    m_set_nz16(s, v);
    m_rts(s);
}
