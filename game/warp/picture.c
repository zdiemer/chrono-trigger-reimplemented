/* The time warp's picture, put together while the screen is dark as the warp
 * starts.
 *
 * The tunnel is a Mode 7 plane seen in perspective: its tiles are solid
 * colours, tile n all colour n, and its map a gradient across each row that a
 * sine bends (warp_plane_map), so the plane is bands of colour that ripple. The
 * matrix down the screen scales it least at the top and bottom of the picture,
 * where the plane is near, and most at its middle, where it runs off into the
 * distance; the two halves of the picture show it from either side, as the
 * inside of a tube. Over it, the
 * light at the tunnel's end is sixteen large sprites showing one quarter of a
 * drawing four ways round, a quarter disc of rays from its corner in bands of
 * blue, blended half into the tunnel.
 *
 * warp_draw_picture sets the screen up and draws all of that; the circle's
 * window tables come next (warp/circle.c), and warp_start_line_effects then
 * points the plane's line effects and the circle's at their tables. The video
 * goes through the video service; the work RAM the original streams through the
 * CPU's port is written through its variables, the port's cursor left where the
 * original leaves it as scaffolding. */
#include "warp/warp.h"
#include "warp/warp_data.h"

#include "cpu_io.h"
#include "original_effects.h"
#include "platform/input.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "ram.h"
#include "vars.h"

/* ---- The screen ---------------------------------------------------------------------------------- */

/* Mode 7 for the tunnel, on the main screen alone and masked by the windows
 * there; the light's sprites on the sub screen, 16x16 and 32x32 with their
 * tiles at VRAM word $4000, added to the tunnel at half strength where the
 * colour window doesn't stop it. Window 1 hides the tunnel outside its edges
 * (the circle, as it opens), window 2 spans the screen; neither is set to
 * anything yet but the screen's width. */
enum { LIGHT_TILES_VRAM = 0x4000, UNUSED_TILEMAP_VRAM = 0x4800, SCREEN_RIGHT = 0xFF };

static const VidScreen warp_screen = {
    .sprites = { .size_select = 4, .base = LIGHT_TILES_VRAM },
    .mode = { .mode = 7 },
    .mosaic_size = 1,
    .bg = { { VID_TILEMAP(UNUSED_TILEMAP_VRAM, 32, 32) } },
    .windows = { .w1 = { [VID_BG1] = VID_WIN_OUT }, .w1_right = SCREEN_RIGHT, .w2_right = SCREEN_RIGHT,
                 .win_main = VID_BG1_BIT, .win_sub = VID_OBJ_BIT },
    .main_layers = VID_BG1_BIT,
    .sub_layers = VID_OBJ_BIT,
    .math = { .prevent_math = VID_REGION_INSIDE, .sub_screen = true, .half = true, .layers = VID_BG1_BIT },
};

/* The controller port's programmable lines, all high. */
enum { IO_LINES_RELEASED = 0xFF };

static void set_the_screen_up(Platform *pf)
{
    vid_apply_screen(pf, PF_AT(0xFDEB13), &warp_screen);
    in_set_io_lines(pf, PF_AT(0xFDEB7E), IO_LINES_RELEASED);
    tm_reset_timers(pf, PF_AT(0xFDEB81));
}

/* ---- The plane ----------------------------------------------------------------------------------- */

/* Mode 7's 256 tiles, each one colour: its own number. Mode 7 keeps a tile's
 * pixels in the high bytes of video memory, a byte a pixel, and the original
 * writes them a byte at a time. */
enum { PLANE_TILES = 256, TILE_PIXELS = 64 };

static void draw_the_plane_tiles(Platform *pf)
{
    vid_start_vram_uploads(pf, PF_AT(0xFDDBD9), &(const VidVramPort){ .step = 1, .after_high = true });
    vid_set_vram_address(pf, PF_AT(0xFDDBDF), 0);
    for (unsigned tile = 0; tile < PLANE_TILES; tile++)
        for (unsigned pixel = 0; pixel < TILE_PIXELS; pixel++)
            vid_write_vram_byte(pf, PF_AT(0xFDDBE6), true, (uint8_t)tile);
}

/* The plane's map: 128 rows of 128 tiles. Each row runs through the first 128
 * colours in order from a place the sine picks, the rows a quarter turn of it
 * apart in 64 rows, and the second 64 rows the same as the first. */
enum { PLANE_SIDE = 128, PLANE_COLOURS = 128, BEND_ROWS = 64, BEND_TURN = 4 };

