/* Menu: queueing VRAM uploads. Records of 8 bytes at $0C00 + $0D11 (VRAM
 * address, source address and bank, size) are sent by the menu's vblank
 * upload routine ($C2:8663), which empties the queue. */
#include "menu/vram.h"

#include "port/machine.h"

static void plp(Snes *s)
{
    s->cpu.p = m_pull8(s);
    if (s->cpu.p & CPU_FLAG_X) {
        s->cpu.x &= 0xFF;
        s->cpu.y &= 0xFF;
    }
}

/* PHP : JSR $83A2 : MVN $7E,<bank> : SEP #$20 : $0D11 += 8 : PLP : RTS */
static void queue_record(Snes *s, uint16_t jsr_site, uint8_t src_bank)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->p);
    /* $C2:83A2: Y = $0C00 + $0D11, A = 7 */
    m_push16(s, (uint16_t)(jsr_site + 2));
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_C);
    m_lda16(s, m_read16(s, m_abs(s, 0x0D11)));
    m_lda16(s, c->a & 0x00FF);
    c->a = m_adc16(s, c->a, 0x0C00);
    m_ldy(s, c->a);
    m_lda16(s, 0x0007);
    m_pull16(s);
    m_mvn(s, 0x7E, src_bank);
    m_sep(s, CPU_FLAG_M);
    uint32_t count = m_abs(s, 0x0D11);
    m_lda8(s, m_read8(s, count));
    m_set_flag(s, CPU_FLAG_C, false);
    m_set_a8(s, m_adc8(s, (uint8_t)c->a, 0x08));
    m_write8(s, count, (uint8_t)c->a);
    plp(s);
    m_rts(s);
}

// $C2:8385
/* Queue the 8-byte upload record at $C2:X. */
void menu_queue_vram_upload(Snes *s)
{
    queue_record(s, 0x8386, 0xC2);
}

// $C2:838E
/* Queue the 8-byte upload record at $7E:X. */
void menu_queue_vram_upload_7e(Snes *s)
{
    queue_record(s, 0x838F, 0x7E);
}
