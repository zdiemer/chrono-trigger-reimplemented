/* Scenes: the title screen.
 *
 * The title opens on a pendulum swinging over the screen, a Mode 7 plane the
 * program turns and scales every frame; the logo then sweeps in along a curve
 * and settles, the layers behind it come on, and the picture ripples: in BG
 * mode 2 every column of BG1 takes its own vertical offset from BG3's map, and
 * each frame the offsets move a column to the right with a new one coming in at
 * the left, so a wave runs across the picture. The endings come back to it.
 *
 * Its pieces here:
 * - the loader's blocks, each putting a part of the title's graphics in VRAM
 *   (the loader around them decompresses the program's resources and is still
 *   the original's, so each block is an entry of its own): the sprites' tiles,
 *   the layers' tiles, the plane's pixels (built from 4-bit tiles, a row at a
 *   time: expand_plane_tile) and its name table, and BG1's and BG2's tilemaps;
 * - the pendulum's screen (set_up_pendulum) and its place each frame
 *   (place_pendulum);
 * - the logo's task, from the moment it lands (move_logo);
 * - the ripple: mode 2 (start_ripple), the offsets moved on each frame
 *   (ripple_columns) and sent to VRAM with BG3's scroll (upload_ripple).
 *
 * All of it is the scene program's code, in work RAM, so every entry is guarded
 * by a hash of its code. */
#include "scene/scene.h"

#include "cpu_io.h"
#include "original_effects.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"


/* Where the title keeps its graphics in VRAM. */
enum {
    PLANE_VRAM = 0x0000,         /* the Mode 7 plane: its name table in the low bytes, its pixels in the high */
    SPRITE_TILES_VRAM = 0x4000,
    LAYER_TILES_VRAM = 0x5000,
    BG2_MAP_VRAM = 0x6000,
    BG1_MAP_VRAM = 0x6400,
    BG3_MAP_VRAM = 0x7000,       /* in mode 2, its first two rows are the columns' offsets */
};

/* ---- The loader's blocks --------------------------------------------------------------------- */

/* The blocks share a guard over the run of the loader they lie in. */
bool scene_title_loader_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E597B, 124, 0x49B5A7DC40436AD2ull);
}

/* A block begins where its graphics go in VRAM; the port stops where the loader
 * goes on to unpack them, which reads none of the registers. */
static void begin_block(Snes *s, uint16_t vram, uint32_t site, uint32_t stop)
{
    vid_set_vram_address(m_platform(s), PF_AT(site), vram);
    m_stop_at(s, stop, DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C);
}

// $7E:597B
/* The sprites' tiles, the first block: the VRAM port set to whole words for
 * the blocks after it too. */
void scene_title_load_sprite_graphics(Snes *s)
{
    vid_begin_vram_uploads(m_platform(s), PF_AT(0x7E597D), SCENE_VRAM_WHOLE_WORDS, PF_GAME(0), NULL);
    begin_block(s, SPRITE_TILES_VRAM, 0x7E5983, 0x7E5986);
}

// $7E:59A0
/* The tiles BG1 and BG2 share. */
void scene_title_load_layer_tiles(Snes *s) { begin_block(s, LAYER_TILES_VRAM, 0x7E59A3, 0x7E59A6); }

// $7E:59AA
/* The plane's pixels, which the loader builds a tile at a time
 * (scene_title_expand_plane_tile). */
void scene_title_load_plane_graphics(Snes *s) { begin_block(s, PLANE_VRAM, 0x7E59AD, 0x7E59B0); }

// $7E:59BC
void scene_title_load_bg1_map(Snes *s) { begin_block(s, BG1_MAP_VRAM, 0x7E59BF, 0x7E59C2); }

// $7E:59CC
void scene_title_load_bg2_map(Snes *s) { begin_block(s, BG2_MAP_VRAM, 0x7E59CF, 0x7E59D2); }

/* The rest of BG1's tilemap: from the second of its rows the block before
 * didn't cover, $F00 bytes. */
