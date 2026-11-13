#include "maps.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "packed.h"

/* ---- Cells ------------------------------------------------------------------------------------- */

typedef enum { CELL_BYTE, CELL_NIBBLES, CELL_ENTRY, CELL_COLOR, CELL_ATTRIBUTES } Cell;

/* A tilemap entry: the tile's number in ten bits, its palette in three, and
 * whether it is in front, flipped across and flipped down. */
enum {
    ENTRY_TILE = 0x03FF,
    ENTRY_PALETTE_SHIFT = 10,
    ENTRY_PALETTE = 0x07,
    ENTRY_FRONT = 0x2000,
    ENTRY_ACROSS = 0x4000,
    ENTRY_DOWN = 0x8000,
};

/* A color's word: five bits each of red, green and blue from the bottom, and
 * a top bit the hardware ignores. */
enum { COLOR_MAX = 31, GREEN_SHIFT = 5, BLUE_SHIFT = 10, COLOR_TOP_BIT = 0x8000 };

/* A tile's attributes' first byte: its properties, and the bit that starts a
 * run of the same attributes where they are packed. */
enum { ATTRIBUTE_RUN = 0x80, RUN_OF_256 = 0x00, LONGEST_ATTRIBUTE_RUN = 256 };

/* The shape a codec gives: what a cell is, how many blocks go across, and a
 * block's cells each way (a look's two by two, or one). */
typedef struct {
    Cell cell;
    unsigned across, block;
    bool field_map;
} Shape;

static unsigned cell_bytes(Cell c) { return c == CELL_ATTRIBUTES ? 3 : c == CELL_ENTRY || c == CELL_COLOR ? 2 : 1; }
static uint32_t block_bytes(Shape s) { return s.block * s.block * cell_bytes(s.cell); }

static bool counted(const char *codec, const char *prefix, unsigned *n)
{
    size_t len = strlen(prefix);
    if (strncmp(codec, prefix, len) != 0)
        return false;
    char *end;
    unsigned long v = strtoul(codec + len, &end, 10);
    *n = (unsigned)v;
    return *end == 0 && v > 0 && v <= 256;
}

static bool shape_of(const AssetEntry *e, Shape *s)
{
    const char *c = asset_codec_names[e->codec];
    unsigned n = 0;
    *s = (Shape){ CELL_BYTE, 16, 1, false };
    if (!strcmp(c, "lz") || !strcmp(c, "lz:event"))
        return true;
    if (!strcmp(c, "lz:field-map")) {
        s->field_map = true;
        s->cell = CELL_ATTRIBUTES;
        return true;
    }
    if (!strcmp(c, "lz:looks")) {
        *s = (Shape){ CELL_ENTRY, 8, 2, false };
        return true;
    }
    if (!strcmp(c, "lz:chunky")) {   /* mode 7's tiles: 8x8 bytes, a byte a pixel */
        *s = (Shape){ CELL_BYTE, 4, 8, false };
        return true;
    }
    if (counted(c, "lz:bytes:", &n))
        *s = (Shape){ CELL_BYTE, n, 1, false };
    else if (counted(c, "lz:nibbles:", &n) && n % 2 == 0)
        *s = (Shape){ CELL_NIBBLES, n / 2, 1, false };
    else if (counted(c, "lz:tilemap:", &n))
        *s = (Shape){ CELL_ENTRY, n, 1, false };
    else if (counted(c, "lz:colors:", &n))
        *s = (Shape){ CELL_COLOR, n, 1, false };
    else
        return false;
    return true;
}

static bool decodes(const AssetEntry *e, bool *named)
{
    Shape s;
    *named = true;
    return shape_of(e, &s);
}

