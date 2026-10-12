/* Scenes: the endings the scene program's modes 10, 11, 14 and 15 share.
 *
 * Four of the game's endings start the same scene program setup ($7E:8538),
 * and their scripts tell them apart (the Beyond Time ending is one). The
 * setup builds a Mode 7 ground seen in perspective, with the captions that
 * fade in and out over it (their letters' sprites, the frames between them),
 * and starts the two scripts its mode picks. From there the scripts call the
 * routines below, each a scene of the ending:
 *
 * - a picture that zooms: a frame of a small animation, scaled by the main
 *   loop each pass to the size channel 1 asks for and drawn as sprites through
 *   a window round it (the frame routine $7E:8930, its gathering, placing and
 *   upload), loaded with a new ground ($7E:8790) or on its own ($7E:87CC);
 * - the stars ($7E:8BAF): every task stopped, the star field scattered
 *   (stars.c), its sprites' tiles and colors;
 * - a Mode 7 picture ($7E:8C14): its pixels and name table, its colors;
 * - the credits ($7E:8CD0, then the vblank task $7E:8D56): their letters, the
 *   picture they roll over (a 4x4 pattern of tiles across a 32x32 map), and
 *   the screen they roll up on, slowly, through the staff roll's own tasks
 *   (staff_roll.c); mode 15 goes straight on with its script instead;
 * - the last scene ($7E:8DF3, then the vblank task $7E:8E44): its colors and
 *   the window a frame task sweeps across it ($7E:9A0C).
 *
 * The captions' turns are captions.c's, the star field's flight stars.c's.
 *
 * Every entry is the scene program's code in work RAM, guarded by a hash of
 * it. What the scripts call runs on the calling channel's page with the
 * scripts' data bank, and works on its own: the routine keeps the page and a
 * byte of the bank on the stack while it works, as the original does, so the
 * original's code it calls out to finds the stack where it expects.
 *
 * Left as the original's, behind call-outs: the program's unpacker
 * ($7E:3691), the first step of a script ($7E:3749), the captions' first
 * timing ($7E:9BB8), the expansion of the Mode 7 picture's tiles ($7E:5E3C,
 * through $7E:5EF8), and the sprite list the swept window's rider is
 * ($7E:3A97). */
#include "scene/race.h"

#include "charge.h"
#include "core/palette.h"
#include "cpu_io.h"
#include "field/field.h"
#include "field/scratch.h"
#include "original_effects.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* ---- What the endings share -------------------------------------------------------------------- */

/* The scene's mode, as the program's mode table took it (race.h's mask). */
static unsigned ending_mode(const GameState *g) { return scene_flags(g) & SCENE_MODE; }

/* The two endings that start their captions from the first of their text. */
enum { MODE_CAPTIONED = 10, MODE_CAPTIONED_TOO = 11, MODE_WITHOUT_CREDITS = 15 };

/* Channel 1's registers, as the scripts move the zooming picture with them:
 * its zoom, and the point it is drawn round. Channel 0's: the frame of its
 * animation, and BG2's scroll in the credits. */
enum { PICTURE_CHANNEL = 1, PICTURE_ZOOM = 0, PICTURE_X = 1, PICTURE_Y = 2 };
enum { STEERING_CHANNEL = 0, CREDITS_SCROLL = 2, PICTURE_FRAME = 5 };

static uint16_t picture_register(const GameState *g, unsigned reg) { return scene_channel_register(g, PICTURE_CHANNEL, reg); }

/* Where the unpacker leaves what goes on to VRAM, and other places in the
 * second bank the scenes unpack to. */
enum { UNPACK_BUFFER = 0x7F0200 };

/* The unpacker's destination moved within its bank, or to the first bank (a
 * bank byte of 0) keeping its address. */
static void unpack_at(GameState *g, uint16_t at) { set_decompress_dest(g, (decompress_dest(g) & 0xFF0000) | at); }
static void unpack_into_first_bank(GameState *g) { set_decompress_dest(g, decompress_dest(g) & 0xFFFF); }

/* The program's transfer of what the unpacker made: a word at a time into
 * VRAM, or a byte at a time into the high halves of VRAM's words (the Mode 7
 * plane's pixels). */
enum { BYTES_ONE_REGISTER = 0x00 };

static void send_unpacked_as(GameState *g, uint8_t mode, uint8_t port_register)
{
    set_scene_call_transfer_control(g, scene_program_own(), mode);
    set_scene_call_transfer_register(g, scene_program_own(), port_register);
}

static void send_unpacked(GameState *g, Platform *pf) { TIMED_AS(g, 0x7E38E7, scene_send_unpacked(g, pf)); }

/* Colors as they are out of the program's resources, into the loaded palette
 * from color `first`, `count` of them (the copy's count is handed over on
 * `page`). */
enum { LOADED_PALETTE_PAGE = 0x2000, WORKING_PALETTE_PAGE = 0x2200, COLOR_BYTES = 2 };
enum { PAST_THE_WORKING_PALETTE = WORKING_PALETTE_PAGE + 256 * COLOR_BYTES };

static void copy_colors(GameState *g, SceneProgramPage page, unsigned resource, unsigned first, unsigned count)
{
    scene_copy_resource_to(g, page, resource, (uint16_t)(LOADED_PALETTE_PAGE + first * COLOR_BYTES),
                           (uint16_t)(count * COLOR_BYTES));
}

/* Every task stopped, on both lists. The original copies the lists' bytes out
 * of the cartridge, from the start of the race's plane, which is 0. */
enum { FRAME_TASK_SLOTS = 16, VBLANK_TASK_SLOTS = 8 };

// $7E:7EF8
static void stop_frame_tasks(GameState *g)
{
    for (unsigned slot = 0; slot < FRAME_TASK_SLOTS; slot++)
        set_scene_tasks(g, slot, NO_TASK);
    ORIGINAL_EFFECT(charge_block_move(g, FRAME_TASK_SLOTS * 2));
}

// $7E:7F05
static void stop_vblank_tasks(GameState *g)
{
    for (unsigned slot = 0; slot < VBLANK_TASK_SLOTS; slot++)
        set_scene_early_tasks(g, slot, NO_TASK);
    ORIGINAL_EFFECT(charge_block_move(g, VBLANK_TASK_SLOTS * 2));
}

/* A scene starts on a black screen with no per-line effects. */
static void blank_the_screen(GameState *g, Platform *pf, uint32_t effects_site, uint32_t display_site)
{
    vid_enable_line_effects(pf, PF_AT(effects_site), 0);
    set_scene_line_effects(g, 0);
    vid_set_display(pf, PF_AT(display_site), &(VidDisplay){ .blank = true });
}

/* ---- The zooming picture ---------------------------------------------------------------------- */

/* The picture is one of two shapes: 32 rows drawn as eight 16x16 sprites, or
 * the tall one, 64 rows as sixteen. Each has its list of sprites in the
 * program, its sprite tiles in VRAM, and the lines of each half of the window
 * round it. */
typedef enum { PICTURE_SHORT, PICTURE_TALL } PictureShape;

typedef struct {
    uint16_t rows;            /* the rows the scaler makes */
    uint16_t tile_bytes;      /* its tiles, which is also where the scaler's source ends */
    uint16_t sprite_list;     /* the list in the program */
    uint8_t sprites;          /* in the list */
    uint8_t half_lines;       /* the window's lines on each half */
} PictureLayout;

static const PictureLayout layouts[] = {
    [PICTURE_SHORT] = { .rows = 0x20, .tile_bytes = 0x400, .sprite_list = 0x88B9, .sprites = 8, .half_lines = 0x10 },
    [PICTURE_TALL] = { .rows = 0x40, .tile_bytes = 0x800, .sprite_list = 0x88DA, .sprites = 16, .half_lines = 0x20 },
};

static PictureShape picture_shape(const GameState *g)
{
    return (uint8_t)scene_ending_picture_tall(g) ? PICTURE_TALL : PICTURE_SHORT;
}

/* The zoom, as the scaler steps through the picture's rows: 128ths of a source
 * row an output row, so $80 is the picture's own size and more is smaller. */
enum { ZOOM_BITS = 0x01FF, ACTUAL_SIZE = 0x80, HALF_SIZE = 0x100 };

/* How far the picture reaches out from its point at a zoom: 8 over the zoom,
 * in 8.8 (half the picture's width as it is shown). */
enum { EIGHT = 0x0800 };

/* Where the scaler puts the picture's rows, in the second bank: from the
 * start of its output, two 8-pixel groups a pass. */
enum { SCALER_OUTPUT_START = 0x0200, PAIRS_ACROSS = 4 };

// $7E:8930
/* The main loop's call each pass: unless the last picture still waits to be
 * sent, the frame of the animation is handed to the scaler at the zoom channel
 * 1 asks for, the shape's rows of it. Returns whether it was. */
static bool hand_picture_to_scaler(GameState *g)
{
    if (scene_ending_picture_ready(g))
        return false;
    const PictureLayout *layout = &layouts[picture_shape(g)];
    uint16_t zoom = picture_register(g, PICTURE_ZOOM) & ZOOM_BITS;
    set_scene_scaler_rows_left(g, layout->rows);
    set_scene_scaler_source_limit(g, layout->tile_bytes);
    set_scene_scaler_row_step(g, zoom);
    set_scene_ending_picture_zoom(g, zoom);
    set_scene_scaler_source_base(g, scene_ending_picture_source(g));
    set_scene_scaler_output(g, SCALER_OUTPUT_START);
    set_scene_scaler_plane_output(g, SCALER_OUTPUT_START);
    set_scene_ending_picture_half(g, scene_divide_8_8_by(g, scene_program_own(), EIGHT, zoom));
    set_scene_scaler_column_step(g, (uint16_t)(zoom << 1));
    set_scene_scaler_pairs(g, PAIRS_ACROSS);
    set_scene_scaler_row_position(g, 0);
    return true;
}

/* How many of the shape's sprites the picture covers at its zoom: all of them
 * at its own size or larger, the first quarter down to half its size, and one
 * below that. */