enum { BG1_MAP_REST_VRAM = 0x6480, BG1_MAP_REST_BYTES = 0x0F00 };

// $7E:59D5
/* The rest of BG1's tilemap filled with the two bytes the transfer before this
 * one ended on. The loader programs the channel's mode, the VRAM address and the
 * size and nothing else, so the fill reads on from wherever that transfer left
 * the channel's source -- an address this routine never computes and no call
 * could name. The SNES-only escape carries it (`continue_channel`: the channel
 * supplies what the steps don't). The VRAM port is the setting the block before
 * left (`port`), and the decompressor's destination is moved to bank 0 first. */
static void fill_bg1_map(GameState *g, Platform *pf, uint8_t port)
{
    static const uint8_t steps[] = {
        VID_STEP_CONTROL, VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_SIZE_LO,
        VID_STEP_SIZE_HI, VID_STEP_START,     VID_STEP_END,
    };
    set_decompress_dest(g, decompress_dest(g) & 0x00FFFF);
    VidVramPort vram_port = vid_vram_port_decode(port);
    vid_begin_vram_uploads(pf, PF_AT(0x7E59D8), &vram_port, PF_GAME(0), NULL);
    const VidSnesXfer x = { SCENE_DMA_CHANNEL, (uint8_t)(SCENE_DMA_WORDS | SCENE_DMA_FIXED_SOURCE), steps, NULL };
    const VidSnesTransfer t = { .size = BG1_MAP_REST_BYTES, .target_reg = VID_SNES_VRAM_ADDRESS,
                                .target = BG1_MAP_REST_VRAM, .continue_channel = true };
    vid_snes_transfer(pf, PF_AT(0x7E59DD), &t, &x);
}

/* The VRAM port's setting is in A. Stops at $7E:59F1, where the loader goes on. */
void scene_title_fill_bg1_map(Snes *s)
{
    fill_bg1_map(state_of(s), m_platform(s), (uint8_t)s->cpu.a);
    m_stop_at(s, 0x7E59F1, DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C);
}

/* ---- The plane's pixels ------------------------------------------------------------------------ */

enum { TILE_ROWS = 8, TILE_PIXELS = 8, TILE_PLANES = 4, PLANES_2_AND_3 = 0x10, ROW_BYTES = 2 };

/* The port reads the tile where the original does, through the data bank, and
 * keeps the rows it unpacks in locals where the original shifts them in the
 * scene's scratch: the same only while the tile doesn't lie over that scratch,
 * which no tile does. */
enum { SCRATCH_FIRST = 0x003B, SCRATCH_END = 0x0040, TILE_BYTES = 0x20 };

bool scene_title_expand_plane_tile_present(Snes *s)
{
    if (!scene_on_scene_page(s) || !m_code_matches(s, 0x7E5E54, 164, 0xEE4E5864DAEE75ACull))
        return false;
    if (!m_bank_reaches_low_ram(s))
        return true;
    uint16_t tile = s->cpu.x;
    return (uint16_t)(SCRATCH_FIRST - tile) >= TILE_BYTES && (uint16_t)(tile - SCRATCH_FIRST) >= SCRATCH_END - SCRATCH_FIRST;
}

/* The scratch the original unpacks each row in, as it leaves it: every bit
 * shifted out of the four planes, and its row count run down. */
static void leave_scratch_empty(GameState *g)
{
    for (unsigned i = 0; i <= TILE_PLANES; i++)
        set_scene_bitplane_scratch_byte(g, i, 0);
}

// $7E:5E54
/* One tile of 4-bit planar graphics at `tile` (in `bank`) turned into the
 * plane's pixels: eight rows of eight, each pixel the byte of its four plane
 * bits, into the high bytes of VRAM. Returns where the tile ends. */
