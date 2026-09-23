/* The jet bike race's display work, in the program the boot at $C3:0014
 * decompresses to $7E:3000 (scene/display.c holds the rest of it). The race is a
 * Mode 7 plane, drawn on the lines its per-line effect leaves in that mode while
 * the rest of the screen stays in mode 1. The program rebuilds it every frame:
 * it uploads a scaled road strip and a column of the plane's name table, and
 * moves the rotation center and the flat layers' scroll. Only
 * `events-jetbike-race` reaches any of it.
 *
 * Every entry is guarded by a hash of its bytes, as the program's other ports
 * are: the same addresses hold other code and data at other times. Each
 * routine's work is a function of the platform and the game's state; the entry
 * wrappers leave what the program reads after them (their don't-care sets say
 * the rest is dead) and stop where it goes on.
 */
#include "scene/race.h"

#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "scene/scene.h"
#include "vars.h"
#include "wram.h"

/* The scenes upload over DMA channel 7; the race's per-line effects use
 * channels 0-5, from a table of descriptors. */
enum { SCENE_DMA = 7, DMA_ONE_REGISTER = 0x00, DMA_WORDS = 0x01, DMA_FIXED_BYTE = 0x08,
       DMA_FROM_REGISTER = 0x80 };

/* The registers the race's transfers name: the VRAM data port's low byte, the
 * PPU multiplier's product and the VRAM read port. */
enum { VRAM_DATA = 0x18, MULTIPLIER_PRODUCT = 0x34, VRAM_READ = 0x39 };

/* $2115 as the race sets it: a word at a time for the tilemap uploads, the low
 * byte alone where it writes the Mode 7 plane's name table (which lives in the
 * low half of every VRAM word), and a row of that table at a time where the
 * column upload walks down it. */
static const VidVramPort word_port = { .step = 1, .after_high = true };
static const VidVramPort low_byte_port = { .step = 1, .after_high = false };
static const VidVramPort column_port = { .step = 128, .after_high = false };

/* A transfer programmed control-and-destination first, then source, size and
 * start: two 16-bit stores, a byte and a 16-bit store. */
static const uint8_t transfer_steps[] = {
    VID_STEP_CONTROL,     VID_STEP_DEST,    VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
    VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO, VID_STEP_SIZE_HI,   VID_STEP_START,
    VID_STEP_END,
};

/* The VRAM address, then the size, then the channel, then the port: the order
 * both of the race's per-frame uploads program. */
static const uint8_t upload_steps[] = {
    VID_STEP_TARGET_LO,   VID_STEP_TARGET_HI, VID_STEP_SIZE_LO,  VID_STEP_SIZE_HI,
    VID_STEP_CONTROL,     VID_STEP_DEST,      VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
    VID_STEP_SOURCE_BANK, VID_STEP_VRAM_PORT, VID_STEP_START,     VID_STEP_END,
};

/* Clear a block of memory by copying the Mode 7 multiplier's product out of
 * $2134, the way scene_clear_work_ram and $C0:2DF1 do: the transfer reads a
 * register rather than writing one, so it goes through the SNES-only escape and
 * leaves no video state behind. */
static void clear_block(Platform *pf, uint32_t addr, uint16_t size, uint32_t site)
{
    const VidSnesXfer x = { SCENE_DMA, DMA_FROM_REGISTER, transfer_steps, NULL };
    const VidSnesTransfer t = { .dest = MULTIPLIER_PRODUCT, .source = addr, .size = size };
    vid_snes_transfer(pf, PF_AT(site), &t, &x);
}

/* The registers a stop leaves nothing in that the program reads. */
enum { NOTHING_READ = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C };

/* LDX #addr : STX $2116, which is where the next VRAM writes go. */
static void set_vram_address(Snes *s, uint16_t addr, uint32_t site, uint32_t stop)
{
    vid_set_vram_address(m_platform(s), PF_AT(site), addr);
    m_stop_at(s, stop, NOTHING_READ);
}

/* ---- per-line effects ---------------------------------------------------------- */

enum { DESCRIPTOR = 5, END_OF_DESCRIPTORS = 0xFF, LINE_CHANNELS = 8, TABLE_BANK = 0x7E };

/* The code, and a table every descriptor of which names a target the service
 * knows. There are eight channels to fill, so a table without a terminator in
 * eight records is one the original would run off the end of. Peeks only: a
 * guard runs on the real machine, where a bus read would cost time and set open
 * bus. The routine reads the table with a data bank of $7E, which it sets
 * itself, so the guard reads there too. */
