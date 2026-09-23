/* Scenes: the display work of the program the boot at $C3:0014 decompresses to
 * $7E:3000 (the attract sequence and title screen, the jet bike race, and the
 * title again after a game over).
 *
 * The program writes the registers itself, so P2 has to name what each write
 * means. Every entry in RAM is guarded by a hash of its bytes, as the other
 * ports of the program are (scene/program.c): the same addresses hold other
 * code and data at other times. Each routine's work is a function of the
 * platform and the game's state; the entry wrappers leave the registers its
 * callers read (liveness follows the program's callers: the entries' don't-care
 * sets say which are dead) and stop where the program goes on.
 */
#include "scene/display.h"

#include "cpu_io.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "scene/scene.h"
#include "vars.h"
#include "wram.h"

/* The scenes upload over DMA channel 7. */
enum { SCENE_DMA = 7, DMA_ONE_REGISTER = 0x00, DMA_WORDS = 0x01, DMA_FIXED_BYTE = 0x08,
       DMA_FROM_REGISTER = 0x80 };

/* $2115 as the program sets it: a word at a time, stepping after the low byte
 * for the writes it makes through the port itself, after the high byte for the
 * transfers. */
static const VidVramPort low_byte_port = { .step = 1, .after_high = false };
static const VidVramPort word_port = { .step = 1, .after_high = true };

/* A transfer's register writes in the order the scene program's memory clear
 * makes them: the control and destination together (one 16-bit store), then the
 * source, the size and the start. */
static const uint8_t transfer_steps[] = {
    VID_STEP_CONTROL,     VID_STEP_DEST,    VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
    VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO, VID_STEP_SIZE_HI,   VID_STEP_START,
    VID_STEP_END,
};

/* Where the scene's sprite writers start again after an upload: the first
 * entry, and the first byte of the high table. */
enum { FIRST_HIGH_BYTE = 0x0900 };

/* The PPU's multiplier product ($2134), which a transfer from it copies. */
enum { MULTIPLIER_PRODUCT = 0x34 };

/* ---- the program at $7E:3000 --------------------------------------------------- */

bool scene_program_restart_present(Snes *s) { return m_code_matches(s, 0x7E302A, 14, 0x614033C5F8C83156ull); }

// $7E:302A
/* The program stops: interrupts and per-line effects off and the display
 * blanked, before it moves the stack and jumps to the reset entry. */
static void stop_display(Platform *pf)
{
    tm_enable_interrupts(pf, PF_AT(0x7E302F), 0);
    vid_enable_line_effects(pf, PF_AT(0x7E3032), 0);
    vid_set_display(pf, PF_AT(0x7E3035), &(VidDisplay){ .blank = true });
}

/* Stops at the call that follows, which the program has not been taken apart
 * past, with 8-bit A. */
void scene_program_restart(Snes *s)
{
    stop_display(m_platform(s));
    s->cpu.p |= CPU_FLAG_M;
    m_stop_at(s, 0x7E3038, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
}

bool scene_clear_work_ram_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E3092, 87, 0x2A1AE096D5B36F10ull);
}

/* scene_flags bit 7: the title is coming back, and the shorter clear keeps
 * what the scene has below $0920 past the direct page. */
enum { SCENE_FLAG_TITLE_AGAIN = 0x80 };

// $7E:3092
/* Clear the program's work RAM by copying the PPU multiplier's product out of
 * $2134: with the matrix set to (A, A) times 0 the product is 0, and two
 * transfers in the other direction spread it over $00:0000-$00:00FF and then
 * over either $00:0920 or $00:0900 upwards, depending on the scene's flags.
 * The transfers read a register rather than writing one, so they go through
 * the SNES-only escape; only the matrix they leave behind is video state. The
 * second transfer keeps the first one's mode, destination and bank, which this
 * port knows because it made them. Returns true for the shorter clear. */
