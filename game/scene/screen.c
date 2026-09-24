/* Scenes: the scene program's screen -- setting it up, and what it sends each
 * frame.
 *
 * Every scene starts from the same screen: blank, the sprite graphics at VRAM
 * $4000, every layer, window and effect off (init_video). Its vblank tasks
 * then send each frame's sprites and palette (upload_sprites, upload_palette),
 * and hide every sprite in the table for the scene to place again
 * (clear_sprite_table). Its loaders move graphics into VRAM with a transfer of
 * their own (transfer), and a scene with per-line effects programs them from a
 * table of descriptors (setup_line_effects).
 *
 * Two of these clear memory by copying the product the Mode 7 multiplier
 * offers, 0 or $E0, out of the PPU: a transfer that reads a register rather
 * than writing one, which no video call expresses, so it goes through the
 * SNES-only escape (vid_snes_transfer); the matrix values they leave behind are
 * the video state. The transfers' `steps` say the order the SNES backend
 * programs the channel in, which is still per call (docs/TODO.md).
 *
 * The program is in work RAM, where other code and data are at other times, so
 * every entry is guarded by a hash of its code. */
#include "scene/scene.h"

#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* The sprite table and the working palette, in the first bank of work RAM. */
enum { SPRITE_TABLE = 0x000700, SPRITE_TABLE_BYTES = 0x0200, HIGH_TABLE = 0x0900, WORKING_PALETTE = 0x7E2200 };
enum { COLORS = 256 };

/* Where the scene keeps the sprites' graphics. */
enum { SPRITE_GRAPHICS = 0x4000 };

/* A clear's channel: its mode and destination together, then the source, the
 * size and the start. */
static const uint8_t clear_steps[] = {
    VID_STEP_CONTROL,     VID_STEP_DEST,    VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
    VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO, VID_STEP_SIZE_HI,   VID_STEP_START,
    VID_STEP_END,
};

/* ---- The program stops ---------------------------------------------------------------------- */

bool scene_program_restart_present(Snes *s) { return m_code_matches(s, 0x7E302A, 14, 0x614033C5F8C83156ull); }

// $7E:302A
/* The program stops: interrupts and per-line effects off and the screen
 * blanked, before it moves the stack and jumps to the reset code. */
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

/* ---- The program's work RAM cleared --------------------------------------------------------- */

bool scene_clear_work_ram_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E3092, 87, 0x2A1AE096D5B36F10ull);
}

/* What the clear covers: the direct page, then low RAM from past the stack up
 * to the sound driver interface's page -- or, for the title coming back, from
 * the task lists, keeping what lies between. */
enum { DIRECT_PAGE_BYTES = 0x0100, FROM_THE_STACK = 0x000900, FROM_THE_TASKS = 0x000920, SOUND_PAGE = 0x1E00 };

// $7E:3092
/* Clear the program's work RAM: every byte set to the product the Mode 7
 * multiplier offers, with the matrix set so that it is 0 -- A times 0, where A
 * is `matrix_left`, whatever the caller had in the accumulator, which the
 * multiplier keeps afterwards. The second transfer keeps the first one's mode,
 * destination and bank, so it programs only its source and size. Returns
 * whether it was the title's shorter clear. */
