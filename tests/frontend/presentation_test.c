/* Checks the window's presentation (frontend/presentation.c): integer scaling
 * pixel-exact and letterboxed at 1080p, 1440p and 4K, fit scaling filling the
 * output at the picture's aspect and drawn sharp-bilinear from the whole
 * scale above it, scanlines over each picture row, and the settings' text
 * read back as written. Exits non-zero on failure.
 *
 *   presentation_test */
#include <stdio.h>
#include <string.h>

#include "presentation.h"

static int failures;

#define CHECK(cond, what)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, what);                                  \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* ct_native's picture: 256 across, 208 rows shown, pixels 7:6 (a 4:3 screen's width over 224 rows). */
enum { WIDTH = 256, ROWS = 208 };
#define ASPECT (4.0 / 3.0 * 224 / 256)

static void integer_at(int out_w, int out_h, int across, int down)
{
    PresentLayout l = present_layout(SCALING_INTEGER, WIDTH, ROWS, ASPECT, out_w, out_h);
    char what[96];
    snprintf(what, sizeof what, "integer scaling at %dx%d", out_w, out_h);
    CHECK(l.across == across && l.down == down && !l.smooth, what);
    CHECK(l.place.w == WIDTH * across && l.place.h == ROWS * down, what);
    CHECK(l.place.x == (out_w - l.place.w) / 2 && l.place.y == (out_h - l.place.h) / 2, what);
}

static void fit_at(int out_w, int out_h)
{
    PresentLayout l = present_layout(SCALING_FIT, WIDTH, ROWS, ASPECT, out_w, out_h);
    char what[96];
    snprintf(what, sizeof what, "fit scaling at %dx%d", out_w, out_h);
    CHECK(l.place.h == out_h || l.place.w == out_w, what);   /* fills one way */
    CHECK(l.place.w <= out_w && l.place.h <= out_h, what);
    double aspect = (double)l.place.w / l.place.h, wanted = WIDTH * ASPECT / ROWS;
    CHECK(aspect > wanted * 0.995 && aspect < wanted * 1.005, what);
    CHECK(WIDTH * l.across >= l.place.w && WIDTH * (l.across - 1) < l.place.w, what);
    CHECK(ROWS * l.down >= l.place.h && ROWS * (l.down - 1) < l.place.h, what);
    CHECK(l.smooth, what);
}