static uint8_t sprites_covered(const PictureLayout *layout, uint16_t zoom)
{
    if (zoom < ACTUAL_SIZE)
        return layout->sprites;
    if (zoom < HALF_SIZE)
        return (uint8_t)(layout->sprites / 4);
    return 1;
}

// $7E:8983
/* Back from the scaler: the picture gathered into sprite tiles in the tilemap
 * buffer, its list cut to the sprites it covers, and the tiles marked to be
 * sent. */
static void gather_picture(GameState *g)
{
    TIMED_AS(g, 0x7EB574, scene_gather_picture_into_tiles(g));
    PictureShape shape = picture_shape(g);
    set_scene_ending_picture_sprite_counts(g, shape,
                                           (uint8_t)(sprites_covered(&layouts[shape], scene_ending_picture_zoom(g)) + 1));
    set_scene_ending_picture_ready(g, (uint16_t)(scene_ending_picture_ready(g) + 1));
}

/* The window round the picture is shut above it, open over its two halves
 * from its left to 64 pixels on (or the screen's edge). A line count holds
 * at most 127, so the lines above it are two counts. */
enum { WINDOW_WIDTH = 0x40, SCREEN_EDGE = 0xFF };

static void open_window_round(GameState *g, const PictureLayout *layout, uint8_t left, uint8_t top)
{
    set_scene_ending_window_above(g, 0, (uint8_t)(top / 2));
    set_scene_ending_window_above(g, 1, (uint8_t)(top / 2 + (top & 1)));
    unsigned right = (unsigned)left + WINDOW_WIDTH;
    for (unsigned half = 0; half < 2; half++) {
        set_scene_ending_window_from(g, half, left);
        set_scene_ending_window_to(g, half, (uint8_t)(right > SCREEN_EDGE ? SCREEN_EDGE : right));
        set_scene_ending_window_lines(g, half, layout->half_lines);
    }
}

// $7E:8850
/* Each frame (and in each vblank): the picture's sprites placed round the
 * point channel 1 holds it at, its reach to the left and half that above, and
 * the window opened round them. Returns the list drawn. */
static uint16_t place_picture(GameState *g)
{
    const PictureLayout *layout = &layouts[picture_shape(g)];
    uint8_t reach = (uint8_t)scene_ending_picture_half(g);
    uint8_t top = (uint8_t)(picture_register(g, PICTURE_Y) - reach / 2);
    uint8_t left = (uint8_t)(picture_register(g, PICTURE_X) - reach);
    set_scene_sprite_offset_y(g, top);
    set_scene_sprite_offset_y_high(g, 0);
    set_scene_sprite_offset_x(g, left);
    open_window_round(g, layout, left, top);
    TIMED_AS(g, 0xC30D5E, scene_draw_sprite_list(g, layout->sprite_list, true));
    return layout->sprite_list;
}

/* The pictures' frames lie in the second bank from $1200, $400 apart, 32 of
 * them; channel 0's register picks one, rounded to the nearest. */
enum { FIRST_FRAME = 0x1200, FRAME_SHIFT = 5, FRAMES = 0x1F, ROUNDING = 0x10 };

// $7E:891B
/* Each frame: the picture's frame of its animation, for the next scaling. */
static uint16_t pick_picture_frame(GameState *g)
{
    uint16_t frame = (uint16_t)((scene_channel_register(g, STEERING_CHANNEL, PICTURE_FRAME) + ROUNDING) >> FRAME_SHIFT & FRAMES);
    uint16_t source = (uint16_t)(FIRST_FRAME + (frame << 10));
    set_scene_ending_picture_source(g, source);
    return source;
}

/* The picture's sprite tiles in VRAM. */
enum { PICTURE_TILES_VRAM = 0x5800 };

// $7E:89C0
/* In vblank: a gathered picture's tiles, from the tilemap buffer, and the
 * picture marked sent (its mark's low byte cleared, as the original clears it). */
static void upload_picture(GameState *g, Platform *pf)
{
    if (!(uint8_t)scene_ending_picture_ready(g))
        return;
    vid_send_vram(pf, PF_AT(0x7E89E7), PICTURE_TILES_VRAM, PF_GAME(SCENE_TILEMAP_BUFFER), scene_scaler_source_limit(g),
                  SCENE_VRAM_WHOLE_WORDS);
    set_scene_ending_picture_ready(g, scene_ending_picture_ready(g) & 0xFF00);
}

/* ---- The captions' letters and the ground: the setup's pieces ---------------------------------- */

/* The captions' letters, unpacked as 2-bit tiles and sent as 4-bit ones (the
 * upper two planes empty) to the sprites' tiles at $5000. */
enum { LETTER_TILES_VRAM = 0x5000, LETTER_TILES = 0x80, TILE_ROWS = 8, TILE_2BPP_BYTES = 16 };

// $7E:66B0
static void send_letters_widened(GameState *g, Platform *pf)
{
    uint32_t from = (uint32_t)scene_unpacked_bank(g) << 16 | (uint16_t)decompress_dest(g);
    for (unsigned tile = 0; tile < LETTER_TILES; tile++) {
        for (unsigned row = 0; row < TILE_ROWS; row++)
            vid_write_vram(pf, PF_AT(0x7E66C3), wram_read16(g, from + tile * TILE_2BPP_BYTES + row * 2));
        for (unsigned row = 0; row < TILE_ROWS; row++)
            vid_write_vram(pf, PF_AT(0x7E66C8), 0);
    }
    /* the original reads the tiles through the work RAM port */
    ORIGINAL_EFFECT(hw_port_seek_address(g, (uint16_t)(from + LETTER_TILES * TILE_2BPP_BYTES)));
}

/* The ground's name table, from what was unpacked: each 64-tile row of it
 * followed by 64 empty tiles, across the plane's 128. */
enum { GROUND_ROWS = 0x40, GROUND_ROW_BYTES = 0x40, PLANE_MAP_HALF = 0x7F2200 };

// $7E:37CA
/* The original counts the rows down in the scene's first cell. */
static void pad_ground_rows(GameState *g)
{
    for (uint32_t row = 0; row < GROUND_ROWS; row++) {
        uint32_t to = PLANE_MAP_HALF + row * 2 * GROUND_ROW_BYTES;
        wram_copy(g, UNPACK_BUFFER + row * GROUND_ROW_BYTES, to, GROUND_ROW_BYTES);
        wram_fill8(g, to + GROUND_ROW_BYTES, GROUND_ROW_BYTES, 0);
        ORIGINAL_EFFECT(charge_block_move(g, 2 * GROUND_ROW_BYTES - 1));
    }
    ORIGINAL_EFFECT(set_scene_call_first(g, scene_program_own(), 0));
}

/* Past the plane's name table (which the two halves leave VRAM's address at
 * the end of), the low bytes of the next $1000 words are the blank tile. */
enum { BLANKED_PAST_THE_PLANE = 0x1000 };

static void blank_past_the_plane(Platform *pf)
{
    const VidSnesTransfer t = { .dest = VID_SNES_VRAM_DATA_LOW, .source = SCENE_BLANK_TILE, .size = BLANKED_PAST_THE_PLANE };
    vid_snes_copy(pf, PF_AT(0x7E85B2), &t);
}

/* Mode 10's picture, made of tiles: 320 tile numbers, each picking one of the
 * unpacked tiles, laid out in turn from $D800 in the second bank; the last
 * $800 bytes of it go to VRAM $5C00, and a vblank task of the program's goes on
 * with it. */
enum { MOSAIC_TILES = 0x140, MOSAIC_TILE_BYTES = 0x20, MOSAIC_TILESET = 0x7F0200, MOSAIC_NUMBERS = 0x7F2200 };
enum { MOSAIC = 0x7FD800, MOSAIC_SENT = 0x7FE000, MOSAIC_SENT_BYTES = 0x800, MOSAIC_VRAM = 0x5C00 };
enum { MOSAIC_TASK = 0x9CE2, MOSAIC_COUNTER = 0x7F8000 };

// $7E:860C
static void lay_out_mosaic(GameState *g)
{
    for (uint32_t i = 0; i < MOSAIC_TILES; i++) {
        uint8_t tile = wram_read8(g, MOSAIC_NUMBERS + i);
        wram_copy(g, MOSAIC_TILESET + (uint32_t)tile * MOSAIC_TILE_BYTES, MOSAIC + i * MOSAIC_TILE_BYTES, MOSAIC_TILE_BYTES);
    }
    /* the original reads the numbers through the work RAM port and counts them
     * down in a word of the second bank */
    ORIGINAL_EFFECT(hw_port_seek_address(g, (uint16_t)(MOSAIC_NUMBERS + MOSAIC_TILES)));
    ORIGINAL_EFFECT(wram_write16(g, MOSAIC_COUNTER, 0));
    ORIGINAL_EFFECT(charge_block_move(g, MOSAIC_TILES * MOSAIC_TILE_BYTES));
}

/* The ground's line tables and the scene's colors, out of the program's
 * resources: each table 256 bytes, the colors 128 of the loaded palette and 32
 * more from color 128. */
typedef struct {
    uint8_t resource;
    uint16_t to;
    uint16_t bytes;
} ResourceCopy;

#define LOADED_COLOR(n) (uint16_t)(LOADED_PALETTE_PAGE + (n) * COLOR_BYTES)

static const ResourceCopy ground_copies[] = {
    { 50, LOADED_COLOR(0), 0x100 }, { 51, 0xC995, 0x100 }, { 52, 0xCA95, 0x100 }, { 53, 0xCB95, 0x100 },
    { 48, LOADED_COLOR(128), 0x40 },
};
enum { GROUND_COPIES = sizeof ground_copies / sizeof ground_copies[0] };

/* The captions' letters' colors: white, light grey and dark grey, in the last
 * sprite palette, and black behind everything. */
enum { CAPTION_COLORS = 241, CLEAR_COLOR = 0 };
static const uint16_t caption_colors[] = { 0x77BD, 0x4631, 0x1CE7 };