static bool clear_work_ram(GameState *g, Platform *pf, uint8_t matrix_left)
{
    static const uint8_t again_steps[] = {
        VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SIZE_LO, VID_STEP_SIZE_HI, VID_STEP_START,
        VID_STEP_END,
    };
    const VidSnesXfer first_channel = { SCENE_DMA_CHANNEL, SCENE_DMA_FROM_REGISTER, clear_steps, NULL };
    const VidSnesXfer again = { SCENE_DMA_CHANNEL, SCENE_DMA_FROM_REGISTER, again_steps, NULL };
    vid_set_mode7(pf, PF_AT(0x7E3096), &(VidMode7){ .a = (int16_t)(matrix_left << 8 | matrix_left) }, VID_M7_MATRIX_A);
    vid_set_mode7(pf, PF_AT(0x7E309D), &(VidMode7){ .b = 0 }, VID_M7_MATRIX_B);
    const VidSnesTransfer direct_page = { .dest = VID_SNES_MULTIPLIER_PRODUCT, .source = 0x000000, .size = DIRECT_PAGE_BYTES };
    vid_snes_transfer(pf, PF_AT(0x7E30B6), &direct_page, &first_channel);
    bool keep_below_tasks = scene_flags(g) & SCENE_FLAG_TITLE_WITH_LINE_IRQ;
    uint32_t from = keep_below_tasks ? FROM_THE_TASKS : FROM_THE_STACK;
    const VidSnesTransfer rest = { .dest = VID_SNES_MULTIPLIER_PRODUCT, .source = from, .size = (uint16_t)(SOUND_PAGE - from) };
    vid_snes_transfer(pf, PF_AT(keep_below_tasks ? 0x7E30D2 : 0x7E30E6), &rest, &again);
    return keep_below_tasks;
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

/* ---- A scene's first screen ------------------------------------------------------------------ */

bool scene_init_video_present(Snes *s)
{
    return scene_on_scene_page(s) && m_code_matches(s, 0x7E30F9, 126, 0x4F3F340A8EB09E78ull);
}

/* The screen every scene starts from: BG mode 0 with every layer's tilemap and
 * tiles at VRAM 0, 32 tiles square and unscrolled; no mosaic, no Mode 7 plane,
 * no window or color math, nothing on either screen; the sprites small, their
 * graphics at SPRITE_GRAPHICS. */
typedef struct {
    VidObjConfig sprites;
    VidMode mode;
    VidBg layer;
    VidMode7 plane;
    VidWindows windows;
    VidColorMath math;
    VidVideoConfig config;
} SceneScreen;

static const SceneScreen first_screen = {
    .sprites = { .size_select = 0, .base = SPRITE_GRAPHICS, .name_select = 0 },
    .layer = { .map_w = 32, .map_h = 32 },
};

enum {
    EVERY_WINDOW_SETTING = VID_WINDOWS_BG1_BG2 | VID_WINDOWS_BG3_BG4 | VID_WINDOWS_OBJ_BACK | VID_WINDOWS_W1_LEFT |
                           VID_WINDOWS_W1_RIGHT | VID_WINDOWS_W2_LEFT | VID_WINDOWS_W2_RIGHT | VID_WINDOWS_BG_LOGIC |
                           VID_WINDOWS_OBJ_LOGIC,
};

// $7E:30F9
/* Blank the screen and set it as first_screen says, with the fixed color black
 * and the sprite cursor at the start of the high table. */
static void init_video(GameState *g, Platform *pf)
{
    const SceneScreen *s = &first_screen;
    set_scene_sprite_high_pointer(g, HIGH_TABLE);
    vid_set_display(pf, PF_AT(0x7E3108), &(VidDisplay){ .blank = true });
    vid_set_obj_config(pf, PF_AT(0x7E310C), &s->sprites);
    vid_set_fixed_color(pf, PF_AT(0x7E3110), VID_RED | VID_GREEN | VID_BLUE, 0);

    vid_set_oam_address(pf, PF_AT(0x7E3118), 0x0000, false);
    vid_set_mode(pf, PF_AT(0x7E311A), &s->mode);
    vid_set_mosaic(pf, PF_AT(0x7E311C), 0, 1);
    static const uint32_t map_sites[4] = { 0x7E311E, 0x7E311E, 0x7E3120, 0x7E3120 };
    for (int bg = VID_BG1; bg <= VID_BG4; bg++)
        vid_set_bg_map(pf, PF_AT(map_sites[bg]), (VidLayer)bg, &s->layer);
    vid_set_bg_chars(pf, PF_AT(0x7E3122), VID_BG1, s->layer.char_base, s->layer.char_base);
    vid_set_bg_chars(pf, PF_AT(0x7E3122), VID_BG3, s->layer.char_base, s->layer.char_base);
    for (int bg = VID_BG1; bg <= VID_BG4; bg++)
        vid_set_scroll(pf, PF_AT((uint32_t)(0x7E3124 + 8 * bg)), (VidLayer)bg, s->layer.hofs, s->layer.vofs,
                       VID_SCROLL_H | VID_SCROLL_V);
    vid_begin_vram_uploads(pf, PF_AT(0x7E3144), &(VidVramPort){ .step = 1 }, PF_GAME(0), NULL);
    vid_set_mode7(pf, PF_AT(0x7E3146), &s->plane, VID_M7_SELECT);
    vid_set_mode7(pf, PF_AT(0x7E3148), &s->plane, VID_M7_MATRIX);
    vid_set_mode7(pf, PF_AT(0x7E3158), &s->plane, VID_M7_CENTER);
    vid_set_palette_address(pf, PF_AT(0x7E315E), 0);

    vid_set_windows(pf, PF_AT(0x7E3160), &s->windows, EVERY_WINDOW_SETTING);
    vid_set_screens(pf, PF_AT(0x7E316A), 0, 0, VID_SCREEN_MAIN | VID_SCREEN_SUB);
    vid_set_windows(pf, PF_AT(0x7E316C), &s->windows, VID_WINDOWS_MAIN | VID_WINDOWS_SUB);
    vid_set_color_math(pf, PF_AT(0x7E316E), &s->math, VID_MATH_CONTROL);
    vid_set_color_math(pf, PF_AT(0x7E316E), &s->math, VID_MATH_LAYERS);
    vid_set_fixed_color(pf, PF_AT(0x7E3170), 0, 0);   /* every channel bit clear, not black */
    vid_set_video_config(pf, PF_AT(0x7E3170), &s->config);
}

/* Written through a direct page on the register block, which it leaves on the
 * scene's page again; A's high byte ends 0, with 8-bit A and 16-bit X. */
void scene_init_video(Snes *s)
{
    init_video(state_of(s), m_platform(s));
    s->cpu.p = (uint8_t)((s->cpu.p | CPU_FLAG_M) & ~CPU_FLAG_X);
    s->cpu.a = 0x0000;
    m_rts(s);
}

/* ---- Each frame's sprites and palette -------------------------------------------------------- */

bool scene_upload_sprites_present(Snes *s)
{
    return scene_on_scene_page(s) && m_code_matches(s, 0x7E380E, 53, 0x5555FCBF0F118757ull);
}

// $7E:380E
/* Send the sprite table to OAM, draw from the sprite the scene put on top
 * (with the priority rotation), and start the sprite cursor over: the first
 * entry, and the first high-table byte. */
static void upload_sprites(GameState *g, Platform *pf)
{
    static const uint8_t steps[] = {
        VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_SOURCE_BANK, VID_STEP_CONTROL,
        VID_STEP_DEST,      VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,   VID_STEP_SIZE_LO,
        VID_STEP_SIZE_HI,   VID_STEP_START,     VID_STEP_END,
    };
    static const VidSnesXfer x = { SCENE_DMA_CHANNEL, SCENE_DMA_ONE_REGISTER, steps, NULL };
    vid_upload_oam(pf, PF_AT(0x7E3811), PF_GAME(SPRITE_TABLE), &x);
    vid_set_oam_address(pf, PF_AT(0x7E3830), scene_sprite_priority_start(g), true);
    set_scene_sprite_index(g, 0);
    set_scene_sprite_high_pointer(g, HIGH_TABLE);
}

/* Nothing it leaves in the registers is read. */
void scene_upload_sprites(Snes *s)
{
    upload_sprites(state_of(s), m_platform(s));
    m_rts(s);
}

bool scene_clear_sprite_table_present(Snes *s) { return m_code_matches(s, 0x7E3843, 45, 0x36CEE4DC10A522A8ull); }

/* A Y below the picture, where a sprite shows nowhere: the product of 1 times
 * -32 the clear copies over every byte of the table. */
enum { BELOW_THE_PICTURE = -32 };

// $7E:3843
/* Hide every sprite: every byte of the sprite table set to a Y below the
 * picture, for the scene to place its sprites again. */
static void clear_sprite_table(Platform *pf)
{
    static const uint8_t steps[] = {
        VID_STEP_SOURCE_BANK, VID_STEP_DEST,    VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
        VID_STEP_SIZE_LO,     VID_STEP_SIZE_HI, VID_STEP_CONTROL,   VID_STEP_START,
        VID_STEP_END,
    };
    static const VidSnesXfer x = { SCENE_DMA_CHANNEL, SCENE_DMA_FROM_REGISTER, steps, NULL };
    vid_set_mode7(pf, PF_AT(0x7E3845), &(VidMode7){ .a = 1 }, VID_M7_MATRIX_A);
    vid_set_mode7(pf, PF_AT(0x7E384D), &(VidMode7){ .b = BELOW_THE_PICTURE * 256 }, VID_M7_MATRIX_B_HIGH);
    const VidSnesTransfer t = { .dest = VID_SNES_MULTIPLIER_PRODUCT, .source = SPRITE_TABLE, .size = SPRITE_TABLE_BYTES };
    vid_snes_transfer(pf, PF_AT(0x7E3850), &t, &x);
    vid_set_mode7(pf, PF_AT(0x7E386C), &(VidMode7){ .b = 0 }, VID_M7_MATRIX_B_HIGH);
}

void scene_clear_sprite_table(Snes *s)
{
    clear_sprite_table(m_platform(s));
    m_rts(s);
}

bool scene_upload_palette_present(Snes *s) { return m_code_matches(s, 0x7E388B, 36, 0x84B3B18FAFEEAE44ull); }

// $7E:388B
/* Send the working palette to CGRAM. */
static void upload_palette(Platform *pf)
{
    static const uint8_t steps[] = {
        VID_STEP_CONTROL,   VID_STEP_TARGET_LO, VID_STEP_SOURCE_BANK, VID_STEP_DEST,
        VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SIZE_LO,     VID_STEP_SIZE_HI,
        VID_STEP_START,     VID_STEP_END,
    };
    static const VidSnesXfer x = { SCENE_DMA_CHANNEL, SCENE_DMA_ONE_REGISTER, steps, NULL };
    vid_upload_palette(pf, PF_AT(0x7E388D), 0, PF_GAME(WORKING_PALETTE), COLORS, &x);
}

void scene_upload_palette(Snes *s)
{
    upload_palette(m_platform(s));
    m_rts(s);
}

/* ---- The loaders' transfer ------------------------------------------------------------------ */

bool scene_transfer_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E38E7, 36, 0xDD4B67DA62CC8E67ull);
}

