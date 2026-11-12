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
 *   original waits for; the loader owns all of its resource steps;
 * - the frame's tasks, which the task list runs (scene/frame.c): the
 *   pendulum's swing (swing_the_pendulum) and its place (place_pendulum), the
 *   logo's sweep and landing (move_logo), the sprites drawn over the picture
 *   -- the clock, its hands, the copyright and the name sliding in -- the
 *   wait for the bottom right picture to go, and the watch for a button that
 *   ends the title;
 * - the ripple: mode 2 (start_ripple), the offsets moved on each frame
 *   (ripple_columns) and sent to VRAM with BG3's scroll (upload_ripple). */
#include "scene/scene.h"

#include "charge.h"
#include "core/palette.h"
#include "cpu_io.h"
#include "platform/audio.h"
#include "platform/video.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* The rows of this file's tasks (below), where the task lists find them. */
enum TitleTaskRow {
    ROW_SWING_PENDULUM,
    ROW_MOVE_LOGO,
    ROW_DRAW_THE_CLOCK,
    ROW_DRAW_THE_HANDS,
    ROW_DRAW_THE_COPYRIGHT,
    ROW_SLIDE_THE_NAME,
    ROW_COUNT_THE_WAIT_DOWN,
    ROW_WATCH_FOR_A_BUTTON,
    ROW_RIPPLE,
    ROW_CALM_THE_RIPPLE,
    ROW_PLACE_PENDULUM,
    ROW_UPLOAD_RIPPLE,
};

/* Where the title keeps its graphics in VRAM. */
enum {
    PLANE_VRAM = 0x0000,         /* the Mode 7 plane: its name table in the low bytes, its pixels in the high */
    SPRITE_TILES_VRAM = 0x4000,
    LAYER_TILES_VRAM = 0x5000,
    BG2_MAP_VRAM = 0x6000,
    BG1_MAP_VRAM = 0x6400,
    BG3_MAP_VRAM = 0x7000,       /* in mode 2, its first two rows are the columns' offsets */
};

/* The rest of BG1's tilemap: from the second of its rows the block before
 * didn't cover, $F00 bytes. */
enum { BG1_MAP_REST_VRAM = 0x6480, BG1_MAP_REST_BYTES = 0x0F00 };

// $7E:59D5
/* Blank the rest of BG1's map after loading the plane's name table. Both
 * callers have just filled that table from SCENE_BLANK_TILE, so this fill
 * repeats the same blank byte into both halves. The SNES encoding reuses
 * the source programmed by that preceding transfer. */
static void fill_bg1_map(GameState *g, Platform *pf, uint8_t port)
{
    set_decompress_dest(g, decompress_dest(g) & 0x00FFFF);
    VidVramPort vram_port = vid_vram_port_decode(port);
    vid_start_vram_uploads(pf, PF_AT(0x7E59D8), &vram_port);
    vid_send_vram_fill(pf, PF_AT(0x7E59DD), BG1_MAP_REST_VRAM,
                       PF_GAME(SCENE_BLANK_TILE), BG1_MAP_REST_BYTES, NULL);
}

/* ---- The plane's pixels ------------------------------------------------------------------------ */

enum { TILE_ROWS = 8, TILE_PIXELS = 8, TILE_PLANES = 4, ROW_BYTES = 2 };

/* The two forms of the expansion, by the program's routine that does each:
 * the title's tiles of four planes and the endings' of eight. */
enum { EXPAND_TILE = 0x5E54, EXPAND_WIDE_TILE = 0x5EF8, PROGRAM_BANK = 0x7E0000 };

typedef struct {
    uint16_t routine;
    unsigned planes;
    uint32_t site;   /* where it writes the pixels */
} TileExpansion;

static const TileExpansion four_planes = { EXPAND_TILE, TILE_PLANES, 0x7E5E75 };
static const TileExpansion eight_planes = { EXPAND_WIDE_TILE, SCENE_MOST_PLANES, 0x7E5F2E };