// $7E:8772
static void set_caption_colors(GameState *g)
{
    for (unsigned i = 0; i < sizeof caption_colors / sizeof caption_colors[0]; i++)
        set_palette_base(g, CAPTION_COLORS + i, caption_colors[i]);
    set_palette_base(g, CLEAR_COLOR, 0);
}

/* The working palette's first 120 colors made the clear color's first byte
 * (black): a copy of each byte onto the next. The original's store of a 0
 * before it lands outside work RAM, through a data bank of 0. */
enum { DARKENED_BYTES = 0xEF, WORKING_PALETTE = 0x7E2200 };

static void darken_first_colors(GameState *g)
{
    wram_fill8(g, WORKING_PALETTE + 1, DARKENED_BYTES, wram_read8(g, WORKING_PALETTE));
    ORIGINAL_EFFECT(charge_block_move(g, DARKENED_BYTES));
}

/* The captions: the first comes the next frame, each stays $140 frames once in,
 * and $40 go by between them. */
enum { FIRST_CAPTION_SOON = 1, CAPTION_GAP = 0x40, CAPTION_SHOWN = 0x140, CAPTIONS_TEXT = 0xA651 };
enum { CAPTION_LETTERS_IN_FRONT = 0x3F };

/* The scripts: channel 0's and channel 1's, by mode. */
typedef struct {
    uint16_t steering, picture;
} EndingScripts;

static EndingScripts scripts_for(unsigned mode)
{
    switch (mode) {
    case MODE_CAPTIONED: return (EndingScripts){ 0x90CF, 0x9341 };
    case MODE_CAPTIONED_TOO: return (EndingScripts){ 0x9450, 0x97A6 };
    default: return (EndingScripts){ 0x9613, 0x970F };
    }
}

/* The ground's perspective starts from the line above the horizon. */
enum { PERSPECTIVE_FROM_ABOVE_THE_HORIZON = 0x9B46 };

/* The scene's per-line effects (the plane's matrix in two halves, its scroll,
 * the screens, the BG mode, the window and the color math) and the five
 * channels they take; and its fade, a level every frame. */
enum { ENDING_LINE_EFFECTS = 0x8FEA, ENDING_LINE_CHANNELS = 0x1F, FADE_EVERY_FRAME = 1 };

/* The ground on BG1 in Mode 7, transparent past the plane's edge... */
static const VidBg ground_bg1 = { VID_TILEMAP(0x7000, 32, 32) };
static const VidBg ground_bg2 = { VID_TILEMAP(0x7400, 32, 32) };
enum { GROUND_TILES_VRAM = 0x6000 };

/* ...the sprites masked outside window 1 on the main screen, with BG1, which
 * the sub screen's sprites are added onto. */
static const VidWindows sprites_outside_window = { .w1 = { [VID_OBJ] = VID_WIN_OUT } };
static const VidColorMath sprites_added = { .sub_screen = true, .layers = VID_BG1_BIT | VID_BG3_BIT };

static void set_up_ground_layers(Platform *pf)
{
    vid_set_mode7(pf, PF_AT(0x7E8724), &(VidMode7){ .outside = VID_M7_TILE0 }, VID_M7_SELECT);
    vid_set_mode(pf, PF_AT(0x7E8729), &(VidMode){ .mode = 7 });
    vid_set_windows(pf, PF_AT(0x7E872E), &sprites_outside_window, VID_WINDOWS_OBJ_BACK);
    vid_set_windows(pf, PF_AT(0x7E8733), &(VidWindows){ .win_main = VID_BG1_BIT | VID_OBJ_BIT }, VID_WINDOWS_MAIN);
    vid_set_screens(pf, PF_AT(0x7E8739), VID_BG1_BIT, VID_OBJ_BIT, VID_SCREEN_MAIN | VID_SCREEN_SUB);
    vid_set_color_math(pf, PF_AT(0x7E873F), &sprites_added, VID_MATH_CONTROL | VID_MATH_LAYERS);
    vid_set_bg_map(pf, PF_AT(0x7E8744), VID_BG1, &ground_bg1);
    vid_set_bg_map(pf, PF_AT(0x7E8749), VID_BG2, &ground_bg2);
    vid_set_bg_chars(pf, PF_AT(0x7E874E), VID_BG1, GROUND_TILES_VRAM, GROUND_TILES_VRAM);
}

/* ---- Where the setups call the original's code ------------------------------------------------ */

/* The program's unpacker ($7E:3691): the resource's place in the program's
 * list in X, into the destination set. It reads nothing else but the overflow,
 * the data bank and the direct page, which it hands back. */
enum { UNPACK = 0x7E3691, UNPACK_IGNORES = DC_A | DC_Y | DC_N | DC_Z | DC_C };

static void unpack(Snes *s, uint32_t site, uint16_t resource)
{
    s->cpu.x = resource;
    m_jsr_dc(s, site, UNPACK, UNPACK_IGNORES);
}

/* The steering script's first step ($7E:3749), called with 16-bit A and the
 * program's bank kept on the stack. It reads Y and the overflow: Y as the
 * picture script's start left it, the overflow clear, as the regrouping code's
 * writer left it. */
enum { FIRST_STEP = 0x7E3749, FIRST_STEP_IGNORES = DC_A | DC_X | DC_N | DC_Z | DC_C };

static void first_script_step(Snes *s, uint16_t picture_script)
{
    s->cpu.db = SCENE_LOWER_WORK_RAM;   /* as the darkening's block move left it */
    m_push8(s, s->cpu.db);
    m_set_flag(s, CPU_FLAG_M, false);
    s->cpu.y = picture_script;
    m_set_flag(s, CPU_FLAG_V, false);
    m_jsr_dc(s, 0x7E86E6, FIRST_STEP, FIRST_STEP_IGNORES);
    set_scene_perspective_start(state_of(s), PERSPECTIVE_FROM_ABOVE_THE_HORIZON);
    s->cpu.db = m_pull8(s);
    m_set_flag(s, CPU_FLAG_M, true);
    s->cpu.d = 0x0000;
}

/* The captions' first timing ($7E:9BB8), which draws on the program's random
 * numbers; it reads none of the registers. */
enum { TIME_CAPTIONS = 0x7E9BB8, TIME_CAPTIONS_IGNORES = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C };

/* The stars' scatter starts its draws from what the original hands its first
 * one unexamined: A as the screen's blanking left it, and the carry the
 * script's call came with, which nothing here touches. The overflow its last
 * draw leaves is what the script routine leaves. */
enum { SCATTER_STARS = 0x7E5777, BLANKING_LEFT_IN_A = 0x0080 };

static void scatter_stars(Snes *s)
{
    GameState *g = state_of(s);
    SceneDraw first = scene_draw_from(BLANKING_LEFT_IN_A, m_flag(s, CPU_FLAG_C), TM_ENTROPY_BYTES);
    TIMED_AS(g, SCATTER_STARS, scene_scatter_stars(g, m_platform(s), first));
    ORIGINAL_EFFECT(m_set_flag(s, CPU_FLAG_V, scene_random_last_overflow()));
}

/* The program's loop over a picture's tiles ($7E:5E3C), Y of them, each
 * expanded into the Mode 7 plane's pixels by the routine the loop calls. */
enum { EXPAND_TILES = 0x7E5E3C, EXPAND_IGNORES = DC_A | DC_X | DC_N | DC_V | DC_Z | DC_C };

static void expand_tiles(Snes *s, uint16_t tiles)
{
    s->cpu.y = tiles;
    /* dc-evidence: ct_fuzz --poison-dc scene_ending_start_mode7_picture, seed 1: 300 states,
       1200 call-outs scrambled, 0 mismatched */
    m_jsr_dc(s, 0x7E8C70, EXPAND_TILES, EXPAND_IGNORES);
}

/* ---- The setup: the ground, the captions, the scripts ------------------------------------------- */

/* The captions' letters, and the regrouping code for the zooming picture. */
enum { LETTERS = 0x63, MATRIX_CODE = 0xB574, MATRIX_DESTINATION = SCENE_TILEMAP_BUFFER, MATRIX_SOURCE = 0x0200 };

/* The ground: its pixels into the high halves of VRAM's words, its name table
 * in two halves, then the sprites' second page of tiles. */
enum { GROUND_PIXELS = 0x7E, GROUND_MAP_FIRST = 0x81, GROUND_MAP_SECOND = 0xD2, MORE_SPRITE_TILES = 0x87 };
enum { MORE_SPRITE_TILES_VRAM = 0x4000 };

/* Mode 10's picture: its tiles, and its tile numbers unpacked after them. */
enum { MOSAIC_TILESET_RESOURCE = 0xBD, MOSAIC_NUMBERS_RESOURCE = 0xB4, MOSAIC_NUMBERS_AT = 0x2200 };

/* The scene's palette, unpacked into the loaded one from color 192. */
enum { ENDING_PALETTE = 0x8A, ENDING_PALETTE_AT = 0x2180 };

