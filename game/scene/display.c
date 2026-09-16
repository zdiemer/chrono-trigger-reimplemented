/* Scenes: the display work of the program the boot at $C3:0014 decompresses to
 * $7E:3000 (the attract sequence and title screen, the jet bike race, and the
 * title again after a game over), and the bank C3 routine its vblank handler
 * calls.
 *
 * The program writes the registers itself, so P2 has to name what each write
 * means. Every entry in RAM is guarded by a hash of its bytes, as the other
 * ports of the program are (scene/program.c): the same addresses hold other
 * code and data at other times.
 */
#include "scene/display.h"

#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"
#include "scene/scene.h"

/* The scenes upload over DMA channel 7. */
enum { SCENE_DMA = 7, DMA_ONE_REGISTER = 0x00, DMA_WORDS = 0x01, DMA_FIXED_BYTE = 0x08,
       DMA_FROM_REGISTER = 0x80 };

/* The work RAM data port: memory access, not video (docs/PLATFORM.md). */
enum { WRAM_PORT = 0x2180, WRAM_ADDR = 0x2181 };

/* $2115 as the program sets it: a word at a time, stepping after the low byte
 * for the writes it makes through the port itself, after the high byte for the
 * transfers. */
static const VidVramPort low_byte_port = { .step = 1, .after_high = false };
static const VidVramPort word_port = { .step = 1, .after_high = true };

/* A transfer's register writes in the order the program makes them: the control
 * and destination together (one 16-bit store), then the source, the size and the
 * start. Several of the program's own transfers share it. */
static const uint8_t transfer_steps[] = {
    VID_STEP_CONTROL,     VID_STEP_DEST,    VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
    VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO, VID_STEP_SIZE_HI,   VID_STEP_START,
    VID_STEP_END,
};

static uint8_t abs8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr)); }

/* ASL on a direct-page byte, and ROL on the 8-bit accumulator. */
static void asl_dp8(Snes *s, uint8_t cell)
{
    uint32_t a = m_dp(s, cell);
    uint8_t v = m_read8(s, a);
    uint8_t r = (uint8_t)(v << 1);
    m_write8(s, a, r);
    m_set_flag(s, CPU_FLAG_C, v & 0x80);
    m_set_nz8(s, r);
}

static void rol_a8(Snes *s)
{
    uint8_t v = (uint8_t)s->cpu.a;
    uint8_t r = (uint8_t)(v << 1 | (m_flag(s, CPU_FLAG_C) ? 1 : 0));
    m_set_flag(s, CPU_FLAG_C, v & 0x80);
    m_set_a8(s, r);
    m_set_nz8(s, r);
}

// $C3:08D0 (also inlined by scene_nmi, which reaches it through a JSL)
/* The first half of the scenes' vblank work, when something other than the NMI
 * handler calls it: acknowledge, per-line effects off, forced blank, the task
 * list, then the enables and brightness back. Returns with RTL. */
void scene_frame_work(Snes *s)
{
    if (scene_run_frame_work(s))
        m_rtl(s);
}

/* ---- the program at $7E:3000 --------------------------------------------------- */

bool scene_program_restart_present(Snes *s) { return m_code_matches(s, 0x7E302A, 14, 0x614033C5F8C83156ull); }

// $7E:302A
/* The program stops: interrupts and per-line effects off and the display
 * blanked, before it moves the stack and jumps to the reset entry. Stops at the
 * call that follows, which the program has not been taken apart past. */
void scene_program_restart(Snes *s)
{
    Platform *pf = m_platform(s);
    m_lda16(s, 0x0080);
    m_sep(s, CPU_FLAG_M);
    tm_enable_interrupts(pf, PF_AT(0x7E302F), 0);
    vid_enable_line_effects(pf, PF_AT(0x7E3032), 0);
    vid_set_display(pf, PF_AT(0x7E3035), &(VidDisplay){ .blank = true });
    s->cpu.pc = 0x3038;
}

bool scene_clear_work_ram_present(Snes *s) { return m_code_matches(s, 0x7E3092, 87, 0x2A1AE096D5B36F10ull); }

// $7E:3092
/* Clear the program's work RAM by copying the PPU multiplier's product out of
 * $2134: with the matrix set to $0404 times 0 the product is 0, and two
 * transfers in the other direction spread it over $00:0000-$00:00FF and then
 * over either $00:0920 or $00:0900 upwards, depending on bit 7 of the mode word
 * at $0384. The transfers read a register rather than writing one, so they go
 * through the SNES-only escape; only the matrix they leave behind is video
 * state. The second transfer keeps the first one's mode, destination and bank,
 * which this port knows because it made them.
 *
 * Returns where the mode word says the shorter clear (the title screen coming
 * back), and otherwise stops at $7E:30E9, where the program goes on. */