static uint8_t row_start(const GameState *g, unsigned row)
{
    uint8_t angle = (uint8_t)(row % BEND_ROWS * BEND_TURN);
    return (uint8_t)((uint8_t)(warp_sine(g, angle) + 128) / 2);
}

static void draw_the_plane_map(GameState *g, Platform *pf)
{
    for (unsigned row = 0; row < PLANE_SIDE; row++) {
        uint8_t start = row_start(g, row);
        for (unsigned column = 0; column < PLANE_SIDE; column++)
            set_warp_plane_map(g, row, column, (uint8_t)((start + column) % PLANE_COLOURS));
    }
    /* the map is Mode 7's: the low bytes of video memory from its start */
    vid_send_vram_half(pf, PF_AT(0xFDF1C1), 0, PF_GAME(RAM_WARP_PLANE_MAP), PLANE_SIDE * PLANE_SIDE,
                       VID_VRAM_LOW_BYTES, &(const VidVramPort){ .step = 1, .after_high = false });
}

/* ---- The palette --------------------------------------------------------------------------------- */

/* The tunnel's colours, the plane's first 64 (red and blue ramps), and the
 * light's: every sprite palette the same blue shades. The rest are black. */
enum { COLOURS = 256, FIRST_SPRITE_COLOUR = 128, BLUE_SHIFT = 10 };

static void mix_the_palette(GameState *g)
{
    for (unsigned colour = 0; colour < COLOURS; colour++)
        set_palette_base(g, colour, 0);
    for (unsigned colour = 0; colour < WARP_TUNNEL_COLOURS; colour++)
        set_palette_base(g, colour,
                         (uint16_t)(warp_tunnel_red(g, colour) | warp_tunnel_blue(g, colour) << BLUE_SHIFT));
    for (unsigned colour = FIRST_SPRITE_COLOUR; colour < COLOURS; colour++)
        set_palette_base(g, colour, (uint16_t)(warp_light_blue(g, colour % WARP_LIGHT_SHADES) << BLUE_SHIFT));
}

/* ---- The light's sprites ------------------------------------------------------------------------- */

/* Every sprite hidden below the screen, small and pushed past its right edge;
 * then the light's sixteen, large. The high table keeps two bits a sprite, X's
 * ninth and the large size, four sprites a byte. */
enum { SPRITES = 128, HIDDEN_Y = 0xF0, HIGH_TABLE_BYTES = 0x20, SPRITES_A_HIGH_BYTE = 4 };
enum { ALL_SMALL_PAST_THE_EDGE = 0x55, ALL_LARGE_ON_SCREEN = 0xAA };

static void place_the_sprites(GameState *g)
{
    for (unsigned i = 0; i < SPRITES; i++)
        set_oam_buffer_y(g, i, HIDDEN_Y);
    for (unsigned at = 0; at < HIGH_TABLE_BYTES; at++)
        set_oam_high_table_byte(g, at, ALL_SMALL_PAST_THE_EDGE);
    for (unsigned i = 0; i < WARP_LIGHT_SPRITES; i++) {
        WarpSprite sprite = warp_light_sprite(g, i);
        set_oam_buffer_x(g, i, sprite.x);
        set_oam_buffer_y(g, i, sprite.y);
        set_oam_buffer_tile(g, i, sprite.tile);
        set_oam_buffer_attributes(g, i, sprite.attributes);
    }
    for (unsigned at = 0; at < WARP_LIGHT_SPRITES / SPRITES_A_HIGH_BYTE; at++)
        set_oam_high_table_byte(g, at, ALL_LARGE_ON_SCREEN);
}

/* ---- The light ----------------------------------------------------------------------------------- */

/* The light is drawn into 256 sprite tiles, 16 a row, 128 pixels square, in
 * the sprites' four-bitplane form: each tile's rows hold bitplanes 0 and 1 a
 * byte each, then, 16 bytes on, bitplanes 2 and 3. */
enum { LIGHT_SIDE = 128, TILE_SIDE = 8, TILE_BYTES = 32, TILES_ACROSS = 16, ROW_BYTES = 2, UPPER_PLANES = 16 };
enum { LIGHT_BYTES = TILE_BYTES * TILES_ACROSS * TILES_ACROSS, BITPLANES = 4 };

static uint16_t pixel_row_start(uint8_t y)
{
    return (uint16_t)(y / TILE_SIDE * TILES_ACROSS * TILE_BYTES + y % TILE_SIDE * ROW_BYTES);
}

