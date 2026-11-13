#include "fonts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A glyph's line is a byte of each bitplane, one after the other: the left
 * eight pixels' in a glyph's own bytes, the 12x12 font's right four in a
 * byte it shares with the glyph beside it. */
enum { PLANES = 2, BYTE_PIXELS = 8, HALF_BYTE = 4, WIDTH_MAX = 0xFF };

typedef struct {
    unsigned size;       /* pixels across and lines down */
    uint32_t glyphs;
    int widths;          /* the row of the glyphs' widths, or -1 */
    uint32_t first;      /* the first glyph with a width */
} Font;

static const char WIDTHS[] = "font-widths:";

/* The row of the widths of the font at row `font`, and its first glyph's number; -1 where it has none. */
static int widths_of(int font, uint32_t *first)
{
    size_t name_len = strlen(asset_catalogue[font].name), prefix = sizeof WIDTHS - 1;
    for (uint16_t i = 0; i < asset_catalogue_count; i++) {
        const char *codec = asset_codec_names[asset_catalogue[i].codec];
        if (!strncmp(codec, WIDTHS, prefix) && !strncmp(codec + prefix, asset_catalogue[font].name, name_len) &&
            codec[prefix + name_len] == ':') {
            *first = (uint32_t)strtoul(codec + prefix + name_len + 1, NULL, 16);
            return i;
        }
    }
    return -1;
}

/* The font at a row of the catalogue; its widths looked for only where
 * `widths`, a line of pixels needing none of them. */
static bool font_of(int row, bool widths, Font *f)
{
    const AssetEntry *e = &asset_catalogue[row];
    const char *codec = asset_codec_names[e->codec];
    if (!strcmp(codec, "font:8x8"))
        f->size = 8;
    else if (!strcmp(codec, "font:12x12"))
        f->size = 12;
    else
        return false;
    f->glyphs = e->length / (PLANES * f->size * f->size / BYTE_PIXELS);
    f->first = 0;
    f->widths = widths ? widths_of(row, &f->first) : -1;
    return true;
}

static bool decodes(const AssetEntry *e, bool *named)
{
    *named = strncmp(asset_codec_names[e->codec], WIDTHS, sizeof WIDTHS - 1) != 0;
    return !*named || !strncmp(asset_codec_names[e->codec], "font:", 5);
}

static bool has_width(const Font *f, uint32_t glyph)
{
    return f->widths >= 0 && glyph >= f->first && glyph - f->first < asset_catalogue[f->widths].length;
}

/* Where a pixel of a glyph's line is: the line's first plane's byte (the
 * second's follows it) and the pixel's bit in it. */
static uint32_t pixel_at(const Font *f, uint32_t glyph, unsigned line, unsigned x, unsigned *bit)
{
    uint32_t glyph_bytes = PLANES * f->size;   /* the left eight pixels' bytes, and a pair's right four's */
    if (x < BYTE_PIXELS) {
        *bit = BYTE_PIXELS - 1 - x;
        return glyph * glyph_bytes + PLANES * line;
    }
    *bit = (glyph & 1 ? HALF_BYTE - 1 : BYTE_PIXELS - 1) - (x - BYTE_PIXELS);
    return f->glyphs * glyph_bytes + glyph / 2 * glyph_bytes + PLANES * line;
}

static const char PIXEL[] = ".123";

