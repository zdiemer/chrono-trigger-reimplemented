/* Checks the unpacker (game/data/decompress.c) with no ROM, on streams made
 * up here: what replaced its comparison with the original's routine when the
 * data subsystem retired the gate (docs/REIMPLEMENTATION.md, "The data
 * subsystem's teardown").
 *
 * A stream is written by an encoder in this file from a list of items --
 * literal bytes and references back into what is already unpacked -- after
 * the format as the unpacker's header describes it: segments, each ending in
 * a header that says how many items the next control byte describes and where
 * the next segment ends; control bytes a bit an item from the lowest; a
 * control byte of 0 eight literals; references with a four-bit length and a
 * twelve-bit distance, or five and eleven where the first header's top bits
 * say so. What the items make is worked out beside the encoding, a byte at a
 * time as a reference copies, and the unpacker must make exactly that: the
 * bytes at the destination in either bank of work RAM, nothing after them,
 * the length it returns and keeps (decompress_length), and the work RAM port
 * left after what it wrote. One worked stream, then random ones: every
 * segment layout, a short first control byte after a header, its unused
 * high bits set, references that overlap what they copy, runs of literals as
 * a control byte of 0.
 *
 * Exits non-zero on failure.
 *
 *   unpack_test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "data/decompress.h"
#include "port/vars_ram.h"
#include "snes/system.h"
#include "vars.h"

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* ---- The cartridge, and the machine the unpacker runs on ---------------------------------- */

enum { ROM_SIZE = 0x400000, STREAM_BANK = 0xC4, STREAM_AT = 0x1000 };
static uint8_t *rom;
static Snes snes;

static uint32_t stream_address(void) { return (uint32_t)STREAM_BANK << 16 | STREAM_AT; }

/* ---- The encoder -------------------------------------------------------------------------- */

enum { MAX_STREAM = 0x8000, MAX_OUTPUT = 0x6000, MAX_ITEMS = 0x2000, SENTINEL = 0xA5 };

typedef struct {
    bool reference;
    uint8_t literal;
    uint16_t distance;
    uint8_t length;   /* 3 and up */
} Item;

typedef struct {
    uint8_t bytes[MAX_STREAM];
    unsigned n;
    uint8_t made[MAX_OUTPUT];   /* what the items make */
    unsigned made_n;
} Stream;

static void emit(Stream *s, uint8_t b) { s->bytes[s->n++] = b; }

static void make(Stream *s, const Item *item)
{
    if (!item->reference) {
        s->made[s->made_n++] = item->literal;
        return;
    }
    unsigned from = s->made_n - item->distance;
    for (unsigned k = 0; k < item->length; k++)
        s->made[s->made_n++] = s->made[from + k];   /* a byte at a time: a near reference repeats */
}

static void emit_item(Stream *s, const Item *item, bool long_runs)
{
    if (!item->reference) {
        emit(s, item->literal);
    } else {
        uint16_t word = long_runs ? (uint16_t)((item->length - 3) << 11 | item->distance)
                                  : (uint16_t)((item->length - 3) << 12 | item->distance);
        emit(s, (uint8_t)word);
        emit(s, (uint8_t)(word >> 8));
    }
    make(s, item);
}

/* A control byte and the items it describes, `count` of them. A short group's
 * control byte with no reference gets a bit past its items, which the
 * unpacker ignores, so it isn't read as eight literals. */
static void emit_group(Stream *s, const Item *items, unsigned count, bool long_runs)
{
    uint8_t control = 0;
    for (unsigned k = 0; k < count; k++)
        control |= (uint8_t)(items[k].reference << k);
    if (count < 8 && !control)
        control = 0x80;
    emit(s, control);
    for (unsigned k = 0; k < count; k++)
        emit_item(s, &items[k], long_runs);
}

/* A stream of `segments` segments: the first's control bytes describe eight
 * items each, a later one's first describes `first_counts[i]` (1 to 8) and
 * the rest eight; segment i has groups[i] control bytes after its first. */