static void set_light_pixel(GameState *g, uint8_t x, uint8_t y, uint8_t colour)
{
    static const uint8_t plane_byte[BITPLANES] = { 0, 1, UPPER_PLANES, UPPER_PLANES + 1 };
    uint16_t at = (uint16_t)(pixel_row_start(y) + x / TILE_SIDE * TILE_BYTES);
    uint8_t bit = (uint8_t)(0x80 >> x % TILE_SIDE);
    for (unsigned plane = 0; plane < BITPLANES; plane++) {
        uint8_t bits = warp_light_tiles_byte(g, (unsigned)(at + plane_byte[plane]));
        bits = (colour >> plane & 1) ? (uint8_t)(bits | bit) : (uint8_t)(bits & ~bit);
        set_warp_light_tiles_byte(g, (unsigned)(at + plane_byte[plane]), bits);
    }
}

/* The bit masks the original draws the light with, a pixel's bit in its row
 * set and cleared. The drawing here works them out, but the masks stay in work
 * RAM after the warp, where the field's palette animations, which share the
 * bytes (palette_animations), read some of them on the location after: so they
 * are the game's to write. */
enum { PIXELS_A_ROW = 8 };

static void set_the_pixel_masks(GameState *g)
{
    for (unsigned i = 0; i < PIXELS_A_ROW; i++) {
        set_warp_pixel_set_masks(g, i, (uint8_t)(0x80 >> i));
        set_warp_pixel_clear_masks(g, i, (uint8_t)~(0x80 >> i));
    }
}

/* What the original leaves of its drawing: the last ray's steps and place and
 * the last pixel's row. */
enum { WORK_RAY_STEP_ACROSS = 6, WORK_RAY_STEP_DOWN = 8, WORK_RAY_ACROSS = 10, WORK_RAY_DOWN = 12, WORK_PIXEL_ROW = 14 };

typedef struct {
    uint16_t step_across, step_down, across, down;
    uint8_t last_y;
} Ray;

static void leave_the_last_ray(GameState *g, const Ray *r)
{
    warp_leave_work_word(g, WORK_RAY_STEP_ACROSS, r->step_across);
    warp_leave_work_word(g, WORK_RAY_STEP_DOWN, r->step_down);
    warp_leave_work_word(g, WORK_RAY_ACROSS, r->across);
    warp_leave_work_word(g, WORK_RAY_DOWN, r->down);
    warp_leave_work_word(g, WORK_PIXEL_ROW, pixel_row_start(r->last_y));
}

/* A quarter turn of rays from the top left corner, each drawn a pixel at a
 * time until it leaves the quarter, in bands of colour that widen as they go
 * out: a band's colour runs from 15 down to 1 and round again, never the
 * clear colour 0. The rays and the band's widening take their steps from the
 * sine, in 256ths of a pixel. */
enum { QUARTER_TURN = 64, BAND_RIPPLE = 16, BAND_SHADES = 16, ONE = 0x100 };

static Ray draw_a_ray(GameState *g, uint8_t angle)
{
    Ray r = { .step_across = (uint8_t)warp_sine(g, angle), .step_down = (uint8_t)warp_sine(g, (uint8_t)(angle + QUARTER_TURN)) };
    uint8_t ripple = (uint8_t)((angle + QUARTER_TURN) * BAND_RIPPLE);
    uint16_t widening = (uint16_t)(ONE + (warp_sine(g, ripple) + 128) / 8);
    uint16_t band = ONE;
    for (;;) {
        uint8_t x = (uint8_t)(r.across >> 8), y = (uint8_t)(r.down >> 8);
        set_light_pixel(g, x, y, (uint8_t)(BAND_SHADES - (band >> 8) % BAND_SHADES) % BAND_SHADES);
        r.last_y = y;
        band = (uint16_t)(band + widening);
        widening--;
        if ((band >> 8) % BAND_SHADES == 0)
            band = (uint16_t)(band + ONE);
        r.across = (uint16_t)(r.across + r.step_across);
        if (r.across >> 8 >= LIGHT_SIDE)
            break;
        r.down = (uint16_t)(r.down + r.step_down);
        if (r.down >> 8 >= LIGHT_SIDE)
            break;
    }
    return r;
}

static void draw_the_light(GameState *g, Platform *pf)
{
    for (unsigned at = 0; at < LIGHT_BYTES; at++)
        set_warp_light_tiles_byte(g, at, 0);
    Ray last = { 0 };
    for (unsigned angle = 0; angle <= QUARTER_TURN; angle++)
        last = draw_a_ray(g, (uint8_t)angle);
    ORIGINAL_EFFECT(leave_the_last_ray(g, &last));
    vid_send_vram(pf, PF_AT(0xFDEC91), LIGHT_TILES_VRAM, PF_GAME(RAM_WARP_LIGHT_TILES), LIGHT_BYTES,
                  VID_VRAM_WORD_PORT);
}

