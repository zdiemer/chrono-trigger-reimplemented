#include "decoded.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { NOT_HERE = 0xFFFFFFFFu };

/* The comment a text's opening names the catalogue's rows it was written
 * from in: a hash of the rows its kind decodes, their names, types, codecs
 * and lengths, which are what its names and forms mean. A change to other
 * rows leaves it, so a text's edits stay good through changes that aren't
 * its kind's. */
static const char ROWS_STAMP[] = "# rows ";

enum { FNV_OFFSET = 0x811C9DC5u, FNV_PRIME = 0x01000193u };

static uint32_t hashed(uint32_t h, const void *bytes, size_t n)
{
    const uint8_t *p = bytes;
    for (size_t i = 0; i < n; i++)
        h = (h ^ p[i]) * FNV_PRIME;
    return h;
}

static uint32_t rows_hash(const DecodedKind *kind)
{
    uint32_t h = FNV_OFFSET;
    for (uint16_t i = 0; i < asset_catalogue_count; i++) {
        const AssetEntry *e = &asset_catalogue[i];
        bool named;
        if (!kind->decodes(e, &named))
            continue;
        const char *type = asset_type_names[e->type], *codec = asset_codec_names[e->codec];
        uint8_t length[4] = { (uint8_t)e->length, (uint8_t)(e->length >> 8), (uint8_t)(e->length >> 16),
                              (uint8_t)(e->length >> 24) };
        h = hashed(h, e->name, strlen(e->name) + 1);
        h = hashed(h, type, strlen(type) + 1);
        h = hashed(h, codec, strlen(codec) + 1);
        h = hashed(h, length, sizeof length);
        h = hashed(h, &named, 1);
    }
    return h;
}

static int stamp_line(const DecodedKind *kind, char *out, size_t n)
{
    return snprintf(out, n, "%s%08X\n", ROWS_STAMP, (unsigned)rows_hash(kind));
}

bool decoded_of_these_rows(const DecodedKind *kind, const char *text, size_t len)
{
    char stamp[64];
    int n = stamp_line(kind, stamp, sizeof stamp);
    for (const char *s = text, *end = text + len; s < end && *s == '#';) {
        const char *newline = memchr(s, '\n', (size_t)(end - s));
        if (!newline)
            break;
        if ((size_t)(newline + 1 - s) == (size_t)n && !memcmp(s, stamp, (size_t)n))
            return true;
        s = newline + 1;
    }
    return false;
}

int decoded_entry(const char *name, size_t len, uint16_t *hint)
{
    for (uint16_t k = 0; k < asset_catalogue_count; k++) {
        uint16_t i = (uint16_t)((*hint + k) % asset_catalogue_count);
        const char *n = asset_catalogue[i].name;
        if (strlen(n) == len && !memcmp(n, name, len)) {
            *hint = (uint16_t)(i + 1);
            return i;
        }
    }
    return -1;
}

/* ---- Writing ----------------------------------------------------------------------------------- */

