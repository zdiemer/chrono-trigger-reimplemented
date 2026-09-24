/* Scenes: the jet bike race's display.
 *
 * The race is a Mode 7 plane -- the track, seen in perspective -- drawn on the
 * lines its per-line effects leave in mode 7, with flat layers (the sky, the
 * scenery) above it in mode 1. The program sets the race up: its work buffers
 * cleared, the flat layers' tilemaps and tiles placed, and its graphics loaded
 * a block at a time (the loader around the blocks decompresses the program's
 * resources and is still the original's, so each block is an entry of its own).
 * Then every frame it moves the camera (the plane's rotation center and the
 * flat layers' scroll), sends a column of the plane's name table where the
 * track scrolls in, the scaled road strip, and whichever half of the racers'
 * tilemap the race has finished drawing, and waits for that upload before it
 * draws the next. The engine's sound follows the bike: the race sets the pitch
 * of the effect voices every frame.
 *
 * Only `events-jetbike-race` reaches any of it. It is the scene program's
 * code, in work RAM, so every entry is guarded by a hash of its code. */
#include "scene/scene.h"

#include "platform/audio.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"


/* The VRAM port a word at a time for the tilemaps, the low byte alone for the
 * plane's name table (the low half of every VRAM word), and a row of that table
 * at a time where the column upload walks down it. */
static const VidVramPort down_a_column = { .step = 128, .after_high = false };

/* Where the race keeps its graphics in VRAM. */
enum {
    PLANE_VRAM = 0x0000,          /* the Mode 7 plane: its name table in the low bytes, its pixels in the high */
    PLANE_COLUMN_VRAM = 0x0B00,   /* the column of the name table the track scrolls in at */
    RACERS_MAP_A_VRAM = 0x4000,   /* the two halves of the racers' tilemap, drawn alternately */
    RACERS_MAP_B_VRAM = 0x4400,
    ROAD_STRIP_VRAM = 0x5180,
    SPRITE_TILES_VRAM = 0x5000,   /* the sprites' second page of tiles */
    BG1_MAP_VRAM = 0x6000,
    BG2_MAP_VRAM = 0x6400,
    BG3_MAP_VRAM = 0x6800,
    LAYER_TILES_VRAM = 0x7000,
    MORE_LAYER_TILES_VRAM = 0x7100,
};

/* The race's buffers in work RAM. */
enum {
    LINE_TABLES = 0x7EF000, LINE_TABLES_BYTES = 0x1000,
    TILEMAP_BUFFER_BYTES = 0x1200, TILEMAP_HALF_BYTES = 0x0800,
    ROAD_STRIP = 0x7EE100, ROAD_STRIP_BYTES = 0x0040,
    SCALER_OUTPUT = 0x7F0300, SCALER_OUTPUT_BYTES = 0x0700,
    PLANE_COLUMN_COPY = 0x7EC995, PLANE_COLUMN_BYTES = 0x0013, PLANE_COLUMN_READ_BYTES = 0x0014,
    PLANE_MAP_HALF = 0x7F2200, PLANE_MAP_HALF_BYTES = 0x2000,
    RACERS_MAP_BYTES = 0x1000,
};

/* The plane's name table in the cartridge, whose first column the race sends
 * before it has one of its own. */
enum { PLANE_MAP_IN_ROM = 0xD20000 };

/* A transfer's channel programmed mode and destination first, then source, size
 * and start. */
static const uint8_t transfer_steps[] = {
    VID_STEP_CONTROL,     VID_STEP_DEST,    VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
    VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO, VID_STEP_SIZE_HI,   VID_STEP_START,
    VID_STEP_END,
};

/* The VRAM address, then the size, then the channel, then the port: the order
 * both of the race's per-frame uploads program them in. */
static const uint8_t upload_steps[] = {
    VID_STEP_TARGET_LO,   VID_STEP_TARGET_HI, VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,
    VID_STEP_CONTROL,     VID_STEP_DEST,      VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
    VID_STEP_SOURCE_BANK, VID_STEP_VRAM_PORT, VID_STEP_START,     VID_STEP_END,
};

