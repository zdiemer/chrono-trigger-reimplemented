/* Battle: setup run once when a battle starts, from $C1:0023 ($CC:E2E4) and
 * $C1:002D ($CC:ED87): state tables reset and filled from ROM and from the
 * party and enemy data at $2980-$2C9F, name tiles, the battlefield grid,
 * object graphics pointers and upload descriptors, OAM and palettes.
 *
 * Both routines run with 8-bit A and 16-bit X and Y. Most of the work is
 * table fills whose flags end up overwritten, but the routines are compared
 * at their call-outs and returns, so the helpers below set flags the way the
 * instructions they stand for do. */
#include "battle/setup.h"

#include "port/machine.h"
#include "ram.h"

/* Instruction-sized helpers: absolute addresses in the data bank, direct-page
 * cells, 8-bit A and 16-bit X and Y. */
static uint8_t abs8(Snes *s, uint32_t addr) { return m_read8(s, m_abs(s, RAM_ABS(addr))); }
static uint8_t absx8(Snes *s, uint32_t addr) { return m_read8(s, m_abs(s, RAM_ABS(addr)) + s->cpu.x); }
static uint8_t absy8(Snes *s, uint32_t addr) { return m_read8(s, m_abs(s, RAM_ABS(addr)) + s->cpu.y); }
static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }
static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }

static uint8_t lda(Snes *s, uint32_t addr) { return m_lda8(s, abs8(s, addr)); }
static uint8_t lda_x(Snes *s, uint32_t addr) { return m_lda8(s, absx8(s, addr)); }
static uint8_t lda_y(Snes *s, uint32_t addr) { return m_lda8(s, absy8(s, addr)); }
static uint8_t lda_dp(Snes *s, uint8_t cell) { return m_lda8(s, dp8(s, cell)); }
static uint8_t lda_long_x(Snes *s, uint32_t addr) { return m_lda8(s, m_read8(s, addr + s->cpu.x)); }

static void sta(Snes *s, uint32_t addr) { m_write8(s, m_abs(s, RAM_ABS(addr)), (uint8_t)s->cpu.a); }
static void sta_x(Snes *s, uint32_t addr) { m_write8(s, m_abs(s, RAM_ABS(addr)) + s->cpu.x, (uint8_t)s->cpu.a); }
static void sta_y(Snes *s, uint32_t addr) { m_write8(s, m_abs(s, RAM_ABS(addr)) + s->cpu.y, (uint8_t)s->cpu.a); }
static void sta_dp(Snes *s, uint8_t cell) { m_write8(s, m_dp(s, cell), (uint8_t)s->cpu.a); }
/* LDA/STA dp,X: the direct-page address plus X wraps within bank 0. */
static uint8_t lda_dpx(Snes *s, uint8_t cell) { return m_lda8(s, m_read8(s, (uint16_t)(s->cpu.d + cell + s->cpu.x))); }
static void sta_dpx(Snes *s, uint8_t cell)
{
    m_write8(s, (uint16_t)(s->cpu.d + cell + s->cpu.x), (uint8_t)s->cpu.a);
}
static void stz(Snes *s, uint32_t addr) { m_write8(s, m_abs(s, RAM_ABS(addr)), 0x00); }
static void stz_x(Snes *s, uint32_t addr) { m_write8(s, m_abs(s, RAM_ABS(addr)) + s->cpu.x, 0x00); }
static void stz_dp(Snes *s, uint8_t cell) { m_write8(s, m_dp(s, cell), 0x00); }

static void stx_dp(Snes *s, uint8_t cell) { set_dp16(s, cell, s->cpu.x); }
static void sty_dp(Snes *s, uint8_t cell) { set_dp16(s, cell, s->cpu.y); }
static void ldx_dp(Snes *s, uint8_t cell) { m_ldx16(s, dp16(s, cell)); }
static void ldy_dp(Snes *s, uint8_t cell) { m_ldy(s, dp16(s, cell)); }
static void tay(Snes *s) { m_set_index(s, &s->cpu.y, s->cpu.a); }
static void tya(Snes *s) { m_lda8(s, (uint8_t)s->cpu.y); }
static void dex(Snes *s) { m_set_index(s, &s->cpu.x, (uint16_t)(s->cpu.x - 1)); }
static void dey(Snes *s) { m_set_index(s, &s->cpu.y, (uint16_t)(s->cpu.y - 1)); }
static void cpx(Snes *s, uint16_t v) { m_cmp16(s, s->cpu.x, v); }
static void cpy(Snes *s, uint16_t v) { m_cmp16(s, s->cpu.y, v); }

static void clc(Snes *s) { m_set_flag(s, CPU_FLAG_C, false); }
static void sec(Snes *s) { m_set_flag(s, CPU_FLAG_C, true); }
static void adc(Snes *s, uint8_t v) { m_set_a8(s, m_adc8(s, (uint8_t)s->cpu.a, v)); }
static void sbc(Snes *s, uint8_t v) { m_set_a8(s, m_sbc8(s, (uint8_t)s->cpu.a, v)); }
static void and_a(Snes *s, uint8_t v) { m_lda8(s, (uint8_t)(s->cpu.a & v)); }
static void ora_a(Snes *s, uint8_t v) { m_lda8(s, (uint8_t)(s->cpu.a | v)); }
static void eor_a(Snes *s, uint8_t v) { m_lda8(s, (uint8_t)(s->cpu.a ^ v)); }
static void cmp(Snes *s, uint8_t v) { m_cmp8(s, (uint8_t)s->cpu.a, v); }
static void asl_a(Snes *s) { m_set_a8(s, m_asl8(s, (uint8_t)s->cpu.a)); }
static void lsr_a(Snes *s) { m_set_a8(s, m_lsr8(s, (uint8_t)s->cpu.a)); }
static void inc_a(Snes *s) { m_lda8(s, (uint8_t)(s->cpu.a + 1)); }
static void dec_a(Snes *s) { m_lda8(s, (uint8_t)(s->cpu.a - 1)); }
static uint8_t a8(const Snes *s) { return (uint8_t)s->cpu.a; }

/* INC/DEC on a byte, setting N and Z; returns the new value. */
static uint8_t step8(Snes *s, uint32_t addr, int delta)
{
    uint8_t v = (uint8_t)(m_read8(s, addr) + delta);
    m_write8(s, addr, v);
    m_set_nz8(s, v);
    return v;
}
static uint8_t inc_dp(Snes *s, uint8_t cell) { return step8(s, m_dp(s, cell), +1); }
static uint8_t dec_dp(Snes *s, uint8_t cell) { return step8(s, m_dp(s, cell), -1); }
static void inc_abs(Snes *s, uint32_t addr) { step8(s, m_abs(s, RAM_ABS(addr)), +1); }
static void inc_abs_x(Snes *s, uint32_t addr) { step8(s, m_abs(s, RAM_ABS(addr)) + s->cpu.x, +1); }

/* INC on a 16-bit word (16-bit A mode), setting N and Z. */
static void inc16(Snes *s, uint32_t addr)
{
    uint16_t v = (uint16_t)(m_read16(s, addr) + 1);
    m_write16(s, addr, v);
    m_set_nz16(s, v);
}

/* Shifts on the 8-bit accumulator, `n` times. */
static void asl_a_n(Snes *s, int n)
{
    for (int i = 0; i < n; i++)
        asl_a(s);
}

static bool widths_ok(const Snes *s) { return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) == CPU_FLAG_M; }

/* LDX #last : STA table,X (for each table) : DEX : BPL */
static void store_down(Snes *s, uint16_t last, const uint32_t *tables, int n)
{
    m_ldx16(s, last);
    do {
        for (int i = 0; i < n; i++)
            sta_x(s, tables[i]);
        dex(s);
    } while (!(s->cpu.x & 0x8000));
}
#define STORE_DOWN(s, last, ...) \
    store_down(s, last, (const uint32_t[]){ __VA_ARGS__ }, \
               (int)(sizeof((const uint32_t[]){ __VA_ARGS__ }) / sizeof(uint32_t)))

/* TDC : TAX : LDA src,X : STA dst,X : INX : CPX #n : BNE. Also stands for the
 * loop at $CC:E6D7, which copies two bytes per pass: the compare at the odd
 * counts is overwritten and never ends the loop. */
static void copy_rom_up(Snes *s, uint32_t src, uint32_t dst, uint16_t n)
{
    m_tdc(s);
    m_tax(s);
    do {
        lda_long_x(s, src);
        sta_x(s, dst);
        m_inx(s);
        cpx(s, n);
    } while (s->cpu.x != n);
}

/* LDA src,X : STA dst,Y : INY : INX : CPX #n : BNE */
static void copy_rom_to_y(Snes *s, uint32_t src, uint32_t dst, uint16_t n)
{
    do {
        lda_long_x(s, src);
        sta_y(s, dst);
        m_iny(s);
        m_inx(s);
        cpx(s, n);
    } while (s->cpu.x != n);
}

/* TDC : TAX : LDA #$29 : STZ table,X : STA table+1,X : INX : INX : CPX #n : BNE
 * -- fill a table of words with $2900. */
static void fill_2900_words(Snes *s, uint32_t table, uint16_t n)
{
    m_tdc(s);
    m_tax(s);
    m_lda8(s, 0x29);
    do {
        stz_x(s, table);
        sta_x(s, (uint16_t)(table + 1));
        m_inx(s);
        m_inx(s);
        cpx(s, n);
    } while (s->cpu.x != n);
}

/* LDA #$FF : STA table,X : LDA #$29 : STA table+1,X : INX : INX : CPX #n : BNE */
static void fill_29ff_words(Snes *s, uint32_t table, uint16_t n)
{
    do {
        m_lda8(s, 0xFF);
        sta_x(s, table);
        m_lda8(s, 0x29);
        sta_x(s, (uint16_t)(table + 1));
        m_inx(s);
        m_inx(s);
        cpx(s, n);
    } while (s->cpu.x != n);
}

