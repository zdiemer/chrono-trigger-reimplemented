/* Field mode: the per-line effects its vblank work sets up. */
#include "field/display.h"

#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"

bool field_display_bank_is_io(Snes *s) { return m_io_bank(s->cpu.db); }

// $FD:C1EE
/* Program the field's eight per-line effect slots, all reading tables in bank
 * $7F indirectly: the layers' tilemap addresses, BG1-BG3 scroll, the screens
 * and window masks, window 1's edges, the color math layers with the fixed
 * color, and one edge of window 2 -- the left one, or the right one while
 * $013C bit 0 is set. The tables lie at $7F:0F80 and up, or at $7F:1238 and up
 * while the low nibble of $0153 is set (two sets, presumably swapped between
 * frames). The slots themselves are turned on elsewhere; the field NMI copies
 * the enable shadow $0128 every frame. Returns with RTL.
 *
 * The original writes all eight controls, then all eight destinations, and so
 * on; the calls below write each slot's registers together. Nothing observes
 * the order: the routine makes no calls, and every register ends up the same. */
void field_setup_line_effects(Snes *s)
{
    Cpu *c = &s->cpu;
    static const uint8_t steps[] = {
        VID_STEP_CONTROL,     VID_STEP_DEST,      VID_STEP_SOURCE_BANK, VID_STEP_INDIRECT_BANK,
        VID_STEP_SOURCE_LO,   VID_STEP_SOURCE_HI, VID_STEP_END,
    };
    static const VidLineTarget targets[8] = {
        VID_LINE_BG_MAPS,     VID_LINE_SCROLL_BG1, VID_LINE_SCROLL_BG2, VID_LINE_SCROLL_BG3,
        VID_LINE_SCREENS,     VID_LINE_WINDOW1,    VID_LINE_MATH_LAYERS, VID_LINE_WINDOW2_LEFT,
    };
    static const uint16_t tables[2][8] = {
        { 0x0F80, 0x0FD7, 0x102E, 0x1085, 0x10DC, 0x1133, 0x118A, 0x11E1 },
        { 0x1238, 0x128F, 0x12E6, 0x133D, 0x1394, 0x13EB, 0x1442, 0x1499 },
    };
    static const uint32_t sites[8] = { 0xFDC1F9, 0xFDC1FD, 0xFDC201, 0xFDC205,
                                       0xFDC209, 0xFDC20D, 0xFDC211, 0xFDC215 };

    m_push16(s, c->d);
    m_rep(s, CPU_FLAG_M);
    c->d = m_lda16(s, M_DP_DMA);
    m_sep(s, CPU_FLAG_M);

    m_lda8(s, m_read8(s, m_abs(s, 0x013C)));
    m_bit_imm8(s, 0x01);
    bool right_edge = ((uint8_t)c->a & 0x01) != 0;
    /* The values the original loads for each slot's registers only reach A on
     * their way to the registers, which the calls below write instead. */
    int set = m_lda8(s, (uint8_t)(m_read8(s, m_abs(s, 0x0153)) & 0x0F)) != 0 ? 1 : 0;
    for (int i = 0; i < 8; i++) {
        m_ldx16(s, tables[set][i]);
        const VidLineEffect e = {
            .target = i == 7 && right_edge ? VID_LINE_WINDOW2_RIGHT : targets[i],
            .table = PF_GAME(0x7F0000 | tables[set][i]),
            .indirect = true,
            .indirect_bank = 0x7F,
        };
        vid_set_line_effect(m_platform(s), PF_AT(sites[i]), (uint8_t)i, &e, steps);
    }
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    m_rtl(s);
}

/* The field's uploads use DMA channel 7: OAM one byte at a time, VRAM in words. */
enum { FIELD_DMA = 7, DMA_ONE_REGISTER = 0x00, DMA_WORDS = 0x01 };

// $C0:ECA3
/* Upload the sprite table: the 544-byte OAM image at $00:0700, from OAM address
 * 0 with the priority rotation off. */