/* Clear a buffer by copying the Mode 7 multiplier's product (0 here) out of
 * the PPU, the way scene_clear_work_ram does: a transfer that reads a register
 * rather than writing one, which goes through the SNES-only escape. */
static void clear_buffer(Platform *pf, uint32_t buffer, uint16_t bytes, uint32_t site)
{
    const VidSnesXfer x = { SCENE_DMA_CHANNEL, SCENE_DMA_FROM_REGISTER, transfer_steps, NULL };
    const VidSnesTransfer t = { .dest = VID_SNES_MULTIPLIER_PRODUCT, .source = buffer, .size = bytes };
    vid_snes_transfer(pf, PF_AT(site), &t, &x);
}

/* The registers a stop leaves nothing in that the program reads. */
enum { NOTHING_READ = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C };

/* ---- Setting the race up ---------------------------------------------------------------------- */

bool scene_race_clear_buffers_present(Snes *s) { return m_code_matches(s, 0x7E3C09, 56, 0xBB98A27C8E730EE7ull); }

// $7E:3C09
/* The race's two work buffers cleared: the per-line tables its effects read,
 * and the tilemap buffer with what follows it. */
static void clear_race_buffers(Platform *pf)
{
    clear_buffer(pf, LINE_TABLES, LINE_TABLES_BYTES, 0x7E3C22);
    clear_buffer(pf, (uint32_t)SCENE_LOWER_WORK_RAM << 16 | SCENE_TILEMAP_BUFFER, TILEMAP_BUFFER_BYTES, 0x7E3C3E);
}

/* Stops where the program goes on to the per-line effects. */
void scene_race_clear_buffers(Snes *s)
{
    clear_race_buffers(m_platform(s));
    m_stop_at(s, 0x7E3C41, NOTHING_READ);
}

bool scene_race_setup_maps_present(Snes *s) { return m_code_matches(s, 0x7E3CFF, 70, 0xCBA05FD5E80212BEull); }

static const VidBg race_bg1 = { .map_base = BG1_MAP_VRAM, .map_w = 32, .map_h = 32 };
static const VidBg race_bg2 = { .map_base = BG2_MAP_VRAM, .map_w = 32, .map_h = 32 };
static const VidBg race_bg3 = { .map_base = BG3_MAP_VRAM, .map_w = 32, .map_h = 32 };
static const VidColorMath race_math = { .sub_screen = true };

// $7E:3CFF
/* The flat layers, on the lines the per-line effect leaves in mode 1: BG1-BG3's
 * tilemaps and their tiles, color math over the sub screen, BG1-BG3 and the
 * sprites on the main screen and BG3 alone on the sub screen. Then the column of
 * the plane's name table the track scrolls in at is read back out of VRAM into
 * the race's copy of it, a transfer in the other direction, which goes through
 * the SNES-only escape. */
