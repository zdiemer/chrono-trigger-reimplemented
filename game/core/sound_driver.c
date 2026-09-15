/* The CPU side of the sound driver interface in bank C7.
 *
 * Callers put a command in $1E00 and its parameters in $1E01-$1E03 and JSL to
 * $C7:0004. The routine runs with the data bank at $00 and the direct page at
 * $1E00, so the cells below are named by their direct page offset ($00 =
 * $1E00). It talks to the SPC700 through the APU ports $2140-$2143: it writes
 * parameters to ports 1-3 and a value to port 0, then waits in a loop until
 * the SPC700 echoes that value on port 0. Commands $18-$2F and $80-$FF are
 * sent that way directly; commands $10, $11, $14 and $15 send the song command
 * and then upload data blocks three bytes at a time, each write to port 0 a
 * counter kept in $84 (the `JSR $09DA` echo waits).
 *
 * The wait loops depend on the SPC700 running while the CPU polls, so they
 * stay in the original: the native routines stop with PC at a loop's first
 * instruction (or call out to the subroutines $09DA and $09EA, which contain
 * one), and further entries pick up where a loop exits. The four loops that
 * run most are idle entries, taking the loop's own time per pass in ON mode.
 * Rare paths no movie reaches, where
 * the protocol differs (retries after the SPC700 answers a block header
 * with the wrong value, block data crossing a bank boundary, command $71),
 * stop at their first instruction and are left to the original. */
#include "core/sound_driver.h"

#include "port/machine.h"

#define M8_X16 CPU_FLAG_M
#define M16_X16 0

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }
static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }

/* dp,X: the direct page cell plus X, wrapping within bank 0. */
static uint32_t dpx(Snes *s, uint8_t cell, uint16_t offset) { return (uint16_t)(s->cpu.d + cell + s->cpu.x + offset); }
static uint8_t dpx8(Snes *s, uint8_t cell) { return m_read8(s, dpx(s, cell, 0)); }
static void set_dpx8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, dpx(s, cell, 0), v); }
static uint16_t dpx16(Snes *s, uint8_t cell)
{
    return (uint16_t)(m_read8(s, dpx(s, cell, 0)) | m_read8(s, dpx(s, cell, 1)) << 8);
}
static void set_dpx16(Snes *s, uint8_t cell, uint16_t v)
{
    m_write8(s, dpx(s, cell, 0), v & 0xFF);
    m_write8(s, dpx(s, cell, 1), v >> 8);
}

/* abs,X and abs,Y in the data bank, carrying into the next bank. */
static uint32_t absx(Snes *s, uint16_t addr) { return m_abs(s, addr) + s->cpu.x; }
static uint32_t absy(Snes *s, uint16_t addr) { return m_abs(s, addr) + s->cpu.y; }

/* [dp],Y: the 24-bit pointer in the cell plus Y. */
static uint32_t long_y(Snes *s, uint8_t cell)
{
    uint32_t p = (uint32_t)dp8(s, cell) | (uint32_t)dp8(s, (uint8_t)(cell + 1)) << 8 |
                 (uint32_t)dp8(s, (uint8_t)(cell + 2)) << 16;
    return (p + s->cpu.y) & 0xFFFFFF;
}

/* (dp): the 16-bit pointer in the cell, in the data bank. */
static uint32_t ind(Snes *s, uint8_t cell) { return m_abs(s, dp16(s, cell)); }

static void port_write(Snes *s, uint16_t reg, uint8_t v) { m_write8(s, m_abs(s, reg), v); }
static uint8_t port_read(Snes *s, uint16_t reg) { return m_read8(s, m_abs(s, reg)); }

static void clc(Snes *s) { m_set_flag(s, CPU_FLAG_C, false); }
static void inx(Snes *s) { m_set_index(s, &s->cpu.x, (uint16_t)(s->cpu.x + 1)); }
static void iny(Snes *s) { m_set_index(s, &s->cpu.y, (uint16_t)(s->cpu.y + 1)); }
static void dex(Snes *s) { m_set_index(s, &s->cpu.x, (uint16_t)(s->cpu.x - 1)); }
static void dey(Snes *s) { m_set_index(s, &s->cpu.y, (uint16_t)(s->cpu.y - 1)); }

/* INC dp at 8 and 16 bits. */
static void inc_dp8(Snes *s, uint8_t cell)
{
    uint8_t v = (uint8_t)(dp8(s, cell) + 1);
    set_dp8(s, cell, v);
    m_set_nz8(s, v);
}

static void inc_dp16(Snes *s, uint8_t cell)
{
    uint16_t v = (uint16_t)(dp16(s, cell) + 1);
    set_dp16(s, cell, v);
    m_set_nz16(s, v);
}

/* ROL A, 8-bit. */
static uint8_t rol_a8(Snes *s)
{
    uint8_t a = (uint8_t)s->cpu.a;
    uint8_t r = (uint8_t)(a << 1 | (m_flag(s, CPU_FLAG_C) ? 1 : 0));
    m_set_flag(s, CPU_FLAG_C, a & 0x80);
    return m_lda8(s, r);
}

static uint16_t pull16_nz(Snes *s)
{
    uint16_t v = m_pull16(s);
    m_set_nz16(s, v);
    return v;
}

static void plp(Snes *s)
{
    s->cpu.p = m_pull8(s);
    if (s->cpu.p & CPU_FLAG_X) {
        s->cpu.x &= 0xFF;
        s->cpu.y &= 0xFF;
    }
}

/* JSR from $C7:site to $C7:target; false if the callee came back with other
 * register widths than `widths` (the port then stops at the return address). */
static bool jsr(Snes *s, uint16_t site, uint16_t target, uint8_t widths)
{
    m_jsr(s, 0xC70000 | site, 0xC70000 | target);
    return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) == widths;
}

/* LDA $84 : JSR $09DA (the call at `site`) [: STA $84] -- write the upload
 * counter to port 0 and wait for its echo; $09DA returns the next counter. */
static bool send_counter(Snes *s, uint16_t site, bool keep)
{
    m_lda8(s, dp8(s, 0x84));
    if (!jsr(s, site, 0x09DA, M8_X16))
        return false;
    if (keep)
        set_dp8(s, 0x84, (uint8_t)s->cpu.a);
    return true;
}

/* LDA [$12],Y : STA $2141 : INY, and likewise for $2142 and $2143, then the
 * counter (`site`: the JSR $09DA). */
