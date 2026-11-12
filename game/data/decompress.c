/* The unpacker: the game's compressed streams -- event scripts, graphics,
 * maps, and the code it runs from work RAM -- unpacked into work RAM.
 *
 * A stream is a run of segments. It starts with the first segment's length;
 * where a segment ends, a header byte says how many bits of the next control
 * byte count (none: the stream is over), and a word where the next segment
 * ends. Inside a segment, control bytes each describe the items after them,
 * a bit an item from the lowest: 0 a literal byte, 1 a reference two bytes
 * long to a run already unpacked -- how far back, and how long less three.
 * A reference's length takes the word's top four bits and its distance the
 * low twelve, or five and eleven where the first segment's header has either
 * of its top two bits set. A control byte of 0 stands for eight literals,
 * however many bits the segment's header said count.
 *
 * Literals go out through the work RAM port, and a reference copies inside
 * the destination's bank of work RAM, then points the port after what it
 * copied: so what the port leaves pointing at is where unpacking stopped.
 * The unpacked length is kept for the callers (decompress_length).
 *
 * The gate no longer compares it with the original's routine (REIMPLEMENTATION.md,
 * "The data subsystem's teardown"): it has no registered entry, every port
 * calls it as C, and the cells the original keeps its place in as it works
 * ($0309-$0316), which nothing else reads, it doesn't keep at all. */
#include "data/decompress.h"

#include <stdbool.h>

#include "charge.h"
#include "cpu_io.h"
#include "rom.h"
#include "vars.h"
#include "wram.h"

/* The stream: its bank, and the offset into it, read the way the original
 * reads a stream's bytes, an offset from the bank's start that runs on into
 * the next bank. */
typedef struct {
    const GameState *g;
    uint8_t bank;
    uint16_t at;
} Stream;

static uint8_t stream_byte(const Stream *in, uint16_t ahead)
{
    return rom_read8(in->g, ((uint32_t)in->bank << 16) + in->at + ahead);
}

static uint16_t stream_word(const Stream *in, uint16_t ahead)
{
    return (uint16_t)(stream_byte(in, ahead) | stream_byte(in, (uint16_t)(ahead + 1)) << 8);
}

/* A reference's two bytes: how far back the run lies, the word's low bits,
 * and how long the run is, its high byte's top bits. */
typedef struct {
    uint16_t distance;
    uint8_t length_less_three;
} Reference;

enum { LONG_RUNS = 0xC0 };   /* the first header's bits that make a reference's length five bits */

static Reference reference_at(const Stream *in, bool long_runs)
{
    uint8_t high = stream_byte(in, 1);
    uint16_t word = stream_word(in, 0);
    return long_runs ? (Reference){ word & 0x07FF, (uint8_t)(high >> 3) }
                     : (Reference){ word & 0x0FFF, (uint8_t)(high >> 4) };
}

/* Where the output goes: the place in its bank of work RAM. */
typedef struct {
    GameState *g;
    uint32_t bank;    /* $7E0000 or $7F0000 */
    uint16_t at;
} Output;

static void put_literal(Output *out, uint8_t v)
{
    hw_port_write(out->g, v);
    out->at++;
}

static void copy_run(Output *out, Reference r)
{
    uint16_t from = (uint16_t)(out->at - r.distance);
    for (unsigned n = r.length_less_three + 3u; n > 0; n--, from++, out->at++)
        wram_write8(out->g, out->bank | out->at, wram_read8(out->g, out->bank | from));
    hw_port_seek_address(out->g, out->at);
}

/* The unpacking keeps the rates its entries were calibrated at (charge.h,
 * charge_retired): the port's own accesses at the rate of the
 * entry it was reached through, and the accesses the original makes besides
 * at the jump's entry's, as its charge was made. Both backends use those
 * retained rates. The original reads the stream
 * and writes the output as the port does, but keeps its place, its bits and
 * its counts in its direct page's cells, reading and writing them at each
 * step where the port keeps them in locals. Those accesses, by what
 * it does: its cells set up, and read again at the end; a look at whether
 * the segment ends, at every turn of its loop; a segment's header; a control
 * byte; a reference, the run's length and its place kept and read back, and
 * the data bank kept on the stack over the copy; and an item done, with the
 * control byte going on or done. */
