/* The jet bike race's display work, in the program the boot at $C3:0014
 * decompresses to $7E:3000 (scene/display.c holds the rest of it). The race is a
 * Mode 7 plane, drawn on the lines its per-line effect leaves in that mode while
 * the rest of the screen stays in mode 1. The program rebuilds it every frame:
 * it uploads a scaled road strip and a column of the plane's name table, and
 * moves the rotation center and the flat layers' scroll. Only
 * `events-jetbike-race` reaches any of it.
 *
 * Every entry is guarded by a hash of its bytes, as the program's other ports
 * are: the same addresses hold other code and data at other times.
 */
#include "scene/race.h"

#include "platform/video.h"
#include "port/machine.h"

/* The scenes upload over DMA channel 7; the race's per-line effects use
 * channels 0-5, from a table of descriptors. */
enum { SCENE_DMA = 7, DMA_ONE_REGISTER = 0x00, DMA_WORDS = 0x01, DMA_FIXED_BYTE = 0x08,
       DMA_FROM_REGISTER = 0x80 };

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
static void clear_block(Snes *s, uint32_t addr, uint16_t size, uint32_t site)
{
    const VidSnesXfer x = { SCENE_DMA, DMA_FROM_REGISTER, transfer_steps, NULL };
    const VidSnesTransfer t = { .dest = 0x34, .source = addr, .size = size };
    m_ldx(s, (uint16_t)(0x80 | 0x34 << 8));   /* the channel's mode and destination in one store */
    m_ldx(s, (uint16_t)addr);
    m_lda8(s, (uint8_t)(addr >> 16));
    m_ldx(s, size);
    m_lda8(s, 0x80);
    vid_snes_transfer(m_platform(s), PF_AT(site), &t, &x);
}

/* LDX #addr : STX $2116, which is where the next VRAM writes go. */
static void set_vram_address(Snes *s, uint16_t addr, uint32_t site, uint16_t stop)
{
    m_ldx(s, addr);
    vid_set_vram_address(m_platform(s), PF_AT(site), addr);
    s->cpu.pc = stop;
}

/* ---- per-line effects ---------------------------------------------------------- */

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
    for (int record = 0; record < 8; record++) {
        uint8_t control = snes_bus_peek(&s->bus, 0x7E0000u | at);
        if (control == 0xFF)
            return true;
        /* A `VidLineEffect` carries the transfer mode and the indirect bit and
         * nothing else, so a control byte with any other bit set would be
         * written back as a different one. */
        if (control & ~0x47u)
            return false;
        VidLineEffect e = { 0 };
        if (!vid_line_effect_decode(control, snes_bus_peek(&s->bus, 0x7E0000u | (uint16_t)(at + 1)), &e))
            return false;
        at = (uint16_t)(at + 5);
    }
    return false;
}

// $7E:36CC
/* Program the per-line effects from a table of five-byte descriptors at Y in
 * bank $7E, one to each channel from 0 upwards, until a descriptor whose first
 * byte is $FF. Each holds the channel's mode and destination register, the
 * address of its table and the bank the indirect addresses lie in; the program
 * reads the last byte as a word, so the byte after it -- the next descriptor's
 * mode, or the terminator -- lands in the size and table-cursor registers, which
 * HDMA rewrites (VidLineEffect.snes_spill).
 *
 * The race's table names window 1's left edge, the Mode 7 matrix in two halves,
 * BG1's scroll, the color math layers and the BG mode. The guard walks the whole
 * table first, so the port never starts on one it could not express; the bail-out
 * in the loop is what would happen if it ever did. */