static bool send_three_bytes(Snes *s, uint16_t site)
{
    for (uint16_t reg = 0x2141; reg <= 0x2143; reg++) {
        port_write(s, reg, m_lda8(s, m_read8(s, long_y(s, 0x12))));
        iny(s);
    }
    return send_counter(s, site, true);
}

/* LDA [$12],Y : STA cell : INY : BNE : INC $14 */
static uint8_t read_header_byte(Snes *s)
{
    uint8_t v = m_lda8(s, m_read8(s, long_y(s, 0x12)));
    iny(s);
    if (s->cpu.y == 0)
        inc_dp8(s, 0x14);
    return v;
}

/* SEP #$20 : LDA #hi : STA $2143 : LDA #lo : STA $2142 : LDA #cmd : STA $2141,
 * then the counter (the JSR $09DA at `site`) and LDA #cmd : CMP $2141. Returns
 * false if the port stops: after a call-out with other widths, or at the JSR
 * $09EA (site + $0C) that resends when the SPC700 answered something else. */
static bool send_block_header(Snes *s, uint16_t site, uint8_t hi, uint8_t lo, uint8_t cmd)
{
    m_sep(s, CPU_FLAG_M);
    port_write(s, 0x2143, m_lda8(s, hi));
    port_write(s, 0x2142, m_lda8(s, lo));
    port_write(s, 0x2141, m_lda8(s, cmd));
    if (!send_counter(s, site, true))
        return false;
    uint8_t want = m_lda8(s, cmd);
    uint8_t got = port_read(s, 0x2141);
    m_cmp8(s, want, got);
    if (want != got) {
        s->cpu.pc = (uint16_t)(site + 0x0C);
        return false;
    }
    return true;
}

/* LDA #$E0 : STA $84 */
static void reset_counter(Snes *s) { set_dp8(s, 0x84, m_lda8(s, 0xE0)); }

// $C7:0192
/* The driver's common exit: clear the command in $00, restore Y, X, A, P, the
 * direct page and the data bank, and return to the JSL. */
static void driver_return(Snes *s)
{
    Cpu *c = &s->cpu;
    m_sep(s, CPU_FLAG_M);
    set_dp8(s, 0x00, 0x00);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    c->y = pull16_nz(s);
    c->x = pull16_nz(s);
    c->a = pull16_nz(s);
    plp(s);
    c->d = pull16_nz(s);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_rtl(s);
}

// $C7:0A12
/* (JSR at `site`, inlined.) From the first of the 32 cells at $63 (every
 * other one) that is zero or at least $F2: if it is at least $F2, clear $20,
 * $40 and $62 from that cell's slot up to slot $1F. */
static void clear_slots_from_f2(Snes *s, uint16_t site)
{
    Cpu *c = &s->cpu;
    m_push16(s, (uint16_t)(site + 2));
    m_push8(s, c->p);
    m_sep(s, CPU_FLAG_M);
    m_ldx(s, 0x0000);
    bool clear = false;
    do {
        uint8_t v = m_lda8(s, dpx8(s, 0x63));
        if (v == 0)
            break;
        uint8_t limit = dp8(s, 0xF2);
        m_cmp8(s, v, limit);
        if (v >= limit) {
            clear = true;
            break;
        }
        inx(s);
        inx(s);
        m_cmp16(s, c->x, 0x0020);
    } while (c->x != 0x0020);
    if (clear) {
        m_lda8(s, 0x00);
        do {
            set_dpx8(s, 0x20, (uint8_t)c->a);
            set_dpx8(s, 0x40, (uint8_t)c->a);
            set_dpx8(s, 0x62, (uint8_t)c->a);
            inx(s);
            m_cmp16(s, c->x, 0x0020);
        } while (c->x != 0x0020);
    }
    plp(s);
    m_pull16(s);
}

// $C7:09FD
/* (JSR at `site`, inlined.) Store A in $F0 and $F1 - A * 8 in $F2, then
 * clear slots from $F2 ($C7:0A12). */
static void set_f0(Snes *s, uint16_t site)
{
    Cpu *c = &s->cpu;
    m_push16(s, (uint16_t)(site + 2));
    m_push8(s, c->p);
    m_sep(s, CPU_FLAG_M);
    set_dp8(s, 0xF0, (uint8_t)c->a);
    for (int i = 0; i < 3; i++)
        m_set_a8(s, m_asl8(s, (uint8_t)c->a));
    m_lda8(s, (uint8_t)(c->a ^ 0xFF));
    m_lda8(s, (uint8_t)(c->a + 1));
    clc(s);
    m_set_a8(s, m_adc8(s, (uint8_t)c->a, 0xF1));
    set_dp8(s, 0xF2, (uint8_t)c->a);
    clear_slots_from_f2(s, 0x0A0D);
    plp(s);
    m_pull16(s);
}

// $C7:061C
/* Write $03, $02 and $01 to ports 3, 2 and 1 and the command $00 to port 0,
 * with X = $0400 for the echo wait at $C7:0635, where the port stops. */
static void send_command(Snes *s)
{
    m_sep(s, CPU_FLAG_M);
    port_write(s, 0x2143, m_lda8(s, dp8(s, 0x03)));
    port_write(s, 0x2142, m_lda8(s, dp8(s, 0x02)));
    port_write(s, 0x2141, m_lda8(s, dp8(s, 0x01)));
    m_lda8(s, dp8(s, 0x00));
    m_ldx(s, 0x0400);
    port_write(s, 0x2140, (uint8_t)s->cpu.a);
    s->cpu.pc = 0x0635;
}

// $C7:0734
/* (JSR at $C7:01AB, inlined.) Set bit 2 of the command in $00 when it is $14
 * or above, or when the song $01 is in the list at $C7:0AD8 (ended by a
 * negative byte). */
static void mark_listed_song(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push16(s, 0x01AD);
    m_push8(s, c->p);
    m_sep(s, CPU_FLAG_M);
    uint8_t command = m_lda8(s, dp8(s, 0x00));
    m_cmp8(s, command, 0x14);
    bool set = command >= 0x14;
    if (!set) {
        m_ldx(s, 0x0000);
        for (;;) {
            uint8_t v = m_lda8(s, m_read8(s, 0xC70AD8 + c->x));
            if (v & 0x80)
                break;
            uint8_t song = dp8(s, 0x01);
            m_cmp8(s, v, song);
            if (v == song) {
                set = true;
                break;
            }
            inx(s);
        }
    }
    if (set)
        set_dp8(s, 0x00, m_lda8(s, (uint8_t)(0x04 | dp8(s, 0x00))));
    plp(s);
    m_pull16(s);
}