static const TileExpansion *expansion_of(unsigned planes) { return planes == TILE_PLANES ? &four_planes : &eight_planes; }

// $7E:5E54, $7E:5EF8
/* One tile of planar graphics at `tile` (in `bank`), four planes or eight,
 * turned into the plane's pixels: eight rows of eight, each pixel the byte of
 * its plane bits, into the high bytes of VRAM. */
void scene_expand_plane_tile(GameState *g, Platform *pf, uint8_t bank, uint16_t tile, unsigned planes)
{
    if (planes > SCENE_MOST_PLANES)
        planes = SCENE_MOST_PLANES;
    for (int row = 0; row < TILE_ROWS; row++) {
        uint8_t plane_bytes[SCENE_MOST_PLANES];
        for (unsigned pair = 0; pair < planes / 2; pair++) {
            uint16_t two = wram_read16(g, (uint32_t)bank << 16 | (uint16_t)(tile + pair * SCENE_PLANE_PAIR_BYTES));
            plane_bytes[2 * pair] = (uint8_t)two;
            plane_bytes[2 * pair + 1] = (uint8_t)(two >> 8);
        }
        tile = (uint16_t)(tile + ROW_BYTES);
        for (int pixel = TILE_PIXELS - 1; pixel >= 0; pixel--) {
            uint8_t color = 0;
            for (int plane = (int)planes - 1; plane >= 0; plane--)
                color = (uint8_t)(color << 1 | (plane_bytes[plane] >> pixel & 1));
            vid_write_vram_byte(pf, PF_AT(expansion_of(planes)->site), true, color);
        }
    }
    charge_accesses(g, planes + (planes == TILE_PLANES));
    /* The eight-plane form counts its rows in the bitmap scaler's cell, which
     * the scaler reads before it writes: the count run out stays there. */
    if (planes == SCENE_MOST_PLANES)
        set_scene_wide_tile_rows_left(g, 0);
}

// $7E:5E3C
/* A picture's tiles in the second bank, `count` of them from `first`, each
 * `step` bytes on from the last, expanded in turn into the plane's pixels.
 * Returns past the last. */
uint16_t scene_expand_tiles(GameState *g, Platform *pf, uint16_t first, uint16_t count, unsigned planes, uint16_t step)
{
    const TileExpansion *e = expansion_of(planes);
    uint16_t tile = first;
    for (uint16_t n = 0; n < count; n++) {
        TIMED_AS(g, PROGRAM_BANK | e->routine, scene_expand_plane_tile(g, pf, SCENE_UPPER_WORK_RAM, tile, e->planes));
        tile = (uint16_t)(tile + step);
    }
    return tile;
}

/* The loop's code but for its call's target and its stride, which the scenes
 * write; a target that is one of the two expansions, whose code is there; a
 * count of tiles that stays in the bank (a tile is at most 64 bytes). */
enum { MOST_TILE_BYTES = 0x40, BANK_BYTES = 0x10000 };

/* The plane's name table is 128 tiles a row. The title's picture is 32 rows of
 * 32, which the loader has put at $0200 in a bank of work RAM (the second, in
 * the game) as words whose low bytes are the tile numbers; the rest of each row
 * is tile 0, and the rest of the table the blank tile. */
enum { PLANE_ROW = 128, PICTURE_ROWS = 32, PICTURE_COLUMNS = 32 };
enum { PICTURE_TILES = 0x0200, PICTURE_BYTES = PICTURE_ROWS * PICTURE_COLUMNS * 2 };
enum { REST_OF_PLANE_BYTES = 0x1000 };

