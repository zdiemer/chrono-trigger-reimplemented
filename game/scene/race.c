/* Scenes: the jet bike race's setup and display.
 *
 * The race is a Mode 7 plane -- the track, seen in perspective -- drawn on the
 * lines its per-line effects leave in mode 7, with flat layers (the sky, the
 * scenery) above it in mode 1. The program sets the race up from its mode
 * table: its graphics unpacked from the program's resources and sent to VRAM
 * (the unpacker is still the original's, a call-out per resource), the racers'
 * pictures arranged for the scaler, its work buffers cleared, its per-line
 * effects, the bikes at the start, the scripts its two channels run, and the
 * flat layers. Then every frame it moves the camera (the plane's rotation
 * center and the flat layers' scroll), sends a column of the plane's name table
 * where the track scrolls in, the scaled road strip, and whichever half of the
 * racers' tilemap the race has finished drawing. The engine's sound follows the
 * bike: the race sets the pitch of the effect voices every frame, and sends its
 * own effects (the bikes bumping, the button that ends the scene) and waits for
 * the driver to take them. Without its panel (the attract sequence's) the race
 * ends behind a curtain.
 *
 * Only `events-jetbike-race` reaches any of it. It is the scene program's
 * code, in work RAM, so every entry is guarded by a hash of its code. What the
 * race does each frame -- the bikes, the camera, the drawing -- is in
 * race_rules.c, race_camera.c, race_projection.c and race_drawing.c (race.h). */
#include "scene/race.h"

#include "platform/audio.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "charge.h"
#include "cpu_io.h"
#include "original_effects.h"
#include "vars.h"
#include "wram.h"


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

/* Clear a buffer by copying the Mode 7 multiplier's product (0 here) out of
 * the PPU, the way scene_clear_work_ram does: a transfer that reads a register
 * rather than writing one, which goes through the SNES-only escape. */
static void clear_buffer(Platform *pf, uint32_t buffer, uint16_t bytes, uint32_t site)
{
    const VidSnesTransfer t = { .dest = VID_SNES_MULTIPLIER_PRODUCT, .source = buffer, .size = bytes };
    vid_snes_copy(pf, PF_AT(site), &t);
}

/* The registers a stop leaves nothing in that the program reads. */
enum { NOTHING_READ = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C };

/* ---- The setup's pieces ------------------------------------------------------------------------ */

// $7E:3C09
/* The race's two work buffers cleared: the per-line tables its effects read,
 * and the tilemap buffer with what follows it. */
static void clear_race_buffers(Platform *pf)
{
    clear_buffer(pf, LINE_TABLES, LINE_TABLES_BYTES, 0x7E3C22);
    clear_buffer(pf, (uint32_t)SCENE_LOWER_WORK_RAM << 16 | SCENE_TILEMAP_BUFFER, TILEMAP_BUFFER_BYTES, 0x7E3C3E);
}

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
                    VID_BG1_BIT | VID_BG2_BIT | VID_BG3_BIT | VID_OBJ_BIT, VID_BG3_BIT, VID_SCREEN_MAIN | VID_SCREEN_SUB);
    vid_start_vram_uploads(pf, PF_AT(0x7E3D1F), &down_a_column);
    vid_set_vram_address(pf, PF_AT(0x7E3D25), PLANE_COLUMN_VRAM);

    const VidSnesTransfer t = { .dest = VID_SNES_VRAM_READ, .source = PLANE_COLUMN_COPY,
                                .size = PLANE_COLUMN_READ_BYTES };
    vid_snes_copy(pf, PF_AT(0x7E3D41), &t);
}

// $7E:3D8C
/* Both halves of the racers' tilemap filled with the blank tile, before the
 * race draws into them. */
static void fill_racers_map(Platform *pf)
{
    vid_start_vram_uploads(pf, PF_AT(0x7E3D8E), SCENE_VRAM_WHOLE_WORDS);
    vid_send_vram_fill(pf, PF_AT(0x7E3DB0), RACERS_MAP_A_VRAM, PF_GAME(SCENE_BLANK_TILE), RACERS_MAP_BYTES, NULL);
}

// $7E:8752
/* Half the plane's 128x128 name table, from where the loader unpacked it, into
 * the low byte of each VRAM word from where the half before it ended: the two
 * calls cover it all. One byte to a word is what the upload calls model as
 * words, so it goes through the SNES-only escape, which the observer follows
 * byte by byte. */
static void upload_plane_half(Platform *pf)
{
    vid_start_vram_uploads(pf, PF_AT(0x7E8752), SCENE_VRAM_LOW_BYTES);
    const VidSnesTransfer t = { .dest = VID_SNES_VRAM_DATA_LOW, .source = PLANE_MAP_HALF, .size = PLANE_MAP_HALF_BYTES };
    vid_snes_copy(pf, PF_AT(0x7E876E), &t);
}