void field_upload_sprites(Snes *s)
{
    /* STA $2102 : STA $2103 : STA $4370 : STA $4371 : STX $4372 : STA $4374 : STX $4375 : STA $420B */
    static const uint8_t steps[] = {
        VID_STEP_TARGET_LO, VID_STEP_TARGET_HI,   VID_STEP_CONTROL, VID_STEP_DEST,   VID_STEP_SOURCE_LO,
        VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO, VID_STEP_SIZE_HI, VID_STEP_START,
        VID_STEP_END,
    };
    static const VidSnesXfer x = { FIELD_DMA, DMA_ONE_REGISTER, steps, NULL };
    m_lda8(s, 0x00);
    m_lda8(s, 0x00);
    m_lda8(s, 0x04);
    m_ldx16(s, 0x0700);
    m_lda8(s, 0x00);
    m_ldx16(s, 0x0220);
    vid_upload_oam(m_platform(s), PF_AT(0xC0ECA5), PF_GAME(0x000700), &x);
    m_lda8(s, 0x80);
    m_rts(s);
}

/* dp,X: the direct page cell plus X, wrapping inside bank 0. */
static uint16_t dpx(Snes *s, uint8_t cell) { return (uint16_t)(s->cpu.d + cell + s->cpu.x); }

// $C0:B1B2
/* Run the queued VRAM uploads: with the direct page at $0900, each entry of the
 * queue is two transfers from bank $7F, described by a VRAM address ($0950,
 * $0970), a source address ($0940, $0960) and a size ($0980, $0990); $09A0 + X
 * is zero past the last entry, and the queue is emptied by clearing $09A0. A
 * size of 0 means a whole 64 KiB, as the transfer registers read it. Leaves the
 * direct page at $0900, as the original does. */
void field_upload_vram_queue(Snes *s)
{
    Cpu *c = &s->cpu;
    static const uint8_t channel_steps[] = { VID_STEP_DEST, VID_STEP_CONTROL, VID_STEP_SOURCE_BANK, VID_STEP_END };
    /* STY $2116 : STY $4372 : STY $4375 : STA $420B */
    static const uint8_t steps[] = {
        VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
        VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,   VID_STEP_START,     VID_STEP_END,
    };
    static const VidSnesXfer channel = { FIELD_DMA, DMA_WORDS, channel_steps, NULL };
    static const VidSnesXfer x = { FIELD_DMA, DMA_WORDS, steps, NULL };
    m_rep(s, CPU_FLAG_M);
    c->d = m_lda16(s, 0x0900);
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, 0x80);
    m_lda8(s, 0x18);
    m_lda8(s, 0x01);
    m_lda8(s, 0x7F);
    vid_begin_vram_uploads(m_platform(s), PF_AT(0xC0B1BC), VID_VRAM_WORD_PORT, PF_GAME(0x7F0000), &channel);
    m_ldx16(s, 0x0000);
    while (m_lda8(s, m_read8(s, dpx(s, 0xA0))) != 0) {
        static const struct { uint8_t vram, source, size; uint32_t site; } halves[2] = {
            { 0x50, 0x40, 0x80, 0xC0B1D7 },
            { 0x70, 0x60, 0x90, 0xC0B1EB },
        };
        for (int h = 0; h < 2; h++) {
            uint16_t vram = m_set_index(s, &c->y, m_read16(s, dpx(s, halves[h].vram)));
            uint16_t source = m_set_index(s, &c->y, m_read16(s, dpx(s, halves[h].source)));
            uint32_t len = m_set_index(s, &c->y, m_read16(s, dpx(s, halves[h].size)));
            vid_upload_vram(m_platform(s), PF_AT(halves[h].site), vram, PF_GAME(0x7F0000 | source),
                            len ? len : 0x10000, &x);
            m_lda8(s, 0x80);
        }
        m_inx(s);
        m_inx(s);
    }
    m_write8(s, m_dp(s, 0xA0), 0x00);
    m_rts(s);
}