// $C7:01A1
/* Commands $10, $11, $14 and $15: play song $01. Unless it is out of range
 * ($C7:0AE9), send the song command ($C7:0A39, a call-out), then work out
 * which of the song's 16-bit block ids ($C7:0E11, 16 per song) are loaded in
 * the 16 slots at $20 (addresses $60, lengths $40) and upload the missing
 * ones ($C7:0655), then send the tables built at $1F80 and from $C7:0C20 and
 * $C7:0C9C, the block at $C7:0D18 indexed by the song, and 12 three-byte
 * entries from $C7:1871. Stops at the final echo wait ($C7:060C). */
static void play_song(Snes *s)
{
    Cpu *c = &s->cpu;
    m_sep(s, CPU_FLAG_M);
    uint8_t song = m_lda8(s, dp8(s, 0x01));
    uint8_t count = m_read8(s, 0xC70AE9);
    m_cmp8(s, song, count);
    if (song >= count) {
        driver_return(s);
        return;
    }
    mark_listed_song(s);
    if (!jsr(s, 0x01AE, 0x0A39, M8_X16))
        return;

    /* $C7:01B1: clear the two lists at $1F00 and $1F20 */
    set_dp8(s, 0x05, m_lda8(s, dp8(s, 0x01)));
    m_rep(s, CPU_FLAG_M);
    m_ldx(s, 0x0020);
    do {
        m_write16(s, absx(s, 0x1EFE), 0x0000);
        m_write16(s, absx(s, 0x1F1E), 0x0000);
        dex(s);
        dex(s);
    } while (c->x != 0);
    /* the last nonzero slot id at $1E20 */
    m_ldy(s, 0x0020);
    do {
        if (m_lda16(s, m_read16(s, absy(s, 0x1E1E))) != 0)
            break;
        dey(s);
        dey(s);
    } while (c->y != 0);
    set_dp16(s, 0x0C, c->y);
    set_dp16(s, 0x12, m_lda16(s, 0x1F00));
    set_dp16(s, 0x14, m_lda16(s, 0x1F20));
    m_lda16(s, dp16(s, 0x04));
    m_lda16(s, c->a & 0xFF00);
    for (int i = 0; i < 3; i++)
        c->a = m_lsr16(s, c->a);
    m_tax(s);
    c->a = m_adc16(s, c->a, 0x0020);
    set_dp16(s, 0x0E, c->a);
    /* $C7:01EA: the song's ids go to $1F00; those not loaded also to $1F20 */
    for (;;) {
        uint16_t id = m_lda16(s, m_read16(s, 0xC70E11 + c->x));
        if (id == 0)
            break;
        m_write16(s, ind(s, 0x12), id);
        inc_dp16(s, 0x12);
        inc_dp16(s, 0x12);
        bool loaded = false;
        if (m_ldy(s, dp16(s, 0x0C)) != 0) {
            do {
                uint16_t v = m_read16(s, absy(s, 0x1E1E));
                m_cmp16(s, id, v);
                if (id == v) {
                    loaded = true;
                    break;
                }
                dey(s);
                dey(s);
            } while (c->y != 0);
        }
        if (!loaded) {
            m_write16(s, ind(s, 0x14), id);
            inc_dp16(s, 0x14);
            inc_dp16(s, 0x14);
        }
        inx(s);
        inx(s);
        uint16_t end = dp16(s, 0x0E);
        m_cmp16(s, c->x, end);
        if (c->x == end)
            break;
    }
    /* $C7:020F */
    if (m_lda16(s, m_read16(s, m_abs(s, 0x1F20))) == 0)
        goto send_tables;

    /* $C7:0217: forget slot ids at $20 the song doesn't use */
    m_rep(s, CPU_FLAG_M);
    m_ldx(s, 0x0000);
    do {
        uint16_t id = m_lda16(s, dpx16(s, 0x20));
        if (id == 0)
            break;
        m_ldy(s, 0x0000);
        bool used = false;
        do {
            uint16_t v = m_read16(s, absy(s, 0x1F00));
            m_cmp16(s, id, v);
            if (id == v) {
                used = true;
                break;
            }
            iny(s);
            iny(s);
            m_cmp16(s, c->y, 0x0020);
        } while (c->y != 0x0020);
        if (!used)
            set_dpx16(s, 0x20, 0x0000);
        inx(s);
        inx(s);
        m_cmp16(s, c->x, 0x0020);
    } while (c->x != 0x0020);
    /* $C7:0238: the first free slot, and the last used one */
    m_ldx(s, 0x0000);
    do {
        if (m_lda16(s, dpx16(s, 0x20)) == 0)
            break;
        inx(s);
        inx(s);
        m_cmp16(s, c->x, 0x0020);
    } while (c->x != 0x0020);
    set_dp16(s, 0x0C, c->x);
    m_ldx(s, 0x0020);
    do {
        if (m_lda16(s, dpx16(s, 0x1E)) != 0)
            break;
        dex(s);
        dex(s);
    } while (c->x != 0);
    {
        uint16_t first_free = dp16(s, 0x0C);
        m_cmp16(s, c->x, first_free);
        if (c->x == first_free)
            goto load_blocks;
    }

    /* $C7:025A: move the slots still used down into the free ones, telling the
     * SPC700 each block's old and new address (command 7) */
    m_sep(s, CPU_FLAG_M);
    port_write(s, 0x2141, m_lda8(s, 0x07));
    m_ldy(s, 0x0000);
    set_dp16(s, 0x0C, c->y);
    do {
        m_rep(s, CPU_FLAG_M);
        if (m_lda16(s, m_read16(s, absy(s, 0x1E20))) == 0) {
            m_tyx(s);
            bool found = false;
            for (;;) {
                inx(s);
                inx(s);
                m_cmp16(s, c->x, 0x0020);
                if (c->x == 0x0020)
                    break;
                if (m_lda16(s, dpx16(s, 0x20)) != 0) {
                    found = true;
                    break;
                }
            }
            if (!found)
                goto counter_reset;
            /* $C7:027E */
            m_lda16(s, dpx16(s, 0x60));
            m_sep(s, CPU_FLAG_M);
            port_write(s, 0x2142, (uint8_t)c->a);
            m_xba(s);
            port_write(s, 0x2143, (uint8_t)c->a);
            if (!send_counter(s, 0x028B, true))
                return;
            if (m_lda8(s, dp8(s, 0x0C)) == 0) {
                inc_dp8(s, 0x0C);
                uint8_t want = m_lda8(s, 0x07);
                uint8_t got = port_read(s, 0x2141);
                m_cmp8(s, want, got);
                if (want != got) {
                    c->pc = 0x02E4;
                    return;
                }
            }
            /* $C7:029D */
            m_rep(s, CPU_FLAG_M);
            m_write16(s, absy(s, 0x1E20), m_lda16(s, dpx16(s, 0x20)));
            set_dpx16(s, 0x20, 0x0000);
            m_lda16(s, m_read16(s, absy(s, 0x1E60)));
            m_sep(s, CPU_FLAG_M);
            port_write(s, 0x2142, (uint8_t)c->a);
            m_xba(s);
            port_write(s, 0x2143, (uint8_t)c->a);
            m_xba(s);
            m_rep(s, CPU_FLAG_M);
            clc(s);
            c->a = m_adc16(s, c->a, dpx16(s, 0x40));
            m_write16(s, absy(s, 0x1E62), c->a);
            m_sep(s, CPU_FLAG_M);
            if (!send_counter(s, 0x02BF, true))
                return;
            m_rep(s, CPU_FLAG_M);
            m_write16(s, absy(s, 0x1E40), m_lda16(s, dpx16(s, 0x40)));
            m_sep(s, CPU_FLAG_M);
            port_write(s, 0x2142, (uint8_t)c->a);
            m_xba(s);
            port_write(s, 0x2143, (uint8_t)c->a);
            if (!send_counter(s, 0x02D6, true))
                return;
        }
        /* $C7:02DB */
        iny(s);
        iny(s);
        m_cmp16(s, c->y, 0x0020);
    } while (c->y != 0x0020);

counter_reset:
    /* $C7:0320 */
    m_sep(s, CPU_FLAG_M);
    reset_counter(s);

load_blocks:
    /* $C7:0326: after the last used slot, load the blocks in $1F20 */
    m_sep(s, CPU_FLAG_M);
    set_dp8(s, 0x0A, 0x00);
    m_ldx(s, 0x0000);
    {
        bool full = true;
        do {
            if (m_lda8(s, dpx8(s, 0x20)) == 0) {
                full = false;
                break;
            }
            inx(s);
            inx(s);
            m_cmp16(s, c->x, 0x0020);
        } while (c->x != 0x0020);
        if (full) {
            m_lda8(s, 0x00);
            do {
                set_dpx8(s, 0x1F, (uint8_t)c->a);
                dex(s);
            } while (c->x != 0);
        }
    }
    m_ldy(s, 0x0000);
    for (;;) {
        /* $C7:0342 */
        m_rep(s, CPU_FLAG_M);
        uint16_t id = m_lda16(s, m_read16(s, absy(s, 0x1F20)));
        if (id == 0) {
            /* $C7:0361: the next block's address in $F6; clear the rest */
            m_rep(s, CPU_FLAG_M);
            set_dp16(s, 0xF6, m_lda16(s, dpx16(s, 0x60)));
            do {
                m_lda16(s, 0x0000);
                set_dpx16(s, 0x62, c->a);
                set_dpx16(s, 0x40, c->a);
                inx(s);
                inx(s);
                m_cmp16(s, c->x, 0x0020);
            } while (c->x != 0x0020);
            break;
        }
        set_dpx16(s, 0x20, id);
        set_dp16(s, 0x1C, c->x);
        set_dp16(s, 0x1E, c->y);
        if (!jsr(s, 0x034F, 0x0655, M16_X16))
            return;
        m_ldx(s, dp16(s, 0x1C));
        m_ldy(s, dp16(s, 0x1E));
        iny(s);
        iny(s);
        inx(s);
        inx(s);
        m_cmp16(s, c->y, 0x0020);
        if (c->y == 0x0020)
            break;
    }
    /* $C7:0375 */
    m_sep(s, CPU_FLAG_M);
    reset_counter(s);

send_tables:
    /* $C7:037B: for each of the song's ids, its slot's address and address
     * plus the id's entry at $C7:0BA4 (or two zero words) at $1F80 */
    m_rep(s, CPU_FLAG_M);
    set_dp16(s, 0x12, m_lda16(s, 0x1F80));
    m_ldx(s, 0x0000);
    do {
        uint16_t id = m_lda16(s, m_read16(s, absx(s, 0x1F00)));
        if (id == 0)
            break;
        m_ldy(s, 0x0000);
        bool loaded = false;
        do {
            uint16_t v = m_read16(s, absy(s, 0x1E20));
            m_cmp16(s, id, v);
            if (id == v) {
                loaded = true;
                break;
            }
            iny(s);
            iny(s);
            m_cmp16(s, c->y, 0x0040);
        } while (c->y != 0x0040);
        if (loaded) {
            m_push16(s, c->x);
            m_lda16(s, (uint16_t)(c->a - 1));
            c->a = m_asl16(s, c->a);
            m_tax(s);
            m_lda16(s, m_read16(s, absy(s, 0x1E60)));
            m_write16(s, ind(s, 0x12), c->a);
            inc_dp16(s, 0x12);
            inc_dp16(s, 0x12);
            clc(s);
            c->a = m_adc16(s, c->a, m_read16(s, 0xC70BA4 + c->x));
            m_write16(s, ind(s, 0x12), c->a);
            c->x = pull16_nz(s);
        } else {
            m_lda16(s, 0x0000);
            m_write16(s, ind(s, 0x12), c->a);
            inc_dp16(s, 0x12);
            inc_dp16(s, 0x12);
            m_write16(s, ind(s, 0x12), c->a);
        }
        inc_dp16(s, 0x12);
        inc_dp16(s, 0x12);
        inx(s);
        inx(s);
        m_cmp16(s, c->x, 0x0020);
    } while (c->x != 0x0020);

    /* $C7:03C8: the $40 bytes at $1F80 to SPC700 address $1E80 */
    if (!send_block_header(s, 0x03DB, 0x1E, 0x80, 0x02))
        return;
    m_ldx(s, 0x0000);
    do {
        port_write(s, 0x2141, m_lda8(s, m_read8(s, absx(s, 0x1F80))));
        port_write(s, 0x2142, m_lda8(s, m_read8(s, absx(s, 0x1F81))));
        if (!send_counter(s, 0x03FD, true))
            return;
        inx(s);
        inx(s);
        m_cmp16(s, c->x, 0x0040);
    } while (c->x != 0x0040);
    reset_counter(s);

    /* $C7:040D and $C7:045B: each id's word from $C7:0C20 to $1F40, then from
     * $C7:0C9C to $1FC0 */
    static const struct { uint16_t header_site, table, site; uint8_t lo; } tables[] = {
        { 0x0420, 0x0C20, 0x044B, 0x40 },
        { 0x046E, 0x0C9C, 0x0499, 0xC0 },
    };
    for (int t = 0; t < 2; t++) {
        if (!send_block_header(s, tables[t].header_site, 0x1F, tables[t].lo, 0x02))
            return;
        m_ldy(s, 0x0000);
        do {
            m_rep(s, CPU_FLAG_M);
            m_lda16(s, m_read16(s, absy(s, 0x1F00)));
            m_lda16(s, (uint16_t)(c->a - 1));
            c->a = m_asl16(s, c->a);
            m_tax(s);
            m_lda16(s, m_read16(s, 0xC70000 + tables[t].table + c->x));
            m_sep(s, CPU_FLAG_M);
            port_write(s, 0x2141, (uint8_t)c->a);
            m_xba(s);
            port_write(s, 0x2142, (uint8_t)c->a);
            if (!send_counter(s, tables[t].site, true))
                return;
            iny(s);
            iny(s);
            m_cmp16(s, c->y, 0x0020);
        } while (c->y != 0x0020);
        reset_counter(s);
    }

    /* $C7:04A9: the slot of the first id, less 2, in $FC */
    {
        uint8_t first = m_lda8(s, m_read8(s, m_abs(s, 0x1F00)));
        if (first != 0) {
            m_ldx(s, 0x0020);
            do {
                uint8_t v = dpx8(s, 0x1E);
                m_cmp8(s, first, v);
                if (first == v)
                    break;
                dex(s);
                dex(s);
            } while (c->x != 0);
            dex(s);
            dex(s);
            set_dp16(s, 0xFC, c->x);
        }
    }
    /* $C7:04C0: when $10 changes between zero and nonzero, count in $F3; while
     * $F3 is set, upload the data at SPC700 address $2F00 */
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, dp8(s, 0x10));
    clc(s);
    m_set_a8(s, m_adc8(s, (uint8_t)c->a, 0xFF));
    m_lda8(s, 0x00);
    {
        uint8_t nonzero = rol_a8(s);
        uint8_t seen = dp8(s, 0x11);
        m_cmp8(s, nonzero, seen);
        if (nonzero != seen) {
            set_dp8(s, 0x10, nonzero);
            set_dp8(s, 0x11, nonzero);
            inc_dp8(s, 0xF3);
        }
    }
    if (m_lda8(s, dp8(s, 0xF3)) != 0 && !jsr(s, 0x04D8, 0x0922, M8_X16))
        return;

    /* $C7:04DB: the song's block from the table at $C7:0D18 to SPC700 address
     * $2000 (a 16-bit length, then the data) */
    if (!send_block_header(s, 0x04EE, 0x20, 0x00, 0x03))
        return;
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, dp16(s, 0x05));
    m_lda16(s, c->a & 0x00FF);
    set_dp16(s, 0x82, c->a);
    c->a = m_asl16(s, c->a);
    clc(s);
    c->a = m_adc16(s, c->a, dp16(s, 0x82));
    m_tax(s);
    m_sep(s, CPU_FLAG_M);
    set_dp8(s, 0x12, m_lda8(s, m_read8(s, 0xC70D18 + c->x)));
    set_dp8(s, 0x13, m_lda8(s, m_read8(s, 0xC70D19 + c->x)));
    set_dp8(s, 0x14, m_lda8(s, m_read8(s, 0xC70D1A + c->x)));
    m_ldy(s, dp16(s, 0x12));
    set_dp8(s, 0x12, 0x00);
    set_dp8(s, 0x13, 0x00);
    set_dp8(s, 0x0C, read_header_byte(s));
    set_dp8(s, 0x0D, read_header_byte(s));
    m_ldx(s, dp16(s, 0x0C));
    m_cmp16(s, c->x, 0x0F00);
    if (c->x >= 0x0F00)
        inc_dp8(s, 0xF3);
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, c->y);
    clc(s);
    c->a = m_adc16(s, c->a, dp16(s, 0x0C));
    m_sep(s, CPU_FLAG_M);
    if (m_flag(s, CPU_FLAG_C)) {
        c->pc = 0x054C;   /* the data crosses into the next bank */
        return;
    }
    do {
        /* $C7:0578 */
        if (!send_three_bytes(s, 0x058C))
            return;
        dex(s);
        dex(s);
        dex(s);
    } while (!(c->x & 0x8000));
    reset_counter(s);

    /* $C7:059A: 12 three-byte entries from $C7:1871 + song * $24 to SPC700
     * address $F120 */
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, dp16(s, 0x05));
    m_lda16(s, c->a & 0x00FF);
    set_dp16(s, 0x82, c->a);
    c->a = m_asl16(s, c->a);
    clc(s);
    c->a = m_adc16(s, c->a, dp16(s, 0x82));
    c->a = m_asl16(s, c->a);
    c->a = m_asl16(s, c->a);
    set_dp16(s, 0x82, c->a);
    c->a = m_asl16(s, c->a);
    clc(s);
    c->a = m_adc16(s, c->a, dp16(s, 0x82));
    m_set_index(s, &c->y, c->a);
    m_sep(s, CPU_FLAG_M);
    set_dp8(s, 0x12, m_lda8(s, 0x71));
    set_dp8(s, 0x13, m_lda8(s, 0x18));
    set_dp8(s, 0x14, m_lda8(s, 0xC7));
    if (!send_block_header(s, 0x05CF, 0xF1, 0x20, 0x03))
        return;
    m_ldx(s, 0x000C);
    do {
        if (!send_three_bytes(s, 0x05F7))
            return;
        dex(s);
    } while (c->x != 0);

    /* $C7:05FF: $E0 to ports 1 and 0, and the echo wait */
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, 0xE0);
    m_ldx(s, 0x0400);
    port_write(s, 0x2141, (uint8_t)c->a);
    port_write(s, 0x2140, (uint8_t)c->a);
    c->pc = 0x060C;
}