void scene_setup_line_effects(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    /* STA $00,X : STA $02,X : STA $04,X : STA $07,X, each a 16-bit store. */
    static const uint8_t steps[] = {
        VID_STEP_CONTROL,     VID_STEP_DEST,           VID_STEP_SOURCE_LO,    VID_STEP_SOURCE_HI,
        VID_STEP_SOURCE_BANK, VID_STEP_SPILL_SIZE_LO,  VID_STEP_INDIRECT_BANK,
        VID_STEP_SPILL_CURSOR_LO, VID_STEP_END,
    };
    m_push16(s, c->d);
    m_push16(s, M_DP_DMA);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    m_push16(s, 0x007E);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_rep(s, CPU_FLAG_M);
    m_ldx(s, 0x0000);

    for (;;) {
        uint32_t record = m_abs(s, c->y);
        m_lda16(s, m_read16(s, record));
        m_lda16(s, (uint16_t)(c->a & 0x00FF));
        m_cmp16(s, (uint16_t)c->a, 0x00FF);
        if (m_flag(s, CPU_FLAG_Z))
            break;

        /* Decide before touching anything, so that a descriptor no target names
         * leaves the machine as the terminator check found it. */
        VidLineEffect e = { 0 };
        if (!vid_line_effect_decode(snes_bus_peek(&s->bus, record), snes_bus_peek(&s->bus, record + 1), &e)) {
            c->pc = 0x36E5;
            return;
        }
        m_lda16(s, m_read16(s, record));
        uint16_t table = m_lda16(s, m_read16(s, m_abs(s, (uint16_t)(c->y + 2))));
        uint16_t bank = m_lda16(s, m_read16(s, m_abs(s, (uint16_t)(c->y + 4))));
        e.table = PF_GAME((uint32_t)(bank & 0xFF) << 16 | table);
        e.indirect_bank = (uint8_t)bank;
        e.snes_spill = (uint8_t)(bank >> 8);
        vid_set_line_effect(pf, PF_AT(0x7E36E8), (uint8_t)(c->x >> 4), &e, steps);

        m_txa(s);
        m_set_flag(s, CPU_FLAG_C, false);
        s->cpu.a = m_adc16(s, (uint16_t)c->a, 0x0010);
        m_ldx(s, (uint16_t)c->a);
        m_lda16(s, c->y);
        m_set_flag(s, CPU_FLAG_C, false);
        s->cpu.a = m_adc16(s, (uint16_t)c->a, 0x0005);
        m_ldy(s, (uint16_t)c->a);
    }
    m_sep(s, CPU_FLAG_M);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    m_rts(s);
}

/* ---- setting the race up -------------------------------------------------------- */

bool scene_race_clear_buffers_present(Snes *s) { return m_code_matches(s, 0x7E3C09, 56, 0xBB98A27C8E730EE7ull); }

// $7E:3C09
/* The race's two work buffers cleared: $1000 bytes at $7E:F000 (the per-line
 * tables its effects read) and $1200 at $7E:0C00. Stops where the program goes
 * on to the per-line effects. */
void scene_race_clear_buffers(Snes *s)
{
    clear_block(s, 0x7EF000, 0x1000, 0x7E3C22);
    clear_block(s, 0x7E0C00, 0x1200, 0x7E3C3E);
    s->cpu.pc = 0x3C41;
}

bool scene_race_setup_maps_present(Snes *s) { return m_code_matches(s, 0x7E3CFF, 70, 0xCBA05FD5E80212BEull); }

// $7E:3CFF
/* Where the race's layers live on the lines its per-line effect leaves in mode
 * 1: BG1, BG2 and BG3's tilemaps at VRAM $6000, $6400 and $6800, their tiles at
 * $7000, color math over the sub screen, BG1-BG3 and the sprites on the main
 * screen and BG3 alone on the sub screen. It ends by reading $14 bytes of VRAM
 * from $0B00 -- the start of the Mode 7 name table -- back into $7E:C995, a
 * transfer in the other direction, which goes through the SNES-only escape. */