/* STA $7FFD,X : INC A : INX : INX : CPX #n : BNE -- words counting up from A. */
static void fill_7ffd_counting(Snes *s, uint16_t n)
{
    do {
        sta_x(s, RAM_BATTLE_LOAD_TILEMAP);
        inc_a(s);
        m_inx(s);
        m_inx(s);
        cpx(s, n);
    } while (s->cpu.x != n);
}

/* $CC:E2E4-$CC:E413: reset flags and slot tables; $99E1, $A0D4, $A0D5 and
 * $A0DB from bits of $2990/$2991, and $9F20 from $CC:FBC5 indexed by $2992. */
static void reset_battle_tables(Snes *s)
{
    Cpu *c = &s->cpu;
    m_lda8(s, 0x40);
    sta_dp(s, 0xE4);
    m_lda8(s, 0x01);
    STORE_DOWN(s, 0x0003, RAM_BATTLE_ENEMY_FRAME_DELAY);
    inc_a(s);
    STORE_DOWN(s, 0x0003, (RAM_BATTLE_ENEMY_FRAME_DELAY + 4));
    m_tdc(s);
    m_write8(s, 0x002183, (uint8_t)c->a);
    STORE_DOWN(s, 0x00FF, RAM_BATTLE_OBJECT_X);
    dec_a(s);
    static const uint32_t cells[15] = { (RAM_BATTLE_SELECTED_TECH + 4), 0x99D4, 0x99D5, 0x99D6, 0x99D7, 0x95D2, RAM_BATTLE_ITEM_ARROWS_SHOWN, RAM_BATTLE_COLOR_PHASE,
                                        RAM_BATTLE_MENU_SLOT_MEMBERS, (RAM_BATTLE_MENU_SLOT_MEMBERS + 1), (RAM_BATTLE_MENU_SLOT_MEMBERS + 2), 0xA6DC, RAM_BATTLE_MENU_SLOT, RAM_BATTLE_MENU_SLOT_SHOWN, RAM_BATTLE_STATUS_MEMBER };
    for (int i = 0; i < 15; i++)
        sta(s, cells[i]);
    STORE_DOWN(s, 0x0004, RAM_BATTLE_MENU_MEMBER);
    STORE_DOWN(s, 0x0101, RAM_BATTLE_TECH_LIST_IDS);
    STORE_DOWN(s, 0x0007, RAM_BATTLE_ENEMY_ANIMATION_LOADED, 0x9752, (RAM_BATTLE_OBJECT_PALETTE_INITIAL + 3), RAM_BATTLE_ENEMY_KINDS, RAM_BATTLE_ANCHOR_SOURCES);
    STORE_DOWN(s, 0x000A, RAM_BATTLE_OBJECT_FRAME_SHOWN, RAM_BATTLE_OBJECT_POSE, RAM_BATTLE_OBJECT_POSE_LAST);
    STORE_DOWN(s, 0x0006, 0x9919);
    m_lda8(s, 0x08);
    sta(s, RAM_BATTLE_BLINK_TIMER);
    m_lda8(s, 0x02);
    sta(s, RAM_BATTLE_CURSOR_PRIORITY);
    m_lda8(s, 0x03);
    sta(s, 0x99D9);
    lda(s, 0x2990);
    and_a(s, 0x40);
    eor_a(s, 0x40);
    sta(s, 0x99E1);
    lda(s, 0x2992);
    m_tax(s);
    lda_long_x(s, 0xCCFBC5);
    sta(s, RAM_BATTLE_WINDOW_LAYOUT);
    lda(s, 0x2991);
    and_a(s, 0x40);
    sta(s, 0xA0D4);
    lda(s, 0x2991);
    and_a(s, 0x80);
    sta(s, 0xA0D5);
    lda(s, 0x2990);
    and_a(s, 0x80);
    sta(s, 0xA0DB);
    /* LDX then LDA before these loops: their flags are the loops' DEX's anyway */
    m_lda8(s, 0xFF);
    STORE_DOWN(s, 0x000E, 0x9842);
    stz(s, 0x9851);
    m_ldx16(s, 0x0002);
    m_lda8(s, 0xF0);
    do {
        stz_x(s, RAM_BATTLE_CURSOR_OFFSET_X);
        sta_x(s, RAM_BATTLE_CURSOR_OFFSET_Y);
        dex(s);
    } while (!(c->x & 0x8000));
    m_lda8(s, 0x08);
    STORE_DOWN(s, 0x0002, RAM_BATTLE_OBJECT_HALF_WIDTH, RAM_BATTLE_OBJECT_HALF_HEIGHT);
    m_ldx16(s, 0x0002);
    do {
        m_lda8(s, 0x02);
        sta_x(s, 0x98BF);
        m_lda8(s, 0x01);
        sta_x(s, 0x973F);
        dex(s);
    } while (!(c->x & 0x8000));
    m_lda8(s, 0x20);
    STORE_DOWN(s, 0x000F, RAM_BATTLE_ENEMY_EFFECT_DELAY);
    lda(s, RAM_BATTLE_SPRITE_ATTRIBUTES);
    STORE_DOWN(s, 0x000A, RAM_BATTLE_OBJECT_ATTRIBUTE_BITS, 0x9F3B);
    STORE_DOWN(s, 0x0007, 0xAB5F, 0xAB57);
}

/* $CC:E414-$CC:E465: the 32 rows of 4x4 cells at $8E2D and $8C2D from the
 * 4-byte patterns at $CC:F4C4 and $CC:F4CC; $A5C2-$A5CC = $C0; and $902D + i =
 * i / 16 for 512 bytes. */
static void fill_cell_tables(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    tay(s);
    do {
        m_tdc(s);
        m_tax(s);
        do {
            lda_long_x(s, 0xCCF4C4);
            for (uint16_t i = 0; i < 4; i++)
                sta_y(s, (uint16_t)(0x8E2D + i));
            lda_long_x(s, 0xCCF4CC);
            for (uint16_t i = 0; i < 4; i++)
                sta_y(s, (uint16_t)(RAM_BATTLE_OAM_HIGH_BITS + i));
            for (int i = 0; i < 4; i++)
                m_iny(s);
            m_inx(s);
            cpx(s, 0x0004);
        } while (c->x != 0x0004);
        cpy(s, 0x0200);
    } while (c->y != 0x0200);
    m_lda8(s, 0xC0);
    STORE_DOWN(s, 0x000A, 0xA5C2);
    m_tdc(s);
    m_tax(s);
    do {
        m_rep(s, CPU_FLAG_M);
        m_txa(s);
        for (int i = 0; i < 4; i++)
            c->a = m_lsr16(s, c->a);
        m_sep(s, CPU_FLAG_M);
        sta_x(s, RAM_BATTLE_OAM_HIGH_OFFSETS);
        m_inx(s);
        cpx(s, 0x0200);
    } while (c->x != 0x0200);
}

/* $CC:E466-$CC:E4B9: the 256-byte base battlefield grid at $7BFD (a top row
 * pattern from $CC:F694, eleven rows from $CC:F644, three rows from $CC:F694
 * again), then bit 7 of each cell's $2A23 and $2B23 bytes ORed in as bits 7
 * and 6. */
static void build_base_grid(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_tax(s);
    tay(s);
    copy_rom_to_y(s, 0xCCF694, RAM_BATTLE_BASE_GRID, 0x0020);
    m_lda8(s, 0x0B);
    sta_dp(s, 0x80);
    do {
        m_tdc(s);
        m_tax(s);
        copy_rom_to_y(s, 0xCCF644, RAM_BATTLE_BASE_GRID, 0x0010);
    } while (dec_dp(s, 0x80) != 0);
    m_tdc(s);
    m_tax(s);
    copy_rom_to_y(s, 0xCCF694, RAM_BATTLE_BASE_GRID, 0x0030);
    m_ldx16(s, 0x00FF);
    do {
        lda_x(s, 0x2B23);
        and_a(s, 0x80);
        lsr_a(s);
        sta_dp(s, 0x80);
        lda_x(s, 0x2A23);
        and_a(s, 0x80);
        ora_a(s, dp8(s, 0x80));
        ora_a(s, absx8(s, RAM_BATTLE_BASE_GRID));
        sta_x(s, RAM_BATTLE_BASE_GRID);
        dex(s);
    } while (!(c->x & 0x8000));
}

/* $CC:E4BA-$CC:E564: the $2900-word buffers at $99E3, $A6E1 and $0CC0; the
 * table at $0B40 from $D1:5800; the word tables at $7FFD (words $29FF, then
 * counting runs from $00, $30, $10 and $40 at offsets $40-$BF) and $80FD
 * (bytes from $D1:5D50, then words $29FF). */
static void reset_word_buffers(Snes *s)
{
    fill_2900_words(s, RAM_BATTLE_MENU_LIST_TILES, 0x0500);
    fill_2900_words(s, RAM_BATTLE_MENU_TILEMAP_TILE, 0x0180);
    fill_2900_words(s, RAM_BATTLE_WINDOW_TILEMAP_TILE, 0x04C0);
    copy_rom_up(s, 0xD15800, RAM_BATTLE_WINDOW_FRAMES_TILEMAP_TILE, 0x0180);
    m_tdc(s);
    m_tax(s);
    fill_29ff_words(s, RAM_BATTLE_LOAD_TILEMAP, 0x0100);
    m_tdc(s);
    m_ldx16(s, 0x0040);
    fill_7ffd_counting(s, 0x0060);
    m_lda8(s, 0x30);
    fill_7ffd_counting(s, 0x0080);
    m_lda8(s, 0x10);
    fill_7ffd_counting(s, 0x00A0);
    m_lda8(s, 0x40);
    fill_7ffd_counting(s, 0x00C0);
    copy_rom_up(s, 0xD15D50, RAM_BATTLE_LOAD_TILEMAP_2, 0x00C0);
    fill_29ff_words(s, RAM_BATTLE_LOAD_TILEMAP_2, 0x0200);
}

