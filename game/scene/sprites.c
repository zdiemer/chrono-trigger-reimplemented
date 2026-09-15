/* Scenes: sprite lists copied into the OAM buffer at $0700. */
#include "scene/scene.h"

#include "port/machine.h"

/* Absolute indexed address: the data bank's address plus the index, carrying
 * into the next bank. */
static uint32_t abs_idx(const Snes *s, uint16_t addr, uint16_t index) { return m_abs(s, addr) + index; }

/* DEC on a direct-page byte, setting N and Z. */
static uint8_t dec_dp8(Snes *s, uint8_t cell)
{
    uint8_t v = (uint8_t)(m_read8(s, m_dp(s, cell)) - 1);
    m_write8(s, m_dp(s, cell), v);
    m_set_nz8(s, v);
    return v;
}

/* LDA $2180 through the data bank: with bank $00-$3F or $80-$BF this is the
 * work RAM port, whose address advances with each read. */
static uint8_t read_port(Snes *s) { return m_lda8(s, m_read8(s, m_abs(s, 0x2180))); }

/* CLC : ADC dp : STA $07xx,Y (8-bit). */
static void store_offset(Snes *s, uint8_t cell, uint16_t oam)
{
    m_set_flag(s, CPU_FLAG_C, false);
    uint8_t v = m_adc8(s, (uint8_t)s->cpu.a, m_read8(s, m_dp(s, cell)));
    m_set_a8(s, v);
    m_write8(s, abs_idx(s, oam, s->cpu.y), v);
}

/* INY x4 : TYA : AND #$0F -- with 8-bit A; true when Y reached a multiple of 16
 * (the next high-table byte). */
static bool next_entry(Snes *s)
{
    for (int i = 0; i < 4; i++)
        m_iny(s);
    m_lda8(s, (uint8_t)s->cpu.y);
    return m_lda8(s, (uint8_t)(s->cpu.a & 0x0F)) == 0;
}

/* The common head of $C3:0D0E and $C3:0D5E: STA $2181 : SEP #$20 : LDY $11 :
 * LDX $13 : STZ $2183 : LDA $2180 : STA $19. */
static void open_list(Snes *s)
{
    Cpu *c = &s->cpu;
    m_write16(s, m_abs(s, 0x2181), c->a);
    m_sep(s, CPU_FLAG_M);
    m_ldy(s, m_read16(s, m_dp(s, 0x11)));
    m_ldx(s, m_read16(s, m_dp(s, 0x13)));
    m_write8(s, m_abs(s, 0x2183), 0x00);
    m_write8(s, m_dp(s, 0x19), read_port(s));
}

/* The common tail: REP #$20 : STX $13 : STY $11 : RTL. */
static void close_list(Snes *s)
{
    m_rep(s, CPU_FLAG_M);
    m_write16(s, m_dp(s, 0x13), s->cpu.x);
    m_write16(s, m_dp(s, 0x11), s->cpu.y);
    m_rtl(s);
}

// $C3:0D0E
/* Copies a sprite list from work RAM ($7E, 16-bit address in A) into the OAM
 * buffer: a count byte n, then n-1 four-byte entries (tile, attributes, X, Y),
 * written at $0700+Y as X plus dp $15, Y plus dp $17, tile and attributes.
 * Each entry shifts the high-table byte at X right two bits (when it isn't 0),
 * and X advances every fourth entry. The OAM index and high-table pointer are
 * read from and saved back to dp $11 and $13. Entered with 16-bit A and X. */
void scene_load_sprite_list(Snes *s)
{
    Cpu *c = &s->cpu;
    open_list(s);
    while (dec_dp8(s, 0x19) != 0) {
        m_write8(s, abs_idx(s, 0x0702, c->y), read_port(s));
        m_write8(s, abs_idx(s, 0x0703, c->y), read_port(s));
        read_port(s);
        store_offset(s, 0x15, 0x0700);
        read_port(s);
        store_offset(s, 0x17, 0x0701);
        uint8_t high = m_lda8(s, m_read8(s, abs_idx(s, 0x0000, c->x)));
        if (high != 0) {
            m_lsr8(s, high);
            high = m_lsr8(s, (uint8_t)(high >> 1));
            m_set_a8(s, high);
            m_write8(s, abs_idx(s, 0x0000, c->x), high);
        }
        if (next_entry(s))
            m_inx(s);
    }
    close_list(s);
}