/* ---- Where the setup calls the original's code ------------------------------------------------- */

/* The direct page and the data bank the original has at a call. */
static void on_page(Snes *s, uint16_t page, uint8_t bank)
{
    s->cpu.d = page;
    s->cpu.db = bank;
}

/* The program's unpacker ($7E:3691): the resource's place in the program's
 * resource list in X, and the unpacking into the destination the caller has
 * set; it reads nothing else but the overflow, the data bank and the direct
 * page, which it hands back. */
enum { UNPACK = 0x7E3691, UNPACK_IGNORES = DC_A | DC_Y | DC_N | DC_Z | DC_C };

static void unpack(Snes *s, uint32_t site, uint16_t resource)
{
    s->cpu.x = resource;
    m_jsr_dc(s, site, UNPACK, UNPACK_IGNORES);
}

/* While the setup works on the racers' pictures the program keeps the data
 * bank and the direct page on the stack, works through the work RAM port's
 * registers' page with the data bank on its pictures' bank, and its sums have
 * left the overflow clear. */
enum { REGISTERS_PAGE = M_DP_PPU };

static void unpack_holding(Snes *s, uint32_t site, uint16_t resource)
{
    uint8_t bank = s->cpu.db;
    uint16_t page = m_direct_page(s);
    m_push8(s, bank);
    m_push16(s, page);
    on_page(s, REGISTERS_PAGE, SCENE_UPPER_WORK_RAM);
    m_set_flag(s, CPU_FLAG_V, false);
    unpack(s, site, resource);
    page = m_pull16(s);
    on_page(s, page, m_pull8(s));
}

/* The camera script's first step: the program's routine for channel 0's
 * ($7E:3749), called with 16-bit A and its data bank kept on the stack. It
 * reads Y and the overflow: Y as the divisors' clear left it, past the table
 * (scene_race_divisors' 256 bytes from $1446, and one), and the overflow clear. */
enum { FIRST_STEP_CALL = 0x7E3CD8, CHANNEL_0_STEP = 0x7E3749, DIVISORS_CLEARED_TO = 0x1546 };
enum { FIRST_STEP_IGNORES = DC_A | DC_X | DC_N | DC_Z | DC_C };

static void first_script_step(Snes *s)
{
    uint8_t bank = s->cpu.db;
    m_push8(s, bank);
    m_set_flag(s, CPU_FLAG_M, false);
    s->cpu.y = DIVISORS_CLEARED_TO;
    m_set_flag(s, CPU_FLAG_V, false);
    m_jsr_dc(s, FIRST_STEP_CALL, CHANNEL_0_STEP, FIRST_STEP_IGNORES);
    on_page(s, m_direct_page(s), m_pull8(s));
}

/* ---- Setting the race up ------------------------------------------------------------------------- */

/* The race's buffers, where it puts its graphics as it unpacks them, in the
 * program's two banks of work RAM. */
enum {
    UNPACKED_PALETTE = 0x002000,         /* the first bank, as the unpacker takes it (a bank byte of 0) */
    SCENE_PALETTE = 0x7E2200, PALETTE_BYTES = 0x200,
    UNPACK_BUFFER = 0x7F0200,            /* the unpacker's output for what goes on to VRAM */
    PLANE_ROW_BYTES = 0x40, PLANE_ROWS_A_HALF = 0x40,
};

/* The pictures the racers are scaled from (the bank of pictures, in the second
 * bank): rows of 64 pixels, a byte a pixel. Crono's are unpacked in two parts,
 * the second's eight-pixel blocks placed in the first's rows; Johnny's are
 * Crono's mirrored, half a row each, beside 32 empty pixels; the shared
 * pictures' last part is mirrored too. */
enum {
    PICTURES = 0x7F0A00, PICTURE_ROW = 0x40,
    CRONO_PICTURES_SECOND = 0x7F8A00,
    CRONO_PICTURES_COPY = 0x7F3800, CRONO_PICTURES_COPY_BYTES = 0x2E00,
    BLOCK_ROWS = 8, BLOCK_PIXELS = 8,
    MIRRORED_FIRST = 0x7F5A00, MIRRORED_SECOND = 0x7F7800, MIRRORED_THIRD = 0x7FAE00,
    MIRRORED_PIXELS = 0x20,
};

/* The race's graphics in the program's resources, each unpacked and sent where
 * it goes: the palette, the plane's pixels and its name table (in two halves,
 * each source row doubled across), the flat layers' and the sprites' tiles,
 * and the flat layers' maps. Each names the program's calls it stands for. */
typedef enum { INTO_PALETTE, PLANE_PIXELS, PLANE_MAP_PART, TILES, LAYER_MAP } RaceGraphicsKind;