static uint16_t expand_plane_tile(GameState *g, Platform *pf, uint8_t bank, uint16_t tile)
{
    for (int row = 0; row < TILE_ROWS; row++) {
        uint16_t planes_0_1 = wram_read16(g, (uint32_t)bank << 16 | tile);
        uint16_t planes_2_3 = wram_read16(g, (uint32_t)bank << 16 | (uint16_t)(tile + PLANES_2_AND_3));
        uint8_t planes[TILE_PLANES] = { (uint8_t)planes_0_1, (uint8_t)(planes_0_1 >> 8), (uint8_t)planes_2_3,
                                        (uint8_t)(planes_2_3 >> 8) };
        tile = (uint16_t)(tile + ROW_BYTES);
        for (int pixel = TILE_PIXELS - 1; pixel >= 0; pixel--) {
            uint8_t color = 0;
            for (int plane = TILE_PLANES - 1; plane >= 0; plane--)
                color = (uint8_t)(color << 1 | (planes[plane] >> pixel & 1));
            vid_write_vram_byte(pf, PF_AT(0x7E5E75), true, color);
        }
    }
    ORIGINAL_EFFECT(leave_scratch_empty(g));
    return tile;
}

/* X is the tile, in the data bank; back, it is where the tile ends. */
void scene_title_expand_plane_tile(Snes *s)
{
    s->cpu.x = expand_plane_tile(state_of(s), m_platform(s), s->cpu.db, s->cpu.x);
    m_rts(s);
}

/* ---- The plane's name table ----------------------------------------------------------------- */

bool scene_title_load_plane_map_present(Snes *s)
{
    return m_bank_reaches_io(s) && m_code_matches(s, 0x7E5AB6, 79, 0x2270DB27658DB4FFull);
}

/* The plane's name table is 128 tiles a row. The title's picture is 32 rows of
 * 32, which the loader has put in work RAM as words whose low bytes are the
 * tile numbers; the rest of each row is tile 0, and the rest of the table the
 * blank tile. */
enum { PLANE_ROW = 128, PICTURE_ROWS = 32, PICTURE_COLUMNS = 32 };
enum { PICTURE_TILES = 0x0200, REST_OF_PLANE_BYTES = 0x1000 };

// $7E:5AB6
/* The plane's name table: the picture's rows, a tile at a time through the
 * VRAM port, then the rest by a transfer. The transfer carries on from where
 * the rows left the VRAM address, and repeats its two bytes into the data
 * port's low half alone, which the upload calls model as words, so it goes
 * through the SNES-only escape. */
static void load_plane_map(GameState *g, Platform *pf)
{
    static const uint8_t steps[] = {
        VID_STEP_CONTROL,     VID_STEP_DEST,    VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
        VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO, VID_STEP_SIZE_HI,   VID_STEP_START,
        VID_STEP_END,
    };
    static const VidSnesXfer x = { SCENE_DMA_CHANNEL, SCENE_DMA_FIXED_SOURCE, steps, NULL };
    hw_port_seek_address(g, PICTURE_TILES);
    vid_begin_vram_uploads(pf, PF_AT(0x7E5ABE), SCENE_VRAM_LOW_BYTES, PF_GAME(0), NULL);
    vid_set_vram_address(pf, PF_AT(0x7E5AC4), PLANE_VRAM);
    for (int row = 0; row < PICTURE_ROWS; row++) {
        for (int column = 0; column < PICTURE_COLUMNS; column++) {
            vid_write_vram_byte(pf, PF_AT(0x7E5ACE), false, hw_port_read(g));
            hw_port_read(g);   /* the word's high byte, dropped */
        }
        for (int column = PICTURE_COLUMNS; column < PLANE_ROW; column++)
            vid_write_vram_byte(pf, PF_AT(0x7E5ADC), false, 0);
    }
    const VidSnesTransfer t = { .dest = VID_SNES_VRAM_DATA_LOW, .source = SCENE_BLANK_TILE,
                                .size = REST_OF_PLANE_BYTES };
    vid_snes_transfer(pf, PF_AT(0x7E5AEB), &t, &x);
}

/* A comes back with the transfer's start in its low byte. */
void scene_title_load_plane_map(Snes *s)
{
    load_plane_map(state_of(s), m_platform(s));
    m_set_a8(s, 0x80);
    m_rts(s);
}

