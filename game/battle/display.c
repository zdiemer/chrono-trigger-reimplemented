/* Battle: per-frame display tables, the palette upload and the display setup. */
#include "battle/entries.h"

#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"
#include "ram.h"

/* LDA abs : CLC : ADC abs (16-bit) */
static uint16_t add_abs16(Snes *s, uint16_t a, uint16_t b)
{
    m_lda16(s, m_read16(s, m_abs(s, a)));
    m_set_flag(s, CPU_FLAG_C, false);
    return s->cpu.a = m_adc16(s, s->cpu.a, m_read16(s, m_abs(s, b)));
}

static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }

/* SEP #$10 : REP #$20, then the BG1 and BG2 offsets both entries share:
 * $45/$47 = $2C70/$2C72 + $CCA4/$CCA6 + $BB01/$BB03, $49/$4B = $2C74/$2C76 +
 * $CCA8/$CCAA. */
static void scroll_bg12(Snes *s)
{
    m_sep(s, CPU_FLAG_X);
    m_rep(s, CPU_FLAG_M);
    add_abs16(s, 0x2C70, 0xCCA4);
    m_set_flag(s, CPU_FLAG_C, false);
    set_dp16(s, 0x45, s->cpu.a = m_adc16(s, s->cpu.a, m_read16(s, m_abs(s, 0xBB01))));
    add_abs16(s, 0x2C72, 0xCCA6);
    m_set_flag(s, CPU_FLAG_C, false);
    set_dp16(s, 0x47, s->cpu.a = m_adc16(s, s->cpu.a, m_read16(s, m_abs(s, 0xBB03))));
    set_dp16(s, 0x49, add_abs16(s, 0x2C74, 0xCCA8));
    set_dp16(s, 0x4B, add_abs16(s, 0x2C76, 0xCCAA));
}

/* $CD:0706: write the offsets in $45-$4F to all 32 entries of the three HDMA
 * scroll tables ($BC89, $BD09, $BD89 for BG1-BG3: in each 16-byte block, four
 * horizontal/vertical word pairs), then return with 8-bit A and 16-bit X. */
static void fill_scroll_tables(Snes *s)
{
    Cpu *c = &s->cpu;
    static const uint16_t tables[3] = { 0xBC89, 0xBD09, 0xBD89 };
    m_set_index(s, &c->x, 0x00);
    do {
        for (int t = 0; t < 3; t++) {
            uint16_t h = m_lda16(s, dp16(s, (uint8_t)(0x45 + 4 * t)));
            for (uint16_t k = 0; k < 16; k += 4)
                m_write16(s, m_abs(s, (uint16_t)(tables[t] + k)) + c->x, h);
            uint16_t v = m_lda16(s, dp16(s, (uint8_t)(0x47 + 4 * t)));
            for (uint16_t k = 2; k < 16; k += 4)
                m_write16(s, m_abs(s, (uint16_t)(tables[t] + k)) + c->x, v);
        }
        m_txa(s);
        m_set_flag(s, CPU_FLAG_C, false);
        c->a = m_adc16(s, c->a, 0x0010);
        m_tax(s);
        m_cmp8(s, (uint8_t)c->x, 0x80);
    } while (c->x != 0x80);
    m_tdc(s);
    m_sep(s, CPU_FLAG_M);
    m_rep(s, CPU_FLAG_X);
    m_rts(s);
}

// $CD:06C4
/* Fill the battle's BG1-BG3 scroll HDMA tables from the base scroll ($2C70),
 * the shake/offset values ($CCA4) and, for BG1 and BG2, $BB01. */
void battle_update_scroll_tables(Snes *s)
{
    scroll_bg12(s);
    set_dp16(s, 0x4D, add_abs16(s, 0x2C78, 0xCCAC));
    set_dp16(s, 0x4F, add_abs16(s, 0x2C7A, 0xCCAE));
    fill_scroll_tables(s);
}

// $CD:076C
/* The same, with BG3 scrolled by $CA5A + $CA5E and $CA5C + $CA60 instead of
 * its base scroll. */
void battle_update_scroll_tables_bg3_moving(Snes *s)
{
    scroll_bg12(s);
    add_abs16(s, 0xCA5A, 0xCA5E);
    m_set_flag(s, CPU_FLAG_C, false);
    set_dp16(s, 0x4D, s->cpu.a = m_adc16(s, s->cpu.a, m_read16(s, m_abs(s, 0xCCAC))));
    add_abs16(s, 0xCA5C, 0xCA60);
    m_set_flag(s, CPU_FLAG_C, false);
    set_dp16(s, 0x4F, s->cpu.a = m_adc16(s, s->cpu.a, m_read16(s, m_abs(s, 0xCCAE))));
    fill_scroll_tables(s);
}