// $C7:0004
/* JSL entry (a JMP to $C7:0140): save the registers, set the data bank to $00
 * and the direct page to $1E00, and dispatch on the command in $00 (returning
 * at once while bit 7 of $05 is clear): $18-$2F and $80-$FF are sent as they
 * are (stopping at the echo wait at $C7:0635), $10-$17 through the table at
 * $C7:0AD9 (play_song, or return), $30-$3F become command $10 with song
 * $00-$0F and $FF in $02-$03 ($C7:0A98), $70 sets $10/$11 to whether $01 is
 * nonzero and, if that changed, sends $FE (stopping at the wait at $C7:08FA),
 * and $71 checks its block fits (then stops at $C7:07AE, where it uploads). */
void sound_driver_command(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->db);
    m_push16(s, c->d);
    m_push8(s, c->p);
    m_rep(s, CPU_FLAG_M);
    m_rep(s, CPU_FLAG_X);
    m_push16(s, c->a);
    m_push16(s, c->x);
    m_push16(s, c->y);
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, 0x00);
    m_push8(s, (uint8_t)c->a);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_ldx(s, 0x1E00);
    m_push16(s, c->x);
    c->d = pull16_nz(s);

    for (;;) {
        /* $C7:0155 */
        m_sep(s, CPU_FLAG_M);
        if (!(m_lda8(s, dp8(s, 0x05)) & 0x80)) {
            driver_return(s);
            return;
        }
        uint8_t command = m_lda8(s, dp8(s, 0x00));
        if (command == 0) {
            driver_return(s);
            return;
        }
        if (command & 0x80) {
            send_command(s);
            return;
        }
        m_cmp8(s, command, 0x10);
        if (command < 0x10) {
            driver_return(s);
            return;
        }
        m_cmp8(s, command, 0x18);
        if (command < 0x18) {
            /* AND #$0F : ASL A : XBA : LDA #$00 : PHA : XBA : PHA : PLX : JMP ($0AD9,X) */
            m_lda8(s, command & 0x0F);
            m_set_a8(s, m_asl8(s, (uint8_t)c->a));
            m_xba(s);
            m_lda8(s, 0x00);
            m_push8(s, (uint8_t)c->a);
            m_xba(s);
            m_push8(s, (uint8_t)c->a);
            c->x = pull16_nz(s);
            uint16_t target = m_jump_table(s, 0xC7, 0x0AD9, c->x);
            if (target == 0x01A1)
                play_song(s);
            else if (target == 0x0192)
                driver_return(s);
            else
                c->pc = target;
            return;
        }
        m_cmp8(s, command, 0x30);
        if (command < 0x30) {
            send_command(s);
            return;
        }
        m_cmp8(s, command, 0x40);
        if (command < 0x40) {
            /* $C7:071D: the four bytes for song n from $C7:0A98, and dispatch again */
            m_rep(s, CPU_FLAG_M);
            m_lda16(s, c->a & 0x000F);
            c->a = m_asl16(s, c->a);
            c->a = m_asl16(s, c->a);
            m_tax(s);
            set_dp16(s, 0x02, m_lda16(s, m_read16(s, 0xC70A9A + c->x)));
            set_dp16(s, 0x00, m_lda16(s, m_read16(s, 0xC70A98 + c->x)));
            continue;
        }
        m_cmp8(s, command, 0x70);
        if (command == 0x70) {
            /* $C7:08E3 */
            m_sep(s, CPU_FLAG_M);
            m_lda8(s, dp8(s, 0x01));
            clc(s);
            m_set_a8(s, m_adc8(s, (uint8_t)c->a, 0xFF));
            m_lda8(s, 0x00);
            uint8_t nonzero = rol_a8(s);
            uint8_t seen = dp8(s, 0x11);
            m_cmp8(s, nonzero, seen);
            if (nonzero == seen) {
                driver_return(s);
                return;
            }
            set_dp8(s, 0x10, nonzero);
            set_dp8(s, 0x11, nonzero);
            port_write(s, 0x2140, m_lda8(s, 0xFE));
            c->pc = 0x08FA;
            return;
        }
        m_cmp8(s, command, 0x71);
        if (command != 0x71) {
            driver_return(s);
            return;
        }
        /* $C7:0755 */
        if (m_ldx(s, dp16(s, 0x20)) == 0) {
            driver_return(s);
            return;
        }
        m_sep(s, CPU_FLAG_M);
        if (m_lda8(s, dp8(s, 0x01)) == 0) {
            driver_return(s);
            return;
        }
        m_rep(s, CPU_FLAG_M);
        m_lda16(s, (uint16_t)(c->a - 1));
        m_lda16(s, c->a & 0x00FF);
        set_dp16(s, 0x82, c->a);
        c->a = m_asl16(s, c->a);
        set_dp16(s, 0x02, c->a);
        clc(s);
        c->a = m_adc16(s, c->a, dp16(s, 0x82));
        m_tax(s);
        m_sep(s, CPU_FLAG_M);
        set_dp8(s, 0x12, m_lda8(s, m_read8(s, 0xC70AEA + c->x)));
        set_dp8(s, 0x13, m_lda8(s, m_read8(s, 0xC70AEB + c->x)));
        set_dp8(s, 0x14, m_lda8(s, m_read8(s, 0xC70AEC + c->x)));
        m_ldy(s, dp16(s, 0x12));
        set_dp8(s, 0x12, 0x00);
        set_dp8(s, 0x13, 0x00);
        set_dp8(s, 0x0C, read_header_byte(s));
        set_dp8(s, 0x0D, read_header_byte(s));
        m_rep(s, CPU_FLAG_M);
        m_ldx(s, dp16(s, 0xFC));
        m_lda16(s, dp16(s, 0x0C));
        clc(s);
        c->a = m_adc16(s, c->a, dpx16(s, 0x60));
        m_lda16(s, (uint16_t)(c->a - 1));
        uint16_t end = dpx16(s, 0x62);
        m_cmp16(s, c->a, end);
        if (c->a >= end) {
            driver_return(s);
            return;
        }
        c->pc = 0x07AE;
        return;
    }
}