static bool clear_work_ram(GameState *g, Platform *pf, uint8_t a)
{
    static const uint8_t again_steps[] = {
        VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SIZE_LO, VID_STEP_SIZE_HI, VID_STEP_START,
        VID_STEP_END,
    };
    const VidSnesXfer x = { SCENE_DMA, DMA_FROM_REGISTER, transfer_steps, NULL };
    const VidSnesXfer again = { SCENE_DMA, DMA_FROM_REGISTER, again_steps, NULL };
    vid_set_mode7(pf, PF_AT(0x7E3096), &(VidMode7){ .a = (int16_t)(a << 8 | a) }, VID_M7_MATRIX_A);
    vid_set_mode7(pf, PF_AT(0x7E309D), &(VidMode7){ .b = 0 }, VID_M7_MATRIX_B);
    const VidSnesTransfer first = { .dest = MULTIPLIER_PRODUCT, .source = 0x000000, .size = 0x0100 };
    vid_snes_transfer(pf, PF_AT(0x7E30B6), &first, &x);
    bool shorter = scene_flags(g) & SCENE_FLAG_TITLE_AGAIN;
    const VidSnesTransfer second = { .dest = MULTIPLIER_PRODUCT, .source = shorter ? 0x000920 : 0x000900,
                                     .size = (uint16_t)(shorter ? 0x14E0 : 0x1500) };
    vid_snes_transfer(pf, PF_AT(shorter ? 0x7E30D2 : 0x7E30E6), &second, &again);
    return shorter;
}

/* The shorter clear returns with A's high byte 0 and D back on the scene's
 * page; the longer one stops at $7E:30E9, where the program goes on. */
void scene_clear_work_ram(Snes *s)
{
    bool shorter = clear_work_ram(state_of(s), m_platform(s), (uint8_t)s->cpu.a);
    s->cpu.d = 0x0000;
    if (!shorter) {
        m_stop_at(s, 0x7E30E9, DC_A | DC_X | DC_Y | DC_C);
        return;
    }
    s->cpu.a = 0x0000;
    m_rts(s);
}

bool scene_init_video_present(Snes *s)
{
    return scene_on_scene_page(s) && m_code_matches(s, 0x7E30F9, 126, 0x4F3F340A8EB09E78ull);
}

// $7E:30F9
/* The scenes' video setup: forced blank, sprite graphics at VRAM $4000, a black
 * fixed color, and everything else -- the mode, mosaic, tilemaps, character
 * bases, scroll, Mode 7 and the windows -- cleared. The sprite writers' high
 * table pointer is set first. */
static void init_video(GameState *g, Platform *pf)
{
    set_scene_sprite_high_pointer(g, FIRST_HIGH_BYTE);
    vid_set_display(pf, PF_AT(0x7E3108), &(VidDisplay){ .blank = true });
    VidObjConfig obj = vid_obj_config_decode(0x02);
    vid_set_obj_config(pf, PF_AT(0x7E310C), &obj);
    vid_set_fixed_color(pf, PF_AT(0x7E3110), VID_RED | VID_GREEN | VID_BLUE, 0);

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
}

/* Written through a direct page on the register block, which it leaves on the
 * scene's page again; A's high byte ends 0 and 8-bit A, 16-bit X. */
void scene_init_video(Snes *s)
{
    init_video(state_of(s), m_platform(s));
    s->cpu.p = (uint8_t)((s->cpu.p | CPU_FLAG_M) & ~CPU_FLAG_X);
    s->cpu.a = 0x0000;
    m_rts(s);
}

bool scene_upload_sprites_present(Snes *s)
{
    return scene_on_scene_page(s) && m_code_matches(s, 0x7E380E, 53, 0x5555FCBF0F118757ull);
}

// $7E:380E
/* Send the sprite table at $00:0700 to OAM, then point the sprite writers back
 * at its start: OAM address the priority entry with the priority rotation on,
 * the entry index cleared and the high-table pointer back at the high table.
 *
 * The original writes the address register's high byte first; the call writes
 * the low one first, which leaves the same address and rotation. */