// $7E:38E7
/* The program's own transfer: what the decompressor has just unpacked (its
 * destination and length) into the register and in the mode the caller put in
 * the scene's scratch. Both are game data the loaders choose, so it goes
 * through the SNES-only escape; the movies' loaders all feed the VRAM data port. */
static void transfer(const GameState *g, Platform *pf)
{
    static const uint8_t steps[] = {
        VID_STEP_CONTROL,     VID_STEP_DEST,    VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
        VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO, VID_STEP_SIZE_HI,   VID_STEP_START,
        VID_STEP_END,
    };
    SceneProgramPage scene = scene_program_own();
    const VidSnesXfer x = { SCENE_DMA_CHANNEL, scene_call_transfer_control(g, scene), steps, NULL };
    const VidSnesTransfer t = { .dest = scene_call_transfer_register(g, scene), .source = decompress_dest(g),
                                .size = decompress_length(g) };
    vid_snes_transfer(pf, PF_AT(0x7E38EA), &t, &x);
}

/* Returns with RTL; nothing it leaves in the registers is read. */
void scene_transfer(Snes *s)
{
    transfer(state_of(s), m_platform(s));
    m_rtl(s);
}

/* ---- Per-line effects from descriptors --------------------------------------------------- */

/* A table of descriptors, five bytes each, ended by a first byte of $FF: a
 * channel's mode and the register it feeds, the address of its table, and the
 * bank the table's indirect addresses lie in. The program reads the last value
 * as a word, so the byte after it -- the next descriptor's mode, or the end --
 * lands in the channel's size and table-cursor registers, which HDMA rewrites
 * (VidLineEffect.snes_spill). */