/* $CC:F063, called with JSR from `site`: TDC, then JSL $C1:0018, which with A =
 * 0 runs $C1:01A9: presumably the 16-character name at $94A0 turned into two
 * rows of tiles, $94A0 and $94B0. False if it came back with other widths
 * (the port then stops there, inside $CC:F063). */
static bool convert_name(Snes *s, uint32_t site)
{
    m_push16(s, (uint16_t)(site + 2));
    m_tdc(s);
    m_jsl(s, 0xCCF066, 0xC10018);
    if (!widths_ok(s))
        return false;
    m_pull16(s);
    return true;
}

/* TDC : TAX : LDA $94A0,X : STA dst,Y : INY : INX : CPX #n : BNE, then the
 * same for $94B0: both converted rows, `n` bytes each. */
static void store_name_rows(Snes *s, uint32_t dst, uint16_t n)
{
    for (uint32_t row = RAM_BATTLE_NAME_BUFFER; row <= RAM_BATTLE_NAME_BUFFER + 0x10; row += 0x10) {
        m_tdc(s);
        m_tax(s);
        do {
            lda_x(s, row);
            sta_y(s, dst);
            m_iny(s);
            m_inx(s);
            cpx(s, n);
        } while (s->cpu.x != n);
    }
}

/* $CC:E595 and $CC:E659: for `count` names ($80), copy the 5 bytes at `src` +
 * the name's offset from $CC:F83F to $94A0 (with $94A5 = 0), convert them,
 * and store both rows at `dst` + its offset from $CC:F837. */
static bool convert_names(Snes *s, uint32_t src, uint32_t dst, uint8_t count, uint32_t site)
{
    m_tdc(s);
    tay(s);
    sty_dp(s, 0x80);
    do {
        lda_dp(s, 0x80);
        m_tax(s);
        lda_long_x(s, 0xCCF83F);
        tay(s);
        for (uint16_t i = 0; i < 5; i++) {
            lda_y(s, (uint16_t)(src + i));
            sta(s, (uint16_t)(RAM_BATTLE_NAME_BUFFER + i));
        }
        stz(s, (RAM_BATTLE_NAME_BUFFER + 5));
        if (!convert_name(s, site))
            return false;
        lda_dp(s, 0x80);
        m_tax(s);
        lda_long_x(s, 0xCCF837);
        tay(s);
        store_name_rows(s, dst, 0x0005);
        inc_dp(s, 0x80);
        lda_dp(s, 0x80);
        cmp(s, count);
    } while (a8(s) != count);
    return true;
}

/* $CC:E565-$CC:E594: seven 5-byte entries from the 6-byte records at $2C23
 * to $9430. */
static void copy_name_records(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_tax(s);
    tay(s);
    do {
        for (uint16_t i = 0; i < 5; i++) {
            lda_y(s, (uint16_t)(RAM_CHARACTER_NAMES + i));
            sta_x(s, (uint16_t)(RAM_BATTLE_CHARACTER_NAMES + i));
        }
        tya(s);
        clc(s);
        adc(s, 0x06);
        tay(s);
        m_txa(s);
        clc(s);
        adc(s, 0x05);
        m_tax(s);
        cpx(s, 0x0023);
    } while (c->x != 0x0023);
}

/* $CC:E5F3-$CC:E658: for the three party slots ($2980 + slot; negative:
 * empty), note the slot whose low bits are 4 in $A0FF (then times 5) and the
 * one whose are 6 in $A115 (counted in $A117), copy 5 bytes of $2C23 + the
 * offset for those bits ($CC:F846) to $9403 + slot * 5, and mark the slot
 * present in $96F5. */
static void read_party_slots(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_tax(s);
    stx_dp(s, 0x80);
    stx_dp(s, 0x82);
    do {
        ldx_dp(s, 0x80);
        if (!(lda_x(s, RAM_PARTY_MEMBERS) & 0x80)) {
            and_a(s, 0x07);
            cmp(s, 0x04);
            if (a8(s) == 0x04) {
                m_write16(s, m_abs(s, RAM_ABS(RAM_BATTLE_FROG_SLOT)), c->x);
            } else {
                cmp(s, 0x06);
                if (a8(s) == 0x06) {
                    m_write16(s, m_abs(s, RAM_ABS(RAM_BATTLE_MAGUS_SLOT)), c->x);
                    inc_abs(s, RAM_BATTLE_MAGUS_PRESENT);
                }
            }
            m_tax(s);
            lda_long_x(s, 0xCCF846);
            m_tax(s);
            ldy_dp(s, 0x82);
            for (uint16_t i = 0; i < 5; i++) {
                lda_x(s, (uint16_t)(RAM_CHARACTER_NAMES + i));
                sta_y(s, (uint16_t)(RAM_BATTLE_PARTY_NAMES + i));
            }
            ldx_dp(s, 0x80);
            inc_abs_x(s, RAM_BATTLE_SLOT_PRESENT);
        }
        inc_dp(s, 0x80);
        clc(s);
        lda_dp(s, 0x82);
        adc(s, 0x05);
        sta_dp(s, 0x82);
        cmp(s, 0x0F);
    } while (a8(s) != 0x0F);
    /* $CC:E64D */
    lda(s, RAM_BATTLE_FROG_SLOT);
    asl_a_n(s, 2);
    clc(s);
    adc(s, abs8(s, RAM_BATTLE_FROG_SLOT));
    sta(s, RAM_BATTLE_FROG_SLOT);
}

/* $CC:E6B7-$CC:E70F: palette colors from $FF:9C68, $FF:9C70 and $FF:9E0A into
 * the palette buffer at $2000, copied whole to $2200; then $9888-$988A and
 * $993C-$993E = $0A, $0C, $0E. */
static void load_palettes(Snes *s)
{
    Cpu *c = &s->cpu;
    copy_rom_up(s, 0xFF9C68, (RAM_PALETTE_BASE + 0x10), 0x0008);
    copy_rom_up(s, 0xFF9C70, (RAM_PALETTE_BASE + 0x18), 0x0008);
    copy_rom_up(s, 0xFF9E0A, (RAM_PALETTE_BASE + 0x13A), 0x0006);
    m_ldx16(s, 0x01FF);
    do {
        lda_x(s, RAM_PALETTE_BASE);
        sta_x(s, RAM_PALETTE_WORKING);
        dex(s);
    } while (!(c->x & 0x8000));
    m_lda8(s, 0x0A);
    for (uint16_t i = 0; i < 3; i++) {
        if (i != 0) {
            inc_a(s);
            inc_a(s);
        }
        sta(s, (uint16_t)(RAM_BATTLE_OBJECT_PALETTE_INITIAL + i));
        sta(s, (uint16_t)(RAM_BATTLE_OBJECT_PALETTE + i));
    }
}

/* $CC:E710-$CC:E754: for each party member ($84; its $1C48 entry at $82 =
 * member * 5), copy 6 bytes from $D1:4B00 + ($CD:6CEC + that entry) * 6 to
 * $23BA + member * 32 in the palette buffer. */
static void copy_party_colors(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_tax(s);
    stx_dp(s, 0x82);
    stx_dp(s, 0x84);
    do {
        ldy_dp(s, 0x82);
        lda_y(s, 0x1C48);
        m_tax(s);
        lda_long_x(s, 0xCD6CEC);
        m_rep(s, CPU_FLAG_M);
        set_dp16(s, 0x80, c->a);
        c->a = m_asl16(s, c->a);
        clc(s);
        c->a = m_adc16(s, c->a, dp16(s, 0x80));
        c->a = m_asl16(s, c->a);
        m_tax(s);
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        lda_dp(s, 0x84);
        asl_a_n(s, 5);
        tay(s);
        m_lda8(s, 0x06);
        sta_dp(s, 0x80);
        do {
            lda_long_x(s, 0xD14B00);
            sta_y(s, (RAM_PALETTE_WORKING + 0x1BA));
            m_inx(s);
            m_iny(s);
        } while (dec_dp(s, 0x80) != 0);
        clc(s);
        lda_dp(s, 0x82);
        adc(s, 0x05);
        sta_dp(s, 0x82);
        inc_dp(s, 0x84);
        lda_dp(s, 0x84);
        cmp(s, 0x03);
    } while (a8(s) != 0x03);
}

/* $CC:E755-$CC:E77F: for members 2 down to 0, with the record base from
 * $CC:F395 in $80: set $9F25 + member when its record's $1AC6 byte is
 * positive, and $9F28 + member when its $1AFE byte is. */
static void mark_member_records(Snes *s)
{
    Cpu *c = &s->cpu;
    m_ldy(s, 0x0002);
    do {
        tya(s);
        asl_a(s);
        m_tax(s);
        lda_long_x(s, 0xCCF395);
        sta_dp(s, 0x80);
        lda_long_x(s, 0xCCF396);
        sta_dp(s, 0x81);
        ldx_dp(s, 0x80);
        if (!(lda_x(s, (RAM_BATTLE_TECH_RECORDS_ID + 10 * RAM_BATTLE_TECH_RECORDS_STRIDE)) & 0x80)) {
            m_lda8(s, 0x01);
            sta_y(s, RAM_BATTLE_DOUBLE_TECHS);
        }
        if (!(lda_x(s, (RAM_BATTLE_TECH_RECORDS_ID + 18 * RAM_BATTLE_TECH_RECORDS_STRIDE)) & 0x80)) {
            m_lda8(s, 0x01);
            sta_y(s, RAM_BATTLE_TRIPLE_TECHS);
        }
        dey(s);
    } while (!(c->y & 0x8000));
}