// $7E:5AB6
/* The plane's name table: the picture's rows, a tile at a time through the
 * VRAM port, then the rest filled with the blank tile. Only tile numbers
 * change; the pixels in the other half of the plane stay intact. */
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
    vid_fill_vram_half(pf, PF_AT(0x7E5AEB), PF_GAME(SCENE_BLANK_TILE), REST_OF_PLANE_BYTES, VID_VRAM_LOW_BYTES);
    /* the port, where the original's reads of the picture leave it */
    charge_io_accesses(g, 2);
}

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
    uint16_t down = (uint16_t)scene_scale_by_256ths(g, own, sine, zoom);
    set_scene_mode7_matrix_a(g, across);
    set_scene_mode7_matrix_d(g, across);
    set_scene_mode7_matrix_b(g, down);
    set_scene_mode7_matrix_c(g, (uint16_t)-down);
    uint32_t reciprocal = scene_divide32(g, own, ZOOM_ONE, zoom);
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

/* ---- The logo ------------------------------------------------------------------------------------ */

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
    uint16_t x = (uint16_t)scene_scale_by_256ths(g, own, across, SWEEP_ACROSS);
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
 * the rightmost first. The first column's new offset is worked out after
 * this (below). */
static void ripple_columns(GameState *g)
{
    for (unsigned column = COLUMNS - 1; column > 0; column--)
        set_column_offset(g, column, column_offset(g, column - 1));
}

/* The first column's new offset: the wave, sixteen sine steps a frame, times
 * the ripple's height (channel 0's seventh register), about the column's
 * place in the picture, as an entry that offsets BG1. */
enum {
    RIPPLE_HEIGHT = 6,
    WAVE_STEPS_A_FRAME_SHIFT = 4,
    PICTURE_PLACE = 0x4A,
    OFFSET_BITS = 0x03FF,
    OFFSETS_BG1 = 0x2000,
};