typedef struct {
    uint16_t resource;
    RaceGraphicsKind kind;
    uint16_t vram;
    uint32_t vram_site;     /* where the program points VRAM at it (0: where the last left off) */
} RaceGraphics;

static const RaceGraphics race_graphics[] = {
    { 0x0F, INTO_PALETTE, 0, 0 },
    { 0x15, PLANE_PIXELS, PLANE_VRAM, 0x7E3DCB },
    { 0x18, PLANE_MAP_PART, PLANE_VRAM, 0x7E3DE4 },
    { 0x1B, PLANE_MAP_PART, PLANE_VRAM, 0 },
    { 0x27, TILES, LAYER_TILES_VRAM, 0x7E3E13 },
    { 0x2A, TILES, MORE_LAYER_TILES_VRAM, 0x7E3E23 },
    { 0x12, TILES, SPRITE_TILES_VRAM, 0x7E3E33 },
    { 0x1E, LAYER_MAP, BG1_MAP_VRAM, 0x7E3E77 },
    { 0x24, LAYER_MAP, BG3_MAP_VRAM, 0x7E3E8D },
    { 0x21, LAYER_MAP, BG2_MAP_VRAM, 0x7E3EA3 },
};

/* The program's call that unpacks each, beside the table. */
static const uint32_t race_graphics_unpacked_at[] = {
    0x7E3DBF, 0x7E3DD1, 0x7E3DEA, 0x7E3DF7, 0x7E3E19, 0x7E3E29, 0x7E3E39, 0x7E3E7D, 0x7E3E93, 0x7E3EA9,
};
enum { RACE_GRAPHICS = sizeof race_graphics / sizeof race_graphics[0] };

/* A layer's map is the first $800 bytes of what its resource unpacks to. */
enum { LAYER_MAP_BYTES = 0x800 };

/* The program's transfer (scene_transfer), set to send what the unpacker made
 * a byte at a time into the high half of VRAM's words (the plane's pixels), or
 * a word at a time. */
static void send_unpacked_as(GameState *g, uint8_t mode, uint8_t port_register)
{
    set_scene_call_transfer_control(g, scene_program_own(), mode);
    set_scene_call_transfer_register(g, scene_program_own(), port_register);
}

enum { BYTES_ONE_REGISTER = 0x00 };

// $7E:37A6
/* Half the plane's name table, from what was unpacked: each 64-tile row of
 * the track sent twice across the 128 tiles of the plane's row. */
static void double_plane_rows(GameState *g)
{
    for (uint32_t row = 0; row < PLANE_ROWS_A_HALF; row++) {
        uint32_t from = UNPACK_BUFFER + row * PLANE_ROW_BYTES, to = PLANE_MAP_HALF + row * 2 * PLANE_ROW_BYTES;
        wram_copy(g, from, to, PLANE_ROW_BYTES);
        wram_copy(g, from, to + PLANE_ROW_BYTES, PLANE_ROW_BYTES);
        ORIGINAL_EFFECT(charge_block_move(g, 2 * PLANE_ROW_BYTES));
    }
}

/* One of the race's graphics: VRAM pointed at it, unpacked (a call-out: the
 * unpacker is still the original's, from the program's call at
 * `unpack_site`), and sent or kept. */
static void load_graphics(Snes *s, uint32_t unpack_site, const RaceGraphics *r)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    if (r->vram_site)
        vid_set_vram_address(pf, PF_AT(r->vram_site), r->vram);
    unpack(s, unpack_site, r->resource);
    switch (r->kind) {
    case INTO_PALETTE:
        wram_copy(g, (uint32_t)SCENE_LOWER_WORK_RAM << 16 | UNPACKED_PALETTE, SCENE_PALETTE, PALETTE_BYTES);
        ORIGINAL_EFFECT(charge_block_move(g, PALETTE_BYTES));
        return;
    case PLANE_PIXELS:
        send_unpacked_as(g, BYTES_ONE_REGISTER, VID_SNES_VRAM_DATA_HIGH);
        TIMED_AS(g, 0x7E38E7, scene_send_unpacked(g, pf));
        return;
    case PLANE_MAP_PART:
        double_plane_rows(g);
        TIMED_AS(g, 0x7E8752, upload_plane_half(pf));
        return;
    case LAYER_MAP:
        set_decompress_length(g, LAYER_MAP_BYTES);
        TIMED_AS(g, 0x7E38E7, scene_send_unpacked(g, pf));
        return;
    case TILES:
        TIMED_AS(g, 0x7E38E7, scene_send_unpacked(g, pf));
        return;
    }
}

/* What changes as the graphics go from one kind to the next: past the palette,
 * the unpacker's output goes to the buffer the plane is sent from; before the
 * tiles, VRAM takes whole words and the transfer sends words; before the maps,
 * the road strip and the scaler's output are cleared. */