/* The battle uses DMA channel 7 for its uploads; its per-line effects are
 * slots 0-6, each reading a table in bank $7E indirectly. */
enum { BATTLE_UPLOAD_DMA = 7, DMA_TWO_BYTES_ONE_REGISTER = 0x02, DMA_WORDS = 0x01 };

// $CD:09CE
/* Upload the working palette: the 256 colors at $7E:2200 into CGRAM from entry
 * 0, two bytes at a time into the same register. Returns with RTL. */
void battle_upload_palette(Snes *s)
{
    /* STZ $4377 : STZ $2121 : STX $4370 : STX $4372 : STA $4374 : STX $4375 : STA $420B */
    static const uint8_t steps[] = {
        VID_STEP_INDIRECT_BANK, VID_STEP_TARGET_LO, VID_STEP_CONTROL, VID_STEP_DEST, VID_STEP_SOURCE_LO,
        VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO, VID_STEP_SIZE_HI, VID_STEP_START, VID_STEP_END,
    };
    static const VidSnesXfer x = { BATTLE_UPLOAD_DMA, DMA_TWO_BYTES_ONE_REGISTER, steps, NULL };
    m_ldx16(s, 0x2202);
    m_ldx16(s, 0x2200);
    m_lda8(s, 0x7E);
    m_ldx16(s, 0x0200);
    vid_upload_palette(m_platform(s), PF_AT(0xCD09CE), 0, PF_GAME(0x7E2200), 0x100, &x);
    m_lda8(s, 0x80);
    m_rtl(s);
}

static bool widths_ok(const Snes *s) { return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) == CPU_FLAG_M; }

/* Call out; false if the callee didn't leave 8-bit A and 16-bit X. */
static bool call(Snes *s, uint32_t site, uint32_t target, bool long_call)
{
    if (long_call)
        m_jsl(s, site, target);
    else
        m_jsr(s, site, target);
    return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) == CPU_FLAG_M;
}

// $CD:09F1
/* Set the battle's display up for a frame: upload the palette, program the
 * seven per-line effect slots from their tables in bank $7E, let the window
 * slot ($CD:0AEE) follow, turn every slot on, set the brightness from $7E:BB00
 * and run the frame's scroll, palette and counter work. Entered with the data
 * bank on the registers; RTS. */
void battle_setup_display(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    /* STA $43x0 : STA $43x1 : STX $43x2 : STA $43x4 : STA $43x7 */
    static const uint8_t steps[] = {
        VID_STEP_CONTROL, VID_STEP_DEST, VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK,
        VID_STEP_INDIRECT_BANK, VID_STEP_END,
    };
    static const struct {
        uint8_t slot, control, dest;
        uint16_t table;
        uint32_t site;
        VidLineTarget target;
    } effects[] = {
        { 0, 0x44, 0x07, 0xC867, 0xCD09F7, VID_LINE_BG_MAPS },
        { 1, 0x43, 0x0D, 0xC894, 0xCD0A11, VID_LINE_SCROLL_BG1 },
        { 2, 0x43, 0x0F, 0xC8C1, 0xCD0A2B, VID_LINE_SCROLL_BG2 },
        { 3, 0x43, 0x11, 0xC8EE, 0xCD0A45, VID_LINE_SCROLL_BG3 },
        { 4, 0x44, 0x2C, 0xC91B, 0xCD0A5F, VID_LINE_SCREENS },
        { 5, 0x44, 0x30, 0xC948, 0xCD0A79, VID_LINE_COLOR_MATH },
        { 6, 0x41, 0x05, 0xC975, 0xCD0A93, VID_LINE_MODE },
    };

    if (!call(s, 0xCD09F1, 0xCD09CE, true))
        return;
    for (size_t i = 0; i < sizeof effects / sizeof effects[0]; i++) {
        m_lda8(s, effects[i].control);
        m_lda8(s, effects[i].dest);
        m_ldx16(s, effects[i].table);
        m_lda8(s, 0x7E);
        m_lda8(s, 0x7E);
        const VidLineEffect e = { .target = effects[i].target, .table = PF_GAME(0x7E0000 | effects[i].table),
                                  .indirect = true, .indirect_bank = 0x7E };
        vid_set_line_effect(pf, PF_AT(effects[i].site), effects[i].slot, &e, steps);
    }

    m_lda8(s, 0x7E);
    m_push8(s, (uint8_t)c->a);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    if (m_lda8(s, m_read8(s, m_abs(s, 0xCAD3))) != 0 && !call(s, 0xCD0AB4, 0xC1000F, true))
        return;
    m_push8(s, c->db);
    m_tdc(s);
    m_push8(s, (uint8_t)c->a);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    if (!call(s, 0xCD0ABC, 0xCD0AEE, false))
        return;
    m_lda8(s, 0xFF);
    vid_enable_line_effects(pf, PF_AT(0xCD0AC1), 0xFF);
    VidDisplay display = vid_display_decode(m_lda8(s, m_read8(s, 0x7EBB00)));
    vid_set_display(pf, PF_AT(0xCD0AC8), &display);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    if (!call(s, 0xCD0ACC, 0xCD06C4, false) || !call(s, 0xCD0ACF, 0xCD0B1E, false) ||
        !call(s, 0xCD0AD2, 0xD1F426, true) || !call(s, 0xCD0AD6, 0xFDFFF7, true) ||
        !call(s, 0xCD0ADA, 0xCD0ADE, false))
        return;
    m_rts(s);
}