/* LDA $83 : ASL : TAX : LDA $CCF395,X : STA $84 : LDA $CCF396,X : STA $85 :
 * LDX $84 -- member $83's record base. */
static void load_member_records(Snes *s)
{
    lda_dp(s, 0x83);
    asl_a(s);
    m_tax(s);
    lda_long_x(s, 0xCCF395);
    sta_dp(s, 0x84);
    lda_long_x(s, 0xCCF396);
    sta_dp(s, 0x85);
    ldx_dp(s, 0x84);
}

/* LDA $83 : ASL : TAX : LDA $86,X : STA $84 : LDA $87,X : STA $85 : LDY $84
 * -- the end of member $83's list, saved in $86 + member * 2. */
static void load_member_list_end(Snes *s)
{
    lda_dp(s, 0x83);
    asl_a(s);
    m_tax(s);
    lda_dpx(s, 0x86);
    sta_dp(s, 0x84);
    lda_dpx(s, 0x87);
    sta_dp(s, 0x85);
    ldy_dp(s, 0x84);
}

/* $CC:E7A4 and $CC:E818: for entries $82 up to `end` of the member's records
 * (7 bytes each from `table` + X), append each first byte other than $FF to
 * the list at $94D0 + Y, with the entry number at $9551 + Y, counting them in
 * $8C. */
static void append_member_entries(Snes *s, uint32_t table, uint8_t end)
{
    Cpu *c = &s->cpu;
    do {
        uint8_t v = lda_x(s, table);
        cmp(s, 0xFF);
        if (v != 0xFF) {
            sta_y(s, RAM_BATTLE_TECH_LIST_IDS);
            lda_dp(s, 0x82);
            sta_y(s, RAM_BATTLE_TECH_LIST);
            m_iny(s);
            inc_dp(s, 0x8C);
        }
        m_rep(s, CPU_FLAG_M | CPU_FLAG_C);
        m_txa(s);
        c->a = m_adc16(s, c->a, 0x0007);
        m_tax(s);
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        inc_dp(s, 0x82);
        lda_dp(s, 0x82);
        cmp(s, end);
    } while (a8(s) != end);
}

/* $CC:E7C8 and $CC:E83C: when `flag` + member is set, append `marker` to the
 * list; save the list end in $86 + member * 2 and move to the next member.
 * True while members remain. */
static bool finish_member_list(Snes *s, uint32_t flag, uint8_t marker)
{
    lda_dp(s, 0x83);
    m_tax(s);
    if (lda_x(s, flag) != 0) {
        m_lda8(s, marker);
        sta_y(s, RAM_BATTLE_TECH_LIST_IDS);
        m_iny(s);
    }
    sty_dp(s, 0x84);
    lda_dp(s, 0x83);
    asl_a(s);
    m_tax(s);
    lda_dp(s, 0x84);
    sta_dpx(s, 0x86);
    lda_dp(s, 0x85);
    sta_dpx(s, 0x87);
    inc_dp(s, 0x83);
    lda_dp(s, 0x83);
    cmp(s, 0x03);
    return a8(s) != 0x03;
}

/* $CC:E780-$CC:E8D1: each party member's list at $94D0 + the offset from
 * $CC:F38C (member $83): its record entries 0-7, then $FC when $9F25 is set;
 * entries 10-15, then $FD when $9F28 is set; and entry 18 when $9F28 is set
 * (with $12 as its number unless negative). Then $A02A + member counts the
 * list's entries up to the first $FF, at most 20. */
static void build_member_lists(Snes *s)
{
    m_tdc(s);
    m_tax(s);
    stx_dp(s, 0x80);
    stx_dp(s, 0x82);
    do {
        stz_dp(s, 0x82);
        stz_dp(s, 0x8C);
        lda_dp(s, 0x83);
        m_tax(s);
        lda_long_x(s, 0xCCF38C);
        tay(s);
        load_member_records(s);
        append_member_entries(s, RAM_BATTLE_TECH_RECORDS_ID, 0x08);
    } while (finish_member_list(s, RAM_BATTLE_DOUBLE_TECHS, 0xFC));

    /* $CC:E7EC */
    m_tdc(s);
    m_tax(s);
    stx_dp(s, 0x80);
    stx_dp(s, 0x82);
    do {
        m_lda8(s, 0x0A);
        sta_dp(s, 0x82);
        stz_dp(s, 0x8C);
        load_member_list_end(s);
        load_member_records(s);
        append_member_entries(s, (RAM_BATTLE_TECH_RECORDS_ID + 10 * RAM_BATTLE_TECH_RECORDS_STRIDE), 0x10);
    } while (finish_member_list(s, RAM_BATTLE_TRIPLE_TECHS, 0xFD));

    /* $CC:E863 */
    m_tdc(s);
    m_tax(s);
    stx_dp(s, 0x80);
    stx_dp(s, 0x82);
    do {
        lda_dp(s, 0x83);
        m_tax(s);
        if (lda_x(s, RAM_BATTLE_TRIPLE_TECHS) != 0) {
            m_lda8(s, 0x12);
            sta_dp(s, 0x82);
            load_member_list_end(s);
            load_member_records(s);
            uint8_t v = lda_x(s, (RAM_BATTLE_TECH_RECORDS_ID + 18 * RAM_BATTLE_TECH_RECORDS_STRIDE));
            sta_y(s, RAM_BATTLE_TECH_LIST_IDS);
            if (!(v & 0x80)) {
                m_lda8(s, 0x12);
                sta_y(s, RAM_BATTLE_TECH_LIST);
            }
        }
        inc_dp(s, 0x83);
        lda_dp(s, 0x83);
        cmp(s, 0x03);
    } while (a8(s) != 0x03);

    /* $CC:E8AA */
    m_ldx16(s, 0x0002);
    stx_dp(s, 0x80);
    do {
        lda_dp(s, 0x80);
        m_tax(s);
        lda_long_x(s, 0xCCF38C);
        tay(s);
        ldx_dp(s, 0x80);
        stz_x(s, RAM_BATTLE_TECH_LIST_LENGTH);
        for (;;) {
            uint8_t v = lda_y(s, RAM_BATTLE_TECH_LIST_IDS);
            cmp(s, 0xFF);
            if (v == 0xFF)
                break;
            m_iny(s);
            inc_abs_x(s, RAM_BATTLE_TECH_LIST_LENGTH);
            uint8_t n = lda_x(s, RAM_BATTLE_TECH_LIST_LENGTH);
            cmp(s, 0x14);
            if (n == 0x14)
                break;
        }
    } while (!(dec_dp(s, 0x80) & 0x80));
}

/* $CC:E8D2-$CC:E932: for party slots 2 down to 0 that are present ($2980),
 * with Y = the slot's value * 4: $9916 from $2C7C + Y when $A0D4 is set; when
 * $A0D5 is set, $A0D8, $A863 and $A866 from $2C7F, $2C7E and $2C7D, clearing
 * the last two unless the member's list entry they select holds the $2C7F
 * value. Then with $A0D5 set, $95E6 and $95E5 from $2C98 and $2C99. */
static void read_slot_extras(Snes *s)
{
    m_ldx16(s, 0x0002);
    stx_dp(s, 0x80);
    do {
        ldx_dp(s, 0x80);
        if (!(lda_x(s, RAM_PARTY_MEMBERS) & 0x80)) {
            asl_a_n(s, 2);
            tay(s);
            if (lda(s, 0xA0D4) != 0) {
                lda_y(s, 0x2C7C);
                sta_x(s, 0x9916);
            }
            if (lda(s, 0xA0D5) != 0) {
                lda_y(s, 0x2C7F);
                sta_x(s, 0xA0D8);
                lda_y(s, 0x2C7E);
                sta_x(s, 0xA863);
                lda_y(s, 0x2C7D);
                sta_x(s, 0xA866);
                clc(s);
                adc(s, absx8(s, 0xA863));
                ldx_dp(s, 0x80);
                adc(s, m_read8(s, 0xCCF38C + s->cpu.x));
                m_tax(s);
                uint8_t want = lda_y(s, 0x2C7F);
                uint8_t have = absx8(s, RAM_BATTLE_TECH_LIST_IDS);
                m_cmp8(s, want, have);
                if (want != have) {
                    ldx_dp(s, 0x80);
                    stz_x(s, 0xA863);
                    stz_x(s, 0xA866);
                }
            }
        }
    } while (!(dec_dp(s, 0x80) & 0x80));
    if (lda(s, 0xA0D5) != 0) {
        lda(s, RAM_BATTLE_REMEMBERED_ITEM_TOP);
        sta(s, RAM_BATTLE_ITEM_TOP);
        lda(s, RAM_BATTLE_REMEMBERED_ITEM_ROW);
        sta(s, RAM_BATTLE_ITEM_ROW);
    }
}

/* LDA tile,X : ASL x4 : CLC : ADC fine,X -- a coordinate from a tile number
 * and an offset within the tile. */
static void tile_coordinate(Snes *s, uint32_t tile, uint32_t fine)
{
    lda_x(s, tile);
    asl_a_n(s, 4);
    clc(s);
    adc(s, absx8(s, fine));
}

/* $CC:E933-$CC:E979: copy the 15 bytes at $29B0 to $96A4 and place the
 * three party objects from them (5 bytes each): X at $1D0C, Y at $1D23,
 * $1D7F = $0F, $A4DC from the fifth byte, $96DB cleared. */
