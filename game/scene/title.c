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
 * - the setup (scene_title_set_up), as the program's dispatch calls it: the
 *   title's resources unpacked (scene/boot.c) and sent to VRAM -- the sprites'
 *   tiles, the layers' tiles, the plane's pixels (built from 4-bit tiles, a
 *   row at a time: expand_plane_tile) and its name table, and BG1's and BG2's
 *   tilemaps -- the palette, channel 0's first script, the title's state, the
 *   pendulum's screen (set_up_pendulum) and the title's song, whose start the
 *   original waits for; the loader's blocks are entries of their own too;
 * - the frame's tasks, which the task list runs as C (scene/frame.c): the
 *   pendulum's swing (swing_the_pendulum) and its place (place_pendulum), the
 *   logo's sweep and landing (move_logo), the sprites drawn over the picture
 *   -- the clock, its hands, the copyright and the name sliding in -- the
 *   wait for the bottom right picture to go, and the watch for a button that
 *   ends the title;
 * - the ripple: mode 2 (start_ripple), the offsets moved on each frame
 *   (ripple_columns) and sent to VRAM with BG3's scroll (upload_ripple).
 *
 * All of it is the scene program's code, in work RAM, so every entry is guarded
 * by a hash of its code. */
#include "scene/scene.h"

#include "charge.h"
#include "core/palette.h"
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
    vid_start_vram_uploads(m_platform(s), PF_AT(0x7E597D), SCENE_VRAM_WHOLE_WORDS);
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
 * supplies what the original doesn't program). The VRAM port is the setting the block before
 * left (`port`), and the decompressor's destination is moved to bank 0 first. */