static void before_kind(GameState *g, Platform *pf, RaceGraphicsKind kind)
{
    switch (kind) {
    case PLANE_PIXELS:
        set_decompress_dest(g, UNPACK_BUFFER);
        return;
    case TILES:
        vid_start_vram_uploads(pf, PF_AT(0x7E3E03), SCENE_VRAM_WHOLE_WORDS);
        send_unpacked_as(g, SCENE_TRANSFER_WORDS, VID_SNES_VRAM_DATA_LOW);
        return;
    case LAYER_MAP:
        wram_fill8(g, ROAD_STRIP, ROAD_STRIP_BYTES, 0);
        ORIGINAL_EFFECT(charge_block_move(g, ROAD_STRIP_BYTES - 1));
        clear_buffer(pf, SCALER_OUTPUT, SCALER_OUTPUT_BYTES, 0x7E3E71);
        return;
    case INTO_PALETTE:
    case PLANE_MAP_PART:
        return;
    }
}

// $7E:3D8C
/* The race's graphics: the racers' tilemap blanked, then each in turn. */
static void load_race_graphics(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    fill_racers_map(pf);
    set_decompress_dest(g, UNPACKED_PALETTE);
    RaceGraphicsKind kind = INTO_PALETTE;
    for (unsigned i = 0; i < RACE_GRAPHICS; i++) {
        if (race_graphics[i].kind != kind) {
            kind = race_graphics[i].kind;
            before_kind(g, pf, kind);
        }
        load_graphics(s, race_graphics_unpacked_at[i], &race_graphics[i]);
    }
}

/* ---- The racers' pictures ----------------------------------------------------------------------- */

/* An 8x8 block of pixels into the pictures' rows at `to`. */
static void place_block(GameState *g, uint32_t from, uint32_t to)
{
    for (uint32_t row = 0; row < BLOCK_ROWS; row++)
        wram_copy(g, from + row * BLOCK_PIXELS, to + row * PICTURE_ROW, BLOCK_PIXELS);
    ORIGINAL_EFFECT(charge_block_move(g, BLOCK_ROWS * BLOCK_PIXELS));
}

/* Where the second part's blocks go in Crono's pictures, the second twice. */
static const uint16_t crono_blocks[] = { 0x3808, 0x3E08, 0x4408, 0x4C0C, 0x520C };
static const uint8_t crono_block_of[] = { 0, 1, 1, 2, 3 };
enum { CRONO_BLOCKS = sizeof crono_blocks / sizeof crono_blocks[0], BLOCK_BYTES = BLOCK_ROWS * BLOCK_PIXELS };

// $7E:3EB7
/* The program counts each block's rows down in the scene's first cell. */
static void place_crono_blocks(GameState *g)
{
    for (unsigned i = 0; i < CRONO_BLOCKS; i++)
        place_block(g, CRONO_PICTURES_SECOND + crono_block_of[i] * BLOCK_BYTES,
                    (uint32_t)SCENE_UPPER_WORK_RAM << 16 | crono_blocks[i]);
    ORIGINAL_EFFECT(set_scene_call_first(g, scene_program_own(), 0));
}

// $7E:3ED3
/* `rows` rows of pictures mirrored into the rows from `to`: from each source
 * row, its pixels from `last` back to the row's start, then empty ones to
 * the end of the row. Returns where the next row goes. */
static uint32_t mirror_rows(GameState *g, uint32_t to, uint32_t from, uint16_t last, unsigned rows)
{
    for (unsigned row = 0; row < rows; row++) {
        for (unsigned i = 0; i < MIRRORED_PIXELS; i++)
            wram_write8(g, to + i, wram_read8(g, (from & 0xFF0000) | (uint16_t)(from + last - i)));
        wram_fill8(g, to + MIRRORED_PIXELS, PICTURE_ROW - MIRRORED_PIXELS, 0);
        to += PICTURE_ROW;
        from = (from & 0xFF0000) | (uint16_t)(from + PICTURE_ROW);
    }
    return to;
}

/* The mirroring's two cells in the second bank: the source's offset and a
 * count of runs, both at the start of the unpack buffer. */
enum { MIRROR_LAST = 0x7F0200, MIRROR_RUNS = 0x7F0202 };
enum { MIRROR_RUN_ROWS = 8 };

static void leave_mirror_last(GameState *g, uint16_t last) { wram_write16(g, MIRROR_LAST, last); }
static void leave_mirror_runs(GameState *g, uint8_t runs) { wram_write8(g, MIRROR_RUNS, runs); }

/* Johnny's pictures, mirrored from Crono's. */
enum { FIRST_LAST = 0x20, CRONO_FIRST_ROWS = 72, CRONO_SECOND_ROWS = 48, CRONO_COPY_ROWS = 72 };
enum { CRONO_SECOND_PART = 0x7F2C00 };