void scene_race_setup_maps(Snes *s)
{
    Platform *pf = m_platform(s);
    m_ldx(s, (uint16_t)(0x60 | 0x64 << 8));   /* BG1's and BG2's tilemap bytes in one store */
    VidBg bg = { 0 };
    vid_bg_map_decode(&bg, 0x60);
    vid_set_bg_map(pf, PF_AT(0x7E3D02), VID_BG1, &bg);
    vid_bg_map_decode(&bg, 0x64);
    vid_set_bg_map(pf, PF_AT(0x7E3D02), VID_BG2, &bg);
    m_lda8(s, 0x68);
    vid_bg_map_decode(&bg, 0x68);
    vid_set_bg_map(pf, PF_AT(0x7E3D07), VID_BG3, &bg);
    m_lda8(s, 0x77);
    vid_set_bg_chars(pf, PF_AT(0x7E3D0C), VID_BG1, 0x7000, 0x7000);
    vid_set_bg_chars(pf, PF_AT(0x7E3D0F), VID_BG3, 0x7000, 0x7000);
    m_lda8(s, 0x02);
    VidColorMath math = { 0 };
    vid_color_math_decode(&math, VID_MATH_CONTROL, 0x02);
    vid_set_color_math(pf, PF_AT(0x7E3D14), &math, VID_MATH_CONTROL);
    m_ldx(s, (uint16_t)(0x17 | 0x04 << 8));   /* the two screens' layer bytes in one store */
    vid_set_screens(pf, PF_AT(0x7E3D1A), 0x17, 0x04, VID_SCREEN_MAIN | VID_SCREEN_SUB);
    m_lda8(s, 0x02);
    vid_begin_vram_uploads(pf, PF_AT(0x7E3D1F), &column_port, PF_GAME(0), NULL);
    m_ldx(s, 0x0B00);
    vid_set_vram_address(pf, PF_AT(0x7E3D25), 0x0B00);

    const VidSnesXfer x = { SCENE_DMA, DMA_FROM_REGISTER, transfer_steps, NULL };
    const VidSnesTransfer t = { .dest = 0x39, .source = 0x7EC995, .size = 0x0014 };
    m_ldx(s, (uint16_t)(0x80 | 0x39 << 8));
    m_ldx(s, 0xC995);
    m_lda8(s, 0x7E);
    m_ldx(s, 0x0014);
    m_lda8(s, 0x80);
    vid_snes_transfer(pf, PF_AT(0x7E3D41), &t, &x);
    m_rts(s);
}

bool scene_race_fill_map_present(Snes *s) { return m_code_matches(s, 0x7E3D8C, 39, 0x563C9A2564F8E0A1ull); }

// $7E:3D8C
/* Fill VRAM $4000 upwards, $1000 bytes, with the word at $7E:350B: the two
 * tilemaps at $4000 and $4400 the race sends a half of each frame, before it
 * draws into them. Stops where the program goes on to decompress its
 * graphics. */
void scene_race_fill_map(Snes *s)
{
    Platform *pf = m_platform(s);
    m_lda8(s, 0x80);
    vid_begin_vram_uploads(pf, PF_AT(0x7E3D8E), &word_port, PF_GAME(0), NULL);
    static const uint8_t steps[] = {
        VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_CONTROL,     VID_STEP_DEST,
        VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO,
        VID_STEP_SIZE_HI,   VID_STEP_START,     VID_STEP_END,
    };
    const VidSnesXfer x = { SCENE_DMA, (uint8_t)(DMA_WORDS | DMA_FIXED_BYTE), steps, NULL };
    m_ldx(s, 0x4000);
    m_ldx(s, (uint16_t)(DMA_WORDS | DMA_FIXED_BYTE | 0x18 << 8));
    m_ldx(s, 0x350B);
    m_lda8(s, 0x7E);
    m_ldx(s, 0x1000);
    m_lda8(s, 0x80);
    vid_fill_vram(pf, PF_AT(0x7E3DB0), 0x4000, PF_GAME(0x7E350B), 0x1000, &x);
    s->cpu.pc = 0x3DB3;
}

/* The race's graphics, one VRAM address per block; the eight stretches share a
 * guard over the run of code they lie in. */
bool scene_race_load_graphics_present(Snes *s) { return m_code_matches(s, 0x7E3DC8, 222, 0xC87D4C0C6E227419ull); }

// $7E:3DC8
void scene_race_set_vram_0000(Snes *s) { set_vram_address(s, 0x0000, 0x7E3DCB, 0x3DCE); }
// $7E:3DE1
void scene_race_set_vram_0000_again(Snes *s) { set_vram_address(s, 0x0000, 0x7E3DE4, 0x3DE7); }

// $7E:3E01
/* The VRAM port back to whole words, the program's own transfer set to copy
 * into the data port, and the first tile block's address. */