static void place_party_objects(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_tax(s);
    do {
        lda_x(s, RAM_BATTLE_PARTY_PLACEMENT_TILE_X);
        sta_x(s, 0x96A4);
        m_inx(s);
        cpx(s, 0x000F);
    } while (c->x != 0x000F);
    m_tdc(s);
    tay(s);
    m_tax(s);
    do {
        tile_coordinate(s, 0x96A4, 0x96A6);
        sta_y(s, (RAM_BATTLE_OBJECT_X + 0xC));
        tile_coordinate(s, 0x96A5, 0x96A7);
        sta_y(s, (RAM_BATTLE_OBJECT_Y + 0xC));
        m_lda8(s, 0x0F);
        sta_y(s, (RAM_BATTLE_OBJECT_PART_COUNT + 0xC));
        lda_x(s, 0x96A8);
        sta_y(s, 0xA4DC);
        m_tdc(s);
        sta_y(s, RAM_BATTLE_OBJECT_FACING);
        m_txa(s);
        clc(s);
        adc(s, 0x05);
        m_tax(s);
        m_iny(s);
        cpy(s, 0x0003);
    } while (c->y != 0x0003);
}

/* $CC:E97A-$CC:EA32: for each party member ($80), from its character ($2980)
 * in the tables at $E4:2000, $E4:2300 (3-byte entries at character * 3) and
 * $E4:2600, $E4:2800 (words at character * 2): the 3-byte values at $1C57 and
 * $1C78 + member * 3, long pointers ($E4) at $1C99 and $1CBA, and in $966E
 * and $A2C8 the low and high bytes of (next $E4:2600 word - this one) / 4. */
static void load_party_graphics(Snes *s)
{
    Cpu *c = &s->cpu;
    stz_dp(s, 0x80);
    stz_dp(s, 0x81);
    do {
        ldy_dp(s, 0x80);
        lda_y(s, RAM_PARTY_MEMBERS);
        asl_a(s);
        m_push8(s, a8(s));
        clc(s);
        adc(s, absy8(s, RAM_PARTY_MEMBERS));
        m_tax(s);
        tya(s);
        asl_a(s);
        clc(s);
        adc(s, dp8(s, 0x80));
        tay(s);
        for (uint16_t i = 0; i < 3; i++) {
            lda_long_x(s, 0xE42000 + i);
            sta_y(s, (uint16_t)(RAM_BATTLE_OBJECT_GRAPHICS + i));
        }
        for (uint16_t i = 0; i < 3; i++) {
            lda_long_x(s, 0xE42300 + i);
            sta_y(s, (uint16_t)(RAM_BATTLE_OBJECT_TILE_LISTS + i));
        }
        m_lda8(s, m_pull8(s));
        m_tax(s);
        static const struct { uint32_t src, dst; } ptrs[2] = { { 0xE42600, RAM_BATTLE_OBJECT_FRAME_LISTS },
                                                          { 0xE42800, RAM_BATTLE_OBJECT_DELAY_LISTS } };
        for (int p = 0; p < 2; p++) {
            for (uint16_t i = 0; i < 2; i++) {
                lda_long_x(s, ptrs[p].src + i);
                sta_y(s, (uint16_t)(ptrs[p].dst + i));
                sta_dp(s, (uint8_t)(0x82 + 2 * p + i));
            }
            m_lda8(s, 0xE4);
            sta_y(s, (uint16_t)(ptrs[p].dst + 2));
        }
        m_txa(s);
        lsr_a(s);
        inc_a(s);
        asl_a(s);
        m_tax(s);
        lda_long_x(s, 0xE42600);
        sta_dp(s, 0x86);
        lda_long_x(s, 0xE42601);
        sta_dp(s, 0x87);
        lda_long_x(s, 0xE42800);
        sta_dp(s, 0x88);
        lda_long_x(s, 0xE42801);
        sta_dp(s, 0x89);
        m_rep(s, CPU_FLAG_M);
        sec(s);
        m_lda16(s, dp16(s, 0x86));
        c->a = m_sbc16(s, c->a, dp16(s, 0x82));
        c->a = m_lsr16(s, c->a);
        c->a = m_lsr16(s, c->a);
        set_dp16(s, 0x82, c->a);
        sec(s);
        m_lda16(s, dp16(s, 0x88));
        c->a = m_sbc16(s, c->a, dp16(s, 0x84));
        set_dp16(s, 0x84, c->a);
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        ldx_dp(s, 0x80);
        lda_dp(s, 0x82);
        sta_x(s, RAM_BATTLE_FACING_STRIDE_LOW);
        lda_dp(s, 0x83);
        sta_x(s, RAM_BATTLE_FACING_STRIDE_HIGH);
        inc_dp(s, 0x80);
        lda_dp(s, 0x80);
        cmp(s, 0x03);
    } while (a8(s) != 0x03);
}

/* $CC:EA33-$CC:EA56: copy 5 bytes of each of the eight 12-byte enemy
 * records at $29BF to $96B3. */
static void copy_enemy_headers(Snes *s)
{
    m_tdc(s);
    m_tax(s);
    tay(s);
    stz_dp(s, 0x80);
    do {
        stz_dp(s, 0x81);
        do {
            lda_x(s, RAM_BATTLE_ENEMY_PLACEMENT_TILE_X);
            sta_y(s, 0x96B3);
            m_inx(s);
            m_iny(s);
            inc_dp(s, 0x81);
            lda_dp(s, 0x81);
            cmp(s, 0x05);
        } while (a8(s) != 0x05);
        clc(s);
        m_txa(s);
        adc(s, 0x07);
        m_tax(s);
        inc_dp(s, 0x80);
        lda_dp(s, 0x80);
        cmp(s, 0x08);
    } while (a8(s) != 0x08);
}

/* A 16-bit read and write in the data bank, indexed. */
static uint16_t absx16(Snes *s, uint32_t addr) { return m_read16(s, m_abs(s, RAM_ABS(addr)) + s->cpu.x); }
static void set_absy16(Snes *s, uint32_t addr, uint16_t v) { m_write16(s, m_abs(s, RAM_ABS(addr)) + s->cpu.y, v); }

/* REP #$20 : LDA $8C (or $29C4,X) : ASL : STA $82 : ASL : ASL : CLC : ADC $82 :
 * TAX -- the enemy's index times 10 (16-bit A). */
static void enemy_times_10(Snes *s, uint16_t v)
{
    Cpu *c = &s->cpu;
    m_lda16(s, v);
    c->a = m_asl16(s, c->a);
    set_dp16(s, 0x82, c->a);
    c->a = m_asl16(s, c->a);
    c->a = m_asl16(s, c->a);
    clc(s);
    c->a = m_adc16(s, c->a, dp16(s, 0x82));
    m_tax(s);
}

/* $CC:EA6A-$CC:EC25: set up enemy $80 (object 3 + $80) from its record at
 * $29BF + X and its enemy number ($29C4, a word) in the 10-byte table at
 * $E4:F600: kind, position, graphics pointers from $E4:2300, $E4:2600 and
 * $E4:2800, and the flags and values of $E4:F604-$E4:F609; enemies $D8, $94
 * and $AD also get $1D26 incremented. Marks it present in $96F8. */
