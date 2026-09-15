/* Menu: the save file screen's slot display, and the sprite reset the menu
 * runs each frame.
 *
 * $0D49-$0D4B are nonzero for save slots holding a game, $54 is the selected
 * slot, and $7E:7600 + slot * $40 holds each slot's summary, which the screen
 * copies to $9890: three $10-byte party member records, a negative first byte
 * marking an empty one. The slot's contents are drawn into the tilemap buffer
 * at $3200 through window command lists run by $C2:ED31. */
#include "menu/save_screen.h"

#include "menu/vram.h"
#include "port/machine.h"

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }
static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }
static uint32_t abs_x(Snes *s, uint16_t offset) { return m_abs(s, offset) + s->cpu.x; }
static uint32_t abs_y(Snes *s, uint16_t offset) { return m_abs(s, offset) + s->cpu.y; }

static void plp(Snes *s)
{
    s->cpu.p = m_pull8(s);
    if (s->cpu.p & CPU_FLAG_X) {
        s->cpu.x &= 0xFF;
        s->cpu.y &= 0xFF;
    }
}

/* After a call-out: true if the widths are as the code that follows assumes. */
static bool widths_are(Snes *s, uint8_t mx)
{
    return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) == mx;
}

/* Stop at `pc` in bank $C2, handing the rest to the original. */
static void stop_at(Snes *s, uint16_t pc)
{
    s->cpu.pb = 0xC2;
    s->cpu.pc = pc;
}

/* STZ abs (16-bit) : LDX #from : LDY #from+2 : LDA #count : MVN $7E,$7E,
 * filling count+2 bytes with zeros. */
static void clear_block(Snes *s, uint16_t from, uint16_t count)
{
    m_write16(s, m_abs(s, from), 0x0000);
    m_ldx(s, from);
    m_ldy(s, (uint16_t)(from + 2));
    m_lda16(s, count);
    m_mvn(s, 0x7E, 0x7E);
}

// $C2:E92D
/* Hide sprites 16-95 of the OAM buffer ($0740: Y = $E0) and set their size
 * bits ($0904-$0917 = $AA), then, unless $0D9B was nonzero (it is cleared),
 * place the cursor sprite at $0770: position from the word at $9380 + $54 * 6,
 * tile and attributes from $58, and attribute bit 6 when $56 is nonzero. */
void menu_reset_sprites(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->p);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_lda16(s, 0xE000);
    m_write16(s, m_abs(s, 0x0740), c->a);
    m_ldx(s, 0x0740);
    m_ldy(s, 0x0742);
    m_lda16(s, 0x013D);
    m_mvn(s, 0x7E, 0x7E);
    m_lda16(s, 0xAAAA);
    m_write16(s, m_abs(s, 0x0904), c->a);
    m_ldx(s, 0x0904);
    m_ldy(s, 0x0906);
    m_lda16(s, 0x0011);
    m_mvn(s, 0x7E, 0x7E);
    /* $C2:E954 */
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, 0xFF);
    m_trb8(s, m_abs(s, 0x0D9B));
    if (m_flag(s, CPU_FLAG_Z)) {
        m_lda8(s, dp8(s, 0x54));
        /* JSR $EA27: X = A * 6 */
        m_push16(s, 0xE961);
        m_push8(s, c->p);
        m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
        m_lda16(s, c->a & 0x00FF);
        c->a = m_asl16(s, c->a);
        m_push16(s, c->a);
        c->a = m_asl16(s, c->a);
        c->a = m_adc16(s, c->a, m_read16(s, (uint16_t)(c->s + 1)));
        m_tax(s);
        m_lda16(s, m_pull16(s));
        plp(s);
        m_pull16(s);

        m_rep(s, CPU_FLAG_M);
        m_write16(s, m_abs(s, 0x0770), m_lda16(s, m_read16(s, abs_x(s, 0x9380))));
        m_write16(s, m_abs(s, 0x0772), m_lda16(s, dp16(s, 0x58)));
        m_sep(s, CPU_FLAG_M);
        if (m_lda8(s, dp8(s, 0x56)) != 0) {
            m_lda8(s, 0x40);
            m_tsb8(s, m_abs(s, 0x0773));
        }
    }
    plp(s);
    m_rts(s);
}

// $C2:CFFB
/* Load the row table at $9720 from $C2:D044 ($21 bytes), copy $9710-$971F
 * to $969A with its pointers at $9694-$9699 ($969A, $9300, $10 entries), and
 * mark row $7F's 3-byte entry at $969C: $20 and pointer $9740. Sets $0D13 bits
 * 6-7. */