void scene_race_begin_tile_uploads(Snes *s)
{
    Platform *pf = m_platform(s);
    m_lda8(s, 0x80);
    vid_begin_vram_uploads(pf, PF_AT(0x7E3E03), &word_port, PF_GAME(0), NULL);
    m_write8(s, m_abs(s, 0x00F0), m_lda8(s, DMA_WORDS));
    m_write8(s, m_abs(s, 0x00F2), m_lda8(s, 0x18));
    set_vram_address(s, 0x7000, 0x7E3E13, 0x3E16);
}

// $7E:3E20
void scene_race_set_vram_7100(Snes *s) { set_vram_address(s, 0x7100, 0x7E3E23, 0x3E26); }
// $7E:3E30
void scene_race_set_vram_5000(Snes *s) { set_vram_address(s, 0x5000, 0x7E3E33, 0x3E36); }

// $7E:3E58
/* The scaler's output buffer at $7F:0300 cleared ($700 bytes), and the next tile
 * block's VRAM address. */
void scene_race_clear_scratch(Snes *s)
{
    clear_block(s, 0x7F0300, 0x0700, 0x7E3E71);
    set_vram_address(s, 0x6000, 0x7E3E77, 0x3E7A);
}

// $7E:3E8A
void scene_race_set_vram_6800(Snes *s) { set_vram_address(s, 0x6800, 0x7E3E8D, 0x3E90); }
// $7E:3EA0
void scene_race_set_vram_6400(Snes *s) { set_vram_address(s, 0x6400, 0x7E3EA3, 0x3EA6); }

/* ---- the race running ----------------------------------------------------------- */

bool scene_race_move_camera_present(Snes *s) { return m_code_matches(s, 0x7E43F2, 60, 0xBD782F2009885B3Aull); }

// $7E:43F2
/* One frame of the camera: the Mode 7 rotation center from $142C-$142F, BG2's
 * horizontal scroll from $1430 and BG3's from $1434-$1437, each value a pair of
 * bytes the race keeps in the order the registers take them. Stops where the
 * routine goes on to copy its own state around. */
void scene_race_move_camera(Snes *s)
{
    Platform *pf = m_platform(s);
    uint16_t v[8];
    for (int i = 0; i < 4; i++)
        v[i] = m_lda8(s, m_read8(s, m_abs(s, (uint16_t)(0x142C + i))));
    vid_set_mode7(pf, PF_AT(0x7E43F5),
                  &(VidMode7){ .cx = (int16_t)(v[0] | v[1] << 8), .cy = (int16_t)(v[2] | v[3] << 8) },
                  VID_M7_CENTER);
    for (int i = 0; i < 2; i++)
        v[i] = m_lda8(s, m_read8(s, m_abs(s, (uint16_t)(0x1430 + i))));
    vid_set_scroll(pf, PF_AT(0x7E440D), VID_BG2, (uint16_t)(v[0] | v[1] << 8), 0, VID_SCROLL_H);
    for (int i = 0; i < 4; i++)
        v[i] = m_lda8(s, m_read8(s, m_abs(s, (uint16_t)(0x1434 + i))));
    vid_set_scroll(pf, PF_AT(0x7E4419), VID_BG3, (uint16_t)(v[0] | v[1] << 8),
                   (uint16_t)(v[2] | v[3] << 8), VID_SCROLL_H | VID_SCROLL_V);
    s->cpu.pc = 0x442E;
}

bool scene_race_upload_column_present(Snes *s) { return m_code_matches(s, 0x7E4643, 52, 0xAE7FE41C71F7EB77ull); }

/* $13 bytes into the low halves of VRAM words from $0B00, stepping 128 words (a
 * row) at a time: a column of the Mode 7 plane's name table, which lives in the
 * low half of every word. The transfer writes the data port's low half alone,
 * which vid_upload_vram would model as words, so it goes through the SNES-only
 * escape. Both entries end by jumping into the task list's exit. */