static void setup_race_maps(Platform *pf)
{
    vid_set_bg_map(pf, PF_AT(0x7E3D02), VID_BG1, &race_bg1);
    vid_set_bg_map(pf, PF_AT(0x7E3D02), VID_BG2, &race_bg2);
    vid_set_bg_map(pf, PF_AT(0x7E3D07), VID_BG3, &race_bg3);
    vid_set_bg_chars(pf, PF_AT(0x7E3D0C), VID_BG1, LAYER_TILES_VRAM, LAYER_TILES_VRAM);
    vid_set_bg_chars(pf, PF_AT(0x7E3D0F), VID_BG3, LAYER_TILES_VRAM, LAYER_TILES_VRAM);
    vid_set_color_math(pf, PF_AT(0x7E3D14), &race_math, VID_MATH_CONTROL);
    vid_set_screens(pf, PF_AT(0x7E3D1A),
                    VID_LAYER_BIT(VID_BG1) | VID_LAYER_BIT(VID_BG2) | VID_LAYER_BIT(VID_BG3) | VID_LAYER_BIT(VID_OBJ),
                    VID_LAYER_BIT(VID_BG3), VID_SCREEN_MAIN | VID_SCREEN_SUB);
    vid_begin_vram_uploads(pf, PF_AT(0x7E3D1F), &down_a_column, PF_GAME(0), NULL);
    vid_set_vram_address(pf, PF_AT(0x7E3D25), PLANE_COLUMN_VRAM);

    const VidSnesXfer x = { SCENE_DMA_CHANNEL, SCENE_DMA_FROM_REGISTER, transfer_steps, NULL };
    const VidSnesTransfer t = { .dest = VID_SNES_VRAM_READ, .source = PLANE_COLUMN_COPY,
                                .size = PLANE_COLUMN_READ_BYTES };
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
/* Both halves of the racers' tilemap filled with the blank tile, before the
 * race draws into them. */
static void fill_racers_map(Platform *pf)
{
    static const uint8_t steps[] = {
        VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_CONTROL,     VID_STEP_DEST,
        VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO,
        VID_STEP_SIZE_HI,   VID_STEP_START,     VID_STEP_END,
    };
    vid_begin_vram_uploads(pf, PF_AT(0x7E3D8E), SCENE_VRAM_WHOLE_WORDS, PF_GAME(0), NULL);
    const VidSnesXfer x = { SCENE_DMA_CHANNEL, (uint8_t)(SCENE_DMA_WORDS | SCENE_DMA_FIXED_SOURCE), steps, NULL };
    vid_fill_vram(pf, PF_AT(0x7E3DB0), RACERS_MAP_A_VRAM, PF_GAME(SCENE_BLANK_TILE), RACERS_MAP_BYTES, &x);
}

/* Stops where the program goes on to decompress its graphics. */
void scene_race_fill_map(Snes *s)
{
    fill_racers_map(m_platform(s));
    m_stop_at(s, 0x7E3DB3, NOTHING_READ);
}

/* ---- The loader's blocks -------------------------------------------------------------------- */

/* The blocks share a guard over the run of the loader they lie in. */
bool scene_race_loader_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E3DC8, 222, 0xC87D4C0C6E227419ull);
}

/* A block begins where its graphics go in VRAM; the port stops where the loader
 * goes on to unpack them. */
static void begin_block(Snes *s, uint16_t vram, uint32_t site, uint32_t stop)
{
    vid_set_vram_address(m_platform(s), PF_AT(site), vram);
    m_stop_at(s, stop, NOTHING_READ);
}

// $7E:3DC8
/* The plane's pixels, into the high bytes of its VRAM. */
void scene_race_load_plane_graphics(Snes *s) { begin_block(s, PLANE_VRAM, 0x7E3DCB, 0x7E3DCE); }

// $7E:3DE1
/* The plane's name table, in two halves (scene_race_upload_plane). */
void scene_race_load_plane_map(Snes *s) { begin_block(s, PLANE_VRAM, 0x7E3DE4, 0x7E3DE7); }

// $7E:3E01
/* The flat layers' tiles: the VRAM port back to whole words, and the program's
 * transfer (scene_transfer) set to copy words into the VRAM data port for the
 * blocks from here on. */
static void begin_layer_tiles(GameState *g, Platform *pf)
{
    vid_begin_vram_uploads(pf, PF_AT(0x7E3E03), SCENE_VRAM_WHOLE_WORDS, PF_GAME(0), NULL);
    set_scene_call_transfer_control(g, scene_program_own(), SCENE_DMA_WORDS);
    set_scene_call_transfer_register(g, scene_program_own(), VID_SNES_VRAM_DATA_LOW);
}

void scene_race_load_layer_tiles(Snes *s)
{
    begin_layer_tiles(state_of(s), m_platform(s));
    begin_block(s, LAYER_TILES_VRAM, 0x7E3E13, 0x7E3E16);
}

// $7E:3E20
void scene_race_load_more_layer_tiles(Snes *s) { begin_block(s, MORE_LAYER_TILES_VRAM, 0x7E3E23, 0x7E3E26); }

// $7E:3E30
void scene_race_load_sprite_graphics(Snes *s) { begin_block(s, SPRITE_TILES_VRAM, 0x7E3E33, 0x7E3E36); }

// $7E:3E58
/* BG1's tilemap, after the scaler's output buffer is cleared for the racers. */
void scene_race_load_bg1_map(Snes *s)
{
    clear_buffer(m_platform(s), SCALER_OUTPUT, SCALER_OUTPUT_BYTES, 0x7E3E71);
    begin_block(s, BG1_MAP_VRAM, 0x7E3E77, 0x7E3E7A);
}