static void mirror_crono(GameState *g)
{
    uint32_t to = mirror_rows(g, MIRRORED_FIRST, PICTURES, FIRST_LAST, CRONO_FIRST_ROWS);
    mirror_rows(g, to, CRONO_SECOND_PART, FIRST_LAST, CRONO_SECOND_ROWS);
    mirror_rows(g, MIRRORED_SECOND, CRONO_PICTURES_COPY, FIRST_LAST, CRONO_COPY_ROWS);
}

/* The last part, unpacked over Crono's second, mirrored from its 24th pixel. */
enum { THIRD_RESOURCE = 0x0C, THIRD_LAST = 0x18, THIRD_RUNS = 12 };

/* The work RAM port as the mirroring leaves it, writing the rows: after
 * Crono's pictures, in the second bank (which the program names by the bank
 * register's one bit), and after the last part's, in the bank the unpacker
 * left it in. */
enum { AFTER_CRONO_MIRRORED = 0x8A00, AFTER_THIRD_MIRRORED = 0xC600, PORT_SECOND_BANK = 0x01 };

/* The racers' pictures: Crono's unpacked in two parts and the first copied on,
 * the second's blocks placed in it, Johnny's mirrored from them, and the last
 * part unpacked and mirrored. The program keeps the data bank and the direct
 * page on the stack while it works on them, through the last part's unpacking. */
enum { CRONO_FIRST_RESOURCE = 0x06, CRONO_SECOND_RESOURCE = 0x09 };

static void load_racers_pictures(Snes *s)
{
    GameState *g = state_of(s);
    set_decompress_dest(g, PICTURES);
    unpack(s, 0x7E3B69, CRONO_FIRST_RESOURCE);
    set_decompress_dest(g, CRONO_PICTURES_SECOND);
    unpack(s, 0x7E3B75, CRONO_SECOND_RESOURCE);
    wram_copy(g, PICTURES, CRONO_PICTURES_COPY, CRONO_PICTURES_COPY_BYTES);
    ORIGINAL_EFFECT(charge_block_move(g, CRONO_PICTURES_COPY_BYTES));
    place_crono_blocks(g);
    mirror_crono(g);
    ORIGINAL_EFFECT(leave_mirror_last(g, THIRD_LAST));
    ORIGINAL_EFFECT(hw_port_seek(g, AFTER_CRONO_MIRRORED, PORT_SECOND_BANK));
    unpack_holding(s, 0x7E3BEF, THIRD_RESOURCE);
    mirror_rows(g, MIRRORED_THIRD, CRONO_PICTURES_SECOND, THIRD_LAST, THIRD_RUNS * MIRROR_RUN_ROWS);
    ORIGINAL_EFFECT(leave_mirror_runs(g, 0));
    ORIGINAL_EFFECT(hw_port_seek_address(g, AFTER_THIRD_MIRRORED));
}

/* ---- The race's scripts and the demo's screen ------------------------------------------------------ */

/* The camera's script and the race's, on channels 0 and 1: the race's own, or
 * in the demo one of the two camera scripts in turn, each showing. */
enum {
    RACE_CAMERA_SCRIPT = 0x3F14, RACE_SCRIPT = 0x400A,
    DEMO_CAMERA_SCRIPT = 0x404D, DEMO_OTHER_CAMERA_SCRIPT = 0x40C3, DEMO_SCRIPT = 0x415E,
};

/* The scene's fade steps every 8 frames. */
enum { FADE_PERIOD = 8 };

/* The race's per-line effects (all but the curtain's, with the panel), and
 * the descriptor table they come from. */
enum { RACE_LINE_EFFECTS = 0x3F, CURTAIN_LINE_EFFECT = 0x01, LINE_EFFECT_DESCRIPTORS = 0x3D45 };

/* The race without its panel, the attract sequence's, closes behind a
 * curtain: window 1 inside on every layer and the color window, masking
 * BG1-BG3 and the sprites on the main screen and BG3 on the sub, its left edge
 * moved by a per-line table at $7E:E000 (scene_curtain_*), from the right as
 * the scene ends. */
enum { CURTAIN_TABLE = 0xE000, CURTAIN_FROM_THE_RIGHT = 2 };

static const VidWindows race_curtain = {
    .w1 = { [VID_BG1] = VID_WIN_IN, [VID_BG2] = VID_WIN_IN, [VID_BG3] = VID_WIN_IN, [VID_BG4] = VID_WIN_IN,
            [VID_OBJ] = VID_WIN_IN, [VID_BACK] = VID_WIN_IN },
    .w1_left = 0x00, .w1_right = 0xFF, .w2_left = 0x00,
    .win_main = VID_BG1_BIT | VID_BG2_BIT | VID_BG3_BIT | VID_OBJ_BIT, .win_sub = VID_BG3_BIT,
};