void scene_clear_work_ram(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    static const uint8_t again_steps[] = {
        VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SIZE_LO, VID_STEP_SIZE_HI, VID_STEP_START,
        VID_STEP_END,
    };
    const VidSnesXfer x = { SCENE_DMA, DMA_FROM_REGISTER, transfer_steps, NULL };
    const VidSnesXfer again = { SCENE_DMA, DMA_FROM_REGISTER, again_steps, NULL };

    m_push16(s, M_DP_DMA);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    uint8_t v = (uint8_t)c->a;
    vid_set_mode7(pf, PF_AT(0x7E3096), &(VidMode7){ .a = (int16_t)(v << 8 | v) }, VID_M7_MATRIX_A);
    m_tdc(s);
    vid_set_mode7(pf, PF_AT(0x7E309D), &(VidMode7){ .b = 0 }, VID_M7_MATRIX_B);
    m_ldx(s, 0x3480);
    m_ldx(s, 0x0000);
    m_ldx(s, 0x0100);
    m_lda8(s, 0x80);
    const VidSnesTransfer first = { .dest = 0x34, .source = 0x000000, .size = 0x0100 };
    vid_snes_transfer(pf, PF_AT(0x7E30B6), &first, &x);

    m_push16(s, 0x0000);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    m_lda8(s, m_read8(s, m_abs(s, 0x0384)));
    m_bit_imm8(s, 0x80);
    bool shorter = !m_flag(s, CPU_FLAG_Z);
    m_ldx(s, shorter ? 0x0920 : 0x0900);
    m_ldx(s, shorter ? 0x14E0 : 0x1500);
    m_lda8(s, 0x80);
    const VidSnesTransfer second = { .dest = 0x34, .source = shorter ? 0x000920 : 0x000900,
                                     .size = (uint16_t)(shorter ? 0x14E0 : 0x1500) };
    vid_snes_transfer(pf, PF_AT(shorter ? 0x7E30D2 : 0x7E30E6), &second, &again);
    if (!shorter) {
        c->pc = 0x30E9;
        return;
    }
    m_tdc(s);
    m_xba(s);
    m_rts(s);
}

bool scene_init_video_present(Snes *s) { return m_code_matches(s, 0x7E30F9, 126, 0x4F3F340A8EB09E78ull); }

// $7E:30F9
/* The scenes' video setup, written through a direct page on the register block:
 * forced blank, sprite graphics at VRAM $4000, a black fixed color, and
 * everything else -- the mode, mosaic, tilemaps,
 * character bases, scroll, Mode 7 and the windows -- cleared. The sprite list
 * pointer at $13 is set first, with the caller's direct page still in place. */