typedef struct {
    uint8_t mode, target;
    uint16_t table;
    uint8_t bank, spill;
} LineEffectDescriptor;

enum { DESCRIPTOR_BYTES = 5, END_OF_DESCRIPTORS = 0xFF, LINE_CHANNELS = 8, DESCRIPTOR_BANK = SCENE_LOWER_WORK_RAM };

static uint32_t descriptor_at(uint16_t at, unsigned offset) { return (uint32_t)DESCRIPTOR_BANK << 16 | (uint16_t)(at + offset); }

static LineEffectDescriptor read_descriptor(const GameState *g, uint16_t at)
{
    uint16_t head = wram_read16(g, descriptor_at(at, 0));
    uint16_t table = wram_read16(g, descriptor_at(at, 2));
    uint16_t bank = wram_read16(g, descriptor_at(at, 4));
    return (LineEffectDescriptor){ (uint8_t)head, (uint8_t)(head >> 8), table, (uint8_t)bank, (uint8_t)(bank >> 8) };
}

/* A `VidLineEffect` carries the transfer mode and the indirect bit and nothing
 * else, so a mode byte with any other bit set would be written back as a
 * different one. */
enum { LINE_EFFECT_MODE_BITS = 0x47 };

/* The code, and a table every descriptor of which names a target the service
 * knows. There are eight channels to fill, so a table without its end in eight
 * descriptors is one the original would run off the end of. Peeks only, from
 * bank $7E, which the routine sets itself. */
