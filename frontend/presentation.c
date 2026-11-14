/* The window's presentation (frontend/presentation.h). */
#include "presentation.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* Halves round up, the 7:6 pixel at three rows a row (3.5 across) among them. */
static int nearest(double v) { return (int)(v + 0.5 + 1e-9); }

static PresentRect centred(int w, int h, int out_w, int out_h)
{
    return (PresentRect){ (out_w - w) / 2, (out_h - h) / 2, w, h };
}

/* The largest picture of the right aspect that fits, drawn sharp-bilinear
 * from the whole-number scale at or above it. */
static PresentLayout fit(int width, int rows, double aspect, int out_w, int out_h)
{
    double across = out_w / (width * aspect), down = (double)out_h / rows;
    double scale = across < down ? across : down;
    int w = nearest(width * aspect * scale), h = nearest(rows * scale);
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;
    PresentLayout l = { centred(w, h, out_w, out_h), (w + width - 1) / width, (h + rows - 1) / rows, false };
    l.smooth = w != width * l.across || h != rows * l.down;
    return l;
}

/* Integer scaling: `down` output rows a picture row, the most that fit, and
 * across the whole number nearest that times the aspect. */
static int across_for(int down, double aspect)
{
    int across = nearest(down * aspect);
    return across < 1 ? 1 : across;
}

PresentLayout present_layout(Scaling scaling, int width, int rows, double aspect, int out_w, int out_h)
{
    if (scaling == SCALING_INTEGER) {
        for (int down = out_h / rows; down >= 1; down--) {
            int across = across_for(down, aspect);
            if (width * across <= out_w)
                return (PresentLayout){ centred(width * across, rows * down, out_w, out_h), across, down, false };
        }
    }
    return fit(width, rows, aspect, out_w, out_h);
}

void present_window_size(int scale, int width, int rows, double aspect, int *w, int *h)
{
    int exact = width * across_for(scale, aspect), shaped = nearest(width * aspect * scale);
    *w = exact > shaped ? exact : shaped;
    *h = rows * scale;
}

int present_scanlines(PresentRect place, int rows, PresentRect *out, int most)
{
    if (place.h < 2 * rows)
        return 0;
    int n = 0;
    for (int r = 0; r < rows && n < most; r++) {
        int top = place.y + r * place.h / rows, bottom = place.y + (r + 1) * place.h / rows;
        int dark = (bottom - top + 2) / 3;
        out[n++] = (PresentRect){ place.x, bottom - dark, place.w, dark };
    }
    return n;
}

/* ---- pixels and the view ----------------------------------------------------------- */

/* A television showed the original's 256 pixels across the width of a 4:3
 * screen, and hid eight rows top and bottom in its overscan. */
enum { ORIGINAL_WIDTH = 256, TV_CROP = 8 };

static const int view_shapes[VIEW_SHAPES][2] = { { 0, 0 }, { 4, 3 }, { 16, 10 }, { 16, 9 }, { 21, 9 } };

PixelLook present_pixel_look(PixelShape shape, int height)
{
    if (shape == PIXELS_SQUARE)
        return (PixelLook){ 0, 1.0 };
    return (PixelLook){ TV_CROP, 4.0 / 3.0 * height / ORIGINAL_WIDTH };
}

int present_view_width(const PresentSettings *s, int height, int least, int most)
{
    int width = s->view_width ? s->view_width : least;
    if (s->view != VIEW_WIDTH) {
        PixelLook look = present_pixel_look(s->pixels, height);
        int rows = height - 2 * look.crop;
        width = nearest(rows * view_shapes[s->view][0] / (view_shapes[s->view][1] * look.aspect));
    }
    return width < least ? least : width > most ? most : width;
}

/* ---- names and the settings' text ---------------------------------------------- */

static const char *const scaling_names[SCALINGS] = { "integer", "fit" };
static const char *const filter_names[FILTERS] = { "none", "scanlines" };
static const char *const mode_names[FULLSCREEN_MODES] = { "borderless", "exclusive" };
static const char *const pixel_names[PIXEL_SHAPES] = { "tv", "square" };
static const char *const view_names[VIEW_SHAPES] = { "", "4:3", "16:10", "16:9", "21:9" };