static void upload_sprites(GameState *g, Platform *pf)
{
    /* STX $2102 : STZ $4374 : STX $4370 : STX $4372 : STX $4375 : STA $420B */
    static const uint8_t steps[] = {
        VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_SOURCE_BANK, VID_STEP_CONTROL,
        VID_STEP_DEST,      VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,   VID_STEP_SIZE_LO,
        VID_STEP_SIZE_HI,   VID_STEP_START,     VID_STEP_END,
    };
    static const VidSnesXfer x = { SCENE_DMA, DMA_ONE_REGISTER, steps, NULL };
    vid_upload_oam(pf, PF_AT(0x7E3811), PF_GAME(0x000700), &x);
    vid_set_oam_address(pf, PF_AT(0x7E3830), scene_sprite_priority_start(g), true);
    set_scene_sprite_index(g, 0);
    set_scene_sprite_high_pointer(g, FIRST_HIGH_BYTE);
}

/* Nothing it leaves in the registers is read. */
void scene_upload_sprites(Snes *s)
{
    upload_sprites(state_of(s), m_platform(s));
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
static void clear_sprite_table(Platform *pf)
{
    /* STZ $4374 : STA $4371 : STX $4372 : STX $4375 : STA $4370 : STA $420B */
    static const uint8_t steps[] = {
        VID_STEP_SOURCE_BANK, VID_STEP_DEST,    VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
        VID_STEP_SIZE_LO,     VID_STEP_SIZE_HI, VID_STEP_CONTROL,   VID_STEP_START,
        VID_STEP_END,
    };
    static const VidSnesXfer x = { SCENE_DMA, DMA_FROM_REGISTER, steps, NULL };
    vid_set_mode7(pf, PF_AT(0x7E3845), &(VidMode7){ .a = 0x0001 }, VID_M7_MATRIX_A);
    vid_set_mode7(pf, PF_AT(0x7E384D), &(VidMode7){ .b = (int16_t)0xE000 }, VID_M7_MATRIX_B_HIGH);
    const VidSnesTransfer t = { .dest = MULTIPLIER_PRODUCT, .source = 0x000700, .size = 0x0200 };
    vid_snes_transfer(pf, PF_AT(0x7E3850), &t, &x);
    vid_set_mode7(pf, PF_AT(0x7E386C), &(VidMode7){ .b = 0 }, VID_M7_MATRIX_B_HIGH);
}

void scene_clear_sprite_table(Snes *s)
{
    clear_sprite_table(m_platform(s));
    m_rts(s);
}

bool scene_upload_palette_present(Snes *s)
{
    return m_code_matches(s, 0x7E388B, 36, 0x84B3B18FAFEEAE44ull);
}

// $7E:388B
/* Send the working palette at $7E:2200 to CGRAM. */
static void upload_palette(Platform *pf)
{
    /* STA $4370 : STA $2121 : STA $4374 : STA $4371 : STX $4372 : STX $4375 : STA $420B */
    static const uint8_t steps[] = {
        VID_STEP_CONTROL,   VID_STEP_TARGET_LO, VID_STEP_SOURCE_BANK, VID_STEP_DEST,
        VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SIZE_LO,     VID_STEP_SIZE_HI,
        VID_STEP_START,     VID_STEP_END,
    };
    static const VidSnesXfer x = { SCENE_DMA, DMA_ONE_REGISTER, steps, NULL };
    vid_upload_palette(pf, PF_AT(0x7E388D), 0, PF_GAME(0x7E2200), 256, &x);
}

void scene_upload_palette(Snes *s)
{
    upload_palette(m_platform(s));
    m_rts(s);
}

bool scene_transfer_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E38E7, 36, 0xDD4B67DA62CC8E67ull);
}

// $7E:38E7
/* The program's own transfer, described by cells the caller fills: the
 * channel's mode and destination register in the scene's first two scratch
 * words, and the source and size where the decompressor leaves them (its
 * destination and length). Both mode and destination are game data, so this
 * goes through the SNES-only escape; the destinations the movies reach are the
 * VRAM data port. */
