#include "palettes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A color's word: five bits each of red, green and blue from the bottom, and
 * a top bit the hardware ignores. */
enum { COLOR_MAX = 31, GREEN_SHIFT = 5, BLUE_SHIFT = 10, TOP_BIT = 0x8000, NOT_HERE = 0xFFFFFFFFu };

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

/* The catalogue's row of a name, looked for from `hint` on first: a store is
 * asked for the resources in the catalogue's order. */
static int entry_index(const char *name, size_t name_len, uint16_t *hint)
{
    for (uint16_t k = 0; k < asset_catalogue_count; k++) {
        uint16_t i = (uint16_t)((*hint + k) % asset_catalogue_count);
        const char *n = asset_catalogue[i].name;
        if (strlen(n) == name_len && !memcmp(n, name, name_len)) {
            *hint = (uint16_t)(i + 1);
            return i;
        }
    }
    return -1;
}

/* ---- Writing ----------------------------------------------------------------------------------- */

typedef struct {
    char *text;
    size_t len, cap;
    bool full;
} Text;

static void put(Text *t, const char *s)
{
    size_t n = strlen(s);
    if (t->len + n > t->cap) {
        size_t cap = t->cap ? t->cap * 2 : 1 << 16;
        while (cap < t->len + n)
            cap *= 2;
        char *more = realloc(t->text, cap);
        if (!more) {
            t->full = true;
            return;
        }
        t->text = more;
        t->cap = cap;
    }
    memcpy(t->text + t->len, s, n);
    t->len += n;
}

static void put_palette(Text *t, const AssetEntry *e, const uint8_t *bytes, unsigned row_colors,
                        unsigned palette_rows)
{
    char line[96];
    snprintf(line, sizeof line, "\n[%s]\n", e->name);
    put(t, line);
    for (uint32_t c = 0; c < e->length / 2; c++) {
        unsigned row = c / row_colors;
        if (c % row_colors == 0) {
            if (palette_rows > 1)
                snprintf(line, sizeof line, "# palette %u, row %u\n", row / palette_rows, row % palette_rows);
            else
                snprintf(line, sizeof line, "# row %u\n", row);
            put(t, line);
        }
        unsigned word = (unsigned)bytes[2 * c] | (unsigned)bytes[2 * c + 1] << 8;
        snprintf(line, sizeof line, "%2u %2u %2u%s\n", word & COLOR_MAX, word >> GREEN_SHIFT & COLOR_MAX,
                 word >> BLUE_SHIFT & COLOR_MAX, word & TOP_BIT ? " +" : "");
        put(t, line);
    }
}

char *palettes_export(const AssetStore *store, size_t *len, const char **why)
{
    Text t = { 0 };
    put(&t, "# Chrono Trigger's palettes, from your cartridge: not for sharing.\n"
            "# A color a line: red green blue, 0-31 each; '+' keeps the word's top bit.\n"
            "# Edit and start the game again; delete the file to write it afresh.\n");
    for (uint16_t i = 0; i < asset_catalogue_count; i++) {
        const AssetEntry *e = &asset_catalogue[i];
        unsigned row_colors, palette_rows;
        if (!palette_codec(e, &row_colors, &palette_rows))
            continue;
        uint32_t n = 0;
        const uint8_t *bytes = store->find(store->ctx, e->name, asset_type_names[e->type], &n);
        if (!bytes || n != e->length) {
            free(t.text);
            *why = "the store hasn't every palette of the catalogue";
            return NULL;
        }
        put_palette(&t, e, bytes, row_colors, palette_rows);
    }
    if (t.full) {
        free(t.text);
        *why = "out of memory";
        return NULL;
    }
    *len = t.len;
    return t.text;
}

/* ---- Reading ----------------------------------------------------------------------------------- */

struct AssetPalettes {
    const AssetStore *base;
    uint8_t *bytes;        /* every palette's, one after another */
    uint32_t *at;          /* where each row of the catalogue's are in `bytes`, or NOT_HERE */
    uint16_t hint;
};

/* A line without its comment and the spaces about it. */
static void trimmed(const char **s, const char **end)
{
    const char *hash = memchr(*s, '#', (size_t)(*end - *s));
    if (hash)
        *end = hash;
    while (*s < *end && (**s == ' ' || **s == '\t'))
        (*s)++;
    while (*end > *s && ((*end)[-1] == ' ' || (*end)[-1] == '\t' || (*end)[-1] == '\r'))
        (*end)--;
}

static bool number(const char **s, const char *end, unsigned *v)
{
    while (*s < end && (**s == ' ' || **s == '\t'))
        (*s)++;
    if (*s == end || **s < '0' || **s > '9')
        return false;
    *v = 0;
    while (*s < end && **s >= '0' && **s <= '9' && *v <= COLOR_MAX)
        *v = *v * 10 + (unsigned)(*(*s)++ - '0');
    return *v <= COLOR_MAX && (*s == end || **s == ' ' || **s == '\t' || **s == '+');
}