void scene_init_video(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    m_write16(s, m_dp(s, 0x13), m_ldx(s, 0x0900));
    m_sep(s, CPU_FLAG_M);
    m_rep(s, CPU_FLAG_X);
    m_push16(s, M_DP_PPU);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);

    m_lda8(s, 0x80);
    vid_set_display(pf, PF_AT(0x7E3108), &(VidDisplay){ .blank = true });
    m_lda8(s, 0x02);
    VidObjConfig obj = vid_obj_config_decode(0x02);
    vid_set_obj_config(pf, PF_AT(0x7E310C), &obj);
    m_lda8(s, 0xE0);
    vid_set_fixed_color(pf, PF_AT(0x7E3110), VID_RED | VID_GREEN | VID_BLUE, 0);

    m_ldx(s, 0x0000);
    m_tdc(s);
    m_xba(s);
    m_txa(s);
    vid_set_oam_address(pf, PF_AT(0x7E3118), 0x0000, false);
    VidMode mode = vid_mode_decode(0x00);
    vid_set_mode(pf, PF_AT(0x7E311A), &mode);
    vid_set_mosaic(pf, PF_AT(0x7E311C), 0x00, 1);
    static const uint32_t map_sites[4] = { 0x7E311E, 0x7E311E, 0x7E3120, 0x7E3120 };
    for (int bg = 0; bg < 4; bg++) {
        VidBg b = { 0 };
        vid_bg_map_decode(&b, 0x00);
        vid_set_bg_map(pf, PF_AT(map_sites[bg]), (VidLayer)bg, &b);
    }
    vid_set_bg_chars(pf, PF_AT(0x7E3122), VID_BG1, 0x0000, 0x0000);
    vid_set_bg_chars(pf, PF_AT(0x7E3122), VID_BG3, 0x0000, 0x0000);
    for (int bg = 0; bg < 4; bg++)
        vid_set_scroll(pf, PF_AT((uint32_t)(0x7E3124 + 8 * bg)), (VidLayer)bg, 0, 0,
                       VID_SCROLL_H | VID_SCROLL_V);
    vid_begin_vram_uploads(pf, PF_AT(0x7E3144), &low_byte_port, PF_GAME(0), NULL);
    vid_set_mode7(pf, PF_AT(0x7E3146), &(VidMode7){ 0 }, VID_M7_SELECT);
    vid_set_mode7(pf, PF_AT(0x7E3148), &(VidMode7){ 0 }, VID_M7_MATRIX);
    vid_set_mode7(pf, PF_AT(0x7E3158), &(VidMode7){ 0 }, VID_M7_CENTER);
    vid_set_palette_address(pf, PF_AT(0x7E315E), 0x00);
    static const uint32_t window_fields[9] = {
        VID_WINDOWS_BG1_BG2, VID_WINDOWS_BG3_BG4, VID_WINDOWS_OBJ_BACK,
        VID_WINDOWS_W1_LEFT, VID_WINDOWS_W1_RIGHT, VID_WINDOWS_W2_LEFT, VID_WINDOWS_W2_RIGHT,
        VID_WINDOWS_BG_LOGIC, VID_WINDOWS_OBJ_LOGIC,
    };
    VidWindows w = { 0 };
    uint32_t fields = 0;
    for (int i = 0; i < 9; i++) {
        vid_windows_decode(&w, window_fields[i], 0x00);
        fields |= window_fields[i];
    }
    vid_set_windows(pf, PF_AT(0x7E3160), &w, fields);
    vid_set_screens(pf, PF_AT(0x7E316A), 0x00, 0x00, VID_SCREEN_MAIN | VID_SCREEN_SUB);
    vid_windows_decode(&w, VID_WINDOWS_MAIN, 0x00);
    vid_windows_decode(&w, VID_WINDOWS_SUB, 0x00);
    vid_set_windows(pf, PF_AT(0x7E316C), &w, VID_WINDOWS_MAIN | VID_WINDOWS_SUB);
    VidColorMath math = { 0 };
    vid_color_math_decode(&math, VID_MATH_CONTROL, 0x00);
    vid_set_color_math(pf, PF_AT(0x7E316E), &math, VID_MATH_CONTROL);
    vid_color_math_decode(&math, VID_MATH_LAYERS, 0x00);
    vid_set_color_math(pf, PF_AT(0x7E316E), &math, VID_MATH_LAYERS);
    vid_set_fixed_color(pf, PF_AT(0x7E3170), 0, 0);   /* every channel bit clear, not black */
    vid_set_video_config(pf, PF_AT(0x7E3170), &(VidVideoConfig){ 0 });

    m_push16(s, 0x0000);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    m_rts(s);
}

bool scene_upload_sprites_present(Snes *s) { return m_code_matches(s, 0x7E380E, 53, 0x5555FCBF0F118757ull); }

// $7E:380E
/* Send the sprite table at $00:0700 to OAM, then point the sprite writers back
 * at its start: OAM address $10 with the priority rotation on, the entry index
 * at $11 cleared and the high-table pointer at $13 back to $0900.
 *
 * The original writes the address register's high byte first; the call writes
 * the low one first, which leaves the same address and rotation. */
void scene_upload_sprites(Snes *s)
{
    Platform *pf = m_platform(s);
    /* STX $2102 : STZ $4374 : STX $4370 : STX $4372 : STX $4375 : STA $420B */
    static const uint8_t steps[] = {
        VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_SOURCE_BANK, VID_STEP_CONTROL,
        VID_STEP_DEST,      VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,   VID_STEP_SIZE_LO,
        VID_STEP_SIZE_HI,   VID_STEP_START,     VID_STEP_END,
    };
    static const VidSnesXfer x = { SCENE_DMA, DMA_ONE_REGISTER, steps, NULL };
    m_ldx(s, 0x0000);
    m_ldx(s, 0x0400);
    m_ldx(s, 0x0700);
    m_ldx(s, 0x0220);
    m_lda8(s, 0x80);
    vid_upload_oam(pf, PF_AT(0x7E3811), PF_GAME(0x000700), &x);
    m_lda8(s, 0x80);
    uint8_t first = m_lda8(s, m_read8(s, m_dp(s, 0x10)));
    vid_set_oam_address(pf, PF_AT(0x7E3830), first, true);
    m_write16(s, m_dp(s, 0x11), m_ldx(s, 0x0000));
    m_write16(s, m_dp(s, 0x13), m_ldx(s, 0x0900));
    m_rts(s);
}