static void transfer(const GameState *g, Platform *pf)
{
    /* STA $4370 : STA $4371 : STX $4372 : STA $4374 : STX $4375 : STA $420B */
    static const uint8_t steps[] = {
        VID_STEP_CONTROL,     VID_STEP_DEST,    VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
        VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO, VID_STEP_SIZE_HI,   VID_STEP_START,
        VID_STEP_END,
    };
    uint8_t control = scene_scratch8(g, scene_math_scene(), SCRATCH_A);
    uint8_t dest = scene_scratch8(g, scene_math_scene(), SCRATCH_B);
    const VidSnesXfer x = { SCENE_DMA, control, steps, NULL };
    const VidSnesTransfer t = { .dest = dest, .source = decompress_dest(g), .size = decompress_length(g) };
    vid_snes_transfer(pf, PF_AT(0x7E38EA), &t, &x);
}

/* Returns with RTL; nothing it leaves in the registers is read. */
void scene_transfer(Snes *s)
{
    transfer(state_of(s), m_platform(s));
    m_rtl(s);
}

/* The graphics the title screen loads, one VRAM address per block. The six
 * stretches share a guard over the whole run of code they lie in. */
bool scene_load_graphics_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E597B, 124, 0x49B5A7DC40436AD2ull);
}

/* LDX #addr : STX $2116, which is where the next VRAM writes go; the loader
 * carries on without reading the registers. */
static void set_vram_address(Snes *s, uint16_t addr, uint32_t site, uint32_t stop)
{
    vid_set_vram_address(m_platform(s), PF_AT(site), addr);
    m_stop_at(s, stop, DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C);
}

// $7E:597B
/* The title screen's graphics: the VRAM port a word at a time and the first
 * block's address. Stops at $7E:5986, where the loader carries on. */
void scene_set_vram_4000(Snes *s)
{
    vid_begin_vram_uploads(m_platform(s), PF_AT(0x7E597D), &word_port, PF_GAME(0), NULL);
    set_vram_address(s, 0x4000, 0x7E5983, 0x7E5986);
}

// $7E:59A0
void scene_set_vram_5000(Snes *s) { set_vram_address(s, 0x5000, 0x7E59A3, 0x7E59A6); }
// $7E:59AA
void scene_set_vram_0000(Snes *s) { set_vram_address(s, 0x0000, 0x7E59AD, 0x7E59B0); }
// $7E:59BC
void scene_set_vram_6400(Snes *s) { set_vram_address(s, 0x6400, 0x7E59BF, 0x7E59C2); }
// $7E:59CC
void scene_set_vram_6000(Snes *s) { set_vram_address(s, 0x6000, 0x7E59CF, 0x7E59D2); }

// $7E:59D5
/* The last of the title screen's graphics: $F00 bytes of VRAM from $6480 filled
 * with the two bytes the transfer before this one repeated. It programs the
 * channel's mode, the VRAM address and the size, and nothing else, so the fill
 * reads on from wherever that transfer left the source -- an address this
 * routine never computes and no call could name. The SNES-only escape carries
 * it: `continue_channel` says the channel supplies what the steps don't, which
 * only a backend holding the channel can resolve. The VRAM port is the setting
 * the routine before left (`port`), and the decompressor's destination is moved
 * to bank 0 first. */
static void fill_graphics_tail(GameState *g, Platform *pf, uint8_t port)
{
    /* STA $2115 : STA $4370 : STX $2116 : STX $4375 : STA $420B */
    static const uint8_t steps[] = {
        VID_STEP_CONTROL,  VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_SIZE_LO,
        VID_STEP_SIZE_HI,  VID_STEP_START,     VID_STEP_END,
    };
    set_decompress_dest(g, decompress_dest(g) & 0x00FFFF);
    VidVramPort vram_port = vid_vram_port_decode(port);
    vid_begin_vram_uploads(pf, PF_AT(0x7E59D8), &vram_port, PF_GAME(0), NULL);
    const VidSnesXfer x = { SCENE_DMA, (uint8_t)(DMA_WORDS | DMA_FIXED_BYTE), steps, NULL };
    const VidSnesTransfer t = { .size = 0x0F00, .target_reg = 0x16, .target = 0x6480,
                                .continue_channel = true };
    vid_snes_transfer(pf, PF_AT(0x7E59DD), &t, &x);
}

