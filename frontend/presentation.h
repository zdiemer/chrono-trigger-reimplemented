/* How the window driver shows the renderer's finished picture on a display
 * (docs/PLAYING.md, "Scaling and fullscreen"): where on the output it goes,
 * how it is scaled there, what filter lies over it, and how the window fills
 * the screen. The picture the game draws is the same under every choice;
 * these are the driver's settings, remembered between launches as text
 * beside the asset location, never the player's state.
 *
 * - Integer scaling gives every picture pixel the same whole number of
 *   output pixels across and down (down a whole multiple of the picture's
 *   rows, across the nearest whole multiple to that times the pixel's
 *   aspect), so the picture is pixel-exact and letterboxed.
 * - Fit scaling fills as much of the output as the picture's aspect allows,
 *   and is drawn sharp-bilinear: scaled up by whole numbers to the nearest
 *   size at or above the output's, then smoothly down to it, so pixel edges
 *   blur by at most one output pixel.
 * - Scanlines darken the lower part of each picture row's output rows.
 * - The pixels show as a television showed them -- a little wider than tall,
 *   the original's 256 across filling a 4:3 screen's width, and the eight
 *   rows top and bottom it hid in its overscan left out -- or square, with
 *   every row shown.
 * - The view's width (how much of the game's picture there is across) can be
 *   worked out from a screen shape -- 4:3, 16:10, 16:9, 21:9 -- so that the
 *   picture as shown has that shape. It is fixed at launch, since the game
 *   draws the view it was started with.
 *
 * Nothing here calls SDL: window.c draws what this lays out. */
#ifndef CT_FRONTEND_PRESENTATION_H
#define CT_FRONTEND_PRESENTATION_H

#include <stdbool.h>
#include <stddef.h>

typedef enum { SCALING_INTEGER, SCALING_FIT, SCALINGS } Scaling;
typedef enum { FILTER_NONE, FILTER_SCANLINES, FILTERS } Filter;
typedef enum { FULLSCREEN_BORDERLESS, FULLSCREEN_EXCLUSIVE, FULLSCREEN_MODES } FullscreenMode;

typedef enum { PIXELS_TV, PIXELS_SQUARE, PIXEL_SHAPES } PixelShape;
/* The view: a width given in pixels, or a screen shape to work it out from. */
typedef enum { VIEW_WIDTH, VIEW_4_3, VIEW_16_10, VIEW_16_9, VIEW_21_9, VIEW_SHAPES } ViewShape;

/* The window's settings, which a launch remembers. */
typedef struct {
    Scaling scaling;
    Filter filter;
    bool fullscreen;
    FullscreenMode fullscreen_mode;
    PixelShape pixels;
    ViewShape view;
    int view_width;   /* VIEW_WIDTH: pixels across, 0 for the original's */
} PresentSettings;

#define PRESENT_SETTINGS_DEFAULT { SCALING_INTEGER, FILTER_NONE, false, FULLSCREEN_BORDERLESS, PIXELS_TV, VIEW_WIDTH, 0 }

/* How a pixel shape shows a picture `height` rows tall: the rows left out top
 * and bottom, and how many times as wide as tall a pixel shows. */
typedef struct {
    int crop;
    double aspect;
} PixelLook;

PixelLook present_pixel_look(PixelShape shape, int height);

/* The view's width under the settings, for a picture `height` rows tall: the
 * width given, or the one whose picture, as the settings' pixels show it, has
 * the view's shape; held within least..most (none given: least). */
int present_view_width(const PresentSettings *settings, int height, int least, int most);

typedef struct {
    int x, y, w, h;
} PresentRect;

/* The picture's place on an output of out_w x out_h, for a picture `width`
 * pixels across and `rows` down whose pixels show `aspect` times as wide as
 * tall. `across` and `down` are the output pixels a picture pixel takes under
 * integer scaling; under fit scaling (or where the output is too small for
 * one whole multiple) they are the whole-number scale the picture is first
 * drawn at before it is smoothed down to `place` (0 where it is drawn
 * straight there). */
typedef struct {
    PresentRect place;
    int across, down;
    bool smooth;     /* scaled down to `place` from across x down: sharp-bilinear */
} PresentLayout;

PresentLayout present_layout(Scaling scaling, int width, int rows, double aspect, int out_w, int out_h);

/* The window's size at `scale` times the picture's rows: as wide as integer
 * scaling lays the picture out there, or as the picture at its true aspect
 * where that is wider (where the whole multiple across rounds down), so the
 * window is never narrower than the picture's shape. */
void present_window_size(int scale, int width, int rows, double aspect, int *w, int *h);

/* The rows a scanline filter darkens over `place` for a picture `rows` tall:
 * up to `most` of them into out[], each picture row's lower part; returns how
 * many. None where a picture row is under two output rows tall. */
int present_scanlines(PresentRect place, int rows, PresentRect *out, int most);

/* Names for the settings, as the command line and the settings text spell
 * them ("integer", "fit"; "none", "scanlines"; "borderless", "exclusive";
 * "tv", "square"; a view "16:9", or a width "320"). */
const char *present_scaling_name(Scaling s);
const char *present_filter_name(Filter f);
const char *present_fullscreen_mode_name(FullscreenMode m);
const char *present_pixel_shape_name(PixelShape p);
bool present_scaling_from(const char *name, Scaling *out);
bool present_pixel_shape_from(const char *name, PixelShape *out);
bool present_view_from(const char *name, ViewShape *view, int *width);
bool present_filter_from(const char *name, Filter *out);
bool present_fullscreen_mode_from(const char *name, FullscreenMode *out);

/* The settings as text, a "name = value" line each; and read back over
 * *settings, a line at a time, ignoring lines it doesn't know (a later
 * build's). Returns the bytes written (0 where `size` is too small). */
size_t present_settings_write(const PresentSettings *settings, char *out, size_t size);
void present_settings_read(PresentSettings *settings, const char *text, size_t len);

#endif