int main(void)
{
    integer_at(1920, 1080, 6, 5);
    integer_at(2560, 1440, 7, 6);
    integer_at(3840, 2160, 12, 10);
    integer_at(WIDTH * 4, ROWS * 3, 4, 3);   /* the window ct_native opens */
    fit_at(1920, 1080);
    fit_at(2560, 1440);
    fit_at(3840, 2160);
    fit_at(1000, 2000);

    /* A window smaller than the picture: fit, smoothed down from it. */
    PresentLayout small = present_layout(SCALING_INTEGER, WIDTH, ROWS, ASPECT, 200, 150);
    CHECK(small.smooth && small.place.w <= 200 && small.place.h <= 150, "a small window doesn't fit the picture");

    int w, h;
    present_window_size(3, WIDTH, ROWS, ASPECT, &w, &h);
    CHECK(w == WIDTH * 4 && h == ROWS * 3, "the window's size isn't integer scaling's");
    present_window_size(2, WIDTH, ROWS, ASPECT, &w, &h);
    CHECK(w == 597 && h == ROWS * 2, "a window narrower than the picture's shape");

    PresentRect place = { 10, 20, WIDTH * 6, ROWS * 5 }, lines[ROWS];
    int n = present_scanlines(place, ROWS, lines, ROWS);
    CHECK(n == ROWS, "a scanline for each row");
    CHECK(lines[0].y == 20 + 5 - 2 && lines[0].h == 2 && lines[0].x == 10 && lines[0].w == WIDTH * 6,
          "the first row's scanline");
    CHECK(lines[ROWS - 1].y + lines[ROWS - 1].h == 20 + ROWS * 5, "the last scanline ends with the picture");
    place.h = ROWS;
    CHECK(present_scanlines(place, ROWS, lines, ROWS) == 0, "scanlines on rows one output row tall");

    /* Square pixels: every row, no widening; integer scaling square multiples. */
    PixelLook square = present_pixel_look(PIXELS_SQUARE, 224), tv = present_pixel_look(PIXELS_TV, 224);
    CHECK(square.crop == 0 && square.aspect == 1.0, "square pixels crop or widen");
    CHECK(tv.crop == 8 && tv.aspect > 1.16 && tv.aspect < 1.17, "tv pixels aren't the television's");
    PresentLayout sq = present_layout(SCALING_INTEGER, 398, 224, 1.0, 1920, 1080);
    CHECK(sq.across == 4 && sq.down == 4 && sq.place.w == 1592 && sq.place.h == 896, "square integer at 1080p");
    PresentLayout sqfit = present_layout(SCALING_FIT, 398, 224, 1.0, 1920, 1080);
    CHECK(sqfit.place.h == 1080 && sqfit.place.w == 1919, "square fit at 1080p");

    /* Views worked out from a screen shape, for each pixel shape. */
    static const struct {
        ViewShape view;
        PixelShape pixels;
        int width;
    } views[] = {
        { VIEW_16_9, PIXELS_SQUARE, 398 }, { VIEW_16_10, PIXELS_SQUARE, 358 }, { VIEW_4_3, PIXELS_SQUARE, 299 },
        { VIEW_21_9, PIXELS_SQUARE, 480 },   /* 523, held to the widest */
        { VIEW_16_9, PIXELS_TV, 317 }, { VIEW_16_10, PIXELS_TV, 285 }, { VIEW_21_9, PIXELS_TV, 416 },
        { VIEW_4_3, PIXELS_TV, 256 },        /* 238, held to the original's */
    };
    for (unsigned i = 0; i < sizeof views / sizeof views[0]; i++) {
        PresentSettings v = PRESENT_SETTINGS_DEFAULT;
        v.view = views[i].view;
        v.pixels = views[i].pixels;
        char what[64];
        snprintf(what, sizeof what, "view %u's width %d", i, present_view_width(&v, 224, 256, 480));
        CHECK(present_view_width(&v, 224, 256, 480) == views[i].width, what);
    }
    PresentSettings given = PRESENT_SETTINGS_DEFAULT;
    CHECK(present_view_width(&given, 224, 256, 480) == 256, "no view given isn't the original's");
    given.view_width = 320;
    CHECK(present_view_width(&given, 224, 256, 480) == 320, "a width given isn't kept");
    ViewShape shape;
    int width;
    CHECK(present_view_from("16:9", &shape, &width) && shape == VIEW_16_9 && width == 0, "16:9 doesn't read");
    CHECK(present_view_from("320", &shape, &width) && shape == VIEW_WIDTH && width == 320, "320 doesn't read");
    CHECK(!present_view_from("16:8", &shape, &width), "16:8 reads");

    PresentSettings s = { SCALING_FIT, FILTER_SCANLINES, true, FULLSCREEN_EXCLUSIVE, PIXELS_SQUARE, VIEW_16_9, 0 },
                    back = PRESENT_SETTINGS_DEFAULT;
    char text[256];
    size_t len = present_settings_write(&s, text, sizeof text);
    CHECK(len > 0, "the settings' text");
    present_settings_read(&back, text, len);
    CHECK(s.scaling == back.scaling && s.filter == back.filter && s.fullscreen == back.fullscreen &&
              s.fullscreen_mode == back.fullscreen_mode && s.pixels == back.pixels && s.view == back.view &&
              s.view_width == back.view_width,
          "the settings read back differ");
    const char *later = "# a later build\r\nscaling = INTEGER\r\nbrightness = 3\nfullscreen = maybe\n";
    present_settings_read(&back, later, strlen(later));
    CHECK(back.scaling == SCALING_INTEGER && back.fullscreen && back.filter == FILTER_SCANLINES,
          "the settings' text read past what it doesn't know");
    Scaling scaling;
    CHECK(!present_scaling_from("stretch", &scaling), "an unknown scaling reads");

    if (!failures)
        printf("presentation: ok\n");
    return failures != 0;
}
