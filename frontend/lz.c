#include "lz.h"

#include <stdlib.h>
#include <string.h>

enum {
    FIRST_LENGTH = 2,    /* the first segment's length, before it */
    HEADER = 3,          /* a header byte and the word where the next segment ends */
    CONTROL_ITEMS = 8,
    COUNT_BITS = 0x3F,   /* a header's count of the next control byte's items */
    LONG_RUNS = 0x40,    /* the bit the packer sets in every header of a stream with long runs */
    LONG_RUNS_ANY = 0xC0,  /* the bits the game reads it from */
    SHORTEST_RUN = 3,
};

typedef struct {
    unsigned ring, longest;   /* 4096 and 18, or 2048 and 34 */
    unsigned distance_bits;
} Runs;

static Runs runs_of(bool long_runs) { return long_runs ? (Runs){ 2048, 34, 11 } : (Runs){ 4096, 18, 12 }; }

/* ---- Unpacking ----------------------------------------------------------------------------------- */

bool lz_unpack(const uint8_t *in, size_t in_len, uint8_t *out, size_t *out_len, bool *long_runs)
{
    if (in_len < FIRST_LENGTH + 1)
        return false;
    size_t at = FIRST_LENGTH, end = FIRST_LENGTH + (in[0] | (size_t)in[1] << 8), n = 0;
    if (end >= in_len)
        return false;
    *long_runs = (in[end] & LONG_RUNS_ANY) != 0;
    Runs runs = runs_of(*long_runs);
    unsigned items = CONTROL_ITEMS;
    for (;;) {
        if (at == end) {
            unsigned count = in[at] & COUNT_BITS;
            if (!count) {
                *out_len = n;
                return true;
            }
            if (at + HEADER > in_len)
                return false;
            items = count;
            end = in[at + 1] | (size_t)in[at + 2] << 8;
            at += HEADER;
            if (end < at || end >= in_len)
                return false;
            continue;
        }
        if (at > end)
            return false;
        uint8_t control = in[at++];
        if (!control) {   /* eight literals, and the count kept for the next control byte */
            if (at + CONTROL_ITEMS > in_len || n + CONTROL_ITEMS > LZ_MOST)
                return false;
            memcpy(out + n, in + at, CONTROL_ITEMS);
            at += CONTROL_ITEMS;
            n += CONTROL_ITEMS;
            continue;
        }
        for (unsigned k = 0; k < items; k++, control >>= 1) {
            if (!(control & 1)) {
                if (at >= in_len || n >= LZ_MOST)
                    return false;
                out[n++] = in[at++];
                continue;
            }
            if (at + 2 > in_len)
                return false;
            unsigned word = in[at] | (unsigned)in[at + 1] << 8;
            size_t distance = word & ((1u << runs.distance_bits) - 1), length = (word >> runs.distance_bits) + SHORTEST_RUN;
            at += 2;
            if (distance == 0 || distance > n || n + length > LZ_MOST)
                return false;
            for (size_t i = 0; i < length; i++, n++)
                out[n] = out[n - distance];
        }
        items = CONTROL_ITEMS;
    }
}

/* ---- Okumura's search ---------------------------------------------------------------------------- */

/* The longest earlier run matching the bytes ahead, found by a binary tree
 * of the ring's places ordered by the bytes from each: Okumura's
 * InsertNode/DeleteNode, kept as written, since which of the equally long
 * runs it finds is what makes the cartridge's bytes. */
enum { RING_MOST = 4096, LONGEST_MOST = 34, ROOTS = 256 };

typedef struct {
    int ring, longest, none;
    uint8_t text[RING_MOST + LONGEST_MOST - 1];
    int left[RING_MOST + 1], right[RING_MOST + 1 + ROOTS], parent[RING_MOST + 1];
    int match_at, match_length;
} Search;

static void insert_place(Search *t, int r)
{
    int cmp = 1, p = t->ring + 1 + t->text[r], i = 0;
    const uint8_t *key = &t->text[r];
    t->right[r] = t->left[r] = t->none;
    t->match_length = 0;
    for (;;) {
        if (cmp >= 0) {
            if (t->right[p] != t->none) {
                p = t->right[p];
            } else {
                t->right[p] = r;
                t->parent[r] = p;
                return;
            }
        } else {
            if (t->left[p] != t->none) {
                p = t->left[p];
            } else {
                t->left[p] = r;
                t->parent[r] = p;
                return;
            }
        }
        for (i = 1; i < t->longest; i++)
            if ((cmp = key[i] - t->text[p + i]) != 0)
                break;
        if (i > t->match_length) {
            t->match_at = p;
            if ((t->match_length = i) >= t->longest)
                break;
        }
    }
    /* as long a run as there can be: the new place takes the old one's */
    t->parent[r] = t->parent[p];
    t->left[r] = t->left[p];
    t->right[r] = t->right[p];
    t->parent[t->left[p]] = r;
    t->parent[t->right[p]] = r;
    if (t->right[t->parent[p]] == p)
        t->right[t->parent[p]] = r;
    else
        t->left[t->parent[p]] = r;
    t->parent[p] = t->none;
}

static void delete_place(Search *t, int p)
{
    int q;
    if (t->parent[p] == t->none)
        return;
    if (t->right[p] == t->none) {
        q = t->left[p];
    } else if (t->left[p] == t->none) {
        q = t->right[p];
    } else {
        q = t->left[p];
        if (t->right[q] != t->none) {
            do
                q = t->right[q];
            while (t->right[q] != t->none);
            t->right[t->parent[q]] = t->left[q];
            t->parent[t->left[q]] = t->parent[q];
            t->left[q] = t->left[p];
            t->parent[t->left[p]] = q;
        }
        t->right[q] = t->right[p];
        t->parent[t->right[p]] = q;
    }
    t->parent[q] = t->parent[p];
    if (t->right[t->parent[p]] == p)
        t->right[t->parent[p]] = q;
    else
        t->left[t->parent[p]] = q;
    t->parent[p] = t->none;
}