void menu_load_row_table(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->p);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_ldx(s, 0xD044);
    m_ldy(s, 0x9720);
    m_lda16(s, 0x0020);
    m_mvn(s, 0x7E, 0xC2);
    m_ldx(s, 0x9710);
    m_ldy(s, 0x969A);
    m_lda16(s, 0x000F);
    m_mvn(s, 0x7E, 0x7E);
    m_write16(s, m_abs(s, 0x9694), m_lda16(s, 0x969A));
    m_write16(s, m_abs(s, 0x9696), m_lda16(s, 0x9300));
    m_write16(s, m_abs(s, 0x9698), m_lda16(s, 0x0010));
    m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_lda8(s, dp8(s, 0x7F));
    m_set_a8(s, m_asl8(s, (uint8_t)c->a));
    m_set_a8(s, m_adc8(s, (uint8_t)c->a, dp8(s, 0x7F)));
    m_tax(s);
    m_write8(s, abs_x(s, 0x969D), m_lda8(s, 0x20));
    m_lda8(s, 0xC0);
    m_tsb8(s, m_abs(s, 0x0D13));
    m_rep(s, CPU_FLAG_M);
    m_write16(s, abs_x(s, 0x969E), m_lda16(s, 0x9740));
    plp(s);
    m_rts(s);
}

// $C2:D466
/* Draw party member record $24's portrait: a 2x2 block of tiles at the
 * tilemap position in $61 (offsets $5E, $60, $9E, $A0), tiles from $C2:D4B4
 * indexed by the member in $00 and attributes $11 + $24 * 4, then its 16
 * colors from $FF:CC84 + member * $20 to $9500 + $24 * $20. */
void menu_draw_member_portrait(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->p);
    m_sep(s, CPU_FLAG_M);
    m_ldy(s, dp16(s, 0x61));
    m_lda8(s, dp8(s, 0x24));
    m_set_a8(s, m_asl8(s, (uint8_t)c->a));
    m_set_a8(s, m_asl8(s, (uint8_t)c->a));
    m_lda8(s, (uint8_t)c->a | 0x11);
    static const uint16_t attr[4] = { 0x005F, 0x0061, 0x009F, 0x00A1 };
    for (int i = 0; i < 4; i++)
        m_write8(s, abs_y(s, attr[i]), (uint8_t)c->a);
    set_dp8(s, 0x01, 0x00);
    m_ldx(s, dp16(s, 0x00));
    m_lda8(s, m_read8(s, 0xC2D4B4 + c->x));
    static const uint16_t tile[4] = { 0x005E, 0x0060, 0x009E, 0x00A0 };
    for (int i = 0; i < 4; i++) {
        if (i > 0)
            m_lda8(s, (uint8_t)(c->a + 1));
        m_write8(s, abs_y(s, tile[i]), (uint8_t)c->a);
    }
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_lda16(s, dp16(s, 0x00));
    for (int i = 0; i < 5; i++)
        c->a = m_asl16(s, c->a);
    c->a = m_adc16(s, c->a, 0xCC84);
    m_tax(s);
    m_lda16(s, dp16(s, 0x24));
    for (int i = 0; i < 5; i++)
        c->a = m_asl16(s, c->a);
    c->a = m_adc16(s, c->a, 0x9500);
    m_ldy(s, c->a);
    m_lda16(s, 0x001F);
    m_mvn(s, 0x7E, 0xFF);
    plp(s);
    m_rts(s);
}

// $C2:D36C
/* Draw the summary of save slot $79 (nothing for slot 3 or an empty slot):
 * copy its $40 bytes to $9890, draw the slot's header through the command
 * list $C2:C15E and $C2:F28D, update the palette set $0D77 (through $C2:F90C)
 * and a row of $26 bits (through $C2:FB97) when $0D84 is clear, then each
 * member's record through the list $C2:C168 and its portrait, three rows of
 * $C0 bytes apart in the tilemap from $3200. Increments $0D15. */