bool scene_setup_line_effects_present(Snes *s)
{
    if (!m_code_matches(s, 0x7E36CC, 60, 0x45191A42A1EEF6A8ull))
        return false;
    uint16_t at = s->cpu.y;
    for (int record = 0; record < LINE_CHANNELS; record++) {
        uint8_t control = snes_bus_peek(&s->bus, 0x7E0000u | at);
        if (control == END_OF_DESCRIPTORS)
            return true;
        /* A `VidLineEffect` carries the transfer mode and the indirect bit and
         * nothing else, so a control byte with any other bit set would be
         * written back as a different one. */
        if (control & ~0x47u)
            return false;
        VidLineEffect e = { 0 };
        if (!vid_line_effect_decode(control, snes_bus_peek(&s->bus, 0x7E0000u | (uint16_t)(at + 1)), &e))
            return false;
        at = (uint16_t)(at + DESCRIPTOR);
    }
    return false;
}

/* Where the descriptors end, and the overflow of the last step past one, which
 * the program reads. */
typedef struct {
    uint16_t end;
    bool overflow;
} LineEffects;

static bool add16_overflows(uint16_t a, uint16_t b)
{
    uint16_t sum = (uint16_t)(a + b);
    return (~(a ^ b) & (a ^ sum) & 0x8000) != 0;
}

// $7E:36CC
/* Program the per-line effects from a table of five-byte descriptors at `at` in
 * bank $7E, one to each channel from 0 upwards, until a descriptor whose first
 * byte is $FF. Each holds the channel's mode and destination register, the
 * address of its table and the bank the indirect addresses lie in; the program
 * reads the last byte as a word, so the byte after it -- the next descriptor's
 * mode, or the terminator -- lands in the size and table-cursor registers, which
 * HDMA rewrites (VidLineEffect.snes_spill).
 *
 * The race's table names window 1's left edge, the Mode 7 matrix in two halves,
 * BG1's scroll, the color math layers and the BG mode. The guard has walked
 * the whole table and refused one with a descriptor no target names, and
 * nothing here can change it, so every descriptor decodes. */
static LineEffects setup_line_effects(const GameState *g, Platform *pf, uint16_t at, bool overflow)
{
    /* STA $00,X : STA $02,X : STA $04,X : STA $07,X, each a 16-bit store. */
    static const uint8_t steps[] = {
        VID_STEP_CONTROL,     VID_STEP_DEST,           VID_STEP_SOURCE_LO,    VID_STEP_SOURCE_HI,
        VID_STEP_SOURCE_BANK, VID_STEP_SPILL_SIZE_LO,  VID_STEP_INDIRECT_BANK,
        VID_STEP_SPILL_CURSOR_LO, VID_STEP_END,
    };
    for (uint8_t channel = 0;; channel++) {
        uint32_t record = (uint32_t)TABLE_BANK << 16;
        uint16_t head = wram_read16(g, record | at);
        VidLineEffect e = { 0 };
        if ((head & 0xFF) == END_OF_DESCRIPTORS || !vid_line_effect_decode((uint8_t)head, (uint8_t)(head >> 8), &e))
            return (LineEffects){ at, overflow };
        uint16_t table = wram_read16(g, record | (uint16_t)(at + 2));
        uint16_t bank = wram_read16(g, record | (uint16_t)(at + 4));
        e.table = PF_GAME((uint32_t)(bank & 0xFF) << 16 | table);
        e.indirect_bank = (uint8_t)bank;
        e.snes_spill = (uint8_t)(bank >> 8);
        vid_set_line_effect(pf, PF_AT(0x7E36E8), channel, &e, steps);
        overflow = add16_overflows(at, DESCRIPTOR);
        at = (uint16_t)(at + DESCRIPTOR);
    }
}

/* Y is the table, and back it is where the descriptors end. The routine works
 * with the direct page on the DMA registers and the data bank $7E, and puts
 * both back. */
void scene_setup_line_effects(Snes *s)
{
    LineEffects r = setup_line_effects(state_of(s), m_platform(s), s->cpu.y, m_flag(s, CPU_FLAG_V));
    s->cpu.y = r.end;
    m_set_flag(s, CPU_FLAG_V, r.overflow);
    s->cpu.p |= CPU_FLAG_M;
    m_rts(s);
}

/* ---- setting the race up -------------------------------------------------------- */

bool scene_race_clear_buffers_present(Snes *s) { return m_code_matches(s, 0x7E3C09, 56, 0xBB98A27C8E730EE7ull); }