/* One block of a battle's graphics, uploaded over DMA channel 7 in words. The
 * loads differ in which of the channel's registers they rewrite: the first of a
 * step programs the whole channel, the rest reuse its mode and destination, and
 * one reuses the source bank as well. */
typedef struct {
    uint32_t source;
    uint16_t size, vram;
    const uint8_t *steps;
    uint32_t site;
} BattleLoad;

static const uint8_t load_steps_channel[] = {
    VID_STEP_CONTROL,   VID_STEP_DEST,      VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK,
    VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,   VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_START,
    VID_STEP_END,
};
static const uint8_t load_steps_same_channel[] = {
    VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,
    VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_START,       VID_STEP_END,
};
static const uint8_t load_steps_same_bank[] = {
    VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SIZE_LO, VID_STEP_SIZE_HI,
    VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_START,   VID_STEP_END,
};

static void run_loads(Snes *s, const BattleLoad *loads, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        const VidSnesXfer x = { BATTLE_UPLOAD_DMA, DMA_WORDS, loads[i].steps, NULL };
        vid_upload_vram(m_platform(s), PF_AT(loads[i].site), loads[i].vram, PF_GAME(loads[i].source), loads[i].size,
                        &x);
        m_lda8(s, 0x80);   /* the value the original leaves in A after starting the transfer */
    }
}

/* INC on an absolute byte, setting N and Z. */
static uint8_t inc_abs8(Snes *s, uint16_t addr)
{
    uint8_t v = (uint8_t)(m_read8(s, m_abs(s, addr)) + 1);
    m_write8(s, m_abs(s, addr), v);
    m_set_nz8(s, v);
    return v;
}

/* $CC:E22E: the two 64-byte blocks at $FF:9CF0 and $FF:9D30 into VRAM $0000
 * and $0100 (the second reuses the source bank). Inlined into step 2 below
 * with the stack bytes its JSR writes. */
static void load_window_tiles(Snes *s)
{
    static const BattleLoad loads[] = {
        { 0xFF9CF0, 0x0040, 0x0000, load_steps_channel, 0xCCE22E },
        { 0xFF9D30, 0x0040, 0x0100, load_steps_same_bank, 0xCCE267 },
    };
    m_jsr_stack_bytes(s, 0xE1AB);   /* JSR $E22E at $CC:E1A9 */
    run_loads(s, loads, sizeof loads / sizeof loads[0]);
}

// $CC:E000
/* Load a battle's graphics into VRAM, one step per call, counted by $98CA
 * (nothing to do while it is 0): step 1 the background tiles at $FF:8C60 and
 * two 64-byte palettes-worth of tiles from bank $D1, step 2 the window and
 * message tiles built in work RAM, step 3 the tile block at $0B40 and the
 * decoded tile stream at $7E:2D00. Returns with RTL. */