enum { ORIGINAL_START_AND_END = 9, ORIGINAL_LOOK_AT_THE_SEGMENT = 2, ORIGINAL_SEGMENT = 5, ORIGINAL_CONTROL = 1, ORIGINAL_REFERENCE = 11, ORIGINAL_ITEM_GOES_ON = 4, ORIGINAL_CONTROL_DONE = 3,  };

/* The routines the unpacking is timed as: the jump the game's code calls, or
 * the routine itself, which the scene engine calls directly; their entries
 * were calibrated apart. */
enum { THE_UNPACKER = 0xC30002, THE_UNPACKER_DIRECT = 0xC30557 };

/* What the unpacking came to, counted as it goes: the turns of its loop,
 * the segments' headers, the control bytes, the references, and the items
 * the control bytes said. Every control byte's run ends with its last item;
 * the others go on to the next. */
typedef struct {
    uint32_t turns, segments, controls, references, items;
} Counts;

static uint32_t original_accesses(const Counts *n)
{
    return ORIGINAL_START_AND_END + n->turns * ORIGINAL_LOOK_AT_THE_SEGMENT + n->segments * ORIGINAL_SEGMENT +
           n->controls * (ORIGINAL_CONTROL + ORIGINAL_CONTROL_DONE) + n->references * ORIGINAL_REFERENCE +
           (n->items - n->controls) * ORIGINAL_ITEM_GOES_ON;
}

/* The first segment's length, and so where it ends, comes first. */
enum { FIRST_LENGTH_BYTES = 2, HEADER_BYTES = 3, BITS_IN_A_CONTROL = 8, HEADER_BITS = 0x3F };

/* Check a packed resource's output extent without changing the destination or
 * the unpacker's state. Input is bounded to one bank as well, so a malformed
 * stream whose headers cycle cannot make a resource guard run forever. */
static bool stream_fits(const GameState *g, uint32_t from, uint16_t capacity)
{
    Stream in = { g, (uint8_t)(from >> 16), (uint16_t)(from + FIRST_LENGTH_BYTES) };
    uint16_t first_length = (uint16_t)(rom_read8(g, from) | rom_read8(g, (from + 1) & 0xFFFFFF) << 8);
    uint16_t segment_end = (uint16_t)(in.at + first_length);
    bool long_runs = rom_read8(g, (uint32_t)in.bank << 16 | segment_end) & LONG_RUNS;
    uint8_t bits_left = BITS_IN_A_CONTROL;
    uint32_t input_left = 0x10000u - FIRST_LENGTH_BYTES;
    for (;;) {
        if (input_left == 0)
            return false;
        if (in.at == segment_end) {
            uint8_t bits = stream_byte(&in, 0) & HEADER_BITS;
            if (!bits)
                return true;
            if (input_left < HEADER_BYTES)
                return false;
            bits_left = bits;
            segment_end = (uint16_t)(stream_word(&in, 1) + (uint16_t)from);
            in.at += HEADER_BYTES;
            input_left -= HEADER_BYTES;
            continue;
        }
        uint8_t control = stream_byte(&in, 0);
        in.at++;
        input_left--;
        if (!control) {
            if (capacity < BITS_IN_A_CONTROL || input_left < BITS_IN_A_CONTROL)
                return false;
            capacity -= BITS_IN_A_CONTROL;
            in.at += BITS_IN_A_CONTROL;
            input_left -= BITS_IN_A_CONTROL;
            continue;
        }
        do {
            bool reference = control & 1;
            uint8_t encoded = reference ? 2 : 1;
            if (input_left < encoded)
                return false;
            uint8_t length = reference ? (uint8_t)(reference_at(&in, long_runs).length_less_three + 3) : 1;
            if (capacity < length)
                return false;
            capacity -= length;
            in.at += encoded;
            input_left -= encoded;
            control >>= 1;
        } while (--bits_left);
        bits_left = BITS_IN_A_CONTROL;
    }
}