/* Stops at $7E:59F1, where the loader goes on. */
void scene_fill_graphics_tail(Snes *s)
{
    fill_graphics_tail(state_of(s), m_platform(s), (uint8_t)s->cpu.a);
    m_stop_at(s, 0x7E59F1, DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C);
}

bool scene_setup_mode7_display_present(Snes *s)
{
    return m_code_matches(s, 0x7E5A59, 61, 0xCB90A7CBF9F435C1ull);
}

// $7E:5A59
/* The Mode 7 scene's display: mode 7 with the plane transparent outside it, BG1
 * alone on the main screen and the sprites alone on the sub screen, the three
 * tilemaps at VRAM $6400 (64 tiles wide), $6000 and $7000, BG1's and BG2's
 * tiles at $5000, color math halving the sub screen over BG1, the rotation
 * center at (32, -64) and BG2's horizontal scroll. This is what the attract
 * sequence's pendulum runs on, so every movie reaches it. */
static void setup_mode7_display(Platform *pf)
{
    VidMode mode = vid_mode_decode(0x07);
    vid_set_mode(pf, PF_AT(0x7E5A61), &mode);
    vid_set_mode7(pf, PF_AT(0x7E5A65), &(VidMode7){ .outside = VID_M7_TRANSPARENT }, VID_M7_SELECT);
    vid_set_screens(pf, PF_AT(0x7E5A6A), 0x01, 0x10, VID_SCREEN_MAIN | VID_SCREEN_SUB);
    VidBg bg = { 0 };
    vid_bg_map_decode(&bg, 0x65);
    vid_set_bg_map(pf, PF_AT(0x7E5A6F), VID_BG1, &bg);
    vid_bg_map_decode(&bg, 0x60);
    vid_set_bg_map(pf, PF_AT(0x7E5A6F), VID_BG2, &bg);
    vid_bg_map_decode(&bg, 0x70);
    vid_set_bg_map(pf, PF_AT(0x7E5A73), VID_BG3, &bg);
    vid_set_bg_chars(pf, PF_AT(0x7E5A77), VID_BG1, 0x5000, 0x5000);
    VidColorMath math = { 0 };
    vid_color_math_decode(&math, VID_MATH_CONTROL, 0x02);
    vid_color_math_decode(&math, VID_MATH_LAYERS, 0x21);
    vid_set_color_math(pf, PF_AT(0x7E5A7C), &math, VID_MATH_CONTROL | VID_MATH_LAYERS);
    vid_set_mode7(pf, PF_AT(0x7E5A80), &(VidMode7){ .cx = 0x0020, .cy = (int16_t)0xFFC0 }, VID_M7_CENTER);
    vid_set_scroll(pf, PF_AT(0x7E5A90), VID_BG2, 0xFF40, 0, VID_SCROLL_H);
}

/* Written through a direct page on the register block, whose address it
 * leaves in A's high byte (8-bit A); stops at $7E:5A96, where the scene goes on
 * to its own state. */
void scene_setup_mode7_display(Snes *s)
{
    setup_mode7_display(m_platform(s));
    s->cpu.p |= CPU_FLAG_M;
    s->cpu.a = M_DP_PPU;
    m_stop_at(s, 0x7E5A96, DC_AL | DC_X | DC_Y | DC_N | DC_Z | DC_C | DC_D);
}

bool scene_upload_tiles_through_port_present(Snes *s)
{
    return m_bank_reaches_io(s) && m_code_matches(s, 0x7E5AB6, 79, 0x2270DB27658DB4FFull);
}