// $7E:3C09
/* The race's two work buffers cleared: the per-line tables its effects read,
 * and the tilemap buffer with what follows it. */
static void clear_race_buffers(Platform *pf)
{
    clear_block(pf, 0x7EF000, 0x1000, 0x7E3C22);
    clear_block(pf, 0x7E0C00, 0x1200, 0x7E3C3E);
}

/* Stops where the program goes on to the per-line effects. */
void scene_race_clear_buffers(Snes *s)
{
    clear_race_buffers(m_platform(s));
    m_stop_at(s, 0x7E3C41, NOTHING_READ);
}

bool scene_race_setup_maps_present(Snes *s) { return m_code_matches(s, 0x7E3CFF, 70, 0xCBA05FD5E80212BEull); }

// $7E:3CFF
/* Where the race's layers live on the lines its per-line effect leaves in mode
 * 1: BG1, BG2 and BG3's tilemaps at VRAM $6000, $6400 and $6800, their tiles at
 * $7000, color math over the sub screen, BG1-BG3 and the sprites on the main
 * screen and BG3 alone on the sub screen. It ends by reading $14 bytes of VRAM
 * from $0B00 -- the start of the Mode 7 name table -- back into the plane's
 * column buffer, a transfer in the other direction, which goes through the
 * SNES-only escape. */
static void setup_race_maps(Platform *pf)
{
    VidBg bg = { 0 };
    vid_bg_map_decode(&bg, 0x60);
    vid_set_bg_map(pf, PF_AT(0x7E3D02), VID_BG1, &bg);
    vid_bg_map_decode(&bg, 0x64);
    vid_set_bg_map(pf, PF_AT(0x7E3D02), VID_BG2, &bg);
    vid_bg_map_decode(&bg, 0x68);
    vid_set_bg_map(pf, PF_AT(0x7E3D07), VID_BG3, &bg);
    vid_set_bg_chars(pf, PF_AT(0x7E3D0C), VID_BG1, 0x7000, 0x7000);
    vid_set_bg_chars(pf, PF_AT(0x7E3D0F), VID_BG3, 0x7000, 0x7000);
    VidColorMath math = { 0 };
    vid_color_math_decode(&math, VID_MATH_CONTROL, 0x02);
    vid_set_color_math(pf, PF_AT(0x7E3D14), &math, VID_MATH_CONTROL);
    vid_set_screens(pf, PF_AT(0x7E3D1A), 0x17, 0x04, VID_SCREEN_MAIN | VID_SCREEN_SUB);
    vid_begin_vram_uploads(pf, PF_AT(0x7E3D1F), &column_port, PF_GAME(0), NULL);
    vid_set_vram_address(pf, PF_AT(0x7E3D25), 0x0B00);

    const VidSnesXfer x = { SCENE_DMA, DMA_FROM_REGISTER, transfer_steps, NULL };
    const VidSnesTransfer t = { .dest = VRAM_READ, .source = 0x7EC995, .size = 0x0014 };
    vid_snes_transfer(pf, PF_AT(0x7E3D41), &t, &x);
}

/* Nothing it leaves in the registers is read but the overflow, which it
 * doesn't touch. */
void scene_race_setup_maps(Snes *s)
{
    setup_race_maps(m_platform(s));
    m_rts(s);
}

bool scene_race_fill_map_present(Snes *s) { return m_code_matches(s, 0x7E3D8C, 39, 0x563C9A2564F8E0A1ull); }

// $7E:3D8C
/* Fill VRAM $4000 upwards, $1000 bytes, with the word at $7E:350B: the two
 * tilemaps at $4000 and $4400 the race sends a half of each frame, before it
 * draws into them. */
static void fill_race_map(Platform *pf)
{
    vid_begin_vram_uploads(pf, PF_AT(0x7E3D8E), &word_port, PF_GAME(0), NULL);
    static const uint8_t steps[] = {
        VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_CONTROL,     VID_STEP_DEST,
        VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO,
        VID_STEP_SIZE_HI,   VID_STEP_START,     VID_STEP_END,
    };
    const VidSnesXfer x = { SCENE_DMA, (uint8_t)(DMA_WORDS | DMA_FIXED_BYTE), steps, NULL };
    vid_fill_vram(pf, PF_AT(0x7E3DB0), 0x4000, PF_GAME(0x7E350B), 0x1000, &x);
}

/* Stops where the program goes on to decompress its graphics. */
void scene_race_fill_map(Snes *s)
{
    fill_race_map(m_platform(s));
    m_stop_at(s, 0x7E3DB3, NOTHING_READ);
}

