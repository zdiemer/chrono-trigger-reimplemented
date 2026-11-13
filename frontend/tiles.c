#include "tiles.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "packed.h"

/* A tile is eight lines of eight pixels; its planes go two at a time, the
 * pair's sixteen bytes a byte of each plane a line. A chunky tile of four
 * bits a pixel (Mode 7's, packed two pixels a byte for the scenes to spread)
 * is four bytes a line, the left pixel the low nibble. */
enum { TILE_SIZE = 8, PAIR_BYTES = 16, TILES_ACROSS = 16, CHUNKY_LINE_BYTES = 4, MOST_PLANES = 8 };

typedef struct {
    unsigned planes;
    bool packed, pool, chunky;
} Tiles;

static bool tiles_of(const AssetEntry *e, Tiles *t)
{
    static const struct {
        const char *codec;
        Tiles tiles;
    } codecs[] = {
        { "tiles:2bpp", { 2, false, false, false } }, { "tiles:4bpp", { 4, false, false, false } },
        { "lz:2bpp", { 2, true, false, false } },     { "lz:4bpp", { 4, true, false, false } },
        { "lz:8bpp", { 8, true, false, false } },     { "lz:4bpp-chunky", { 4, true, false, true } },
        { "tiles:plane-pool", { 3, false, true, false } },
    };
    for (size_t i = 0; i < sizeof codecs / sizeof *codecs; i++) {
        if (!strcmp(asset_codec_names[e->codec], codecs[i].codec)) {
            *t = codecs[i].tiles;
            return true;
        }
    }
    return false;
}

static uint32_t tile_bytes(Tiles t) { return t.chunky ? CHUNKY_LINE_BYTES * TILE_SIZE : t.planes * TILE_SIZE; }

static bool decodes(const AssetEntry *e, bool *named)
{
    Tiles t;
    *named = true;
    return tiles_of(e, &t);
}

/* A pixel's characters: one, or two in hex for eight planes ('..' color 0). */
static unsigned pixel_chars(unsigned planes) { return planes > 4 ? 2 : 1; }

static unsigned chunky_pixel(const uint8_t *tile, unsigned x, unsigned y)
{
    return (unsigned)(tile[y * CHUNKY_LINE_BYTES + x / 2] >> (4 * (x & 1)) & 0x0F);
}

static void set_chunky_pixel(uint8_t *tile, unsigned x, unsigned y, unsigned color)
{
    uint8_t *byte = &tile[y * CHUNKY_LINE_BYTES + x / 2];
    unsigned shift = 4 * (x & 1);
    *byte = (uint8_t)((*byte & ~(0x0Fu << shift)) | (color & 0x0F) << shift);
}

static unsigned pixel(const uint8_t *tile, unsigned planes, unsigned x, unsigned y)
{
    unsigned color = 0;
    for (unsigned p = 0; p < planes; p++)
        color |= (unsigned)(tile[p / 2 * PAIR_BYTES + y * 2 + p % 2] >> (TILE_SIZE - 1 - x) & 1) << p;
    return color;
}

static void set_pixel(uint8_t *tile, unsigned planes, unsigned x, unsigned y, unsigned color)
{
    unsigned bit = TILE_SIZE - 1 - x;
    for (unsigned p = 0; p < planes; p++) {
        uint8_t *byte = &tile[p / 2 * PAIR_BYTES + y * 2 + p % 2];
        *byte = (uint8_t)((*byte & ~(1u << bit)) | (color >> p & 1) << bit);
    }
}

static const char PIXEL[] = ".123456789ABCDEF";

static unsigned tile_pixel(Tiles t, const uint8_t *tile, unsigned x, unsigned y)
{
    return t.chunky ? chunky_pixel(tile, x, y) : pixel(tile, t.planes, x, y);
}

static void set_tile_pixel(Tiles t, uint8_t *tile, unsigned x, unsigned y, unsigned color)
{
    if (t.chunky)
        set_chunky_pixel(tile, x, y, color);
    else
        set_pixel(tile, t.planes, x, y, color);
}