bool scene_clear_sprite_table_present(Snes *s)
{
    return m_code_matches(s, 0x7E3843, 45, 0x36CEE4DC10A522A8ull);
}

// $7E:3843
/* Hide every sprite: with the Mode 7 multiplier set to 1 times -32, the product
 * the PPU offers is $E0, and a transfer in the other direction copies that byte
 * over the $200 bytes of the sprite table at $00:0700 -- the Y position every
 * entry then has is off the bottom of the screen. The transfer reads a register
 * rather than writing one, so it goes through the SNES-only escape; only the
 * two matrix values it leaves behind are video state. */
void scene_clear_sprite_table(Snes *s)
{
    Platform *pf = m_platform(s);
    /* STZ $4374 : STA $4371 : STX $4372 : STX $4375 : STA $4370 : STA $420B */
    static const uint8_t steps[] = {
        VID_STEP_SOURCE_BANK, VID_STEP_DEST,    VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
        VID_STEP_SIZE_LO,     VID_STEP_SIZE_HI, VID_STEP_CONTROL,   VID_STEP_START,
        VID_STEP_END,
    };
    static const VidSnesXfer x = { SCENE_DMA, DMA_FROM_REGISTER, steps, NULL };
    m_lda8(s, 0x01);
    vid_set_mode7(pf, PF_AT(0x7E3845), &(VidMode7){ .a = 0x0001 }, VID_M7_MATRIX_A);
    m_lda8(s, 0xE0);
    vid_set_mode7(pf, PF_AT(0x7E384D), &(VidMode7){ .b = (int16_t)0xE000 }, VID_M7_MATRIX_B_HIGH);
    m_lda8(s, 0x34);
    m_ldx(s, 0x0700);
    m_ldx(s, 0x0200);
    m_lda8(s, 0x80);
    const VidSnesTransfer t = { .dest = 0x34, .source = 0x000700, .size = 0x0200 };
    vid_snes_transfer(pf, PF_AT(0x7E3850), &t, &x);
    vid_set_mode7(pf, PF_AT(0x7E386C), &(VidMode7){ .b = 0 }, VID_M7_MATRIX_B_HIGH);
    m_rts(s);
}

bool scene_upload_palette_present(Snes *s)
{
    return m_code_matches(s, 0x7E388B, 36, 0x84B3B18FAFEEAE44ull);
}

// $7E:388B
/* Send the working palette at $7E:2200 to CGRAM. */
void scene_upload_palette(Snes *s)
{
    Platform *pf = m_platform(s);
    /* STA $4370 : STA $2121 : STA $4374 : STA $4371 : STX $4372 : STX $4375 : STA $420B */
    static const uint8_t steps[] = {
        VID_STEP_CONTROL,   VID_STEP_TARGET_LO, VID_STEP_SOURCE_BANK, VID_STEP_DEST,
        VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SIZE_LO,     VID_STEP_SIZE_HI,
        VID_STEP_START,     VID_STEP_END,
    };
    static const VidSnesXfer x = { SCENE_DMA, DMA_ONE_REGISTER, steps, NULL };
    m_lda8(s, 0x00);
    m_lda8(s, 0x7E);
    m_lda8(s, 0x22);
    m_ldx(s, 0x2200);
    m_ldx(s, 0x0200);
    m_lda8(s, 0x80);
    vid_upload_palette(pf, PF_AT(0x7E388D), 0, PF_GAME(0x7E2200), 256, &x);
    m_rts(s);
}

bool scene_transfer_present(Snes *s) { return m_code_matches(s, 0x7E38E7, 36, 0xDD4B67DA62CC8E67ull); }

// $7E:38E7
/* The program's own transfer, described by the cells the caller fills: the
 * channel's mode in $00:00F0 and its destination register in $00:00F2, the
 * source at $0303-$0305 and the size at $0306. Both mode and destination are
 * game data, so this goes through the SNES-only escape; the destinations the
 * movies reach are the VRAM data port. Returns with RTL. */