// $C3:0D5E
/* Like $C3:0D0E, but an entry's X plus dp $15 (16-bit) at or past 256 is
 * written as $01 with the X high bit set, and the high-table byte at X is
 * rotated right twice taking that bit and the large-size bit. */
void scene_load_sprite_list_clipped(Snes *s)
{
    Cpu *c = &s->cpu;
    open_list(s);
    while (dec_dp8(s, 0x19) != 0) {
        m_write8(s, abs_idx(s, 0x0702, c->y), read_port(s));
        m_write8(s, abs_idx(s, 0x0703, c->y), read_port(s));
        read_port(s);
        m_rep(s, CPU_FLAG_M | CPU_FLAG_C);                  /* REP #$21 */
        m_lda16(s, c->a & 0x00FF);                          /* AND #$00FF */
        m_lda16(s, m_adc16(s, c->a, m_read16(s, m_dp(s, 0x15))));
        m_cmp16(s, c->a, 0x0100);
        if (!m_flag(s, CPU_FLAG_N))                         /* BMI */
            m_lda16(s, 0x9801);
        m_write16(s, abs_idx(s, 0x0700, c->y), c->a);
        c->a = m_asl16(s, c->a);
        m_sep(s, CPU_FLAG_M);
        uint32_t high_addr = abs_idx(s, 0x0000, c->x);
        uint8_t high = m_lda8(s, m_read8(s, high_addr));
        /* ROR A : SEC : ROR A */
        bool carry = m_flag(s, CPU_FLAG_C);
        m_set_flag(s, CPU_FLAG_C, high & 1);
        high = (uint8_t)(high >> 1 | (carry ? 0x80 : 0));
        m_set_flag(s, CPU_FLAG_C, true);
        bool out = high & 1;
        high = (uint8_t)(high >> 1 | 0x80);
        m_set_flag(s, CPU_FLAG_C, out);
        m_lda8(s, high);
        m_write8(s, high_addr, high);
        read_port(s);
        store_offset(s, 0x17, 0x0701);
        if (next_entry(s))
            m_inx(s);
    }
    close_list(s);
}

// $C3:0DBC
/* Adds one sprite to the OAM buffer at $0700 + dp $11 from the four bytes at
 * $7E:0000+X: the first word as tile and attributes, then X plus dp $15 and Y
 * plus dp $17. Shifts the high-table byte at the pointer in dp $13 right two
 * bits (when it isn't 0), advances dp $11 by 4 and, every fourth sprite, the
 * pointer's low byte. Restores P. Entered with 16-bit A and X. */
void scene_add_sprite(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->p);
    m_ldy(s, m_read16(s, m_dp(s, 0x11)));
    uint32_t src = 0x7E0000 + c->x;
    m_lda16(s, m_read16(s, src));
    m_write16(s, abs_idx(s, 0x0702, c->y), c->a);
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, m_read8(s, src + 2));
    store_offset(s, 0x15, 0x0700);
    m_lda8(s, m_read8(s, src + 3));
    store_offset(s, 0x17, 0x0701);
    uint32_t high_addr = m_abs(s, m_read16(s, m_dp(s, 0x13)));
    uint8_t high = m_lda8(s, m_read8(s, high_addr));
    if (high != 0) {
        m_lsr8(s, high);
        high = m_lsr8(s, (uint8_t)(high >> 1));
        m_set_a8(s, high);
        m_write8(s, high_addr, high);
    }
    if (next_entry(s)) {
        uint8_t low = (uint8_t)(m_read8(s, m_dp(s, 0x13)) + 1);   /* INC $13 */
        m_write8(s, m_dp(s, 0x13), low);
        m_set_nz8(s, low);
    }
    m_write16(s, m_dp(s, 0x11), c->y);
    c->p = m_pull8(s);
    m_rtl(s);
}