/* The deferred uploads the field NMI runs from the bit flags in $1D78-$1D7B:
 * twelve routines of the same shape, each sending two or three blocks of the
 * tile graphics built in bank $7E to VRAM. With the direct page at $1D00, a
 * block's size and VRAM address are words on that page and nothing is uploaded
 * when the size is 0; the blocks of a routine follow one another in memory from
 * its base address. The six three-block routines step the VRAM address by a
 * word, the six two-block ones by 32 words (tile columns). */
typedef struct {
    uint8_t count;
    uint8_t size[3], vram[3];   /* direct page cells */
    uint16_t base;              /* where the routine's first block lies in bank $7E */
    bool size_through_a;        /* the size reaches the register through A, not X */
    bool wide_step;
    uint32_t site[3], jsr[3];
} FieldUploadSet;

static const VidVramPort upload_port_word = { .step = 1, .after_high = true };
static const VidVramPort upload_port_column = { .step = 32, .after_high = true };

/* STX/STA $4375 : STX $2116 : STX/STA $4372, then STA $2115 : STA $4370 :
 * STA $4371 : STA $4374 : STA $420B in the shared tail. */
static const uint8_t upload_steps[] = {
    VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI, VID_STEP_TARGET_LO,   VID_STEP_TARGET_HI, VID_STEP_SOURCE_LO,
    VID_STEP_SOURCE_HI, VID_STEP_VRAM_PORT, VID_STEP_CONTROL,   VID_STEP_DEST,      VID_STEP_SOURCE_BANK,
    VID_STEP_START,     VID_STEP_END,
};

static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }

static void field_deferred_upload(Snes *s, const FieldUploadSet *u, int k)
{
    Cpu *c = &s->cpu;
    uint16_t size = m_ldx16(s, dp16(s, u->size[k]));
    if (size == 0)
        return;
    if (u->size_through_a) {
        m_rep(s, CPU_FLAG_M);
        m_lda16(s, size);
        m_sep(s, CPU_FLAG_M);
    }
    uint16_t vram = m_ldx16(s, dp16(s, u->vram[k]));
    uint16_t source;
    if (k == 0) {
        source = m_ldx16(s, u->base);
    } else {
        m_rep(s, CPU_FLAG_M);
        m_lda16(s, dp16(s, u->size[0]));
        m_set_flag(s, CPU_FLAG_C, false);
        if (k == 2)
            c->a = m_adc16(s, c->a, dp16(s, u->size[1]));
        source = c->a = m_adc16(s, c->a, u->base);
        m_sep(s, CPU_FLAG_M);
    }
    const VidSnesXfer x = { FIELD_DMA, DMA_WORDS, upload_steps,
                            u->wide_step ? &upload_port_column : &upload_port_word };
    /* The JSR into the shared tail comes before the transfer, and a transfer
     * long enough to read through the stack page copies its bytes to VRAM. */
    m_jsr_stack_bytes(s, (uint16_t)(u->jsr[k] + 2));
    vid_upload_vram(m_platform(s), PF_AT(u->site[k]), vram, PF_GAME(0x7E0000 | source), size, &x);
    m_lda8(s, 0x01);
    m_lda8(s, 0x18);
    m_lda8(s, 0x7E);
    m_lda8(s, 0x80);
}

static void field_run_deferred_uploads(Snes *s, const FieldUploadSet *u)
{
    for (int k = 0; k < u->count; k++)
        field_deferred_upload(s, u, k);
    m_rts(s);
}

#define FIELD_UPLOAD_ENTRY(name, ...)                                                                                  \
    void name(Snes *s)                                                                                                 \
    {                                                                                                                  \
        static const FieldUploadSet set = __VA_ARGS__;                                                                 \
        field_run_deferred_uploads(s, &set);                                                                            \
    }

// $C0:8445
FIELD_UPLOAD_ENTRY(field_upload_layer1_tiles,
                   { 3, { 0xDB, 0xDD, 0xDF }, { 0xA5, 0xA7, 0xA9 }, 0xC800, true, false,
                     { 0xC0844D, 0xC08468, 0xC0848A }, { 0xC0845D, 0xC0847F, 0xC084A3 } })