static bool write_font(DecodedText *t, const AssetStore *store, const AssetEntry *e, const uint8_t *bytes)
{
    Font f = { 0 };
    font_of((int)(e - asset_catalogue), true, &f);
    const uint8_t *widths = NULL;
    if (f.widths >= 0) {
        const AssetEntry *w = &asset_catalogue[f.widths];
        uint32_t n = 0;
        widths = store->find(store->ctx, w->name, asset_type_names[w->type], &n);
        if (!widths || n != w->length)
            return false;
    }
    char line[64];
    snprintf(line, sizeof line, "# %u glyphs, %ux%u\n", (unsigned)f.glyphs, f.size, f.size);
    decoded_put(t, line);
    for (uint32_t g = 0; g < f.glyphs; g++) {
        int n = snprintf(line, sizeof line, "glyph %02X", (unsigned)g);
        if (has_width(&f, g))
            snprintf(line + n, sizeof line - (size_t)n, " width %u", widths[g - f.first]);
        decoded_put(t, line);
        decoded_put(t, "\n");
        for (unsigned y = 0; y < f.size; y++) {
            for (unsigned x = 0; x < f.size; x++) {
                unsigned bit;
                uint32_t at = pixel_at(&f, g, y, x, &bit);
                line[x] = PIXEL[(bytes[at] >> bit & 1) | (bytes[at + 1] >> bit & 1) << 1];
            }
            line[f.size] = '\n';
            line[f.size + 1] = 0;
            decoded_put(t, line);
        }
    }
    return true;
}

/* A glyph's first line: `glyph XX`, and `width N` where the font has one for it. */
static const char *read_glyph(Decoded *d, const Font *f, DecodedReading *r, const char *s, const char *end)
{
    unsigned glyph, width;
    if (!decoded_word(&s, end, "glyph") || !decoded_number(&s, end, 16, 0xFFFF, &glyph))
        return "a glyph starts `glyph` and its number in hex";
    if (glyph != r->items)
        return r->items == f->glyphs ? "more glyphs than the font has" : "the glyphs go in order: not the next";
    if (has_width(f, glyph)) {
        if (!decoded_word(&s, end, "width") || !decoded_number(&s, end, 10, WIDTH_MAX, &width))
            return "this glyph has a width: `width` and 0-255";
        decoded_bytes(d, f->widths)[glyph - f->first] = (uint8_t)width;
    }
    if (s != end)
        return has_width(f, glyph) ? "nothing goes after a glyph's width" : "this glyph has no width";
    r->items++;
    r->lines = 0;
    return NULL;
}

static const char *read_line(Decoded *d, DecodedReading *r, const char *s, const char *end)
{
    Font f = { 0 };
    font_of(r->entry, false, &f);
    if (r->items == 0 || r->lines == f.size) {
        font_of(r->entry, true, &f);
        return read_glyph(d, &f, r, s, end);
    }
    if ((size_t)(end - s) != f.size)
        return "a line of pixels is as wide as the glyph";
    uint32_t glyph = r->items - 1;
    for (unsigned x = 0; x < f.size; x++) {
        const char *pixel = memchr(PIXEL, s[x], sizeof PIXEL - 1);
        if (!pixel)
            return "a pixel is '.', '1', '2' or '3'";
        unsigned bit, color = (unsigned)(pixel - PIXEL);
        uint32_t at = pixel_at(&f, glyph, r->lines, x, &bit);
        for (unsigned plane = 0; plane < PLANES; plane++) {
            uint8_t *byte = &r->bytes[at + plane];
            *byte = (uint8_t)((*byte & ~(1u << bit)) | (color >> plane & 1) << bit);
        }
    }
    r->lines++;
    return NULL;
}

static const char *read_end(Decoded *d, DecodedReading *r)
{
    (void)d;
    Font f = { 0 };
    font_of(r->entry, false, &f);
    return r->items == f.glyphs && r->lines == f.size ? NULL : "fewer glyphs or lines than the font has";
}

const DecodedKind font_text = {
    "fonts",
    "# Chrono Trigger's fonts, from your cartridge: not for sharing.\n"
    "# A glyph a block: `glyph` and its number in hex, its width where the font has\n"
    "# widths, then a line of pixels for each of its lines: '.' clear, '1'-'3' colors.\n"
    "# Edit and start the game again; delete the file to write it afresh.\n",
    decodes, write_font, read_line, read_end, NULL, NULL,
};