// $7E:3E8A
void scene_race_load_bg3_map(Snes *s) { begin_block(s, BG3_MAP_VRAM, 0x7E3E8D, 0x7E3E90); }

// $7E:3EA0
void scene_race_load_bg2_map(Snes *s) { begin_block(s, BG2_MAP_VRAM, 0x7E3EA3, 0x7E3EA6); }

bool scene_race_upload_plane_present(Snes *s) { return m_code_matches(s, 0x7E8752, 32, 0xE25F56B609E8320Eull); }

// $7E:8752
/* Half the plane's 128x128 name table, from where the loader unpacked it, into
 * the low byte of each VRAM word from where the half before it ended: the two
 * calls cover it all. One byte to a word is what the upload calls model as
 * words, so it goes through the SNES-only escape, which the observer follows
 * byte by byte. */
static void upload_plane_half(Platform *pf)
{
    vid_begin_vram_uploads(pf, PF_AT(0x7E8752), SCENE_VRAM_LOW_BYTES, PF_GAME(0), NULL);
    const VidSnesXfer x = { SCENE_DMA_CHANNEL, SCENE_DMA_ONE_REGISTER, transfer_steps, NULL };
    const VidSnesTransfer t = { .dest = VID_SNES_VRAM_DATA_LOW, .source = PLANE_MAP_HALF, .size = PLANE_MAP_HALF_BYTES };
    vid_snes_transfer(pf, PF_AT(0x7E876E), &t, &x);
}

void scene_race_upload_plane(Snes *s)
{
    upload_plane_half(m_platform(s));
    m_rts(s);
}

/* ---- The race running ----------------------------------------------------------------------- */

bool scene_race_move_camera_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E43F2, 60, 0xBD782F2009885B3Aull);
}

// $7E:43F2
/* The camera, each frame: the plane's rotation center, BG2's horizontal scroll
 * and BG3's, where the race has moved them. */
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

/* A column of the plane's name table down into the low bytes of VRAM, a row
 * (128 words) at a time. The transfer writes the data port's low half alone,
 * which the upload calls model as words, so it goes through the SNES-only
 * escape. */
static void upload_column(Platform *pf, uint32_t column)
{
    static const uint8_t steps[] = {
        VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK, VID_STEP_CONTROL,
        VID_STEP_DEST,      VID_STEP_VRAM_PORT, VID_STEP_TARGET_LO,   VID_STEP_TARGET_HI,
        VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,   VID_STEP_START,       VID_STEP_END,
    };
    const VidSnesXfer x = { SCENE_DMA_CHANNEL, SCENE_DMA_ONE_REGISTER, steps, &down_a_column };
    const VidSnesTransfer t = { .dest = VID_SNES_VRAM_DATA_LOW, .source = column, .size = PLANE_COLUMN_BYTES,
                                .target_reg = VID_SNES_VRAM_ADDRESS, .target = PLANE_COLUMN_VRAM };
    vid_snes_transfer(pf, PF_AT(0x7E4671), &t, &x);
}

/* Both entries end by jumping into the task list's exit. */
static void upload_column_entry(Snes *s, uint32_t column)
{
    upload_column(m_platform(s), column);
    m_stop_at(s, 0x7E39DE, NOTHING_READ);
}

// $7E:4643
/* The column from the plane's name table in the cartridge. */
void scene_race_upload_column_rom(Snes *s) { upload_column_entry(s, PLANE_MAP_IN_ROM); }
// $7E:464D
/* The column from the race's own copy of it. */
void scene_race_upload_column_ram(Snes *s) { upload_column_entry(s, PLANE_COLUMN_COPY); }

bool scene_race_upload_tilemap_present(Snes *s)
{
    return scene_on_scene_page(s) && m_code_matches(s, 0x7E5221, 106, 0x2A63AA398E2A05CBull);
}

// $7E:5221
/* Whichever half of the racers' tilemap the race has finished drawing, from the
 * tilemap buffer, and the upload the race waits on marked done. With neither
 * finished it does nothing. The mark is cleared a byte at a time, as the
 * original clears it, leaving the cell's high byte as it was. */