void warp_draw_picture(GameState *g, Platform *pf)
{
    set_the_screen_up(pf);
    set_the_pixel_masks(g);
    draw_the_plane_tiles(pf);
    draw_the_plane_map(g, pf);
    mix_the_palette(g);
    place_the_sprites(g);
    draw_the_light(g, pf);
}

/* ---- The line effects ---------------------------------------------------------------------------- */

/* The plane's scale on a line of the tables, which run 256 lines with the
 * picture's 224 in the middle: smallest at the tables' ends, largest where the
 * two halves meet, as a perspective's depth (a reciprocal of the distance from
 * the middle) makes it. */
enum { FAR_DISTANCE = 0xF0, NEAR_TILT = 0xE0, UNIT_DEPTH = 0xFFFF, SCALE_TIMES = 255, SCALE_SHIFT = 6 };
enum { TABLE_LINES = 256, TABLE_MIDDLE = 128 };

typedef struct {
    uint8_t tilt;
    uint8_t distance;
    uint16_t depth;
    uint32_t product;
} Scale;

static Scale scale_on(uint8_t line)
{
    uint8_t from_middle = line < TABLE_MIDDLE ? line : (uint8_t)~line;
    Scale sc = { .tilt = (uint8_t)(from_middle * 2 * NEAR_TILT >> 8) };
    sc.distance = (uint8_t)(FAR_DISTANCE - sc.tilt);
    sc.depth = (uint16_t)(UNIT_DEPTH / sc.distance);
    sc.product = (uint32_t)sc.depth * SCALE_TIMES;
    return sc;
}

static uint16_t scale_of(const Scale *sc) { return (uint16_t)(sc->product >> SCALE_SHIFT); }

/* What the original leaves of the scales: the last line's working out, in the
 * work cells, the multiplier (the depth's two bytes multiplied in turn) and the
 * divider. */
enum { WORK_SCALE_TILT = 0, WORK_SCALE_PRODUCT = 2, WORK_SCALE_PRODUCT_HIGH = 4, WORK_SCALE_DEPTH = 6 };

static void leave_the_last_scale(GameState *g, const Scale *sc)
{
    warp_leave_work_byte(g, WORK_SCALE_TILT, sc->tilt);
    warp_leave_work_word(g, WORK_SCALE_PRODUCT, scale_of(sc));
    warp_leave_work_word(g, WORK_SCALE_PRODUCT_HIGH, (uint16_t)(sc->product >> (16 + SCALE_SHIFT)));
    warp_leave_work_word(g, WORK_SCALE_DEPTH, sc->depth);
    hw_set_dividend(g, UNIT_DEPTH);
    hw_divide_by(g, sc->distance);
    hw_set_multiplicand(g, SCALE_TIMES);
    (void)hw_multiply_by(g, (uint8_t)(sc->depth >> 8));
    hw_port_seek(g, RAM_ABS(RAM_WARP_PLANE_MATRIX_SECOND_ROW + TABLE_LINES * RAM_WARP_PLANE_MATRIX_SECOND_ROW_STRIDE),
                 RAM_WARP_PLANE_MATRIX_SECOND_ROW >> 16);
}

static void scale_the_plane(GameState *g)
{
    Scale sc = { 0 };
    for (unsigned line = 0; line < TABLE_LINES; line++) {
        sc = scale_on((uint8_t)line);
        set_warp_plane_matrix_first_row_scale(g, line, scale_of(&sc));
        set_warp_plane_matrix_first_row_turn(g, line, 0);
        set_warp_plane_matrix_second_row_turn(g, line, 0);
        set_warp_plane_matrix_second_row_scale(g, line, scale_of(&sc));
    }
    ORIGINAL_EFFECT(leave_the_last_scale(g, &sc));
}

/* A line effect's list: the picture's top half from one place in its tables,
 * the bottom half from another, then the end. The plane's matrix and the
 * circle's window change down the screen, so their halves run 112 lines of a
 * table each; the plane's place is one value a half. */
enum { HALF_LINES = 112, LINE_BY_LINE = 0x80, FIRST_LINE_SHOWN = 16, LIST_END = 0 };

typedef struct {
    uint8_t top_lines;
    uint16_t top;
    uint8_t bottom_lines;
    uint16_t bottom;
} WarpList;

static WarpList list_by_line(uint32_t table, uint32_t line_bytes)
{
    return (WarpList){ LINE_BY_LINE | HALF_LINES, RAM_ABS(table + FIRST_LINE_SHOWN * line_bytes),
                       LINE_BY_LINE | HALF_LINES, RAM_ABS(table + TABLE_MIDDLE * line_bytes) };
}

