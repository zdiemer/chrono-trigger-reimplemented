#include "tiles.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lz.h"

/* A tile is eight lines of eight pixels; its planes go two at a time, the
 * pair's sixteen bytes a byte of each plane a line. */
enum { TILE_SIZE = 8, PAIR_BYTES = 16, TILES_ACROSS = 16 };

/* How a packed resource's stream runs, as its text says (DecodedReading.form). */
enum { FORM_UNSAID, FORM_SHORT_RUNS, FORM_LONG_RUNS };

typedef struct {
    unsigned planes;
    bool packed;
} Tiles;

static bool tiles_of(const AssetEntry *e, Tiles *t)
{
    static const struct {
        const char *codec;
        Tiles tiles;
    } codecs[] = {
        { "tiles:2bpp", { 2, false } }, { "tiles:4bpp", { 4, false } },
        { "lz:2bpp", { 2, true } },     { "lz:4bpp", { 4, true } },
    };
    for (size_t i = 0; i < sizeof codecs / sizeof *codecs; i++) {
        if (!strcmp(asset_codec_names[e->codec], codecs[i].codec)) {
            *t = codecs[i].tiles;
            return true;
        }
    }
    return false;
}

static uint32_t tile_bytes(Tiles t) { return t.planes * TILE_SIZE; }

