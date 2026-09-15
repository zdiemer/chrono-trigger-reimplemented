/* Field mode: building object sprite graphics when an animation frame changes.
 *
 * The frame builder (reached from the queue $C0:CA76 works through each frame)
 * assembles an object's sprite tiles, 32 bytes each, into $7F:0000 + $D0 from
 * the graphics set whose bank is $CF and base $CD ($CD-$CF also serve as a
 * long pointer), through the WRAM data port $2180-$2183. */
#include "field/graphics.h"

#include "port/machine.h"

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }
static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }
static uint8_t obj8(Snes *s, uint16_t table) { return m_read8(s, m_abs(s, table) + s->cpu.x); }
static void set_obj8(Snes *s, uint16_t table, uint8_t v) { m_write8(s, m_abs(s, table) + s->cpu.x, v); }
static uint16_t obj16(Snes *s, uint16_t table) { return m_read16(s, m_abs(s, table) + s->cpu.x); }

/* The 24-bit address a long pointer on the direct page holds. */
static uint32_t long_ptr(Snes *s, uint8_t cell)
{
    return m_read16(s, m_dp(s, cell)) | (uint32_t)m_read8(s, m_dp(s, (uint8_t)(cell + 2))) << 16;
}

/* AND #$07FF : ASL A x5 : STY $C5 -- the tile number (16-bit A) to its
 * offset, saving Y. */
static void tile_offset(Snes *s)
{
    Cpu *c = &s->cpu;
    m_lda16(s, (uint16_t)(c->a & 0x07FF));
    for (int i = 0; i < 5; i++)
        c->a = m_asl16(s, c->a);
    set_dp16(s, 0xC5, c->y);
}

// $C0:E687
/* Copy sprite tile A (16-bit) of the graphics set to $7F:0000 + $D0: 32 bytes
 * from $CD + A * 32 in bank $D2, $D3, $D4 or $7F by $CF (any other bank reads
 * $D5), a word at a time with the data bank at $7F. The WRAM port address
 * $2181 is then set past the copy, and Y restored from $C5. */
void field_copy_sprite_tile(Snes *s)
{
    Cpu *c = &s->cpu;
    tile_offset(s);
    m_set_flag(s, CPU_FLAG_C, false);
    c->a = m_adc16(s, c->a, dp16(s, 0xCD));
    m_tax(s);
    m_ldy(s, dp16(s, 0xD0));
    m_sep(s, CPU_FLAG_M);
    uint8_t bank = m_lda8(s, dp8(s, 0xCF));
    m_cmp8(s, bank, 0x7F);
    uint8_t source = 0x7F;
    if (bank != 0x7F) {
        m_set_flag(s, CPU_FLAG_C, true);
        uint8_t v = m_sbc8(s, bank, 0xD2);
        m_set_a8(s, v);
        source = 0xD2;
        if (v != 0) {
            source = 0xD3;
            if (m_lda8(s, (uint8_t)(v - 1)) != 0) {
                source = 0xD4;
                if (m_lda8(s, (uint8_t)(c->a - 1)) != 0)
                    source = 0xD5;
            }
        }
    }
    m_push8(s, c->db);
    m_push8(s, m_lda8(s, 0x7F));
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_rep(s, CPU_FLAG_M);
    for (uint16_t k = 0; k < 0x20; k += 2) {
        /* bank $7F reads abs,X in the data bank, the others long,X */
        uint32_t from = source == 0x7F ? m_abs(s, k) + c->x : ((uint32_t)source << 16 | k) + c->x;
        m_lda16(s, m_read16(s, from));
        m_write16(s, m_abs(s, k) + c->y, c->a);
    }
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_lda16(s, c->y);
    m_set_flag(s, CPU_FLAG_C, false);
    c->a = m_adc16(s, c->a, 0x0020);
    m_write16(s, m_abs(s, 0x2181), c->a);
    m_ldy(s, dp16(s, 0xC5));
    m_rts(s);
}

// $C0:E534
/* Write sprite tile A (16-bit) mirrored horizontally to the WRAM port: each
 * of its 32 bytes, read through the long pointer at $CD + A * 32, goes
 * through the bit reversal table at $FD00 in the data bank. Y is restored
 * from $C5. */
void field_copy_sprite_tile_flipped(Snes *s)
{
    Cpu *c = &s->cpu;
    tile_offset(s);
    m_set_index(s, &c->y, c->a);
    m_sep(s, CPU_FLAG_M);
    m_tdc(s);
    m_xba(s);
    for (int i = 0; i < 32; i++) {
        if (i != 0)
            m_iny(s);
        m_lda8(s, m_read8(s, long_ptr(s, 0xCD) + c->y));
        m_tax(s);
        m_lda8(s, m_read8(s, m_abs(s, 0xFD00) + c->x));
        m_write8(s, m_abs(s, 0x2180), (uint8_t)c->a);
    }
    m_rep(s, CPU_FLAG_M);
    m_ldy(s, dp16(s, 0xC5));
    m_rts(s);
}