/* A pixel written: '.' (or '..') for color 0, else its digit (or two). */
static char *put_pixel(char *c, unsigned planes, unsigned color)
{
    static const char HEX[] = "0123456789ABCDEF";
    if (pixel_chars(planes) == 1) {
        *c++ = PIXEL[color];
    } else if (color == 0) {
        *c++ = '.', *c++ = '.';
    } else {
        *c++ = HEX[color >> 4], *c++ = HEX[color & 0x0F];
    }
    return c;
}

/* ---- The battlefield's pool ---------------------------------------------------------------------- *
 *
 * The battlefield's tiles are kept in a pool ($D0:0000) the descriptors cut
 * into tiles (game/battle/tiles.c): each starts where a descriptor says, at
 * eight bytes a step, and is two bit planes (16 bytes) or three, the third a
 * byte a line after the first two (24). The descriptors' table is a resource
 * of its own, raw; the pool is cut as the store under the text has it. */
enum { POOL_STEP = 8, POOL_TWO_PLANES = 16, POOL_THREE_PLANES = 24, POOL_THIRD_PLANE = 16 };
enum { DESCRIPTOR_STEPS = 0x1FFF, DESCRIBES_TWO_PLANES = 0x2000 };
static const char POOL_DESCRIPTORS[] = "battle_tile_descriptors";

static unsigned pool_pixel(const uint8_t *tile, unsigned planes, unsigned x, unsigned y)
{
    unsigned color = pixel(tile, 2, x, y);
    if (planes == 3)
        color |= (unsigned)(tile[POOL_THIRD_PLANE + y] >> (TILE_SIZE - 1 - x) & 1) << 2;
    return color;
}

static void set_pool_pixel(uint8_t *tile, unsigned planes, unsigned x, unsigned y, unsigned color)
{
    set_pixel(tile, 2, x, y, color & 3);
    if (planes == 3) {
        unsigned bit = TILE_SIZE - 1 - x;
        tile[POOL_THIRD_PLANE + y] = (uint8_t)((tile[POOL_THIRD_PLANE + y] & ~(1u << bit)) | (color >> 2 & 1) << bit);
    }
}

/* The planes of the tile starting at each step of the pool (`steps` of
 * them), 0 where none does: false where two descriptors start one there with
 * different planes. A descriptor past the pool cuts nothing of it. */
static bool pool_cut(const uint8_t *descriptors, uint32_t n, uint8_t *cut, uint32_t steps)
{
    memset(cut, 0, steps);
    for (uint32_t i = 0; descriptors && i + 1 < n; i += 2) {
        unsigned word = (unsigned)(descriptors[i] | descriptors[i + 1] << 8), step = word & DESCRIPTOR_STEPS;
        uint8_t planes = word & DESCRIBES_TWO_PLANES ? 2 : 3;
        if (step >= steps)
            continue;
        if (cut[step] && cut[step] != planes)
            return false;
        cut[step] = planes;
    }
    return descriptors != NULL;
}

static uint32_t pool_size(unsigned planes) { return planes == 2 ? POOL_TWO_PLANES : POOL_THREE_PLANES; }

/* The tile at `at` in a pool of `len` bytes: its planes, or 0 where none
 * starts there whole, before the next does. */
static unsigned pool_tile(const uint8_t *cut, uint32_t len, uint32_t at)
{
    unsigned planes = at % POOL_STEP == 0 && at < len ? cut[at / POOL_STEP] : 0;
    if (!planes || at + pool_size(planes) > len)
        return 0;
    for (uint32_t step = at / POOL_STEP + 1; step < (at + pool_size(planes)) / POOL_STEP; step++)
        if (cut[step])
            return 0;
    return planes;
}