static void fill_bg1_map(GameState *g, Platform *pf, uint8_t port)
{
    set_decompress_dest(g, decompress_dest(g) & 0x00FFFF);
    VidVramPort vram_port = vid_vram_port_decode(port);
    vid_start_vram_uploads(pf, PF_AT(0x7E59D8), &vram_port);
    const VidSnesTransfer t = { .size = BG1_MAP_REST_BYTES, .target_reg = VID_SNES_VRAM_ADDRESS,
                                .target = BG1_MAP_REST_VRAM, .continue_channel = true };
    vid_snes_copy(pf, PF_AT(0x7E59DD), &t);
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
 * 32, which the loader has put at $0200 in a bank of work RAM (the second, in
 * the game) as words whose low bytes are the tile numbers; the rest of each row
 * is tile 0, and the rest of the table the blank tile. */
enum { PLANE_ROW = 128, PICTURE_ROWS = 32, PICTURE_COLUMNS = 32 };
enum { PICTURE_TILES = 0x0200, PICTURE_BYTES = PICTURE_ROWS * PICTURE_COLUMNS * 2 };
enum { REST_OF_PLANE_BYTES = 0x1000 };

// $7E:5AB6
/* The plane's name table: the picture's rows, a tile at a time through the
 * VRAM port, then the rest by a transfer. The transfer carries on from where
 * the rows left the VRAM address, and repeats its two bytes into the data
 * port's low half alone, which the upload calls model as words, so it goes
 * through the SNES-only escape. */
void scene_load_plane_map(GameState *g, Platform *pf, uint8_t picture_bank)
{
    vid_start_vram_uploads(pf, PF_AT(0x7E5ABE), SCENE_VRAM_LOW_BYTES);
    vid_set_vram_address(pf, PF_AT(0x7E5AC4), PLANE_VRAM);
    for (int row = 0; row < PICTURE_ROWS; row++) {
        for (int column = 0; column < PICTURE_COLUMNS; column++) {
            uint32_t at = (uint32_t)picture_bank << 16 | (uint16_t)(PICTURE_TILES + (row * PICTURE_COLUMNS + column) * 2);
            uint16_t word = wram_read16(g, at);
            vid_write_vram_byte(pf, PF_AT(0x7E5ACE), false, (uint8_t)word);
        }
        for (int column = PICTURE_COLUMNS; column < PLANE_ROW; column++)
            vid_write_vram_byte(pf, PF_AT(0x7E5ADC), false, 0);
    }
    const VidSnesTransfer t = { .dest = VID_SNES_VRAM_DATA_LOW, .source = SCENE_BLANK_TILE,
                                .size = REST_OF_PLANE_BYTES };
    vid_snes_copy(pf, PF_AT(0x7E5AEB), &t);
    /* the port, where the original's reads of the picture leave it */
    ORIGINAL_EFFECT(hw_port_seek_address(g, (uint16_t)(PICTURE_TILES + PICTURE_BYTES)));
}

/* The original reads the picture through the work RAM port, setting its
 * address and not its bank: the picture is in the bank the loader left the
 * port in. A comes back with the transfer's start in its low byte. */
void scene_title_load_plane_map(Snes *s)
{
    uint8_t picture_bank = (s->bus.wram_port_addr & 0x10000) ? SCENE_UPPER_WORK_RAM : SCENE_LOWER_WORK_RAM;
    scene_load_plane_map(state_of(s), m_platform(s), picture_bank);
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
                                            .layers = VID_BG1_BIT | VID_BACK_BIT };
static const VidBg pendulum_bg1 = { .map_base = BG1_MAP_VRAM, .map_w = 64, .map_h = 32 };
static const VidBg pendulum_bg2 = { .map_base = BG2_MAP_VRAM, .map_w = 32, .map_h = 32 };
static const VidBg pendulum_bg3 = { .map_base = BG3_MAP_VRAM, .map_w = 32, .map_h = 32 };
enum { PENDULUM_BG2_SCROLL = -192 };

// $7E:5A59
static void set_up_pendulum(Platform *pf)
{
    vid_set_mode(pf, PF_AT(0x7E5A61), &pendulum_mode);
    vid_set_mode7(pf, PF_AT(0x7E5A65), &pendulum_plane, VID_M7_SELECT);
    vid_set_screens(pf, PF_AT(0x7E5A6A), VID_BG1_BIT, VID_OBJ_BIT,
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

static TaskEnd place_pendulum_task(GameState *g, Platform *pf)
{
    place_pendulum(g, pf);
    return task_goes_on();
}

void scene_title_place_pendulum(Snes *s) { scene_task_alone(s, SCENE_TASK_PLACE_PENDULUM); }

/* ---- What the title's frame tasks leave -------------------------------------------------------------- */

/* A frame task leaves A as its last load or store had it, which the frame's
 * work reads the high byte of where the task is the list's last; a task that
 * draws sprites leaves the next free entry in Y, which the next task takes on.
 * The vblank tasks leave nothing anything reads. */
static TaskLeftovers leaves_a(uint16_t a) { return (TaskLeftovers){ .a_set = true, .a = a }; }

static TaskLeftovers leaves_a_and_the_next_free_entry(const GameState *g, uint16_t a)
{
    return (TaskLeftovers){ .a_set = true, .a = a, .y_set = true, .y = scene_sprite_index(g) };
}

static TaskLeftovers leaves_nothing_read(const GameState *g, TaskEnd end)
{
    (void)g;
    (void)end;
    return (TaskLeftovers){ 0 };
}

/* ---- The pendulum's swing ------------------------------------------------------------------------ */

/* The pendulum is the plane turned about its centre. Each frame its phase
 * moves on, and the plane turns a half turn and the phase's cosine times the
 * swing (a quarter of the product's middle bytes) more; channel 0's script
 * zooms it (its first register), which scales the turn's matrix, and the
 * plane's vertical scroll follows the zoom's reciprocal. The swing narrows by
 * one a frame, to the narrowest. */
enum { PLANE_ZOOM = 0, PHASE_STEP = 7, HALF_TURN = 0x200, ZOOM_ONE = 0x10000, SCROLL_TOP = 0x100, NARROWEST = 1 };

/* The middle two bytes of a product, where the program takes a scaled value. */
static uint16_t middle_of(uint32_t product) { return (uint16_t)(product >> 8); }

static uint16_t magnitude_of(int16_t v) { return (uint16_t)(v < 0 ? -v : v); }
static uint16_t signed_like(uint16_t magnitude, int16_t like) { return like < 0 ? (uint16_t)-magnitude : magnitude; }

/* How far the plane is turned this frame. */
static uint16_t pendulum_turn(GameState *g, uint16_t phase)
{
    int16_t lean = scene_cosine(g, phase);
    uint32_t leaning = scene_multiply16(g, scene_program_own(), magnitude_of(lean), scene_title_pendulum_swing(g));
    return (uint16_t)(signed_like((uint16_t)(middle_of(leaning) >> 2), lean) + HALF_TURN);
}

// $7E:5BCF
static void swing_the_pendulum(GameState *g)
{
    SceneProgramPage own = scene_program_own();
    uint16_t zoom = scene_channel_register(g, TITLE_CHANNEL, PLANE_ZOOM);
    uint16_t phase = (uint16_t)(scene_title_pendulum_phase(g) + PHASE_STEP);
    set_scene_title_pendulum_phase(g, phase);
    uint16_t turn = pendulum_turn(g, phase);
    /* the cosine is scaled as the word it is, its sign and all, and one more */
    uint16_t across = (uint16_t)(middle_of(scene_multiply16(g, own, (uint16_t)scene_cosine(g, turn), zoom)) + 1);
    int16_t sine = scene_sine(g, turn);
    uint16_t down = signed_like(middle_of(scene_multiply16(g, own, magnitude_of(sine), zoom)), sine);
    set_scene_mode7_matrix_a(g, across);
    set_scene_mode7_matrix_d(g, across);
    set_scene_mode7_matrix_b(g, down);
    set_scene_mode7_matrix_c(g, (uint16_t)-down);
    uint32_t reciprocal = scene_divide32(g, own, ZOOM_ONE, zoom, scene_sine_second_half(turn));
    set_scene_channel_register(g, TITLE_CHANNEL, BG1_SCROLL_Y, (uint16_t)(reciprocal - SCROLL_TOP));
    uint16_t swing = (uint16_t)(scene_title_pendulum_swing(g) - 1);
    set_scene_title_pendulum_swing(g, swing ? swing : NARROWEST);
}

static TaskEnd swing_pendulum_task(GameState *g, Platform *pf)
{
    (void)pf;
    swing_the_pendulum(g);
    return task_goes_on();
}

/* It leaves BG1's scroll, the last it stored. */
static TaskLeftovers swing_pendulum_leaves(const GameState *g, TaskEnd end)
{
    (void)end;
    return leaves_a(scene_channel_register(g, TITLE_CHANNEL, BG1_SCROLL_Y));
}

void scene_title_swing_pendulum(Snes *s) { scene_task_alone(s, SCENE_TASK_SWING_PENDULUM); }

/* The program's multiply comes out as the product for every operand the
 * pendulum gives it, but for the byte after the product, which carries into
 * each of the three in turn, shifted a place by each; and the zoom is never 0,
 * which the divide can't take. Both looked at the way the task will work them
 * out, without the machine's time. */
static bool swing_is_exact(GameState *g)
{
    uint16_t zoom = scene_channel_register(g, TITLE_CHANNEL, PLANE_ZOOM);
    if (zoom == 0)
        return false;
    uint8_t after = scene_call_after_the_product(g, scene_program_own());
    uint16_t phase = (uint16_t)(scene_title_pendulum_phase(g) + PHASE_STEP);
    int16_t lean = scene_cosine(g, phase);
    uint16_t swing = scene_title_pendulum_swing(g);
    if (!scene_multiplies_exactly(magnitude_of(lean), swing, after & 0x80))
        return false;
    uint32_t leaning = (uint32_t)magnitude_of(lean) * swing + (after & 0x80 ? 0x100 : 0);
    uint16_t turn = (uint16_t)(signed_like((uint16_t)(middle_of(leaning) >> 2), lean) + HALF_TURN);
    return scene_multiplies_exactly((uint16_t)scene_cosine(g, turn), zoom, after & 0x40) &&
           scene_multiplies_exactly(magnitude_of(scene_sine(g, turn)), zoom, after & 0x20);
}

/* The task, the sine's lookup, the multiply and the divide. */
bool scene_title_swing_pendulum_present(Snes *s)
{
    if (!scene_on_scene_page_low_bank(s) || !m_code_matches(s, 0x7E5BCF, 0x88, 0x7996EBA4B11B8E02ull) ||
        !m_code_matches(s, 0x7E3461, 0x19, 0xA2486F18B991C163ull) || !m_code_matches(s, 0x7E31BF, 74, 0x479627EBF1311CAFull) ||
        !m_code_matches(s, 0x7E339F, 55, 0x03E628A5184CD799ull))
        return false;
    vars_peek(true);
    bool ok = swing_is_exact(state_of(s));
    vars_peek(false);
    return ok;
}

/* ---- The logo ------------------------------------------------------------------------------------ */

bool scene_title_move_logo_present(Snes *s);

/* The logo sweeps in while the title's timer counts down by two a frame, and
 * lands when it drops below LOGO_LANDS: at (56, 58), with the script that
 * follows it given to channel 0. */
enum { LOGO_LANDS = 0x72, LOGO_X = 0x38, LOGO_Y = 0x3A, AFTER_THE_LOGO = 0x5B51 };

// $7E:5C98
/* The logo's task. Where it lands: BG1, BG2 and the sprites come on (and
 * nothing on the sub screen), it rests where it belongs, and channel 0 goes on
 * to the next script. True if it landed. The timer is compared by the sign of
 * the difference, as the original compares it, so a timer past $8000 lands at
 * once. */
static bool move_logo(GameState *g, Platform *pf)
{
    if ((int16_t)(scene_title_timer(g) - LOGO_LANDS) >= 0)
        return false;
    vid_set_screens(pf, PF_AT(0x7E5CA4), VID_BG1_BIT | VID_BG2_BIT | VID_OBJ_BIT, 0,
                    VID_SCREEN_MAIN | VID_SCREEN_SUB);
    set_scene_channels_script(g, TITLE_CHANNEL, AFTER_THE_LOGO);
    set_scene_title_logo_x(g, LOGO_X);
    set_scene_title_logo_y(g, LOGO_Y);
    return true;
}

/* Until it lands, the logo sweeps in along a curve as the timer counts down
 * two a frame: across by the timer's cosine times $D8 about 32 pixels left of
 * the screen's edge, and down by its sine times $C3 about 64 lines up. Each
 * sum takes in the carry the sine's lookup left (whether its step is in the
 * second half of the turn), as the original's does. */
enum { TIMER_STEP = 2, SWEEP_ACROSS = 0xD8, SWEEP_DOWN = 0xC3, SWEEP_LEFT = 0xFFE0, SWEEP_UP = 0xFFC0 };

static uint16_t magnitude_of_word(uint16_t v) { return (v & 0x8000) ? (uint16_t)-v : v; }

// $7E:5CBA
static void sweep_the_logo(GameState *g, uint16_t timer)
{
    SceneProgramPage own = scene_program_own();
    set_scene_title_timer(g, timer);
    int16_t across = scene_cosine(g, timer);
    uint16_t x = signed_like(middle_of(scene_multiply16(g, own, magnitude_of(across), SWEEP_ACROSS)), across);
    set_scene_title_logo_x(g, (uint16_t)(x + SWEEP_LEFT + scene_sine_second_half((uint16_t)(timer + 0x100))));
    uint16_t down = magnitude_of_word(middle_of(scene_multiply16(g, own, magnitude_of(scene_sine(g, timer)), SWEEP_DOWN)));
    set_scene_title_logo_y(g, (uint16_t)(down + SWEEP_UP + scene_sine_second_half(timer)));
}

/* Landed, the task is done. */
static TaskEnd move_logo_task(GameState *g, Platform *pf)
{
    if (move_logo(g, pf))
        return task_is_done();
    sweep_the_logo(g, (uint16_t)(scene_title_timer(g) - TIMER_STEP));
    return task_goes_on();
}

/* It leaves the logo's Y as it was last stored. */
static TaskLeftovers move_logo_leaves(const GameState *g, TaskEnd end)
{
    (void)end;
    return leaves_a(scene_title_logo_y(g));
}

void scene_title_move_logo(Snes *s) { scene_task_alone(s, SCENE_TASK_MOVE_LOGO); }

/* The task, the sine's lookup and the multiply; the multiply comes out as the
 * product for the sweep's two, with the byte after the product carried into
 * each in turn. */
bool scene_title_move_logo_present(Snes *s)
{
    if (!scene_on_scene_page_io_bank(s) || !m_code_matches(s, 0x7E5C98, 0x69, 0xFA57CFEE9EC2DECBull) ||
        !m_code_matches(s, 0x7E3461, 0x19, 0xA2486F18B991C163ull) || !m_code_matches(s, 0x7E31BF, 74, 0x479627EBF1311CAFull))
        return false;
    vars_peek(true);
    GameState *g = state_of(s);
    uint16_t timer = (uint16_t)(scene_title_timer(g) - TIMER_STEP);
    uint8_t after = scene_call_after_the_product(g, scene_program_own());
    bool ok = scene_multiplies_exactly(magnitude_of(scene_cosine(g, timer)), SWEEP_ACROSS, after & 0x80) &&
              scene_multiplies_exactly(magnitude_of(scene_sine(g, timer)), SWEEP_DOWN, after & 0x40);
    vars_peek(false);
    return ok;
}

/* ---- The routines the scripts call, reached by their addresses ------------------------------------ */

static const SceneScriptRoutine *routine_of(uint16_t routine);

/* The fades set the data bank to the first bank's around their stores, and
 * through the stack's imbalance come back with it on the program's. */
enum { PROGRAM_DATA_BANK = 0x7E };

/* A routine a caller reaches by its address (the interpreter runs them as C):
 * its work on the channel whose page D is, and back with 16-bit A. */
static void routine_alone(Snes *s, uint16_t routine)
{
    const SceneScriptRoutine *r = routine_of(routine);
    r->run(state_of(s), m_platform(s), scene_program_page_at_entry(s).index);
    ORIGINAL_EFFECT(s->cpu.a = r->leaves_a(s->cpu.a));
    if (r->leaves_the_program_bank)
        s->cpu.db = PROGRAM_DATA_BANK;
    s->cpu.p &= (uint8_t)~CPU_FLAG_M;
    m_rts(s);
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

static void start_ripple_on(GameState *g, Platform *pf, uint8_t channel) { start_ripple(g, pf, channel); }

/* It leaves the scroll it stored. */
static uint16_t start_ripple_leaves(uint16_t called_with)
{
    (void)called_with;
    return (uint16_t)RIPPLE_BG1_SCROLL_Y;
}

void scene_title_start_ripple(Snes *s) { routine_alone(s, SCENE_ROUTINE_START_RIPPLE); }

/* ---- The sprites' colors fading -------------------------------------------------------------------- */

/* The scripts' two fades of the title's sprite colors, in the palette
 * animations' first slot: sprite palette 4 fading out to black four levels a
 * frame, and sprite palette 3's first eleven colors, tinted yellow at half
 * strength, fading back to the colors as loaded two levels a frame. */
enum { TITLE_FADE_SLOT = 0 };
static const PaletteFade palette_4_darkens = {
    .first_color = 0xC0, .color_count = 16, .step = 4, .period = 1, .from_level = 0, .to_level = 0xFF,
};
static const PaletteFade palette_3_untints = {
    .first_color = 0xB0, .color_count = 11, .step = 2, .period = 1, .from_level = 0x80, .to_level = 0,
    .tint = PALETTE_TINT_YELLOW,
};

// $7E:5DB4
static void darken_sprite_palette_4(GameState *g, Platform *pf, uint8_t channel)
{
    (void)pf;
    (void)channel;
    palette_start_fade_to_black(g, TITLE_FADE_SLOT, palette_4_darkens);
}

// $7E:5DE9
static void untint_sprite_palette_3(GameState *g, Platform *pf, uint8_t channel)
{
    (void)pf;
    (void)channel;
    palette_start_fade_to_white(g, TITLE_FADE_SLOT, palette_3_untints);
}

/* Each stores its slot a byte at a time, the last byte stored over the high
 * byte A came with. */
static uint16_t darken_sprite_palette_4_leaves(uint16_t called_with)
{
    return (uint16_t)((called_with & 0xFF00) | palette_4_darkens.to_level);
}

static uint16_t untint_sprite_palette_3_leaves(uint16_t called_with)
{
    return (uint16_t)((called_with & 0xFF00) | palette_3_untints.to_level);
}

void scene_title_darken_sprite_palette_4(Snes *s) { routine_alone(s, SCENE_ROUTINE_DARKEN_SPRITE_PALETTE_4); }
void scene_title_untint_sprite_palette_3(Snes *s) { routine_alone(s, SCENE_ROUTINE_UNTINT_SPRITE_PALETTE_3); }

/* Their code: they set the data bank to the first bank's themselves. */
bool scene_title_darken_sprite_palette_4_present(Snes *s) { return m_code_matches(s, 0x7E5DB4, 0x35, 0x66F8CACF30C8ED2Aull); }
bool scene_title_untint_sprite_palette_3_present(Snes *s) { return m_code_matches(s, 0x7E5DE9, 0x35, 0xCD5DBFFFAB320D2Bull); }

/* The task, the sine's lookup and the multiply; the multiply comes out as the
 * product for the wave the task gives it. */
bool scene_title_ripple_columns_present(Snes *s);

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

/* The first column's new offset: the wave, sixteen sine steps a frame, times
 * the ripple's height (channel 0's seventh register), about the column's
 * place in the picture, as an entry that offsets BG1. */
enum { RIPPLE_HEIGHT = 6, WAVE_STEPS_A_FRAME_SHIFT = 4, PICTURE_PLACE = 0x4A, OFFSET_BITS = 0x03FF,
       OFFSETS_BG1 = 0x2000 };

static uint16_t wave(GameState *g)
{
    int16_t sine = scene_sine(g, (uint16_t)(scene_frame_counter(g) << WAVE_STEPS_A_FRAME_SHIFT));
    uint16_t height = scene_channel_register(g, TITLE_CHANNEL, RIPPLE_HEIGHT);
    return signed_like(middle_of(scene_multiply16(g, scene_program_own(), magnitude_of(sine), height)), sine);
}

// $7E:600E
static void wave_the_first_column(GameState *g)
{
    set_column_offset(g, 0, (uint16_t)(((uint16_t)(wave(g) - PICTURE_PLACE) & OFFSET_BITS) | OFFSETS_BG1));
}

static TaskEnd ripple_task(GameState *g, Platform *pf)
{
    (void)pf;
    ripple_columns(g);
    wave_the_first_column(g);
    return task_goes_on();
}

/* It leaves the first column's offset, the last it stored. */
static TaskLeftovers ripple_leaves(const GameState *g, TaskEnd end)
{
    (void)end;
    return leaves_a(column_offset(g, 0));
}

void scene_title_ripple_columns(Snes *s) { scene_task_alone(s, SCENE_TASK_RIPPLE); }

/* The ripple calms: every 32 frames its height drops by one, and at none the
 * task is done. */
enum { CALMS_EVERY = 0x1F };

// $7E:5BBF
static TaskEnd calm_the_ripple_task(GameState *g, Platform *pf)
{
    (void)pf;
    if (scene_frame_counter(g) & CALMS_EVERY)
        return task_goes_on();
    uint16_t height = (uint16_t)(scene_channel_register(g, TITLE_CHANNEL, RIPPLE_HEIGHT) - 1);
    set_scene_channel_register(g, TITLE_CHANNEL, RIPPLE_HEIGHT, height);
    return height ? task_goes_on() : task_is_done();
}

/* It leaves the frame counter's place in the 32, which it looked at. */
static TaskLeftovers calm_the_ripple_leaves(const GameState *g, TaskEnd end)
{
    (void)end;
    return leaves_a(scene_frame_counter(g) & CALMS_EVERY);
}

void scene_title_calm_the_ripple(Snes *s) { scene_task_alone(s, SCENE_TASK_CALM_THE_RIPPLE); }

bool scene_title_calm_the_ripple_present(Snes *s)
{
    return scene_on_scene_page_low_bank(s) && m_code_matches(s, 0x7E5BBF, 0x10, 0xEDDBC4A520DBAF3Dull);
}

bool scene_title_ripple_columns_present(Snes *s)
{
    if (!m_bank_reaches_low_ram(s) || !scene_on_scene_page(s) || !m_code_matches(s, 0x7E5FFE, 16, 0x0744D6BC8E4FFD3Cull) ||
        !m_code_matches(s, 0x7E600E, 0x31, 0xCD6FF1A3B834EB74ull) || !m_code_matches(s, 0x7E3461, 0x19, 0xA2486F18B991C163ull) ||
        !m_code_matches(s, 0x7E31BF, 74, 0x479627EBF1311CAFull))
        return false;
    vars_peek(true);
    GameState *g = state_of(s);
    int16_t sine = scene_sine(g, (uint16_t)(scene_frame_counter(g) << WAVE_STEPS_A_FRAME_SHIFT));
    bool ok = scene_multiplies_exactly(magnitude_of(sine), scene_channel_register(g, TITLE_CHANNEL, RIPPLE_HEIGHT),
                                       scene_call_after_the_product(g, scene_program_own()) & 0x80);
    vars_peek(false);
    return ok;
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
    vid_send_vram(pf, PF_AT(0x7E6215), BG3_MAP_VRAM, PF_GAME(SCENE_TILEMAP_BUFFER), OFFSET_ROWS_BYTES,
                  SCENE_VRAM_WHOLE_WORDS);
    vid_set_scroll(pf, PF_AT(0x7E623E), VID_BG3, scene_bg3_scroll_x(g), 0, VID_SCROLL_H);
}

static TaskEnd upload_ripple_task(GameState *g, Platform *pf)
{
    upload_ripple(g, pf);
    return task_goes_on();
}

void scene_title_upload_ripple(Snes *s) { scene_task_alone(s, SCENE_TASK_UPLOAD_RIPPLE); }

/* ---- The setup ---------------------------------------------------------------------------------- */

/* The title's resources, by their place in the program's resource table: the
 * sprites' tiles, the layers' tiles (which the plane's pixels are built from
 * too), BG1's map, BG2's map (whose first rows are the plane's picture too)
 * and the palette. */
enum {
    SPRITE_TILES_RESOURCE = 0x48, LAYER_TILES_RESOURCE = 0x45, BG1_MAP_RESOURCE = 0x4E, BG2_MAP_RESOURCE = 0x4B,
    PALETTE_RESOURCE = 0x51,
};

/* Where the unpacker puts them: the second bank's $0200 for what goes on to
 * VRAM; the palette at $2000 in the bank the fill of BG1's map leaves the
 * unpacker's destination in, the first. */
enum { UNPACKED_GRAPHICS = 0x7F0200, UNPACKED_PALETTE_AT = 0x2000 };

/* The program's transfer sends what was unpacked a word at a time into
 * VRAM's data port, a byte to each of its two registers. */
enum { WORDS_TO_TWO_REGISTERS = 0x01 };

/* The plane's pixels: the first 160 of the layers' tiles, 32 bytes each. */
enum { PLANE_TILES = 0xA0, TILE_PLANES_BYTES = 0x10 };

/* The title's resources are unpacked by the program's unpacker (scene/boot.c),
 * from the setup's own calls; it reads X, the resource, and the overflow. */
enum { UNPACKER = 0x7E3691, THE_UNPACKER_READS_X_AND_THE_OVERFLOW = DC_A | DC_Y | DC_N | DC_Z | DC_C };

static bool unpack(Snes *s, uint32_t site, uint16_t resource)
{
    s->cpu.x = resource;
    return scene_run(s, site, UNPACKER, THE_UNPACKER_READS_X_AND_THE_OVERFLOW, scene_unpack_resource);
}

static void send_unpacked(GameState *g, Platform *pf) { TIMED_AS(g, 0x7E38E7, scene_send_unpacked(g, pf)); }

// $7E:5E3C
/* The plane's pixels, from the layers' tiles as they were unpacked: each
 * tile expanded to a byte a pixel in turn. */
static void build_plane_pixels(GameState *g, Platform *pf)
{
    uint16_t tile = (uint16_t)UNPACKED_GRAPHICS;
    for (unsigned n = 0; n < PLANE_TILES; n++) {
        TIMED_AS(g, 0x7E5E54, tile = expand_plane_tile(g, pf, SCENE_UPPER_WORK_RAM, tile));
        tile = (uint16_t)(tile + TILE_PLANES_BYTES);
    }
}

/* The title's state as it starts: five sprites at the left that slide to
 * where they stop as it settles (in sixteenths of a pixel), the logo's
 * timer and where it starts its sweep, and the pendulum's swing. */
enum { SLIDES = 5, SLIDE_START = 0x05A0 };
static const uint16_t slide_ends[SLIDES] = { 0x0670, 0x0770, 0x0860, 0x09A0, 0x0A90 };
enum { LOGO_TIMER = 0x00F8, LOGO_START_X = 0x00D8, LOGO_START_Y = 0x0082 };
enum { PENDULUM_PHASE = 0x14C8, PENDULUM_SWING = 0x0200 };

static void the_title_starts(GameState *g)
{
    for (unsigned n = 0; n < SLIDES; n++) {
        set_scene_title_slide_ends(g, n, slide_ends[n]);
        set_scene_title_slides(g, n, SLIDE_START);
    }
    set_scene_title_timer(g, LOGO_TIMER);
    set_scene_title_logo_x(g, LOGO_START_X);
    set_scene_title_logo_y(g, LOGO_START_Y);
    set_scene_title_pendulum_phase(g, PENDULUM_PHASE);
    set_scene_title_pendulum_swing(g, PENDULUM_SWING);
}

/* The sprites' last palette's first color, white in the palette and on the
 * screen. */
enum { LOGO_HIGHLIGHT = 0xF1, WHITE = 0xFFFF };

/* Channel 0's first script, the pendulum's, run a turn from the setup ($7E:3749
 * on the channel's page); the setup keeps its page and data bank on the stack
 * across it, and the script's first commands read Y as the palette's copy left
 * it, past the working palette. */
enum { FIRST_SCRIPT = 0x5B05, CHANNEL_0_CALL = 0x7E5A0A, CHANNEL_0_STEP = 0x7E3749, PAST_THE_WORKING_PALETTE = 0x2400 };
enum { THE_STEP_READS_Y = DC_A | DC_X | DC_N | DC_Z };

static void leave_y_past_the_palette(Snes *s) { s->cpu.y = PAST_THE_WORKING_PALETTE; }

/* The palette animations' stop leaves Y past their last slot, which the
 * song's call to the driver reads. */
enum { PAST_THE_ANIMATIONS_SLOTS = 0x0090 };
static void leave_y_past_the_slots(Snes *s) { s->cpu.y = PAST_THE_ANIMATIONS_SLOTS; }

static bool run_the_first_script(Snes *s)
{
    uint16_t page = s->cpu.d;
    uint8_t bank = s->cpu.db;
    m_push16(s, page);
    m_push8(s, bank);
    m_set_flag(s, CPU_FLAG_M, false);
    ORIGINAL_EFFECT(leave_y_past_the_palette(s));
    if (!scene_run(s, CHANNEL_0_CALL, CHANNEL_0_STEP, THE_STEP_READS_Y, scene_run_channel_0_script))
        return false;
    s->cpu.db = m_pull8(s);
    s->cpu.d = m_pull16(s);
    return true;
}

/* The title's music, which the setup waits for. */
enum { TITLE_SONG = 0x30, SONG_CALL = 0x7E5AA7, MUSIC_WAIT = 0x7E5AAA };
enum { FULL_BRIGHTNESS = 0x0F, FADE_EVERY_FRAME = 1 };
enum { THE_WAIT_READS_A_HIGH_AND_THE_OVERFLOW = DC_AL | DC_X | DC_N | DC_Z | DC_D };

/* What the pendulum's screen leaves in A's high byte, written through a
 * direct page on the register block: the song's call hands it on to the
 * program's first frame. */
static void leave_a_on_the_registers_page(Snes *s) { s->cpu.a = (uint16_t)(M_DP_PPU | (s->cpu.a & 0x00FF)); }

/* The name table's transfer leaves the VRAM port's setting for whole words in
 * A, which the fill takes as its own. */
enum { WHOLE_WORDS_PORT = 0x80 };

// $7E:5978
/* The title set up, as the program's dispatch calls it (scene/program.c): its graphics unpacked
 * and sent, the plane's pixels built from the layers' tiles and its name
 * table from BG2's map, the rest of BG1's map filled, the palette made
 * current, channel 0's script started, the title's state, the logo's
 * highlight white, the pendulum's screen; the screen at full brightness, the
 * fades a step a frame, the palette animations stopped, and the title's song
 * started; true unless a call-out stopped it. It returns through the original's
 * wait for the song to start playing ($7E:5AAA), where the port stops. */
bool scene_title_set_up_scene(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    set_decompress_dest(g, UNPACKED_GRAPHICS);
    vid_start_vram_uploads(pf, PF_AT(0x7E597D), SCENE_VRAM_WHOLE_WORDS);
    vid_set_vram_address(pf, PF_AT(0x7E5983), SPRITE_TILES_VRAM);
    if (!unpack(s, 0x7E5989, SPRITE_TILES_RESOURCE))
        return false;
    set_scene_call_transfer_control(g, scene_program_own(), WORDS_TO_TWO_REGISTERS);
    set_scene_call_transfer_register(g, scene_program_own(), VID_SNES_VRAM_DATA_LOW);
    send_unpacked(g, pf);
    if (!unpack(s, 0x7E599D, LAYER_TILES_RESOURCE))
        return false;
    vid_set_vram_address(pf, PF_AT(0x7E59A3), LAYER_TILES_VRAM);
    send_unpacked(g, pf);
    vid_set_vram_address(pf, PF_AT(0x7E59AD), PLANE_VRAM);
    build_plane_pixels(g, pf);
    if (!unpack(s, 0x7E59B9, BG1_MAP_RESOURCE))
        return false;
    vid_set_vram_address(pf, PF_AT(0x7E59BF), BG1_MAP_VRAM);
    send_unpacked(g, pf);
    if (!unpack(s, 0x7E59C9, BG2_MAP_RESOURCE))
        return false;
    vid_set_vram_address(pf, PF_AT(0x7E59CF), BG2_MAP_VRAM);
    send_unpacked(g, pf);
    TIMED_AS(g, 0x7E5AB6, scene_load_plane_map(g, pf, scene_unpacked_bank(g)));
    fill_bg1_map(g, pf, WHOLE_WORDS_PORT);
    set_decompress_dest(g, (decompress_dest(g) & 0xFF0000u) | UNPACKED_PALETTE_AT);
    if (!unpack(s, 0x7E59FA, PALETTE_RESOURCE))
        return false;
    scene_use_loaded_palette(g);
    set_scene_channels_script(g, TITLE_CHANNEL, FIRST_SCRIPT);
    if (!run_the_first_script(s))
        return false;
    the_title_starts(g);
    set_palette_working(g, LOGO_HIGHLIGHT, WHITE);
    set_palette_base(g, LOGO_HIGHLIGHT, WHITE);
    set_up_pendulum(pf);
    set_scene_brightness(g, FULL_BRIGHTNESS);
    set_scene_fade_period(g, FADE_EVERY_FRAME);
    set_scene_fade_countdown(g, FADE_EVERY_FRAME);
    TIMED_AS(g, 0xFDFFEE, palette_stop_animations(g));
    ORIGINAL_EFFECT(leave_y_past_the_slots(s));
    set_scene_song(g, TITLE_SONG);
    set_scene_song_playing(g, TITLE_SONG);
    m_set_flag(s, CPU_FLAG_M, true);
    ORIGINAL_EFFECT(leave_a_on_the_registers_page(s));
    scene_bridge_start_song(s, SONG_CALL, TITLE_SONG);
    return true;
}

/* The original's wait for the title's song to start playing, which returns
 * to the program's first frame. */
void scene_title_wait_for_its_song(Snes *s) { m_stop_at(s, MUSIC_WAIT, THE_WAIT_READS_A_HIGH_AND_THE_OVERFLOW); }

void scene_title_set_up(Snes *s)
{
    if (scene_title_set_up_scene(s))
        scene_title_wait_for_its_song(s);
}

/* The setup's code, and the program's routines it runs inline: the
 * decompressor's destination set, the plane's tiles' loop, the palette made
 * current, the song started, the transfer, the plane's name table, BG1's
 * fill, the pendulum's screen and the tiles' expansion. */
bool scene_title_set_up_present(Snes *s)
{
    return scene_on_scene_page(s) && m_bank_reaches_io(s) && m_code_matches(s, 0x7E5978, 0x132, 0x83E38A729D4C39C0ull) &&
           m_code_matches(s, 0x7E5E3C, 24, 0x47BD25FE5072C3D3ull) && m_code_matches(s, 0x7E89ED, 0x14, 0x22A581F747CC4E58ull) &&
           m_code_matches(s, 0x7E38AF, 0x14, 0xD31DAC893D4743DCull) && m_code_matches(s, 0x7E375F, 0x1B, 0x5BCDF92C47B920DDull) &&
           scene_transfer_present(s) && scene_title_load_plane_map_present(s) && scene_title_set_up_pendulum_present(s) &&
           m_code_matches(s, 0x7E5E54, 164, 0xEE4E5864DAEE75ACull);
}

/* ---- The title's sprites --------------------------------------------------------------------------- */

/* The title's frame tasks that draw its sprites, each a list or two of the
 * program's (scene/sprites.c) where the title puts them. A list's routine
 * leaves the next free entry in Y, which the next task takes on. */
static void draw_list_at(GameState *g, uint16_t x, uint16_t y, uint16_t list)
{
    scene_draw_lists_at(g, x, y);
    TIMED_AS(g, 0xC30D0E, scene_draw_sprite_list(g, list, false));
}

/* The lists' sprites fit in the tables from the cursor, all looked at without
 * the machine's time. */
static bool lists_fit(Snes *s, const uint16_t *lists, unsigned count)
{
    vars_peek(true);
    unsigned sprites = 0;
    for (unsigned n = 0; n < count; n++)
        sprites += scene_sprite_list_length(state_of(s), lists[n]);
    bool ok = scene_sprites_fit(state_of(s), sprites);
    vars_peek(false);
    return ok;
}

/* The clock, in two lists, its upper half and its lower half a quarter of the
 * plane's height below, where the logo is. */
enum { CLOCK_UPPER = 0x603F, CLOCK_LOWER = 0x60BC, CLOCK_HALF = 0x40 };

// $7E:5D01
static void draw_the_clock(GameState *g)
{
    uint16_t x = scene_title_logo_x(g), y = scene_title_logo_y(g);
    draw_list_at(g, x, y, CLOCK_UPPER);
    draw_list_at(g, x, (uint16_t)(y + CLOCK_HALF), CLOCK_LOWER);
}

static TaskEnd draw_the_clock_task(GameState *g, Platform *pf)
{
    (void)pf;
    draw_the_clock(g);
    return task_goes_on();
}

static TaskLeftovers draw_the_clock_leaves(const GameState *g, TaskEnd end)
{
    (void)end;
    return leaves_a_and_the_next_free_entry(g, CLOCK_LOWER);
}

void scene_title_draw_the_clock(Snes *s) { scene_task_alone(s, SCENE_TASK_DRAW_THE_CLOCK); }

/* The clock's hands, where the clock settles. */
enum { HANDS = 0x61B4, HANDS_X = 0x3A, HANDS_Y = 0x64 };

// $7E:5D20
static TaskEnd draw_the_hands_task(GameState *g, Platform *pf)
{
    (void)pf;
    draw_list_at(g, HANDS_X, HANDS_Y, HANDS);
    return task_goes_on();
}

static TaskLeftovers draw_the_hands_leaves(const GameState *g, TaskEnd end)
{
    (void)end;
    return leaves_a_and_the_next_free_entry(g, HANDS);
}

void scene_title_draw_the_hands(Snes *s) { scene_task_alone(s, SCENE_TASK_DRAW_THE_HANDS); }

/* The copyright line, in two lists. */
enum { COPYRIGHT_Y = 0xD0, COPYRIGHT_LEFT = 0x6186, COPYRIGHT_LEFT_X = 0x78, COPYRIGHT_RIGHT = 0x61A7,
       COPYRIGHT_RIGHT_X = 0xB8 };

// $7E:5E1E
static TaskEnd draw_the_copyright_task(GameState *g, Platform *pf)
{
    (void)pf;
    draw_list_at(g, COPYRIGHT_LEFT_X, COPYRIGHT_Y, COPYRIGHT_LEFT);
    draw_list_at(g, COPYRIGHT_RIGHT_X, COPYRIGHT_Y, COPYRIGHT_RIGHT);
    return task_goes_on();
}

static TaskLeftovers draw_the_copyright_leaves(const GameState *g, TaskEnd end)
{
    (void)end;
    return leaves_a_and_the_next_free_entry(g, COPYRIGHT_RIGHT);
}

void scene_title_draw_the_copyright(Snes *s) { scene_task_alone(s, SCENE_TASK_DRAW_THE_COPYRIGHT); }

/* The rest of the name, five sprite groups that slide right out from behind
 * the clock as the title settles, each at its own pace (sixteenths of a pixel
 * a frame) until it stops where it belongs. */
enum { SLIDES_Y = 0x52, SUBPIXEL_SHIFT = 4, SCREEN_X_BITS = 0xFF };
static const struct {
    uint16_t list, pace;
} slides[SLIDES] = {
    { 0x60F1, 0x04 }, { 0x610A, 0x09 }, { 0x6123, 0x0E }, { 0x6148, 0x14 }, { 0x6161, 0x18 },
};

// $7E:5D71
static void slide_the_name(GameState *g)
{
    for (unsigned n = 0; n < SLIDES; n++) {
        uint16_t at = (uint16_t)(scene_title_slides(g, n) + slides[n].pace);
        uint16_t end = scene_title_slide_ends(g, n);
        if ((int16_t)(at - end) >= 0)   /* compared as the program compares, by the sign of the difference */
            at = end;
        set_scene_title_slides(g, n, at);
        draw_list_at(g, (uint16_t)((at >> SUBPIXEL_SHIFT) & SCREEN_X_BITS), SLIDES_Y, slides[n].list);
    }
}

static TaskEnd slide_the_name_task(GameState *g, Platform *pf)
{
    (void)pf;
    slide_the_name(g);
    return task_goes_on();
}

static TaskLeftovers slide_the_name_leaves(const GameState *g, TaskEnd end)
{
    (void)end;
    return leaves_a_and_the_next_free_entry(g, slides[SLIDES - 1].list);
}

void scene_title_slide_the_name(Snes *s) { scene_task_alone(s, SCENE_TASK_SLIDE_THE_NAME); }

/* ---- The title's wait for a button ----------------------------------------------------------------- */

/* The bottom right's picture (a list clipped at the screen's edge) shows while
 * the title's wait counts down a frame at a time. With a second of the wait
 * left the title starts watching for a button and lets the console reset; at
 * the end the picture goes, and its task leaves the list. */
enum { BOTTOM_RIGHT = 0x61D9, BOTTOM_RIGHT_X = 0x90, BOTTOM_RIGHT_Y = 0xB0, A_SECOND_LEFT = 60 };
// $7E:5D32
/* Done when the wait is over. */
static TaskEnd count_the_wait_down_task(GameState *g, Platform *pf)
{
    (void)pf;
    scene_draw_lists_at(g, BOTTOM_RIGHT_X, BOTTOM_RIGHT_Y);
    TIMED_AS(g, 0xC30D5E, scene_draw_sprite_list(g, BOTTOM_RIGHT, true));
    uint16_t left = (uint16_t)(scene_title_wait(g) - 1);
    set_scene_title_wait(g, left);
    if (left == 0)
        return task_is_done();
    if (left == A_SECOND_LEFT) {
        scene_add_task(g, SCENE_FRAME_TASKS, SCENE_TASK_WATCH_FOR_A_BUTTON);
        set_scene_reset_disabled(g, 0);
    }
    return task_goes_on();
}

/* It leaves what the picture's clipped list left, at the end; the watch it
 * adds, as it adds it; and the wait otherwise. */
static TaskLeftovers count_the_wait_down_leaves(const GameState *g, TaskEnd end)
{
    if (end.outcome == TASK_IS_DONE)
        return leaves_a_and_the_next_free_entry(g, scene_clipped_list_leaves_a(g, BOTTOM_RIGHT, BOTTOM_RIGHT));
    if (scene_title_wait(g) == A_SECOND_LEFT) {
        TaskLeftovers l = leaves_a_and_the_next_free_entry(g, SCENE_TASK_WATCH_FOR_A_BUTTON);
        l.a_8bit = true;   /* the reset's flag, stored a byte */
        return l;
    }
    return leaves_a_and_the_next_free_entry(g, scene_title_wait(g));
}

void scene_title_count_the_wait_down(Snes *s) { scene_task_alone(s, SCENE_TASK_COUNT_THE_WAIT_DOWN); }

/* The watch for a button: once the title takes buttons, and none has been
 * taken yet, any button ends the title -- the music fades out (scene/screen.c's
 * task) -- and the task leaves the list. A button other than the shoulders
 * also marks the game started from the title, for the field. */
enum { SHOULDER_BUTTONS = 0x30, STARTED_FROM_THE_TITLE = 1, BUTTON_TAKEN = 1 };

// $7E:5B99
/* Done once a button is taken. */
static TaskEnd watch_for_a_button_task(GameState *g, Platform *pf)
{
    (void)pf;
    if (scene_title_ignores_buttons(g) || scene_title_button_taken(g) || scene_joypad(g) == 0)
        return task_goes_on();
    set_scene_title_button_taken(g, BUTTON_TAKEN);
    scene_add_task(g, SCENE_FRAME_TASKS, SCENE_TASK_MUSIC_FADES);
    if (!(scene_joypad(g) & SHOULDER_BUTTONS))
        set_scene_title_left_with_a_button(g, STARTED_FROM_THE_TITLE);
    return task_is_done();
}

/* Watching, it leaves A the last word it looked at: the first of the two flags
 * that stopped it, or the buttons. */
static uint16_t last_looked_at(const GameState *g)
{
    if (scene_title_ignores_buttons(g))
        return scene_title_ignores_buttons(g);
    if (scene_title_button_taken(g))
        return scene_title_button_taken(g);
    return scene_joypad(g);
}

/* Taken, it leaves the task it added. */
static TaskLeftovers watch_for_a_button_leaves(const GameState *g, TaskEnd end)
{
    if (end.outcome != TASK_IS_DONE)
        return leaves_a(last_looked_at(g));
    TaskLeftovers l = leaves_a(SCENE_TASK_MUSIC_FADES);
    l.a_8bit = true;   /* the started flag, stored a byte */
    return l;
}

void scene_title_watch_for_a_button(Snes *s) { scene_task_alone(s, SCENE_TASK_WATCH_FOR_A_BUTTON); }

/* ---- The guards ------------------------------------------------------------------------------------- */

/* Each task's code, the list routines' room in the sprite tables, and for the
 * ones that add a task a free slot on the list (the program's search for one
 * runs on past the list). */
bool scene_title_draw_the_clock_present(Snes *s)
{
    return scene_on_scene_page_low_bank(s) && m_code_matches(s, 0x7E5D01, 0x1F, 0xB936C903C78757F8ull) &&
           lists_fit(s, (const uint16_t[]){ CLOCK_UPPER, CLOCK_LOWER }, 2);
}

bool scene_title_draw_the_hands_present(Snes *s)
{
    return scene_on_scene_page_low_bank(s) && m_code_matches(s, 0x7E5D20, 0x12, 0xC1DD4B546447006Full) &&
           lists_fit(s, (const uint16_t[]){ HANDS }, 1);
}

bool scene_title_draw_the_copyright_present(Snes *s)
{
    return scene_on_scene_page_low_bank(s) && m_code_matches(s, 0x7E5E1E, 0x1E, 0xEC6A93C4974CF6BFull) &&
           lists_fit(s, (const uint16_t[]){ COPYRIGHT_LEFT, COPYRIGHT_RIGHT }, 2);
}

bool scene_title_slide_the_name_present(Snes *s)
{
    if (!scene_on_scene_page_low_bank(s) || !m_code_matches(s, 0x7E5D5D, 0x14, 0xA935FDCB5F62F6B7ull) ||
        !m_code_matches(s, 0x7E5D71, 0x36, 0x85628350BE2195F4ull))
        return false;
    uint16_t lists[SLIDES];
    for (unsigned n = 0; n < SLIDES; n++)
        lists[n] = slides[n].list;
    return lists_fit(s, lists, SLIDES);
}

static bool a_slot_free(Snes *s)
{
    vars_peek(true);
    bool ok = scene_task_listed(state_of(s), SCENE_FRAME_TASKS, NO_TASK);
    vars_peek(false);
    return ok;
}

bool scene_title_count_the_wait_down_present(Snes *s)
{
    return scene_on_scene_page_io_bank(s) && m_code_matches(s, 0x7E5D32, 0x2B, 0x22D6F5D871A739F0ull) &&
           m_code_matches(s, 0x7E395E, 0x16, 0xBC6CC1EBF7C4F36Aull) &&
           lists_fit(s, (const uint16_t[]){ BOTTOM_RIGHT }, 1) && a_slot_free(s);
}

bool scene_title_watch_for_a_button_present(Snes *s)
{
    return scene_on_scene_page_low_bank(s) && m_code_matches(s, 0x7E5B99, 0x26, 0x155297E6694755C1ull) &&
           m_code_matches(s, 0x7E395E, 0x16, 0xBC6CC1EBF7C4F36Aull) && a_slot_free(s);
}

/* ---- The title's tasks, for the lists ------------------------------------------------------------- */

const SceneTaskPort scene_title_tasks[] = {
    { SCENE_TASK_SWING_PENDULUM, SCENE_FRAME_TASKS, swing_pendulum_task, scene_title_swing_pendulum_present,
      swing_pendulum_leaves, false },
    { SCENE_TASK_MOVE_LOGO, SCENE_FRAME_TASKS, move_logo_task, scene_title_move_logo_present, move_logo_leaves, false },
    { SCENE_TASK_DRAW_THE_CLOCK, SCENE_FRAME_TASKS, draw_the_clock_task, scene_title_draw_the_clock_present,
      draw_the_clock_leaves, false },
    { SCENE_TASK_DRAW_THE_HANDS, SCENE_FRAME_TASKS, draw_the_hands_task, scene_title_draw_the_hands_present,
      draw_the_hands_leaves, false },
    { SCENE_TASK_DRAW_THE_COPYRIGHT, SCENE_FRAME_TASKS, draw_the_copyright_task,
      scene_title_draw_the_copyright_present, draw_the_copyright_leaves, false },
    { SCENE_TASK_SLIDE_THE_NAME, SCENE_FRAME_TASKS, slide_the_name_task, scene_title_slide_the_name_present,
      slide_the_name_leaves, false },
    { SCENE_TASK_COUNT_THE_WAIT_DOWN, SCENE_FRAME_TASKS, count_the_wait_down_task,
      scene_title_count_the_wait_down_present, count_the_wait_down_leaves, false },
    { SCENE_TASK_WATCH_FOR_A_BUTTON, SCENE_FRAME_TASKS, watch_for_a_button_task,
      scene_title_watch_for_a_button_present, watch_for_a_button_leaves, false },
    { SCENE_TASK_RIPPLE, SCENE_FRAME_TASKS, ripple_task, scene_title_ripple_columns_present, ripple_leaves, false },
    { SCENE_TASK_CALM_THE_RIPPLE, SCENE_FRAME_TASKS, calm_the_ripple_task, scene_title_calm_the_ripple_present,
      calm_the_ripple_leaves, false },
    { SCENE_TASK_PLACE_PENDULUM, SCENE_VBLANK_TASKS, place_pendulum_task, scene_title_place_pendulum_present,
      leaves_nothing_read, false },
    { SCENE_TASK_UPLOAD_RIPPLE, SCENE_VBLANK_TASKS, upload_ripple_task, scene_title_upload_ripple_present,
      leaves_nothing_read, false },
};
const unsigned scene_title_task_count = sizeof scene_title_tasks / sizeof scene_title_tasks[0];

/* ---- The routines the title's scripts call, for the interpreter -------------------------------------- */

const SceneScriptRoutine scene_title_routines[] = {
    { SCENE_ROUTINE_START_RIPPLE, start_ripple_on, scene_title_start_ripple_present, start_ripple_leaves, false },
    { SCENE_ROUTINE_DARKEN_SPRITE_PALETTE_4, darken_sprite_palette_4, scene_title_darken_sprite_palette_4_present,
      darken_sprite_palette_4_leaves, true },
    { SCENE_ROUTINE_UNTINT_SPRITE_PALETTE_3, untint_sprite_palette_3, scene_title_untint_sprite_palette_3_present,
      untint_sprite_palette_3_leaves, true },
};
const unsigned scene_title_routine_count = sizeof scene_title_routines / sizeof scene_title_routines[0];

static const SceneScriptRoutine *routine_of(uint16_t routine)
{
    for (unsigned n = 0; n < scene_title_routine_count; n++)
        if (scene_title_routines[n].routine == routine)
            return &scene_title_routines[n];
    return &scene_title_routines[0];
}