static void setup_enemy(Snes *s)
{
    Cpu *c = &s->cpu;
    clc(s);
    lda_dp(s, 0x80);
    adc(s, 0x03);
    tay(s);
    lda_x(s, RAM_BATTLE_ENEMY_PLACEMENT_KIND);
    and_a(s, 0x07);
    sta_y(s, RAM_BATTLE_OBJECT_KIND);
    m_push16(s, c->x);
    m_tax(s);
    lda_long_x(s, 0xCCF505);
    sta_y(s, (RAM_BATTLE_OBJECT_PART_COUNT + 0xC));
    c->x = m_pull16(s);
    m_set_nz16(s, c->x);
    tile_coordinate(s, RAM_BATTLE_ENEMY_PLACEMENT_TILE_X, RAM_BATTLE_ENEMY_PLACEMENT_X_OFFSET);
    sta_y(s, (RAM_BATTLE_OBJECT_X + 0xC));
    tile_coordinate(s, RAM_BATTLE_ENEMY_PLACEMENT_TILE_Y, RAM_BATTLE_ENEMY_PLACEMENT_Y_OFFSET);
    sta_y(s, (RAM_BATTLE_OBJECT_Y + 0xC));
    lda_x(s, (RAM_BATTLE_ENEMY_PLACEMENT + 4));
    sta_y(s, 0xA4DC);
    m_tdc(s);
    sta_y(s, RAM_BATTLE_OBJECT_FACING);
    sta_y(s, RAM_BATTLE_OBJECT_FACING_LOADED);
    lda_x(s, RAM_BATTLE_ENEMY_PLACEMENT_PALETTE);
    asl_a(s);
    sta_y(s, RAM_BATTLE_OBJECT_PALETTE_INITIAL);
    sta_y(s, RAM_BATTLE_OBJECT_PALETTE);
    lda_x(s, RAM_BATTLE_ENEMY_PLACEMENT_KIND);
    and_a(s, 0x80);
    sta_y(s, RAM_BATTLE_OBJECT_HIDDEN);
    lda_dp(s, 0x80);
    asl_a(s);
    clc(s);
    adc(s, dp8(s, 0x80));
    tay(s);
    lda_x(s, (RAM_BATTLE_ENEMY_PLACEMENT + 0xB));
    sta_y(s, (RAM_BATTLE_OBJECT_GRAPHICS + 0xB));

    /* $CC:EACA */
    m_rep(s, CPU_FLAG_M);
    set_absy16(s, (RAM_BATTLE_OBJECT_GRAPHICS + 9), m_lda16(s, absx16(s, (RAM_BATTLE_ENEMY_PLACEMENT + 9))));
    uint16_t enemy = m_lda16(s, absx16(s, RAM_BATTLE_ENEMY_PLACEMENT_ENEMY));
    set_dp16(s, 0x8C, enemy);
    enemy_times_10(s, enemy);
    m_lda16(s, m_read16(s, 0xE4F603 + c->x));
    set_dp16(s, 0x8E, m_lda16(s, c->a & 0x00FF));
    m_lda16(s, m_read16(s, 0xE4F601 + c->x));
    set_dp16(s, 0x82, m_lda16(s, c->a & 0x00FF));
    c->a = m_asl16(s, c->a);
    clc(s);
    c->a = m_adc16(s, c->a, dp16(s, 0x82));
    m_tax(s);
    set_absy16(s, (RAM_BATTLE_OBJECT_TILE_LISTS + 9), m_lda16(s, m_read16(s, 0xE42300 + c->x)));
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    lda_long_x(s, 0xE42302);
    sta_y(s, (RAM_BATTLE_OBJECT_TILE_LISTS + 0xB));
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, dp16(s, 0x8E));
    c->a = m_asl16(s, c->a);
    m_tax(s);
    m_lda16(s, m_read16(s, 0xE42600 + c->x));
    set_absy16(s, (RAM_BATTLE_OBJECT_FRAME_LISTS + 9), c->a);
    set_dp16(s, 0x84, c->a);
    m_lda16(s, m_read16(s, 0xE42800 + c->x));
    set_absy16(s, (RAM_BATTLE_OBJECT_DELAY_LISTS + 9), c->a);
    set_dp16(s, 0x86, c->a);
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, 0xE4);
    sta_y(s, (RAM_BATTLE_OBJECT_FRAME_LISTS + 0xB));
    m_lda8(s, 0xE4);
    sta_y(s, (RAM_BATTLE_OBJECT_DELAY_LISTS + 0xB));
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, dp16(s, 0x8E));
    m_lda16(s, (uint16_t)(c->a + 1));
    c->a = m_asl16(s, c->a);
    m_tax(s);
    set_dp16(s, 0x88, m_lda16(s, m_read16(s, 0xE42600 + c->x)));
    set_dp16(s, 0x8A, m_lda16(s, m_read16(s, 0xE42800 + c->x)));
    sec(s);
    m_lda16(s, dp16(s, 0x88));
    c->a = m_sbc16(s, c->a, dp16(s, 0x84));
    c->a = m_lsr16(s, c->a);
    c->a = m_lsr16(s, c->a);
    set_dp16(s, 0x84, c->a);
    sec(s);
    m_lda16(s, dp16(s, 0x8A));
    c->a = m_sbc16(s, c->a, dp16(s, 0x86));
    set_dp16(s, 0x86, c->a);
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    clc(s);
    lda_dp(s, 0x80);
    adc(s, 0x03);
    m_tax(s);
    lda_dp(s, 0x84);
    sta_x(s, RAM_BATTLE_FACING_STRIDE_LOW);
    lda_dp(s, 0x85);
    sta_x(s, RAM_BATTLE_FACING_STRIDE_HIGH);
    m_rep(s, CPU_FLAG_M);
    enemy_times_10(s, dp16(s, 0x8C));
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);

    /* $CC:EB73 */
    ldy_dp(s, 0x80);
    lda_long_x(s, 0xE4F604);
    sta_y(s, RAM_BATTLE_ENEMY_FLAGS);
    and_a(s, 0x80);
    if (a8(s) != 0) {
        sta_y(s, 0xA5C5);
        sta_y(s, (RAM_BATTLE_OBJECT_EXTRA_DRAW_ON + 3));
    }
    lda_y(s, RAM_BATTLE_ENEMY_FLAGS);
    and_a(s, 0x08);
    if (a8(s) != 0) {
        inc_abs(s, 0xAA63);
        lda_dp(s, 0x80);
        sta(s, 0xA002);
    }
    lda_y(s, RAM_BATTLE_ENEMY_FLAGS);
    and_a(s, 0x20);
    if (a8(s) != 0) {
        m_push16(s, c->x);
        lda_dp(s, 0x80);
        m_tax(s);
        inc_abs_x(s, 0xA006);
        c->x = m_pull16(s);
        m_set_nz16(s, c->x);
    }
    lda_y(s, RAM_BATTLE_ENEMY_FLAGS);
    and_a(s, 0x04);
    sta_y(s, 0xA08F);
    m_tdc(s);
    sta_y(s, (RAM_BATTLE_OBJECT_SHADOW + 3));
    static const uint32_t dsts[4] = { (RAM_BATTLE_CURSOR_OFFSET_X + 3), (RAM_BATTLE_CURSOR_OFFSET_Y + 3), (RAM_BATTLE_OBJECT_HALF_WIDTH + 3), (RAM_BATTLE_OBJECT_HALF_HEIGHT + 3) };
    for (uint16_t i = 0; i < 4; i++) {
        lda_long_x(s, 0xE4F605 + i);
        sta_y(s, dsts[i]);
    }
    lda_long_x(s, 0xE4F609);
    and_a(s, 0x07);
    sta_y(s, RAM_BATTLE_ORBIT_RADIUS_STEP);
    lda_long_x(s, 0xE4F609);
    for (int i = 0; i < 4; i++)
        lsr_a(s);
    sta_y(s, RAM_BATTLE_ORBIT_PERIOD);
    sta_y(s, RAM_BATTLE_ORBIT_COUNTDOWN);
    lsr_a(s);
    asl_a_n(s, 2);
    sta_dp(s, 0x82);
    lda_y(s, RAM_BATTLE_ORBIT_RADIUS_STEP);
    lsr_a(s);
    clc(s);
    adc(s, dp8(s, 0x82));
    m_tax(s);
    lda_long_x(s, 0xCCF544);
    sta_y(s, RAM_BATTLE_ENEMY_EFFECT_PERIOD);
    clc(s);
    adc(s, dp8(s, 0x80));
    sta_y(s, RAM_BATTLE_ENEMY_EFFECT_DELAY);
    lda_dp(s, 0x80);
    asl_a(s);
    m_tax(s);
    lda_dp(s, 0x8C);
    sta_x(s, RAM_BATTLE_ENEMY_NUMBERS);
    uint8_t high = lda_dp(s, 0x8D);
    sta_x(s, (RAM_BATTLE_ENEMY_NUMBERS + 1));
    if (high == 0) {
        static const uint8_t special[3] = { 0xD8, 0x94, 0xAD };
        uint8_t id = lda_x(s, RAM_BATTLE_ENEMY_NUMBERS);
        for (int i = 0; i < 3; i++) {
            cmp(s, special[i]);
            if (id == special[i]) {
                ldx_dp(s, 0x80);
                inc_abs_x(s, (RAM_BATTLE_OBJECT_Y + 0xF));
                break;
            }
        }
    }
    ldx_dp(s, 0x80);
    inc_abs_x(s, (RAM_BATTLE_SLOT_PRESENT + 3));
}

/* $CC:EA57-$CC:EC62: the eight enemy slots, in the order of $CC:F939 (record
 * offsets; a negative $29C5 means no enemy). When an enemy has flag $08 (the
 * last one's slot in $A002), the others' offsets from it go to $AA64 (X) and
 * $AA6C (Y). */
static void setup_enemies(Snes *s)
{
    Cpu *c = &s->cpu;
    stz_dp(s, 0x80);
    stz_dp(s, 0x81);
    do {
        ldx_dp(s, 0x80);
        lda_long_x(s, 0xCCF939);
        m_tax(s);
        if (!(lda_x(s, (RAM_BATTLE_ENEMY_PLACEMENT_ENEMY + 1)) & 0x80))
            setup_enemy(s);
        inc_dp(s, 0x80);
        lda_dp(s, 0x80);
        cmp(s, 0x08);
    } while (a8(s) != 0x08);

    /* $CC:EC31 */
    if (lda(s, 0xAA63) == 0)
        return;
    lda(s, 0xA002);
    m_tax(s);
    lda_x(s, (RAM_BATTLE_OBJECT_X + 0xF));
    sta_dp(s, 0x80);
    lda_x(s, (RAM_BATTLE_OBJECT_Y + 0xF));
    sta_dp(s, 0x81);
    m_tdc(s);
    m_tax(s);
    do {
        if (lda_x(s, (RAM_BATTLE_SLOT_PRESENT + 3)) != 0) {
            sec(s);
            lda_x(s, (RAM_BATTLE_OBJECT_X + 0xF));
            sbc(s, dp8(s, 0x80));
            sta_x(s, 0xAA64);
            sec(s);
            lda_x(s, (RAM_BATTLE_OBJECT_Y + 0xF));
            sbc(s, dp8(s, 0x81));
            sta_x(s, 0xAA6C);
        }
        m_inx(s);
        cpx(s, 0x0008);
    } while (c->x != 0x0008);
}

/* $CC:EC63-$CC:ECB1: look up each enemy number ($9853, 8 words) in the list
 * of 4-byte entries at $E1:DE00 (ended by a negative word): a match with
 * value 3 ORs 1 into $A0A7, value 4 sets $A113 = 4, others go to $99D2. */
static void scan_enemy_specials(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_tax(s);
    tay(s);
    sty_dp(s, 0x80);
    sty_dp(s, 0x82);
    m_rep(s, CPU_FLAG_M);
    for (;;) {
        uint16_t id = m_lda16(s, m_read16(s, 0xE1DE00 + c->x));
        if (id & 0x8000) {
            m_tdc(s);
            m_tax(s);
            m_iny(s);
            m_iny(s);
            cpy(s, 0x0010);
            if (c->y == 0x0010)
                break;
            continue;
        }
        uint16_t enemy = m_read16(s, m_abs(s, RAM_ABS(RAM_BATTLE_ENEMY_NUMBERS)) + c->y);
        m_cmp16(s, id, enemy);
        if (id == enemy) {
            uint16_t value = m_lda16(s, m_read16(s, 0xE1DE02 + c->x));
            m_cmp16(s, value, 0x0003);
            if (value == 0x0003) {
                set_dp16(s, 0x80, m_lda16(s, 0x0001));
            } else {
                m_cmp16(s, value, 0x0004);
                if (value == 0x0004)
                    set_dp16(s, 0x82, value);
                else
                    m_write16(s, m_abs(s, 0x99D2), value);
            }
        }
        for (int i = 0; i < 4; i++)
            m_inx(s);
    }
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    lda(s, 0xA0A7);
    ora_a(s, dp8(s, 0x80));
    sta(s, 0xA0A7);
    lda_dp(s, 0x82);
    sta(s, 0xA113);
}