static void write_pool(DecodedText *t, const uint8_t *bytes, uint32_t len, const uint8_t *cut)
{
    char line[TILES_ACROSS * (TILE_SIZE + 1) + 2];
    uint32_t at = 0, first = 0;
    while (pool_tile(cut, len, at)) {
        uint32_t offsets[TILES_ACROSS], across = 0;
        unsigned planes[TILES_ACROSS];
        for (; across < TILES_ACROSS && (planes[across] = pool_tile(cut, len, at)) != 0; across++) {
            offsets[across] = at;
            at += pool_size(planes[across]);
        }
        snprintf(line, sizeof line, "# %04X\n", (unsigned)first);
        decoded_put(t, line);
        for (unsigned y = 0; y < TILE_SIZE; y++) {
            char *c = line;
            for (uint32_t k = 0; k < across; k++) {
                if (k)
                    *c++ = ' ';
                for (unsigned x = 0; x < TILE_SIZE; x++)
                    *c++ = PIXEL[pool_pixel(bytes + offsets[k], planes[k], x, y)];
            }
            *c++ = '\n';
            *c = 0;
            decoded_put(t, line);
        }
        first += across;
    }
    for (; at < len; at += TILES_ACROSS) {   /* what no tile is cut from */
        decoded_put(t, "rest");
        for (uint32_t k = at; k < len && k < at + TILES_ACROSS; k++) {
            snprintf(line, sizeof line, " %02X", bytes[k]);
            decoded_put(t, line);
        }
        decoded_put(t, "\n");
    }
}

/* ---- Writing ----------------------------------------------------------------------------------- */

static void write_tiles(DecodedText *t, const uint8_t *bytes, uint32_t count, Tiles tiles)
{
    char line[TILES_ACROSS * (2 * TILE_SIZE + 1) + 2];
    for (uint32_t first = 0; first < count; first += TILES_ACROSS) {
        uint32_t across = count - first < TILES_ACROSS ? count - first : TILES_ACROSS;
        snprintf(line, sizeof line, "# %04X\n", (unsigned)first);
        decoded_put(t, line);
        for (unsigned y = 0; y < TILE_SIZE; y++) {
            char *c = line;
            for (uint32_t k = 0; k < across; k++) {
                const uint8_t *tile = bytes + (first + k) * tile_bytes(tiles);
                if (k)
                    *c++ = ' ';
                for (unsigned x = 0; x < TILE_SIZE; x++)
                    c = put_pixel(c, tiles.planes, tile_pixel(tiles, tile, x, y));
            }
            *c++ = '\n';
            *c = 0;
            decoded_put(t, line);
        }
    }
}

static bool write_resource(DecodedText *t, const AssetStore *store, const AssetEntry *e, const uint8_t *bytes)
{
    Tiles tiles = { 0 };
    tiles_of(e, &tiles);
    if (tiles.pool) {
        uint32_t n = 0;
        const uint8_t *descriptors = store->find(store->ctx, POOL_DESCRIPTORS, "words", &n);
        uint8_t *cut = malloc(e->length / POOL_STEP + 1);
        bool written = cut && pool_cut(descriptors, n, cut, e->length / POOL_STEP);
        if (written) {
            decoded_put(t, "# tiles of two or three bit planes, as the descriptors cut the pool\n");
            write_pool(t, bytes, e->length, cut);
        }
        free(cut);
        return written;
    }
    size_t len = e->length;
    bool long_runs = false;
    uint8_t *unpacked = tiles.packed ? packed_unpack(bytes, e->length, &len, &long_runs) : NULL;
    if (tiles.packed && !unpacked)
        return false;
    bool whole = len % tile_bytes(tiles) == 0;
    if (whole) {
        char line[80];
        uint32_t count = (uint32_t)(len / tile_bytes(tiles));
        if (tiles.chunky)
            snprintf(line, sizeof line, "# %u tiles, %u bits a pixel, chunky%s\n", (unsigned)count, tiles.planes,
                     tiles.packed ? ", packed" : "");
        else
            snprintf(line, sizeof line, "# %u tiles, %u bit planes%s\n", (unsigned)count, tiles.planes,
                     tiles.packed ? ", packed" : "");
        decoded_put(t, line);
        if (tiles.packed)
            packed_write_form(t, long_runs);
        write_tiles(t, unpacked ? unpacked : bytes, count, tiles);
    }
    free(unpacked);
    return whole;
}