/* ---- The pendulum ------------------------------------------------------------------------------- */

bool scene_title_set_up_pendulum_present(Snes *s) { return m_code_matches(s, 0x7E5A59, 61, 0xCB90A7CBF9F435C1ull); }

/* The pendulum's screen: the plane (BG1 in mode 7, transparent outside it)
 * alone on the main screen and the sprites alone on the sub screen, which color
 * math adds to the plane and the backdrop; BG1-BG3's tilemaps and tiles where
 * the later scenes use them; the rotation center at (32, -64), and BG2 scrolled
 * 192 pixels left. */
static const VidMode pendulum_mode = { .mode = 7 };
static const VidMode7 pendulum_plane = { .outside = VID_M7_TRANSPARENT, .cx = 32, .cy = -64 };
static const VidColorMath pendulum_math = { .sub_screen = true,
                                            .layers = VID_LAYER_BIT(VID_BG1) | VID_LAYER_BIT(VID_BACK) };
static const VidBg pendulum_bg1 = { .map_base = BG1_MAP_VRAM, .map_w = 64, .map_h = 32 };
static const VidBg pendulum_bg2 = { .map_base = BG2_MAP_VRAM, .map_w = 32, .map_h = 32 };
static const VidBg pendulum_bg3 = { .map_base = BG3_MAP_VRAM, .map_w = 32, .map_h = 32 };
enum { PENDULUM_BG2_SCROLL = -192 };

// $7E:5A59
static void set_up_pendulum(Platform *pf)
{
    vid_set_mode(pf, PF_AT(0x7E5A61), &pendulum_mode);
    vid_set_mode7(pf, PF_AT(0x7E5A65), &pendulum_plane, VID_M7_SELECT);
    vid_set_screens(pf, PF_AT(0x7E5A6A), VID_LAYER_BIT(VID_BG1), VID_LAYER_BIT(VID_OBJ),
                    VID_SCREEN_MAIN | VID_SCREEN_SUB);
    vid_set_bg_map(pf, PF_AT(0x7E5A6F), VID_BG1, &pendulum_bg1);
    vid_set_bg_map(pf, PF_AT(0x7E5A6F), VID_BG2, &pendulum_bg2);
    vid_set_bg_map(pf, PF_AT(0x7E5A73), VID_BG3, &pendulum_bg3);
    vid_set_bg_chars(pf, PF_AT(0x7E5A77), VID_BG1, LAYER_TILES_VRAM, LAYER_TILES_VRAM);
    vid_set_color_math(pf, PF_AT(0x7E5A7C), &pendulum_math, VID_MATH_CONTROL | VID_MATH_LAYERS);
    vid_set_mode7(pf, PF_AT(0x7E5A80), &pendulum_plane, VID_M7_CENTER);
    vid_set_scroll(pf, PF_AT(0x7E5A90), VID_BG2, (uint16_t)PENDULUM_BG2_SCROLL, 0, VID_SCROLL_H);
}

/* Written through a direct page on the register block, whose address it
 * leaves in A's high byte (8-bit A); stops at $7E:5A96, where the scene goes on
 * to its own state. */
void scene_title_set_up_pendulum(Snes *s)
{
    set_up_pendulum(m_platform(s));
    s->cpu.p |= CPU_FLAG_M;
    s->cpu.a = M_DP_PPU;
    m_stop_at(s, 0x7E5A96, DC_AL | DC_X | DC_Y | DC_N | DC_Z | DC_C | DC_D);
}

bool scene_title_place_pendulum_present(Snes *s)
{
    return scene_on_scene_page_low_bank(s) && m_code_matches(s, 0x7E5C57, 65, 0xA13E0C2757626FD3ull);
}

/* Channel 0's script keeps BG1's scroll (the plane's, in Mode 7) in two of its
 * registers. */
enum { TITLE_CHANNEL = 0, BG1_SCROLL_X = 1, BG1_SCROLL_Y = 2 };

// $7E:5C57
/* Each frame: the plane turned and scaled by the matrix the scene worked out,
 * and scrolled where channel 0's script has moved it. */