static void set_up_curtain(GameState *g, Platform *pf)
{
    set_scene_curtain_table_bank(g, SCENE_LOWER_WORK_RAM);
    set_scene_curtain_table_bank_high(g, 0);
    set_scene_curtain_table(g, CURTAIN_TABLE);
    set_scene_curtain_steps(g, 0);
    set_scene_race_ending_sweep(g, CURTAIN_FROM_THE_RIGHT);
    vid_set_windows(pf, PF_AT(0x7E3C9E), &race_curtain,
                    VID_WINDOWS_BG1_BG2 | VID_WINDOWS_BG3_BG4 | VID_WINDOWS_OBJ_BACK | VID_WINDOWS_W1_LEFT |
                        VID_WINDOWS_W1_RIGHT | VID_WINDOWS_W2_LEFT | VID_WINDOWS_MAIN | VID_WINDOWS_SUB);
}

/* The scripts the race starts with: with the panel, the race's own, without
 * the curtain, and Crono's score so far cleared; without it, the curtain and
 * the next of the two camera scripts, a showing each. */
static void choose_scripts(GameState *g, Platform *pf)
{
    set_scene_fade_period(g, FADE_PERIOD);
    set_scene_fade_countdown(g, FADE_PERIOD);
    if (!(scene_flags(g) & SCENE_FLAG_NO_PANEL)) {
        set_scene_channels_script(g, 0, RACE_CAMERA_SCRIPT);
        set_scene_channels_script(g, 1, RACE_SCRIPT);
        set_scene_line_effects(g, RACE_LINE_EFFECTS & ~CURTAIN_LINE_EFFECT);
        set_scene_race_final_score(g, 0);
        return;
    }
    set_up_curtain(g, pf);
    uint16_t showings = (uint16_t)(scene_race_demo_showings(g) + 1);
    set_scene_race_demo_showings(g, showings);
    set_scene_channels_script(g, 0, (showings & 1) ? DEMO_OTHER_CAMERA_SCRIPT : DEMO_CAMERA_SCRIPT);
    set_scene_channels_script(g, 1, DEMO_SCRIPT);
}

// $7E:3B48
/* The race's setup, from the program's mode table: the demo marked, the race's
 * song, its graphics and the racers' pictures, its buffers cleared and its
 * per-line effects, the bikes at the start, the scripts, the camera script's
 * first step, the perspective's and the main loop's calls pointed at the
 * race's, the regrouping code written, and the flat layers. */
enum { RACE_SONG = 0x2C };

static void mark_the_demo(GameState *g)
{
    if (race_in_attract_mode(g)) {
        uint16_t demo = scene_race_demo(g);
        set_scene_race_demo(g, (uint16_t)((demo & 0xFF00) | (uint8_t)(demo + 1)));
    }
}

/* The scaler's output taken as full before the first picture, which so clears
 * all of it past its own end. */
enum { SCALER_OUTPUT_AS_FULL = 0x0A00 };

/* The main loop's call each pass, while the race runs: the racers' pictures
 * scaled (scene_race_scale_pictures). */
enum { MAIN_LOOP_SCALES_PICTURES = 0x51D1, PERSPECTIVE_FROM_THE_HORIZON = 0x45B0 };

/* The regrouping code is written for the scaler's picture into the tilemap
 * buffer, through the work RAM port. */
enum { MATRIX_CODE = 0xB574, MATRIX_DESTINATION = SCENE_TILEMAP_BUFFER, MATRIX_SOURCE = 0x0200 };

void scene_race_set_up(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    mark_the_demo(g);
    if (!(scene_flags(g) & SCENE_FLAG_NO_SONGS)) {
        set_scene_song(g, RACE_SONG);
        set_scene_song_playing(g, RACE_SONG);
        scene_bridge_start_song(s, RACE_SONG);
    }
    scene_bridge_enter(s, 0x7E3B5D);
    load_race_graphics(s);
    scene_bridge_leave(s);
    load_racers_pictures(s);
    clear_race_buffers(pf);
    set_scene_scaler_output_end(g, SCALER_OUTPUT_AS_FULL);
    TIMED_AS(g, 0x7E36CC, scene_set_up_line_effects(g, pf, LINE_EFFECT_DESCRIPTORS));
    set_scene_line_effects(g, RACE_LINE_EFFECTS);
    race_start(g);
    choose_scripts(g, pf);
    first_script_step(s);
    set_scene_perspective_start(g, PERSPECTIVE_FROM_THE_HORIZON);
    set_scene_main_loop_call(g, MAIN_LOOP_SCALES_PICTURES);
    hw_port_seek(g, MATRIX_CODE, SCENE_LOWER_WORK_RAM);
    TIMED_AS(g, 0xC309E9, scene_write_matrix_code_for(g, MATRIX_DESTINATION, MATRIX_SOURCE));
    setup_race_maps(pf);
}

/* ---- Entry: the setup ------------------------------------------------------------------------------ */