void menu_draw_save_slot(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->p);
    m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_write8(s, m_abs(s, 0x0D5D), 0x00);
    m_ldx(s, dp8(s, 0x79));
    m_cmp8(s, (uint8_t)c->x, 0x03);
    if ((uint8_t)c->x == 0x03 || m_lda8(s, m_read8(s, abs_x(s, 0x0D49))) == 0) {
        plp(s);
        m_rts(s);
        return;
    }
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_lda16(s, dp16(s, 0x78));
    m_lda16(s, c->a & 0xFF00);
    c->a = m_lsr16(s, c->a);
    c->a = m_lsr16(s, c->a);
    c->a = m_adc16(s, c->a, 0x7600);
    m_tax(s);
    m_ldy(s, 0x9890);
    m_lda16(s, 0x003F);
    m_mvn(s, 0x7E, 0x7E);
    if (m_lda16(s, m_read16(s, m_abs(s, 0x0D84))) == 0) {
        m_ldx(s, 0xC15E);
        m_jsr(s, 0xC2D3A1, 0xC2ED31);
        if (!widths_are(s, 0)) {
            stop_at(s, 0xD3A4);
            return;
        }
        m_ldx(s, 0x3310);
        m_ldy(s, 0x98C9);
        m_jsr(s, 0xC2D3AA, 0xC2F28D);
        if (!widths_are(s, 0)) {
            stop_at(s, 0xD3AD);
            return;
        }
        m_lda16(s, m_read16(s, m_abs(s, 0x98C2)));
        c->a = m_lsr16(s, c->a);
        m_tax(s);
        m_lda16(s, m_read16(s, 0xFFD296 + c->x));
        m_lda16(s, c->a & 0x01FF);
        if (m_flag(s, CPU_FLAG_C)) {
            for (int i = 0; i < 4; i++)
                c->a = m_lsr16(s, c->a);
        }
        m_sep(s, CPU_FLAG_M);
        m_lda8(s, (uint8_t)c->a & 0x0F);
        m_cmp8(s, (uint8_t)c->a, m_read8(s, m_abs(s, 0x0D77)));
        if (!m_flag(s, CPU_FLAG_Z)) {
            m_write8(s, m_abs(s, 0x0D77), (uint8_t)c->a);
            m_write8(s, m_abs(s, 0x020C), (uint8_t)c->a);
            m_write16(s, m_abs(s, 0x0DC5), m_ldx(s, 0x5E00));
            m_write16(s, m_abs(s, 0x020D), m_ldx(s, 0xD396));
            m_write8(s, m_abs(s, 0x020F), m_lda8(s, 0xFF));
            m_jsr(s, 0xC2D3DF, 0xC2F90C);
            if (!widths_are(s, CPU_FLAG_M)) {
                stop_at(s, 0xD3E2);
                return;
            }
            set_dp8(s, 0x26, m_lda8(s, m_read8(s, m_abs(s, 0x0234))));
        }
        m_ldx(s, 0x5248);
        m_tdc(s);
        if (m_lda8(s, dp8(s, 0x26)) != 0) {
            for (int i = 0; i < 3; i++) {
                m_set_a8(s, m_lsr8(s, (uint8_t)c->a));
                m_set_a8(s, m_adc8(s, (uint8_t)c->a, 0x00));
            }
            m_ldy(s, c->a);
            m_lda8(s, 0x01);
            m_jsr(s, 0xC2D3FB, 0xC2FB97);
            if (!widths_are(s, CPU_FLAG_M)) {
                stop_at(s, 0xD3FE);
                return;
            }
        }
        m_ldx(s, 0xD45F);
        m_jsr(s, 0xC2D401, 0xC28385);
        if (!widths_are(s, CPU_FLAG_M)) {
            stop_at(s, 0xD404);
            return;
        }
    }
    /* $C2:D404 */
    set_dp16(s, 0x61, m_ldx(s, 0x3200));
    set_dp16(s, 0x02, m_ldx(s, 0x9890));
    if (m_m8(s)) {
        set_dp8(s, 0x24, 0x00);
        set_dp8(s, 0x25, 0x00);
    } else {
        set_dp16(s, 0x24, 0x0000);
        set_dp16(s, 0x25, 0x0000);
    }
    for (;;) {
        m_sep(s, CPU_FLAG_M);
        m_ldx(s, dp16(s, 0x02));
        set_dp8(s, 0x00, m_lda8(s, m_read8(s, abs_x(s, 0x0000))));
        if (!(c->a & 0x80)) {
            m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
            m_ldy(s, 0x9A90);
            m_lda16(s, 0x0008);
            m_mvn(s, 0x7E, 0x7E);
            m_ldy(s, 0x9AE0);
            m_lda16(s, 0x0005);
            m_mvn(s, 0x7E, 0x7E);
            m_write16(s, m_abs(s, 0x9AA2), m_lda16(s, m_read16(s, abs_x(s, 0x0000))));
            m_ldx(s, 0xC168);
            m_jsr(s, 0xC2D43A, 0xC2ED31);
            if (!widths_are(s, 0)) {
                stop_at(s, 0xD43D);
                return;
            }
            m_push16(s, 0xD43F);
            menu_draw_member_portrait(s);
            m_lda16(s, dp16(s, 0x61));
            m_set_flag(s, CPU_FLAG_C, false);
            c->a = m_adc16(s, c->a, 0x00C0);
            set_dp16(s, 0x61, c->a);
        }
        /* $C2:D448 */
        m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
        m_lda16(s, dp16(s, 0x02));
        c->a = m_adc16(s, c->a, 0x0010);
        set_dp16(s, 0x02, c->a);
        uint16_t row = (uint16_t)(dp16(s, 0x24) + 1);
        set_dp16(s, 0x24, row);
        m_lda16(s, row);
        m_cmp16(s, c->a, 0x0003);
        if (row >= 0x0003)
            break;
    }
    uint16_t count = (uint16_t)(m_read16(s, m_abs(s, 0x0D15)) + 1);
    m_write16(s, m_abs(s, 0x0D15), count);
    m_set_nz16(s, count);
    plp(s);
    m_rts(s);
}