/* The title's first tiles: 32 rows, each 32 bytes wide, then 96 words blank. */
enum { TITLE_ROWS = 32, TITLE_ROW_BYTES = 32, TITLE_ROW_GAP = 96 };

// $7E:5AB6
/* Build the title's first $1000 VRAM words by hand: with the work RAM port at
 * $7E:0200 and the VRAM port stepping after the low byte, each of 32 rows takes
 * 32 low bytes from work RAM (the high byte of every pair is read and dropped)
 * and then leaves 96 words blank. The tilemap that goes with them follows as a
 * transfer of $1000 bytes from $7E:350B. */
static void upload_title_tiles(GameState *g, Platform *pf)
{
    /* STX $4370 : STX $4372 : STA $4374 : STX $4375 : STA $420B */
    static const uint8_t steps[] = {
        VID_STEP_CONTROL,   VID_STEP_DEST,        VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
        VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,   VID_STEP_START,
        VID_STEP_END,
    };
    static const VidSnesXfer x = { SCENE_DMA, DMA_FIXED_BYTE, steps, NULL };
    hw_port_seek_address(g, 0x0200);
    vid_begin_vram_uploads(pf, PF_AT(0x7E5ABE), &low_byte_port, PF_GAME(0), NULL);
    vid_set_vram_address(pf, PF_AT(0x7E5AC4), 0x0000);
    for (int row = 0; row < TITLE_ROWS; row++) {
        for (int i = 0; i < TITLE_ROW_BYTES; i++) {
            vid_write_vram_byte(pf, PF_AT(0x7E5ACE), false, hw_port_read(g));
            hw_port_read(g);
        }
        for (int i = 0; i < TITLE_ROW_GAP; i++)
            vid_write_vram_byte(pf, PF_AT(0x7E5ADC), false, 0x00);
    }
    /* The loop above left the VRAM address at $1000 and the transfer carries on
     * from there without programming it again. It repeats two bytes into the
     * data port's low half alone, which the upload call models as words, so it
     * goes through the SNES-only escape. */
    const VidSnesTransfer t = { .dest = 0x18, .source = 0x7E350B, .size = 0x1000 };
    vid_snes_transfer(pf, PF_AT(0x7E5AEB), &t, &x);
}

/* A comes back with the transfer's start in its low byte. */
void scene_upload_tiles_through_port(Snes *s)
{
    upload_title_tiles(state_of(s), m_platform(s));
    m_set_a8(s, 0x80);
    m_rts(s);
}

bool scene_set_mode7_matrix_present(Snes *s)
{
    return scene_on_scene_page_low_bank(s) && m_code_matches(s, 0x7E5C57, 65, 0xA13E0C2757626FD3ull);
}

/* Channel 0's second and third registers are the Mode 7 plane's scroll in the
 * pendulum scene. */
enum { PLANE_SCROLL_X_BYTE = 2, PLANE_SCROLL_Y_BYTE = 4 };

// $7E:5C57
/* Copy the scene's Mode 7 matrix and the plane's scroll (BG1's, which in Mode 7
 * is the plane's own) to the hardware. */
static void set_mode7_matrix(const GameState *g, Platform *pf)
{
    VidMode7 m7 = { .a = (int16_t)scene_mode7_matrix_a(g), .b = (int16_t)scene_mode7_matrix_b(g),
                    .c = (int16_t)scene_mode7_matrix_c(g), .d = (int16_t)scene_mode7_matrix_d(g) };
    vid_set_mode7(pf, PF_AT(0x7E5C59), &m7, VID_M7_MATRIX);
    uint16_t x = (uint16_t)(scene_channels_registers_byte(g, 0, PLANE_SCROLL_X_BYTE) |
                            scene_channels_registers_byte(g, 0, PLANE_SCROLL_X_BYTE + 1) << 8);
    uint16_t y = (uint16_t)(scene_channels_registers_byte(g, 0, PLANE_SCROLL_Y_BYTE) |
                            scene_channels_registers_byte(g, 0, PLANE_SCROLL_Y_BYTE + 1) << 8);
    vid_set_scroll(pf, PF_AT(0x7E5C82), VID_BG1, x, y, VID_SCROLL_H | VID_SCROLL_V);
}