/* The setup's code and the program's routines it runs inline: the loader and
 * the pictures' arrangers, the bikes' start and the divisors' clear, the
 * descriptors of the per-line effects, the palette's copy, the unpacker's
 * destination, the transfer, the plane's rows doubled and sent, the song's
 * start and the first script step. */
bool scene_race_set_up_present(Snes *s)
{
    return scene_on_scene_page(s) && m_bank_reaches_io(s) && m_code_matches(s, 0x7E3B48, 0x3CC, 0x513F3387F89BAD7Cull) &&
           m_code_matches(s, 0x7E45D6, 0x6D, 0x6ACAE04A5CF51C92ull) && m_code_matches(s, 0x7E4309, 0x10, 0xA622B4E22F9620ABull) &&
           m_code_matches(s, 0x7E37A6, 0x24, 0x5FBEEF82E0975340ull) && m_code_matches(s, 0x7E38AF, 0x14, 0xD31DAC893D4743DCull) &&
           m_code_matches(s, 0x7E89ED, 0x14, 0x22A581F747CC4E58ull) && m_code_matches(s, 0x7E38E7, 36, 0xDD4B67DA62CC8E67ull) &&
           m_code_matches(s, 0x7E8752, 32, 0xE25F56B609E8320Eull) && m_code_matches(s, 0x7E36CC, 60, 0x45191A42A1EEF6A8ull) &&
           m_code_matches(s, 0x7E375F, 0x1B, 0x5BCDF92C47B920DDull) && m_code_matches(s, 0x7E3749, 0x0B, 0xA2E313B1BE3A1D93ull);
}

/* Entered from the program's mode table with 8-bit A; back, the matrix code's
 * writer has left the data bank and the direct page at 0 and the overflow
 * clear, which the program reads. */
void scene_race_set_up_entry(Snes *s)
{
    scene_race_set_up(s);
    s->cpu.d = 0x0000;
    s->cpu.db = 0x00;
    m_set_flag(s, CPU_FLAG_V, false);
    s->cpu.p |= CPU_FLAG_M;
    m_rts(s);
}

bool scene_race_upload_plane_present(Snes *s) { return m_code_matches(s, 0x7E8752, 32, 0xE25F56B609E8320Eull); }

void scene_race_upload_plane(Snes *s)
{
    upload_plane_half(m_platform(s));
    m_rts(s);
}

/* ---- The curtain ----------------------------------------------------------------------------- */

/* Without its panel, the race ends behind a curtain (the field's, whose step
 * is still the original's: $C3:000E), a task while it sweeps. Its window's left
 * edge goes through one of two per-line tables, in the program's data, the
 * other each step. */
enum { CURTAIN_TABLE_EVEN = 0x3D6B, CURTAIN_TABLE_ODD = 0x3D64, CURTAIN_WINDOW_SLOT = 0 };

bool scene_race_sweep_curtain_present(Snes *s)
{
    return scene_on_scene_page(s) && m_bank_reaches_io(s) && m_code_matches(s, 0x7E3D64, 40, 0xF435DB14E38FCB6Eull);
}

// $7E:3D72
static uint16_t curtain_table(const GameState *g)
{
    return (scene_curtain_steps(g) & 1) ? CURTAIN_TABLE_ODD : CURTAIN_TABLE_EVEN;
}

static void show_curtain_step(const GameState *g, Platform *pf)
{
    bool odd = scene_curtain_steps(g) & 1;
    vid_set_line_table(pf, PF_AT(odd ? 0x7E3D81 : 0x7E3D88), CURTAIN_WINDOW_SLOT, curtain_table(g));
}

/* The curtain's step saves every register and puts them back, and reads
 * none but X, which it saves where the analysis counts it read: the task's
 * own, handed it as it is. */
enum { CURTAIN_STEP = 0xC3000E, CURTAIN_IGNORES = DC_A | DC_Y | DC_N | DC_V | DC_Z | DC_C };

/* What the task leaves: 8-bit A the steps halved, X the table, the carry the
 * step's oddness. */
static void leave_curtain_registers(Snes *s)
{
    const GameState *g = state_of(s);
    uint8_t steps = scene_curtain_steps(g);
    s->cpu.p |= CPU_FLAG_M;
    m_set_a8(s, (uint8_t)(steps >> 1));
    s->cpu.x = curtain_table(g);
    m_set_flag(s, CPU_FLAG_C, steps & 1);
    m_set_nz16(s, s->cpu.x);
}

void scene_race_sweep_curtain(Snes *s)
{
    m_jsl_dc(s, 0x7E3D72, CURTAIN_STEP, CURTAIN_IGNORES);
    show_curtain_step(state_of(s), m_platform(s));
    ORIGINAL_EFFECT(leave_curtain_registers(s));
    m_rts(s);
}

/* ---- The race running ----------------------------------------------------------------------- */