// $C2:E743
/* Show save slot $54: move off empty slots (back when $5A bit 3 is set,
 * forward otherwise, wrapping within 0-3), flip $9392 bits $70 with a sound
 * on slot 3 when SRAM byte $30:7FE2 is set and $5A has bits 0-1, remember the
 * slot in $79 and the next in $7F, load the row table ($C2:CFFB), clear the
 * tilemap buffer $3200-$343F and the rows at $5248 and $5288, draw the slot
 * ($C2:D36C) and queue three uploads. */
void menu_show_save_slot(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->p);
    m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
    /* With no slot in use the original searches forever: leave that to it. */
    bool any = false;
    for (uint16_t i = 0; i < 4; i++)
        any |= snes_bus_peek(&s->bus, m_abs(s, (uint16_t)(0x0D49 + i))) != 0;
    if (!any && snes_bus_peek(&s->bus, m_abs(s, 0x0D49) + snes_bus_peek(&s->bus, m_dp(s, 0x54))) == 0) {
        stop_at(s, 0xE746);
        return;
    }
    for (;;) {
        m_ldx(s, dp8(s, 0x54));
        if (m_lda8(s, m_read8(s, abs_x(s, 0x0D49))) != 0)
            break;
        m_lda8(s, dp8(s, 0x5A));
        if (m_lda8(s, (uint8_t)c->a & 0x08) == 0) {
            m_inx(s);
            m_inx(s);
        }
        m_set_index(s, &c->x, (uint16_t)(c->x - 1));
        m_txa(s);
        set_dp8(s, 0x54, m_lda8(s, (uint8_t)c->a & 0x03));
    }
    if (m_lda8(s, m_read8(s, 0x307FE2)) != 0) {
        m_lda8(s, dp8(s, 0x54));
        m_cmp8(s, (uint8_t)c->a, 0x03);
        if ((uint8_t)c->a == 0x03) {
            m_lda8(s, dp8(s, 0x5A));
            if (m_lda8(s, (uint8_t)c->a & 0x03) != 0) {
                m_lda8(s, m_read8(s, m_abs(s, 0x9392)));
                m_write8(s, m_abs(s, 0x9392), m_lda8(s, (uint8_t)c->a ^ 0x70));
                m_jsr(s, 0xC2E777, 0xC2EAC2);
                if (!widths_are(s, CPU_FLAG_M | CPU_FLAG_X)) {
                    stop_at(s, 0xE77A);
                    return;
                }
            }
        }
    }
    set_dp8(s, 0x79, m_lda8(s, dp8(s, 0x54)));
    m_lda8(s, (uint8_t)(c->a + 1));
    set_dp8(s, 0x7F, m_lda8(s, (uint8_t)c->a & 0x03));
    m_jsr(s, 0xC2E783, 0xC2CFFB);
    if (!widths_are(s, CPU_FLAG_M | CPU_FLAG_X)) {
        stop_at(s, 0xE786);
        return;
    }
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    clear_block(s, 0x3200, 0x023D);
    m_ldx(s, 0x3200);
    m_ldy(s, 0x5248);
    m_lda16(s, 0x0011);
    m_mvn(s, 0x7E, 0x7E);
    m_ldy(s, 0x5288);
    m_lda16(s, 0x0011);
    m_mvn(s, 0x7E, 0x7E);
    m_jsr(s, 0xC2E7AC, 0xC2D36C);
    static const uint16_t sites[3] = { 0xE7AF, 0xE7B5, 0xE7BB };
    static const uint16_t records[3] = { 0xFBDC, 0xFC0D, 0xFC29 };
    for (int i = 0; i < 3; i++) {
        if (m_x8(s) || s->cpu.e) {
            stop_at(s, sites[i]);
            return;
        }
        m_ldx(s, records[i]);
        m_jsr(s, 0xC20000 | (uint16_t)(sites[i] + 3), 0xC28385);
    }
    plp(s);
    m_rts(s);
}