const char *present_pixel_shape_name(PixelShape p) { return pixel_names[p]; }

const char *present_scaling_name(Scaling s) { return scaling_names[s]; }
const char *present_filter_name(Filter f) { return filter_names[f]; }
const char *present_fullscreen_mode_name(FullscreenMode m) { return mode_names[m]; }

static bool same_word(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return false;
    return !*a && !*b;
}

static int named(const char *name, const char *const *names, int count)
{
    for (int i = 0; i < count; i++)
        if (same_word(name, names[i]))
            return i;
    return -1;
}

bool present_scaling_from(const char *name, Scaling *out)
{
    int i = named(name, scaling_names, SCALINGS);
    if (i >= 0)
        *out = (Scaling)i;
    return i >= 0;
}

bool present_filter_from(const char *name, Filter *out)
{
    int i = named(name, filter_names, FILTERS);
    if (i >= 0)
        *out = (Filter)i;
    return i >= 0;
}

bool present_pixel_shape_from(const char *name, PixelShape *out)
{
    int i = named(name, pixel_names, PIXEL_SHAPES);
    if (i >= 0)
        *out = (PixelShape)i;
    return i >= 0;
}

bool present_view_from(const char *name, ViewShape *view, int *width)
{
    int i = named(name, view_names, VIEW_SHAPES);
    if (i > 0) {
        *view = (ViewShape)i;
        *width = 0;
        return true;
    }
    int n = 0;
    const char *c = name;
    for (; *c >= '0' && *c <= '9' && n < 10000; c++)
        n = n * 10 + (*c - '0');
    if (c == name || *c)
        return false;
    *view = VIEW_WIDTH;
    *width = n;
    return true;
}

bool present_fullscreen_mode_from(const char *name, FullscreenMode *out)
{
    int i = named(name, mode_names, FULLSCREEN_MODES);
    if (i >= 0)
        *out = (FullscreenMode)i;
    return i >= 0;
}

size_t present_settings_write(const PresentSettings *s, char *out, size_t size)
{
    char view[16];
    if (s->view == VIEW_WIDTH)
        snprintf(view, sizeof view, "%d", s->view_width ? s->view_width : ORIGINAL_WIDTH);
    else
        snprintf(view, sizeof view, "%s", view_names[s->view]);
    int n = snprintf(out, size,
                     "scaling = %s\nfilter = %s\nfullscreen = %s\nfullscreen mode = %s\npixels = %s\nview = %s\n",
                     present_scaling_name(s->scaling), present_filter_name(s->filter), s->fullscreen ? "on" : "off",
                     present_fullscreen_mode_name(s->fullscreen_mode), present_pixel_shape_name(s->pixels), view);
    return n < 0 || (size_t)n >= size ? 0 : (size_t)n;
}

static void trim(char *s)
{
    size_t n = strlen(s), lead = strspn(s, " \t\r");
    while (n > lead && isspace((unsigned char)s[n - 1]))
        s[--n] = 0;
    memmove(s, s + lead, n - lead + 1);
}

static void read_line(PresentSettings *s, char *line)
{
    char *eq = strchr(line, '=');
    if (!eq)
        return;
    *eq = 0;
    char *value = eq + 1;
    trim(line);
    trim(value);
    if (same_word(line, "scaling"))
        present_scaling_from(value, &s->scaling);
    else if (same_word(line, "filter"))
        present_filter_from(value, &s->filter);
    else if (same_word(line, "fullscreen") && (same_word(value, "on") || same_word(value, "off")))
        s->fullscreen = same_word(value, "on");
    else if (same_word(line, "fullscreen mode"))
        present_fullscreen_mode_from(value, &s->fullscreen_mode);
    else if (same_word(line, "pixels"))
        present_pixel_shape_from(value, &s->pixels);
    else if (same_word(line, "view"))
        present_view_from(value, &s->view, &s->view_width);
}

void present_settings_read(PresentSettings *s, const char *text, size_t len)
{
    size_t at = 0;
    while (at < len) {
        char line[128];
        size_t n = 0;
        while (at < len && text[at] != '\n') {
            if (n < sizeof line - 1)
                line[n++] = text[at];
            at++;
        }
        at++;
        line[n] = 0;
        read_line(s, line);
    }
}