static void load_letters_and_ground(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    vid_start_vram_uploads(pf, PF_AT(0x7E853A), SCENE_VRAM_WHOLE_WORDS);
    set_decompress_dest(g, UNPACK_BUFFER);
    vid_set_vram_address(pf, PF_AT(0x7E8543), LETTER_TILES_VRAM);
    unpack(s, 0x7E8549, LETTERS);
    send_letters_widened(g, pf);
    set_scene_caption_letter_attributes(g, CAPTION_LETTERS_IN_FRONT);
    hw_port_seek(g, MATRIX_CODE, SCENE_LOWER_WORK_RAM);
    TIMED_AS(g, 0xC309E9, scene_write_matrix_code_for(g, MATRIX_DESTINATION, MATRIX_SOURCE));

    vid_start_vram_uploads(pf, PF_AT(0x7E856F), SCENE_VRAM_WHOLE_WORDS);
    vid_set_vram_address(pf, PF_AT(0x7E8575), 0x0000);
    unpack(s, 0x7E857B, GROUND_PIXELS);
    send_unpacked_as(g, BYTES_ONE_REGISTER, VID_SNES_VRAM_DATA_HIGH);
    send_unpacked(g, pf);

    vid_start_vram_uploads(pf, PF_AT(0x7E858C), SCENE_VRAM_LOW_BYTES);
    vid_set_vram_address(pf, PF_AT(0x7E8592), 0x0000);
    unpack(s, 0x7E8598, GROUND_MAP_FIRST);
    pad_ground_rows(g);
    TIMED_AS(g, 0x7E8752, scene_upload_plane_half(pf));
    unpack(s, 0x7E85A5, GROUND_MAP_SECOND);
    pad_ground_rows(g);
    TIMED_AS(g, 0x7E8752, scene_upload_plane_half(pf));
    blank_past_the_plane(pf);

    vid_start_vram_uploads(pf, PF_AT(0x7E85B7), SCENE_VRAM_WHOLE_WORDS);
    vid_set_vram_address(pf, PF_AT(0x7E85BD), MORE_SPRITE_TILES_VRAM);
    unpack(s, 0x7E85C3, MORE_SPRITE_TILES);
    send_unpacked_as(g, SCENE_TRANSFER_WORDS, VID_SNES_VRAM_DATA_LOW);
    send_unpacked(g, pf);
}

/* Mode 10's picture, laid out from its tiles and sent; the original works
 * with the data bank and the direct page moved, the first kept on the stack. */
static void load_mosaic(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    unpack(s, 0x7E85E4, MOSAIC_TILESET_RESOURCE);
    unpack_at(g, MOSAIC_NUMBERS_AT);
    unpack(s, 0x7E85F0, MOSAIC_NUMBERS_RESOURCE);
    lay_out_mosaic(g);
    vid_send_vram(pf, PF_AT(0x7E864C), MOSAIC_VRAM, PF_GAME(MOSAIC_SENT), MOSAIC_SENT_BYTES, NULL);
    scene_add_task(g, SCENE_VBLANK_TASKS, MOSAIC_TASK);
}

static void load_ground_colors(Snes *s)
{
    GameState *g = state_of(s);
    unpack_into_first_bank(g);
    for (unsigned i = 0; i < GROUND_COPIES; i++)
        scene_copy_resource_to(g, scene_program_own(), ground_copies[i].resource, ground_copies[i].to, ground_copies[i].bytes);
    set_caption_colors(g);
    set_decompress_dest(g, (decompress_dest(g) & 0xFF0000) | ENDING_PALETTE_AT);
    unpack(s, 0x7E869B, ENDING_PALETTE);
    scene_use_loaded_palette(g);
    darken_first_colors(g);
}

/* The captions start: in modes 10 and 11 from the first of their text. */
static void start_captions(GameState *g)
{
    set_scene_ending_caption_countdown(g, FIRST_CAPTION_SOON);
    set_scene_ending_caption_gap(g, CAPTION_GAP);
    set_scene_ending_caption_shown(g, CAPTION_SHOWN);
}

// $7E:8538
/* The shared endings' setup, from the program's mode table. */
static void set_up_ending(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    unsigned mode = ending_mode(g);
    load_letters_and_ground(s);
    if (mode == MODE_CAPTIONED)
        load_mosaic(s);
    load_ground_colors(s);

    EndingScripts scripts = scripts_for(mode);
    if (mode == MODE_CAPTIONED || mode == MODE_CAPTIONED_TOO)
        set_scene_ending_caption_text(g, CAPTIONS_TEXT);
    set_scene_channels_script(g, 0, scripts.steering);
    set_scene_channels_script(g, 1, scripts.picture);
    first_script_step(s, scripts.picture);
    TIMED_AS(g, 0xFDE275, palette_stop_animations(g));
    start_captions(g);
    set_scene_reset_disabled(g, 1);
    m_jsr_dc(s, 0x7E870E, TIME_CAPTIONS, TIME_CAPTIONS_IGNORES);

    TIMED_AS(g, 0x7E36CC, scene_set_up_line_effects(g, pf, ENDING_LINE_EFFECTS));
    set_scene_line_effects(g, ENDING_LINE_CHANNELS);
    set_scene_fade_period(g, FADE_EVERY_FRAME);
    set_scene_fade_countdown(g, FADE_EVERY_FRAME);
    set_up_ground_layers(pf);
}

/* ---- The scenes the scripts call --------------------------------------------------------------- */

/* The new ground the zooming picture's second showing has, and its picture;
 * the color math past the plane's last line. */
enum { NEW_GROUND = 0x84, PICTURE_FRAMES = 0xD8, PICTURE_FRAMES_AT = 0x0A00, FAR_COLOR_MATH = 0x05 };

/* The picture's frames, unpacked in the second bank and mirrored into place
 * from $5600: fifteen runs of sixteen rows, from the last run's rows up, each
 * row's 32 pixels from its 32nd back. */
enum { MIRRORED_FRAMES = 0x7F5600, FIRST_RUN = 0x4E00, RUNS = 15, RUN_ROWS = 16, RUN_BYTES = 0x400, MIRROR_LAST = 0x20 };
enum { PICTURE_ROW_BYTES = 0x40 };

/* The scaler's output taken as full before the first picture, which so clears
 * all of it past its own end. */
enum { SCALER_OUTPUT_AS_FULL = 0x0A00 };

/* The picture's colors, into the loaded palette and straight into the working
 * one, at color 128. */
enum { PICTURE_COLORS = 55, PICTURE_PALETTE = 128 };

/* The main loop's call each pass: the picture's scaling. */
enum { MAIN_LOOP_SCALES_PICTURE = 0x8930 };

/* The mirroring's working values, as the original leaves them in the second
 * bank's first words (the scaler's picture, which writes over them). */
static void leave_mirroring(GameState *g, uint16_t last_run)
{
    set_scene_race_picture(g, 0, MIRROR_LAST);
    set_scene_race_picture(g, 1, last_run);
    set_scene_race_picture(g, 2, 0);
}

static void mirror_picture_frames(GameState *g)
{
    uint32_t to = MIRRORED_FRAMES;
    uint16_t run = FIRST_RUN;
    for (unsigned i = 0; i < RUNS; i++) {
        to = scene_mirror_rows(g, to, (uint32_t)SCENE_UPPER_WORK_RAM << 16 | run, MIRROR_LAST, RUN_ROWS);
        run = (uint16_t)(run - RUN_BYTES);
    }
    ORIGINAL_EFFECT(leave_mirroring(g, run));
    ORIGINAL_EFFECT(hw_port_seek(g, (uint16_t)to, 1));
}

/* What the picture's loading leaves for the scene: the scaler's output full,
 * the picture's colors, and the main loop scaling it. Returns where the last
 * copy ended. */
static uint16_t load_picture_colors(GameState *g, SceneProgramPage page)
{
    set_scene_scaler_output_end(g, SCALER_OUTPUT_AS_FULL);
    unpack_into_first_bank(g);
    copy_colors(g, page, PICTURE_COLORS, PICTURE_PALETTE, 16);
    scene_copy_resource_to(g, page, PICTURE_COLORS, (uint16_t)(WORKING_PALETTE_PAGE + PICTURE_PALETTE * COLOR_BYTES), 32);
    return (uint16_t)(WORKING_PALETTE_PAGE + PICTURE_PALETTE * COLOR_BYTES + 32);
}

/* A routine a script calls keeps its page and the scripts' bank on the stack
 * while it works (the bank byte its data bank's push left), with 8-bit A, and
 * goes back with them and 16-bit A. */
static void begin_script_routine(Snes *s)
{
    m_push16(s, s->cpu.d);
    m_push8(s, SCENE_LOWER_WORK_RAM);
    s->cpu.db = 0x00;
    s->cpu.p |= CPU_FLAG_M;
}

static void end_script_routine(Snes *s)
{
    s->cpu.db = m_pull8(s);
    s->cpu.p &= (uint8_t)~CPU_FLAG_M;
    s->cpu.d = m_pull16(s);
    m_rts(s);
}

static SceneProgramPage calling_page(Snes *s)
{
    SceneChannel ch = 0;
    scene_channel_at(m_direct_page(s), &ch);
    return scene_program_channel(ch);
}

/* The picture's frames unpacked and mirrored into place. */
static uint16_t load_picture(Snes *s, SceneProgramPage page)
{
    GameState *g = state_of(s);
    set_decompress_dest(g, UNPACK_BUFFER);
    unpack_at(g, PICTURE_FRAMES_AT);
    unpack(s, 0x7E87E9, PICTURE_FRAMES);
    mirror_picture_frames(g);
    return load_picture_colors(g, page);
}

/* What the picture's routines leave: Y where the last copy of its colors
 * ended, and the overflow of the mirroring's last step back a run, clear. */
static void leave_picture_loaded(Snes *s, uint16_t copied_to)
{
    s->cpu.y = copied_to;
    m_set_flag(s, CPU_FLAG_V, false);
}

// $7E:87CC
/* A new picture zooms: the main loop scales it from now on. */
static void start_picture(GameState *g, Platform *pf, uint32_t effects_site)
{
    set_scene_main_loop_call(g, MAIN_LOOP_SCALES_PICTURE);
    vid_enable_line_effects(pf, PF_AT(effects_site), 0);
}

// $7E:8790
/* The picture over a new ground: the ground's name table (each row doubled
 * across the plane) sent twice over, the plane's settings cleared, and the
 * color math past it. */
static void load_new_ground(Snes *s, SceneProgramPage page)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    scene_use_loaded_palette(g);
    set_scene_plane_far_color_math(g, FAR_COLOR_MATH);
    set_decompress_dest(g, UNPACK_BUFFER);
    unpack(s, 0x7E87B0, NEW_GROUND);
    scene_double_plane_rows(g);
    /* the original counts the rows down in its page's first cell */
    ORIGINAL_EFFECT(set_scene_call_first(g, page, 0));
    vid_start_vram_uploads(pf, PF_AT(0x7E87B8), SCENE_VRAM_LOW_BYTES);
    vid_set_vram_address(pf, PF_AT(0x7E87BB), 0x0000);
    vid_set_mode7(pf, PF_AT(0x7E87C1), &(VidMode7){ .outside = VID_M7_WRAP }, VID_M7_SELECT);
    TIMED_AS(g, 0x7E8752, scene_upload_plane_half(pf));
    TIMED_AS(g, 0x7E8752, scene_upload_plane_half(pf));
}