// $C0:84C1
FIELD_UPLOAD_ENTRY(field_upload_layer2_tiles,
                   { 3, { 0xE5, 0xE7, 0xE9 }, { 0xB9, 0xBB, 0xBD }, 0xD000, true, false,
                     { 0xC084C9, 0xC084E4, 0xC08506 }, { 0xC084D9, 0xC084FB, 0xC0851F } })
// $C0:8523
FIELD_UPLOAD_ENTRY(field_upload_layer3_tiles,
                   { 3, { 0xEF, 0xF1, 0xF3 }, { 0xCD, 0xCF, 0xD1 }, 0xD800, true, false,
                     { 0xC0852B, 0xC08546, 0xC08568 }, { 0xC0853B, 0xC0855D, 0xC08581 } })
// $C0:8585
FIELD_UPLOAD_ENTRY(field_upload_layer1_tiles_alt,
                   { 3, { 0xDB, 0xDD, 0xDF }, { 0x9F, 0xA1, 0xA3 }, 0xC800, true, false,
                     { 0xC0858D, 0xC085A8, 0xC085CA }, { 0xC0859D, 0xC085BF, 0xC085E3 } })
// $C0:85E7
FIELD_UPLOAD_ENTRY(field_upload_layer2_tiles_alt,
                   { 3, { 0xE5, 0xE7, 0xE9 }, { 0xB3, 0xB5, 0xB7 }, 0xD000, true, false,
                     { 0xC085EF, 0xC0860A, 0xC0862C }, { 0xC085FF, 0xC08621, 0xC08645 } })
// $C0:8649
FIELD_UPLOAD_ENTRY(field_upload_layer3_tiles_alt,
                   { 3, { 0xEF, 0xF1, 0xF3 }, { 0xC7, 0xC9, 0xCB }, 0xD800, true, false,
                     { 0xC08651, 0xC0866C, 0xC0868E }, { 0xC08661, 0xC08683, 0xC086A7 } })
// $C0:86AB
FIELD_UPLOAD_ENTRY(field_upload_layer1_columns,
                   { 2, { 0xE1, 0xE3 }, { 0xAF, 0xB1 }, 0xC880, false, true,
                     { 0xC086AF, 0xC086C4 }, { 0xC086BD, 0xC086D9 } })
// $C0:86F7
FIELD_UPLOAD_ENTRY(field_upload_layer2_columns,
                   { 2, { 0xEB, 0xED }, { 0xC3, 0xC5 }, 0xD080, false, true,
                     { 0xC086FB, 0xC08710 }, { 0xC08709, 0xC08725 } })
// $C0:8729
FIELD_UPLOAD_ENTRY(field_upload_layer3_columns,
                   { 2, { 0xF5, 0xF7 }, { 0xD7, 0xD9 }, 0xD880, false, true,
                     { 0xC0872D, 0xC08742 }, { 0xC0873B, 0xC08757 } })
// $C0:875B
FIELD_UPLOAD_ENTRY(field_upload_layer1_columns_alt,
                   { 2, { 0xE1, 0xE3 }, { 0xAB, 0xAD }, 0xC880, false, true,
                     { 0xC0875F, 0xC08774 }, { 0xC0876D, 0xC08789 } })
// $C0:878D
FIELD_UPLOAD_ENTRY(field_upload_layer2_columns_alt,
                   { 2, { 0xEB, 0xED }, { 0xBF, 0xC1 }, 0xD080, false, true,
                     { 0xC08791, 0xC087A6 }, { 0xC0879F, 0xC087BB } })
// $C0:87BF
FIELD_UPLOAD_ENTRY(field_upload_layer3_columns_alt,
                   { 2, { 0xF5, 0xF7 }, { 0xD3, 0xD5 }, 0xD880, false, true,
                     { 0xC087C3, 0xC087D8 }, { 0xC087D1, 0xC087ED } })

// $FD:C0D7
/* Set the field's video mode up: mode 1 with the BG3 priority bit, the three
 * layers' tilemaps at $6800, $7000 and $7200 (64 tiles wide) with character
 * bases at $1000, $1000 and $5000, no sprites or interlace, every scroll at 0,
 * BG1-BG3 and the sprites on the main screen, color math adding the sub screen,
 * and a black fixed color. Writes through a direct page on the registers
 * themselves; returns with RTL. */
