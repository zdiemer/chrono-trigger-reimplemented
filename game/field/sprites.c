/* Field mode: building the OAM buffer from the objects' sprites each frame.
 *
 * The OAM buffer is at $0700 (4 bytes per sprite, high table bytes from
 * $0900). Objects are drawn into three ranges by their priority bits
 * ($0F80,X & $0C): entries from $0770 (high table from $0907), from $08A0
 * ($091A) and from $0710 ($0901). The next free entry of each is kept in
 * $DB/$DD/$DF on the direct page (the field's direct page is $0100) and its
 * high table pointer in $81/$85/$89; the ends reached last frame in
 * $7B/$7D/$7F. Entries are written through the WRAM data port $2180, with
 * the direct page moved to $2100.
 *
 * An object's sprites are 8-byte records at $7F:4BC0 + $1700,X (screen X,
 * screen Y, then offsets and tile and attribute bytes); $1201,X & 3 selects 4,
 * 8, 12 or 24 of them, with 1, 2, 3 or 6 high table bytes ($7F:4F00,X ...). */
#include "field/sprites.h"

#include "port/machine.h"

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }
static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }
static uint16_t abs16(Snes *s, uint16_t addr) { return m_read16(s, m_abs(s, addr)); }
static void set_abs16(Snes *s, uint16_t addr, uint16_t v) { m_write16(s, m_abs(s, addr), v); }
static uint8_t absx8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr) + s->cpu.x); }
static void set_absx8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr) + s->cpu.x, v); }

/* JSR out, expecting 8-bit A and 16-bit X back; false if the callee left other
 * widths (the port then stops at the return address). */
static bool call(Snes *s, uint32_t site, uint32_t target)
{
    m_jsr(s, site, target);
    return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) == CPU_FLAG_M;
}

// $C0:B271
/* Build the frame's sprite entries: reset the three ranges' pointers, draw
 * the objects of each row list from the last row ($0E00 + $7E) to the first
 * through $C0:B309, then hide (Y = $E0) the entries each range used last frame
 * beyond what it uses now, and remember the new ends. */
void field_build_sprite_table(Snes *s)
{
    Cpu *c = &s->cpu;
    static const struct { uint8_t cell; uint16_t v; } init[8] = {
        { 0x89, 0x0901 }, { 0x8B, 0x0000 }, { 0x81, 0x0907 }, { 0x83, 0x0000 },
        { 0x85, 0x091A }, { 0x87, 0x0000 }, { 0xDF, 0x0710 }, { 0xDB, 0x0770 },
    };
    for (int i = 0; i < 8; i++)
        set_dp16(s, init[i].cell, m_ldx(s, init[i].v));
    m_lda8(s, 0x00);
    m_write8(s, m_abs(s, 0x2183), 0x00);
    set_dp8(s, 0x6E, 0x00);
    set_dp16(s, 0xDD, m_ldx(s, 0x08A0));
    m_ldy(s, 0x007E);
    do {
        uint8_t object = m_lda8(s, m_read8(s, m_abs(s, 0x0E00) + c->y));
        if (!(object & 0x80)) {
            set_dp8(s, 0x6D, object);
            if (!call(s, 0xC0B2AF, 0xC0B309))
                return;
            for (;;) {
                m_ldx(s, dp16(s, 0x6D));
                uint8_t next = m_lda8(s, absx8(s, 0x0E81));
                if (next & 0x80)
                    break;
                set_dp8(s, 0x6D, next);
                if (!call(s, 0xC0B2BB, 0xC0B309))
                    return;
            }
        }
        m_ldy(s, (uint16_t)(c->y - 1));
        m_ldy(s, (uint16_t)(c->y - 1));
    } while (!m_flag(s, CPU_FLAG_N));

    static const struct { uint8_t next, end; uint16_t limit; } ranges[3] = {
        { 0xDB, 0x7B, 0x08A0 }, { 0xDD, 0x7D, 0x0900 }, { 0xDF, 0x7F, 0x0770 },
    };
    for (int i = 0; i < 3; i++) {
        m_ldx(s, dp16(s, ranges[i].next));
        if (i == 0)
            m_lda8(s, 0xE0);
        for (;;) {
            m_cmp16(s, c->x, dp16(s, ranges[i].end));
            if (m_flag(s, CPU_FLAG_C))
                break;
            m_write8(s, m_abs(s, 0x0001) + c->x, (uint8_t)c->a);
            for (int k = 0; k < 4; k++)
                m_inx(s);
            m_cmp16(s, c->x, ranges[i].limit);
            if (m_flag(s, CPU_FLAG_C))
                break;
        }
        set_dp16(s, ranges[i].end, m_ldx(s, dp16(s, ranges[i].next)));
    }
    m_rts(s);
}

