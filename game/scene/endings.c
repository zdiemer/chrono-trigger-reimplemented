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
 *   the window a frame task sweeps across it ($7E:9A0C), and "The End" drawn
 *   over it ($7E:99FA).
 *
 * The captions' turns are captions.c's, the star field's flight stars.c's,
 * the Beyond Time ending's night (its fireworks, which the setup places, and
 * its flight over the world) beyond_time.c's.
 *
 * And the scripts' small routines: the picture stopped, the ground's colors
 * changed or stepped toward a later set, the captions' place, the fades to
 * black; and the credits' pictures sent in the vblank.
 *
 * Every entry is the scene program's code in work RAM, guarded by a hash of
 * it. What the scripts call runs on the calling channel's page with the
 * scripts' data bank, and works on its own: the routine keeps the page and a
 * byte of the bank on the stack while it works, as the original does, so the
 * original's code it calls out to finds the stack where it expects.
 *
 * The setup and the routines that load are one sequence each (`*_with`),
 * whose steps that are the program's other routines -- its unpacker
 * ($7E:3691), a script's first step ($7E:3749), the stars' scatter -- the
 * machine's entries take as calls of the original's and the native program
 * as C (EndingSteps). */
#include "scene/race.h"

#include "charge.h"
#include "core/palette.h"
#include "core/program.h"
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
enum { PAIRS_ACROSS = 4 };

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
    set_scene_ending_picture_zoom(g, zoom);
    set_scene_ending_picture_half(g, scene_divide_8_8_by(g, scene_program_own(), EIGHT, zoom));
    scene_scaler_start(g, (SceneScaling){ .source = scene_ending_picture_source(g), .rows = layout->rows, .pairs = PAIRS_ACROSS,
                                          .row_step = zoom, .limit = layout->tile_bytes });
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
 * them, a frame for each 32nd of a turn of the animation (1024 steps a turn). */
enum { FIRST_FRAME = 0x1200, FRAME_SHIFT = 5, FRAMES = 0x1F, FRAME_BYTES_SHIFT = 10, ROUNDING = 0x10 };

// $7E:8921
void scene_ending_show_picture_frame(GameState *g, uint16_t turn)
{
    uint16_t frame = (uint16_t)(turn >> FRAME_SHIFT & FRAMES);
    set_scene_ending_picture_source(g, (uint16_t)(FIRST_FRAME + (frame << FRAME_BYTES_SHIFT)));
}

// $7E:891B
/* Each frame: the picture's frame of its animation, for the next scaling,
 * where channel 0's register turns it, rounded to the nearest. */