/* ASL A : ASL A : CLC : ADC $D9 : ADC $D6 : STA $D6 -- the entry for frame
 * $D9 of animation A in the table at $D6 (4 frames per animation). */
static void frame_entry(Snes *s)
{
    Cpu *c = &s->cpu;
    c->a = m_asl16(s, c->a);
    c->a = m_asl16(s, c->a);
    m_set_flag(s, CPU_FLAG_C, false);
    c->a = m_adc16(s, c->a, dp16(s, 0xD9));
    c->a = m_adc16(s, c->a, dp16(s, 0xD6));
    set_dp16(s, 0xD6, c->a);
    m_sep(s, CPU_FLAG_M);
}

// $C0:CB3A
/* Object X's current animation frame number into A, read through the long
 * pointer $D6/$D8 (the caller sets the bank). The table is $1500,X plus
 * $1480,X times 0-3 by the facing $1600,X (0, 1 or negative, 2, 3 up to
 * $81 by the signed compare), and the entry animation * 4 + frame ($1681,X),
 * the animation from $1781,X during a scripted animation ($1780,X = 2) or
 * $1680,X. An entry of $FF ends the animation: a scripted one counts down
 * $7F:0B01,X (not below 1) and returns carry clear; otherwise the frame goes
 * back to the animation's first entry ($1681,X = $FF). Carry set with a frame. */
void field_read_animation_frame(Snes *s)
{
    Cpu *c = &s->cpu;
    uint8_t facing = m_lda8(s, obj8(s, 0x1600));
    m_rep(s, CPU_FLAG_M);
    if (facing == 0) {
        m_lda16(s, obj16(s, 0x1500));
    } else {
        m_cmp8(s, facing, 0x02);
        bool negative = m_flag(s, CPU_FLAG_N);
        m_lda16(s, obj16(s, 0x1480));
        if (facing == 0x02) {
            c->a = m_asl16(s, c->a);
        } else if (!negative) {
            set_dp16(s, 0xD9, c->a);
            m_set_flag(s, CPU_FLAG_C, false);
            c->a = m_adc16(s, c->a, dp16(s, 0xD9));
            c->a = m_adc16(s, c->a, dp16(s, 0xD9));
            c->a = m_adc16(s, c->a, obj16(s, 0x1500));
            set_dp16(s, 0xD6, c->a);
            goto entry;
        }
        m_set_flag(s, CPU_FLAG_C, false);
        c->a = m_adc16(s, c->a, obj16(s, 0x1500));
    }
    set_dp16(s, 0xD6, c->a);
entry:
    /* $C0:CB7A */
    set_dp16(s, 0xD9, m_lda16(s, (uint16_t)(obj16(s, 0x1681) & 0x00FF)));
    uint16_t mode = m_lda16(s, (uint16_t)(obj16(s, 0x1780) & 0x00FF));
    m_cmp16(s, mode, 0x0002);
    if (mode == 0x0002) {
        m_lda16(s, (uint16_t)(obj16(s, 0x1781) & 0x00FF));
        frame_entry(s);
        uint8_t frame = m_lda8(s, m_read8(s, long_ptr(s, 0xD6)));
        m_cmp8(s, frame, 0xFF);
        if (frame != 0xFF) {
            m_set_flag(s, CPU_FLAG_C, true);
            m_rts(s);
            return;
        }
        uint8_t loops = m_lda8(s, m_read8(s, 0x7F0B01u + c->x));
        loops = m_lda8(s, (uint8_t)(loops - 1));
        if (loops != 0)
            m_write8(s, 0x7F0B01u + c->x, loops);
        m_set_flag(s, CPU_FLAG_C, false);
        m_rts(s);
        return;
    }
    /* $C0:CBB1 */
    m_lda16(s, (uint16_t)(obj16(s, 0x1680) & 0x00FF));
    frame_entry(s);
    uint8_t frame = m_lda8(s, m_read8(s, long_ptr(s, 0xD6)));
    m_cmp8(s, frame, 0xFF);
    if (frame == 0xFF) {
        m_rep(s, CPU_FLAG_M);
        m_lda16(s, dp16(s, 0xD6));
        m_set_flag(s, CPU_FLAG_C, true);
        c->a = m_sbc16(s, c->a, dp16(s, 0xD9));
        set_dp16(s, 0xD6, c->a);
        m_sep(s, CPU_FLAG_M);
        set_obj8(s, 0x1681, m_lda8(s, 0xFF));
        m_lda8(s, m_read8(s, long_ptr(s, 0xD6)));
    }
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}