void field_init_video_mode(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    m_push16(s, c->d);
    m_rep(s, CPU_FLAG_M);
    c->d = m_lda16(s, M_DP_PPU);
    m_sep(s, CPU_FLAG_M);

    vid_set_mode(pf, PF_AT(0xFDC0E2), &(VidMode){ .mode = 1, .bg3_priority = true });
    vid_set_bg_chars(pf, PF_AT(0xFDC0E6), VID_BG1, 0x1000, 0x1000);
    vid_set_bg_chars(pf, PF_AT(0xFDC0EA), VID_BG3, 0x5000, 0x0000);
    static const struct { uint8_t byte; uint32_t site; } maps[3] = {
        { 0x69, 0xFDC0EE }, { 0x71, 0xFDC0F2 }, { 0x79, 0xFDC0F6 },
    };
    for (int bg = 0; bg < 3; bg++) {
        VidBg b = { 0 };
        vid_bg_map_decode(&b, maps[bg].byte);
        vid_set_bg_map(pf, PF_AT(maps[bg].site), (VidLayer)bg, &b);
    }
    vid_set_obj_config(pf, PF_AT(0xFDC0FA), &(VidObjConfig){ 0 });
    vid_set_video_config(pf, PF_AT(0xFDC0FC), &(VidVideoConfig){ 0 });
    static const uint32_t scroll_sites[3] = { 0xFDC0FE, 0xFDC106, 0xFDC10E };
    for (int bg = 0; bg < 3; bg++)
        vid_set_scroll(pf, PF_AT(scroll_sites[bg]), (VidLayer)bg, 0, 0, VID_SCROLL_H | VID_SCROLL_V);
    m_lda8(s, 0x13);
    vid_set_screens(pf, PF_AT(0xFDC118), 0x13, 0, VID_SCREEN_MAIN);
    m_lda8(s, 0x02);
    VidColorMath math = { 0 };
    vid_color_math_decode(&math, VID_MATH_CONTROL, 0x02);
    vid_set_color_math(pf, PF_AT(0xFDC11C), &math, VID_MATH_CONTROL);
    m_lda8(s, 0xE0);
    vid_set_fixed_color(pf, PF_AT(0xFDC120), VID_RED | VID_GREEN | VID_BLUE, 0);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    m_rtl(s);
}

// $C0:A509
/* Copy the screen shadows to the hardware: the main and sub screen layers from
 * $0BD7/$0BD8, the color math layers from $0BDF, and no mosaic. */
void field_set_screens(Snes *s)
{
    Platform *pf = m_platform(s);
    uint8_t main_layers = m_lda8(s, m_read8(s, m_abs(s, 0x0BD7)));
    vid_set_screens(pf, PF_AT(0xC0A50C), main_layers, 0, VID_SCREEN_MAIN);
    uint8_t sub_layers = m_lda8(s, m_read8(s, m_abs(s, 0x0BD8)));
    vid_set_screens(pf, PF_AT(0xC0A512), 0, sub_layers, VID_SCREEN_SUB);
    VidColorMath math = { 0 };
    vid_color_math_decode(&math, VID_MATH_LAYERS, m_lda8(s, m_read8(s, m_abs(s, 0x0BDF))));
    vid_set_color_math(pf, PF_AT(0xC0A518), &math, VID_MATH_LAYERS);
    m_lda8(s, 0x00);
    vid_set_mosaic(pf, PF_AT(0xC0A51D), 0x00, 1);
    m_rts(s);
}

// $C0:2DC8
/* The transfer the screen transition routines describe on the direct page: the
 * VRAM address in $48, the transfer mode in $4A, the source in $4B-$4D and the
 * size in $4E, over DMA channel 7. */