void scene_transfer(Snes *s)
{
    Platform *pf = m_platform(s);
    /* STA $4370 : STA $4371 : STX $4372 : STA $4374 : STX $4375 : STA $420B */
    static const uint8_t steps[] = {
        VID_STEP_CONTROL,     VID_STEP_DEST,    VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
        VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO, VID_STEP_SIZE_HI,   VID_STEP_START,
        VID_STEP_END,
    };
    uint8_t control = m_lda8(s, abs8(s, 0x00F0));
    uint8_t dest = m_lda8(s, abs8(s, 0x00F2));
    uint16_t source = m_ldx(s, m_read16(s, m_abs(s, 0x0303)));
    uint8_t bank = m_lda8(s, abs8(s, 0x0305));
    uint16_t size = m_ldx(s, m_read16(s, m_abs(s, 0x0306)));
    m_lda8(s, 0x80);
    const VidSnesXfer x = { SCENE_DMA, control, steps, NULL };
    const VidSnesTransfer t = { .dest = dest, .source = (uint32_t)bank << 16 | source, .size = size };
    vid_snes_transfer(pf, PF_AT(0x7E38EA), &t, &x);
    m_rtl(s);
}

/* The graphics the title screen loads, one VRAM address per block. The six
 * stretches share a guard over the whole run of code they lie in. */
bool scene_load_graphics_present(Snes *s) { return m_code_matches(s, 0x7E597B, 124, 0x49B5A7DC40436AD2ull); }

static void set_vram_address(Snes *s, uint16_t addr, uint32_t site, uint16_t stop)
{
    m_ldx(s, addr);
    vid_set_vram_address(m_platform(s), PF_AT(site), addr);
    s->cpu.pc = stop;
}

// $7E:597B
/* The title screen's graphics: the VRAM port a word at a time and the first
 * block's address. Stops at $7E:5986, where the loader carries on. */
void scene_set_vram_4000(Snes *s)
{
    m_lda8(s, 0x80);
    vid_begin_vram_uploads(m_platform(s), PF_AT(0x7E597D), &word_port, PF_GAME(0), NULL);
    set_vram_address(s, 0x4000, 0x7E5983, 0x5986);
}

// $7E:59A0
void scene_set_vram_5000(Snes *s) { set_vram_address(s, 0x5000, 0x7E59A3, 0x59A6); }
// $7E:59AA
void scene_set_vram_0000(Snes *s) { set_vram_address(s, 0x0000, 0x7E59AD, 0x59B0); }
// $7E:59BC
void scene_set_vram_6400(Snes *s) { set_vram_address(s, 0x6400, 0x7E59BF, 0x59C2); }
// $7E:59CC
void scene_set_vram_6000(Snes *s) { set_vram_address(s, 0x6000, 0x7E59CF, 0x59D2); }

// $7E:59D5
/* The last of the title screen's graphics: $F00 bytes of VRAM from $6480 filled
 * with the two bytes the transfer before this one repeated. It programs the
 * channel's mode, the VRAM address and the size, and nothing else, so the fill
 * reads on from wherever that transfer left the source -- an address this
 * routine never computes and no call could name. The SNES-only escape carries
 * it: `continue_channel` says the channel supplies what the steps don't, which
 * only a backend holding the channel can resolve. The VRAM port comes from what
 * the routine before left in A. */
void scene_fill_graphics_tail(Snes *s)
{
    Platform *pf = m_platform(s);
    /* STA $2115 : STA $4370 : STX $2116 : STX $4375 : STA $420B */
    static const uint8_t steps[] = {
        VID_STEP_CONTROL,  VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_SIZE_LO,
        VID_STEP_SIZE_HI,  VID_STEP_START,     VID_STEP_END,
    };
    m_write8(s, m_abs(s, 0x0305), 0x00);
    VidVramPort port = vid_vram_port_decode((uint8_t)s->cpu.a);
    vid_begin_vram_uploads(pf, PF_AT(0x7E59D8), &port, PF_GAME(0), NULL);
    m_lda8(s, 0x09);
    m_ldx(s, 0x6480);
    m_ldx(s, 0x0F00);
    m_lda8(s, 0x80);
    const VidSnesXfer x = { SCENE_DMA, (uint8_t)(DMA_WORDS | DMA_FIXED_BYTE), steps, NULL };
    const VidSnesTransfer t = { .size = 0x0F00, .target_reg = 0x16, .target = 0x6480,
                                .continue_channel = true };
    vid_snes_transfer(pf, PF_AT(0x7E59DD), &t, &x);
    s->cpu.pc = 0x59F1;
}