void decoded_put(DecodedText *t, const char *s)
{
    size_t n = strlen(s);
    if (t->full)
        return;
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

static const uint8_t *served(const AssetStore *store, const AssetEntry *e)
{
    uint32_t n = 0;
    const uint8_t *bytes = store->find(store->ctx, e->name, asset_type_names[e->type], &n);
    return bytes && n == e->length ? bytes : NULL;
}

char *decoded_export(const DecodedKind *kind, const AssetStore *store, size_t *len, const char **why)
{
    DecodedText t = { 0 };
    char stamp[64];
    stamp_line(kind, stamp, sizeof stamp);
    decoded_put(&t, kind->header);
    decoded_put(&t, stamp);
    for (uint16_t i = 0; i < asset_catalogue_count; i++) {
        const AssetEntry *e = &asset_catalogue[i];
        bool named;
        if (!kind->decodes(e, &named) || !named)
            continue;
        const uint8_t *bytes = served(store, e);
        decoded_put(&t, "\n[");
        decoded_put(&t, e->name);
        decoded_put(&t, "]\n");
        if (!bytes || !kind->write(&t, store, e, bytes)) {
            free(t.text);
            *why = "the store hasn't every resource of the catalogue";
            return NULL;
        }
    }
    char end[2] = { 'x', 0 };
    decoded_put(&t, end);   /* room for the NUL after the text, which a caller may search it by */
    if (t.full) {
        free(t.text);
        *why = "out of memory";
        return NULL;
    }
    t.text[--t.len] = 0;
    *len = t.len;
    return t.text;
}

/* ---- Reading ----------------------------------------------------------------------------------- */

struct Decoded {
    const AssetStore *base;
    uint8_t *bytes;   /* every decoded resource's, one after another */
    uint32_t *at;     /* where each row of the catalogue's are in `bytes`, or NOT_HERE */
    uint8_t *given;   /* whether the text gave the row */
    const uint8_t **beneath;   /* what the store beneath serves for each row, once asked: NULL before */
    uint16_t hint;
};

uint8_t *decoded_bytes(Decoded *d, int entry)
{
    d->given[entry] = 1;
    return d->bytes + d->at[entry];
}

const uint8_t *decoded_beneath(const Decoded *d, int entry)
{
    if (!d->beneath[entry])
        d->beneath[entry] = served(d->base, &asset_catalogue[entry]);
    return d->beneath[entry];
}

static bool space(char c) { return c == ' ' || c == '\t'; }

/* A line without its comment and the spaces about it. */
static void trimmed(const char **s, const char **end)
{
    const char *hash = memchr(*s, '#', (size_t)(*end - *s));
    if (hash)
        *end = hash;
    while (*s < *end && space(**s))
        (*s)++;
    while (*end > *s && (space((*end)[-1]) || (*end)[-1] == '\r'))
        (*end)--;
}

static int digit(char c, unsigned base)
{
    int v = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 99;
    return v < (int)base ? v : -1;
}

bool decoded_number(const char **s, const char *end, unsigned base, unsigned most, unsigned *v)
{
    while (*s < end && space(**s))
        (*s)++;
    if (*s == end || digit(**s, base) < 0)
        return false;
    *v = 0;
    while (*s < end && digit(**s, base) >= 0 && *v <= most)
        *v = *v * base + (unsigned)digit(*(*s)++, base);
    return *v <= most && (*s == end || space(**s) || **s == '+');
}

bool decoded_word(const char **s, const char *end, const char *word)
{
    size_t n = strlen(word);
    while (*s < end && space(**s))
        (*s)++;
    if ((size_t)(end - *s) < n || memcmp(*s, word, n) || (*s + n < end && !space((*s)[n])))
        return false;
    *s += n;
    return true;
}

uint8_t *decoded_grow(DecodedReading *r, uint32_t more)
{
    if (more > UINT32_MAX - r->made_len)
        return NULL;
    if (r->made_len + more > r->made_room) {
        uint32_t room = r->made_room ? r->made_room : 1 << 12;
        while (room < r->made_len + more)
            room = room > UINT32_MAX / 2 ? UINT32_MAX : room * 2;
        uint8_t *bigger = realloc(r->made, room);
        if (!bigger)
            return NULL;
        r->made = bigger;
        r->made_room = room;
    }
    uint8_t *at = r->made + r->made_len;
    memset(at, 0, more);
    r->made_len += more;
    return at;
}

static Decoded *refuse(Decoded *d, const char *what, const char **why)
{
    decoded_close(d);
    *why = what;
    return NULL;
}

Decoded *decoded_import(const DecodedKind *kind, const char *text, size_t len, const AssetStore *base,
                        unsigned *line, const char **why)
{
    Decoded *d = calloc(1, sizeof *d);
    size_t total = 0;
    *line = 0;
    if (d) {
        d->at = malloc(sizeof *d->at * asset_catalogue_count);
        d->given = calloc(asset_catalogue_count, 1);
        d->beneath = calloc(asset_catalogue_count, sizeof *d->beneath);
    }
    if (!d || !d->at || !d->given || !d->beneath)
        return refuse(d, "out of memory", why);
    for (uint16_t i = 0; i < asset_catalogue_count; i++) {
        bool named;
        d->at[i] = kind->decodes(&asset_catalogue[i], &named) ? (uint32_t)total : NOT_HERE;
        if (d->at[i] != NOT_HERE)
            total += asset_catalogue[i].length;
    }
    d->base = base;
    d->bytes = calloc(total ? total : 1, 1);
    if (!d->bytes)
        return refuse(d, "out of memory", why);
    DecodedReading r = { .entry = -1 };
    const char *wrong = NULL;
    unsigned header = 0;   /* the [name] line of the resource being read: where its end is found wrong */
    unsigned last = 0;     /* its last line, where read_end may say instead */
    for (const char *s = text, *stop = text + len; s < stop && !wrong;) {
        const char *newline = memchr(s, '\n', (size_t)(stop - s));
        const char *end = newline ? newline : stop, *next = newline ? newline + 1 : stop;
        ++*line;
        trimmed(&s, &end);
        if (s == end) {
            s = next;
            continue;
        }
        if (*s == '[') {
            const char *close = memchr(s, ']', (size_t)(end - s));
            bool named = false;
            int entry = close && close + 1 == end ? decoded_entry(s + 1, (size_t)(close - s - 1), &d->hint) : -1;
            if (r.entry >= 0 && (wrong = kind->read_end(d, &r)) != NULL) {
                *line = r.wrong_at_its_last_line ? last : header;
                break;
            }
            header = last = *line;
            if (!close || close + 1 != end)
                wrong = "a resource's name is [name]";
            else if (entry < 0 || !kind->decodes(&asset_catalogue[entry], &named) || !named)
                wrong = "the catalogue has nothing of that name to read here";
            else if (d->given[entry])
                wrong = "that name is given twice";
            else
                r = (DecodedReading){ .entry = entry, .bytes = decoded_bytes(d, entry), .made = r.made,
                                      .made_room = r.made_room };
        } else if (r.entry < 0) {
            wrong = "a line before any [name]";
        } else {
            wrong = kind->read_line(d, &r, s, end);
            last = *line;
        }
        s = next;
    }
    if (!wrong && r.entry >= 0 && (wrong = kind->read_end(d, &r)) != NULL)
        *line = r.wrong_at_its_last_line ? last : header;
    free(r.made);
    if (wrong)
        return refuse(d, wrong, why);
    return d;
}

static const uint8_t *decoded_find(void *ctx, const char *name, const char *type, uint32_t *length)
{
    Decoded *d = ctx;
    int i = decoded_entry(name, strlen(name), &d->hint);
    if (i >= 0 && d->given[i] && !strcmp(type, asset_type_names[asset_catalogue[i].type])) {
        *length = asset_catalogue[i].length;
        return d->bytes + d->at[i];
    }
    return d->base->find(d->base->ctx, name, type, length);
}

void decoded_store(Decoded *d, AssetStore *store)
{
    store->find = decoded_find;
    store->ctx = d;
}

void decoded_close(Decoded *d)
{
    if (d) {
        free(d->bytes);
        free(d->at);
        free(d->given);
        free(d->beneath);
        free(d);
    }
}

bool decoded_round_trip(const DecodedKind *kind, const AssetStore *store, const char **why)
{
    size_t len = 0;
    unsigned line;
    char *text = decoded_export(kind, store, &len, why);
    Decoded *d = text ? decoded_import(kind, text, len, store, &line, why) : NULL;
    free(text);
    if (!d)
        return false;
    bool same = true;
    for (uint16_t i = 0; i < asset_catalogue_count && same; i++) {
        const AssetEntry *e = &asset_catalogue[i];
        if (d->at[i] == NOT_HERE)
            continue;
        const uint8_t *was = served(store, e);
        same = d->given[i] && was && !memcmp(was, d->bytes + d->at[i], e->length);
        if (!same)
            *why = e->name;
    }
    decoded_close(d);
    return same;
}