// $C0:B309
/* Draw object $6D: bring its sprite records up to date through $C0:B701 and,
 * unless that returns with carry set (nothing to draw), append its high table
 * bytes at the range's pointer and its sprites' X, Y, tile and attribute bytes
 * ($4BC0, $4BC1, $4BC6, $4BC7 of each record) through the WRAM port at the
 * range's next entry, which advances by the sprite count times 4. The four
 * record counts are separate copies of the code in the original, which pushes
 * the extra high table bytes and pulls them back as it writes them. */
void field_draw_object_sprites(Snes *s)
{
    Cpu *c = &s->cpu;
    if (!call(s, 0xC0B309, 0xC0B701))
        return;
    if (m_flag(s, CPU_FLAG_C)) {
        m_rts(s);
        return;
    }
    m_ldx(s, dp16(s, 0x6D));
    uint8_t mode = m_lda8(s, (uint8_t)(absx8(s, 0x1201) & 0x03));
    if (mode != 0) {
        m_cmp8(s, mode, 0x01);
        if (mode != 0x01)
            m_cmp8(s, mode, 0x02);
    }
    m_ldx(s, dp16(s, 0x6D));
    m_push16(s, c->d);
    m_rep(s, CPU_FLAG_M);
    c->d = m_lda16(s, 0x2100);
    m_sep(s, CPU_FLAG_M);
    uint8_t priority = m_lda8(s, (uint8_t)(absx8(s, 0x0F80) & 0x0C));
    int range = 0;
    if (priority != 0) {
        m_bit_imm8(s, 0x04);
        range = (priority & 0x04) ? 1 : 2;
    }
    const uint16_t pointer_cell = (uint16_t)(0x0181 + 4 * range), entry_cell = (uint16_t)(0x01DB + 2 * range);

    static const uint32_t high_bytes[6] = { 0x7F4F00, 0x7F4F01, 0x7F4B40, 0x7F4B41, 0x7F4F80, 0x7F4F81 };
    static const struct { int high, sprites; uint16_t size; } layouts[4] = {
        { 1, 4, 0x0010 }, { 2, 8, 0x0020 }, { 3, 12, 0x0030 }, { 6, 24, 0x0060 },
    };
    const int high = layouts[mode].high;
    for (int k = high - 1; k >= 1; k--)
        m_push8(s, m_lda8(s, m_read8(s, high_bytes[k] + c->x)));
    uint8_t v = m_lda8(s, m_read8(s, high_bytes[0] + c->x));
    m_ldx(s, abs16(s, pointer_cell));
    m_write8(s, m_abs(s, 0x0000) + c->x, v);
    m_inx(s);
    for (int k = 1; k < high; k++) {
        v = m_lda8(s, m_pull8(s));
        m_write8(s, m_abs(s, 0x0000) + c->x, v);
        m_inx(s);
    }
    set_abs16(s, pointer_cell, c->x);
    set_dp16(s, 0x81, m_ldx(s, abs16(s, entry_cell)));
    m_ldx(s, abs16(s, 0x016D));
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, abs16(s, entry_cell));
    m_set_flag(s, CPU_FLAG_C, false);
    c->a = m_adc16(s, c->a, layouts[mode].size);
    set_abs16(s, entry_cell, c->a);

    /* $C0:B363 */
    m_lda16(s, m_read16(s, m_abs(s, 0x1700) + c->x));
    m_tax(s);
    m_sep(s, CPU_FLAG_M);
    m_write8(s, m_abs(s, 0x01C9), m_lda8(s, (uint8_t)layouts[mode].sprites));
    static const uint32_t record_bytes[4] = { 0x7F4BC0, 0x7F4BC1, 0x7F4BC6, 0x7F4BC7 };
    uint8_t left;
    do {
        for (int k = 0; k < 4; k++)
            set_dp8(s, 0x80, m_lda8(s, m_read8(s, record_bytes[k] + c->x)));
        m_rep(s, CPU_FLAG_M);
        m_txa(s);
        m_set_flag(s, CPU_FLAG_C, false);
        c->a = m_adc16(s, c->a, 0x0008);
        m_tax(s);
        m_sep(s, CPU_FLAG_M);
        uint32_t counter = m_abs(s, 0x01C9);
        left = (uint8_t)(m_read8(s, counter) - 1);
        m_write8(s, counter, left);
        m_set_nz8(s, left);
    } while (left != 0);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    m_rts(s);
}

/* JSR out twice to reload an object's sprite records, then mark them current
 * ($1B00,X = $80). */