static void place_pendulum(const GameState *g, Platform *pf)
{
    VidMode7 m7 = { .a = (int16_t)scene_mode7_matrix_a(g), .b = (int16_t)scene_mode7_matrix_b(g),
                    .c = (int16_t)scene_mode7_matrix_c(g), .d = (int16_t)scene_mode7_matrix_d(g) };
    vid_set_mode7(pf, PF_AT(0x7E5C59), &m7, VID_M7_MATRIX);
    vid_set_scroll(pf, PF_AT(0x7E5C82), VID_BG1, scene_channel_register(g, TITLE_CHANNEL, BG1_SCROLL_X),
                   scene_channel_register(g, TITLE_CHANNEL, BG1_SCROLL_Y), VID_SCROLL_H | VID_SCROLL_V);
}

/* Nothing it leaves in the registers is read. */
void scene_title_place_pendulum(Snes *s)
{
    place_pendulum(state_of(s), m_platform(s));
    m_rts(s);
}

/* ---- The logo ------------------------------------------------------------------------------------ */

bool scene_title_move_logo_present(Snes *s)
{
    return scene_on_scene_page_low_bank(s) && m_code_matches(s, 0x7E5C98, 40, 0x8FB5D2E8EEA2AD92ull);
}

/* The logo sweeps in while the title's timer counts down by two a frame, and
 * lands when it drops below LOGO_LANDS: at (56, 58), with the script that
 * follows it given to channel 0. */
enum { LOGO_LANDS = 0x72, LOGO_X = 0x38, LOGO_Y = 0x3A, AFTER_THE_LOGO = 0x5B51 };

// $7E:5C98
/* The logo's task. Where it lands: BG1, BG2 and the sprites come on (and
 * nothing on the sub screen), it rests where it belongs, and channel 0 goes on
 * to the next script. True if it landed; its sweep until then is still the
 * original's. The timer is compared by the sign of the difference, as the
 * original compares it, so a timer past $8000 lands at once. */
static bool move_logo(GameState *g, Platform *pf)
{
    if ((int16_t)(scene_title_timer(g) - LOGO_LANDS) >= 0)
        return false;
    vid_set_screens(pf, PF_AT(0x7E5CA4), VID_LAYER_BIT(VID_BG1) | VID_LAYER_BIT(VID_BG2) | VID_LAYER_BIT(VID_OBJ), 0,
                    VID_SCREEN_MAIN | VID_SCREEN_SUB);
    set_scene_channels_script(g, TITLE_CHANNEL, AFTER_THE_LOGO);
    set_scene_title_logo_x(g, LOGO_X);
    set_scene_title_logo_y(g, LOGO_Y);
    return true;
}

/* Landed, the task takes itself off its list through the list's exit at
 * $7E:39A7; still sweeping, it goes on at $7E:5CBA with A the timer less two,
 * where this port stops. */
void scene_title_move_logo(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t timer_after = (uint16_t)(scene_title_timer(g) - 2);
    if (!move_logo(g, m_platform(s))) {
        s->cpu.a = timer_after;
        m_stop_at(s, 0x7E5CBA, DC_X | DC_N | DC_V | DC_Z | DC_C);
        return;
    }
    s->cpu.a = LOGO_Y;
    m_stop_at(s, 0x7E39A7, DC_AL | DC_X | DC_N | DC_V | DC_Z | DC_C);
}

/* ---- The ripple ---------------------------------------------------------------------------------- */

bool scene_title_start_ripple_present(Snes *s)
{
    return scene_on_channel_page(s) && m_code_matches(s, 0x7E5DA7, 13, 0xBAB77B39FA282F4Dull);
}

/* Where the ripple starts BG1: 71 lines down. */
enum { RIPPLE_BG1_SCROLL_Y = -71 };

// $7E:5DA7
/* A script's call: BG mode 2 (each column of BG1 offset on its own) with no
 * mosaic, and BG1's vertical scroll in the calling channel's register. */