static uint16_t wave(GameState *g)
{
    int16_t sine = scene_sine(g, (uint16_t)(scene_frame_counter(g) << WAVE_STEPS_A_FRAME_SHIFT));
    uint16_t height = scene_channel_register(g, TITLE_CHANNEL, RIPPLE_HEIGHT);
    return (uint16_t)scene_scale_by_256ths(g, scene_program_own(), sine, height);
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
enum { UNPACKED_GRAPHICS = SCENE_UNPACK_BUFFER, UNPACKED_PALETTE_AT = 0x2000 };

/* The program's transfer sends what was unpacked a word at a time into
 * VRAM, a byte to each half of the word. */
enum { WORDS_TO_TWO_REGISTERS = 0x01 };

/* The plane's pixels: the first 160 of the layers' tiles, 32 bytes each. */
enum { PLANE_TILES = 0xA0 };

static void send_unpacked(GameState *g, Platform *pf) { TIMED_AS(g, 0x7E38E7, scene_send_unpacked(g, pf)); }

// $7E:5E3C
/* The plane's pixels, from the layers' tiles as they were unpacked: each
 * tile expanded to a byte a pixel in turn. */
static void build_plane_pixels(GameState *g, Platform *pf)
{
    (void)scene_expand_tiles(g, pf, (uint16_t)UNPACKED_GRAPHICS, PLANE_TILES, TILE_PLANES, scene_plane_tile_bytes(TILE_PLANES));
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

/* Channel 0's first script, the pendulum's, run a turn from the setup
 * ($7E:3749). */
enum { FIRST_SCRIPT = 0x5B05, CHANNEL_0_STEP = 0x7E3749 };

/* The title's music, which the program waits for (scene/program.c). */
enum { TITLE_SONG = 0x30 };
enum { FULL_BRIGHTNESS = 0x0F, FADE_EVERY_FRAME = 1 };

/* The fill of BG1's map steps VRAM whole words at a time, as the name
 * table's transfer before it did. */
enum { WHOLE_WORDS_PORT = 0x80 };

/* ---- The setup --------------------------------------------------------------------------------- */

/* The title's song started, as the program's call of the driver starts it. */
static void start_the_song(Platform *pf)
{
    au_command(pf, PF_CALL(0x7E3775, 0xC70004),
               (AudioCommand){ .command = AU_START_SONG, .p1 = TITLE_SONG, .p2 = AU_FULL_VOLUME, .p3 = AU_FULL_VOLUME,
                               .count = 3 });
}

// $7E:5978
bool scene_title_set_up(GameState *g, Platform *pf)
{
    set_decompress_dest(g, UNPACKED_GRAPHICS);
    vid_start_vram_uploads(pf, PF_AT(0x7E597D), SCENE_VRAM_WHOLE_WORDS);
    vid_set_vram_address(pf, PF_AT(0x7E5983), SPRITE_TILES_VRAM);
    scene_unpacks(g, SPRITE_TILES_RESOURCE);
    set_scene_call_transfer_control(g, scene_program_own(), WORDS_TO_TWO_REGISTERS);
    set_scene_call_transfer_register(g, scene_program_own(), VID_SNES_VRAM_DATA_LOW);
    send_unpacked(g, pf);
    scene_unpacks(g, LAYER_TILES_RESOURCE);
    vid_set_vram_address(pf, PF_AT(0x7E59A3), LAYER_TILES_VRAM);
    send_unpacked(g, pf);
    vid_set_vram_address(pf, PF_AT(0x7E59AD), PLANE_VRAM);
    build_plane_pixels(g, pf);
    scene_unpacks(g, BG1_MAP_RESOURCE);
    vid_set_vram_address(pf, PF_AT(0x7E59BF), BG1_MAP_VRAM);
    send_unpacked(g, pf);
    scene_unpacks(g, BG2_MAP_RESOURCE);
    vid_set_vram_address(pf, PF_AT(0x7E59CF), BG2_MAP_VRAM);
    send_unpacked(g, pf);
    TIMED_AS(g, 0x7E5AB6, scene_load_plane_map(g, pf, scene_unpacked_bank(g)));
    fill_bg1_map(g, pf, WHOLE_WORDS_PORT);
    set_decompress_dest(g, (decompress_dest(g) & 0xFF0000u) | UNPACKED_PALETTE_AT);
    scene_unpacks(g, PALETTE_RESOURCE);
    scene_use_loaded_palette(g);
    set_scene_channels_script(g, TITLE_CHANNEL, FIRST_SCRIPT);
    SceneTurn turn = SCENE_TURN_ENDED;
    TIMED_AS(g, CHANNEL_0_STEP, turn = scene_channel_turn(g, pf, TITLE_CHANNEL));
    if (turn != SCENE_TURN_ENDED)
        return false;
    the_title_starts(g);
    set_palette_working(g, LOGO_HIGHLIGHT, WHITE);
    set_palette_base(g, LOGO_HIGHLIGHT, WHITE);
    set_up_pendulum(pf);
    set_scene_brightness(g, FULL_BRIGHTNESS);
    set_scene_fade_period(g, FADE_EVERY_FRAME);
    set_scene_fade_countdown(g, FADE_EVERY_FRAME);
    TIMED_AS(g, 0xFDFFEE, palette_stop_animations(g));
    set_scene_song(g, TITLE_SONG);
    set_scene_song_playing(g, TITLE_SONG);
    start_the_song(pf);
    return true;
}

/* ---- The title's sprites --------------------------------------------------------------------------- */

/* The title's frame tasks that draw its sprites, each a list or two of the
 * program's (scene/sprites.c) where the title puts them, each going on from
 * the next free entry the last left. */
static void draw_list_at(GameState *g, uint16_t x, uint16_t y, uint16_t list)
{
    scene_draw_lists_at(g, x, y);
    TIMED_AS(g, 0xC30D0E, scene_draw_sprite_list(g, list, false));
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

/* The clock's hands, where the clock settles. */
enum { HANDS = 0x61B4, HANDS_X = 0x3A, HANDS_Y = 0x64 };

// $7E:5D20
static TaskEnd draw_the_hands_task(GameState *g, Platform *pf)
{
    (void)pf;
    draw_list_at(g, HANDS_X, HANDS_Y, HANDS);
    return task_goes_on();
}

/* The copyright line, in two lists. */
enum {
    COPYRIGHT_Y = 0xD0,
    COPYRIGHT_LEFT = 0x6186,
    COPYRIGHT_LEFT_X = 0x78,
    COPYRIGHT_RIGHT = 0x61A7,
    COPYRIGHT_RIGHT_X = 0xB8,
};

// $7E:5E1E
static TaskEnd draw_the_copyright_task(GameState *g, Platform *pf)
{
    (void)pf;
    draw_list_at(g, COPYRIGHT_LEFT_X, COPYRIGHT_Y, COPYRIGHT_LEFT);
    draw_list_at(g, COPYRIGHT_RIGHT_X, COPYRIGHT_Y, COPYRIGHT_RIGHT);
    return task_goes_on();
}

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

/* ---- The title's tasks, for the lists ------------------------------------------------------------- */

const SceneTaskPort scene_title_tasks[] = {
    [ROW_SWING_PENDULUM] = { SCENE_TASK_SWING_PENDULUM, SCENE_FRAME_TASKS, swing_pendulum_task },
    [ROW_MOVE_LOGO] = { SCENE_TASK_MOVE_LOGO, SCENE_FRAME_TASKS, move_logo_task },
    [ROW_DRAW_THE_CLOCK] = { SCENE_TASK_DRAW_THE_CLOCK, SCENE_FRAME_TASKS, draw_the_clock_task },
    [ROW_DRAW_THE_HANDS] = { SCENE_TASK_DRAW_THE_HANDS, SCENE_FRAME_TASKS, draw_the_hands_task },
    [ROW_DRAW_THE_COPYRIGHT] = { SCENE_TASK_DRAW_THE_COPYRIGHT, SCENE_FRAME_TASKS, draw_the_copyright_task },
    [ROW_SLIDE_THE_NAME] = { SCENE_TASK_SLIDE_THE_NAME, SCENE_FRAME_TASKS, slide_the_name_task },
    [ROW_COUNT_THE_WAIT_DOWN] = { SCENE_TASK_COUNT_THE_WAIT_DOWN, SCENE_FRAME_TASKS, count_the_wait_down_task },
    [ROW_WATCH_FOR_A_BUTTON] = { SCENE_TASK_WATCH_FOR_A_BUTTON, SCENE_FRAME_TASKS, watch_for_a_button_task },
    [ROW_RIPPLE] = { SCENE_TASK_RIPPLE, SCENE_FRAME_TASKS, ripple_task },
    [ROW_CALM_THE_RIPPLE] = { SCENE_TASK_CALM_THE_RIPPLE, SCENE_FRAME_TASKS, calm_the_ripple_task },
    [ROW_PLACE_PENDULUM] = { SCENE_TASK_PLACE_PENDULUM, SCENE_VBLANK_TASKS, place_pendulum_task },
    [ROW_UPLOAD_RIPPLE] = { SCENE_TASK_UPLOAD_RIPPLE, SCENE_VBLANK_TASKS, upload_ripple_task },
};
const unsigned scene_title_task_count = sizeof scene_title_tasks / sizeof scene_title_tasks[0];

/* ---- The routines the title's scripts call, for the interpreter -------------------------------------- */

const SceneScriptRoutine scene_title_routines[] = {
    [SCENE_ROW_START_RIPPLE] = { SCENE_ROUTINE_START_RIPPLE, start_ripple_on },
    [SCENE_ROW_DARKEN_SPRITE_PALETTE_4] = { SCENE_ROUTINE_DARKEN_SPRITE_PALETTE_4, darken_sprite_palette_4 },
    [SCENE_ROW_UNTINT_SPRITE_PALETTE_3] = { SCENE_ROUTINE_UNTINT_SPRITE_PALETTE_3, untint_sprite_palette_3 },
};
const unsigned scene_title_routine_count = sizeof scene_title_routines / sizeof scene_title_routines[0];