/* The stars' sprites' tiles, and their colors as sprite palette 1. */
enum { STAR_TILES = 0x33, STAR_TILES_VRAM = 0x4600, STAR_COLORS = 19, STAR_PALETTE = 144 };

// $7E:8BAF
/* The stars: everything stopped, the star field scattered, and its tiles and
 * colors loaded. */
static void start_stars(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    blank_the_screen(g, pf, 0x7E8BC0, 0x7E8BC8);
    scatter_stars(s);
    set_decompress_dest(g, UNPACK_BUFFER);
    send_unpacked_as(g, SCENE_TRANSFER_WORDS, VID_SNES_VRAM_DATA_LOW);
    vid_set_vram_address(pf, PF_AT(0x7E8BDE), STAR_TILES_VRAM);
    unpack(s, 0x7E8BE4, STAR_TILES);
    send_unpacked(g, pf);
    set_decompress_dest(g, decompress_dest(g) & 0xFF);
    copy_colors(g, scene_program_own(), STAR_COLORS, STAR_PALETTE, 16);
    scene_use_loaded_palette(g);
}

/* The Mode 7 picture: its tiles, expanded into the plane's pixels by the
 * program's loop (made to take $30 bytes a tile, through its other
 * expansion), its name table, and its colors from color 16. */
enum { PLANE_TILES = 0xCF, PLANE_TILES_VRAM = 0x6000, PLANE_TILES_TO_EXPAND = 0xA4 };
enum { EXPAND_WIDE_TILE = 0x5EF8, WIDE_TILE_BYTES = 0x30 };
enum { PLANE_MAP = 0xB1, PLANE_MAP_VRAM = 0x4400, PLANE_COLORS = 0x54, PLANE_COLORS_AT = 0x2020 };

// $7E:8C14
static void start_mode7_picture(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    blank_the_screen(g, pf, 0x7E8C25, 0x7E8C2D);
    vid_set_mode7(pf, PF_AT(0x7E8C30), &(VidMode7){ .outside = VID_M7_TRANSPARENT }, VID_M7_SELECT);
    set_decompress_dest(g, UNPACK_BUFFER);
    send_unpacked_as(g, SCENE_TRANSFER_WORDS, VID_SNES_VRAM_DATA_LOW);
    vid_start_vram_uploads(pf, PF_AT(0x7E8C42), SCENE_VRAM_WHOLE_WORDS);
    unpack(s, 0x7E8C48, PLANE_TILES);
    vid_set_vram_address(pf, PF_AT(0x7E8C4E), PLANE_TILES_VRAM);
    send_unpacked(g, pf);
    vid_set_vram_address(pf, PF_AT(0x7E8C58), 0x0000);
    set_scene_plane_tiles_expander(g, EXPAND_WIDE_TILE);
    set_scene_plane_tiles_stride(g, WIDE_TILE_BYTES);
    expand_tiles(s, PLANE_TILES_TO_EXPAND);
    unpack(s, 0x7E8C76, PLANE_MAP);
    vid_set_vram_address(pf, PF_AT(0x7E8C7C), PLANE_MAP_VRAM);
    send_unpacked(g, pf);
    TIMED_AS(g, 0x7E5AB6, scene_load_plane_map(g, pf, scene_unpacked_bank(g)));
    set_decompress_dest(g, PLANE_COLORS_AT);
    unpack(s, 0x7E8C8E, PLANE_COLORS);
    scene_set_letter_colors(g);
    scene_use_loaded_palette(g);
    TIMED_AS(g, 0x7E388B, scene_send_palette(pf));
    vid_set_screens(pf, PF_AT(0x7E8CA1), VID_BG1_BIT, VID_OBJ_BIT, VID_SCREEN_MAIN | VID_SCREEN_SUB);
    vid_set_windows(pf, PF_AT(0x7E8CA7), &(VidWindows){ 0 }, VID_WINDOWS_MAIN | VID_WINDOWS_SUB);
    vid_set_mode(pf, PF_AT(0x7E8CAC), &(VidMode){ .mode = 7 });
}

/* The credits' letters, the picture they roll over (its tiles, and the map
 * made below), and the pattern's tiles. */
enum { CREDITS_LETTERS = 0xAE, CREDITS_PICTURE = 0x8D, CREDITS_PICTURE_AT = 0x0400, CREDITS_PATTERN = 0xC0 };
enum { CREDITS_PATTERN_AT = 0x0F00, PATTERN_MAP = 0x1000 };

/* The picture behind the credits is a 32x32 map of a 4x4 block of tiles over
 * and over: row r's tiles are 4(r mod 4) to 4(r mod 4) + 3, round again. */
enum { MAP_SIDE = 32, BLOCK_SIDE = 4 };

/* The original writes the map a word at a time through the work RAM port, the
 * word's low byte through the address before the port's -- the sound CPU's
 * port 3, which it mirrors -- and leaves its counts at 0 in the scene's cells. */
static void leave_pattern_written(GameState *g, uint32_t end)
{
    hw_port_seek_address(g, (uint16_t)end);
    hw_port_leave_spill(g, 0);
    SceneProgramPage own = scene_program_own();
    set_scene_call_first(g, own, scene_call_first(g, own) & 0xFF00);
}

// $7E:8D24
static void write_pattern_map(GameState *g)
{
    uint32_t at = (uint32_t)scene_unpacked_bank(g) << 16 | PATTERN_MAP;
    for (unsigned row = 0; row < MAP_SIDE; row++) {
        for (unsigned column = 0; column < MAP_SIDE; column++) {
            uint8_t tile = (uint8_t)((row % BLOCK_SIDE) * BLOCK_SIDE + column % BLOCK_SIDE);
            wram_write16(g, at, tile);
            at += 2;
        }
    }
    ORIGINAL_EFFECT(leave_pattern_written(g, at));
    /* the rows' count, run down to 0 in the scene's second cell, which the next
     * frame's controller read takes for the buttons held before */
    SceneProgramPage own = scene_program_own();
    set_scene_call_second(g, own, scene_call_second(g, own) & 0xFF00);
}

/* Mode 15 has no credits: its steering script goes on to the end. */
enum { NO_CREDITS_SCRIPT = 0x9315 };

// $7E:8CD0
/* The credits made ready: everything but the vblank tasks stopped. Returns
 * whether there are credits. */
static bool prepare_credits(Snes *s)
{
    GameState *g = state_of(s);
    vid_enable_line_effects(m_platform(s), PF_AT(0x7E8CDD), 0);
    if (ending_mode(g) == MODE_WITHOUT_CREDITS) {
        set_scene_channels_script(g, STEERING_CHANNEL, NO_CREDITS_SCRIPT);
        return false;
    }
    set_decompress_dest(g, UNPACK_BUFFER);
    unpack(s, 0x7E8CF8, CREDITS_LETTERS);
    unpack_at(g, CREDITS_PICTURE_AT);
    unpack(s, 0x7E8D04, CREDITS_PICTURE);
    unpack_at(g, CREDITS_PATTERN_AT);
    unpack(s, 0x7E8D10, CREDITS_PATTERN);
    write_pattern_map(g);
    return true;
}

/* The credits' screen: the letters' blank tiles and the map blanked, the
 * per-line effect, the text, and the layers -- the pattern on BG1, the
 * credits on BG2, mode 4, the credits subtracted from what is under them. */
enum { CREDITS_TILES_VRAM = 0x4000, CREDITS_BLANK_TILES = 0x7F0200, BLANK_TILES_BYTES = 0x200 };
enum { CREDITS_LINE_EFFECTS = 0x9023, CREDITS_LINE_CHANNELS = 0x01 };
enum { CREDITS_TEXT = 0xB099, ROW_BEFORE_THE_MAP = 0xFFE0 };

static const VidBg credits_pattern_bg = { VID_TILEMAP(0x4400, 32, 32) };
static const VidBg credits_bg = { VID_TILEMAP(0x4800, 32, 32) };
static const VidBg credits_bg3 = { VID_TILEMAP(0x6000, 32, 32) };
static const VidColorMath credits_subtracted = { .subtract = true, .layers = VID_BG2_BIT };

// $7E:8D56
static void show_credits(GameState *g, Platform *pf)
{
    vid_send_vram(pf, PF_AT(0x7E8D7A), CREDITS_TILES_VRAM, PF_GAME(CREDITS_BLANK_TILES), BLANK_TILES_BYTES,
                  SCENE_VRAM_WHOLE_WORDS);
    scene_blank_credits_map(pf, PF_AT(0x7E8DA9));
    TIMED_AS(g, 0x7E36CC, scene_set_up_line_effects(g, pf, CREDITS_LINE_EFFECTS));
    set_scene_line_effects(g, CREDITS_LINE_CHANNELS);
    set_scene_credits_text(g, CREDITS_TEXT);
    set_scene_credits_row_vram(g, ROW_BEFORE_THE_MAP);
    vid_set_scroll(pf, PF_AT(0x7E8DC8), VID_BG2, 0, 0, VID_SCROLL_H);
    vid_set_bg_chars(pf, PF_AT(0x7E8DCC), VID_BG1, 0x6000, 0x4000);
    vid_set_bg_chars(pf, PF_AT(0x7E8DD0), VID_BG3, 0x0000, 0x0000);
    vid_set_bg_map(pf, PF_AT(0x7E8DD4), VID_BG1, &credits_pattern_bg);
    vid_set_bg_map(pf, PF_AT(0x7E8DD8), VID_BG2, &credits_bg);
    vid_set_bg_map(pf, PF_AT(0x7E8DDC), VID_BG3, &credits_bg3);
    vid_set_mode(pf, PF_AT(0x7E8DE0), &(VidMode){ .mode = 4 });
    vid_set_color_math(pf, PF_AT(0x7E8DE4), &credits_subtracted, VID_MATH_LAYERS);
    vid_set_screens(pf, PF_AT(0x7E8DE9), VID_BG1_BIT | VID_BG2_BIT | VID_OBJ_BIT, 0, VID_SCREEN_MAIN | VID_SCREEN_SUB);
    vid_set_windows(pf, PF_AT(0x7E8DEE), &(VidWindows){ 0 }, VID_WINDOWS_MAIN | VID_WINDOWS_SUB);
}