/* The race's graphics, one VRAM address per block; the eight stretches share a
 * guard over the run of code they lie in. */
bool scene_race_load_graphics_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E3DC8, 222, 0xC87D4C0C6E227419ull);
}

// $7E:3DC8
void scene_race_set_vram_0000(Snes *s) { set_vram_address(s, 0x0000, 0x7E3DCB, 0x7E3DCE); }
// $7E:3DE1
void scene_race_set_vram_0000_again(Snes *s) { set_vram_address(s, 0x0000, 0x7E3DE4, 0x7E3DE7); }

// $7E:3E01
/* The VRAM port back to whole words, the program's own transfer (scene_transfer)
 * set to copy words into the data port, and the first tile block's address. */
static void begin_tile_uploads(GameState *g, Platform *pf)
{
    vid_begin_vram_uploads(pf, PF_AT(0x7E3E03), &word_port, PF_GAME(0), NULL);
    set_scene_scratch8(g, scene_math_scene(), SCRATCH_A, DMA_WORDS);
    set_scene_scratch8(g, scene_math_scene(), SCRATCH_B, VRAM_DATA);
}

void scene_race_begin_tile_uploads(Snes *s)
{
    begin_tile_uploads(state_of(s), m_platform(s));
    set_vram_address(s, 0x7000, 0x7E3E13, 0x7E3E16);
}

// $7E:3E20
void scene_race_set_vram_7100(Snes *s) { set_vram_address(s, 0x7100, 0x7E3E23, 0x7E3E26); }
// $7E:3E30
void scene_race_set_vram_5000(Snes *s) { set_vram_address(s, 0x5000, 0x7E3E33, 0x7E3E36); }

// $7E:3E58
/* The scaler's output buffer in bank $7F cleared, and the next tile block's
 * VRAM address. */
void scene_race_clear_scratch(Snes *s)
{
    clear_block(m_platform(s), 0x7F0300, 0x0700, 0x7E3E71);
    set_vram_address(s, 0x6000, 0x7E3E77, 0x7E3E7A);
}

// $7E:3E8A
void scene_race_set_vram_6800(Snes *s) { set_vram_address(s, 0x6800, 0x7E3E8D, 0x7E3E90); }
// $7E:3EA0
void scene_race_set_vram_6400(Snes *s) { set_vram_address(s, 0x6400, 0x7E3EA3, 0x7E3EA6); }

/* ---- the race running ----------------------------------------------------------- */

bool scene_race_move_camera_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E43F2, 60, 0xBD782F2009885B3Aull);
}

// $7E:43F2
/* One frame of the camera: the Mode 7 rotation center, BG2's horizontal scroll
 * and BG3's, from where the race keeps them. */
static void move_camera(const GameState *g, Platform *pf)
{
    vid_set_mode7(pf, PF_AT(0x7E43F5),
                  &(VidMode7){ .cx = (int16_t)scene_race_center_x(g), .cy = (int16_t)scene_race_center_y(g) },
                  VID_M7_CENTER);
    vid_set_scroll(pf, PF_AT(0x7E440D), VID_BG2, scene_race_bg2_scroll_x(g), 0, VID_SCROLL_H);
    vid_set_scroll(pf, PF_AT(0x7E4419), VID_BG3, scene_race_bg3_scroll_x(g), scene_race_bg3_scroll_y(g),
                   VID_SCROLL_H | VID_SCROLL_V);
}

/* Stops where the routine goes on to copy its own state around. */
void scene_race_move_camera(Snes *s)
{
    move_camera(state_of(s), m_platform(s));
    m_stop_at(s, 0x7E442E, NOTHING_READ);
}

bool scene_race_upload_column_present(Snes *s) { return m_code_matches(s, 0x7E4643, 52, 0xAE7FE41C71F7EB77ull); }

/* $13 bytes into the low halves of VRAM words from $0B00, stepping 128 words (a
 * row) at a time: a column of the Mode 7 plane's name table, which lives in the
 * low half of every word. The transfer writes the data port's low half alone,
 * which vid_upload_vram would model as words, so it goes through the SNES-only
 * escape. */
static void upload_column(Platform *pf, uint32_t source)
{
    static const uint8_t steps[] = {
        VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK, VID_STEP_CONTROL,
        VID_STEP_DEST,      VID_STEP_VRAM_PORT, VID_STEP_TARGET_LO,   VID_STEP_TARGET_HI,
        VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,   VID_STEP_START,       VID_STEP_END,
    };
    const VidSnesXfer x = { SCENE_DMA, DMA_ONE_REGISTER, steps, &column_port };
    const VidSnesTransfer t = { .dest = VRAM_DATA, .source = source, .size = 0x0013,
                                .target_reg = 0x16, .target = 0x0B00 };
    vid_snes_transfer(pf, PF_AT(0x7E4671), &t, &x);
}

