/* The service-built picture keeps the memory each line saw, including
 * changes to sprites and tile data while later lines are still being built. */
#include <stdio.h>
#include <stdlib.h>

#include "platform/native/host.h"
#include "platform/native/render.h"
#include "platform/observe.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static unsigned fallback_reads;
static uint8_t unexpected_read(void *ctx, uint32_t at)
{
    (void)ctx; (void)at;
    fallback_reads++;
    return 0xEE;
}
static const uint8_t *split_bytes(uint32_t at)
{
    static const uint8_t end = 0x35, start = 0x71;
    return (uint16_t)at == 0xFFFF ? &end : &start;
}

static void resource_transfers(void)
{
    Platform pf = {0};
    const uint8_t tiles[] = { 0x12, 0x34, 0x56, 0x78 };
    vid_host_start(&pf, unexpected_read, NULL);
    vid_send_vram(&pf, PF_AT(0), 0, (PfData){ .addr = 0xC01000, .ptr = tiles }, 4,
                  &(VidVramPort){ .step = 1, .after_high = true });
    vid_send_vram(&pf, PF_AT(0), 2, (PfData){ .addr = 0xC0FFFF, .byte_at = split_bytes }, 2, NULL);
    vid_send_vram_fill(&pf, PF_AT(0), 3, PF_VALUE(0xA5), 3, NULL);
    const VidFrame *f = vid_host_picture(&pf);
    CHECK(f->vram[0] == 0x12 && f->vram[3] == 0x78);
    CHECK(f->vram[4] == 0x35 && f->vram[5] == 0x71);
    CHECK(f->vram[6] == 0xA5 && f->vram[7] == 0xA5 && f->vram[8] == 0xA5);
    /* What was sent reads back as words, at their plain addresses. */
    CHECK(vid_vram_word(&pf, PF_AT(0), 0) == 0x3412 && vid_vram_word(&pf, PF_AT(0), 1) == 0x7856);
    CHECK(vid_vram_word(&pf, PF_AT(0), 2) == 0x7135 && vid_vram_word(&pf, PF_AT(0), 0x8000) == 0x3412);
    CHECK(fallback_reads == 0);
    vid_host_stop(&pf);
}

/* A wider view's own columns are shown but never read back, and leave the
 * game's port and address as they were: the game finds VRAM as at the
 * original's view. */
static void view_sends(void)
{
    Platform pf = {0};
    const uint8_t game[] = { 0x11, 0x22, 0x33, 0x44 }, margin[] = { 0xAA, 0xBB, 0xCC, 0xDD }, more[] = { 0x55, 0x66 };
    vid_host_start(&pf, unexpected_read, NULL);
    vid_send_vram(&pf, PF_AT(0), 0x100, (PfData){ .ptr = game }, sizeof game, &(VidVramPort){ .step = 1, .after_high = true });
    vid_send_view_vram(&pf, 0x100, (PfData){ .ptr = margin }, sizeof margin, &(VidVramPort){ .step = 32, .after_high = true });
    const VidFrame *f = vid_host_picture(&pf);
    CHECK(f->vram[0x200] == 0xAA && f->vram[0x201] == 0xBB && f->vram[0x240] == 0xCC);
    CHECK(vid_vram_word(&pf, PF_AT(0), 0x100) == 0x2211 && vid_vram_word(&pf, PF_AT(0), 0x101) == 0x4433);
    CHECK(vid_vram_word(&pf, PF_AT(0), 0x120) == 0);
    /* The game's next send goes on from its own address, a word at a time. */
    vid_continue_vram(&pf, PF_AT(0), (PfData){ .ptr = more }, sizeof more);
    CHECK(vid_vram_word(&pf, PF_AT(0), 0x102) == 0x6655 && f->vram[0x204] == 0x55);
    /* A game send over a view's column is the game's again. */
    vid_send_vram(&pf, PF_AT(0), 0x100, (PfData){ .ptr = more }, sizeof more, NULL);
    CHECK(vid_vram_word(&pf, PF_AT(0), 0x100) == 0x6655 && f->vram[0x200] == 0x55);
    vid_host_stop(&pf);
}

static uint8_t effect_byte(void *ctx, uint32_t at)
{
    (void)ctx;
    return at == 0 ? 255 : at <= 127 ? 15 : 0;
}