/* The last scene's colors: its own from color 240 and from color 0, sixteen
 * each, and colors 224 to 239 of the working palette black (copied out of the
 * cartridge, from the race's plane, which is 0); and the window the scene's
 * task sweeps, from the right with its corners at the middle of the screen. */
enum { LAST_COLORS_HIGH = 54, LAST_COLORS_LOW = 57, BLACKENED_COLOR = 224, BLACKENED_BYTES = 0x20 };
enum { SWEEP_FROM = 0xE0, SWEEP_CORNERS_AT = 0x80, SWEEP_CORNERS = 4 };

// $7E:8DF3
static uint16_t set_last_colors(GameState *g, SceneProgramPage page)
{
    set_decompress_dest(g, decompress_dest(g) & 0xFF);
    copy_colors(g, page, LAST_COLORS_HIGH, 240, 16);
    copy_colors(g, page, LAST_COLORS_LOW, 0, 16);
    scene_use_loaded_palette(g);
    uint32_t black = (uint32_t)SCENE_LOWER_WORK_RAM << 16 | (uint16_t)(WORKING_PALETTE_PAGE + BLACKENED_COLOR * COLOR_BYTES);
    wram_fill8(g, black, BLACKENED_BYTES, 0);
    ORIGINAL_EFFECT(charge_block_move(g, BLACKENED_BYTES));
    set_scene_ending_window_left(g, SWEEP_FROM);
    for (unsigned corner = 0; corner < SWEEP_CORNERS; corner++)
        set_scene_ending_window_corners(g, corner, SWEEP_CORNERS_AT);
    return (uint16_t)(black + BLACKENED_BYTES);
}

/* The last scene's screen: BG1 on the main screen with BG2 and the sprites
 * on the sub screen, BG2 masked outside window 1 there (its edges at 0, the
 * scene's task moves them), color math adding them onto BG1 and the backdrop
 * over a black fixed color, its per-line effect for the window. */
enum { SWEEP_LINE_EFFECTS = 0x9029, SWEEP_LINE_CHANNELS = 0x01 };

static const VidColorMath sweep_math = { .half = false, .layers = VID_BG1_BIT | VID_BACK_BIT };
static const VidBg sweep_bg2 = { VID_TILEMAP(0x4C00, 32, 32) };
static const VidBg sweep_bg3 = { VID_TILEMAP(0x6000, 32, 32) };
static const VidWindows sweep_window = { .w1 = { [VID_BG2] = VID_WIN_OUT }, .win_sub = VID_BG2_BIT };

// $7E:8E44
static void show_last_scene(GameState *g, Platform *pf)
{
    vid_set_color_math(pf, PF_AT(0x7E8E4A), &sweep_math, VID_MATH_LAYERS);
    vid_set_fixed_color(pf, PF_AT(0x7E8E4E), VID_ALL_CHANNELS, 0);
    vid_set_screens(pf, PF_AT(0x7E8E52), VID_BG1_BIT, 0, VID_SCREEN_MAIN);
    vid_set_screens(pf, PF_AT(0x7E8E56), 0, VID_BG2_BIT | VID_OBJ_BIT, VID_SCREEN_SUB);
    vid_set_mode(pf, PF_AT(0x7E8E5A), &(VidMode){ .mode = 4 });
    vid_set_bg_map(pf, PF_AT(0x7E8E5E), VID_BG2, &sweep_bg2);
    vid_set_bg_chars(pf, PF_AT(0x7E8E62), VID_BG1, 0x6000, 0x0000);
    vid_set_bg_map(pf, PF_AT(0x7E8E66), VID_BG3, &sweep_bg3);
    vid_set_windows(pf, PF_AT(0x7E8E6A), &sweep_window, VID_WINDOWS_BG1_BG2);
    vid_set_windows(pf, PF_AT(0x7E8E6E), &sweep_window, VID_WINDOWS_SUB);
    vid_set_windows(pf, PF_AT(0x7E8E72), &sweep_window, VID_WINDOWS_W1_LEFT);
    vid_set_windows(pf, PF_AT(0x7E8E74), &sweep_window, VID_WINDOWS_W2_LEFT);
    vid_set_video_config(pf, PF_AT(0x7E8E78), &(VidVideoConfig){ 0 });
    TIMED_AS(g, 0x7E36CC, scene_set_up_line_effects(g, pf, SWEEP_LINE_EFFECTS));
    set_scene_line_effects(g, SWEEP_LINE_CHANNELS);
}

/* ---- The last scene's window ------------------------------------------------------------------- */

/* A frame task sweeps the last scene's window across it: a four-sided shape
 * drawn into the per-line table the scene's window effect reads (the field's
 * window shapes' drawing, field/window_shape.c), from its left edge to the
 * screen's right, its left side's two corners and its right side's moving down
 * a little a frame. While the left edge comes in from the right a sprite rides
 * on it, two pixels a frame; once it is at 0, the corners go on down until the
 * last is past the bottom, and the window is gone: the fixed color and the
 * video settings go back to plain, and the task ends. Meanwhile BG2's picture
 * drifts down a line every other frame. The corners are kept in quarter lines,
 * and move at different speeds, so the shape slants. */
enum { SWEEP_TABLE_BANK = 0x7E, SWEEP_TABLE = 0xE000, SCREEN_RIGHT_EDGE = 0xFF };
enum { LEFT_TOP, RIGHT_BOTTOM, RIGHT_TOP, LEFT_BOTTOM };   /* scene_ending_window_corners */
enum { QUARTERS = 4, LEFT_TOP_DOWN = 8, LEFT_BOTTOM_DOWN = 9, SWEPT_PAST = 0x390, EDGE_STEP = 2 };
enum { RIDER = 0xA1E2, RIDER_BEHIND_THE_EDGE = 12 };

static uint8_t corner_line(const GameState *g, unsigned corner)
{
    return (uint8_t)(scene_ending_window_corners(g, corner) / QUARTERS);
}

static void move_corner(GameState *g, unsigned corner, uint16_t by)
{
    uint16_t at = scene_ending_window_corners(g, corner);
    if (at < SWEPT_PAST)
        set_scene_ending_window_corners(g, corner, (uint16_t)(at + by));
}

/* The shape's four corners, round it from the left edge's top. */
typedef struct {
    uint8_t x, y;
} SweepCorner;

static void sweep_corners(const GameState *g, SweepCorner corners[4])
{
    uint8_t left = (uint8_t)scene_ending_window_left(g);
    corners[0] = (SweepCorner){ left, (uint8_t)(corner_line(g, LEFT_TOP) + LEFT_TOP_DOWN) };
    corners[1] = (SweepCorner){ left, (uint8_t)(corner_line(g, LEFT_BOTTOM) + LEFT_BOTTOM_DOWN) };
    corners[2] = (SweepCorner){ SCREEN_RIGHT_EDGE, corner_line(g, RIGHT_TOP) };
    corners[3] = (SweepCorner){ SCREEN_RIGHT_EDGE, corner_line(g, RIGHT_BOTTOM) };
}

static void draw_sweep_window(GameState *g)
{
    SweepCorner corners[4];
    sweep_corners(g, corners);
    set_field_window_shape_table_address(g, SWEEP_TABLE);
    set_field_window_shape_table_bank(g, SWEEP_TABLE_BANK);
    for (unsigned c = 0; c < 4; c++) {
        set_field_window_shape_corners_x(g, c, corners[c].x);
        set_field_window_shape_corners_y(g, c, corners[c].y);
    }
    TIMED_AS(g, 0xC30EFA, field_draw_window_shape(g));
}

typedef enum { SWEEP_GOES_ON, SWEEP_OVER } Sweep;

/* BG2's picture drifts down a line: its scroll, which the steering channel
 * keeps as a word, steps back in its low byte alone, as the original's byte
 * step does, so it wraps round within 256 lines and the high byte stays as
 * the script set it. */
static void drift_picture_down(GameState *g)
{
    uint16_t scroll = scene_channel_register(g, STEERING_CHANNEL, CREDITS_SCROLL);
    uint8_t line = (uint8_t)(scroll - 1);
    set_scene_channel_register(g, STEERING_CHANNEL, CREDITS_SCROLL, (uint16_t)((scroll >> 8) << 8 | line));
}

/* The window's edges after the rider's frame: the left edge in by two, its
 * corners down together (the rider follows the top one) or, at 0, on down
 * at their own speeds; the right side's down. */
static Sweep move_sweep_window(GameState *g, Platform *pf)
{
    uint16_t left = scene_ending_window_left(g);
    if (left) {
        set_scene_ending_window_left(g, (uint16_t)(left - EDGE_STEP));
        uint16_t top = (uint16_t)(scene_ending_window_corners(g, LEFT_TOP) + QUARTERS);
        set_scene_ending_window_corners(g, LEFT_TOP, top);
        set_scene_ending_window_corners(g, LEFT_BOTTOM, top);
    } else {
        move_corner(g, LEFT_TOP, 1);
        move_corner(g, LEFT_BOTTOM, 3);
    }
    if (scene_ending_window_corners(g, RIGHT_BOTTOM) >= SWEPT_PAST) {
        vid_set_fixed_color(pf, PF_AT(0x7E9A9C), 0, 0);
        vid_set_video_config(pf, PF_AT(0x7E9A9C), &(VidVideoConfig){ 0 });
        return SWEEP_OVER;
    }
    set_scene_ending_window_corners(g, RIGHT_BOTTOM, (uint16_t)(scene_ending_window_corners(g, RIGHT_BOTTOM) + 1));
    move_corner(g, RIGHT_TOP, 3);
    if (tm_odd_frame(pf, PF_AT(0x7E9AB3))) {
        drift_picture_down(g);
    }
    return SWEEP_GOES_ON;
}