bool scene_setup_mode7_display_present(Snes *s)
{
    return m_code_matches(s, 0x7E5A59, 61, 0xCB90A7CBF9F435C1ull);
}

// $7E:5A59
/* The Mode 7 scene's display, written through a direct page on the register
 * block: mode 7 with the plane transparent outside it, BG1 alone on the main
 * screen and BG2 alone on the sub screen, the three tilemaps at VRAM $6400
 * (64 tiles wide), $6000 and $7000, BG1's and BG2's tiles at $5000, color math
 * halving the sub screen over BG1, the rotation center at (32, -64) and BG2's
 * horizontal scroll. Stops at $7E:5A96, where the scene goes on to its own
 * state. This is what the attract sequence's pendulum runs on, so every movie
 * reaches it. */
void scene_setup_mode7_display(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    c->d = m_lda16(s, M_DP_PPU);
    m_set_nz16(s, c->d);
    m_sep(s, CPU_FLAG_M);

    m_lda8(s, 0x07);
    VidMode mode = vid_mode_decode(0x07);
    vid_set_mode(pf, PF_AT(0x7E5A61), &mode);
    m_lda8(s, 0x80);
    vid_set_mode7(pf, PF_AT(0x7E5A65), &(VidMode7){ .outside = VID_M7_TRANSPARENT }, VID_M7_SELECT);
    m_ldx(s, 0x1001);
    vid_set_screens(pf, PF_AT(0x7E5A6A), 0x01, 0x10, VID_SCREEN_MAIN | VID_SCREEN_SUB);
    m_ldx(s, 0x6065);
    VidBg bg = { 0 };
    vid_bg_map_decode(&bg, 0x65);
    vid_set_bg_map(pf, PF_AT(0x7E5A6F), VID_BG1, &bg);
    vid_bg_map_decode(&bg, 0x60);
    vid_set_bg_map(pf, PF_AT(0x7E5A6F), VID_BG2, &bg);
    m_lda8(s, 0x70);
    vid_bg_map_decode(&bg, 0x70);
    vid_set_bg_map(pf, PF_AT(0x7E5A73), VID_BG3, &bg);
    m_lda8(s, 0x55);
    vid_set_bg_chars(pf, PF_AT(0x7E5A77), VID_BG1, 0x5000, 0x5000);
    m_ldx(s, (uint16_t)(0x02 | 0x21 << 8));   /* the two color-math bytes in one store */
    VidColorMath math = { 0 };
    vid_color_math_decode(&math, VID_MATH_CONTROL, 0x02);
    vid_color_math_decode(&math, VID_MATH_LAYERS, 0x21);
    vid_set_color_math(pf, PF_AT(0x7E5A7C), &math, VID_MATH_CONTROL | VID_MATH_LAYERS);
    m_lda8(s, 0x20);
    m_lda8(s, 0x00);
    m_lda8(s, 0xC0);
    m_lda8(s, 0xFF);
    vid_set_mode7(pf, PF_AT(0x7E5A80), &(VidMode7){ .cx = 0x0020, .cy = (int16_t)0xFFC0 }, VID_M7_CENTER);
    m_lda8(s, 0x40);
    m_lda8(s, 0xFF);
    vid_set_scroll(pf, PF_AT(0x7E5A90), VID_BG2, 0xFF40, 0, VID_SCROLL_H);
    c->pc = 0x5A96;
}

bool scene_upload_tiles_through_port_present(Snes *s)
{
    return m_code_matches(s, 0x7E5AB6, 79, 0x2270DB27658DB4FFull);
}

// $7E:5AB6
/* Build the title's first $1000 VRAM words by hand: with the work RAM port at
 * $7E:0200 and the VRAM port stepping after the low byte, each of 32 rows takes
 * 32 low bytes from work RAM (the high byte of every pair is read and dropped)
 * and then leaves 96 words blank. The tilemap that goes with them follows as a
 * transfer of $1000 bytes from $7E:350B. */