void field_vram_transfer(Snes *s)
{
    Cpu *c = &s->cpu;
    /* STX $2116 : STA $2115 : STA $4370 : STA $4371 : STX $4372 : STA $4374 : STX $4375 : STA $420B */
    static const uint8_t steps[] = {
        VID_STEP_TARGET_LO,   VID_STEP_TARGET_HI, VID_STEP_VRAM_PORT, VID_STEP_CONTROL, VID_STEP_DEST,
        VID_STEP_SOURCE_LO,   VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO, VID_STEP_SIZE_HI,
        VID_STEP_START,       VID_STEP_END,
    };
    uint16_t vram = m_ldx16(s, dp16(s, 0x48));
    m_lda8(s, 0x80);
    uint8_t control = m_lda8(s, m_read8(s, m_dp(s, 0x4A)));
    m_lda8(s, 0x18);
    uint16_t source = m_ldx16(s, dp16(s, 0x4B));
    uint8_t bank = m_lda8(s, m_read8(s, m_dp(s, 0x4D)));
    uint32_t size = m_ldx16(s, dp16(s, 0x4E));
    const VidSnesXfer x = { FIELD_DMA, control, steps, &upload_port_word };
    vid_upload_vram(m_platform(s), PF_AT(0xC02DCA), vram, PF_GAME((uint32_t)bank << 16 | source),
                    size ? size : 0x10000, &x);
    m_lda8(s, 0x80);
    m_rts(s);
    (void)c;
}

/* Four pairs of tilemap words the field keeps up to date in VRAM, presumably
 * the corners of the four party windows: slot k has its words at $1C02 + $40*k
 * and $1C22 + $40*k, normally the tiles $2902 + $40*k and $2912 + $40*k. */
typedef struct {
    uint16_t vram[2], tile[2];
    uint32_t site[6];
} FieldTilePair;

static void field_write_tile_pair(Snes *s, const FieldTilePair *p)
{
    Platform *pf = m_platform(s);
    for (int i = 0; i < 2; i++) {
        m_ldx16(s, p->vram[i]);
        vid_set_vram_address(pf, PF_AT(p->site[3 * i]), p->vram[i]);
        m_ldx16(s, p->tile[i]);
        vid_write_vram(pf, PF_AT(p->site[3 * i + 1]), p->tile[i]);
        m_inx(s);
        vid_write_vram(pf, PF_AT(p->site[3 * i + 2]), (uint16_t)(p->tile[i] + 1));
    }
}

static const FieldTilePair field_window_tiles[4] = {
    { { 0x1C02, 0x1C22 }, { 0x2902, 0x2912 }, { 0xC0F15C, 0xC0F161, 0xC0F164, 0xC0F169, 0xC0F16E, 0xC0F128 } },
    { { 0x1C42, 0x1C62 }, { 0x2942, 0x2952 }, { 0xC0F145, 0xC0F14A, 0xC0F14D, 0xC0F152, 0xC0F157, 0xC0F128 } },
    { { 0x1C82, 0x1CA2 }, { 0x2982, 0x2992 }, { 0xC0F12E, 0xC0F133, 0xC0F136, 0xC0F13B, 0xC0F140, 0xC0F128 } },
    { { 0x1CC2, 0x1CE2 }, { 0x29C2, 0x29D2 }, { 0xC0F113, 0xC0F118, 0xC0F11B, 0xC0F120, 0xC0F125, 0xC0F128 } },
};

// $C0:F159
void field_write_window_tiles_0(Snes *s) { field_write_tile_pair(s, &field_window_tiles[0]); m_rts(s); }
// $C0:F142
void field_write_window_tiles_1(Snes *s) { field_write_tile_pair(s, &field_window_tiles[1]); m_rts(s); }
// $C0:F12B
void field_write_window_tiles_2(Snes *s) { field_write_tile_pair(s, &field_window_tiles[2]); m_rts(s); }
// $C0:F110
void field_write_window_tiles_3(Snes *s) { field_write_tile_pair(s, &field_window_tiles[3]); m_rts(s); }

// $C0:F05E
/* Refresh the four window tile pairs from the slot in $0163: the other three
 * get their own tiles, the one $0163 names gets $28FC-$28FF instead. With
 * $0163 outside 0-3 all four get their own and $0163 becomes $80. Writes
 * through a direct page on the registers themselves. */