// $7E:8E88
/* In vblank: BG2's scroll down, where the steering channel holds it. */
static void scroll_bg2(const GameState *g, Platform *pf)
{
    vid_set_scroll(pf, PF_AT(0x7E8E8B), VID_BG2, 0, scene_channel_register(g, STEERING_CHANNEL, CREDITS_SCROLL), VID_SCROLL_V);
}

/* ---- Entries ----------------------------------------------------------------------------------- */

/* The code an entry runs inline, by its routine: the unpacker's destination,
 * a resource's copy, the palette made current, the program's transfer, and
 * the exit the scripts' routines share ($7E:8AF0). */
static bool shared_pieces_present(Snes *s)
{
    return m_code_matches(s, 0x7E89ED, 0x14, 0x22A581F747CC4E58ull) && m_code_matches(s, 0x7E36AA, 0x22, 0xCCA1DE26086A09CBull) &&
           m_code_matches(s, 0x7E38AF, 0x14, 0xD31DAC893D4743DCull) && m_code_matches(s, 0x7E38E7, 0x24, 0xDD4B67DA62CC8E67ull) &&
           m_code_matches(s, 0x7E8AF0, 0x5, 0xEBDF284C0857DA9Bull);
}

/* The registers' stop past a vblank task that takes itself off its list, which
 * reads none of them. */
enum { REMOVE_THIS_VBLANK_TASK = 0x7E39DE, NOTHING_READ = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C };

/* The setup's code, with what it runs inline: the letters' widening, the rows'
 * padding, the plane's upload and the blanking past it, the letters' colors,
 * the task list's search, the per-line effects and their descriptors. Mode
 * 10's picture needs room on the vblank list. It stores a byte to $2200 through
 * the data bank, which lands nowhere on a bank that reaches the registers. */
bool scene_ending_set_up_present(Snes *s)
{
    if (!scene_on_scene_page(s) || !m_bank_reaches_io(s) || !shared_pieces_present(s) ||
        !m_code_matches(s, 0x7E8538, 0x21A, 0x6E812E4FAAD367E2ull) || !m_code_matches(s, 0x7E66B0, 0x2D, 0x668182BFFD2B1A5Bull) ||
        !m_code_matches(s, 0x7E37CA, 0x29, 0x94A78C10CD6A42C5ull) || !m_code_matches(s, 0x7E8752, 0x20, 0xE25F56B609E8320Eull) ||
        !m_code_matches(s, 0x7E8772, 0x1E, 0xACB5DF9A02DD40EAull) || !m_code_matches(s, 0x7E3974, 0x16, 0xFF73F0234DD0D06Aull) ||
        !m_code_matches(s, 0x7E36CC, 0x3C, 0x45191A42A1EEF6A8ull) || !m_code_matches(s, 0x7E8FEA, 0x24, 0xFFD32519BE41426Dull))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool fits = ending_mode(g) != MODE_CAPTIONED || scene_task_listed(g, SCENE_VBLANK_TASKS, NO_TASK);
    vars_peek(false);
    return fits;
}

/* Entered from the program's mode table with 8-bit A; back on the scene's
 * page, the data bank as the captions' timing left it, and the overflow the
 * per-line effects' setup leaves, clear. */
void scene_ending_set_up(Snes *s)
{
    set_up_ending(s);
    s->cpu.d = 0x0000;
    m_set_flag(s, CPU_FLAG_V, false);
    s->cpu.p |= CPU_FLAG_M;
    m_rts(s);
}

/* The scripts' routines run on the calling channel's page; the two that load
 * the picture store the main loop's call through the scripts' bank before they
 * set their own. */
static bool picture_loader_present(Snes *s)
{
    return scene_on_channel_page(s) && s->cpu.db == SCENE_LOWER_WORK_RAM && shared_pieces_present(s) &&
           m_code_matches(s, 0x7E87CC, 0x84, 0xB1EB2945775F8A30ull) && m_code_matches(s, 0x7E3ED3, 0x41, 0x4416DDBA3716B6CEull);
}

bool scene_ending_load_picture_present(Snes *s) { return picture_loader_present(s); }

bool scene_ending_load_ground_and_picture_present(Snes *s)
{
    return picture_loader_present(s) && m_code_matches(s, 0x7E8790, 0x3C, 0xCE7FC921DB2D9A93ull) &&
           m_code_matches(s, 0x7E37A6, 0x24, 0x5FBEEF82E0975340ull) && m_code_matches(s, 0x7E8752, 0x20, 0xE25F56B609E8320Eull);
}

/* The script routines' entries: 16-bit A; back with the page and the bank
 * they kept. */
void scene_ending_load_picture(Snes *s)
{
    SceneProgramPage page = calling_page(s);
    begin_script_routine(s);
    start_picture(state_of(s), m_platform(s), 0x7E87DA);
    uint16_t copied_to = load_picture(s, page);
    ORIGINAL_EFFECT(leave_picture_loaded(s, copied_to));
    end_script_routine(s);
}

void scene_ending_load_ground_and_picture(Snes *s)
{
    SceneProgramPage page = calling_page(s);
    begin_script_routine(s);
    start_picture(state_of(s), m_platform(s), 0x7E879E);
    load_new_ground(s, page);
    uint16_t copied_to = load_picture(s, page);
    ORIGINAL_EFFECT(leave_picture_loaded(s, copied_to));
    end_script_routine(s);
}

bool scene_ending_start_stars_present(Snes *s)
{
    return scene_on_channel_page(s) && shared_pieces_present(s) && m_code_matches(s, 0x7E8BAF, 0x56, 0x5EC2D58A5B1FEC0Cull) &&
           m_code_matches(s, 0x7E7EF8, 0xD, 0x03922383AB303EFCull) && m_code_matches(s, 0x7E7F05, 0xD, 0xE7828FA5FF3586ECull) &&
           scene_stars_scatter_code_present(s);
}

/* Back, Y is past the working palette, as the palette's copy left it; the
 * overflow is the scatter's, which nothing after it touches. */
void scene_ending_start_stars(Snes *s)
{
    begin_script_routine(s);
    s->cpu.d = 0x0000;
    stop_frame_tasks(state_of(s));
    stop_vblank_tasks(state_of(s));
    start_stars(s);
    ORIGINAL_EFFECT(s->cpu.y = PAST_THE_WORKING_PALETTE);
    end_script_routine(s);
}

bool scene_ending_start_mode7_picture_present(Snes *s)
{
    return scene_on_channel_page(s) && shared_pieces_present(s) && m_code_matches(s, 0x7E8C14, 0x9E, 0x6355107C822AA15Full) &&
           m_code_matches(s, 0x7E7EF8, 0xD, 0x03922383AB303EFCull) && m_code_matches(s, 0x7E7F05, 0xD, 0xE7828FA5FF3586ECull) &&
           m_code_matches(s, 0x7E5AB2, 0x4, 0xDF62168129B4210Cull) && m_code_matches(s, 0x7E5AB6, 0x4F, 0x2270DB27658DB4FFull) &&
           m_code_matches(s, 0x7E8CB2, 0x1E, 0xDA761D73F1A6D611ull) && m_code_matches(s, 0x7E388B, 0x24, 0x84B3B18FAFEEAE44ull);
}

/* Back, Y is past the working palette, as the palette's copy left it. */
void scene_ending_start_mode7_picture(Snes *s)
{
    begin_script_routine(s);
    s->cpu.d = 0x0000;
    stop_frame_tasks(state_of(s));
    stop_vblank_tasks(state_of(s));
    start_mode7_picture(s);
    ORIGINAL_EFFECT(s->cpu.y = PAST_THE_WORKING_PALETTE);
    end_script_routine(s);
}

bool scene_ending_prepare_credits_present(Snes *s)
{
    return scene_on_channel_page(s) && shared_pieces_present(s) && m_code_matches(s, 0x7E8CD0, 0x86, 0x887BE490F9F7A908ull) &&
           m_code_matches(s, 0x7E7EF8, 0xD, 0x03922383AB303EFCull);
}

/* Back, Y is 0 as the pattern's writing left it, or without credits past the
 * frame tasks' list, as the block move that stopped them left it. */
enum { PAST_THE_FRAME_TASKS = 0x0920 + FRAME_TASK_SLOTS * 2 };

void scene_ending_prepare_credits(Snes *s)
{
    begin_script_routine(s);
    stop_frame_tasks(state_of(s));
    bool credits = prepare_credits(s);
    ORIGINAL_EFFECT(s->cpu.y = credits ? 0 : PAST_THE_FRAME_TASKS);
    end_script_routine(s);
}

bool scene_ending_set_last_colors_present(Snes *s)
{
    return scene_on_channel_page(s) && shared_pieces_present(s) && m_code_matches(s, 0x7E8DF3, 0x51, 0x9C07BD83777F0399ull);
}

/* Back, Y is past the blackened colors, as the block move left it. */
void scene_ending_set_last_colors(Snes *s)
{
    SceneProgramPage page = calling_page(s);
    begin_script_routine(s);
    uint16_t blackened_to = set_last_colors(state_of(s), page);
    ORIGINAL_EFFECT(s->cpu.y = blackened_to);
    end_script_routine(s);
}

/* The vblank tasks that set a scene's screen up once and take themselves off
 * the list. */
bool scene_ending_show_credits_present(Snes *s)
{
    return scene_on_scene_page_io_bank(s) && m_code_matches(s, 0x7E8D56, 0x9D, 0x08121CAB0ACD3DEDull) &&
           m_code_matches(s, 0x7E90A7, 0x1A, 0x841D0D5589A89675ull) && m_code_matches(s, 0x7E36CC, 0x3C, 0x45191A42A1EEF6A8ull) &&
           m_code_matches(s, 0x7E9023, 0x6, 0xF09D7328034315D8ull);
}