/* ---- Reading ----------------------------------------------------------------------------------- */

/* The tiles a line of pixels has (`chars` a pixel): how many, NULL where it is wrong. */
static const char *count_tiles(const char *s, const char *end, unsigned chars, uint32_t *across)
{
    *across = 0;
    while (s < end) {
        const char *tile = s;
        while (s < end && *s != ' ' && *s != '\t')
            s++;
        if (s - tile != (ptrdiff_t)(TILE_SIZE * chars))
            return chars == 1 ? "a tile's line is 8 pixels, a space between tiles"
                              : "a tile's line is 8 pixels of two characters each, a space between tiles";
        if (++*across > TILES_ACROSS)
            return "at most 16 tiles a line";
        while (s < end && (*s == ' ' || *s == '\t'))
            s++;
    }
    return NULL;
}

/* A pixel's characters from *s: its color. */
static bool read_pixel(const char **s, unsigned planes, unsigned *color)
{
    if (pixel_chars(planes) == 1) {
        char c = **s >= 'a' && **s <= 'f' ? (char)(**s - 'a' + 'A') : **s;
        const char *at = memchr(PIXEL, c, (size_t)1 << planes);
        (*s)++;
        *color = at ? (unsigned)(at - PIXEL) : 0;
        return at != NULL;
    }
    const char *p = *s;
    *s += 2;
    if (p[0] == '.' && p[1] == '.') {
        *color = 0;
        return true;
    }
    unsigned v;
    const char *q = p;
    if (!decoded_number(&q, p + 2, 16, 0xFF, &v) || q != p + 2 || v == 0)
        return false;
    *color = v;
    return true;
}

/* The pool's parts: its tiles, then the bytes no tile is cut from. */
enum { POOL_TILES, POOL_REST };

static const char *read_rest(DecodedReading *r, const char *s, const char *end)
{
    uint32_t len = asset_catalogue[r->entry].length;
    unsigned v;
    if (r->lines)
        return "a row of tiles is eight lines";
    r->part = POOL_REST;
    while (s < end && *s == ' ')
        s++;
    while (s < end) {
        if (!decoded_number(&s, end, 16, 0xFF, &v))
            return "the rest is bytes in hex";
        if (r->at >= len)
            return "more bytes than the pool has";
        r->bytes[r->at++] = (uint8_t)v;
        while (s < end && *s == ' ')
            s++;
    }
    return NULL;
}

static const char *read_pool_line(Decoded *d, DecodedReading *r, const char *s, const char *end)
{
    uint32_t len = asset_catalogue[r->entry].length, across = 0;
    if (r->made_len == 0) {   /* the cut, kept beside the bytes being made */
        uint16_t hint = 0;
        int descriptors = decoded_entry(POOL_DESCRIPTORS, sizeof POOL_DESCRIPTORS - 1, &hint);
        uint8_t *cut = decoded_grow(r, len / POOL_STEP + 1);
        if (!cut || descriptors < 0 ||
            !pool_cut(decoded_beneath(d, descriptors), asset_catalogue[descriptors].length, cut, len / POOL_STEP))
            return "the store under the text hasn't the descriptors to cut the pool";
    }
    const char *word = s;
    if (decoded_word(&word, end, "rest"))
        return read_rest(r, word, end);
    if (r->part == POOL_REST)
        return "nothing but the rest after the rest";
    const char *wrong = count_tiles(s, end, 1, &across);
    if (wrong)
        return wrong;
    if (r->lines == 0) {
        if (r->items > 0 && r->across < TILES_ACROSS)
            return "only the last row of tiles has fewer than 16";
        r->across = across;
    } else if (across != r->across) {
        return "each line of a row has the row's tiles";
    }
    uint32_t at = r->at;
    for (uint32_t k = 0; k < across; k++) {
        unsigned planes = pool_tile(r->made, len, at);
        if (!planes)
            return "more tiles than the descriptors cut the pool into";
        while (*s == ' ' || *s == '\t')
            s++;
        for (unsigned x = 0; x < TILE_SIZE; x++, s++) {
            const char *p = memchr(PIXEL, *s, (size_t)1 << planes);
            if (!p)
                return planes == 2 ? "a two-plane tile's pixel is '.' or '1'-'3'"
                                   : "a three-plane tile's pixel is '.' or '1'-'7'";
            set_pool_pixel(r->bytes + at, planes, x, r->lines, (unsigned)(p - PIXEL));
        }
        at += pool_size(planes);
    }
    if (++r->lines == TILE_SIZE) {
        r->items += across;
        r->at = at;
        r->lines = 0;
    }
    return NULL;
}