bool scene_race_move_camera_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E43F2, 0x55, 0xDBEA3A9A200F00DEull);
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

/* Then the camera as sent, kept for the program's per-line work to read
 * while the frame's work changes the originals: the plane's turn, the horizon
 * and the view's position. */
static void keep_camera_sent(GameState *g)
{
    set_scene_race_view_sent(g, 0, scene_race_plane_turn(g));
    set_scene_race_view_sent(g, 2, scene_race_view_position(g, 0));
    set_scene_race_view_sent(g, 1, scene_race_horizon(g));
    set_scene_race_view_sent(g, 3, scene_race_view_position(g, 1));
}

/* Nothing it leaves in the registers is read. */
void scene_race_move_camera(Snes *s)
{
    move_camera(state_of(s), m_platform(s));
    keep_camera_sent(state_of(s));
    m_rts(s);
}

bool scene_race_upload_column_present(Snes *s) { return m_code_matches(s, 0x7E4643, 52, 0xAE7FE41C71F7EB77ull); }

/* A column of the plane's name table down into the low bytes of VRAM, a row
 * (128 words) at a time. The transfer writes the data port's low half alone,
 * which the upload calls model as words, so it goes through the SNES-only
 * escape. */
static void upload_column(Platform *pf, uint32_t column)
{
    const VidSnesTransfer t = { .dest = VID_SNES_VRAM_DATA_LOW, .source = column, .size = PLANE_COLUMN_BYTES,
                                .target_reg = VID_SNES_VRAM_ADDRESS, .target = PLANE_COLUMN_VRAM,
                                .port = &down_a_column };
    vid_snes_copy(pf, PF_AT(0x7E4671), &t);
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
    vid_send_vram(pf, PF_AT(0x7E525B), vram, PF_GAME(SCENE_TILEMAP_BUFFER), TILEMAP_HALF_BYTES, SCENE_VRAM_WHOLE_WORDS);
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
/* The scaled road strip, a word at a time. Also the end of the vblank task that
 * sets the horizon (scene_race_set_horizon, $7E:44FE), which goes on here. */
void race_upload_road_strip(Platform *pf)
{
    vid_send_vram(pf, PF_AT(0x7E5287), ROAD_STRIP_VRAM, PF_GAME(ROAD_STRIP), ROAD_STRIP_BYTES, SCENE_VRAM_WHOLE_WORDS);
}

void scene_race_upload_strip(Snes *s)
{
    race_upload_road_strip(m_platform(s));
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

/* The engine's pitch is Crono's speed's high byte. */
static uint8_t engine_pitch(const GameState *g) { return (uint8_t)(scene_race_racers_speed(g, RACER_CRONO) >> 8); }

// $7E:4BD2
/* The engine's pitch to the sound driver; the race skips it while a song is
 * being sent. */
void race_send_engine_pitch(const GameState *g, Platform *pf)
{
    au_driver_write(pf, PF_AT(0x7E4BD4), 1, engine_pitch(g));
    au_driver_write(pf, PF_AT(0x7E4BD9), 0, EFFECT_PITCH);
}

/* The race's own sound effects, sent by the program's send ($7E:377E), which
 * waits for the driver to take each: the command and its effect and pan into
 * the sound command buffer, then the call, with 8-bit A as the program makes
 * it. The send reads none of the registers but Y and the data bank, which it
 * leaves (and nothing after these calls reads A or the flags it changes). */
enum { SCENE_SEND = 0x7E377E };

static void send_effect(Snes *s, uint32_t site, uint8_t command, uint8_t effect)
{
    GameState *g = state_of(s);
    set_sound_command_parameters(g, 0, effect);
    set_sound_command_parameters(g, 1, AU_CENTRE);
    set_sound_command(g, command);
    m_set_flag(s, CPU_FLAG_M, true);
    au_driver_call(m_platform(s), PF_CALL(site, SCENE_SEND), DC_A | DC_X | DC_N | DC_V | DC_Z | DC_C);
}

/* The bikes bumping, on the second pair of effect voices. */
enum { BUMP_SOUND = 0x98, LEAVING_SOUND = 0xBA };

void race_sound_bump(Snes *s) { send_effect(s, 0x7E4BC8, AU_START_OTHER_EFFECT, BUMP_SOUND); }

/* The button that ends the scene after the race. */
void race_sound_leaving(Snes *s) { send_effect(s, 0x7E426D, AU_START_EFFECT, LEAVING_SOUND); }

/* Continues at $7E:4BDC with 8-bit A the command. */
void scene_race_set_engine_pitch(Snes *s)
{
    race_send_engine_pitch(state_of(s), m_platform(s));
    m_set_a8(s, EFFECT_PITCH);
    m_set_nz8(s, EFFECT_PITCH);
    s->cpu.pc = 0x4BDC;
}