void battle_load_graphics(Snes *s)
{
    static const BattleLoad step1[] = {
        { 0xFF8C60, 0x1000, 0x5800, load_steps_channel, 0xCCE00C },
        { 0xD15EC2, 0x0040, 0x0060, load_steps_same_channel, 0xCCE047 },
        { 0xD15E82, 0x0040, 0x0020, load_steps_same_channel, 0xCCE076 },
    };
    static const BattleLoad step2[] = {
        { 0x000CC0, 0x0780, 0x7820, load_steps_channel, 0xCCE0AF },
        { 0xD15F02, 0x0040, 0x0160, load_steps_same_channel, 0xCCE0EB },
        { 0xD15F02, 0x0040, 0x0120, load_steps_same_channel, 0xCCE11B },
        { 0x7E7FFD, 0x01C0, 0x7B00, load_steps_same_channel, 0xCCE14B },
        { 0x7E81BD, 0x0040, 0x7800, load_steps_same_channel, 0xCCE17A },
    };
    static const BattleLoad step3[] = {
        { 0x000B40, 0x0180, 0x7A00, load_steps_channel, 0xCCE1B3 },
        { 0x7E2D00, 0x0C00, 0x1800, load_steps_channel, 0xCCE1EE },
    };
    uint8_t step = m_lda8(s, m_read8(s, m_abs(s, 0x98CA)));
    if (step == 0) {
        m_rtl(s);
        return;
    }
    step = m_lda8(s, (uint8_t)(step - 1));   /* DEC A per step, testing for zero */
    if (step == 0) {
        run_loads(s, step1, sizeof step1 / sizeof step1[0]);
    } else {
        step = m_lda8(s, (uint8_t)(step - 1));
        if (step == 0) {
            run_loads(s, step2, sizeof step2 / sizeof step2[0]);
            load_window_tiles(s);
        } else {
            step = m_lda8(s, (uint8_t)(step - 1));
            if (step != 0) {
                m_rtl(s);   /* $98CA past the last step: nothing to do */
                return;
            }
            run_loads(s, step3, sizeof step3 / sizeof step3[0]);
        }
    }
    inc_abs8(s, 0x98CA);
    m_rtl(s);
}

// $D1:E8D9
/* Upload sixteen $20-byte blocks from A:X into VRAM over DMA channel 7, the
 * first at the word address in $45 and each next one $20 words further on.
 * The source address isn't rewritten: the transfer leaves it past the block
 * it read, so the blocks follow one another in memory (wrapping in the
 * bank). The count it ran down stays in $47. */
enum { TILE_ROW_BLOCKS = 16, TILE_ROW_BYTES = 0x20, TILE_ROW_WORDS = 0x20 };

static void upload_tile_rows(Snes *s, uint32_t source, uint16_t vram)
{
    /* STX $4372 : STA $4374 : STA $4370 : STA $4371 : STZ $4376, then per block
     * STA $2116 (16-bit) : STY $4375 : STX $420B */
    static const uint8_t first_steps[] = {
        VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK, VID_STEP_CONTROL, VID_STEP_DEST,
        VID_STEP_SIZE_HI,   VID_STEP_TARGET_LO, VID_STEP_TARGET_HI,   VID_STEP_SIZE_LO, VID_STEP_START,
        VID_STEP_END,
    };
    static const uint8_t next_steps[] = {
        VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_SIZE_LO, VID_STEP_START, VID_STEP_END,
    };
    for (uint16_t k = 0; k < TILE_ROW_BLOCKS; k++) {
        /* the count of blocks left is kept in $47 as it goes, which a source
         * overlapping the direct page reads */
        m_write16(s, RAM_BATTLE_SCRATCH + 2, (uint16_t)(TILE_ROW_BLOCKS - k));
        const VidSnesXfer x = { BATTLE_UPLOAD_DMA, DMA_WORDS, k == 0 ? first_steps : next_steps, NULL };
        vid_upload_vram(m_platform(s), PF_AT(k == 0 ? 0xD1E8E0 : 0xD1E902), (uint16_t)(vram + TILE_ROW_WORDS * k),
                        PF_GAME((source & 0xFF0000) | (uint16_t)(source + TILE_ROW_BYTES * k)), TILE_ROW_BYTES, &x);
    }
    m_write16(s, RAM_BATTLE_SCRATCH + 2, 0);
}

