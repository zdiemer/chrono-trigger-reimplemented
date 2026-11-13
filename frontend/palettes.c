#include "palettes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A color's word: five bits each of red, green and blue from the bottom, and
 * a top bit the hardware ignores. */
enum { COLOR_MAX = 31, GREEN_SHIFT = 5, BLUE_SHIFT = 10, TOP_BIT = 0x8000 };

bool palette_codec(const AssetEntry *e, unsigned *row_colors, unsigned *palette_rows)
{
    const char *codec = asset_codec_names[e->codec];
    if (strncmp(codec, "palette:", 8) != 0)
        return false;
    char *end;
    *row_colors = (unsigned)strtoul(codec + 8, &end, 10);
    *palette_rows = *end == 'x' ? (unsigned)strtoul(end + 1, NULL, 10) : 1;
    return true;
}

static bool decodes(const AssetEntry *e, bool *named)
{
    unsigned row_colors, palette_rows;
    *named = true;
    return palette_codec(e, &row_colors, &palette_rows);
}

static bool write_palette(DecodedText *t, const AssetStore *store, const AssetEntry *e, const uint8_t *bytes)
{
    (void)store;
    unsigned row_colors = 1, palette_rows = 1;
    palette_codec(e, &row_colors, &palette_rows);
    char line[96];
    for (uint32_t c = 0; c < e->length / 2; c++) {
        unsigned row = c / row_colors;
        if (c % row_colors == 0) {
            if (palette_rows > 1)
                snprintf(line, sizeof line, "# palette %u, row %u\n", row / palette_rows, row % palette_rows);
            else
                snprintf(line, sizeof line, "# row %u\n", row);
            decoded_put(t, line);
        }
        unsigned word = (unsigned)bytes[2 * c] | (unsigned)bytes[2 * c + 1] << 8;
        snprintf(line, sizeof line, "%2u %2u %2u%s\n", word & COLOR_MAX, word >> GREEN_SHIFT & COLOR_MAX,
                 word >> BLUE_SHIFT & COLOR_MAX, word & TOP_BIT ? " +" : "");
        decoded_put(t, line);
    }
    return true;
}

/* A color line: red, green and blue, and maybe '+'. */
static const char *read_color(Decoded *d, DecodedReading *r, const char *s, const char *end)
{
    (void)d;
    unsigned red, green, blue;
    if (!decoded_number(&s, end, 10, COLOR_MAX, &red) || !decoded_number(&s, end, 10, COLOR_MAX, &green) ||
        !decoded_number(&s, end, 10, COLOR_MAX, &blue))
        return "a color is red green blue, 0-31 each, and maybe '+'";
    bool top = decoded_word(&s, end, "+");
    if (s != end)
        return "a color is red green blue, 0-31 each, and maybe '+'";
    if (r->items == asset_catalogue[r->entry].length / 2)
        return "more colors than the palette has";
    unsigned word = red | green << GREEN_SHIFT | blue << BLUE_SHIFT | (top ? TOP_BIT : 0);
    r->bytes[2 * r->items] = (uint8_t)word;
    r->bytes[2 * r->items + 1] = (uint8_t)(word >> 8);
    r->items++;
    return NULL;
}

static const char *read_end(Decoded *d, DecodedReading *r)
{
    (void)d;
    return r->items == asset_catalogue[r->entry].length / 2 ? NULL : "fewer colors than the palette has";
}

const DecodedKind palette_text = {
    "palettes",
    "# Chrono Trigger's palettes, from your cartridge: not for sharing.\n"
    "# A color a line: red green blue, 0-31 each; '+' keeps the word's top bit.\n"
    "# Edit and start the game again; delete the file to write it afresh.\n",
    decodes, write_palette, read_color, read_end, NULL, NULL,
};