static void encode(Stream *s, const Item *items, unsigned segments, const uint8_t *first_counts,
                   const unsigned *groups, bool long_runs)
{
    s->n = 0;
    s->made_n = 0;
    emit(s, 0);   /* the first segment's length, filled in where it ends */
    emit(s, 0);
    unsigned next = 0, header = 0;
    for (unsigned seg = 0; seg < segments; seg++) {
        unsigned first = seg == 0 ? 8 : first_counts[seg];
        emit_group(s, items + next, first, long_runs);
        next += first;
        for (unsigned k = 0; k < groups[seg]; k++, next += 8)
            emit_group(s, items + next, 8, long_runs);
        /* Where this segment ends: the first's as a length after its two
         * bytes, a later one's as a place in the stream, in the header
         * before it. */
        unsigned end = seg == 0 ? s->n - 2 : s->n;
        unsigned at = seg == 0 ? 0 : header + 1;
        s->bytes[at] = (uint8_t)end;
        s->bytes[at + 1] = (uint8_t)(end >> 8);
        uint8_t runs = seg == 0 && long_runs ? 0x40 : 0;
        header = s->n;
        if (seg + 1 == segments) {
            emit(s, runs);   /* no bits: the stream is over */
        } else {
            emit(s, (uint8_t)(runs | first_counts[seg + 1]));
            emit(s, 0);   /* where the next segment ends, filled in when it does */
            emit(s, 0);
        }
    }
}

/* ---- Running it --------------------------------------------------------------------------- */

static void run(const Stream *s, uint32_t to, const char *what)
{
    GameState *g = state_of(&snes);
    for (unsigned i = 0; i < s->n; i++)
        rom[(STREAM_BANK & 0x3F) << 16 | (STREAM_AT + i)] = s->bytes[i];
    uint8_t *wram = snes.bus.wram + (to & 0x1FFFF);
    memset(wram, SENTINEL, s->made_n + 16);
    set_decompress_source(g, stream_address());
    set_decompress_dest(g, to);
    uint32_t port_before = snes.bus.wram_port_addr;
    CHECK(unpack_stream_fits(g, stream_address(), (uint16_t)s->made_n), "%s: exact capacity refused", what);
    CHECK(unpack_stream_fits(g, stream_address(), (uint16_t)(s->made_n + 1)), "%s: spare capacity refused", what);
    CHECK(!unpack_stream_fits(g, stream_address(), (uint16_t)(s->made_n - 1)), "%s: short capacity accepted", what);
    CHECK(decompress_source(g) == stream_address() && decompress_dest(g) == to,
          "%s: extent check changed the unpacker's state", what);
    CHECK(snes.bus.wram_port_addr == port_before, "%s: extent check moved the work RAM port", what);
    for (unsigned i = 0; i < s->made_n + 16; i++)
        CHECK(wram[i] == SENTINEL, "%s: extent check wrote destination byte %u", what, i);
    uint16_t length = unpack_stream(g);
    CHECK(length == s->made_n, "%s: length %u, made %u", what, length, s->made_n);
    CHECK(decompress_length(g) == length, "%s: the length kept %u, returned %u", what, decompress_length(g), length);
    unsigned first = 0;
    while (first < s->made_n && wram[first] == s->made[first])
        first++;
    CHECK(first == s->made_n, "%s: byte %u of %u unpacked %02X, made %02X", what, first, s->made_n,
          first < s->made_n ? wram[first] : 0, first < s->made_n ? s->made[first] : 0);
    CHECK(wram[s->made_n] == SENTINEL, "%s: wrote past its end", what);
    uint32_t port = to + s->made_n;
    CHECK(snes.bus.wram_port_addr == port, "%s: the port left at %05X, not %05X", what,
          (unsigned)snes.bus.wram_port_addr, (unsigned)port);
}