// $C7:063D
/* The command's echo wait ran out: unless the command in $00 is now zero
 * (then return), send it again. */
void sound_driver_command_timed_out(Snes *s)
{
    if (m_lda8(s, dp8(s, 0x00)) == 0)
        driver_return(s);
    else
        send_command(s);
}

// $C7:0643
/* The command was echoed (A): for command $FC, when the low nibble of $01
 * differs from $F0, set $F0 from it ($C7:09FD). Then return. */
void sound_driver_command_echoed(Snes *s)
{
    uint8_t command = (uint8_t)s->cpu.a;
    m_cmp8(s, command, 0xFC);
    if (command == 0xFC) {
        m_lda8(s, dp8(s, 0x01));
        uint8_t v = m_lda8(s, (uint8_t)s->cpu.a & 0x0F);
        uint8_t f0 = dp8(s, 0xF0);
        m_cmp8(s, v, f0);
        if (v != f0)
            set_f0(s, 0x064F);
    }
    driver_return(s);
}

// $C7:0614
/* After the final echo wait of play_song: store A in $05, clear slots from
 * $F2 ($C7:0A12) and return. */
void sound_driver_upload_finished(Snes *s)
{
    set_dp8(s, 0x05, (uint8_t)s->cpu.a);
    clear_slots_from_f2(s, 0x0616);
    driver_return(s);
}