/* Nothing it leaves in the registers is read. */
void scene_set_mode7_matrix(Snes *s)
{
    set_mode7_matrix(state_of(s), m_platform(s));
    m_rts(s);
}

bool scene_show_layers_present(Snes *s)
{
    return scene_on_scene_page_low_bank(s) && m_code_matches(s, 0x7E5C98, 40, 0x8FB5D2E8EEA2AD92ull);
}

/* What the title's layer task hands channel 0 next, and where it leaves. */
enum { TITLE_TIMER_LIMIT = 0x72, TITLE_NEXT_SCRIPT = 0x5B51 };
enum { TITLE_SHOW_CELL_1 = 0x0038, TITLE_SHOW_CELL_2 = 0x003A };

// $7E:5C98
/* A task of the title screen: while its timer is still below $72, show BG1, BG2
 * and the sprites on the main screen and nothing on the sub screen, and hand
 * channel 0 its next script. True if it did. */
static bool show_layers(GameState *g, Platform *pf)
{
    if (!((uint16_t)(scene_title_timer(g) - TITLE_TIMER_LIMIT) & 0x8000))
        return false;
    vid_set_screens(pf, PF_AT(0x7E5CA4), 0x13, 0x00, VID_SCREEN_MAIN | VID_SCREEN_SUB);
    set_scene_channels_script(g, 0, TITLE_NEXT_SCRIPT);
    set_scene_title_show_cell_1(g, TITLE_SHOW_CELL_1);
    set_scene_title_show_cell_2(g, TITLE_SHOW_CELL_2);
    return true;
}

/* Having shown them it leaves through the task list's exit at $7E:39A7; past
 * the limit the task falls through to $7E:5CBA, where this port stops, with A
 * the timer less two. */
void scene_show_layers(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t less_two = (uint16_t)(scene_title_timer(g) - 2);
    if (!show_layers(g, m_platform(s))) {
        s->cpu.a = less_two;
        m_stop_at(s, 0x7E5CBA, DC_X | DC_N | DC_V | DC_Z | DC_C);
        return;
    }
    s->cpu.a = TITLE_SHOW_CELL_2;
    m_stop_at(s, 0x7E39A7, DC_AL | DC_X | DC_N | DC_V | DC_Z | DC_C);
}

/* A task's page is in low RAM: its step cell, at least, below $2000. */
enum { TASK_PAGES_END = 0x1FFB };

bool scene_set_mode_2_present(Snes *s)
{
    return s->cpu.d < TASK_PAGES_END && m_code_matches(s, 0x7E5DA7, 13, 0xBAB77B39FA282F4Dull);
}

/* A task's page: its next step at $04, which the task list's caller jumps to. */
enum { TASK_NEXT_STEP = 0x04, MODE_2_NEXT_STEP = 0xFFB9 };

// $7E:5DA7
/* Mode 2 (offset-per-tile) with no mosaic, and the task's next step. The task
 * is on a page the caller chooses, which the map has no name for. */
static void set_mode_2(GameState *g, Platform *pf, uint16_t task_page)
{
    VidMode mode = vid_mode_decode(0x02);
    vid_set_mode(pf, PF_AT(0x7E5DAA), &mode);
    vid_set_mosaic(pf, PF_AT(0x7E5DAA), 0x00, 1);
    wram_write16(g, 0x7E0000u | (uint16_t)(task_page + TASK_NEXT_STEP), MODE_2_NEXT_STEP);
}

/* The task's page is D. */
void scene_set_mode_2(Snes *s)
{
    set_mode_2(state_of(s), m_platform(s), s->cpu.d);
    m_rts(s);
}

bool scene_expand_tile_rows_present(Snes *s)
{
    return scene_on_scene_page(s) && m_code_matches(s, 0x7E5E54, 164, 0xEE4E5864DAEE75ACull);
}

