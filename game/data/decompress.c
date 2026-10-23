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
 * The unpacked length is kept for the callers (decompress_length). */
#include "data/decompress.h"

#include "charge.h"
#include "cpu_io.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "rom.h"
#include "vars.h"
#include "wram.h"

/* The stream: its bank, and the offset into it, read the way the original
 * reads a stream's bytes, an offset from the bank's start that runs on into
 * the next bank. */
typedef struct {
    GameState *g;
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

/* The decompressor's own cells, kept here as it works and left as the
 * original leaves them, which nothing reads after it: where the segment
 * ends, the item bits left of the control byte and the control bits left to
 * read, the last run's length and the last reference's place. */
typedef struct {
    uint16_t segment_end;
    uint8_t bits_left, control;
    bool controlled, referred;
    uint8_t run_length;
    uint16_t reference_at;
} Workings;

static void leave_the_cells(GameState *g, const Workings *l)
{
    set_decompress_segment_end(g, l->segment_end);
    set_decompress_bits_left(g, l->bits_left);
    if (l->controlled)
        set_decompress_control(g, l->control);
    set_decompress_copy_length(g, l->referred ? l->run_length : decompress_copy_length(g) & 0x00FF);
    if (l->referred)
        set_decompress_scratch(g, l->reference_at);
}

/* The original's time, which the timing gate measures the port by
 * (charge.h): it reads the stream and writes the output as the port does,
 * but keeps its place, its bits and its counts in its direct page's cells,
 * reading and writing them at each step where the port keeps them in locals.
 * Those accesses, by what it does: its cells set up, and read again at the
 * end; a look at whether the segment ends, at every turn of its loop; a
 * segment's header; a control byte; a reference, the run's length and its
 * place kept and read back, and the data bank kept on the stack over the
 * copy; and an item done, with the control byte going on or done. */
enum {
    ORIGINAL_START_AND_END = 9, ORIGINAL_LOOK_AT_THE_SEGMENT = 2, ORIGINAL_SEGMENT = 5, ORIGINAL_CONTROL = 1,
    ORIGINAL_REFERENCE = 11, ORIGINAL_ITEM_GOES_ON = 4, ORIGINAL_CONTROL_DONE = 3,
};

/* The first segment's length, and so where it ends, comes first. */
enum { FIRST_LENGTH_BYTES = 2, HEADER_BYTES = 3, BITS_IN_A_CONTROL = 8, HEADER_BITS = 0x3F };

// $C3:0557 (entered through the jump at $C3:0002)
uint16_t unpack_stream(GameState *g)
{
    uint32_t mark = 0, accesses = ORIGINAL_START_AND_END;
    uint32_t from = decompress_source(g), to = decompress_dest(g);
    Stream in = { g, (uint8_t)(from >> 16), (uint16_t)(from + FIRST_LENGTH_BYTES) };
    uint16_t first_length = (uint16_t)(rom_read8(g, from) | rom_read8(g, (from + 1) & 0xFFFFFF) << 8);
    Workings l = { .segment_end = (uint16_t)(in.at + first_length), .bits_left = BITS_IN_A_CONTROL };
    Output out = { g, (to >> 16) & 1 ? 0x7F0000u : 0x7E0000u, (uint16_t)to };
    hw_port_seek(g, out.at, (uint8_t)(to >> 16));
    bool long_runs = rom_read8(g, (uint32_t)in.bank << 16 | l.segment_end) & LONG_RUNS;

    for (;;) {
        accesses += ORIGINAL_LOOK_AT_THE_SEGMENT;
        if (in.at == l.segment_end) {
            uint8_t bits = stream_byte(&in, 0) & HEADER_BITS;
            if (!bits)
                break;
            accesses += ORIGINAL_SEGMENT;
            l.bits_left = bits;
            l.segment_end = (uint16_t)(stream_word(&in, 1) + (uint16_t)from);
            in.at += HEADER_BYTES;
            continue;
        }
        uint8_t control = stream_byte(&in, 0);
        if (!control) {
            for (uint16_t n = 1; n <= BITS_IN_A_CONTROL; n++)
                put_literal(&out, stream_byte(&in, n));
            in.at += 1 + BITS_IN_A_CONTROL;
            continue;
        }
        accesses += ORIGINAL_CONTROL;
        in.at++;
        bool reference = control & 1;
        l.control = control >> 1;
        l.controlled = true;
        for (;;) {
            if (reference) {
                Reference r = reference_at(&in, long_runs);
                l.referred = true;
                l.run_length = (uint8_t)(r.length_less_three + 2);
                l.reference_at = in.at;
                accesses += ORIGINAL_REFERENCE;
                copy_run(&out, r);
                in.at += 2;
            } else {
                put_literal(&out, stream_byte(&in, 0));
                in.at++;
            }
            if (--l.bits_left == 0) {
                accesses += ORIGINAL_CONTROL_DONE;
                l.bits_left = BITS_IN_A_CONTROL;
                break;
            }
            accesses += ORIGINAL_ITEM_GOES_ON;
            reference = l.control & 1;
            l.control >>= 1;
        }
    }

    uint16_t length = (uint16_t)(out.at - (uint16_t)to);
    set_decompress_length(g, length);
    ORIGINAL_EFFECT(mark = charge_uncounted_begin(g));
    ORIGINAL_EFFECT(leave_the_cells(g, &l));
    ORIGINAL_EFFECT(charge_uncounted_end(g, mark));
    ORIGINAL_EFFECT(charge_as(g, UNPACK_STREAM_ENTRY, accesses));
    (void)mark;
    (void)accesses;
    return length;
}

/* ---- The entry ------------------------------------------------------------------------------- */

/* The original saves every register on the stack and gives them all back,
 * N and Z as the data bank it pulls last sets them; on the way it takes the
 * stream's bank as its data bank through the stack, which leaves the bank in
 * the byte below the saved registers. */
void decompress(Snes *s)
{
    Cpu *c = &s->cpu;
    GameState *g = state_of(s);
    m_push8(s, c->db);
    m_push16(s, c->d);
    m_push8(s, c->p);
    m_push16(s, c->a);
    m_push16(s, c->x);
    m_push16(s, c->y);
    m_push8(s, (uint8_t)(decompress_source(g) >> 16));
    (void)m_pull8(s);
    unpack_stream(g);
    c->y = m_pull16(s);
    c->x = m_pull16(s);
    c->a = m_pull16(s);
    c->p = m_pull8(s);
    c->d = m_pull16(s);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_rtl(s);
}