/* ---- The worked stream -------------------------------------------------------------------- */

/* "ab" and a reference two back, five long, which copies what it makes as it
 * goes: "abababa"; then four literals "cdef" and a reference nine back, three
 * long, "aba"; one segment, short runs. */
static void test_worked(void)
{
    Item items[8] = {
        { false, 'a', 0, 0 }, { false, 'b', 0, 0 }, { true, 0, 2, 5 },   { false, 'c', 0, 0 },
        { false, 'd', 0, 0 }, { false, 'e', 0, 0 }, { false, 'f', 0, 0 }, { true, 0, 9, 3 },
    };
    static Stream s;
    static const uint8_t first_counts[3] = { 8 };
    static const unsigned groups[3] = { 0 };
    encode(&s, items, 1, first_counts, groups, false);
    static const uint8_t want_stream[] = {
        0x0B, 0x00,                                     /* the first segment: 11 bytes */
        0x84,                                           /* items 2 and 7 references */
        'a', 'b', 0x02, 0x20, 'c', 'd', 'e', 'f', 0x09, 0x00,
        0x00,                                           /* no bits: the end */
    };
    CHECK(s.n == sizeof want_stream && !memcmp(s.bytes, want_stream, s.n), "the worked stream's encoding");
    CHECK(s.made_n == 14 && !memcmp(s.made, "abababacdefaba", 14), "the worked stream's making: %.*s",
          (int)s.made_n, (const char *)s.made);
    run(&s, 0x7F2000, "the worked stream");
}

/* ---- Random streams ----------------------------------------------------------------------- */

static uint32_t seed = 0x2D206BF7u;
static unsigned next_random(unsigned bound)
{
    seed = seed * 1103515245u + 12345u;
    return (unsigned)(seed >> 8) % bound;
}

static void test_random(unsigned streams)
{
    static Item items[MAX_ITEMS];
    static Stream s;
    for (unsigned t = 0; t < streams; t++) {
        bool long_runs = next_random(2);
        unsigned segments = 1 + next_random(3);
        uint8_t first_counts[3];
        unsigned groups[3], total = 0;
        for (unsigned seg = 0; seg < segments; seg++) {
            first_counts[seg] = seg == 0 ? 8 : (uint8_t)(1 + next_random(8));
            groups[seg] = next_random(seg == 0 ? 40 : 12);
            total += first_counts[seg] + 8 * groups[seg];
        }
        unsigned made = 0, max_distance = long_runs ? 0x07FF : 0x0FFF, max_length = long_runs ? 34 : 18;
        bool literal_runs = next_random(4) == 0;   /* now and then whole groups of literals */
        for (unsigned i = 0; i < total; i++) {
            Item *it = &items[i];
            bool reference = made >= 1 && !literal_runs && next_random(5) < 2;
            if (reference) {
                unsigned reach = made < max_distance ? made : max_distance;
                it->reference = true;
                it->distance = (uint16_t)(1 + (next_random(3) ? next_random(reach < 16 ? reach : 16) : next_random(reach)));
                it->length = (uint8_t)(3 + next_random(max_length - 2));
                made += it->length;
            } else {
                *it = (Item){ false, (uint8_t)next_random(256), 0, 0 };
                made++;
            }
        }
        encode(&s, items, segments, first_counts, groups, long_runs);
        char what[64];
        snprintf(what, sizeof what, "random stream %u (%u segments, %s runs)", t, segments, long_runs ? "long" : "short");
        run(&s, t & 1 ? 0x7F4000u : 0x7E8000u, what);
    }
}

int main(void)
{
    rom = calloc(ROM_SIZE, 1);
    if (!rom)
        return 2;
    snes_init(&snes, rom, ROM_SIZE);
    test_worked();
    test_random(2000);
    free(rom);
    if (failures) {
        printf("unpack_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("unpack_test: the worked stream and 2000 random ones unpack as made\n");
    return 0;
}