// $C7:0655
/* Upload the block for the id in slot $1C (X, entered with 16-bit A): its
 * 24-bit pointer from $C7:0AEA + (id - 1) * 3 points to a 16-bit length and
 * the data. Command 3 with the slot's address ($60) on ports 2-3, then the data
 * three bytes at a time; the slot's length goes to $40 and the address after
 * the block to $62. Returns with RTS. */
void sound_driver_upload_block(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->p);
    m_rep(s, CPU_FLAG_M);
    m_ldx(s, dp16(s, 0x1C));
    m_lda16(s, dpx16(s, 0x20));
    m_lda16(s, (uint16_t)(c->a - 1));
    set_dp16(s, 0x82, c->a);
    c->a = m_asl16(s, c->a);
    clc(s);
    c->a = m_adc16(s, c->a, dp16(s, 0x82));
    m_tax(s);
    m_sep(s, CPU_FLAG_M);
    set_dp8(s, 0x12, m_lda8(s, m_read8(s, 0xC70AEA + c->x)));
    set_dp8(s, 0x13, m_lda8(s, m_read8(s, 0xC70AEB + c->x)));
    set_dp8(s, 0x14, m_lda8(s, m_read8(s, 0xC70AEC + c->x)));
    m_ldy(s, dp16(s, 0x12));
    set_dp8(s, 0x12, 0x00);
    set_dp8(s, 0x13, 0x00);
    m_ldx(s, dp16(s, 0x1C));
    port_write(s, 0x2142, m_lda8(s, dpx8(s, 0x60)));
    port_write(s, 0x2143, m_lda8(s, dpx8(s, 0x61)));
    port_write(s, 0x2141, m_lda8(s, 0x03));
    if (!send_counter(s, 0x0691, true))
        return;
    uint8_t want = m_lda8(s, 0x03);
    uint8_t got = port_read(s, 0x2141);
    m_cmp8(s, want, got);
    if (want != got) {
        c->pc = 0x069D;   /* JSR $09EA and start over */
        return;
    }
    /* $C7:06A2 */
    uint8_t lo = m_lda8(s, m_read8(s, long_y(s, 0x12)));
    set_dp8(s, 0x0C, lo);
    set_dpx8(s, 0x40, lo);
    clc(s);
    m_set_a8(s, m_adc8(s, lo, dpx8(s, 0x60)));
    set_dpx8(s, 0x62, (uint8_t)c->a);
    iny(s);
    if (c->y == 0)
        inc_dp8(s, 0x14);
    uint8_t hi = m_lda8(s, m_read8(s, long_y(s, 0x12)));
    set_dp8(s, 0x0D, hi);
    set_dpx8(s, 0x41, hi);
    m_set_a8(s, m_adc8(s, hi, dpx8(s, 0x61)));
    set_dpx8(s, 0x63, (uint8_t)c->a);
    iny(s);
    if (c->y == 0)
        inc_dp8(s, 0x14);
    m_ldx(s, dp16(s, 0x0C));
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, c->y);
    clc(s);
    c->a = m_adc16(s, c->a, dp16(s, 0x0C));
    m_sep(s, CPU_FLAG_M);
    if (m_flag(s, CPU_FLAG_C)) {
        c->pc = 0x06CD;   /* the data crosses into the next bank */
        return;
    }
    do {
        /* $C7:06F9 */
        if (!send_three_bytes(s, 0x070D))
            return;
        dex(s);
        dex(s);
        dex(s);
    } while (c->x != 0);
    reset_counter(s);
    plp(s);
    m_rts(s);
}