bool scene_setup_line_effects_present(Snes *s)
{
    if (!m_code_matches(s, 0x7E36CC, 60, 0x45191A42A1EEF6A8ull))
        return false;
    uint16_t at = s->cpu.y;
    for (int channel = 0; channel < LINE_CHANNELS; channel++) {
        uint8_t mode = snes_bus_peek(&s->bus, descriptor_at(at, 0));
        if (mode == END_OF_DESCRIPTORS)
            return true;
        if (mode & ~LINE_EFFECT_MODE_BITS)
            return false;
        VidLineEffect e = { 0 };
        if (!vid_line_effect_decode(mode, snes_bus_peek(&s->bus, descriptor_at(at, 1)), &e))
            return false;
        at = (uint16_t)(at + DESCRIPTOR_BYTES);
    }
    return false;
}

// $7E:36CC
/* Program the per-line effects from the descriptors at `at`, one to each
 * channel from 0 up. The race's table drives window 1's left edge, the Mode 7
 * matrix in two halves, BG1's scroll, the color math layers and the BG mode.
 * The guard has walked the table and refused one a descriptor of which no
 * target names, so every descriptor decodes. Returns where the descriptors
 * end. */
static uint16_t setup_line_effects(const GameState *g, Platform *pf, uint16_t at)
{
    static const uint8_t steps[] = {
        VID_STEP_CONTROL,     VID_STEP_DEST,          VID_STEP_SOURCE_LO,       VID_STEP_SOURCE_HI,
        VID_STEP_SOURCE_BANK, VID_STEP_SPILL_SIZE_LO, VID_STEP_INDIRECT_BANK,   VID_STEP_SPILL_CURSOR_LO,
        VID_STEP_END,
    };
    for (uint8_t channel = 0;; channel++) {
        LineEffectDescriptor d = read_descriptor(g, at);
        VidLineEffect e = { 0 };
        if (d.mode == END_OF_DESCRIPTORS || !vid_line_effect_decode(d.mode, d.target, &e))
            return at;
        e.table = PF_GAME((uint32_t)d.bank << 16 | d.table);
        e.indirect_bank = d.bank;
        e.snes_spill = d.spill;
        vid_set_line_effect(pf, PF_AT(0x7E36E8), channel, &e, steps);
        at = (uint16_t)(at + DESCRIPTOR_BYTES);
    }
}

/* The overflow of stepping past a descriptor, which the program reads after. */
static bool overflows_stepping_past(uint16_t at)
{
    uint16_t next = (uint16_t)(at + DESCRIPTOR_BYTES);
    return (~(at ^ DESCRIPTOR_BYTES) & (at ^ next) & 0x8000) != 0;
}

/* Y is the table, and back it is where the descriptors end, with the overflow
 * of the last step past one (as it was when there is none). The routine works
 * with the direct page on the DMA registers and the data bank $7E, and puts both
 * back. */
void scene_setup_line_effects(Snes *s)
{
    uint16_t start = s->cpu.y;
    uint16_t end = setup_line_effects(state_of(s), m_platform(s), start);
    if (end != start)
        m_set_flag(s, CPU_FLAG_V, overflows_stepping_past((uint16_t)(end - DESCRIPTOR_BYTES)));
    s->cpu.y = end;
    s->cpu.p |= CPU_FLAG_M;
    m_rts(s);
}