static WarpList list_by_half(uint32_t top, uint32_t bottom)
{
    return (WarpList){ HALF_LINES, RAM_ABS(top), HALF_LINES, RAM_ABS(bottom) };
}

static void write_plane_list(GameState *g, WarpPlaneEffect effect, WarpList list)
{
    set_warp_plane_lists_top_lines(g, effect, list.top_lines);
    set_warp_plane_lists_top_values(g, effect, list.top);
    set_warp_plane_lists_bottom_lines(g, effect, list.bottom_lines);
    set_warp_plane_lists_bottom_values(g, effect, list.bottom);
    set_warp_plane_lists_end(g, effect, LIST_END);
}

/* The slots the warp's line effects run in, from 1. */
enum { MATRIX_FIRST_ROW_SLOT = 1, MATRIX_SECOND_ROW_SLOT, CENTRE_SLOT, SCROLL_SLOT, CIRCLE_SLOT };

static void start_line_effect(Platform *pf, uint32_t site, uint8_t slot, VidLineTarget target, uint32_t list)
{
    const VidLineEffect effect = { .target = target, .table = PF_GAME(list), .indirect = true,
                                   .indirect_bank = (uint8_t)(list >> 16) };
    vid_set_line_slot(pf, PF_AT(site), slot, &effect);
}

static uint32_t plane_list(WarpPlaneEffect effect)
{
    return RAM_WARP_PLANE_LISTS + (uint32_t)effect * RAM_WARP_PLANE_LISTS_STRIDE;
}

void warp_start_line_effects(GameState *g, Platform *pf)
{
    start_line_effect(pf, 0xFDED8B, MATRIX_FIRST_ROW_SLOT, VID_LINE_MODE7_AB, plane_list(WARP_PLANE_FIRST_ROW));
    start_line_effect(pf, 0xFDEDA3, MATRIX_SECOND_ROW_SLOT, VID_LINE_MODE7_CD, plane_list(WARP_PLANE_SECOND_ROW));
    start_line_effect(pf, 0xFDEDBB, CENTRE_SLOT, VID_LINE_MODE7_CENTER, plane_list(WARP_PLANE_CENTRE));
    start_line_effect(pf, 0xFDEDD3, SCROLL_SLOT, VID_LINE_SCROLL_BG1, plane_list(WARP_PLANE_SCROLL));
    start_line_effect(pf, 0xFDEDEB, CIRCLE_SLOT, VID_LINE_WINDOW1, RAM_WARP_CIRCLE_WINDOW_LIST);

    write_plane_list(g, WARP_PLANE_FIRST_ROW, list_by_line(RAM_WARP_PLANE_MATRIX_FIRST_ROW, RAM_WARP_PLANE_MATRIX_FIRST_ROW_STRIDE));
    write_plane_list(g, WARP_PLANE_SECOND_ROW, list_by_line(RAM_WARP_PLANE_MATRIX_SECOND_ROW, RAM_WARP_PLANE_MATRIX_SECOND_ROW_STRIDE));
    write_plane_list(g, WARP_PLANE_CENTRE,
                     list_by_half(RAM_WARP_PLANE_CENTRES, RAM_WARP_PLANE_CENTRES + RAM_WARP_PLANE_CENTRES_STRIDE));
    write_plane_list(g, WARP_PLANE_SCROLL,
                     list_by_half(RAM_WARP_PLANE_SCROLLS, RAM_WARP_PLANE_SCROLLS + RAM_WARP_PLANE_SCROLLS_STRIDE));
    warp_show_circle_table(g, 0);
    set_warp_circle_window_list_top_lines(g, LINE_BY_LINE | HALF_LINES);
    set_warp_circle_window_list_bottom_lines(g, LINE_BY_LINE | HALF_LINES);
    set_warp_circle_window_list_end(g, LIST_END);
    scale_the_plane(g);
}

/* The circle window's list pointed at one of its two tables. */
void warp_show_circle_table(GameState *g, unsigned table)
{
    WarpList list = list_by_line(RAM_WARP_CIRCLE_WINDOWS + (uint32_t)table * WARP_CIRCLE_TABLE_LINES * RAM_WARP_CIRCLE_WINDOWS_STRIDE,
                                 RAM_WARP_CIRCLE_WINDOWS_STRIDE);
    set_warp_circle_window_list_top_values(g, list.top);
    set_warp_circle_window_list_bottom_values(g, list.bottom);
}