void battle_upload_tile_rows(Snes *s)
{
    Cpu *c = &s->cpu;
    /* The data bank and the source bank pass through the stack (PHB : PHA :
     * LDA #0 : PHA : PLB : PLA); a source that overlaps the stack reads them. */
    m_write8(s, c->s, c->db);
    m_write8(s, (uint16_t)(c->s - 1), (uint8_t)c->a);
    m_write8(s, (uint16_t)(c->s - 2), 0x00);
    upload_tile_rows(s, (uint32_t)(uint8_t)c->a << 16 | c->x, m_read16(s, RAM_BATTLE_SCRATCH));
    /* what the loop leaves: the index registers' last values, A = the direct
     * page (TDC), 8-bit A, and the overflow of its last step of the VRAM
     * address (ADC #$0020), which its callers read */
    uint16_t last = (uint16_t)(m_read16(s, RAM_BATTLE_SCRATCH) + TILE_ROW_WORDS * (TILE_ROW_BLOCKS - 1));
    uint16_t next = (uint16_t)(last + TILE_ROW_WORDS);
    m_set_flag(s, CPU_FLAG_V, (~(last ^ TILE_ROW_WORDS) & (last ^ next) & 0x8000) != 0);
    c->x = 0x0080;
    c->y = TILE_ROW_BYTES;
    c->a = c->d;
    c->p |= CPU_FLAG_M;
    m_rtl(s);
}


// $D1:F4C0
/* A second battle vblank handler: per-line effects off, the interrupt
 * acknowledged and re-enabled, the working palette uploaded ($CD:09CE), the
 * per-line effects turned back on with what $C0:0005 returns, the brightness
 * from $45 and the three layers' scroll from their base positions, then
 * $C0:0002 (through $CD:0C89) and $FD:FFF7. Returns with RTI. */
void battle_nmi_alternate(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    c->p &= (uint8_t)~(CPU_FLAG_M | CPU_FLAG_X);
    m_push16(s, c->a);
    m_push16(s, c->x);
    m_push16(s, c->y);
    m_push8(s, c->db);
    m_push16(s, c->d);
    c->d = 0x0000;
    c->a = 0x0000;
    c->p |= CPU_FLAG_M;
    vid_enable_line_effects(pf, PF_AT(0xD1F4CE), 0x00);
    tm_ack_vblank(pf, PF_AT(0xD1F4D2));
    tm_enable_interrupts(pf, PF_AT(0xD1F4D8), TM_INT_VBLANK | TM_INT_LINE | TM_INT_AUTO_JOYPAD);
    m_write8(s, RAM_BATTLE_SCRATCH + 2, 0x00);
    m_jsl_dc(s, 0xD1F4DE, 0xCD09CE, DC_A | DC_X | DC_N | DC_Z | DC_C);
    if (!widths_ok(s))
        return;
    m_jsl_dc(s, 0xD1F4E2, 0xC00005, DC_A | DC_X | DC_Y | DC_FLAGS);
    if (!widths_ok(s))
        return;
    /* The enable register is the one write this handler makes through the data
     * bank, which is the interrupted code's. With another bank -- which is how
     * `battle-golem-twins-gameover` reaches this handler on its way back to the
     * title -- the store lands in memory and no register is written, so the
     * port writes the memory byte and carries on. */
    if (m_io_bank(c->db))
        vid_enable_line_effects(pf, PF_AT(0xD1F4E6), (uint8_t)c->a);
    else
        m_write8(s, m_abs(s, M_REG_LINE_EFFECTS), (uint8_t)c->a);
    VidDisplay display = vid_display_decode(m_read8(s, RAM_BATTLE_SCRATCH));
    vid_set_display(pf, PF_AT(0xD1F4ED), &display);
    c->db = 0x7E;
    static const uint32_t scroll_sites[3] = { 0xD1F4F8, 0xD1F514, 0xD1F530 };
    for (int bg = 0; bg < 3; bg++) {
        uint32_t at = RAM_BATTLE_BG_SCROLL_STRIDE * (unsigned)bg;
        vid_set_scroll(pf, PF_AT(scroll_sites[bg]), (VidLayer)bg, m_read16(s, RAM_BATTLE_BG_SCROLL_X + at),
                       m_read16(s, RAM_BATTLE_BG_SCROLL_Y + at), VID_SCROLL_H | VID_SCROLL_V);
    }
    m_jsl_dc(s, 0xD1F549, 0xCD0C89, DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C);
    if (!widths_ok(s))
        return;
    m_jsl_dc(s, 0xD1F54D, 0xFDFFF7, DC_A | DC_X | DC_Y | DC_FLAGS);
    if (!widths_ok(s))
        return;
    c->p &= (uint8_t)~(CPU_FLAG_M | CPU_FLAG_X);
    c->d = m_pull16(s);
    c->db = m_pull8(s);
    c->y = m_pull16(s);
    c->x = m_pull16(s);
    c->a = m_pull16(s);
    m_rti(s);
}