void scene_upload_tiles_through_port(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    /* STX $4370 : STX $4372 : STA $4374 : STX $4375 : STA $420B */
    static const uint8_t steps[] = {
        VID_STEP_CONTROL,   VID_STEP_DEST,        VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
        VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,   VID_STEP_START,
        VID_STEP_END,
    };
    static const VidSnesXfer x = { SCENE_DMA, DMA_FIXED_BYTE, steps, NULL };
    m_write16(s, m_abs(s, WRAM_ADDR), m_ldx(s, 0x0200));
    m_lda8(s, 0x00);
    vid_begin_vram_uploads(pf, PF_AT(0x7E5ABE), &low_byte_port, PF_GAME(0), NULL);
    m_ldx(s, 0x0000);
    vid_set_vram_address(pf, PF_AT(0x7E5AC4), 0x0000);
    m_ldy(s, 0x0020);
    m_tyx(s);
    do {
        do {
            vid_write_vram_byte(pf, PF_AT(0x7E5ACE), false, m_lda8(s, m_read8(s, m_abs(s, WRAM_PORT))));
            m_lda8(s, m_read8(s, m_abs(s, WRAM_PORT)));
            m_ldx(s, (uint16_t)(c->x - 1));
        } while (c->x != 0);
        m_lda8(s, 0x00);
        m_ldx(s, 0x0060);
        do {
            vid_write_vram_byte(pf, PF_AT(0x7E5ADC), false, (uint8_t)c->a);
            m_ldx(s, (uint16_t)(c->x - 1));
        } while (c->x != 0);
        m_ldx(s, 0x0020);
        m_ldy(s, (uint16_t)(c->y - 1));
    } while (c->y != 0);

    m_ldx(s, 0x1808);
    m_ldx(s, 0x350B);
    m_lda8(s, 0x7E);
    m_ldx(s, 0x1000);
    m_lda8(s, 0x80);
    /* The loop above left the VRAM address at $1000 and the transfer carries on
     * from there without programming it again. It repeats two bytes into the
     * data port's low half alone, which the upload call models as words, so it
     * goes through the SNES-only escape. */
    const VidSnesTransfer t = { .dest = 0x18, .source = 0x7E350B, .size = 0x1000 };
    vid_snes_transfer(pf, PF_AT(0x7E5AEB), &t, &x);
    m_rts(s);
}

bool scene_set_mode7_matrix_present(Snes *s)
{
    return m_code_matches(s, 0x7E5C57, 65, 0xA13E0C2757626FD3ull);
}

// $7E:5C57
/* Copy the Mode 7 matrix the scene keeps at $5A-$61 and BG1's scroll (which in
 * Mode 7 is the plane's own) from $0A02-$0A05 to the hardware. */
void scene_set_mode7_matrix(Snes *s)
{
    Platform *pf = m_platform(s);
    uint8_t v[8];
    for (int i = 0; i < 8; i++)
        v[i] = m_lda8(s, m_read8(s, m_dp(s, (uint8_t)(0x5A + i))));
    VidMode7 m7 = { .a = (int16_t)(v[0] | v[1] << 8), .b = (int16_t)(v[2] | v[3] << 8),
                    .c = (int16_t)(v[4] | v[5] << 8), .d = (int16_t)(v[6] | v[7] << 8) };
    vid_set_mode7(pf, PF_AT(0x7E5C59), &m7, VID_M7_MATRIX);
    uint8_t sc[4];
    for (int i = 0; i < 4; i++)
        sc[i] = m_lda8(s, abs8(s, (uint16_t)(0x0A02 + i)));
    vid_set_scroll(pf, PF_AT(0x7E5C82), VID_BG1, (uint16_t)(sc[0] | sc[1] << 8),
                   (uint16_t)(sc[2] | sc[3] << 8), VID_SCROLL_H | VID_SCROLL_V);
    m_rts(s);
}

bool scene_show_layers_present(Snes *s) { return m_code_matches(s, 0x7E5C98, 40, 0x8FB5D2E8EEA2AD92ull); }

// $7E:5C98
/* A task of the title screen: while its counter at $68 is still below $72, show
 * BG1, BG2 and the sprites on the main screen and nothing on the sub screen,
 * point the task at $5B51 and leave through the task list's exit at $7E:39A7.
 * Past that the task falls through to $7E:5CBA, where this port stops. */
void scene_show_layers(Snes *s)
{
    Cpu *c = &s->cpu;
    m_lda16(s, m_read16(s, m_dp(s, 0x68)));
    m_lda16(s, (uint16_t)(c->a - 1));
    m_lda16(s, (uint16_t)(c->a - 1));
    m_cmp16(s, c->a, 0x0070);
    if (!m_flag(s, CPU_FLAG_N)) {
        c->pc = 0x5CBA;
        return;
    }
    m_lda16(s, 0x0013);
    vid_set_screens(m_platform(s), PF_AT(0x7E5CA4), 0x13, 0x00, VID_SCREEN_MAIN | VID_SCREEN_SUB);
    m_write16(s, m_abs(s, 0x0A20), m_lda16(s, 0x5B51));
    m_write16(s, m_dp(s, 0x62), m_lda16(s, 0x0038));
    m_write16(s, m_dp(s, 0x64), m_lda16(s, 0x003A));
    c->pc = 0x39A7;
}