static void put_cell(DecodedText *t, Cell c, const uint8_t *p, bool first)
{
    char s[24];
    unsigned w = p[0] | (cell_bytes(c) > 1 ? (unsigned)p[1] << 8 : 0);
    if (!first && c != CELL_NIBBLES)
        decoded_put(t, " ");
    if (c == CELL_ENTRY)
        snprintf(s, sizeof s, "%03X:%u:%c%c%c", w & ENTRY_TILE, w >> ENTRY_PALETTE_SHIFT & ENTRY_PALETTE,
                 w & ENTRY_ACROSS ? 'h' : '-', w & ENTRY_DOWN ? 'v' : '-', w & ENTRY_FRONT ? 'p' : '-');
    else if (c == CELL_COLOR)
        snprintf(s, sizeof s, "%02u.%02u.%02u%s", w & COLOR_MAX, w >> GREEN_SHIFT & COLOR_MAX,
                 w >> BLUE_SHIFT & COLOR_MAX, w & COLOR_TOP_BIT ? "+" : "");
    else if (c == CELL_ATTRIBUTES)
        snprintf(s, sizeof s, "%02X%02X%02X", p[0], p[1], p[2]);
    else
        snprintf(s, sizeof s, "%02X", p[0]);
    decoded_put(t, s);
}