static bool decodes(const AssetEntry *e, bool *named)
{
    Tiles t;
    *named = true;
    return tiles_of(e, &t);
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

/* ---- Writing ----------------------------------------------------------------------------------- */

static void write_tiles(DecodedText *t, const uint8_t *bytes, uint32_t count, unsigned planes)
{
    char line[TILES_ACROSS * (TILE_SIZE + 1) + 2];
    for (uint32_t first = 0; first < count; first += TILES_ACROSS) {
        uint32_t across = count - first < TILES_ACROSS ? count - first : TILES_ACROSS;
        snprintf(line, sizeof line, "# %04X\n", (unsigned)first);
        decoded_put(t, line);
        for (unsigned y = 0; y < TILE_SIZE; y++) {
            char *c = line;
            for (uint32_t k = 0; k < across; k++) {
                const uint8_t *tile = bytes + (first + k) * planes * TILE_SIZE;
                if (k)
                    *c++ = ' ';
                for (unsigned x = 0; x < TILE_SIZE; x++)
                    *c++ = PIXEL[pixel(tile, planes, x, y)];
            }
            *c++ = '\n';
            *c = 0;
            decoded_put(t, line);
        }
    }
}

static bool write_resource(DecodedText *t, const AssetStore *store, const AssetEntry *e, const uint8_t *bytes)
{
    (void)store;
    Tiles tiles = { 0 };
    tiles_of(e, &tiles);
    const uint8_t *image = bytes;
    uint8_t *unpacked = NULL;
    size_t len = e->length;
    bool long_runs = false;
    if (tiles.packed) {
        unpacked = malloc(LZ_MOST);
        if (!unpacked || !lz_unpack(bytes, e->length, unpacked, &len, &long_runs)) {
            free(unpacked);
            return false;
        }
        image = unpacked;
    }
    bool whole = len % tile_bytes(tiles) == 0;
    if (whole) {
        char line[80];
        uint32_t count = (uint32_t)(len / tile_bytes(tiles));
        snprintf(line, sizeof line, "# %u tiles, %u bit planes%s\n", (unsigned)count, tiles.planes,
                 tiles.packed ? ", packed" : "");
        decoded_put(t, line);
        if (tiles.packed)
            decoded_put(t, long_runs ? "packed long\n" : "packed short\n");
        write_tiles(t, image, count, tiles.planes);
    }
    free(unpacked);
    return whole;
}

/* ---- Reading ----------------------------------------------------------------------------------- */

static const char *read_form(DecodedReading *r, const char *s, const char *end)
{
    if (!decoded_word(&s, end, "packed"))
        return "a packed resource starts `packed short` or `packed long`";
    if (decoded_word(&s, end, "short"))
        r->form = FORM_SHORT_RUNS;
    else if (decoded_word(&s, end, "long"))
        r->form = FORM_LONG_RUNS;
    else
        return "its runs are `short` or `long`";
    return s == end ? NULL : "nothing goes after a stream's runs";
}

/* The tiles a line of pixels has: how many, NULL where it is wrong. */
static const char *count_tiles(const char *s, const char *end, uint32_t *across)
{
    *across = 0;
    while (s < end) {
        const char *tile = s;
        while (s < end && *s != ' ' && *s != '\t')
            s++;
        if (s - tile != TILE_SIZE)
            return "a tile's line is 8 pixels, a space between tiles";
        if (++*across > TILES_ACROSS)
            return "at most 16 tiles a line";
        while (s < end && (*s == ' ' || *s == '\t'))
            s++;
    }
    return NULL;
}

static const char *read_line(Decoded *d, DecodedReading *r, const char *s, const char *end)
{
    (void)d;
    Tiles tiles = { 0 };
    tiles_of(&asset_catalogue[r->entry], &tiles);
    if (tiles.packed && r->form == FORM_UNSAID)
        return read_form(r, s, end);
    uint32_t across = 0, size = tile_bytes(tiles);
    const char *wrong = count_tiles(s, end, &across);
    if (wrong)
        return wrong;
    if (r->lines == 0) {   /* a row of tiles starts */
        if (r->items > 0 && r->across < TILES_ACROSS)
            return "only the last row of tiles has fewer than 16";
        if (!tiles.packed && r->items + across > asset_catalogue[r->entry].length / size)
            return "more tiles than the resource has";
        if (tiles.packed && ((r->items + across) * size > LZ_MOST || !decoded_grow(r, across * size)))
            return "more tiles than a stream unpacks to";
        r->across = across;
    } else if (across != r->across) {
        return "each line of a row has the row's tiles";
    }
    uint8_t *row = (tiles.packed ? r->made : r->bytes) + r->items * size;
    for (uint32_t k = 0; k < across; k++) {
        while (*s == ' ' || *s == '\t')
            s++;
        for (unsigned x = 0; x < TILE_SIZE; x++, s++) {
            char c = *s >= 'a' && *s <= 'f' ? (char)(*s - 'a' + 'A') : *s;
            const char *at = memchr(PIXEL, c, (size_t)1 << tiles.planes);
            if (!at)
                return tiles.planes == 2 ? "a pixel is '.' or '1'-'3'" : "a pixel is '.', '1'-'9' or 'A'-'F'";
            set_pixel(row + k * size, tiles.planes, x, r->lines, (unsigned)(at - PIXEL));
        }
    }
    if (++r->lines == TILE_SIZE) {
        r->items += across;
        r->lines = 0;
    }
    return NULL;
}

/* Whether a packed resource's tiles are the store beneath's, unpacked: if
 * so its bytes are taken as they are, which is what packing them again
 * would make (lz_test), only sooner. */
static bool unchanged(const Decoded *d, const DecodedReading *r)
{
    const AssetEntry *e = &asset_catalogue[r->entry];
    const uint8_t *was = decoded_beneath(d, r->entry);
    uint8_t *unpacked = was ? malloc(LZ_MOST) : NULL;
    size_t len = 0;
    bool long_runs = false;
    bool same = unpacked && lz_unpack(was, e->length, unpacked, &len, &long_runs) && len == r->made_len &&
                long_runs == (r->form == FORM_LONG_RUNS) && !memcmp(unpacked, r->made, len);
    free(unpacked);
    if (same)
        memcpy(r->bytes, was, e->length);
    return same;
}

static const char *read_end(Decoded *d, DecodedReading *r)
{
    const AssetEntry *e = &asset_catalogue[r->entry];
    Tiles tiles = { 0 };
    tiles_of(e, &tiles);
    if (r->lines)
        return "a row of tiles is eight lines";
    if (!tiles.packed)
        return r->items == e->length / tile_bytes(tiles) ? NULL : "fewer tiles than the resource has";
    if (r->form == FORM_UNSAID)
        return "a packed resource starts `packed short` or `packed long`";
    if (!unchanged(d, r) && !lz_pack(r->made, r->made_len, r->form == FORM_LONG_RUNS, r->bytes, e->length))
        return "packed, these tiles take more room than the cartridge has for them";
    return NULL;
}

const DecodedKind tile_text = {
    "graphics",
    "# Chrono Trigger's graphics, from your cartridge: not for sharing.\n"
    "# 8x8 tiles, sixteen a row, a character a pixel: '.' color 0, then '1'-'3', or\n"
    "# '1'-'9' and 'A'-'F' for four bit planes. A packed resource says how it packs\n"
    "# first; edited, it is packed again as it is read, and must still fit.\n"
    "# Edit and start the game again; delete the file to write it afresh.\n",
    decodes, write_resource, read_line, read_end,
};