bool scene_set_mode_2_present(Snes *s) { return m_code_matches(s, 0x7E5DA7, 13, 0xBAB77B39FA282F4Dull); }

// $7E:5DA7
/* Mode 2 (offset-per-tile) with no mosaic, and the task's next step at $04. */
void scene_set_mode_2(Snes *s)
{
    Platform *pf = m_platform(s);
    m_lda16(s, 0x0002);
    VidMode mode = vid_mode_decode(0x02);
    vid_set_mode(pf, PF_AT(0x7E5DAA), &mode);
    vid_set_mosaic(pf, PF_AT(0x7E5DAA), 0x00, 1);
    m_write16(s, m_dp(s, 0x04), m_lda16(s, 0xFFB9));
    m_rts(s);
}

bool scene_expand_tile_rows_present(Snes *s)
{
    return m_code_matches(s, 0x7E5E54, 164, 0xEE4E5864DAEE75ACull);
}

// $7E:5E54
/* Eight rows of a tile, written straight into VRAM's high bytes: each row takes
 * the words at X and X+$10 into $3B-$3E and shifts their top bits out four at a
 * time, so eight bytes of four interleaved bitplanes go to the data port. */
void scene_expand_tile_rows(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    m_write8(s, m_dp(s, 0x3F), m_lda8(s, 0x08));
    uint8_t rows;
    do {
        m_rep(s, CPU_FLAG_M);
        m_write16(s, m_dp(s, 0x3B), m_lda16(s, m_read16(s, m_abs(s, c->x))));
        m_write16(s, m_dp(s, 0x3D), m_lda16(s, m_read16(s, m_abs(s, (uint16_t)(c->x + 0x10)))));
        m_ldx(s, (uint16_t)(c->x + 2));
        m_sep(s, CPU_FLAG_M);
        for (int i = 0; i < 8; i++) {
            static const uint8_t cell[4] = { 0x3E, 0x3D, 0x3C, 0x3B };
            m_tdc(s);
            for (int k = 0; k < 4; k++) {
                asl_dp8(s, cell[k]);
                rol_a8(s);
            }
            vid_write_vram_byte(pf, PF_AT(0x7E5E75), true, (uint8_t)c->a);
        }
        rows = (uint8_t)(m_read8(s, m_dp(s, 0x3F)) - 1);
        m_write8(s, m_dp(s, 0x3F), rows);
        m_set_nz8(s, rows);
    } while (rows != 0);
    m_rts(s);
}

bool scene_upload_map_and_scroll_present(Snes *s)
{
    return m_code_matches(s, 0x7E6212, 54, 0x0DB9CC06A3C8236Dull);
}

// $7E:6212
/* The race's per-frame tilemap block: $80 bytes from $00:0C00 to VRAM $7000,
 * then BG3's horizontal scroll from $0044. */
void scene_upload_map_and_scroll(Snes *s)
{
    Platform *pf = m_platform(s);
    /* STX $2116 : STA $4370 : STA $4371 : STX $4372 : STA $4374 : STX $4375 :
     * STA $2115 : STA $420B */
    static const uint8_t steps[] = {
        VID_STEP_TARGET_LO,   VID_STEP_TARGET_HI, VID_STEP_CONTROL, VID_STEP_DEST,
        VID_STEP_SOURCE_LO,   VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO,
        VID_STEP_SIZE_HI,     VID_STEP_VRAM_PORT, VID_STEP_START,   VID_STEP_END,
    };
    static const VidSnesXfer x = { SCENE_DMA, DMA_WORDS, steps, &word_port };
    m_ldx(s, 0x7000);
    m_lda8(s, 0x01);
    m_lda8(s, 0x18);
    m_ldx(s, 0x0C00);
    m_lda8(s, 0x00);
    m_ldx(s, 0x0080);
    m_lda8(s, 0x80);
    vid_upload_vram(pf, PF_AT(0x7E6215), 0x7000, PF_GAME(0x000C00), 0x80, &x);
    uint8_t lo = m_lda8(s, abs8(s, 0x0044));
    uint8_t hi = m_lda8(s, abs8(s, 0x0045));
    vid_set_scroll(pf, PF_AT(0x7E623E), VID_BG3, (uint16_t)(lo | hi << 8), 0, VID_SCROLL_H);
    m_rts(s);
}