static int hex_digit(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* `digits` hex digits from s. */
static bool hex(const char *s, unsigned digits, unsigned *v)
{
    *v = 0;
    for (unsigned i = 0; i < digits; i++) {
        int d = hex_digit(s[i]);
        if (d < 0)
            return false;
        *v = *v << 4 | (unsigned)d;
    }
    return true;
}

static bool decimal2(const char *s, unsigned most, unsigned *v)
{
    if (s[0] < '0' || s[0] > '9' || s[1] < '0' || s[1] > '9')
        return false;
    *v = (unsigned)(s[0] - '0') * 10 + (unsigned)(s[1] - '0');
    return *v <= most;
}

static bool flag(char c, char set, unsigned bit, unsigned *w)
{
    if (c == set)
        *w |= bit;
    return c == set || c == '-';
}

/* A cell, the `n` characters from s, into p: NULL, or what is wrong. */
static const char *read_cell(Cell c, const char *s, size_t n, uint8_t *p)
{
    unsigned v = 0, w = 0;
    if (c == CELL_ENTRY) {
        unsigned palette;
        if (n != 9 || !hex(s, 3, &v) || v > ENTRY_TILE || s[3] != ':' || s[4] < '0' || s[4] > '7' || s[5] != ':')
            return "a tilemap entry is TTT:P:hvp, the tile 000-3FF, its palette 0-7, '-' for a flag not set";
        palette = (unsigned)(s[4] - '0');
        w = v | palette << ENTRY_PALETTE_SHIFT;
        if (!flag(s[6], 'h', ENTRY_ACROSS, &w) || !flag(s[7], 'v', ENTRY_DOWN, &w) || !flag(s[8], 'p', ENTRY_FRONT, &w))
            return "a tilemap entry is TTT:P:hvp, the tile 000-3FF, its palette 0-7, '-' for a flag not set";
    } else if (c == CELL_COLOR) {
        unsigned red, green, blue;
        if ((n != 8 && !(n == 9 && s[8] == '+')) || !decimal2(s, COLOR_MAX, &red) || s[2] != '.' ||
            !decimal2(s + 3, COLOR_MAX, &green) || s[5] != '.' || !decimal2(s + 6, COLOR_MAX, &blue))
            return "a color is RR.GG.BB, 00-31 each, and maybe '+'";
        w = red | green << GREEN_SHIFT | blue << BLUE_SHIFT | (n == 9 ? COLOR_TOP_BIT : 0);
    } else if (c == CELL_ATTRIBUTES) {
        if (n != 6 || !hex(s, 6, &v) || v >> 16 & ATTRIBUTE_RUN)
            return "a tile's attributes are six hex digits, the first two at most 7F";
        p[0] = (uint8_t)(v >> 16);
        p[1] = (uint8_t)(v >> 8);
        p[2] = (uint8_t)v;
        return NULL;
    } else {
        if (n != 2 || !hex(s, 2, &v))
            return c == CELL_NIBBLES ? "a row is hex digits, two to a byte" : "a byte is two hex digits";
        p[0] = (uint8_t)v;
        return NULL;
    }
    p[0] = (uint8_t)w;
    p[1] = (uint8_t)(w >> 8);
    return NULL;
}

static bool space(char c) { return c == ' ' || c == '\t'; }

/* The next cell of a line from *s: where it starts, and its length; false at
 * the line's end. A row of nibbles has no spaces: a byte is its two digits. */
static bool next_cell(const char **s, const char *end, Cell c, const char **cell, size_t *n)
{
    if (c == CELL_NIBBLES) {
        *cell = *s;
        *n = end - *s >= 2 ? 2 : (size_t)(end - *s);
        *s += *n;
        return *n > 0;
    }
    while (*s < end && space(**s))
        (*s)++;
    *cell = *s;
    while (*s < end && !space(**s))
        (*s)++;
    *n = (size_t)(*s - *cell);
    return *n > 0;
}

static uint32_t cells_in(const char *s, const char *end, Cell c)
{
    const char *cell;
    size_t n;
    uint32_t count = 0;
    while (next_cell(&s, end, c, &cell, &n))
        count++;
    return count;
}

/* The bytes in hex after `rest`, into p (room for `most`): how many, or -1. */
static int read_rest(const char *s, const char *end, uint8_t *p, unsigned most)
{
    const char *cell;
    size_t n;
    int count = 0;
    while (next_cell(&s, end, CELL_BYTE, &cell, &n)) {
        if ((unsigned)count == most || read_cell(CELL_BYTE, cell, n, p + count) != NULL)
            return -1;
        count++;
    }
    return count;
}

/* ---- A grid of cells --------------------------------------------------------------------------- */

enum { COMMENT_EVERY = 16 };

static void write_grid(DecodedText *t, Shape s, const uint8_t *bytes, size_t len)
{
    uint32_t size = block_bytes(s), blocks = (uint32_t)(len / size), cell = cell_bytes(s.cell);
    char line[48];
    for (uint32_t first = 0, row = 0; first < blocks; first += s.across, row++) {
        uint32_t n = blocks - first < s.across ? blocks - first : s.across;
        if (s.block > 1)
            snprintf(line, sizeof line, s.cell == CELL_ENTRY ? "# look %03X\n" : "# tile %03X\n",
                     (unsigned)first);
        else if (row % COMMENT_EVERY == 0)
            snprintf(line, sizeof line, "# row %u\n", (unsigned)row);
        if (s.block > 1 || row % COMMENT_EVERY == 0)
            decoded_put(t, line);
        for (unsigned y = 0; y < s.block; y++) {
            for (uint32_t k = 0; k < n * s.block; k++) {
                uint32_t at = (first + k / s.block) * size + (y * s.block + k % s.block) * cell;
                put_cell(t, s.cell, bytes + at, k == 0);
            }
            decoded_put(t, "\n");
        }
    }
    if (blocks * size < len) {
        decoded_put(t, "rest");
        for (size_t at = blocks * size; at < len; at++)
            put_cell(t, CELL_BYTE, bytes + at, false);
        decoded_put(t, "\n");
    }
}

/* Parts of a grid: its rows, then the bytes left over. */
enum { GRID_ROWS, GRID_REST };

/* A line of a grid that starts `base` bytes into what the resource makes; a
 * grid `capped` makes no more than the stream beneath unpacks to as it goes
 * (a map's attributes are packed again before they are measured). */
static const char *read_grid_line(DecodedReading *r, Shape s, uint32_t base, bool capped, const char *line,
                                  const char *end)
{
    uint32_t size = block_bytes(s), cell = cell_bytes(s.cell);
    const char *at = line;
    if (r->part == GRID_REST)
        return "nothing goes after the rest";
    if (decoded_word(&at, end, "rest")) {
        if (r->lines)
            return "a row of looks or tiles is all its lines";
        uint8_t left[64];
        int n = read_rest(at, end, left, size - 1);
        if (n <= 0)
            return "the rest is fewer bytes in hex than a cell has";
        uint8_t *p = decoded_grow(r, (uint32_t)n);
        if (!p)
            return "out of memory";
        memcpy(p, left, (size_t)n);
        if (r->made_len > r->most)
            return "more than the cartridge's stream unpacks to";
        r->part = GRID_REST;
        return NULL;
    }
    uint32_t cells = cells_in(line, end, s.cell);
    if (cells % s.block)
        return s.cell == CELL_ENTRY ? "a look is two cells across" : "a tile is eight bytes across";
    uint32_t blocks = cells / s.block;
    if (r->lines == 0) {   /* a row starts */
        if (r->items > 0 && r->across < s.across)
            return "only the last row is short";
        if (blocks > s.across)
            return "more cells than a row has";
        if (!decoded_grow(r, blocks * size))
            return "out of memory";
        if (capped && r->made_len > r->most)
            return "more than the cartridge's stream unpacks to";
        r->across = blocks;
    } else if (blocks != r->across) {
        return "each line of a row has the row's looks or tiles";
    }
    const char *c;
    size_t n;
    for (uint32_t k = 0; next_cell(&line, end, s.cell, &c, &n); k++) {
        uint32_t place = base + (r->items + k / s.block) * size + (r->lines * s.block + k % s.block) * cell;
        const char *wrong = read_cell(s.cell, c, n, r->made + place);
        if (wrong)
            return wrong;
    }
    if (++r->lines == s.block) {
        r->items += r->across;
        r->lines = 0;
    }
    return NULL;
}

/* ---- A location's map ------------------------------------------------------------------------- */

/* The header's six bytes: BG1's and BG2's sizes; BG3's and how the layers
 * move; BG2's and BG3's drift; the screens; color math. */
enum { HEADER_SIZES, HEADER_LAYERS, HEADER_BG2_DRIFT, HEADER_BG3_DRIFT, HEADER_SCREENS, HEADER_COLOR_MATH, HEADER_BYTES };
enum { HAS_BG3 = 0x80, BG2_AT_HALF_PACE = 0x40, BG3_STILL = 0x20, BG2_STILL = 0x10 };
enum { SIZE_BITS = 0x03, TILES_A_STEP = 16, LAYERS = 3 };

/* A layer's size each way, from its bits. */
static unsigned columns_of(const uint8_t *h, unsigned layer)
{
    unsigned bits = layer == 2 ? h[HEADER_LAYERS] : h[HEADER_SIZES] >> (layer == 1 ? 4 : 0);
    return ((bits & SIZE_BITS) + 1) * TILES_A_STEP;
}

static unsigned rows_of(const uint8_t *h, unsigned layer)
{
    unsigned bits = layer == 2 ? h[HEADER_LAYERS] >> 2 : h[HEADER_SIZES] >> (layer == 1 ? 6 : 2);
    return ((bits & SIZE_BITS) + 1) * TILES_A_STEP;
}

static bool has_layer(const uint8_t *h, unsigned layer) { return layer < 2 || (h[HEADER_LAYERS] & HAS_BG3); }

static uint32_t attributes_at(const uint8_t *h)
{
    uint32_t at = HEADER_BYTES;
    for (unsigned layer = 0; layer < LAYERS; layer++)
        if (has_layer(h, layer))
            at += columns_of(h, layer) * rows_of(h, layer);
    return at;
}

/* `n` tiles' attributes, three bytes each, packed into runs as the game reads
 * them: the packed length. Packed in place, `out` being `cells` or before. */
static uint32_t pack_attributes(const uint8_t *cells, uint32_t n, uint8_t *out)
{
    uint32_t len = 0;
    for (uint32_t i = 0; i < n;) {
        uint8_t a[3];
        memcpy(a, cells + 3 * i, 3);
        uint32_t run = 1;
        while (i + run < n && run < LONGEST_ATTRIBUTE_RUN && !memcmp(cells + 3 * (i + run), a, 3))
            run++;
        out[len++] = (uint8_t)(a[0] | (run > 1 ? ATTRIBUTE_RUN : 0));
        out[len++] = a[1];
        out[len++] = a[2];
        if (run > 1)
            out[len++] = run == LONGEST_ATTRIBUTE_RUN ? RUN_OF_256 : (uint8_t)run;
        i += run;
    }
    return len;
}

/* The attributes after a map's layers, unpacked into `cells` (room for
 * BG1's tiles): how many tiles, or -1 where they aren't what packing them
 * again would give. */
static int32_t unpack_attributes(const uint8_t *packed, size_t len, uint32_t most, uint8_t *cells, uint8_t *again)
{
    uint32_t n = 0;
    size_t at = 0;
    while (at < len && n < most) {
        if (at + 3 > len)
            return -1;
        uint32_t run = 1;
        if (packed[at] & ATTRIBUTE_RUN) {
            if (at + 4 > len)
                return -1;
            run = packed[at + 3] == RUN_OF_256 ? LONGEST_ATTRIBUTE_RUN : packed[at + 3];
        }
        if (n + run > most)
            return -1;
        for (uint32_t k = 0; k < run; k++, n++) {
            cells[3 * n] = (uint8_t)(packed[at] & ~ATTRIBUTE_RUN);
            cells[3 * n + 1] = packed[at + 1];
            cells[3 * n + 2] = packed[at + 2];
        }
        at += packed[at] & ATTRIBUTE_RUN ? 4 : 3;
    }
    if (at != len || pack_attributes(cells, n, again) != len || memcmp(again, packed, len))
        return -1;
    return (int32_t)n;
}

/* Every map is unpacked into the same buffer, from $7E:B500 to the end of
 * the bank (the cartridge's biggest, map 107, reaches $7E:FE20): an edited
 * map may be as big as that, whatever the cartridge's was. */
enum { MAP_ROOM = 0x10000 - 0xB500 };

static const char *const LAYER_NAMES[LAYERS] = { "bg1", "bg2", "bg3" };

static void write_header(DecodedText *t, const uint8_t *h)
{
    char line[96];
    uint8_t layers = h[HEADER_LAYERS];
    snprintf(line, sizeof line, "bg1 %ux%u\n", columns_of(h, 0), rows_of(h, 0));
    decoded_put(t, line);
    snprintf(line, sizeof line, "bg2 %ux%u%s%s drift %02X\n", columns_of(h, 1), rows_of(h, 1),
             layers & BG2_AT_HALF_PACE ? " half" : "", layers & BG2_STILL ? " still" : "", h[HEADER_BG2_DRIFT]);
    decoded_put(t, line);
    snprintf(line, sizeof line, "bg3 %ux%u%s%s drift %02X\n", columns_of(h, 2), rows_of(h, 2),
             layers & HAS_BG3 ? "" : " none", layers & BG3_STILL ? " still" : "", h[HEADER_BG3_DRIFT]);
    decoded_put(t, line);
    snprintf(line, sizeof line, "screens %02X\ncolor-math %02X\n", h[HEADER_SCREENS], h[HEADER_COLOR_MATH]);
    decoded_put(t, line);
}

static bool write_field_map(DecodedText *t, const uint8_t *u, size_t len)
{
    if (len < HEADER_BYTES)
        return false;
    uint32_t at = attributes_at(u), most = columns_of(u, 0) * rows_of(u, 0);
    if (at > len)
        return false;
    uint8_t *cells = calloc(3, most), *again = calloc(4, most + 1);
    int32_t n = cells && again ? unpack_attributes(u + at, len - at, most, cells, again) : -1;
    if (n >= 0) {
        write_header(t, u);
        uint32_t layer_at = HEADER_BYTES;
        for (unsigned layer = 0; layer < LAYERS; layer++) {
            if (!has_layer(u, layer))
                continue;
            Shape s = { CELL_BYTE, columns_of(u, layer), 1, false };
            decoded_put(t, layer == 0 ? "map bg1\n" : layer == 1 ? "map bg2\n" : "map bg3\n");
            write_grid(t, s, u + layer_at, columns_of(u, layer) * rows_of(u, layer));
            layer_at += columns_of(u, layer) * rows_of(u, layer);
        }
        decoded_put(t, "attributes\n");
        Shape s = { CELL_ATTRIBUTES, columns_of(u, 0), 1, false };
        write_grid(t, s, cells, 3 * (uint32_t)n);
    }
    free(cells);
    free(again);
    return n >= 0;
}

/* The parts of a map's text, in order: the header's lines, then each
 * layer's `map bgN` and rows, then `attributes` and its rows. */
enum {
    PART_BG1,
    PART_BG2,
    PART_BG3,
    PART_SCREENS,
    PART_COLOR_MATH,
    PART_LAYER_MAPS,   /* then a part a layer for its `map bgN` line and one for its rows */
    PART_ATTRIBUTES = PART_LAYER_MAPS + 2 * LAYERS,
    PART_ATTRIBUTE_ROWS,
};

/* A size, `WxH`, into a layer's two bits each way. */
static bool read_size(const char **s, const char *end, unsigned *columns, unsigned *rows)
{
    unsigned v[2];
    while (*s < end && space(**s))
        (*s)++;
    for (unsigned k = 0; k < 2; k++) {
        v[k] = 0;
        const char *from = *s;
        while (*s < end && **s >= '0' && **s <= '9' && *s - from < 3)
            v[k] = v[k] * 10 + (unsigned)(*(*s)++ - '0');
        if (*s == from || v[k] % TILES_A_STEP || v[k] < TILES_A_STEP || v[k] > 4 * TILES_A_STEP)
            return false;
        v[k] = v[k] / TILES_A_STEP - 1;
        if (k == 0 && (*s == end || *(*s)++ != 'x'))
            return false;
    }
    *columns = v[0];
    *rows = v[1];
    return *s == end || space(**s);
}

static const char *read_header_line(DecodedReading *r, const char *s, const char *end)
{
    uint8_t *h = r->made;
    unsigned columns, rows, v;
    if (r->part <= PART_BG3) {
        static const char *const WRONG[] = {
            "the header starts `bg1 WxH`, 16 to 64 by 16 each way",
            "then `bg2 WxH`, `half` and `still` where so, and `drift XX`",
            "then `bg3 WxH`, `none` and `still` where so, and `drift XX`",
        };
        if (!decoded_word(&s, end, LAYER_NAMES[r->part]) || !read_size(&s, end, &columns, &rows))
            return WRONG[r->part];
        if (r->part == PART_BG1) {
            h[HEADER_SIZES] = (uint8_t)(columns | rows << 2);
            return s == end ? NULL : WRONG[0];
        }
        if (r->part == PART_BG2) {
            h[HEADER_SIZES] = (uint8_t)(h[HEADER_SIZES] | columns << 4 | rows << 6);
            if (decoded_word(&s, end, "half"))
                h[HEADER_LAYERS] |= BG2_AT_HALF_PACE;
            if (decoded_word(&s, end, "still"))
                h[HEADER_LAYERS] |= BG2_STILL;
        } else {
            h[HEADER_LAYERS] = (uint8_t)(h[HEADER_LAYERS] | columns | rows << 2 | HAS_BG3);
            if (decoded_word(&s, end, "none"))
                h[HEADER_LAYERS] &= (uint8_t)~HAS_BG3;
            if (decoded_word(&s, end, "still"))
                h[HEADER_LAYERS] |= BG3_STILL;
        }
        if (!decoded_word(&s, end, "drift") || !decoded_number(&s, end, 16, 0xFF, &v) || s != end)
            return WRONG[r->part];
        h[r->part == PART_BG2 ? HEADER_BG2_DRIFT : HEADER_BG3_DRIFT] = (uint8_t)v;
        return NULL;
    }
    bool screens = r->part == PART_SCREENS;
    if (!decoded_word(&s, end, screens ? "screens" : "color-math") || !decoded_number(&s, end, 16, 0xFF, &v) || s != end)
        return screens ? "then `screens XX`" : "then `color-math XX`";
    h[screens ? HEADER_SCREENS : HEADER_COLOR_MATH] = (uint8_t)v;
    return NULL;
}

/* The part after a layer's rows: the next layer the map has, or its attributes. */
static unsigned after_layer(const uint8_t *h, unsigned layer)
{
    while (++layer < LAYERS)
        if (has_layer(h, layer))
            return PART_LAYER_MAPS + 2 * layer;
    return PART_ATTRIBUTES;
}

static const char *read_map_line(DecodedReading *r, const char *s, const char *end)
{
    if (r->part == PART_BG1 && !decoded_grow(r, HEADER_BYTES))
        return "out of memory";
    if (r->part < PART_LAYER_MAPS) {
        const char *wrong = read_header_line(r, s, end);
        r->part++;
        return wrong;
    }
    const uint8_t *h = r->made;
    if (r->part < PART_ATTRIBUTES && (r->part - PART_LAYER_MAPS) % 2 == 0) {
        unsigned layer = (r->part - PART_LAYER_MAPS) / 2;
        static const char *const WANTED[] = { "then `map bg1`", "then `map bg2`", "then `map bg3`" };
        if (!decoded_word(&s, end, "map") || !decoded_word(&s, end, LAYER_NAMES[layer]) || s != end)
            return WANTED[layer];
        r->part++;
        r->lines = 0;
        return NULL;
    }
    if (r->part < PART_ATTRIBUTES) {
        unsigned layer = (r->part - PART_LAYER_MAPS) / 2;
        uint32_t columns = columns_of(h, layer);
        if (cells_in(s, end, CELL_BYTE) != columns)
            return "a row of a layer's map is as many bytes as the layer is wide";
        uint8_t *row = decoded_grow(r, columns);
        const char *c;
        size_t n;
        if (!row)
            return "out of memory";
        for (uint32_t k = 0; next_cell(&s, end, CELL_BYTE, &c, &n); k++) {
            const char *wrong = read_cell(CELL_BYTE, c, n, row + k);
            if (wrong)
                return wrong;
        }
        if (++r->lines == rows_of(r->made, layer))
            r->part = after_layer(r->made, layer);
        return NULL;
    }
    if (r->part == PART_ATTRIBUTES) {
        if (!decoded_word(&s, end, "attributes") || s != end)
            return "a layer's map is as many rows as the layer is high; then `attributes`";
        r->part = PART_ATTRIBUTE_ROWS;
        r->items = r->lines = 0;
        return NULL;
    }
    Shape a = { CELL_ATTRIBUTES, columns_of(h, 0), 1, false };
    const char *at = s;
    if (decoded_word(&at, end, "rest"))
        return "a map's attributes are whole tiles: no rest";
    if (r->items + cells_in(s, end, CELL_ATTRIBUTES) > columns_of(h, 0) * rows_of(h, 0))
        return "more tiles' attributes than BG1 has tiles";
    return read_grid_line(r, a, attributes_at(h), false, s, end);
}

static const char *read_map_end(Decoded *d, DecodedReading *r)
{
    if (r->part < PART_ATTRIBUTE_ROWS)
        return "a map has its header, each layer's map and then `attributes`";
    uint32_t at = attributes_at(r->made);
    r->made_len = at + pack_attributes(r->made + at, r->items, r->made + at);
    return packed_end(d, r);
}

/* ---- Writing and reading ----------------------------------------------------------------------- */

static bool write_resource(DecodedText *t, const AssetStore *store, const AssetEntry *e, const uint8_t *bytes)
{
    (void)store;
    Shape s;
    shape_of(e, &s);
    size_t len = 0;
    bool long_runs = false;
    uint8_t *unpacked = packed_unpack(bytes, e->length, &len, &long_runs);
    if (!unpacked)
        return false;
    char line[64];
    snprintf(line, sizeof line, "# %u bytes unpacked\n", (unsigned)len);
    decoded_put(t, line);
    packed_write_form(t, long_runs);
    bool written = true;
    if (s.field_map)
        written = write_field_map(t, unpacked, len);
    else
        write_grid(t, s, unpacked, len);
    free(unpacked);
    return written;
}

static const char *read_line(Decoded *d, DecodedReading *r, const char *s, const char *end)
{
    Shape shape;
    shape_of(&asset_catalogue[r->entry], &shape);
    if (r->form == PACKED_UNSAID) {
        const char *wrong = packed_read_form(d, r, s, end);
        if (shape.field_map && r->most < MAP_ROOM)
            r->most = MAP_ROOM;
        return wrong;
    }
    return shape.field_map ? read_map_line(r, s, end) : read_grid_line(r, shape, 0, true, s, end);
}

static const char *read_end(Decoded *d, DecodedReading *r)
{
    Shape shape;
    shape_of(&asset_catalogue[r->entry], &shape);
    if (r->form != PACKED_UNSAID && shape.field_map)
        return read_map_end(d, r);
    if (r->lines)
        return "a row of looks or tiles is all its lines";
    return packed_end(d, r);
}

const DecodedKind map_text = {
    "maps",
    "# Chrono Trigger's maps and tilemaps, from your cartridge: not for sharing.\n"
    "# Packed resources, each unpacked into a grid of cells: bytes in hex, a tilemap\n"
    "# entry as TTT:P:hvp (tile, palette, '-' or h, v, p where it is flipped across,\n"
    "# flipped down or in front), a color as RR.GG.BB (0-31). A location's map gives\n"
    "# its header, each layer's map of the tile set's looks, then each tile's\n"
    "# attributes. Edited, a resource is packed again as it is read, and must fit.\n"
    "# Edit and start the game again; delete the file to write it afresh.\n",
    decodes, write_resource, read_line, read_end,
};