/* Validation is outside the game's work estimate, as it is when the
 * runtime asks an entry guard. The unpacking itself keeps its existing time. */
bool unpack_stream_fits(GameState *g, uint32_t from, uint16_t capacity)
{
    uint32_t mark = charge_uncounted_begin(g);
    bool fits = stream_fits(g, from, capacity);
    charge_uncounted_end(g, mark);
    return fits;
}

static uint16_t unpack(GameState *g, Counts *n)
{
    uint32_t from = decompress_source(g), to = decompress_dest(g);
    Stream in = { g, (uint8_t)(from >> 16), (uint16_t)(from + FIRST_LENGTH_BYTES) };
    uint16_t first_length = (uint16_t)(rom_read8(g, from) | rom_read8(g, (from + 1) & 0xFFFFFF) << 8);
    uint16_t segment_end = (uint16_t)(in.at + first_length);
    uint8_t bits_left = BITS_IN_A_CONTROL;
    Output out = { g, (to >> 16) & 1 ? 0x7F0000u : 0x7E0000u, (uint16_t)to };
    hw_port_seek(g, out.at, (uint8_t)(to >> 16));
    bool long_runs = rom_read8(g, (uint32_t)in.bank << 16 | segment_end) & LONG_RUNS;

    for (;;) {
        n->turns++;
        if (in.at == segment_end) {
            uint8_t bits = stream_byte(&in, 0) & HEADER_BITS;
            if (!bits)
                break;
            n->segments++;
            bits_left = bits;
            segment_end = (uint16_t)(stream_word(&in, 1) + (uint16_t)from);
            in.at += HEADER_BYTES;
            continue;
        }
        uint8_t control = stream_byte(&in, 0);
        if (!control) {
            for (uint16_t k = 1; k <= BITS_IN_A_CONTROL; k++)
                put_literal(&out, stream_byte(&in, k));
            in.at += 1 + BITS_IN_A_CONTROL;
            continue;
        }
        n->controls++;
        in.at++;
        bool reference = control & 1;
        control >>= 1;
        for (;; n->items++) {
            if (reference) {
                n->references++;
                copy_run(&out, reference_at(&in, long_runs));
                in.at += 2;
            } else {
                put_literal(&out, stream_byte(&in, 0));
                in.at++;
            }
            if (--bits_left == 0) {
                n->items++;
                bits_left = BITS_IN_A_CONTROL;
                break;
            }
            reference = control & 1;
            control >>= 1;
        }
    }
    return (uint16_t)(out.at - (uint16_t)to);
}

// $C3:0557 (entered through the jump at $C3:0002)
/* The stream unpacked, timed as `routine`, with `besides` accesses of the
 * entry it was reached through. */
static uint16_t unpack_timed_as(GameState *g, uint32_t routine, uint32_t besides)
{
    Counts n = { 0 };
    uint32_t mark = charge_retired_begin(g);
    uint16_t length = unpack(g, &n);
    set_decompress_length(g, length);
    charge_retired_end(g, mark, routine, besides);
    charge_retired_end(g, charge_retired_begin(g), THE_UNPACKER, original_accesses(&n));
    return length;
}

uint16_t unpack_stream(GameState *g) { return unpack_timed_as(g, THE_UNPACKER, 0); }

uint16_t unpack_stream_direct(GameState *g) { return unpack_timed_as(g, THE_UNPACKER_DIRECT, 0); }

/* Where a machine half reached it through its entry, the entry's own time
 * goes with it: it saved every register on the stack and gave them back, and
 * returned, 25 accesses. */
enum { THE_ENTRY_S_OWN = 25 };

uint16_t unpack_stream_run(GameState *g, bool direct)
{
    return unpack_timed_as(g, direct ? THE_UNPACKER_DIRECT : THE_UNPACKER, THE_ENTRY_S_OWN);
}

uint16_t unpack_stream_called(GameState *g, bool direct)
{
    uint16_t length = unpack_stream_run(g, direct);
    return length;
}