/* An item: a literal byte (length 0), or a run `distance` back. */
typedef struct {
    uint16_t distance;
    uint8_t length, literal;
} Item;

static void put_byte(Search *t, int s, uint8_t c)
{
    t->text[s] = c;
    if (s < t->longest - 1)   /* the ring's first bytes again past its end, for the comparisons */
        t->text[s + t->ring] = c;
}

/* The items of `image`, as Okumura's encoder chooses them; their count. */
static size_t items_of(Search *t, const uint8_t *image, size_t size, Item *items)
{
    int ring = t->ring, longest = t->longest, s = 0, r = ring - longest, len = 0;
    size_t pos = 0, n = 0;
    t->none = ring;
    for (int i = ring + 1; i <= ring + ROOTS; i++)
        t->right[i] = t->none;
    for (int i = 0; i < ring; i++)
        t->parent[i] = t->none;
    memset(t->text, 0, sizeof t->text);
    for (; len < longest && pos < size; len++)
        t->text[r + len] = image[pos++];
    if (len == 0)
        return 0;
    /* The ring before the image is never searched. Once the image is all
     * read, the bytes ahead of the last few items are what the ring held
     * before, except in an image exactly a ring long, where they are 0: the
     * cartridge's packer did so, and which run it found depends on them. */
    bool clear_the_end = size == (size_t)ring;
    insert_place(t, r);
    do {
        int length = t->match_length > len ? len : t->match_length;
        if (length < SHORTEST_RUN) {
            length = 1;
            items[n++] = (Item){ 0, 0, t->text[r] };
        } else {
            items[n++] = (Item){ (uint16_t)((r - t->match_at) & (ring - 1)), (uint8_t)length, 0 };
        }
        int i = 0;
        for (; i < length && pos < size; i++) {
            delete_place(t, s);
            put_byte(t, s, image[pos++]);
            s = (s + 1) & (ring - 1);
            r = (r + 1) & (ring - 1);
            insert_place(t, r);
        }
        while (i++ < length) {
            delete_place(t, s);
            if (clear_the_end)
                put_byte(t, s, 0);
            s = (s + 1) & (ring - 1);
            r = (r + 1) & (ring - 1);
            if (--len)
                insert_place(t, r);
        }
    } while (len > 0);
    return n;
}

/* ---- Packing ------------------------------------------------------------------------------------- */

typedef struct {
    uint8_t *out;
    size_t at, room;
} Writing;

static void put(Writing *w, uint8_t v)
{
    if (w->at < w->room)
        w->out[w->at] = v;
    w->at++;
}

/* A control byte and the `count` items it describes. The bits past them
 * are set, as the cartridge's packer left them, which also keeps the odd
 * items' control byte from reading as eight literals where all are literals. */
static void put_items(Writing *w, const Item *items, size_t count, Runs runs)
{
    uint8_t control = (uint8_t)(0xFFu << count);
    for (size_t k = 0; k < count; k++)
        if (items[k].length)
            control |= (uint8_t)(1u << k);
    put(w, control);
    for (size_t k = 0; k < count; k++) {
        if (!items[k].length) {
            put(w, items[k].literal);
            continue;
        }
        unsigned word = (unsigned)(items[k].length - SHORTEST_RUN) << runs.distance_bits | items[k].distance;
        put(w, (uint8_t)word);
        put(w, (uint8_t)(word >> 8));
    }
}

static void put_groups(Writing *w, const Item *items, size_t from, size_t to, Runs runs)
{
    for (size_t k = from; k < to; k += CONTROL_ITEMS)
        put_items(w, items + k, CONTROL_ITEMS, runs);
}

size_t lz_pack(const uint8_t *image, size_t len, bool long_runs, uint8_t *out, size_t room)
{
    if (len > LZ_MOST)
        return 0;
    Runs runs = runs_of(long_runs);
    Search *t = malloc(sizeof *t);
    Item *items = malloc(sizeof *items * (len ? len : 1));
    if (!t || !items) {
        free(t);
        free(items);
        return 0;
    }
    t->ring = (int)runs.ring;
    t->longest = (int)runs.longest;
    size_t n = items_of(t, image, len, items);
    free(t);
    /* Whole control bytes in the first segment; where the items don't come
     * out even, a header counts the odd ones left, in a second. */
    size_t odd = n % CONTROL_ITEMS, odd_at = n - odd;
    uint8_t flags = long_runs ? LONG_RUNS : 0;
    Writing w = { out, FIRST_LENGTH, room };
    put_groups(&w, items, 0, odd_at, runs);
    size_t first = w.at - FIRST_LENGTH;
    if (odd) {
        size_t header = w.at;
        put(&w, (uint8_t)(flags | odd));
        put(&w, 0);
        put(&w, 0);
        put_items(&w, items + odd_at, odd, runs);
        if (w.at <= room) {
            out[header + 1] = (uint8_t)w.at;
            out[header + 2] = (uint8_t)(w.at >> 8);
        }
    }
    put(&w, flags);
    free(items);
    if (w.at > room || first > 0xFFFF || w.at > LZ_MOST)
        return 0;
    out[0] = (uint8_t)first;
    out[1] = (uint8_t)(first >> 8);
    return w.at;
}