void field_patch_window_tiles(Snes *s)
{
    Cpu *c = &s->cpu;
    static const uint32_t helper_targets[4] = { 0xC0F159, 0xC0F142, 0xC0F12B, 0xC0F110 };
    /* The JSR sites of the three pairs each path refreshes, and of all four on
     * the path that takes no slot as selected. */
    static const uint32_t helper_sites[5][4] = {
        { 0, 0xC0F08B, 0xC0F08E, 0xC0F091 },
        { 0xC0F0F0, 0, 0xC0F0F3, 0xC0F0F6 },
        { 0xC0F0D0, 0xC0F0D3, 0, 0xC0F0D6 },
        { 0xC0F0AB, 0xC0F0AE, 0xC0F0B1, 0 },
        { 0xC0F079, 0xC0F07C, 0xC0F07F, 0xC0F082 },
    };
    static const uint32_t selected_sites[4][6] = {
        { 0xC0F097, 0xC0F09C, 0xC0F09F, 0xC0F0A4, 0xC0F0C9, 0xC0F0CC },
        { 0xC0F0FC, 0xC0F101, 0xC0F104, 0xC0F109, 0xC0F0C9, 0xC0F0CC },
        { 0xC0F0DC, 0xC0F0E1, 0xC0F0E4, 0xC0F0E9, 0xC0F0C9, 0xC0F0CC },
        { 0xC0F0B7, 0xC0F0BC, 0xC0F0BF, 0xC0F0C4, 0xC0F0C9, 0xC0F0CC },
    };
    m_push16(s, c->d);
    m_rep(s, CPU_FLAG_M);
    c->d = m_lda16(s, M_DP_PPU);
    m_sep(s, CPU_FLAG_M);
    m_lda8(s, 0x80);
    vid_begin_vram_uploads(m_platform(s), PF_AT(0xC0F069), VID_VRAM_WORD_PORT, PF_GAME(0), NULL);

    uint8_t slot = m_lda8(s, m_read8(s, m_abs(s, 0x0163)));
    int selected = -1;
    for (int k = 0; k < 4; k++) {
        if (slot == 0) {
            selected = k;
            break;
        }
        if (k < 3)   /* three DEC A, and what is left in A past the last one */
            slot = m_lda8(s, (uint8_t)(slot - 1));
    }
    const uint32_t *sites = helper_sites[selected < 0 ? 4 : selected];
    for (int k = 0; k < 4; k++) {
        if (k == selected)
            continue;
        m_jsr(s, sites[k], helper_targets[k]);
        if (s->cpu.e || (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M)
            return;
    }
    if (selected < 0) {
        c->d = m_pull16(s);
        m_set_nz16(s, c->d);
        m_write8(s, m_dp(s, 0x63), m_lda8(s, 0x80));
        m_rts(s);
        return;
    }
    FieldTilePair p = field_window_tiles[selected];
    p.tile[0] = 0x28FC;
    p.tile[1] = 0x28FE;
    for (int i = 0; i < 6; i++)
        p.site[i] = selected_sites[selected][i];
    field_write_tile_pair(s, &p);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    m_rts(s);
}

/* REP #$30 : PLB : PLD : PLY : PLX : PLA : RTI -- the field IRQ's tail. */
static void field_irq_leave(Snes *s)
{
    Cpu *c = &s->cpu;
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    c->y = m_pull16(s);
    m_set_nz16(s, c->y);
    c->x = m_pull16(s);
    m_set_nz16(s, c->x);
    c->a = m_pull16(s);
    m_set_nz16(s, c->a);
    m_rti(s);
}

// $C0:ECCC (installed at the $0504 line-interrupt stub by field_install_irq)
/* The field's line interrupt: does nothing while $010F is negative or the
 * interrupt isn't the line one, otherwise leaves only the vblank and controller
 * interrupts on, sets the data bank to 0 and waits for hblank (an idle entry at
 * $C0:ECEB, where this port stops). */
void field_irq(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_push16(s, c->a);
    m_push16(s, c->x);
    m_push16(s, c->y);
    m_push16(s, c->d);
    m_push8(s, c->db);
    m_sep(s, CPU_FLAG_M);
    if (m_lda8(s, m_read8(s, 0x00010F)) & 0x80) {
        field_irq_leave(s);
        return;
    }
    if (!(m_lda8(s, tm_ack_line_irq(pf, PF_AT(0xC0ECDB))) & TM_LINE_IRQ_PENDING)) {
        field_irq_leave(s);
        return;
    }
    m_lda8(s, 0x81);
    tm_enable_interrupts(pf, PF_AT(0xC0ECE3), TM_INT_VBLANK | TM_INT_AUTO_JOYPAD);
    m_lda8(s, 0x00);
    m_push8(s, 0x00);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    c->pc = 0xECEB;
}

// $C0:ECEB
/* One pass of the field IRQ's wait for hblank. Idle entry, 8-bit A; exits to
 * the forced blank at $C0:ECF2. */
void field_irq_wait_hblank_loop(Snes *s)
{
    uint8_t v = m_lda8(s, tm_video_status(m_platform(s), PF_AT(0xC0ECEB)));
    if (m_lda8(s, (uint8_t)(v & TM_STATUS_HBLANK)) != 0)
        s->cpu.pc = 0xECF2;
}

// $C0:ECF2
/* The rest of the field IRQ, from hblank: forced blank, per-line effects off,
 * then the tilemap work ($C0:ED15, or $FD:FFFD while $0153 bit 0 is set), and
 * return from the interrupt. */
void field_irq_after_hblank(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    m_lda8(s, 0x80);
    vid_raster_display(pf, PF_AT(0xC0ECF4), VID_AT_LINE_HANDLER, &(VidDisplay){ .blank = true });
    m_lda8(s, 0x00);
    vid_enable_line_effects(pf, PF_AT(0xC0ECF9), 0x00);
    uint8_t mode = m_lda8(s, m_read8(s, m_abs(s, 0x0153)));
    if (m_lda8(s, (uint8_t)(mode & 0x01)) == 0)
        m_jsr(s, 0xC0ED03, 0xC0ED15);
    else
        m_jsl(s, 0xC0ED08, 0xFDFFFD);
    if (c->e || (c->p & (CPU_FLAG_M | CPU_FLAG_X)) != CPU_FLAG_M)
        return;
    field_irq_leave(s);
}

// $C0:2DF1
/* Fill memory with zero, using the Mode 7 matrix registers as a multiplier:
 * with both operands 0 the product the PPU offers is 0, and a transfer in the
 * other direction copies it over $4E bytes at $4B-$4D. The transfer reads a
 * register rather than writing one, so it goes through the SNES-only escape;
 * only the two matrix values it leaves behind are video state. */
void field_fill_memory(Snes *s)
{
    static const uint8_t steps[] = {
        VID_STEP_CONTROL,   VID_STEP_DEST,    VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK,
        VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI, VID_STEP_START,     VID_STEP_END,
    };
    Platform *pf = m_platform(s);
    m_lda8(s, 0x00);
    vid_set_mode7(pf, PF_AT(0xC02DF3), &(VidMode7){ .a = 0, .b = 0 }, VID_M7_MATRIX_AB);
    m_lda8(s, 0x80);
    m_lda8(s, 0x34);
    uint16_t dest = m_ldx16(s, dp16(s, 0x4B));
    uint8_t bank = m_lda8(s, m_read8(s, m_dp(s, 0x4D)));
    uint16_t size = m_ldx16(s, dp16(s, 0x4E));
    const VidSnesXfer x = { FIELD_DMA, 0x80, steps, NULL };   /* from the B bus to memory */
    const VidSnesTransfer t = { .dest = 0x34, .source = (uint32_t)bank << 16 | dest, .size = size };
    vid_snes_transfer(pf, PF_AT(0xC02E01), &t, &x);
    m_lda8(s, 0x80);
    m_rts(s);
}

/* The four tile writers reach the registers through a direct page their caller
 * puts on the PPU block; with another direct page they would write memory. */
bool field_window_tiles_dp_is_ppu(Snes *s) { return s->cpu.d == M_DP_PPU; }