enum { TILE_ROWS = 8, PLANE_PAIR_OFFSET = 0x10 };
enum { ROW_COUNTER = 4 };   /* the byte after the four plane bytes in the scratch */

// $7E:5E54
/* Eight rows of a tile, written straight into VRAM's high bytes: each row takes
 * the words at `from` and 16 bytes on (the tile's two plane pairs, in the
 * data bank) into the bitplane scratch and shifts their top bits out four at a
 * time, so eight bytes of four interleaved bitplanes go to the data port.
 * Returns where the rows end. */
static uint16_t expand_tile_rows(GameState *g, Platform *pf, uint8_t bank, uint16_t from)
{
    set_scene_bitplane_scratch_byte(g, ROW_COUNTER, TILE_ROWS);
    uint8_t rows;
    do {
        uint16_t low = wram_read16(g, (uint32_t)bank << 16 | from);
        for (unsigned i = 0; i < 2; i++)
            set_scene_bitplane_scratch_byte(g, i, (uint8_t)(low >> (8 * i)));
        uint16_t high = wram_read16(g, (uint32_t)bank << 16 | (uint16_t)(from + PLANE_PAIR_OFFSET));
        for (unsigned i = 0; i < 2; i++)
            set_scene_bitplane_scratch_byte(g, 2 + i, (uint8_t)(high >> (8 * i)));
        from = (uint16_t)(from + 2);
        for (int pixel = 0; pixel < 8; pixel++) {
            uint8_t v = 0;
            for (unsigned k = 0; k < 4; k++) {
                unsigned plane = 3 - k;
                uint8_t bits = scene_bitplane_scratch_byte(g, plane);
                set_scene_bitplane_scratch_byte(g, plane, (uint8_t)(bits << 1));
                v = (uint8_t)(v << 1 | (bits >> 7));
            }
            vid_write_vram_byte(pf, PF_AT(0x7E5E75), true, v);
        }
        rows = (uint8_t)(scene_bitplane_scratch_byte(g, ROW_COUNTER) - 1);
        set_scene_bitplane_scratch_byte(g, ROW_COUNTER, rows);
    } while (rows != 0);
    return from;
}

/* X is the tile's address, in the data bank, and back it is where the rows
 * end. */
void scene_expand_tile_rows(Snes *s)
{
    s->cpu.x = expand_tile_rows(state_of(s), m_platform(s), s->cpu.db, s->cpu.x);
    m_rts(s);
}

bool scene_upload_map_and_scroll_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E6212, 54, 0x0DB9CC06A3C8236Dull);
}

// $7E:6212
/* The race's per-frame tilemap block: $80 bytes of the tilemap buffer to VRAM
 * $7000, then BG3's horizontal scroll. */
static void upload_map_and_scroll(const GameState *g, Platform *pf)
{
    /* STX $2116 : STA $4370 : STA $4371 : STX $4372 : STA $4374 : STX $4375 :
     * STA $2115 : STA $420B */
    static const uint8_t steps[] = {
        VID_STEP_TARGET_LO,   VID_STEP_TARGET_HI, VID_STEP_CONTROL, VID_STEP_DEST,
        VID_STEP_SOURCE_LO,   VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO,
        VID_STEP_SIZE_HI,     VID_STEP_VRAM_PORT, VID_STEP_START,   VID_STEP_END,
    };
    static const VidSnesXfer x = { SCENE_DMA, DMA_WORDS, steps, &word_port };
    vid_upload_vram(pf, PF_AT(0x7E6215), 0x7000, PF_GAME(0x000C00), 0x80, &x);
    vid_set_scroll(pf, PF_AT(0x7E623E), VID_BG3, scene_bg3_scroll_x(g), 0, VID_SCROLL_H);
}

/* Nothing it leaves in the registers is read. */
void scene_upload_map_and_scroll(Snes *s)
{
    upload_map_and_scroll(state_of(s), m_platform(s));
    m_rts(s);
}