/* $CC:ECB2-$CC:ED02: collect the distinct enemy numbers of the present
 * enemies ($96F8; $9FFA not negative) in $98FF (up to three words; later ones
 * go past the end, as the original does), counting each in $80/$82/$84, and
 * store the three counts' low bytes in $98CC-$98CE. */
static void count_enemy_kinds(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    tay(s);
    for (uint8_t cell = 0x80; cell <= 0x88; cell = (uint8_t)(cell + 2))
        sty_dp(s, cell);
    do {
        ldx_dp(s, 0x88);
        if (lda_x(s, (RAM_BATTLE_SLOT_PRESENT + 3)) != 0 && !(lda_x(s, (RAM_BATTLE_OBJECT_HIDDEN + 3)) & 0x80)) {
            m_rep(s, CPU_FLAG_M);
            m_tdc(s);
            m_tax(s);
            bool found = false;
            do {
                uint16_t id = m_lda16(s, m_read16(s, m_abs(s, RAM_ABS(RAM_BATTLE_ENEMY_NUMBERS)) + c->y));
                uint16_t have = absx16(s, RAM_BATTLE_ENEMY_KINDS);
                m_cmp16(s, id, have);
                if (id == have) {
                    found = true;
                    break;
                }
                m_inx(s);
                m_inx(s);
                cpx(s, 0x0006);
            } while (c->x != 0x0006);
            if (!found) {
                ldx_dp(s, 0x86);
                m_write16(s, m_abs(s, RAM_ABS(RAM_BATTLE_ENEMY_KINDS)) + c->x, c->a);
                inc16(s, m_dp(s, 0x86));
                inc16(s, m_dp(s, 0x86));
            }
            inc16(s, (uint16_t)(c->d + 0x80 + c->x));
            m_tdc(s);
            m_sep(s, CPU_FLAG_M);
        }
        inc_dp(s, 0x88);
        m_iny(s);
        m_iny(s);
        cpy(s, 0x0010);
    } while (c->y != 0x0010);
    for (uint16_t i = 0; i < 3; i++) {
        lda_dp(s, (uint8_t)(0x80 + 2 * i));
        sta(s, (uint16_t)(RAM_BATTLE_ENEMY_KIND_COUNTS + i));
    }
}

/* $CC:ED03-$CC:ED86: for each of the three enemy kinds collected ($9900
 * high byte not negative), convert its 11-byte name at $CC:6500 + number * 11
 * and store both 12-byte rows at $ACBC + kind * 24. */
static bool convert_enemy_names(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_tax(s);
    stx_dp(s, 0x80);
    do {
        ldx_dp(s, 0x80);
        if (!(lda_x(s, (RAM_BATTLE_ENEMY_KINDS + 1)) & 0x80)) {
            m_rep(s, CPU_FLAG_M);
            m_lda16(s, absx16(s, RAM_BATTLE_ENEMY_KINDS));
            c->a = m_asl16(s, c->a);
            c->a = m_asl16(s, c->a);
            set_dp16(s, 0x82, c->a);
            c->a = m_asl16(s, c->a);
            clc(s);
            c->a = m_adc16(s, c->a, dp16(s, 0x82));
            sec(s);
            c->a = m_sbc16(s, c->a, absx16(s, RAM_BATTLE_ENEMY_KINDS));
            m_tax(s);
            for (uint16_t i = 0; i < 12; i = (uint16_t)(i + 2))
                m_write16(s, m_abs(s, (uint16_t)(RAM_ABS(RAM_BATTLE_NAME_BUFFER) + i)), m_lda16(s, m_read16(s, 0xCC6500 + c->x + i)));
            m_tdc(s);
            m_write16(s, m_abs(s, RAM_ABS(RAM_BATTLE_NAME_BUFFER + 0xB)), c->a);
            m_sep(s, CPU_FLAG_M);
            if (!convert_name(s, 0xCCED50))
                return false;
            lda_dp(s, 0x80);
            asl_a_n(s, 2);
            sta_dp(s, 0x82);
            asl_a(s);
            clc(s);
            adc(s, dp8(s, 0x82));
            tay(s);
            store_name_rows(s, RAM_BATTLE_ENEMY_NAME_TILES, 0x000C);
        }
        inc_dp(s, 0x80);
        inc_dp(s, 0x80);
        lda_dp(s, 0x80);
        cmp(s, 0x06);
    } while (a8(s) != 0x06);
    return true;
}

// $CC:E2E4
/* Battle setup, first part (JSL from $C1:0023): reset the battle's slot and
 * flag tables, build the cell tables, the base battlefield grid ($7BFD) and
 * word buffers, convert the party's and members' names to tiles ($9453,
 * $9412; through $C1:0018 as call-outs), load palettes and party colors, the
 * members' entry lists ($94D0), party and enemy objects (positions, graphics
 * pointers, per-enemy data from $E4:F600) and the enemy kinds and their names
 * ($ACBC). Returns with RTL. */
void battle_setup_state(Snes *s)
{
    reset_battle_tables(s);
    fill_cell_tables(s);
    build_base_grid(s);
    reset_word_buffers(s);
    copy_name_records(s);
    if (!convert_names(s, RAM_BATTLE_CHARACTER_NAMES, RAM_BATTLE_CHARACTER_NAME_TILES, 0x07, 0xCCE5C2))
        return;
    read_party_slots(s);
    if (!convert_names(s, RAM_BATTLE_PARTY_NAMES, RAM_BATTLE_PARTY_NAME_TILES, 0x03, 0xCCE686))
        return;
    load_palettes(s);
    copy_party_colors(s);
    mark_member_records(s);
    build_member_lists(s);
    read_slot_extras(s);
    place_party_objects(s);
    load_party_graphics(s);
    copy_enemy_headers(s);
    setup_enemies(s);
    scan_enemy_specials(s);
    count_enemy_kinds(s);
    if (!convert_enemy_names(s))
        return;
    m_rtl(s);
}

/* TYA : CLC : ADC #$10 : TAY -- the next 16-byte row. */
static void next_row(Snes *s)
{
    tya(s);
    clc(s);
    adc(s, 0x10);
    tay(s);
}

/* LDA $80 : STA $11E3,Y : STA $11E7,Y : STA $11EB,Y : STA $11EF,Y */
static void store_row_value(Snes *s)
{
    lda_dp(s, 0x80);
    for (uint16_t i = 0; i < 16; i = (uint16_t)(i + 4))
        sta_y(s, (uint16_t)((RAM_BATTLE_OBJECT_SPRITE_ENTRIES_ATTRIBUTES + 12 * RAM_BATTLE_OBJECT_SPRITE_ENTRIES_STRIDE) + i));
}

/* $CC:ED87-$CC:EDA9: the 44 words at $11B0: $0000 and then a word from
 * $CC:FAF3. */
static void fill_11b0_table(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_tax(s);
    tay(s);
    do {
        m_tdc(s);
        sta_y(s, RAM_BATTLE_OBJECT_SPRITE_ENTRIES);
        m_iny(s);
        sta_y(s, RAM_BATTLE_OBJECT_SPRITE_ENTRIES);
        m_iny(s);
        for (int i = 0; i < 2; i++) {
            lda_long_x(s, 0xCCFAF3);
            sta_y(s, RAM_BATTLE_OBJECT_SPRITE_ENTRIES);
            m_iny(s);
            m_inx(s);
        }
        cpy(s, 0x00B0);
    } while (c->y != 0x00B0);
}

/* $CC:EDAA-$CC:EDE8: $AA in $0B24 + slot for each present object ($96F5),
 * and in the next bytes for its extra parts ($967C: 1, 2, or 5 for 3 or
 * more), skipping those slots. */
static void mark_object_parts(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_tax(s);
    do {
        if (lda_x(s, RAM_BATTLE_SLOT_PRESENT) != 0) {
            m_lda8(s, 0xAA);
            sta_x(s, RAM_BATTLE_OBJECT_SIZE_BITS);
            uint8_t parts = lda_x(s, RAM_BATTLE_OBJECT_KIND);
            sta_dp(s, 0x80);
            if (parts != 0) {
                m_inx(s);
                m_lda8(s, 0xAA);
                sta_x(s, RAM_BATTLE_OBJECT_SIZE_BITS);
                if (dec_dp(s, 0x80) != 0) {
                    m_inx(s);
                    m_lda8(s, 0xAA);
                    sta_x(s, RAM_BATTLE_OBJECT_SIZE_BITS);
                    if (dec_dp(s, 0x80) != 0) {
                        for (int i = 0; i < 3; i++) {
                            m_inx(s);
                            m_lda8(s, 0xAA);
                            sta_x(s, RAM_BATTLE_OBJECT_SIZE_BITS);
                        }
                    }
                }
            }
        }
        m_inx(s);
        cpx(s, 0x000B);
    } while (c->x != 0x000B);
}

/* $CC:EDE9-$CC:EE8B: for each present enemy ($96F8), $988B + $20 in the
 * four cells of its 16-byte row at $11E3 and of the rows for its extra parts
 * ($967F: 1, 2, or 5 for 3 or more), skipping those enemies' rows. */