static void fill_half_from_cursor(void)
{
    Platform pf = {0};
    const uint8_t original[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    vid_host_start(&pf, unexpected_read, NULL);
    vid_send_vram(&pf, PF_AT(0), 0, (PfData){ .ptr = original }, sizeof original,
                  &(VidVramPort){ .step = 1, .after_high = true });
    vid_start_vram_uploads(&pf, PF_AT(0), &(VidVramPort){ .step = 1 });
    vid_set_vram_address(&pf, PF_AT(0), 1);
    vid_fill_vram_half(&pf, PF_AT(0), PF_VALUE(0xA5), 2, VID_VRAM_LOW_BYTES);
    vid_write_vram_byte(&pf, PF_AT(0), false, 0x33);
    const VidFrame *f = vid_host_picture(&pf);
    CHECK(f->vram[0] == 1 && f->vram[1] == 2);
    CHECK(f->vram[2] == 0xA5 && f->vram[3] == 4);
    CHECK(f->vram[4] == 0xA5 && f->vram[5] == 6);
    CHECK(f->vram[6] == 0x33 && f->vram[7] == 8);

    /* The high-half form obeys the active stride and wraps the VRAM cursor. */
    vid_start_vram_uploads(&pf, PF_AT(0), &(VidVramPort){ .step = 32, .after_high = true });
    vid_set_vram_address(&pf, PF_AT(0), 0x7FE0);
    vid_fill_vram_half(&pf, PF_AT(0), PF_VALUE(0x5A), 2, VID_VRAM_HIGH_BYTES);
    vid_fill_vram_half(&pf, PF_AT(0), PF_VALUE(0xEE), 0, VID_VRAM_HIGH_BYTES);
    vid_write_vram_byte(&pf, PF_AT(0), true, 0x77);
    f = vid_host_picture(&pf);
    CHECK(f->vram[0xFFC1] == 0x5A && f->vram[1] == 0x5A);
    CHECK(f->vram[0] == 1 && f->vram[0x41] == 0x77);
    /* A whole-word fill repeats one source byte too, even when the next
     * source byte differs. It does not alternate a two-byte pattern. */
    vid_send_vram_fill(&pf, PF_AT(0), 0, (PfData){ .ptr = original }, 4,
                       &(VidVramPort){ .step = 1, .after_high = true });
    f = vid_host_picture(&pf);
    CHECK(f->vram[0] == 1 && f->vram[1] == 1 && f->vram[2] == 1 && f->vram[3] == 1);

    /* A send from where the address stands goes on from there: whole words,
     * then one half of each word, stepping as the port says. */
    vid_start_vram_uploads(&pf, PF_AT(0), &(VidVramPort){ .step = 1, .after_high = true });
    vid_set_vram_address(&pf, PF_AT(0), 0x10);
    vid_continue_vram(&pf, PF_AT(0), (PfData){ .ptr = original }, 4);
    vid_continue_vram(&pf, PF_AT(0), (PfData){ .ptr = original + 4 }, 2);
    vid_continue_vram_half(&pf, PF_AT(0), (PfData){ .ptr = original }, 2, VID_VRAM_HIGH_BYTES);
    vid_start_vram_uploads(&pf, PF_AT(0), &(VidVramPort){ .step = 32, .after_high = false });
    vid_continue_vram_half(&pf, PF_AT(0), (PfData){ .ptr = original + 6 }, 2, VID_VRAM_LOW_BYTES);
    f = vid_host_picture(&pf);
    CHECK(f->vram[0x20] == 1 && f->vram[0x21] == 2 && f->vram[0x22] == 3 && f->vram[0x23] == 4);
    CHECK(f->vram[0x24] == 5 && f->vram[0x25] == 6);
    CHECK(f->vram[0x27] == 1 && f->vram[0x29] == 2);
    CHECK(f->vram[0x2A] == 7 && f->vram[0x6A] == 8 && f->vram[0x2B] == 0);
    CHECK(fallback_reads == 0);
    vid_host_stop(&pf);
}

static const VidLineState *line_state(const VidFrame *f, unsigned line)
{
    const VidLineState *s = NULL;
    for (unsigned i = 0; i < f->span_count && f->spans[i].line <= line; i++)
        s = &f->spans[i].state;
    return s;
}

static void raster_changes(void)
{
    Platform pf = {0};
    vid_host_start(&pf, effect_byte, NULL);
    vid_set_display(&pf, PF_AT(0), &(VidDisplay){ .brightness = 15 });
    pf.timing.part = TM_HOST_IN_LINE;
    pf.timing.line = VID_HOST_LAST_LINE;
    vid_set_display(&pf, PF_AT(0), &(VidDisplay){ .blank = true });
    const VidFrame *f = vid_host_picture(&pf);
    CHECK(!line_state(f, 1)->display.blank);
    CHECK(!line_state(f, VID_HOST_LAST_LINE)->display.blank);
    vid_host_frame_begins(&pf);
    f = vid_host_picture(&pf);
    CHECK(line_state(f, 1)->display.blank);

    vid_host_frame_begins(&pf);
    pf.timing.part = TM_HOST_IDLE;
    vid_set_display(&pf, PF_AT(0), &(VidDisplay){ .brightness = 15 });
    vid_set_line_slot(&pf, PF_AT(0), 0,
                      &(VidLineEffect){ .target = VID_LINE_BRIGHTNESS, .table = PF_GAME(0) });
    vid_enable_line_effects(&pf, PF_AT(0), 1);
    vid_raster_display(&pf, PF_AT(0), 30, &(VidDisplay){ .brightness = 7 });
    pf.timing.part = TM_HOST_IN_LINE;
    pf.timing.line = 49;
    vid_enable_line_effects(&pf, PF_AT(0), 0);
    f = vid_host_picture(&pf);
    CHECK(line_state(f, 30)->display.brightness == 7);
    /* The effect is still enabled after the raster display change. Sharing
     * its start line with the later disable would leave brightness at 7. */
    CHECK(line_state(f, 31)->display.brightness == 15);
    vid_host_stop(&pf);
}

static void picture_main(Platform *pf, void *ctx)
{
    const uint16_t *line = ctx;
    vid_set_display(pf, PF_AT(0), &(VidDisplay){ .brightness = 15 });
    vid_set_screens(pf, PF_AT(0), VID_OBJ_BIT, 0, VID_SCREEN_MAIN);
    vid_set_palette_color(pf, PF_AT(0), 0, 31);
    vid_set_sprite(pf, PF_AT(0), 0, &(VidSprite){ .x = 12, .y = 200 });
    tm_set_line(pf, PF_AT(0), *line);
    tm_enable_interrupts(pf, PF_AT(0), TM_INT_LINE);
    for (;;)
        tm_wait_frame(pf, PF_AT(0));
}

static void picture_line(Platform *pf, void *ctx)
{
    (void)ctx;
    vid_set_palette_color(pf, PF_AT(0), 0, 31 << 5);
    vid_start_vram_uploads(pf, PF_AT(0), VID_VRAM_WORD_PORT);
    vid_set_vram_address(pf, PF_AT(0), 0);
    vid_write_vram(pf, PF_AT(0), 0x1234);
    vid_set_sprite(pf, PF_AT(0), 0, &(VidSprite){ .x = 77, .y = 200 });
}

/* The host's frame model folds handler uploads into the completed picture.
 * Keep this scheduling contract separate from the observer's span retention. */
static void handler_uploads(uint16_t line)
{
    Platform pf = {0};
    vid_host_start(&pf, NULL, NULL);
    TmHostProgram program = { .main_thread = picture_main, .line = picture_line, .ctx = &line };
    tm_host_start(&pf, &program, 1);
    tm_host_frame(&pf);
    const VidFrame *f = vid_host_picture(&pf);
    CHECK(!f->spans[0].cgram && !f->spans[0].vram && !f->spans[0].sprites);
    CHECK(f->sprites[0].x == 77);
    CHECK(f->cgram[0] == (31 << 5) && f->vram[0] == 0x34);

    VidRenderer *r = vid_render_create();
    uint32_t *pixels = calloc(VID_RENDER_WIDTH * VID_RENDER_HEIGHT, sizeof *pixels);
    vid_render_frame(r, f, pixels, NULL);
    CHECK(pixels[0] == vid_render_rgb565_to_xrgb8888(vid_render_color(31 << 5, 15)));
    CHECK(pixels[(VID_RENDER_HEIGHT - 1) * VID_RENDER_WIDTH] ==
          vid_render_rgb565_to_xrgb8888(vid_render_color(31 << 5, 15)));

    tm_host_frame(&pf);
    f = vid_host_picture(&pf);
    vid_render_frame(r, f, pixels, NULL);
    CHECK(pixels[0] == vid_render_rgb565_to_xrgb8888(vid_render_color(31 << 5, 15)));
    free(pixels);
    vid_render_destroy(r);
    fiber_destroy(pf.timing.main);
    vid_host_stop(&pf);
}

static void changing_picture(void)
{
    Platform pf = {0};
    vid_host_start(&pf, NULL, NULL);
    VidObserver *ob = pf.video.observer;
    VidRenderer *r = vid_render_create();
    uint32_t *pixels = calloc(VID_RENDER_WIDTH * VID_RENDER_HEIGHT, sizeof *pixels);
    vid_set_display(&pf, PF_AT(0), &(VidDisplay){ .brightness = 15 });
    vid_set_screens(&pf, PF_AT(0), VID_OBJ_BIT, 0, VID_SCREEN_MAIN);
    vid_start_vram_uploads(&pf, PF_AT(0), VID_VRAM_WORD_PORT);
    vid_set_palette_color(&pf, PF_AT(0), 0, 31);
    vid_set_sprite(&pf, PF_AT(0), 0, &(VidSprite){ .x = 12, .y = 200 });
    vid_obs_frame_start(ob);
    vid_obs_line(ob, 0);
    for (unsigned line = 1; line <= VID_RENDER_HEIGHT; line++) {
        if (line == 100) {
            vid_set_palette_color(&pf, PF_AT(0), 0, 31 << 5);
            vid_set_vram_address(&pf, PF_AT(0), 0);
            vid_write_vram(&pf, PF_AT(0), 0x1234);
            vid_set_sprite(&pf, PF_AT(0), 0, &(VidSprite){ .x = 34, .y = 200 });
        }
        vid_obs_line(ob, (uint16_t)line);
    }
    const VidFrame *f = vid_obs_frame(ob);
    CHECK(f->span_count == 2);
    CHECK(f->spans[0].cgram && f->spans[0].cgram[0] == 31);
    CHECK(f->spans[0].vram && f->spans[0].vram[0] == 0);
    CHECK(f->spans[0].sprites && f->spans[0].sprites->sprites[0].x == 12);
    CHECK(f->spans[1].line == 100);
    CHECK(f->vram[0] == 0x34 && f->vram[1] == 0x12);
    CHECK(f->sprites[0].x == 34);
    vid_render_frame(r, f, pixels, NULL);
    CHECK(pixels[0] == vid_render_rgb565_to_xrgb8888(vid_render_color(31, 15)));
    CHECK(pixels[100 * VID_RENDER_WIDTH] == vid_render_rgb565_to_xrgb8888(vid_render_color(31 << 5, 15)));

    /* A palette on every line exceeds the decoder's old 32-snapshot pool.
     * Repeat the frame to check that reuse leaves no stale span pointers. */
    for (unsigned frame = 0; frame < 2; frame++) {
        vid_obs_frame_start(ob);
        vid_obs_line(ob, 0);
        for (unsigned line = 1; line <= VID_RENDER_HEIGHT; line++) {
            vid_set_palette_color(&pf, PF_AT(0), 0, (uint16_t)line);
            vid_obs_line(ob, (uint16_t)line);
        }
        f = vid_obs_frame(ob);
        CHECK(f->span_count == VID_RENDER_HEIGHT);
        for (unsigned i = 0; i + 1 < f->span_count; i++)
            CHECK(f->spans[i].cgram && f->spans[i].cgram[0] == i + 1);
        CHECK(f->cgram[0] == VID_RENDER_HEIGHT);
        /* Even after the last line, keep its memory until the consumer ends
         * the frame. The next frame can then use the newly uploaded color. */
        vid_set_palette_color(&pf, PF_AT(0), 0, 0);
        CHECK(f->spans[f->span_count - 1].cgram[0] == VID_RENDER_HEIGHT);
        vid_obs_frame_end(ob);
    }
    free(pixels);
    vid_render_destroy(r);
    vid_host_stop(&pf);
}

/* The view is the original's screen until the driver chooses another. */
static void view_size(void)
{
    Platform pf = {0};
    VidView v = vid_view(&pf);
    CHECK(v.width == VID_ORIGINAL_VIEW_WIDTH && v.height == VID_ORIGINAL_VIEW_HEIGHT);
    vid_host_set_view(&pf, (VidView){ 400, 224 });
    v = vid_view(&pf);
    CHECK(v.width == 400 && v.height == 224);
}

/* A wider view: the original's screen in its middle; a sprite placed past the
 * screen's right edge shown there, and one the game hid shown nowhere though
 * its nine bits would land in the margin; a background layer left out of the
 * margins until the mode says it holds the picture there. */
static void wider_view(void)
{
    enum { WIDTH = 320, LEFT = (256 - WIDTH) / 2, ROW = 10, MAP = 0x0800, CHARS = 0x1000 };
    Platform pf = {0};
    vid_host_start(&pf, NULL, NULL);
    vid_host_frame_begins(&pf);
    vid_host_set_view(&pf, (VidView){ WIDTH, 224 });
    vid_set_display(&pf, PF_AT(0), &(VidDisplay){ .brightness = 15 });
    vid_set_mode(&pf, PF_AT(0), &(VidMode){ .mode = 1 });
    vid_set_screens(&pf, PF_AT(0), VID_BG1_BIT | VID_OBJ_BIT, 0, VID_SCREEN_MAIN);
    vid_set_obj_config(&pf, PF_AT(0), &(VidObjConfig){ 0 });
    vid_set_bg_map(&pf, PF_AT(0), VID_BG1, &(VidBg){ VID_TILEMAP(MAP, 64, 32) });
    vid_set_bg_chars(&pf, PF_AT(0), VID_BG1, CHARS, CHARS);
    vid_set_palette_color(&pf, PF_AT(0), 0, 31);
    vid_set_palette_color(&pf, PF_AT(0), 1, 31 << 5);
    vid_set_palette_color(&pf, PF_AT(0), 129, 31 << 10);
    uint8_t tile[32] = { 0 };
    for (unsigned row = 0; row < 8; row++)
        tile[2 * row] = 0xFF;   /* every pixel colour 1 */
    vid_send_vram(&pf, PF_AT(0), 0, (PfData){ .ptr = tile }, sizeof tile, VID_VRAM_WORD_PORT);
    vid_send_vram(&pf, PF_AT(0), CHARS + 16, (PfData){ .ptr = tile }, sizeof tile, VID_VRAM_WORD_PORT);
    static uint8_t map[64 * 32 * 2];
    for (unsigned i = 0; i < sizeof map; i += 2)
        map[i] = 1;   /* tile 1 everywhere */
    vid_send_vram(&pf, PF_AT(0), MAP, (PfData){ .ptr = map }, sizeof map, VID_VRAM_WORD_PORT);
    uint8_t oam[0x220] = { 0 };
    for (unsigned n = 0; n < VID_SPRITES; n++)
        oam[4 * n + 1] = VID_OAM_HIDDEN_Y;
    oam[0] = vid_oam_x_low(270), oam[1] = ROW, oam[0x200] = vid_oam_high_bits(270, false);
    oam[4] = vid_oam_x_low(VID_OAM_HIDDEN_X), oam[5] = ROW, oam[0x200] |= vid_oam_high_bits(VID_OAM_HIDDEN_X, false) << 2;
    VidSpritePlace places[VID_SPRITES] = { { 270, ROW, VID_PLACE_AT }, { VID_OAM_HIDDEN_X, ROW, VID_PLACE_HIDDEN } };
    vid_send_oam(&pf, PF_AT(0), (PfData){ .ptr = oam }, places);
    const VidFrame *f = vid_host_picture(&pf);
    CHECK(vid_render_width(f) == WIDTH);
    CHECK(f->sprites[0].placed && f->sprites[0].x == 270 && f->sprites[1].hidden);

    VidRenderer *r = vid_render_create();
    uint32_t *pixels = calloc(VID_RENDER_MAX_PIXELS, sizeof *pixels);
    vid_render_frame(r, f, pixels, NULL);
    uint32_t backdrop = vid_render_rgb565_to_xrgb8888(vid_render_color(31, 15));
    uint32_t layer = vid_render_rgb565_to_xrgb8888(vid_render_color(31 << 5, 15));
    uint32_t sprite = vid_render_rgb565_to_xrgb8888(vid_render_color(31 << 10, 15));
    const uint32_t *row = pixels + ROW * WIDTH;
    CHECK(row[270 - LEFT] == sprite && row[277 - LEFT] == sprite && row[278 - LEFT] == backdrop);
    CHECK(row[256 - LEFT] == backdrop);   /* the hidden one, over no layer */
    CHECK(row[0] == backdrop && row[-LEFT - 1] == backdrop && row[-LEFT] == layer && row[255 - LEFT] == layer);

    vid_show_a_layer_across(&pf, VID_BG1, LEFT, 280);
    f = vid_host_picture(&pf);
    vid_render_frame(r, f, pixels, NULL);
    CHECK(row[0] == layer && row[279 - LEFT] == layer && row[280 - LEFT] == backdrop);

    /* Sprites in the margins, in front of the layer: one of VRAM's tiles
     * straddling the screen's left edge, shown only past it, and one of its
     * own picture, under the table's sprite at 270, which is behind the
     * layer; and every frame starts with none. */
    VidSpritePicture picture = { 0 };
    for (unsigned q = 0; q < VID_PICTURE_TILES; q++)
        for (unsigned line = 0; line < 8; line++)
            picture.tile[q][2 * line + 1] = 0xFF;   /* every dot colour 2 */
    picture.colour[2] = 31;
    vid_show_a_sprite_in_the_margins(&pf, &(VidMarginSprite){ .x = -4, .y = ROW, .priority = 3 }, NULL);
    vid_show_a_sprite_in_the_margins(&pf, &(VidMarginSprite){ .x = 262, .y = ROW - 8, .priority = 3 }, &picture);
    vid_show_a_layer_across(&pf, VID_BG1, LEFT, 280);
    f = vid_host_picture(&pf);
    CHECK(f->margin_count == 2 && !f->margin[0].own && f->margin[1].own);
    vid_render_frame(r, f, pixels, NULL);
    uint32_t own = vid_render_rgb565_to_xrgb8888(vid_render_color(31, 15));
    CHECK(row[-4 - LEFT] == sprite && row[-1 - LEFT] == sprite && row[-LEFT] == layer);
    CHECK(row[262 - LEFT] == own && row[269 - LEFT] == own && row[270 - LEFT] == layer && row[278 - LEFT] == layer);
    CHECK(pixels[(ROW - 8) * WIDTH + 262 - LEFT] == own && pixels[(ROW - 9) * WIDTH + 262 - LEFT] == layer);
    vid_host_frame_begins(&pf);
    vid_show_a_layer_across(&pf, VID_BG1, LEFT, 280);
    f = vid_host_picture(&pf);
    CHECK(f->margin_count == 0);
    vid_render_frame(r, f, pixels, NULL);
    CHECK(row[-4 - LEFT] == layer && row[262 - LEFT] == layer);

    /* A panel's lines (a one-screen tilemap from line PANEL down) keep to the
     * screen, the backdrop beside them, unless the frame shows the picture
     * beside the panels: then their margins are drawn as the line above it. */
    enum { PANEL = 150 };
    static VidFrame panelled;
    vid_host_frame_begins(&pf);
    vid_show_a_layer_across(&pf, VID_BG1, LEFT, 280);
    panelled = *vid_host_picture(&pf);
    CHECK(panelled.span_count == 1 && !panelled.picture_beside_panels);
    panelled.spans[1] = panelled.spans[0];
    panelled.spans[1].line = PANEL;
    panelled.spans[1].state.bg[0].map_w = 32;
    panelled.span_count = 2;
    const uint32_t *panel_row = pixels + (PANEL + 2) * WIDTH;
    vid_render_frame(r, &panelled, pixels, NULL);
    CHECK(panel_row[0] == backdrop && panel_row[-LEFT] == layer && row[0] == layer);
    vid_show_the_picture_beside_the_panels(&pf);
    CHECK(vid_host_picture(&pf)->picture_beside_panels);
    panelled.picture_beside_panels = true;
    vid_render_frame(r, &panelled, pixels, NULL);
    CHECK(panel_row[0] == layer && panel_row[279 - LEFT] == layer && panel_row[280 - LEFT] == backdrop);
    vid_host_frame_begins(&pf);
    CHECK(!vid_host_picture(&pf)->picture_beside_panels);

    /* The original's screen draws from the table alone. */
    vid_host_set_view(&pf, (VidView){ 256, 224 });
    f = vid_host_picture(&pf);
    vid_render_frame(r, f, pixels, NULL);
    CHECK(vid_render_width(f) == 256 && pixels[ROW * 256 + 255] == layer);
    free(pixels);
    vid_render_destroy(r);
    vid_host_stop(&pf);
}

int main(void)
{
    view_size();
    wider_view();
    resource_transfers();
    view_sends();
    fill_half_from_cursor();
    changing_picture();
    raster_changes();
    handler_uploads(99);
    handler_uploads(VID_HOST_LAST_LINE);
    printf("video_test: %d failures\n", failures);
    return failures != 0;
}