static const char *read_line(Decoded *d, DecodedReading *r, const char *s, const char *end)
{
    Tiles tiles = { 0 };
    tiles_of(&asset_catalogue[r->entry], &tiles);
    if (tiles.pool)
        return read_pool_line(d, r, s, end);
    if (tiles.packed && r->form == PACKED_UNSAID)
        return packed_read_form(d, r, s, end);
    uint32_t across = 0, size = tile_bytes(tiles);
    const char *wrong = count_tiles(s, end, pixel_chars(tiles.planes), &across);
    if (wrong)
        return wrong;
    if (r->lines == 0) {   /* a row of tiles starts */
        if (r->items > 0 && r->across < TILES_ACROSS)
            return "only the last row of tiles has fewer than 16";
        if (!tiles.packed && r->items + across > asset_catalogue[r->entry].length / size)
            return "more tiles than the resource has";
        if (tiles.packed && (r->items + across) * size > r->most)
            return "more tiles than the cartridge's stream unpacks to";
        if (tiles.packed && !decoded_grow(r, across * size))
            return "out of memory";
        r->across = across;
    } else if (across != r->across) {
        return "each line of a row has the row's tiles";
    }
    uint8_t *row = (tiles.packed ? r->made : r->bytes) + r->items * size;
    for (uint32_t k = 0; k < across; k++) {
        while (*s == ' ' || *s == '\t')
            s++;
        for (unsigned x = 0; x < TILE_SIZE; x++) {
            unsigned color;
            if (!read_pixel(&s, tiles.planes, &color))
                return tiles.planes == 2   ? "a pixel is '.' or '1'-'3'"
                       : tiles.planes == 4 ? "a pixel is '.', '1'-'9' or 'A'-'F'"
                                           : "a pixel is '..' or two hex digits, 01-FF";
            set_tile_pixel(tiles, row + k * size, x, r->lines, color);
        }
    }
    if (++r->lines == TILE_SIZE) {
        r->items += across;
        r->lines = 0;
    }
    return NULL;
}

static const char *read_end(Decoded *d, DecodedReading *r)
{
    const AssetEntry *e = &asset_catalogue[r->entry];
    Tiles tiles = { 0 };
    tiles_of(e, &tiles);
    if (r->lines)
        return "a row of tiles is eight lines";
    if (tiles.pool)
        return r->at == e->length ? NULL : "fewer tiles and bytes than the pool has";
    if (!tiles.packed)
        return r->items == e->length / tile_bytes(tiles) ? NULL : "fewer tiles than the resource has";
    return packed_end(d, r);
}

const DecodedKind tile_text = {
    "graphics",
    "# Chrono Trigger's graphics, from your cartridge: not for sharing.\n"
    "# 8x8 tiles, sixteen a row, a character a pixel: '.' color 0, then '1'-'3', or\n"
    "# '1'-'9' and 'A'-'F' for four bits a pixel; two for eight bit planes, '..' and\n"
    "# then 01-FF. A packed resource says how it packs\n"
    "# first; edited, it is packed again as it is read, and must still fit.\n"
    "# Edit and start the game again; delete the file to write it afresh.\n",
    decodes, write_resource, read_line, read_end, NULL, NULL,
};