static void upload_column(Snes *s, uint32_t source, uint32_t site)
{
    static const uint8_t steps[] = {
        VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK, VID_STEP_CONTROL,
        VID_STEP_DEST,      VID_STEP_VRAM_PORT, VID_STEP_TARGET_LO,   VID_STEP_TARGET_HI,
        VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,   VID_STEP_START,       VID_STEP_END,
    };
    const VidSnesXfer x = { SCENE_DMA, DMA_ONE_REGISTER, steps, &column_port };
    const VidSnesTransfer t = { .dest = 0x18, .source = source, .size = 0x0013,
                                .target_reg = 0x16, .target = 0x0B00 };
    m_ldx(s, (uint16_t)source);
    m_lda8(s, (uint8_t)(source >> 16));
    m_ldx(s, (uint16_t)(DMA_ONE_REGISTER | 0x18 << 8));
    m_lda8(s, 0x02);
    m_ldx(s, 0x0B00);
    m_ldx(s, 0x0013);
    m_lda8(s, 0x80);
    vid_snes_transfer(m_platform(s), PF_AT(site), &t, &x);
    s->cpu.pc = 0x39DE;
}

// $7E:4643
void scene_race_upload_column_rom(Snes *s) { upload_column(s, 0xD20000, 0x7E4671); }
// $7E:464D
void scene_race_upload_column_ram(Snes *s) { upload_column(s, 0x7EC995, 0x7E4671); }

bool scene_race_upload_tilemap_present(Snes *s) { return m_code_matches(s, 0x7E5221, 106, 0x2A63AA398E2A05CBull); }

// $7E:5221
/* Whichever of the two tilemaps the race marked ready ($AA for VRAM $4000, $AC
 * for $4400): $800 bytes from $00:0C00, a word at a time. With neither marked it
 * returns without uploading. */
void scene_race_upload_tilemap(Snes *s)
{
    uint16_t vram;
    if (m_lda8(s, m_read8(s, m_dp(s, 0xAA))) != 0) {
        m_write8(s, m_dp(s, 0xAA), 0x00);
        vram = 0x4000;
    } else if (m_lda8(s, m_read8(s, m_dp(s, 0xAC))) != 0) {
        m_write8(s, m_dp(s, 0xAC), 0x00);
        vram = 0x4400;
    } else {
        m_rts(s);
        return;
    }
    m_ldx(s, vram);
    m_ldy(s, 0x0800);
    const VidSnesXfer x = { SCENE_DMA, DMA_WORDS, upload_steps, &word_port };
    m_lda8(s, DMA_WORDS);
    m_lda8(s, 0x18);
    m_ldx(s, 0x0C00);
    m_lda8(s, 0x00);
    m_lda8(s, 0x80);
    vid_upload_vram(m_platform(s), PF_AT(0x7E525B), vram, PF_GAME(0x000C00), 0x0800, &x);
    m_write8(s, m_dp(s, 0x39), 0x00);
    m_rts(s);
}

// $7E:5261
/* The scaled road strip: $40 bytes from $7E:E100 to VRAM $5180, a word at a
 * time. Also the tail of the routine at $7E:44FE, which patches the scaler's
 * addresses and jumps here. */
void scene_race_upload_strip(Snes *s)
{
    const VidSnesXfer x = { SCENE_DMA, DMA_WORDS, upload_steps, &word_port };
    m_ldx(s, 0x5180);
    m_ldx(s, 0x0040);
    m_lda8(s, DMA_WORDS);
    m_lda8(s, 0x18);
    m_ldx(s, 0xE100);
    m_lda8(s, 0x7E);
    m_lda8(s, 0x80);
    vid_upload_vram(m_platform(s), PF_AT(0x7E5287), 0x5180, PF_GAME(0x7EE100), 0x0040, &x);
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
void scene_race_upload_plane(Snes *s)
{
    Platform *pf = m_platform(s);
    vid_begin_vram_uploads(pf, PF_AT(0x7E8752), &low_byte_port, PF_GAME(0), NULL);
    const VidSnesXfer x = { SCENE_DMA, DMA_ONE_REGISTER, transfer_steps, NULL };
    const VidSnesTransfer t = { .dest = 0x18, .source = 0x7F2200, .size = 0x2000 };
    m_ldx(s, (uint16_t)(DMA_ONE_REGISTER | 0x18 << 8));
    m_ldx(s, 0x2200);
    m_lda8(s, 0x7F);
    m_ldx(s, 0x2000);
    m_lda8(s, 0x80);
    vid_snes_transfer(pf, PF_AT(0x7E876E), &t, &x);
    m_rts(s);
}