static void start_ripple(GameState *g, Platform *pf, SceneChannel channel)
{
    vid_set_mode(pf, PF_AT(0x7E5DAA), &(VidMode){ .mode = 2 });
    vid_set_mosaic(pf, PF_AT(0x7E5DAA), 0, 1);
    set_scene_channel_register(g, channel, BG1_SCROLL_Y, (uint16_t)RIPPLE_BG1_SCROLL_Y);
}

/* The channel's page is D. */
void scene_title_start_ripple(Snes *s)
{
    start_ripple(state_of(s), m_platform(s), scene_program_page_at_entry(s).index);
    m_rts(s);
}

bool scene_title_ripple_columns_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E5FFE, 16, 0x0744D6BC8E4FFD3Cull);
}

/* BG3's map, which the scene builds in its tilemap buffer: its second row is
 * the columns' vertical offsets. */
enum { VERTICAL_OFFSETS = 0x40, COLUMNS = 32, OFFSET_BYTES = 2 };

static uint16_t column_offset(const GameState *g, unsigned column)
{
    unsigned at = VERTICAL_OFFSETS + column * OFFSET_BYTES;
    return (uint16_t)(scene_tilemap_buffer_byte(g, at) | scene_tilemap_buffer_byte(g, at + 1) << 8);
}

static void set_column_offset(GameState *g, unsigned column, uint16_t offset)
{
    unsigned at = VERTICAL_OFFSETS + column * OFFSET_BYTES;
    set_scene_tilemap_buffer_byte(g, at, (uint8_t)offset);
    set_scene_tilemap_buffer_byte(g, at + 1, (uint8_t)(offset >> 8));
}

// $7E:5FFE
/* The ripple's frame: every column takes the offset of the column to its left,
 * the rightmost first. The new offset of the first column is worked out after
 * this, by the original's code, where the port stops. */
static void ripple_columns(GameState *g)
{
    for (unsigned column = COLUMNS - 1; column > 0; column--)
        set_column_offset(g, column, column_offset(g, column - 1));
}

/* Stops where the new offset is worked out, at $7E:600E, with A the offset
 * the second column took, X where the copy's loop ended and the flags of its
 * last compare. */
void scene_title_ripple_columns(Snes *s)
{
    GameState *g = state_of(s);
    ripple_columns(g);
    s->cpu.a = column_offset(g, 1);
    s->cpu.x = VERTICAL_OFFSETS - OFFSET_BYTES;
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, true);
    m_set_flag(s, CPU_FLAG_C, true);
    s->cpu.pc = 0x600E;
}

bool scene_title_upload_ripple_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E6212, 54, 0x0DB9CC06A3C8236Dull);
}

/* The first two rows of BG3's map, the columns' offsets in mode 2. */
enum { OFFSET_ROWS_BYTES = 0x80 };

// $7E:6212
/* Each frame: the columns' offsets to BG3's map, and BG3's horizontal scroll. */
static void upload_ripple(const GameState *g, Platform *pf)
{
    static const uint8_t steps[] = {
        VID_STEP_TARGET_LO,   VID_STEP_TARGET_HI, VID_STEP_CONTROL, VID_STEP_DEST,
        VID_STEP_SOURCE_LO,   VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO,
        VID_STEP_SIZE_HI,     VID_STEP_VRAM_PORT, VID_STEP_START,   VID_STEP_END,
    };
    const VidSnesXfer x = { SCENE_DMA_CHANNEL, SCENE_DMA_WORDS, steps, SCENE_VRAM_WHOLE_WORDS };
    vid_upload_vram(pf, PF_AT(0x7E6215), BG3_MAP_VRAM, PF_GAME(SCENE_TILEMAP_BUFFER), OFFSET_ROWS_BYTES, &x);
    vid_set_scroll(pf, PF_AT(0x7E623E), VID_BG3, scene_bg3_scroll_x(g), 0, VID_SCROLL_H);
}

/* Nothing it leaves in the registers is read. */
void scene_title_upload_ripple(Snes *s)
{
    upload_ripple(state_of(s), m_platform(s));
    m_rts(s);
}