/* $C7:0956 / $C7:0998: $0C = the 16-bit count at `count`, [$12] = $C7:data,
 * then the data three bytes at a time (the JSR $09DA at `site`). */
static bool upload_2f00_table(Snes *s, uint32_t count, uint16_t data, uint16_t site)
{
    Cpu *c = &s->cpu;
    set_dp8(s, 0x0C, m_lda8(s, m_read8(s, count)));
    set_dp8(s, 0x0D, m_lda8(s, m_read8(s, count + 1)));
    set_dp8(s, 0x12, m_lda8(s, data & 0xFF));
    set_dp8(s, 0x13, m_lda8(s, data >> 8));
    set_dp8(s, 0x14, m_lda8(s, 0xC7));
    m_ldx(s, dp16(s, 0x0C));
    m_ldy(s, 0x0000);
    do {
        if (!send_three_bytes(s, site))
            return false;
        dex(s);
        dex(s);
        dex(s);
    } while (c->x != 0);
    return true;
}

// $C7:0922
/* Command 5 (when the command in $00 is $70) or 3 with address $2F00, then
 * the data from $C7:430B (count at $C7:4309) when $10 is zero, or from
 * $C7:5B0D (count at $C7:5B0B); clears $F3. Returns with RTS. */
void sound_driver_upload_2f00_data(Snes *s)
{
    m_sep(s, CPU_FLAG_M);
    port_write(s, 0x2143, m_lda8(s, 0x2F));
    port_write(s, 0x2142, m_lda8(s, 0x00));
    uint8_t command = m_lda8(s, dp8(s, 0x00));
    m_cmp8(s, command, 0x70);
    uint8_t kind = m_lda8(s, command == 0x70 ? 0x05 : 0x03);
    port_write(s, 0x2141, kind);
    set_dp8(s, 0x86, kind);
    if (!send_counter(s, 0x0941, true))
        return;
    uint8_t want = m_lda8(s, dp8(s, 0x86));
    uint8_t got = port_read(s, 0x2141);
    m_cmp8(s, want, got);
    if (want != got) {
        s->cpu.pc = 0x094D;   /* JSR $09EA and start over */
        return;
    }
    bool done = m_lda8(s, dp8(s, 0x10)) == 0 ? upload_2f00_table(s, 0xC74309, 0x430B, 0x0987)
                                             : upload_2f00_table(s, 0xC75B0B, 0x5B0D, 0x09C9);
    if (!done)
        return;
    set_dp8(s, 0xF3, 0x00);
    reset_counter(s);
    m_rts(s);
}