static void reload_records(Snes *s, uint32_t site, uint32_t reload, uint32_t release)
{
    if (!call(s, site, reload) || !call(s, site + 3, release))
        return;
    m_ldx(s, dp16(s, 0x6D));
    set_absx8(s, 0x1B00, m_lda8(s, 0x80));
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

/* The 8- and 12-record layouts' routines in $C0:B701. */
typedef struct {
    uint8_t minimum;
    uint32_t reload_site, reload, release, position_site, position;
} RecordKind;

// $C0:B701
/* Bring object $6D's sprite records up to date by $1B00,X: zero means
 * nothing to draw (carry set); $80 with no count bits means only the screen
 * position changed, handled for 4 records by $C0:B788 (8: $C0:BA65, 12:
 * $C0:BFF2); otherwise the records are rebuilt ($C0:B8CA, $C0:BCDC, $C0:C2BF,
 * each followed by $C0:E9E2, $C0:E9FF or $C0:EA1F) and $1B00,X set to $80.
 * For 8 and 12 records, counts below 2 or 3 aren't enough to rebuild: with
 * bit 7 clear they mean nothing to draw, with it set a position update. The
 * 24-record layout continues at $C0:C73A, left to the original. */
void field_update_object_sprites(Snes *s)
{
    m_ldx(s, dp16(s, 0x6D));
    uint8_t mode = m_lda8(s, (uint8_t)(absx8(s, 0x1201) & 0x03));
    if (mode == 0) {
        uint8_t flags = m_lda8(s, absx8(s, 0x1B00));
        if (flags == 0) {
            m_set_flag(s, CPU_FLAG_C, true);
            m_rts(s);
            return;
        }
        if ((flags & 0x80) && m_lda8(s, flags & 0x7F) == 0) {
            if (!call(s, 0xC0B732, 0xC0B788))
                return;
            m_set_flag(s, CPU_FLAG_C, false);
            m_rts(s);
            return;
        }
        reload_records(s, 0xC0B71F, 0xC0B8CA, 0xC0E9E2);
        return;
    }
    m_cmp8(s, mode, 0x01);
    static const RecordKind kinds[2] = {
        { 0x02, 0xC0B744, 0xC0BCDC, 0xC0E9FF, 0xC0B759, 0xC0BA65 },
        { 0x03, 0xC0B76B, 0xC0C2BF, 0xC0EA1F, 0xC0B780, 0xC0BFF2 },
    };
    const RecordKind *kind = &kinds[0];
    if (mode != 0x01) {
        m_cmp8(s, mode, 0x02);
        if (mode != 0x02) {
            m_stop_at(s, 0xC0C73A, 0);
            return;
        }
        kind = &kinds[1];
    }
    uint8_t flags = m_lda8(s, absx8(s, 0x1B00));
    if (flags == 0) {
        m_set_flag(s, CPU_FLAG_C, true);
        m_rts(s);
        return;
    }
    if (flags & 0x80) {
        uint8_t count = m_lda8(s, flags & 0x7F);
        m_cmp8(s, count, kind->minimum);
        if (count < kind->minimum) {
            if (!call(s, kind->position_site, kind->position))
                return;
            m_set_flag(s, CPU_FLAG_C, false);
            m_rts(s);
            return;
        }
    } else {
        m_cmp8(s, flags, kind->minimum);
        if (flags < kind->minimum) {
            m_set_flag(s, CPU_FLAG_C, true);
            m_rts(s);
            return;
        }
    }
    reload_records(s, kind->reload_site, kind->reload, kind->release);
}

/* LDA #$E0 */
static void offscreen(Snes *s) { m_lda8(s, 0xE0); }

/* $C0:B788 and $C0:B8CA: position object X's four sprite records;
 * `reload` first copies each record's offsets, tile and attribute bytes from
 * the object's frame data at $7F:4800 + $1700,X. */
static void position_records(Snes *s, bool reload)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->db);
    m_push8(s, m_lda8(s, 0x7F));
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_rep(s, CPU_FLAG_M);
    set_dp16(s, 0xC5, m_lda16(s, (uint16_t)(m_read16(s, 0x000A80u + c->x) & 0x01FF)));
    set_dp16(s, 0xC3, m_lda16(s, m_read16(s, 0x000A00u + c->x)));
    set_dp16(s, 0xE5, 0x0000);
    set_dp16(s, 0xD9, m_lda16(s, m_read16(s, 0x001700u + c->x)));
    m_set_flag(s, CPU_FLAG_C, false);
    c->a = m_adc16(s, c->a, 0x0018);
    for (;;) {
        m_tax(s);
        if (reload) {
            m_lda16(s, m_read16(s, m_abs(s, 0x4802) + c->x));
            m_write16(s, m_abs(s, 0x4BC2) + c->x, c->a);
        } else {
            m_lda16(s, m_read16(s, m_abs(s, 0x4BC2) + c->x));
        }
        m_set_flag(s, CPU_FLAG_C, false);
        c->a = m_adc16(s, c->a, dp16(s, 0xC3));
        m_sep(s, CPU_FLAG_M);
        set_absx8(s, 0x4BC0, (uint8_t)c->a);
        m_xba(s);
        set_dp8(s, 0xE6, m_lda8(s, (uint8_t)(c->a & 0x01)));
        uint8_t bits = m_lda8(s, dp8(s, 0xE5));
        bits = m_asl8(s, bits);
        bits = m_asl8(s, bits);
        m_lda8(s, (uint8_t)(bits | dp8(s, 0xE6)));
        uint16_t first = dp16(s, 0xD9);
        m_cmp16(s, c->x, first);
        if (c->x == first)
            break;
        set_dp8(s, 0xE5, (uint8_t)c->a);
        m_rep(s, CPU_FLAG_M);
        m_txa(s);
        m_set_flag(s, CPU_FLAG_C, true);
        c->a = m_sbc16(s, c->a, 0x0008);
    }
    uint8_t high = m_lda8(s, (uint8_t)(c->a | 0xAA));
    m_ldx(s, dp16(s, 0x6D));
    set_absx8(s, 0x4F00, high);
    m_ldx(s, dp16(s, 0xD9));

    static const uint16_t offsets[5] = { 0x4BC4, 0x4BCC, 0x4BD4, 0x4BDC, 0x4BE4 };
    int kind = m_lda8(s, dp8(s, 0xC6)) != 0 ? 0 : (m_lda8(s, dp8(s, 0xC5)) & 0x80) ? 1 : 2;
    int count = kind == 0 ? 5 : 4;
    for (int i = 0; i < count; i++) {
        if (reload && i < 4) {
            m_lda8(s, absx8(s, (uint16_t)(offsets[i] - 0x03C0)));
            set_absx8(s, offsets[i], (uint8_t)c->a);
        } else {
            m_lda8(s, absx8(s, offsets[i]));
        }
        m_set_flag(s, CPU_FLAG_C, false);
        uint8_t y = m_adc8(s, (uint8_t)c->a, dp8(s, 0xC5));
        m_set_a8(s, y);
        if (kind == 0) {
            if (!m_flag(s, CPU_FLAG_C)) {
                offscreen(s);
            } else {
                m_cmp8(s, y, 0xE0);
                if (!m_flag(s, CPU_FLAG_C))
                    offscreen(s);
            }
        } else if (kind == 1) {
            if (m_flag(s, CPU_FLAG_C)) {
                m_cmp8(s, y, 0xE0);
                if (m_flag(s, CPU_FLAG_C))
                    offscreen(s);
            }
        } else if (m_flag(s, CPU_FLAG_N)) {
            m_cmp8(s, y, 0xE0);
            if (!m_flag(s, CPU_FLAG_C))
                offscreen(s);
        }
        set_absx8(s, (uint16_t)(offsets[i] - 3), (uint8_t)c->a);
    }
    if (reload) {
        m_rep(s, CPU_FLAG_M);
        for (uint16_t k = 0; k < 4; k++) {
            m_lda16(s, m_read16(s, m_abs(s, (uint16_t)(0x4806 + 8 * k)) + c->x));
            m_write16(s, m_abs(s, (uint16_t)(0x4BC6 + 8 * k)) + c->x, c->a);
        }
    }
    m_sep(s, CPU_FLAG_M);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_rts(s);
}

// $C0:B788
/* Move object X's four sprite records to its screen position ($0A00,X and
 * $0A80,X, 9 bits, in $C3/$C5): each record's X byte is its offset ($4BC2)
 * plus the screen X, the carries collected two bits per record (with the size
 * bits $AA) into the object's high table byte $7F:4F00,X; each Y byte is its
 * offset ($4BC4) plus the screen Y, or $E0 (hidden) where the sum leaves the
 * screen: with the screen Y past 255, sums that wrap to $E0 or more are kept
 * (the original writes a fifth record's Y in this case); with a negative low
 * byte, sums that wrap to below $E0; otherwise sums below $80 or from $E0.
 * Runs with the data bank at $7F. */
void field_position_sprites(Snes *s) { position_records(s, false); }

// $C0:B8CA
/* The same after copying the four records' offsets ($4802, $4804) and tile
 * and attribute words ($4806) from the frame data at $7F:4800 + $1700,X. */
void field_load_and_position_sprites(Snes *s) { position_records(s, true); }