/* A color line's word, or false. */
static bool color(const char *s, const char *end, uint16_t *word)
{
    unsigned r, g, b;
    if (!number(&s, end, &r) || !number(&s, end, &g) || !number(&s, end, &b))
        return false;
    while (s < end && (*s == ' ' || *s == '\t'))
        s++;
    bool top = s < end && *s == '+';
    if (top)
        s++;
    if (s != end)
        return false;
    *word = (uint16_t)(r | g << GREEN_SHIFT | b << BLUE_SHIFT | (top ? TOP_BIT : 0));
    return true;
}

static AssetPalettes *refuse(AssetPalettes *p, const char *what, const char **why)
{
    palettes_close(p);
    *why = what;
    return NULL;
}

AssetPalettes *palettes_import(const char *text, size_t len, const AssetStore *base, unsigned *line,
                               const char **why)
{
    AssetPalettes *p = calloc(1, sizeof *p);
    size_t total = 0;
    if (p)
        p->at = malloc(sizeof *p->at * asset_catalogue_count);
    if (!p || !p->at)
        return refuse(p, "out of memory", why);
    for (uint16_t i = 0; i < asset_catalogue_count; i++) {
        unsigned row_colors, palette_rows;
        p->at[i] = palette_codec(&asset_catalogue[i], &row_colors, &palette_rows) ? (uint32_t)total : NOT_HERE;
        if (p->at[i] != NOT_HERE)
            total += asset_catalogue[i].length;
    }
    p->base = base;
    p->bytes = calloc(total ? total : 1, 1);
    uint8_t *given = calloc(asset_catalogue_count, 1);
    if (!p->bytes || !given) {
        free(given);
        return refuse(p, "out of memory", why);
    }
    int open = -1;          /* the resource being read */
    uint32_t colors = 0;    /* its colors so far */
    *line = 0;
    for (const char *s = text, *stop = text + len; s < stop;) {
        const char *newline = memchr(s, '\n', (size_t)(stop - s));
        const char *end = newline ? newline : stop, *next = newline ? newline + 1 : stop;
        ++*line;
        trimmed(&s, &end);
        if (s == end) {
            s = next;
            continue;
        }
        if (*s == '[') {
            if (open >= 0 && colors != asset_catalogue[open].length / 2)
                break;
            const char *close = memchr(s, ']', (size_t)(end - s));
            if (!close || close + 1 != end) {
                free(given);
                return refuse(p, "a palette's name is [name]", why);
            }
            open = entry_index(s + 1, (size_t)(close - s - 1), &p->hint);
            if (open < 0 || p->at[open] == NOT_HERE) {
                free(given);
                return refuse(p, "no palette of the catalogue has that name", why);
            }
            if (given[open]) {
                free(given);
                return refuse(p, "that palette is given twice", why);
            }
            given[open] = 1;
            colors = 0;
        } else {
            uint16_t word;
            if (open < 0) {
                free(given);
                return refuse(p, "a color before any [name]", why);
            }
            if (!color(s, end, &word)) {
                free(given);
                return refuse(p, "a color is red green blue, 0-31 each, and maybe '+'", why);
            }
            if (colors == asset_catalogue[open].length / 2) {
                free(given);
                return refuse(p, "more colors than the palette has", why);
            }
            uint8_t *to = p->bytes + p->at[open] + 2 * colors++;
            to[0] = (uint8_t)word;
            to[1] = (uint8_t)(word >> 8);
        }
        s = next;
    }
    if (open >= 0 && colors != asset_catalogue[open].length / 2) {
        free(given);
        return refuse(p, "fewer colors than the palette has", why);
    }
    for (uint16_t i = 0; i < asset_catalogue_count; i++)
        if (!given[i])
            p->at[i] = NOT_HERE;
    free(given);
    return p;
}

static const uint8_t *palettes_find(void *ctx, const char *name, const char *type, uint32_t *length)
{
    AssetPalettes *p = ctx;
    int i = entry_index(name, strlen(name), &p->hint);
    if (i >= 0 && p->at[i] != NOT_HERE && !strcmp(type, asset_type_names[asset_catalogue[i].type])) {
        *length = asset_catalogue[i].length;
        return p->bytes + p->at[i];
    }
    return p->base->find(p->base->ctx, name, type, length);
}

void palettes_store(AssetPalettes *palettes, AssetStore *store)
{
    store->find = palettes_find;
    store->ctx = palettes;
}

void palettes_close(AssetPalettes *palettes)
{
    if (palettes) {
        free(palettes->bytes);
        free(palettes->at);
        free(palettes);
    }
}

bool palettes_round_trip(const AssetStore *store, const char **why)
{
    size_t len = 0;
    unsigned line;
    char *text = palettes_export(store, &len, why);
    AssetPalettes *p = text ? palettes_import(text, len, store, &line, why) : NULL;
    free(text);
    if (!p)
        return false;
    bool same = true;
    for (uint16_t i = 0; i < asset_catalogue_count && same; i++) {
        const AssetEntry *e = &asset_catalogue[i];
        if (p->at[i] == NOT_HERE)
            continue;
        uint32_t n = 0;
        const uint8_t *was = store->find(store->ctx, e->name, asset_type_names[e->type], &n);
        same = was && n == e->length && !memcmp(was, p->bytes + p->at[i], n);
        if (!same)
            *why = e->name;
    }
    palettes_close(p);
    return same;
}