/* Both entries end by jumping into the task list's exit. */
static void upload_column_entry(Snes *s, uint32_t source)
{
    upload_column(m_platform(s), source);
    m_stop_at(s, 0x7E39DE, NOTHING_READ);
}

// $7E:4643
/* The column from the plane's name table in ROM. */
void scene_race_upload_column_rom(Snes *s) { upload_column_entry(s, 0xD20000); }
// $7E:464D
/* The column from the race's own copy of it in work RAM. */
void scene_race_upload_column_ram(Snes *s) { upload_column_entry(s, 0x7EC995); }

bool scene_race_upload_tilemap_present(Snes *s)
{
    return scene_on_scene_page(s) && m_code_matches(s, 0x7E5221, 106, 0x2A63AA398E2A05CBull);
}

// $7E:5221
/* Whichever of the two tilemaps the race marked ready (the first for VRAM
 * $4000, the second for $4400): the tilemap buffer, $800 bytes a word at a
 * time, and the pending upload cleared. With neither marked it does nothing. */
static void upload_ready_tilemap(GameState *g, Platform *pf)
{
    uint16_t vram;
    if (scene_race_tilemap_a_ready(g) != 0) {
        set_scene_race_tilemap_a_ready(g, 0);
        vram = 0x4000;
    } else if (scene_race_tilemap_b_ready(g) != 0) {
        set_scene_race_tilemap_b_ready(g, 0);
        vram = 0x4400;
    } else {
        return;
    }
    const VidSnesXfer x = { SCENE_DMA, DMA_WORDS, upload_steps, &word_port };
    vid_upload_vram(pf, PF_AT(0x7E525B), vram, PF_GAME(0x000C00), 0x0800, &x);
    set_scene_race_tilemap_pending(g, scene_race_tilemap_pending(g) & 0xFF00);   /* the 8-bit STZ */
}

/* Nothing it leaves in the registers is read. */
void scene_race_upload_tilemap(Snes *s)
{
    upload_ready_tilemap(state_of(s), m_platform(s));
    m_rts(s);
}

/* The strip's code is inside the tilemap upload's, and it reaches nothing
 * through the direct page. */
bool scene_race_upload_strip_present(Snes *s) { return m_code_matches(s, 0x7E5221, 106, 0x2A63AA398E2A05CBull); }

// $7E:5261
/* The scaled road strip: $40 bytes from $7E:E100 to VRAM $5180, a word at a
 * time. Also the tail of the routine at $7E:44FE, which patches the scaler's
 * addresses and jumps here. */
static void upload_road_strip(Platform *pf)
{
    const VidSnesXfer x = { SCENE_DMA, DMA_WORDS, upload_steps, &word_port };
    vid_upload_vram(pf, PF_AT(0x7E5287), 0x5180, PF_GAME(0x7EE100), 0x0040, &x);
}

void scene_race_upload_strip(Snes *s)
{
    upload_road_strip(m_platform(s));
    m_rts(s);
}

bool scene_race_upload_plane_present(Snes *s) { return m_code_matches(s, 0x7E8752, 32, 0xE25F56B609E8320Eull); }

// $7E:8752
/* $2000 bytes from $7F:2200 into the data port's low half alone, carrying on
 * from the VRAM address the block before it left: half of the Mode 7 plane's
 * 128x128 name table, which is the low byte of every VRAM word, so the two calls
 * the loader makes cover all $4000 of them. One byte to a word is what
 * vid_upload_vram would model as words, so it goes through the SNES-only escape,
 * which the observer follows byte by byte. */
static void upload_plane_half(Platform *pf)
{
    vid_begin_vram_uploads(pf, PF_AT(0x7E8752), &low_byte_port, PF_GAME(0), NULL);
    const VidSnesXfer x = { SCENE_DMA, DMA_ONE_REGISTER, transfer_steps, NULL };
    const VidSnesTransfer t = { .dest = VRAM_DATA, .source = 0x7F2200, .size = 0x2000 };
    vid_snes_transfer(pf, PF_AT(0x7E876E), &t, &x);
}

void scene_race_upload_plane(Snes *s)
{
    upload_plane_half(m_platform(s));
    m_rts(s);
}