void scene_ending_show_credits(Snes *s)
{
    show_credits(state_of(s), m_platform(s));
    m_stop_at(s, REMOVE_THIS_VBLANK_TASK, NOTHING_READ);
}

bool scene_ending_show_last_scene_present(Snes *s)
{
    return m_bank_reaches_io(s) && m_code_matches(s, 0x7E8E44, 0x44, 0x670F389BEFF44DF6ull) &&
           m_code_matches(s, 0x7E36CC, 0x3C, 0x45191A42A1EEF6A8ull) && m_code_matches(s, 0x7E9029, 0x6, 0x31D9FBABBC5732E4ull);
}

void scene_ending_show_last_scene(Snes *s)
{
    show_last_scene(state_of(s), m_platform(s));
    m_stop_at(s, REMOVE_THIS_VBLANK_TASK, NOTHING_READ);
}

bool scene_ending_scroll_bg2_present(Snes *s)
{
    return m_bank_reaches_io(s) && m_code_matches(s, 0x7E8E88, 0xD, 0x3B94F261543D6F7Aull);
}

/* A vblank task; nothing it leaves is read. */
void scene_ending_scroll_bg2(Snes *s)
{
    scroll_bg2(state_of(s), m_platform(s));
    m_rts(s);
}

/* The window's task: the scene's page, a data bank on the registers (the
 * rider's sprite list streams through the work RAM port), the window's edges
 * where the sweep keeps them, and no corner on the last line, nor the
 * table's right side, which its drawing reads first: from there the
 * original's fill of the lines below runs round the bank into low RAM. */
bool scene_ending_sweep_window_present(Snes *s)
{
    if (!scene_on_scene_page_io_bank(s) || !m_code_matches(s, 0x7E9A0C, 0xB1, 0x0B37DD86CDAEC523ull))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = scene_ending_window_left(g) < 0x8000 && field_window_shape_right_start(g) < SCREEN_RIGHT_EDGE;
    if (scene_ending_window_left(g))   /* the rider's sprites stay in the table, and off the sweep's cells */
        ok = ok && scene_sprites_fit(g, scene_sprite_list_length(g, RIDER));
    SweepCorner corners[4];
    sweep_corners(g, corners);
    for (unsigned c = 0; c < 4; c++)
        ok = ok && scene_ending_window_corners(g, c) < 0x8000 && corners[c].y < SCREEN_RIGHT_EDGE;
    vars_peek(false);
    return ok;
}

/* The rider is a sprite list drawn by the program's list drawer with signed
 * offsets ($7E:3A97), the list in A and its place in the scene's sprite
 * offsets, with 16-bit A; it reads nothing else but the direct page and the
 * data bank, and comes back with 16-bit A and Y the next free entry. */
enum { DRAW_SIGNED_LIST = 0x7E3A97, DRAW_IGNORES = DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

static void draw_rider(Snes *s)
{
    GameState *g = state_of(s);
    set_scene_sprite_offset_x(g, (uint16_t)(scene_ending_window_left(g) - RIDER_BEHIND_THE_EDGE));
    uint16_t line = (uint16_t)(scene_ending_window_corners(g, LEFT_TOP) / QUARTERS);
    set_scene_sprite_offset_y(g, (uint8_t)line);
    set_scene_sprite_offset_y_high(g, (uint8_t)(line >> 8));
    s->cpu.a = RIDER;
    m_set_flag(s, CPU_FLAG_M, false);
    m_jsl_dc(s, 0x7E9A6A, DRAW_SIGNED_LIST, DRAW_IGNORES);
}

/* The list's exit for a task that takes itself off. */
enum { REMOVE_SWEEP_TASK = 0x7E39A7 };

/* A frame task, entered with 16-bit A. Back, A's high byte is the right
 * side's top corner's (the last the original loaded), or 0 where the sweep
 * is over, and Y where the rider's list or the window's drawing left it. */
void scene_ending_sweep_window(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    draw_sweep_window(g);
    if (scene_ending_window_left(g))
        draw_rider(s);
    else
        ORIGINAL_EFFECT(s->cpu.y = field_window_shape_leaves_y(g));
    if (move_sweep_window(g, pf) == SWEEP_OVER) {
        ORIGINAL_EFFECT(s->cpu.a = 0);
        m_stop_at(s, REMOVE_SWEEP_TASK, DC_AL | DC_X | DC_N | DC_V | DC_Z | DC_C);
        return;
    }
    ORIGINAL_EFFECT(s->cpu.a = scene_ending_window_corners(g, RIGHT_TOP));
    s->cpu.p |= CPU_FLAG_M;
    m_rts(s);
}

/* The upload sends as many bytes as the scaling's source ends at, which is
 * never 0 (65536 bytes to the original) where a picture is waiting. */
bool scene_ending_upload_picture_present(Snes *s)
{
    if (!scene_on_scene_page_io_bank(s) || !m_code_matches(s, 0x7E89C0, 0x2D, 0x30B40D41846E3FB4ull))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool sized = !(uint8_t)scene_ending_picture_ready(g) || scene_scaler_source_limit(g) != 0;
    vars_peek(false);
    return sized;
}

/* A vblank task; nothing it leaves is read. */
void scene_ending_upload_picture(Snes *s)
{
    upload_picture(state_of(s), m_platform(s));
    m_rts(s);
}

/* The placing draws the tall picture's sixteen sprites at most. */
enum { MOST_SPRITES = 16 };

bool scene_ending_place_picture_present(Snes *s)
{
    if (!scene_on_scene_page_io_bank(s) || !m_code_matches(s, 0x7E8850, 0x69, 0x0371646720951031ull))
        return false;
    vars_peek(true);
    bool fits = scene_sprites_fit(state_of(s), MOST_SPRITES);
    vars_peek(false);
    return fits;
}

/* What the placing leaves for the lists' code: A as the clipped list leaves
 * it, from the list's address, and Y the next free sprite. Its scratch: half
 * the reach, in the scene's product cell. */
static void leave_picture_placed(Snes *s, uint16_t list)
{
    GameState *g = state_of(s);
    SceneProgramPage own = scene_program_own();
    set_scene_call_fraction(g, own, (uint16_t)((scene_call_fraction(g, own) & 0xFF00) | (uint8_t)scene_ending_picture_half(g) >> 1));
    s->cpu.a = scene_clipped_list_leaves_a(g, list, list);
    s->cpu.y = scene_sprite_index(g);
}

/* A frame task, and a vblank task (8-bit A); back with 16-bit A. */
void scene_ending_place_picture(Snes *s)
{
    uint16_t list = place_picture(state_of(s));
    ORIGINAL_EFFECT(leave_picture_placed(s, list));
    s->cpu.p &= (uint8_t)~CPU_FLAG_M;
    m_rts(s);
}

bool scene_ending_pick_picture_frame_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E891B, 0x14, 0x7AF538C4E8F9D1D5ull);
}

/* A frame task, entered with 16-bit A and the carry clear (the registry's
 * condition; the list's own compare, for any slot but the first). Back, A is
 * the frame's place, which the lists' code hands on. */
void scene_ending_pick_picture_frame(Snes *s)
{
    uint16_t source = pick_picture_frame(state_of(s));
    ORIGINAL_EFFECT(s->cpu.a = source);
    m_rts(s);
}

/* The scaling reads the picture's shape as a word, the gathering and placing
 * its low byte, which agree while the byte after it is 0, as every ending
 * leaves it. A picture handed to the scaler runs the program's divide inline. */
bool scene_ending_scale_picture_present(Snes *s)
{
    if (!scene_on_scene_page(s) || !m_bank_reaches_low_ram(s) || !m_code_matches(s, 0x7E8930, 0x6, 0xB318CAF0720BDB26ull))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool waiting = scene_ending_picture_ready(g) != 0;
    bool word_agrees = (scene_ending_picture_tall(g) >> 8) == 0;
    vars_peek(false);
    return waiting || (word_agrees && m_code_matches(s, 0x7E8936, 0x4D, 0x65F6F8DD17744FCCull) &&
                       m_code_matches(s, 0x7E32E2, 0xBD, 0xD04B8C1274AF93C8ull));
}

/* The divide's dividend, as the original leaves it in the scene's first cell:
 * shifted out to its high byte's place, the caller's carry brought in. */
static void leave_dividend(Snes *s)
{
    set_scene_call_first(state_of(s), scene_program_own(), (uint16_t)((uint16_t)(EIGHT << 8) | (m_flag(s, CPU_FLAG_C) ? 0x80 : 0)));
}

/* The main loop's call each pass, with 16-bit A. With the last picture still
 * to be sent it is back at once, the carry as it came; otherwise the scaler's
 * rows go on in the original's code, which comes back to the gathering. */
void scene_ending_scale_picture(Snes *s)
{
    GameState *g = state_of(s);
    if (!hand_picture_to_scaler(g)) {
        m_rts(s);
        return;
    }
    ORIGINAL_EFFECT(leave_dividend(s));
    scene_bridge_scale_ending_picture(s);
}

bool scene_ending_gather_picture_present(Snes *s)
{
    return scene_on_scene_page(s) && m_code_matches(s, 0x7E8983, 0x3D, 0xB0AE4CBCF063A65Dull) && scene_matrix_code_present(s);
}

/* Back from the scaler with 16-bit A and the data bank on the scaler's bank;
 * back to the main loop with the data bank the scaling kept on the stack and
 * the carry of the zoom's last compare: whether the picture is under half its
 * size. */
void scene_ending_gather_picture(Snes *s)
{
    GameState *g = state_of(s);
    gather_picture(g);
    s->cpu.db = m_pull8(s);
    m_set_flag(s, CPU_FLAG_C, scene_ending_picture_zoom(g) >= HALF_SIZE);
    m_rts(s);
}