static uint16_t pick_picture_frame(GameState *g)
{
    scene_ending_show_picture_frame(g, (uint16_t)(scene_channel_register(g, STEERING_CHANNEL, PICTURE_FRAME) + ROUNDING));
    return scene_ending_picture_source(g);
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
void scene_send_letters_widened(GameState *g, Platform *pf)
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

/* Past the plane's name table (which the two halves leave VRAM's address at
 * the end of), the low bytes of the next $1000 words are the blank tile. */
enum { BLANKED_PAST_THE_PLANE = 0x1000 };

/* Mode 10's picture, made of tiles: 320 tile numbers, each picking one of the
 * unpacked tiles, laid out in turn from $D800 in the second bank; the last
 * $800 bytes of it go to VRAM $5C00, and a vblank task of the program's goes on
 * with it. */
enum { MOSAIC_TILES = 0x140, MOSAIC_TILE_BYTES = 0x20, MOSAIC_TILESET = SCENE_UNPACK_BUFFER, MOSAIC_NUMBERS = 0x7F2200 };
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

/* The scene's colors, out of the program's resources: the ground's first 128
 * of the loaded palette, three more sets of them that the scripts later
 * change the ground's to (each 256 bytes, kept in the program's bank), and 32
 * more from color 128. */
typedef struct {
    uint8_t resource;
    uint16_t to;
    uint16_t bytes;
} ResourceCopy;

#define LOADED_COLOR(n) (uint16_t)(LOADED_PALETTE_PAGE + (n) * COLOR_BYTES)

enum { GROUND_COLOR_SETS = 3 };
static const uint16_t ground_color_sets[GROUND_COLOR_SETS] = { 0xC995, 0xCA95, 0xCB95 };

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
void scene_set_caption_colours(GameState *g)
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

/* ---- The resources, and the steps that are the program's other routines ----------------------- */

/* The captions' letters, and the regrouping code for the zooming picture. */
enum { LETTERS = 0x63, MATRIX_DESTINATION = SCENE_TILEMAP_BUFFER, MATRIX_SOURCE = 0x0200 };

/* The ground: its pixels into the high halves of VRAM's words, its name table
 * in two halves, then the sprites' second page of tiles. */
enum { GROUND_PIXELS = 0x7E, GROUND_MAP_FIRST = 0x81, GROUND_MAP_SECOND = 0xD2, MORE_SPRITE_TILES = 0x87 };
enum { MORE_SPRITE_TILES_VRAM = 0x4000 };

/* Mode 10's picture: its tiles, and its tile numbers unpacked after them. */
enum { MOSAIC_TILESET_RESOURCE = 0xBD, MOSAIC_NUMBERS_RESOURCE = 0xB4, MOSAIC_NUMBERS_AT = 0x2200 };

/* The scene's palette, unpacked into the loaded one from color 192. */
enum { ENDING_PALETTE = 0x8A, ENDING_PALETTE_AT = 0x2180 };

/* The new ground the zooming picture's second showing has, and its picture;
 * the color math past the plane's last line. */
enum { NEW_GROUND = 0x84, PICTURE_FRAMES = 0xD8, PICTURE_FRAMES_AT = 0x0A00, FAR_COLOR_MATH = 0x05 };

/* The stars' sprites' tiles, and their colors as sprite palette 1. */
enum { STAR_TILES = 0x33, STAR_TILES_VRAM = 0x4600, STAR_COLORS = 19, STAR_PALETTE = 144 };

/* The Mode 7 picture's tiles, name table and colors. */
enum { PLANE_TILES = 0xCF, PLANE_MAP = 0xB1, PLANE_COLORS = 0x54 };

/* The credits' letters, the picture they roll over (its tiles, and the map
 * made below), and the pattern's tiles. */
enum { CREDITS_LETTERS = 0xAE, CREDITS_PICTURE = 0x8D, CREDITS_PICTURE_AT = 0x0400, CREDITS_PATTERN = 0xC0 };
enum { CREDITS_PATTERN_AT = 0x0F00, PATTERN_MAP = 0x1000 };

/* What the endings unpack, each from its own call of the program's unpacker. */
typedef enum {
    UNPACK_LETTERS, UNPACK_GROUND_PIXELS, UNPACK_GROUND_MAP_FIRST, UNPACK_GROUND_MAP_SECOND, UNPACK_MORE_SPRITE_TILES,
    UNPACK_MOSAIC_TILESET, UNPACK_MOSAIC_NUMBERS, UNPACK_ENDING_PALETTE, UNPACK_PICTURE_FRAMES, UNPACK_NEW_GROUND,
    UNPACK_STAR_TILES, UNPACK_PLANE_TILES, UNPACK_PLANE_MAP, UNPACK_PLANE_COLORS, UNPACK_CREDITS_LETTERS,
    UNPACK_CREDITS_PICTURE, UNPACK_CREDITS_PATTERN, ENDING_UNPACKS
} EndingUnpack;

static const struct {
    uint32_t site;
    uint16_t resource;
} ending_unpacks[ENDING_UNPACKS] = {
    [UNPACK_LETTERS] = { 0x7E8549, LETTERS },
    [UNPACK_GROUND_PIXELS] = { 0x7E857B, GROUND_PIXELS },
    [UNPACK_GROUND_MAP_FIRST] = { 0x7E8598, GROUND_MAP_FIRST },
    [UNPACK_GROUND_MAP_SECOND] = { 0x7E85A5, GROUND_MAP_SECOND },
    [UNPACK_MORE_SPRITE_TILES] = { 0x7E85C3, MORE_SPRITE_TILES },
    [UNPACK_MOSAIC_TILESET] = { 0x7E85E4, MOSAIC_TILESET_RESOURCE },
    [UNPACK_MOSAIC_NUMBERS] = { 0x7E85F0, MOSAIC_NUMBERS_RESOURCE },
    [UNPACK_ENDING_PALETTE] = { 0x7E869B, ENDING_PALETTE },
    [UNPACK_PICTURE_FRAMES] = { 0x7E87E9, PICTURE_FRAMES },
    [UNPACK_NEW_GROUND] = { 0x7E87B0, NEW_GROUND },
    [UNPACK_STAR_TILES] = { 0x7E8BE4, STAR_TILES },
    [UNPACK_PLANE_TILES] = { 0x7E8C48, PLANE_TILES },
    [UNPACK_PLANE_MAP] = { 0x7E8C76, PLANE_MAP },
    [UNPACK_PLANE_COLORS] = { 0x7E8C8E, PLANE_COLORS },
    [UNPACK_CREDITS_LETTERS] = { 0x7E8CF8, CREDITS_LETTERS },
    [UNPACK_CREDITS_PICTURE] = { 0x7E8D04, CREDITS_PICTURE },
    [UNPACK_CREDITS_PATTERN] = { 0x7E8D10, CREDITS_PATTERN },
};

/* The endings' steps that are the program's other routines: a resource
 * unpacked, the steering script's first step (with the picture script's
 * start, which it reads), and the stars scattered. The machine's entries take
 * them as calls of the original's, the native program as C. */
typedef struct {
    void (*unpack)(void *ctx, EndingUnpack which);
    bool (*first_step)(void *ctx, uint16_t picture_script);
    void (*scatter_the_stars)(void *ctx);
    void (*tiles_expanded)(void *ctx);   /* NULL: nothing to leave */
} EndingSteps;

/* ---- The setup: the ground, the captions, the scripts ------------------------------------------- */

static void load_letters_and_ground(GameState *g, Platform *pf, const EndingSteps *steps, void *ctx)
{
    vid_start_vram_uploads(pf, PF_AT(0x7E853A), SCENE_VRAM_WHOLE_WORDS);
    set_decompress_dest(g, SCENE_UNPACK_BUFFER);
    vid_set_vram_address(pf, PF_AT(0x7E8543), LETTER_TILES_VRAM);
    steps->unpack(ctx, UNPACK_LETTERS);
    scene_send_letters_widened(g, pf);
    set_scene_caption_letter_attributes(g, CAPTION_LETTERS_IN_FRONT);
    TIMED_AS(g, 0xC309E9, scene_write_matrix_code_for(g, MATRIX_DESTINATION, MATRIX_SOURCE));

    vid_start_vram_uploads(pf, PF_AT(0x7E856F), SCENE_VRAM_WHOLE_WORDS);
    vid_set_vram_address(pf, PF_AT(0x7E8575), 0x0000);
    steps->unpack(ctx, UNPACK_GROUND_PIXELS);
    send_unpacked_as(g, BYTES_ONE_REGISTER, VID_SNES_VRAM_DATA_HIGH);
    send_unpacked(g, pf);

    vid_start_vram_uploads(pf, PF_AT(0x7E858C), SCENE_VRAM_LOW_BYTES);
    vid_set_vram_address(pf, PF_AT(0x7E8592), 0x0000);
    steps->unpack(ctx, UNPACK_GROUND_MAP_FIRST);
    scene_widen_the_plane_map(g);
    TIMED_AS(g, 0x7E8752, scene_upload_plane_half(pf));
    steps->unpack(ctx, UNPACK_GROUND_MAP_SECOND);
    scene_widen_the_plane_map(g);
    TIMED_AS(g, 0x7E8752, scene_upload_plane_half(pf));
    scene_blank_past_the_plane(pf, PF_AT(0x7E85B2), BLANKED_PAST_THE_PLANE);

    vid_start_vram_uploads(pf, PF_AT(0x7E85B7), SCENE_VRAM_WHOLE_WORDS);
    vid_set_vram_address(pf, PF_AT(0x7E85BD), MORE_SPRITE_TILES_VRAM);
    steps->unpack(ctx, UNPACK_MORE_SPRITE_TILES);
    send_unpacked_as(g, SCENE_TRANSFER_WORDS, VID_SNES_VRAM_DATA_LOW);
    send_unpacked(g, pf);
}

/* Mode 10's picture, laid out from its tiles and sent. */
static void load_mosaic(GameState *g, Platform *pf, const EndingSteps *steps, void *ctx)
{
    steps->unpack(ctx, UNPACK_MOSAIC_TILESET);
    unpack_at(g, MOSAIC_NUMBERS_AT);
    steps->unpack(ctx, UNPACK_MOSAIC_NUMBERS);
    lay_out_mosaic(g);
    vid_send_vram(pf, PF_AT(0x7E864C), MOSAIC_VRAM, PF_GAME(MOSAIC_SENT), MOSAIC_SENT_BYTES, NULL);
    scene_add_task(g, SCENE_VBLANK_TASKS, MOSAIC_TASK);
}

static void load_ground_colors(GameState *g, const EndingSteps *steps, void *ctx)
{
    unpack_into_first_bank(g);
    for (unsigned i = 0; i < GROUND_COPIES; i++)
        scene_copy_resource_to(g, scene_program_own(), ground_copies[i].resource, ground_copies[i].to, ground_copies[i].bytes);
    scene_set_caption_colours(g);
    set_decompress_dest(g, (decompress_dest(g) & 0xFF0000) | ENDING_PALETTE_AT);
    steps->unpack(ctx, UNPACK_ENDING_PALETTE);
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
/* The shared endings' setup, from the program's mode table: the captions'
 * letters and the ground loaded (and mode 10's picture), the colors, the two
 * scripts its mode picks started and the steering one's first step taken, the
 * captions and the fireworks ready, the per-line effects and the ground's
 * layers set up. Returns where the per-line effects' descriptors ended, or 0
 * where a step went no further. */
static uint16_t set_up_ending_with(GameState *g, Platform *pf, const EndingSteps *steps, void *ctx)
{
    unsigned mode = ending_mode(g);
    load_letters_and_ground(g, pf, steps, ctx);
    if (mode == MODE_CAPTIONED)
        load_mosaic(g, pf, steps, ctx);
    load_ground_colors(g, steps, ctx);

    EndingScripts scripts = scripts_for(mode);
    if (mode == MODE_CAPTIONED || mode == MODE_CAPTIONED_TOO)
        set_scene_ending_caption_text(g, CAPTIONS_TEXT);
    set_scene_channels_script(g, STEERING_CHANNEL, scripts.steering);
    set_scene_channels_script(g, PICTURE_CHANNEL, scripts.picture);
    if (!steps->first_step(ctx, scripts.picture))
        return 0;
    set_scene_perspective_start(g, PERSPECTIVE_FROM_ABOVE_THE_HORIZON);
    TIMED_AS(g, 0xFDE275, palette_stop_animations(g));
    start_captions(g);
    set_scene_reset_disabled(g, 1);
    TIMED_AS(g, 0x7E9BB8, scene_set_the_fireworks_up(g));

    uint16_t descriptors_end = 0;
    TIMED_AS(g, 0x7E36CC, descriptors_end = scene_set_up_line_effects(g, pf, ENDING_LINE_EFFECTS));
    set_scene_line_effects(g, ENDING_LINE_CHANNELS);
    set_scene_fade_period(g, FADE_EVERY_FRAME);
    set_scene_fade_countdown(g, FADE_EVERY_FRAME);
    set_up_ground_layers(pf);
    return descriptors_end;
}

/* ---- The scenes the scripts call --------------------------------------------------------------- */

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

/* The picture's frames unpacked and mirrored into place. */
static uint16_t load_picture(GameState *g, SceneProgramPage page, const EndingSteps *steps, void *ctx)
{
    set_decompress_dest(g, SCENE_UNPACK_BUFFER);
    unpack_at(g, PICTURE_FRAMES_AT);
    steps->unpack(ctx, UNPACK_PICTURE_FRAMES);
    mirror_picture_frames(g);
    return load_picture_colors(g, page);
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
static void load_new_ground(GameState *g, Platform *pf, SceneProgramPage page, const EndingSteps *steps, void *ctx)
{
    scene_use_loaded_palette(g);
    set_scene_plane_far_color_math(g, FAR_COLOR_MATH);
    set_decompress_dest(g, SCENE_UNPACK_BUFFER);
    steps->unpack(ctx, UNPACK_NEW_GROUND);
    scene_double_plane_rows(g);
    /* the original counts the rows down in its page's first cell */
    ORIGINAL_EFFECT(set_scene_call_first(g, page, 0));
    vid_start_vram_uploads(pf, PF_AT(0x7E87B8), SCENE_VRAM_LOW_BYTES);
    vid_set_vram_address(pf, PF_AT(0x7E87BB), 0x0000);
    vid_set_mode7(pf, PF_AT(0x7E87C1), &(VidMode7){ .outside = VID_M7_WRAP }, VID_M7_SELECT);
    TIMED_AS(g, 0x7E8752, scene_upload_plane_half(pf));
    TIMED_AS(g, 0x7E8752, scene_upload_plane_half(pf));
}

/* The zooming picture's two routines: with the ground it has, or over a new
 * one. Each returns where the last copy of its colors ended. */
static uint16_t load_the_picture_with(GameState *g, Platform *pf, SceneProgramPage page, const EndingSteps *steps, void *ctx)
{
    start_picture(g, pf, 0x7E87DA);
    return load_picture(g, page, steps, ctx);
}

static uint16_t load_the_ground_and_picture_with(GameState *g, Platform *pf, SceneProgramPage page,
                                                 const EndingSteps *steps, void *ctx)
{
    start_picture(g, pf, 0x7E879E);
    load_new_ground(g, pf, page, steps, ctx);
    return load_picture(g, page, steps, ctx);
}

// $7E:8BAF
/* The stars: everything stopped, the star field scattered, and its tiles and
 * colors loaded. */
static void start_stars_with(GameState *g, Platform *pf, const EndingSteps *steps, void *ctx)
{
    stop_frame_tasks(g);
    stop_vblank_tasks(g);
    blank_the_screen(g, pf, 0x7E8BC0, 0x7E8BC8);
    steps->scatter_the_stars(ctx);
    set_decompress_dest(g, SCENE_UNPACK_BUFFER);
    send_unpacked_as(g, SCENE_TRANSFER_WORDS, VID_SNES_VRAM_DATA_LOW);
    vid_set_vram_address(pf, PF_AT(0x7E8BDE), STAR_TILES_VRAM);
    steps->unpack(ctx, UNPACK_STAR_TILES);
    send_unpacked(g, pf);
    set_decompress_dest(g, decompress_dest(g) & 0xFF);
    copy_colors(g, scene_program_own(), STAR_COLORS, STAR_PALETTE, 16);
    scene_use_loaded_palette(g);
}

/* The Mode 7 picture: its tiles, expanded into the plane's pixels, its name
 * table, and its colors from color 16. Its tiles are of eight planes, which
 * the program's loop over tiles ($7E:5E3C) takes once its code is made to
 * call the eight-plane expansion and to move on from the end of a tile's
 * first pair of planes (where the expansion leaves it) to the next tile; the
 * port expands them as C, and makes the code the same, which is the
 * program's state. */
enum { PLANE_TILES_VRAM = 0x6000, PLANE_TILES_TO_EXPAND = 0xA4 };
enum { EXPAND_WIDE_TILE = 0x5EF8, UNPACKED_TILES = 0x0200 };

// $7E:5E3C
static void expand_the_picture_tiles(GameState *g, Platform *pf)
{
    uint16_t tile_bytes = scene_plane_tile_bytes(SCENE_MOST_PLANES);
    set_scene_plane_tiles_expander(g, EXPAND_WIDE_TILE);
    set_scene_plane_tiles_stride(g, (uint8_t)(tile_bytes - SCENE_PLANE_PAIR_BYTES));
    (void)scene_expand_tiles(g, pf, UNPACKED_TILES, PLANE_TILES_TO_EXPAND, SCENE_MOST_PLANES, tile_bytes);
}
enum { PLANE_MAP_VRAM = 0x4400, PLANE_COLORS_AT = 0x2020 };

// $7E:8C14
static void start_mode7_picture_with(GameState *g, Platform *pf, const EndingSteps *steps, void *ctx)
{
    stop_frame_tasks(g);
    stop_vblank_tasks(g);
    blank_the_screen(g, pf, 0x7E8C25, 0x7E8C2D);
    vid_set_mode7(pf, PF_AT(0x7E8C30), &(VidMode7){ .outside = VID_M7_TRANSPARENT }, VID_M7_SELECT);
    set_decompress_dest(g, SCENE_UNPACK_BUFFER);
    send_unpacked_as(g, SCENE_TRANSFER_WORDS, VID_SNES_VRAM_DATA_LOW);
    vid_start_vram_uploads(pf, PF_AT(0x7E8C42), SCENE_VRAM_WHOLE_WORDS);
    steps->unpack(ctx, UNPACK_PLANE_TILES);
    vid_set_vram_address(pf, PF_AT(0x7E8C4E), PLANE_TILES_VRAM);
    send_unpacked(g, pf);
    vid_set_vram_address(pf, PF_AT(0x7E8C58), 0x0000);
    expand_the_picture_tiles(g, pf);
    if (steps->tiles_expanded)
        steps->tiles_expanded(ctx);
    steps->unpack(ctx, UNPACK_PLANE_MAP);
    vid_set_vram_address(pf, PF_AT(0x7E8C7C), PLANE_MAP_VRAM);
    send_unpacked(g, pf);
    TIMED_AS(g, 0x7E5AB6, scene_load_plane_map(g, pf, scene_unpacked_bank(g)));
    set_decompress_dest(g, PLANE_COLORS_AT);
    steps->unpack(ctx, UNPACK_PLANE_COLORS);
    scene_set_letter_colors(g);
    scene_use_loaded_palette(g);
    TIMED_AS(g, 0x7E388B, scene_send_palette(pf));
    vid_set_screens(pf, PF_AT(0x7E8CA1), VID_BG1_BIT, VID_OBJ_BIT, VID_SCREEN_MAIN | VID_SCREEN_SUB);
    vid_set_windows(pf, PF_AT(0x7E8CA7), &(VidWindows){ 0 }, VID_WINDOWS_MAIN | VID_WINDOWS_SUB);
    vid_set_mode(pf, PF_AT(0x7E8CAC), &(VidMode){ .mode = 7 });
}

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
static bool prepare_credits_with(GameState *g, Platform *pf, const EndingSteps *steps, void *ctx)
{
    stop_frame_tasks(g);
    vid_enable_line_effects(pf, PF_AT(0x7E8CDD), 0);
    if (ending_mode(g) == MODE_WITHOUT_CREDITS) {
        set_scene_channels_script(g, STEERING_CHANNEL, NO_CREDITS_SCRIPT);
        return false;
    }
    set_decompress_dest(g, SCENE_UNPACK_BUFFER);
    steps->unpack(ctx, UNPACK_CREDITS_LETTERS);
    unpack_at(g, CREDITS_PICTURE_AT);
    steps->unpack(ctx, UNPACK_CREDITS_PICTURE);
    unpack_at(g, CREDITS_PATTERN_AT);
    steps->unpack(ctx, UNPACK_CREDITS_PATTERN);
    write_pattern_map(g);
    return true;
}

/* The credits' screen: the letters' blank tiles and the map blanked, the
 * per-line effect, the text, and the layers -- the pattern on BG1, the
 * credits on BG2, mode 4, the credits subtracted from what is under them. */
enum { CREDITS_TILES_VRAM = 0x4000, CREDITS_BLANK_TILES = SCENE_UNPACK_BUFFER, BLANK_TILES_BYTES = 0x200 };
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

/* The credits' pictures sent in the vblank, a piece a task, each taking
 * itself off the list after: the picture's tiles, the pattern's map and its
 * tiles, as the credits' loading left them in the second bank; and a stretch
 * of a map from $20A0 blanked with the blank tile. */
typedef struct {
    uint16_t vram;
    uint32_t from;
    uint16_t bytes;
    uint32_t site;
} CreditsPiece;

enum { CREDITS_PICTURE_SENT, CREDITS_PATTERN_MAP_SENT, CREDITS_PATTERN_TILES_SENT, CREDITS_PIECES };
static const CreditsPiece credits_pieces[CREDITS_PIECES] = {
    [CREDITS_PICTURE_SENT] = { 0x5000, 0x7F0000 | CREDITS_PICTURE_AT, 0x0A00, 0x7E9F2B },
    [CREDITS_PATTERN_MAP_SENT] = { 0x4C00, 0x7F0000 | PATTERN_MAP, 0x0800, 0x7E9F39 },
    [CREDITS_PATTERN_TILES_SENT] = { 0x0000, 0x7F0000 | CREDITS_PATTERN_AT, 0x0100, 0x7E9F47 },
};

// $7E:9F2B, $7E:9F39, $7E:9F47
static void send_a_credits_piece(Platform *pf, unsigned piece)
{
    const CreditsPiece *p = &credits_pieces[piece];
    vid_send_vram(pf, PF_AT(p->site), p->vram, PF_GAME(p->from), p->bytes, SCENE_VRAM_WHOLE_WORDS);
}

enum { BLANKED_MAP_VRAM = 0x20A0, BLANKED_MAP_BYTES = 0x06C0 };

// $7E:9F73
static void blank_the_map(Platform *pf)
{
    vid_send_vram_fill(pf, PF_AT(0x7E9F73), BLANKED_MAP_VRAM, PF_GAME(SCENE_BLANK_TILE), BLANKED_MAP_BYTES,
                       SCENE_VRAM_WHOLE_WORDS);
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

/* ---- The steps on the machine, and as C ----------------------------------------------------------- */

/* On the machine: the original's unpacker and first script step, and the
 * stars' scatter starting its draws from A as the screen's blanking left it,
 * and the carry the script's call came with, which nothing here touches. */
enum { BLANKING_LEFT_IN_A = 0x0080 };

static void unpack_on_the_machine(void *ctx, EndingUnpack which)
{
    scene_bridge_unpack(ctx, ending_unpacks[which].site, ending_unpacks[which].resource);
}

/* The Mode 7 picture's tiles' loop leaves the overflow of its last move on
 * (to $2B00: none), which the unpacker after it reads. */
static void tiles_expanded_on_the_machine(void *ctx) { ORIGINAL_EFFECT(m_set_flag(ctx, CPU_FLAG_V, false)); }

/* The steering script's first step, under the program's bank. It reads Y and
 * the overflow: Y as the picture script's start left it, the overflow clear, as
 * the regrouping code's writer left it. */
static bool first_step_on_the_machine(void *ctx, uint16_t picture_script)
{
    Snes *s = ctx;
    s->cpu.db = SCENE_LOWER_WORK_RAM;   /* as the darkening's block move left it */
    m_set_flag(s, CPU_FLAG_V, false);
    scene_bridge_first_script_step(s, 0x7E86E6, picture_script);
    return true;
}

static void scatter_on_the_machine(void *ctx) { scene_bridge_scatter_stars(ctx, BLANKING_LEFT_IN_A); }

static const EndingSteps on_the_machine = { unpack_on_the_machine, first_step_on_the_machine, scatter_on_the_machine,
                                            tiles_expanded_on_the_machine };

/* As C, for a program with no machine. The stars' first draw starts from
 * what the original's does, the carry clear. */
typedef struct {
    GameState *game;
    Platform *pf;
} EndingInC;

static void unpack_the_resource(void *ctx, EndingUnpack which)
{
    EndingInC *c = ctx;
    TIMED_AS(c->game, SCENE_UNPACK, scene_unpacks(c->game, ending_unpacks[which].resource));
}

static bool turn_the_steering_channel(void *ctx, uint16_t picture_script)
{
    EndingInC *c = ctx;
    (void)picture_script;
    SceneTurn turn = SCENE_TURN_ENDED;
    TIMED_AS(c->game, SCENE_FIRST_STEP, turn = scene_channel_turn(c->game, c->pf, STEERING_CHANNEL));
    return turn == SCENE_TURN_ENDED;
}

enum { SCATTER_STARS = 0x7E5777 };

static void scatter_the_stars(void *ctx)
{
    EndingInC *c = ctx;
    TIMED_AS(c->game, SCATTER_STARS,
             scene_scatter_stars(c->game, c->pf, scene_draw_from(BLANKING_LEFT_IN_A, false, TM_ENTROPY_BYTES)));
}

static const EndingSteps as_c = { unpack_the_resource, turn_the_steering_channel, scatter_the_stars, NULL };

/* Mode 10's picture needs room on the vblank list for its task. */
static bool room_for_the_mosaic(const GameState *g)
{
    return ending_mode(g) != MODE_CAPTIONED || scene_task_listed(g, SCENE_VBLANK_TASKS, NO_TASK);
}

enum { ENDINGS_SET_UP = 0x7E8538 };

bool scene_ending_set_up(GameState *g, Platform *pf)
{
    if (!room_for_the_mosaic(g)) {
        program_needs_the_machine(g, ENDINGS_SET_UP, "an ending's picture with no room on the vblank list");
        return false;
    }
    EndingInC c = { g, pf };
    return set_up_ending_with(g, pf, &as_c, &c) != 0;
}

/* The routines the scripts call, as the native program's rows (scene/script.c):
 * each on the calling channel's page. */
static void load_the_picture(GameState *g, Platform *pf, uint8_t channel)
{
    EndingInC c = { g, pf };
    (void)load_the_picture_with(g, pf, scene_program_channel(channel), &as_c, &c);
}

static void load_the_ground_and_picture(GameState *g, Platform *pf, uint8_t channel)
{
    EndingInC c = { g, pf };
    (void)load_the_ground_and_picture_with(g, pf, scene_program_channel(channel), &as_c, &c);
}

static void start_the_stars(GameState *g, Platform *pf, uint8_t channel)
{
    (void)channel;
    EndingInC c = { g, pf };
    start_stars_with(g, pf, &as_c, &c);
}

static void start_the_mode7_picture(GameState *g, Platform *pf, uint8_t channel)
{
    (void)channel;
    EndingInC c = { g, pf };
    start_mode7_picture_with(g, pf, &as_c, &c);
}

static void prepare_the_credits(GameState *g, Platform *pf, uint8_t channel)
{
    (void)channel;
    EndingInC c = { g, pf };
    (void)prepare_credits_with(g, pf, &as_c, &c);
}

static void set_the_last_colours(GameState *g, Platform *pf, uint8_t channel)
{
    (void)pf;
    (void)set_last_colors(g, scene_program_channel(channel));
}

/* The native program's rows alone: on the machine each runs as its own
 * registered entry, which takes the original's unpacker as calls of it. */
const SceneScriptRoutine scene_ending_loading_routines[] = {
    { SCENE_ROUTINE_LOAD_THE_GROUND_AND_PICTURE, load_the_ground_and_picture, scene_ending_load_ground_and_picture_present,
      false },
    { SCENE_ROUTINE_LOAD_THE_PICTURE, load_the_picture, scene_ending_load_picture_present, false },
    { SCENE_ROUTINE_START_THE_STARS, start_the_stars, scene_ending_start_stars_present, false },
    { SCENE_ROUTINE_START_THE_MODE7_PICTURE, start_the_mode7_picture, scene_ending_start_mode7_picture_present, false },
    { SCENE_ROUTINE_PREPARE_THE_CREDITS, prepare_the_credits, scene_ending_prepare_credits_present, false },
    { SCENE_ROUTINE_SET_THE_LAST_COLOURS, set_the_last_colours, scene_ending_set_last_colors_present, false },
};
const unsigned scene_ending_loading_routine_count = sizeof scene_ending_loading_routines / sizeof scene_ending_loading_routines[0];

/* ---- The scripts' other routines: the picture's stop, the ground's colors, the captions' place -- */

/* The main loop's call each pass that does nothing. */
enum { NOTHING_EACH_PASS = 0x3176 };

// $7E:96FB
/* The picture stops zooming: the main loop scales it no more. */
static void stop_the_picture(GameState *g, Platform *pf, uint8_t channel)
{
    (void)pf;
    (void)channel;
    set_scene_main_loop_call(g, NOTHING_EACH_PASS);
}

/* The ground's first 112 colors step toward one of its later sets, a step
 * each of 32 times the planet's step comes round (planet.c). */
enum { STEPPED_COLORS = 0x70, COLOR_STEPS = 0x20 };

static void step_the_ground_toward(GameState *g, unsigned set)
{
    scene_step_the_colours_toward(g, (SceneColourStep){ .toward = ground_color_sets[set], .palette = WORKING_PALETTE_PAGE,
                                                        .colours = STEPPED_COLORS, .steps = COLOR_STEPS });
}

// $7E:98F6, $7E:98FB, $7E:9900
static void step_the_ground_toward_the_first(GameState *g, Platform *pf, uint8_t channel)
{
    (void)pf;
    (void)channel;
    step_the_ground_toward(g, 0);
}

static void step_the_ground_toward_the_second(GameState *g, Platform *pf, uint8_t channel)
{
    (void)pf;
    (void)channel;
    step_the_ground_toward(g, 1);
}

static void step_the_ground_toward_the_third(GameState *g, Platform *pf, uint8_t channel)
{
    (void)pf;
    (void)channel;
    step_the_ground_toward(g, 2);
}

/* The ground's colors become its first later set at once. The original's
 * move counts one byte past the 128 colors, into color 128's low byte. */
enum { GROUND_COLORS_MOVED = 0x101 };

// $7E:9702
static void show_the_ground_s_first_later_colors(GameState *g, Platform *pf, uint8_t channel)
{
    (void)pf;
    (void)channel;
    uint32_t bank = (uint32_t)SCENE_LOWER_WORK_RAM << 16;
    wram_copy(g, bank | ground_color_sets[0], bank | WORKING_PALETTE_PAGE, GROUND_COLORS_MOVED);
    ORIGINAL_EFFECT(charge_block_move(g, GROUND_COLORS_MOVED));
}

// $7E:9919
/* The captions' lines start where the script has stored their place. */
static void place_the_captions(GameState *g, Platform *pf, uint8_t channel)
{
    (void)pf;
    (void)channel;
    set_scene_caption_line_x(g, scene_ending_captions_x(g));
    set_scene_caption_line_y(g, scene_ending_captions_y(g));
}

/* Colors fade to black, a level a frame or every other frame, each run in its
 * own palette animation slot: the sprites' seventh palette but its clear
 * color, and the layers' first 127 colors but the clear color, in two runs. */
static const PaletteFade sprites_fade_out = { .first_color = 0xE1, .color_count = 0x0F, .step = 1, .period = 1,
                                              .from_level = 0, .to_level = 0xFF };
static const PaletteFade first_layer_colors_fade_out = { .first_color = 0x01, .color_count = 0x40, .step = 1, .period = 2,
                                                         .from_level = 0, .to_level = 0xFF };
static const PaletteFade more_layer_colors_fade_out = { .first_color = 0x41, .color_count = 0x3F, .step = 1, .period = 2,
                                                        .from_level = 0, .to_level = 0xFF };

// $7E:9F8A
static void fade_the_sprites_out(GameState *g, Platform *pf, uint8_t channel)
{
    (void)pf;
    (void)channel;
    palette_start_fade_to_black(g, 0, sprites_fade_out);
}

// $7E:9FC8
static void fade_the_first_layer_colors_out(GameState *g, Platform *pf, uint8_t channel)
{
    (void)pf;
    (void)channel;
    palette_start_fade_to_black(g, 1, first_layer_colors_fade_out);
}

// $7E:9FEA
static void fade_more_layer_colors_out(GameState *g, Platform *pf, uint8_t channel)
{
    (void)pf;
    (void)channel;
    palette_start_fade_to_black(g, 2, more_layer_colors_fade_out);
}

const SceneScriptRoutine scene_ending_routines[] = {
    [SCENE_ROW_STOP_THE_PICTURE] = { SCENE_ROUTINE_STOP_THE_PICTURE, stop_the_picture, scene_stop_the_picture_present, false },
    [SCENE_ROW_STEP_TOWARD_THE_FIRST] = { SCENE_ROUTINE_STEP_THE_GROUND_TOWARD_THE_FIRST, step_the_ground_toward_the_first,
                                          scene_step_the_ground_s_colors_present, false },
    [SCENE_ROW_STEP_TOWARD_THE_SECOND] = { SCENE_ROUTINE_STEP_THE_GROUND_TOWARD_THE_SECOND, step_the_ground_toward_the_second,
                                           scene_step_the_ground_s_colors_present, false },
    [SCENE_ROW_STEP_TOWARD_THE_THIRD] = { SCENE_ROUTINE_STEP_THE_GROUND_TOWARD_THE_THIRD, step_the_ground_toward_the_third,
                                          scene_step_the_ground_s_colors_present, false },
    [SCENE_ROW_SHOW_THE_FIRST_LATER_COLORS] = { SCENE_ROUTINE_SHOW_THE_GROUND_S_FIRST_LATER_COLORS,
                                                show_the_ground_s_first_later_colors, scene_show_the_ground_s_colors_present, false },
    [SCENE_ROW_PLACE_THE_CAPTIONS] = { SCENE_ROUTINE_PLACE_THE_CAPTIONS, place_the_captions, scene_place_the_captions_present, false },
    [SCENE_ROW_FADE_THE_SPRITES_OUT] = { SCENE_ROUTINE_FADE_THE_SPRITES_OUT, fade_the_sprites_out,
                                         scene_fade_the_sprites_out_present, false },
    [SCENE_ROW_FADE_THE_FIRST_LAYER_COLORS_OUT] = { SCENE_ROUTINE_FADE_THE_FIRST_LAYER_COLORS_OUT,
                                                    fade_the_first_layer_colors_out,
                                                    scene_fade_the_first_layer_colors_out_present, false },
    [SCENE_ROW_FADE_MORE_LAYER_COLORS_OUT] = { SCENE_ROUTINE_FADE_MORE_LAYER_COLORS_OUT, fade_more_layer_colors_out,
                                               scene_fade_more_layer_colors_out_present, false },
};
const unsigned scene_ending_routine_count = sizeof scene_ending_routines / sizeof scene_ending_routines[0];

#if ORIGINAL_EFFECTS
/* What each leaves in A, as its last load left it: the empty routine's
 * address, the move's count run out, the colors' count, the fades' kind in A's
 * low byte; Y, past the colors the move took; and the captions' place down
 * in A's low byte. */
enum { NOTHING_EACH_PASS_LEFT = NOTHING_EACH_PASS, MOVE_DONE = 0xFFFF, FADE_KIND = 0x50 };

static uint16_t leaves_the_empty_routine(uint16_t called_with)
{
    (void)called_with;
    return NOTHING_EACH_PASS_LEFT;
}

static uint16_t leaves_the_move_done(uint16_t called_with)
{
    (void)called_with;
    return MOVE_DONE;
}

static uint16_t leaves_the_colors_count(uint16_t called_with)
{
    (void)called_with;
    return STEPPED_COLORS;
}

static uint16_t leaves_a_as_called(uint16_t called_with) { return called_with; }
static uint16_t leaves_the_fade_kind(uint16_t called_with) { return (uint16_t)((called_with & 0xFF00) | FADE_KIND); }

static void leaves_y_past_the_colors(Snes *s) { s->cpu.y = (uint16_t)(WORKING_PALETTE_PAGE + GROUND_COLORS_MOVED); }
static void leaves_the_place_down(Snes *s)
{
    s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00) | scene_ending_captions_y(state_of(s)));
}

const SceneRoutineLeaves scene_ending_routine_leaves[] = {
    [SCENE_ROW_STOP_THE_PICTURE] = { leaves_the_empty_routine, NULL },
    [SCENE_ROW_STEP_TOWARD_THE_FIRST] = { leaves_the_colors_count, NULL },
    [SCENE_ROW_STEP_TOWARD_THE_SECOND] = { leaves_the_colors_count, NULL },
    [SCENE_ROW_STEP_TOWARD_THE_THIRD] = { leaves_the_colors_count, NULL },
    [SCENE_ROW_SHOW_THE_FIRST_LATER_COLORS] = { leaves_the_move_done, leaves_y_past_the_colors },
    [SCENE_ROW_PLACE_THE_CAPTIONS] = { leaves_a_as_called, leaves_the_place_down },
    [SCENE_ROW_FADE_THE_SPRITES_OUT] = { leaves_a_as_called, NULL },
    [SCENE_ROW_FADE_THE_FIRST_LAYER_COLORS_OUT] = { leaves_the_fade_kind, NULL },
    [SCENE_ROW_FADE_MORE_LAYER_COLORS_OUT] = { leaves_the_fade_kind, NULL },
};
#endif

/* Their entries, for a caller that reaches one by its address. */
void scene_stop_the_picture(Snes *s) { scene_script_routine_alone(s, &scene_ending_routines[SCENE_ROW_STOP_THE_PICTURE]); }
void scene_step_the_ground_toward_the_first(Snes *s)
{
    scene_script_routine_alone(s, &scene_ending_routines[SCENE_ROW_STEP_TOWARD_THE_FIRST]);
}
void scene_step_the_ground_toward_the_second(Snes *s)
{
    scene_script_routine_alone(s, &scene_ending_routines[SCENE_ROW_STEP_TOWARD_THE_SECOND]);
}
void scene_step_the_ground_toward_the_third(Snes *s)
{
    scene_script_routine_alone(s, &scene_ending_routines[SCENE_ROW_STEP_TOWARD_THE_THIRD]);
}
void scene_show_the_ground_s_first_later_colors(Snes *s)
{
    scene_script_routine_alone(s, &scene_ending_routines[SCENE_ROW_SHOW_THE_FIRST_LATER_COLORS]);
}
void scene_place_the_captions(Snes *s) { scene_script_routine_alone(s, &scene_ending_routines[SCENE_ROW_PLACE_THE_CAPTIONS]); }
void scene_fade_the_sprites_out(Snes *s) { scene_script_routine_alone(s, &scene_ending_routines[SCENE_ROW_FADE_THE_SPRITES_OUT]); }
void scene_fade_the_first_layer_colors_out(Snes *s)
{
    scene_script_routine_alone(s, &scene_ending_routines[SCENE_ROW_FADE_THE_FIRST_LAYER_COLORS_OUT]);
}
void scene_fade_more_layer_colors_out(Snes *s)
{
    scene_script_routine_alone(s, &scene_ending_routines[SCENE_ROW_FADE_MORE_LAYER_COLORS_OUT]);
}

/* Their guards: their code, which the scripts' bank reaches. */
bool scene_stop_the_picture_present(Snes *s) { return m_code_matches(s, 0x7E96FB, 0x07, 0x1FA3E0828B2A5344ull); }
bool scene_step_the_ground_s_colors_present(Snes *s) { return m_code_matches(s, 0x7E98F6, 0x23, 0x79362D7CDB4D5C3Bull); }
bool scene_show_the_ground_s_colors_present(Snes *s) { return m_code_matches(s, 0x7E9702, 0x0D, 0xE2029DA31C9079C5ull); }
bool scene_place_the_captions_present(Snes *s) { return m_code_matches(s, 0x7E9919, 0x11, 0xF58A22A17EA5E1E2ull); }
bool scene_fade_the_sprites_out_present(Snes *s) { return m_code_matches(s, 0x7E9F8A, 0x1F, 0x5C792CC10DB4FE27ull); }
bool scene_fade_the_first_layer_colors_out_present(Snes *s) { return m_code_matches(s, 0x7E9FC8, 0x22, 0x5AA36E26668E882Dull); }
bool scene_fade_more_layer_colors_out_present(Snes *s) { return m_code_matches(s, 0x7E9FEA, 0x22, 0xBE62B562A826D9FCull); }

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

/* "The End", its sprite list drawn low in the middle of the screen. */
enum { THE_END = 0xA1A1, THE_END_X = 0x3C, THE_END_Y = 0x90 };

// $7E:99FA
/* Each frame of the last scene: "The End" over it. */
static void draw_the_end(GameState *g)
{
    scene_draw_lists_at(g, THE_END_X, THE_END_Y);
    TIMED_AS(g, 0xC30D5E, scene_draw_sprite_list(g, THE_END, true));
}

// $7E:8E88
/* In vblank: BG2's scroll down, where the steering channel holds it. */
static void scroll_bg2(const GameState *g, Platform *pf)
{
    vid_set_scroll(pf, PF_AT(0x7E8E8B), VID_BG2, 0, scene_channel_register(g, STEERING_CHANNEL, CREDITS_SCROLL), VID_SCROLL_V);
}

/* ---- What the tasks take ----------------------------------------------------------------------- */

/* The states the tasks are ports for, which their entries' guards and their
 * C both check: what isn't is the machine's. */

/* The placing draws the tall picture's sixteen sprites at most. */
enum { MOST_SPRITES = 16 };

static bool room_for_the_picture(const GameState *g) { return scene_sprites_fit(g, MOST_SPRITES); }

/* The upload sends as many bytes as the scaling's source ends at, which is
 * never 0 (65536 bytes to the original) where a picture is waiting. */
static bool picture_upload_sized(const GameState *g)
{
    return !(uint8_t)scene_ending_picture_ready(g) || scene_scaler_source_limit(g) != 0;
}

static bool room_for_the_end(const GameState *g) { return scene_sprites_fit(g, scene_sprite_list_length(g, THE_END)); }

/* The rider (RIDER), a sprite list of the program's with signed offsets,
 * behind the window's left edge and level with its top. */

/* The sweep: the window's edges where it keeps them, and no corner on the
 * last line, nor the table's right side, which its drawing reads first: from
 * there the original's fill of the lines below runs round the bank into low
 * RAM; and while the left edge comes in, room for the rider's sprites. */
static bool sweep_fits(const GameState *g)
{
    bool ok = scene_ending_window_left(g) < 0x8000 && field_window_shape_right_start(g) < SCREEN_RIGHT_EDGE;
    if (scene_ending_window_left(g))
        ok = ok && scene_sprites_fit(g, scene_sprite_list_length(g, RIDER));
    SweepCorner corners[4];
    sweep_corners(g, corners);
    for (unsigned c = 0; c < 4; c++)
        ok = ok && scene_ending_window_corners(g, c) < 0x8000 && corners[c].y < SCREEN_RIGHT_EDGE;
    return ok;
}

static void draw_rider(GameState *g)
{
    scene_draw_lists_at(g, (uint16_t)(scene_ending_window_left(g) - RIDER_BEHIND_THE_EDGE),
                        (uint16_t)(scene_ending_window_corners(g, LEFT_TOP) / QUARTERS));
    TIMED_AS(g, 0x7E3A97, scene_draw_signed_sprite_list(g, RIDER));
}

// $7E:9A0C
/* A frame of the last scene's window: drawn, the rider on its edge while it
 * comes in, and moved on; over once it has swept past. */
static Sweep sweep_the_window(GameState *g, Platform *pf)
{
    draw_sweep_window(g);
    if (scene_ending_window_left(g))
        draw_rider(g);
    return move_sweep_window(g, pf);
}

/* ---- The tasks as C ---------------------------------------------------------------------------- */

static TaskEnd machine_s(GameState *g, uint32_t routine, const char *what) { return scene_task_needs_the_machine(g, routine, what); }

TaskEnd scene_ending_place_picture(GameState *g, Platform *pf, unsigned slot)
{
    (void)pf;
    (void)slot;
    if (!room_for_the_picture(g))
        return machine_s(g, 0x7E8850, "an ending's picture with no room for its sprites");
    (void)place_picture(g);
    return task_goes_on();
}

TaskEnd scene_ending_pick_picture_frame(GameState *g, Platform *pf, unsigned slot)
{
    (void)pf;
    (void)slot;
    (void)pick_picture_frame(g);
    return task_goes_on();
}

TaskEnd scene_ending_upload_picture(GameState *g, Platform *pf, unsigned slot)
{
    (void)slot;
    if (!picture_upload_sized(g))
        return machine_s(g, 0x7E89C0, "an ending's picture of no size");
    upload_picture(g, pf);
    return task_goes_on();
}

TaskEnd scene_ending_show_credits(GameState *g, Platform *pf, unsigned slot)
{
    (void)slot;
    show_credits(g, pf);
    return task_is_done();
}

TaskEnd scene_ending_show_last_scene(GameState *g, Platform *pf, unsigned slot)
{
    (void)slot;
    show_last_scene(g, pf);
    return task_is_done();
}

TaskEnd scene_send_the_credits_picture(GameState *g, Platform *pf, unsigned slot)
{
    (void)g;
    (void)slot;
    send_a_credits_piece(pf, CREDITS_PICTURE_SENT);
    return task_is_done();
}

TaskEnd scene_send_the_pattern_s_map(GameState *g, Platform *pf, unsigned slot)
{
    (void)g;
    (void)slot;
    send_a_credits_piece(pf, CREDITS_PATTERN_MAP_SENT);
    return task_is_done();
}

TaskEnd scene_send_the_pattern_s_tiles(GameState *g, Platform *pf, unsigned slot)
{
    (void)g;
    (void)slot;
    send_a_credits_piece(pf, CREDITS_PATTERN_TILES_SENT);
    return task_is_done();
}

TaskEnd scene_blank_the_map(GameState *g, Platform *pf, unsigned slot)
{
    (void)g;
    (void)slot;
    blank_the_map(pf);
    return task_is_done();
}

TaskEnd scene_ending_scroll_bg2(GameState *g, Platform *pf, unsigned slot)
{
    (void)slot;
    scroll_bg2(g, pf);
    return task_goes_on();
}

TaskEnd scene_ending_draw_the_end(GameState *g, Platform *pf, unsigned slot)
{
    (void)pf;
    (void)slot;
    if (!room_for_the_end(g))
        return machine_s(g, 0x7E99FA, "The End with no room for its sprites");
    draw_the_end(g);
    return task_goes_on();
}

TaskEnd scene_ending_sweep_window(GameState *g, Platform *pf, unsigned slot)
{
    (void)slot;
    if (!sweep_fits(g))
        return machine_s(g, 0x7E9A0C, "a last scene's window the port doesn't sweep");
    return sweep_the_window(g, pf) == SWEEP_OVER ? task_is_done() : task_goes_on();
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
        !m_code_matches(s, 0x7E36CC, 0x3C, 0x45191A42A1EEF6A8ull) || !m_code_matches(s, 0x7E8FEA, 0x24, 0xFFD32519BE41426Dull) ||
        !scene_set_the_fireworks_up_present(s))
        return false;
    vars_peek(true);
    bool fits = room_for_the_mosaic(state_of(s));
    vars_peek(false);
    return fits;
}

/* Entered from the program's mode table with 8-bit A; back on the scene's
 * page, the data bank bank 0, where the fireworks' setup leaves it (it works
 * on the program's bank and pulls the 0 it pushed under it), and the overflow
 * the per-line effects' setup leaves, clear. */
/* The setup leaves Y past the per-line effects' descriptors, where their
 * setup stopped reading, which the frame's first vblank tasks may read. */
static void leave_y_past_the_descriptors(Snes *s, uint16_t end) { s->cpu.y = end; }

bool scene_ending_set_up_scene(Snes *s)
{
    uint16_t descriptors_end = set_up_ending_with(state_of(s), m_platform(s), &on_the_machine, s);
    ORIGINAL_EFFECT(leave_y_past_the_descriptors(s, descriptors_end));
    s->cpu.db = 0x00;
    s->cpu.d = 0x0000;
    m_set_flag(s, CPU_FLAG_V, false);
    s->cpu.p |= CPU_FLAG_M;
    return true;
}

void scene_ending_set_up_entry(Snes *s)
{
    scene_ending_set_up_scene(s);
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

/* What the picture's routines leave: Y where the last copy of its colors
 * ended, and the overflow of the mirroring's last step back a run, clear. */
static void leave_picture_loaded(Snes *s, uint16_t copied_to)
{
    s->cpu.y = copied_to;
    m_set_flag(s, CPU_FLAG_V, false);
}

/* The script routines' entries: 16-bit A; back with the page and the bank
 * they kept. */
void scene_ending_load_picture(Snes *s)
{
    SceneProgramPage page = calling_page(s);
    begin_script_routine(s);
    uint16_t copied_to = load_the_picture_with(state_of(s), m_platform(s), page, &on_the_machine, s);
    ORIGINAL_EFFECT(leave_picture_loaded(s, copied_to));
    end_script_routine(s);
}

void scene_ending_load_ground_and_picture(Snes *s)
{
    SceneProgramPage page = calling_page(s);
    begin_script_routine(s);
    uint16_t copied_to = load_the_ground_and_picture_with(state_of(s), m_platform(s), page, &on_the_machine, s);
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
    start_stars_with(state_of(s), m_platform(s), &on_the_machine, s);
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
    start_mode7_picture_with(state_of(s), m_platform(s), &on_the_machine, s);
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
    bool credits = prepare_credits_with(state_of(s), m_platform(s), &on_the_machine, s);
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

/* A state half, peeked. */
static bool holds(Snes *s, bool (*takes)(const GameState *g))
{
    vars_peek(true);
    bool ok = takes(state_of(s));
    vars_peek(false);
    return ok;
}

/* The vblank tasks that set a scene's screen up once and take themselves off
 * the list. */
bool scene_ending_show_credits_present(Snes *s)
{
    return scene_on_scene_page_io_bank(s) && m_code_matches(s, 0x7E8D56, 0x9D, 0x08121CAB0ACD3DEDull) &&
           m_code_matches(s, 0x7E90A7, 0x1A, 0x841D0D5589A89675ull) && m_code_matches(s, 0x7E36CC, 0x3C, 0x45191A42A1EEF6A8ull) &&
           m_code_matches(s, 0x7E9023, 0x6, 0xF09D7328034315D8ull);
}

void scene_ending_show_credits_entry(Snes *s)
{
    show_credits(state_of(s), m_platform(s));
    m_stop_at(s, REMOVE_THIS_VBLANK_TASK, NOTHING_READ);
}

bool scene_ending_show_last_scene_present(Snes *s)
{
    return m_bank_reaches_io(s) && m_code_matches(s, 0x7E8E44, 0x44, 0x670F389BEFF44DF6ull) &&
           m_code_matches(s, 0x7E36CC, 0x3C, 0x45191A42A1EEF6A8ull) && m_code_matches(s, 0x7E9029, 0x6, 0x31D9FBABBC5732E4ull);
}

void scene_ending_show_last_scene_entry(Snes *s)
{
    show_last_scene(state_of(s), m_platform(s));
    m_stop_at(s, REMOVE_THIS_VBLANK_TASK, NOTHING_READ);
}

/* The credits' pieces' vblank tasks: their code, the send they share, and the
 * list's exit they jump to, reaching the registers through the data bank.
 * Entered with 8-bit A; nothing they leave is read. */
static bool credits_pieces_present(Snes *s)
{
    return m_bank_reaches_io(s) && m_code_matches(s, 0x7E9F2B, 0x2E, 0x8E5DE0BE1BA03B47ull) &&
           m_code_matches(s, 0x7E9F59, 0x1A, 0x573296F067675F24ull) && m_code_matches(s, 0x7E39DE, 0x04, 0x37224D8D813A8AFCull);
}

bool scene_send_the_credits_picture_present(Snes *s) { return credits_pieces_present(s); }
bool scene_send_the_pattern_s_map_present(Snes *s) { return credits_pieces_present(s); }
bool scene_send_the_pattern_s_tiles_present(Snes *s) { return credits_pieces_present(s); }

bool scene_blank_the_map_present(Snes *s)
{
    return m_bank_reaches_io(s) && m_code_matches(s, 0x7E9F73, 0x17, 0xFCE619F5404A5D5Aull) &&
           m_code_matches(s, 0x7E90AA, 0x17, 0x51D21C0093401867ull) && m_code_matches(s, 0x7E39DE, 0x04, 0x37224D8D813A8AFCull);
}

static void credits_piece_on_the_machine(Snes *s, unsigned piece)
{
    send_a_credits_piece(m_platform(s), piece);
    m_stop_at(s, REMOVE_THIS_VBLANK_TASK, NOTHING_READ);
}

void scene_send_the_credits_picture_entry(Snes *s) { credits_piece_on_the_machine(s, CREDITS_PICTURE_SENT); }
void scene_send_the_pattern_s_map_entry(Snes *s) { credits_piece_on_the_machine(s, CREDITS_PATTERN_MAP_SENT); }
void scene_send_the_pattern_s_tiles_entry(Snes *s) { credits_piece_on_the_machine(s, CREDITS_PATTERN_TILES_SENT); }

void scene_blank_the_map_entry(Snes *s)
{
    blank_the_map(m_platform(s));
    m_stop_at(s, REMOVE_THIS_VBLANK_TASK, NOTHING_READ);
}

bool scene_ending_scroll_bg2_present(Snes *s)
{
    return m_bank_reaches_io(s) && m_code_matches(s, 0x7E8E88, 0xD, 0x3B94F261543D6F7Aull);
}

/* A vblank task; nothing it leaves is read. */
void scene_ending_scroll_bg2_entry(Snes *s)
{
    scroll_bg2(state_of(s), m_platform(s));
    m_rts(s);
}

/* The window's task: the scene's page, a data bank on the registers (the
 * rider's sprite list streams through the work RAM port), and the sweep the
 * port makes. */
bool scene_ending_sweep_window_present(Snes *s)
{
    return scene_on_scene_page_io_bank(s) && m_code_matches(s, 0x7E9A0C, 0xB1, 0x0B37DD86CDAEC523ull) && holds(s, sweep_fits);
}

/* The list's exit for a task that takes itself off. */
enum { REMOVE_SWEEP_TASK = 0x7E39A7 };

/* A frame task, entered with 16-bit A. Back, A's high byte is the right
 * side's top corner's (the last the original loaded), or 0 where the sweep
 * is over, and Y where the rider's list or the window's drawing left it. */
static void leave_the_sweep_s_y(Snes *s, const GameState *g, bool rider_drawn)
{
    s->cpu.y = rider_drawn ? scene_sprite_index(g) : field_window_shape_leaves_y(g);
}

void scene_ending_sweep_window_entry(Snes *s)
{
    GameState *g = state_of(s);
    bool rider_drawn = scene_ending_window_left(g) != 0;
    Sweep sweep = sweep_the_window(g, m_platform(s));
    ORIGINAL_EFFECT(leave_the_sweep_s_y(s, g, rider_drawn));
    if (sweep == SWEEP_OVER) {
        ORIGINAL_EFFECT(s->cpu.a = 0);
        m_stop_at(s, REMOVE_SWEEP_TASK, DC_AL | DC_X | DC_N | DC_V | DC_Z | DC_C);
        return;
    }
    ORIGINAL_EFFECT(s->cpu.a = scene_ending_window_corners(g, RIGHT_TOP));
    s->cpu.p |= CPU_FLAG_M;
    m_rts(s);
}

bool scene_ending_upload_picture_present(Snes *s)
{
    return scene_on_scene_page_io_bank(s) && m_code_matches(s, 0x7E89C0, 0x2D, 0x30B40D41846E3FB4ull) &&
           holds(s, picture_upload_sized);
}

/* A vblank task; nothing it leaves is read. */
void scene_ending_upload_picture_entry(Snes *s)
{
    upload_picture(state_of(s), m_platform(s));
    m_rts(s);
}

bool scene_ending_place_picture_present(Snes *s)
{
    return scene_on_scene_page_io_bank(s) && m_code_matches(s, 0x7E8850, 0x69, 0x0371646720951031ull) &&
           holds(s, room_for_the_picture);
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
void scene_ending_place_picture_entry(Snes *s)
{
    uint16_t list = place_picture(state_of(s));
    ORIGINAL_EFFECT(leave_picture_placed(s, list));
    s->cpu.p &= (uint8_t)~CPU_FLAG_M;
    m_rts(s);
}

/* "The End": the scene's page, a data bank on the registers (the list streams
 * through the work RAM port), room for its sprites from the cursor. */
bool scene_ending_draw_the_end_present(Snes *s)
{
    return scene_on_scene_page_io_bank(s) && m_code_matches(s, 0x7E99FA, 0x12, 0x6A62CB76AC02881Eull) &&
           holds(s, room_for_the_end);
}

/* A frame task; back, Y is the next free entry and A as the clipped list
 * leaves it. */
void scene_ending_draw_the_end_entry(Snes *s)
{
    GameState *g = state_of(s);
    draw_the_end(g);
    s->cpu.y = scene_sprite_index(g);
    ORIGINAL_EFFECT(s->cpu.a = scene_clipped_list_leaves_a(g, THE_END, THE_END));
    m_rts(s);
}

bool scene_ending_pick_picture_frame_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E891B, 0x14, 0x7AF538C4E8F9D1D5ull);
}

/* A frame task, entered with 16-bit A and the carry clear (the registry's
 * condition; the list's own compare, for any slot but the first). Back, A is
 * the frame's place, which the lists' code hands on. */
void scene_ending_pick_picture_frame_entry(Snes *s)
{
    uint16_t source = pick_picture_frame(state_of(s));
    ORIGINAL_EFFECT(s->cpu.a = source);
    m_rts(s);
}

/* The scaling reads the picture's shape as a word, the gathering and placing
 * its low byte, which agree while the byte after it is 0, as every ending
 * leaves it. A picture handed to the scaler runs the program's divide inline. */
static bool picture_waiting(const GameState *g) { return scene_ending_picture_ready(g) != 0; }
static bool shape_word_agrees(const GameState *g) { return (scene_ending_picture_tall(g) >> 8) == 0; }

bool scene_ending_scale_picture_present(Snes *s)
{
    if (!scene_on_scene_page(s) || !m_bank_reaches_low_ram(s) || !m_code_matches(s, 0x7E8930, 0x6, 0xB318CAF0720BDB26ull))
        return false;
    return holds(s, picture_waiting) || (holds(s, shape_word_agrees) && m_code_matches(s, 0x7E8936, 0x4D, 0x65F6F8DD17744FCCull) &&
                                         m_code_matches(s, 0x7E32E2, 0xBD, 0xD04B8C1274AF93C8ull));
}

/* As C, the main loop's call each pass: unless the last picture still waits
 * to be sent, the animation's frame scaled whole at the zoom channel 1 asks
 * for, its first row nudged by nothing (a zoom under $8000 leaves the carry
 * clear), and gathered. False where the program goes no further. */
enum { SCALE_PICTURE = 0x7E8930, GATHER_PICTURE = 0x7E8983 };

bool scene_ending_scale_picture(GameState *g, Platform *pf)
{
    (void)pf;
    if (!picture_waiting(g) && !shape_word_agrees(g)) {
        program_needs_the_machine(g, SCALE_PICTURE, "an ending's picture whose shape the port doesn't take");
        return false;
    }
    if (!hand_picture_to_scaler(g))
        return true;
    bool drawn = false;
    TIMED_AS(g, SCENE_SCALER_ROWS, drawn = scene_scale_the_bitmap(g, false));
    if (!drawn)
        return false;
    TIMED_AS(g, GATHER_PICTURE, gather_picture(g));
    return true;
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
void scene_ending_scale_picture_entry(Snes *s)
{
    GameState *g = state_of(s);
    if (!hand_picture_to_scaler(g)) {
        m_rts(s);
        return;
    }
    ORIGINAL_EFFECT(leave_dividend(s));
    scene_bridge_scale_scene_picture(s, SCALING_THE_ENDING_PICTURE);
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
