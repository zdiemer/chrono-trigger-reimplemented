/* The service-built picture keeps the memory each line saw, including
 * changes to sprites and tile data while later lines are still being built. */
#include <stdio.h>
#include <stdlib.h>

#include "platform/native/host.h"
#include "platform/native/render.h"
#include "platform/observe.h"

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static uint8_t effect_byte(void *ctx, uint32_t at)
{
    (void)ctx;
    return at == 0 ? 255 : at <= 127 ? 15 : 0;
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
    vid_host_start(&pf, effect_byte, NULL, NULL);
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
    vid_host_start(&pf, NULL, NULL, NULL);
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
    vid_host_start(&pf, NULL, NULL, NULL);
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

int main(void)
{
    changing_picture();
    raster_changes();
    handler_uploads(99);
    handler_uploads(VID_HOST_LAST_LINE);
    printf("video_test: %d failures\n", failures);
    return failures != 0;
}