static void upload_racers_map(GameState *g, Platform *pf)
{
    uint16_t vram;
    if (scene_race_tilemap_a_ready(g) != 0) {
        set_scene_race_tilemap_a_ready(g, 0);
        vram = RACERS_MAP_A_VRAM;
    } else if (scene_race_tilemap_b_ready(g) != 0) {
        set_scene_race_tilemap_b_ready(g, 0);
        vram = RACERS_MAP_B_VRAM;
    } else {
        return;
    }
    const VidSnesXfer x = { SCENE_DMA_CHANNEL, SCENE_DMA_WORDS, upload_steps, SCENE_VRAM_WHOLE_WORDS };
    vid_upload_vram(pf, PF_AT(0x7E525B), vram, PF_GAME(SCENE_TILEMAP_BUFFER), TILEMAP_HALF_BYTES, &x);
    set_scene_race_tilemap_pending(g, scene_race_tilemap_pending(g) & 0xFF00);
}

/* Nothing it leaves in the registers is read. */
void scene_race_upload_tilemap(Snes *s)
{
    upload_racers_map(state_of(s), m_platform(s));
    m_rts(s);
}

/* The strip's code is inside the tilemap upload's, and it reaches nothing
 * through the direct page. */
bool scene_race_upload_strip_present(Snes *s) { return m_code_matches(s, 0x7E5221, 106, 0x2A63AA398E2A05CBull); }

// $7E:5261
/* The scaled road strip, a word at a time. Also the end of the routine at
 * $7E:44FE, which points the scaler at the strip and jumps here. */
static void upload_road_strip(Platform *pf)
{
    const VidSnesXfer x = { SCENE_DMA_CHANNEL, SCENE_DMA_WORDS, upload_steps, SCENE_VRAM_WHOLE_WORDS };
    vid_upload_vram(pf, PF_AT(0x7E5287), ROAD_STRIP_VRAM, PF_GAME(ROAD_STRIP), ROAD_STRIP_BYTES, &x);
}

void scene_race_upload_strip(Snes *s)
{
    upload_road_strip(m_platform(s));
    m_rts(s);
}

bool scene_race_wait_for_tilemap_upload_present(Snes *s)
{
    return scene_on_scene_page(s) && m_code_matches(s, 0x7E5211, 4, 0x19A0BC775B1B7057ull);
}

// $7E:5211
/* One pass of the race's wait for the tilemap it marked finished to be sent,
 * which the vblank upload marks done; then on at $7E:5215. Idle entry, 16-bit
 * A. The wait is for the frame (docs/TODO.md). */
void scene_race_wait_for_tilemap_upload_loop(Snes *s)
{
    uint16_t pending = scene_race_tilemap_pending(state_of(s));
    s->cpu.a = pending;
    m_set_nz16(s, pending);
    if (pending == 0)
        s->cpu.pc = 0x5215;
}

/* ---- The engine's sound --------------------------------------------------------------------- */

/* The race's code at $7E:4BCB, whose absolute addressing must reach the APU
 * ports. */
bool scene_race_engine_present(Snes *s)
{
    return m_bank_reaches_io(s) && scene_on_scene_page(s) && m_code_matches(s, 0x7E4BCB, 17, 0x0E86BCEA28400770ull);
}

/* The sound driver's command that sets the pitch of the effect voices, which
 * play the engine (docs/SOUND.md); the race writes it straight to the driver,
 * its parameter first, without waiting for the echo. */
enum { EFFECT_PITCH = 0x89 };

// $7E:4BD2
/* Unless a song is being sent: the engine's pitch to the sound driver. */
static void set_engine_pitch(const GameState *g, Platform *pf)
{
    au_driver_write(pf, PF_AT(0x7E4BD4), 1, scene_race_engine_pitch(g));
    au_driver_write(pf, PF_AT(0x7E4BD9), 0, EFFECT_PITCH);
}

/* Continues at $7E:4BDC with 8-bit A the command. */
void scene_race_set_engine_pitch(Snes *s)
{
    set_engine_pitch(state_of(s), m_platform(s));
    m_set_a8(s, EFFECT_PITCH);
    m_set_nz8(s, EFFECT_PITCH);
    s->cpu.pc = 0x4BDC;
}