static void fill_enemy_rows(Snes *s)
{
    Cpu *c = &s->cpu;
    m_tdc(s);
    m_tax(s);
    tay(s);
    do {
        if (lda_x(s, (RAM_BATTLE_SLOT_PRESENT + 3)) != 0) {
            lda_x(s, (RAM_BATTLE_OBJECT_PALETTE_INITIAL + 3));
            clc(s);
            adc(s, 0x20);
            sta_dp(s, 0x80);
            for (uint16_t i = 0; i < 16; i = (uint16_t)(i + 4))
                sta_y(s, (uint16_t)((RAM_BATTLE_OBJECT_SPRITE_ENTRIES_ATTRIBUTES + 12 * RAM_BATTLE_OBJECT_SPRITE_ENTRIES_STRIDE) + i));
            uint8_t parts = lda_x(s, (RAM_BATTLE_OBJECT_KIND + 3));
            sta_dp(s, 0x82);
            if (parts != 0) {
                next_row(s);
                store_row_value(s);
                m_inx(s);
                if (dec_dp(s, 0x82) != 0) {
                    next_row(s);
                    store_row_value(s);
                    m_inx(s);
                    if (dec_dp(s, 0x82) != 0) {
                        for (int i = 0; i < 3; i++) {
                            next_row(s);
                            store_row_value(s);
                        }
                        for (int i = 0; i < 3; i++)
                            m_inx(s);
                    }
                }
            }
        }
        next_row(s);
        m_inx(s);
        cpx(s, 0x0008);
    } while (c->x != 0x0008);
}

/* $CC:EE8C-$CC:EECF: the tile block at $2F00: 16 rows of 16 bytes from
 * $D1:4C80 each followed by 16 zeros, then $800 bytes from $D1:3900. */
static void build_tile_block(Snes *s)
{
    m_tdc(s);
    tay(s);
    m_tax(s);
    m_lda8(s, 0x10);
    sta_dp(s, 0x80);
    do {
        m_lda8(s, 0x10);
        sta_dp(s, 0x81);
        do {
            lda_long_x(s, 0xD14C80);
            sta_y(s, (RAM_BATTLE_DECODED_TILES + 0x200));
            m_inx(s);
            m_iny(s);
        } while (dec_dp(s, 0x81) != 0);
        m_lda8(s, 0x10);
        sta_dp(s, 0x81);
        m_tdc(s);
        do {
            sta_y(s, (RAM_BATTLE_DECODED_TILES + 0x200));
            m_iny(s);
        } while (dec_dp(s, 0x81) != 0);
    } while (dec_dp(s, 0x80) != 0);
    m_tdc(s);
    m_tax(s);
    m_lda8(s, 0x40);
    sta_dp(s, 0x80);
    do {
        m_lda8(s, 0x20);
        sta_dp(s, 0x81);
        do {
            lda_long_x(s, 0xD13900);
            sta_y(s, (RAM_BATTLE_DECODED_TILES + 0x200));
            m_inx(s);
            m_iny(s);
        } while (dec_dp(s, 0x81) != 0);
    } while (dec_dp(s, 0x80) != 0);
}

/* $CC:EED0-$CC:EF33: OAM buffer entries 0-3 at (0, 0) with tile 0 and
 * attributes $32, $0900 = $FF; 16 bytes from $D1:4D80 to $999F, and 6 bytes
 * each from $D1:38F2 and $D1:38FA to the palette buffer at $235A and $237A. */
static void reset_oam_and_colors(Snes *s)
{
    for (uint16_t i = 0; i < 16; i = (uint16_t)(i + 4))
        stz(s, (uint16_t)(RAM_OAM_BUFFER_TILE + i));
    m_lda8(s, 0x32);
    for (uint16_t i = 0; i < 16; i = (uint16_t)(i + 4))
        sta(s, (uint16_t)(RAM_OAM_BUFFER_ATTRIBUTES + i));
    m_lda8(s, 0xFF);
    sta(s, RAM_OAM_HIGH_TABLE);
    for (uint16_t i = 0; i < 16; i = (uint16_t)(i + 4))
        stz(s, (uint16_t)(RAM_OAM_BUFFER_X + i));
    for (uint16_t i = 0; i < 16; i = (uint16_t)(i + 4))
        stz(s, (uint16_t)(RAM_OAM_BUFFER_Y + i));
    copy_rom_up(s, 0xD14D80, 0x999F, 0x0010);
    copy_rom_up(s, 0xD138F2, (RAM_PALETTE_WORKING + 0x15A), 0x0006);
    copy_rom_up(s, 0xD138FA, (RAM_PALETTE_WORKING + 0x17A), 0x0006);
}

/* $CC:EF34-$CC:F05A: for each present object ($96F5, slots 0-10), its two
 * graphics uploads' descriptors (source $A654/$A66A, VRAM address
 * $A680/$A696, size $A6AC/$A6C2, words at slot * 2, as battle_upload_vram
 * reads them): fixed for graphics kind 2 ($967C; one set for slot 3, another
 * for the rest), otherwise sizes by kind ($CC:F9DF, $CC:F9E9) and addresses
 * and sources by slot ($CC:F9B3, $CC:F9C9, $CC:F987, $CC:F99D). Then count
 * $9F34 when a party member's stat block byte $57 ($5E57, $5ED7, $5F57) is
 * $A4 or $B8, and count $98CA. */
static void set_upload_descriptors(Snes *s)
{
    Cpu *c = &s->cpu;
    static const uint32_t cells[12] = { RAM_BATTLE_UPLOAD_VRAM_ADDRESS, (RAM_BATTLE_UPLOAD_VRAM_ADDRESS + 1), (RAM_BATTLE_UPLOAD_VRAM_ADDRESS + 0x16), (RAM_BATTLE_UPLOAD_VRAM_ADDRESS + 0x17), RAM_BATTLE_UPLOAD_SIZE, (RAM_BATTLE_UPLOAD_SIZE + 1),
                                        (RAM_BATTLE_UPLOAD_SIZE + 0x16), (RAM_BATTLE_UPLOAD_SIZE + 0x17), RAM_BATTLE_UPLOAD_SOURCE, (RAM_BATTLE_UPLOAD_SOURCE + 1), (RAM_BATTLE_UPLOAD_SOURCE + 0x16), (RAM_BATTLE_UPLOAD_SOURCE + 0x17) };
    m_tdc(s);
    m_tax(s);
    stx_dp(s, 0x80);
    do {
        ldx_dp(s, 0x80);
        if (lda_x(s, RAM_BATTLE_SLOT_PRESENT) != 0) {
            lda_dp(s, 0x80);
            asl_a(s);
            tay(s);
            uint8_t kind = lda_x(s, RAM_BATTLE_OBJECT_KIND);
            cmp(s, 0x02);
            if (kind == 0x02) {
                static const uint8_t slot3[12] = { 0x00, 0x04, 0x00, 0x07, 0x00, 0x05,
                                                   0x00, 0x01, 0xAD, 0x6B, 0xAD, 0x71 };
                static const uint8_t other[12] = { 0x80, 0x06, 0x80, 0x07, 0x00, 0x01,
                                                   0x00, 0x05, 0xAD, 0x70, 0xAD, 0x72 };
                m_tyx(s);
                cpy(s, 0x0006);
                const uint8_t *v = c->y == 0x0006 ? slot3 : other;
                for (int i = 0; i < 12; i++) {
                    m_lda8(s, v[i]);
                    sta_y(s, cells[i]);
                }
            } else {
                static const uint32_t by_kind[4] = { 0xCCF9DF, 0xCCF9E0, 0xCCF9E9, 0xCCF9EA };
                static const uint32_t by_slot[8] = { 0xCCF9B3, 0xCCF9B4, 0xCCF9C9, 0xCCF9CA,
                                                     0xCCF987, 0xCCF988, 0xCCF99D, 0xCCF99E };
                asl_a(s);
                m_tax(s);
                for (int i = 0; i < 4; i++) {
                    lda_long_x(s, by_kind[i]);
                    sta_y(s, cells[4 + i]);
                }
                m_tyx(s);
                for (int i = 0; i < 8; i++) {
                    lda_long_x(s, by_slot[i]);
                    sta_y(s, cells[i < 4 ? i : i + 4]);
                }
            }
        }
        inc_dp(s, 0x80);
        lda_dp(s, 0x80);
        cmp(s, 0x0B);
    } while (a8(s) != 0x0B);

    /* $CC:F034 */
    static const uint32_t members[3] = { (RAM_BATTLE_STATS + 0x28), (RAM_BATTLE_STATS + 1 * RAM_BATTLE_STATS_STRIDE + 0x28), (RAM_BATTLE_STATS + 2 * RAM_BATTLE_STATS_STRIDE + 0x28) };
    bool mark = false;
    for (int i = 0; i < 3 && !mark; i++) {
        uint8_t v = lda(s, members[i]);
        cmp(s, 0xA4);
        if (v == 0xA4) {
            mark = true;
            break;
        }
        cmp(s, 0xB8);
        mark = v == 0xB8;
    }
    if (mark)
        inc_abs(s, 0x9F34);
    inc_abs(s, RAM_BATTLE_GRAPHICS_LOAD_STEP);
}

// $CC:ED87
/* Battle setup, second part (JSL from $C1:002D): the $11B0 table, part marks
 * at $0B24, the enemy rows at $11E3, the tile block at $2F00, OAM entries 0-3
 * and colors, and each object's graphics upload descriptors, then $98CA is
 * counted. Stops at the wait for interrupt code to count $98CA to 4
 * ($CC:F05B, an idle entry). */
void battle_setup_graphics(Snes *s)
{
    fill_11b0_table(s);
    mark_object_parts(s);
    fill_enemy_rows(s);
    build_tile_block(s);
    reset_oam_and_colors(s);
    set_upload_descriptors(s);
    s->cpu.pc = 0xF05B;
}