// $C7:0A5A
/* Write $03, $02, $01 and $00 to ports 3-0, with X = $0400 for the echo wait
 * at $C7:0A71, where the port stops. */
static void send_song_command(Snes *s)
{
    port_write(s, 0x2143, m_lda8(s, dp8(s, 0x03)));
    port_write(s, 0x2142, m_lda8(s, dp8(s, 0x02)));
    port_write(s, 0x2141, m_lda8(s, dp8(s, 0x01)));
    port_write(s, 0x2140, m_lda8(s, dp8(s, 0x00)));
    m_ldx(s, 0x0400);
    s->cpu.pc = 0x0A71;
}

// $C7:0A39
/* (JSR from play_song.) Look up song $01 (bit 7 ignored) in the table at
 * $C7:241D: $02 = its second byte, $03 = the low nibble of its first; when
 * that differs from $F0, set $F0 ($C7:09FD). Then send the command; stops at
 * the echo wait. */
void sound_driver_send_song(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->p);
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, dp16(s, 0x01));
    m_lda16(s, c->a & 0x007F);
    c->a = m_asl16(s, c->a);
    m_tax(s);
    m_sep(s, CPU_FLAG_M);
    set_dp8(s, 0x02, m_lda8(s, m_read8(s, 0xC7241E + c->x)));
    m_lda8(s, m_read8(s, 0xC7241D + c->x));
    uint8_t nibble = m_lda8(s, (uint8_t)c->a & 0x0F);
    set_dp8(s, 0x03, nibble);
    uint8_t f0 = dp8(s, 0xF0);
    m_cmp8(s, nibble, f0);
    if (nibble != f0)
        set_f0(s, 0x0A57);
    send_song_command(s);
}

// $C7:0A79
/* The song command's echo wait ran out: send it again. */
void sound_driver_song_timed_out(Snes *s) { send_song_command(s); }

// $C7:0A7B
/* The song command was echoed (A): the next counter, A + 1 masked to 7 bits,
 * goes to $84. If port 1 reads positive, return to play_song; otherwise write
 * the counter to port 0 and stop at the wait for port 0 to change from it
 * minus 1 ($C7:0A8B). */
void sound_driver_song_echoed(Snes *s)
{
    m_lda8(s, (uint8_t)(s->cpu.a + 1));
    set_dp8(s, 0x84, m_lda8(s, (uint8_t)s->cpu.a & 0x7F));
    if (!(m_lda8(s, port_read(s, 0x2141)) & 0x80)) {
        plp(s);
        m_rts(s);
        return;
    }
    port_write(s, 0x2140, m_lda8(s, dp8(s, 0x84)));
    m_lda8(s, (uint8_t)(s->cpu.a - 1));
    s->cpu.pc = 0x0A8B;
}

// $C7:0A90
/* The SPC700 refused the song: drop $C7:0A39's frame (PLA x3) and return from
 * the driver, skipping the rest of play_song. */
void sound_driver_song_refused(Snes *s)
{
    for (int i = 0; i < 3; i++)
        m_lda8(s, m_pull8(s));
    driver_return(s);
}

/* CMP $2140 : BEQ exit : DEX : BNE loop -- one pass of a counted echo wait:
 * PC moves to `echoed` when port 0 reads A, or to `timed_out` when X runs out. */
static void counted_echo_wait(Snes *s, uint16_t echoed, uint16_t timed_out)
{
    uint8_t a = (uint8_t)s->cpu.a;
    uint8_t v = port_read(s, 0x2140);
    m_cmp8(s, a, v);
    if (a == v) {
        s->cpu.pc = echoed;
        return;
    }
    dex(s);
    if (s->cpu.x == 0)
        s->cpu.pc = timed_out;
}

// $C7:0635
/* The echo wait after send_command. Idle entry, 8-bit A and 16-bit X; exits
 * to $C7:0643 when echoed, or $C7:063D after $0400 passes. */
void sound_driver_wait_command_echo_loop(Snes *s) { counted_echo_wait(s, 0x0643, 0x063D); }

// $C7:0A71
/* The echo wait after the song command. Idle entry, 8-bit A and 16-bit X;
 * exits to $C7:0A7B when echoed, or $C7:0A79 after $0400 passes. */
void sound_driver_wait_song_echo_loop(Snes *s) { counted_echo_wait(s, 0x0A7B, 0x0A79); }

// $C7:09E0
/* The echo wait in $C7:09DA: CMP $2140 : BNE loop. Idle entry, 8-bit A;
 * exits to $C7:09E5. */
void sound_driver_wait_echo_loop(Snes *s)
{
    uint8_t a = (uint8_t)s->cpu.a;
    uint8_t v = port_read(s, 0x2140);
    m_cmp8(s, a, v);
    if (a == v)
        s->cpu.pc = 0x09E5;
}

// $C7:0A8B
/* CMP $2140 : BEQ loop -- wait for port 0 to change from A. Idle entry, 8-bit
 * A; exits to $C7:0A90. */
void sound_driver_wait_port0_change_loop(Snes *s)
{
    uint8_t a = (uint8_t)s->cpu.a;
    uint8_t v = port_read(s, 0x2140);
    m_cmp8(s, a, v);
    if (a != v)
        s->cpu.pc = 0x0A90;
}
