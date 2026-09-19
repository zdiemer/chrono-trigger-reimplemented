/* Battle: window effects in bank D1.
 *
 * The battle's two windows are shaped per line: the window line effect
 * (battle_setup_window_hdma, $CD:0AEE) reads, for each of the 224 lines, four
 * bytes -- window 1's left and right edge, window 2's -- from one of two
 * buffers at battle_window_lines ($7E:C161 and $7E:C4E1), switching each
 * frame. The effects here write the buffer for the coming frame: the second
 * one on odd frames (battle_frame_count bit 0), the first on even ones. An
 * empty window has its left edge right of its right edge ($FF, $00); a full
 * one runs from 0 to $FF. Four more lines' entries before the first buffer
 * ($C151) are the top border's, and some effects start one line early into
 * them, as the original does.
 *
 * The screen script commands among them (screen.h) take their operand at the
 * script pointer. */
#include "battle/screen.h"

#include "battle/screen_data.h"
#include "port/machine.h"
#include "ram.h"

enum {
    LINE_BYTES = RAM_BATTLE_WINDOW_LINES_STRIDE,
    LINES = 224,
    BUFFER_BYTES = LINES * LINE_BYTES,
    W1 = 0,               /* offset of window 1's edges in a line entry */
    W2 = 2,
    EDGES_EMPTY = 0x00FF,  /* left $FF, right $00 */
    EDGES_FULL = 0xFF00,   /* left 0, right $FF */
    WAVE_END = 0x0350,     /* the wave effects' byte offset: two lines a step, 212 lines */
    BAND_LINES = 212,
};

/* Two step counts the effects keep between calls: the band's half width, the
 * wave's accumulated operand or the wipe's steps (A), and the band's half
 * height or the counted wave's calls (B). */
#define STEPS_A RAM_BATTLE_WINDOW_STEPS
#define STEPS_B (RAM_BATTLE_WINDOW_STEPS + 1)

/* ---- The machine at the edges -------------------------------------------------------- */

static void set_a(Snes *s, uint16_t a)
{
    s->cpu.a = a;
    s->cpu.p |= CPU_FLAG_M;
}

static bool odd_frame(Snes *s) { return m_read8(s, RAM_BATTLE_FRAME_COUNT) & 0x01; }

/* The buffer the coming frame shows, at a byte offset (which may run into the
 * next buffer or back into the border, as the original's indexing does). */
static uint32_t lines_at(bool odd, uint32_t offset) { return RAM_BATTLE_WINDOW_LINES + (odd ? BUFFER_BYTES : 0) + offset; }

static uint8_t operand(Snes *s, uint16_t n) { return m_read8(s, (m_read16(s, RAM_BATTLE_SCREEN_SCRIPT) | (uint32_t)m_read8(s, RAM_BATTLE_SCREEN_SCRIPT + 2) << 16) + n); }

static uint8_t scratch(Snes *s, uint8_t cell) { return m_read8(s, RAM_BATTLE_SCRATCH + cell); }
static void set_scratch(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, RAM_BATTLE_SCRATCH + cell, v); }
static uint16_t scratch16(Snes *s, uint8_t cell) { return m_read16(s, RAM_BATTLE_SCRATCH + cell); }
static void set_scratch16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, RAM_BATTLE_SCRATCH + cell, v); }

/* The scratch cells, by what the effects keep there ($45 up). */
enum {
    CELL_PHASE2 = 0x00,   /* $45 */
    CELL_PHASE1 = 0x02,   /* $47 */
    CELL_MASK = 0x04,     /* $49 */
    CELL_AMP1 = 0x06,     /* $4B */
    CELL_AMP2 = 0x08,     /* $4D */
    CELL_BASE1 = 0x0A,    /* $4F */
    CELL_BASE2 = 0x0C,    /* $51 */
};

/* The high byte of a product, through the hardware multiplier as the original
 * uses it (its operands and result stay in its registers). */
static void multiply_start(Snes *s, uint8_t a, uint8_t b)
{
    m_write8(s, 0x004202, a);
    m_write8(s, 0x004203, b);
}
static uint8_t product_high(Snes *s) { return m_read8(s, 0x004217); }

static uint16_t sign_extend(uint8_t v) { return (v & 0x80) ? (uint16_t)(0xFF00 | v) : v; }

// $D1:EDCD
/* Screen command $EE: open a band of window around the first animation
 * position, one step a frame. Operand 0: window 1 on every line takes the
 * columns $5DA0 +- the band's half width (clamped to the screen), which grows
 * to 8. Otherwise window 2 covers whole lines from $5DA1 - its half height to
 * $5DA1 + it (at most line $D4), growing to 9. */
static void open_band(Snes *s)
{
    bool odd = odd_frame(s);
    if (operand(s, 0) == 0) {
        uint8_t center = m_read8(s, RAM_BATTLE_ANIMATION_POSITIONS);
        uint8_t half = m_read8(s, STEPS_A);
        uint8_t left = center >= half ? (uint8_t)(center - half) : 0;
        uint8_t right = center + half > 0xFF ? 0xFF : (uint8_t)(center + half);
        set_scratch(s, 0, left);
        set_scratch(s, 1, right);
        if (half < 8)
            m_write8(s, STEPS_A, (uint8_t)(half + 1));
        for (uint16_t line = 0; line < BAND_LINES; line++)
            m_write16(s, lines_at(odd, line * LINE_BYTES + W1), (uint16_t)(right << 8 | left));
        return;
    }
    uint8_t center = m_read8(s, RAM_BATTLE_ANIMATION_POSITIONS + 1);
    uint8_t half = m_read8(s, STEPS_B);
    uint8_t top = center >= half ? (uint8_t)(center - half) : 0;
    uint16_t bottom = (uint16_t)(center + half);
    if (bottom > 0xFF || bottom >= 0xD4)
        bottom = 0xD4;
    set_scratch(s, 0, top);
    set_scratch(s, 1, (uint8_t)bottom);
    if (half < 9)
        m_write8(s, STEPS_B, (uint8_t)(half + 1));
    uint16_t at = (uint16_t)(top << 2), end = (uint16_t)(bottom << 2);
    set_scratch16(s, 0, end);
    for (; at != end; at = (uint16_t)(at + LINE_BYTES))
        m_write16(s, lines_at(odd, (uint32_t)at + W2), EDGES_FULL);
}

void battle_screen_open_band(Snes *s)
{
    open_band(s);
    set_a(s, 0);
    m_rtl(s);
}

// $D1:EEA5
/* Screen command $EC: window 2 reaches over from the left by one more column
 * on each line going up from line $BF - its step count (the count grows by
 * one a call): line n (from -1) gets the columns up to n + 1. The start is
 * computed from A with its high byte, as the original's 16-bit shift does. */
static void wipe_from_left(Snes *s, uint16_t a_high)
{
    uint8_t steps = m_read8(s, STEPS_A);
    uint16_t at = (uint16_t)((a_high | (uint8_t)(0xC0 - steps)) << 2);
    if (!(at & 0x8000)) {
        uint16_t v = 0;
        do {
            m_write16(s, RAM_BATTLE_WINDOW_LINES - LINE_BYTES + W2 + at, v);
            v++;
            at = (uint16_t)(at - LINE_BYTES);
        } while (!(at & 0x8000));
    }
    /* a 16-bit INC: it carries into the other count */
    m_write16(s, STEPS_A, (uint16_t)(m_read16(s, STEPS_A) + 1));
}

void battle_screen_wipe_from_left(Snes *s)
{
    wipe_from_left(s, s->cpu.a & 0xFF00);
    set_a(s, 0);
    m_rtl(s);
}

/* ---- Waves ---------------------------------------------------------------------------- */

/* The product the multiplier holds as a signed offset: its high byte, negated
 * for a phase in the second half of the turn. */
static uint16_t signed_product(Snes *s, bool negative)
{
    uint8_t v = product_high(s);
    if (negative)
        v = (uint8_t)-v;
    return sign_extend(v);
}

/* Window 1's edges for the line pair at `at`: `base` moved by `offset`; then
 * start the product for window 2 (the sine of phase 2 times amplitude 2).
 * Returns whether phase 2 is in the negative half. */
static bool set_wave_w1(Snes *s, uint16_t at, uint16_t offset, uint16_t base)
{
    uint16_t w1 = (uint16_t)(offset + base);
    m_write16(s, RAM_BATTLE_WINDOW_LINES - LINE_BYTES + W1 + at, w1);
    m_write16(s, RAM_BATTLE_WINDOW_LINES + W1 + at, w1);
    uint8_t phase2 = scratch(s, CELL_PHASE2);
    multiply_start(s, battle_sine(s, phase2), scratch(s, CELL_AMP2));
    return phase2 & 0x80;
}

// $D1:F0E9
/* Window 2's edges for the line pair at `at`: the second base moved by
 * `offset`. Returns the edges. */
static uint16_t set_wave_w2(Snes *s, uint16_t at, uint16_t offset)
{
    uint16_t w2 = (uint16_t)(offset + scratch16(s, CELL_BASE2));
    m_write16(s, RAM_BATTLE_WINDOW_LINES - LINE_BYTES + W2 + at, w2);
    m_write16(s, RAM_BATTLE_WINDOW_LINES + W2 + at, w2);
    return w2;
}

/* Both windows of the first buffer shaped as waves down the screen, two lines
 * a step for 212 lines: window 1's edges are its base (per step from
 * battle_wave_edges, or one value) moved by amplitude 1 times the sine of
 * phase 1, window 2's the second base moved by amplitude 2 times the sine of
 * phase 2; both phases step once a line pair. The phases, amplitudes and
 * bases are in the scratch cells. */
static void draw_waves(Snes *s, bool base1_per_step)
{
    for (uint16_t at = 0; at != WAVE_END; at = (uint16_t)(at + 2 * LINE_BYTES)) {
        uint8_t phase1 = scratch(s, CELL_PHASE1);
        multiply_start(s, battle_sine(s, phase1), scratch(s, CELL_AMP1));
        uint16_t d1 = signed_product(s, phase1 & 0x80);
        uint16_t base1 = base1_per_step ? m_read16(s, RAM_BATTLE_WAVE_EDGES + at) : scratch16(s, CELL_BASE1);
        bool negative2 = set_wave_w1(s, at, d1, base1);
        set_wave_w2(s, at, signed_product(s, negative2));
        set_scratch(s, CELL_PHASE2, (uint8_t)(scratch(s, CELL_PHASE2) + 1));
        set_scratch(s, CELL_PHASE1, (uint8_t)(scratch(s, CELL_PHASE1) + 1));
    }
}

/* $D1:EF63, $D1:F07E and $D1:F0E9 as entries of their own: the offset in A,
 * sign-extended by the N flag the caller's last load left, the line pair in
 * Y. The first two return with A holding amplitude 2 and the carry saying
 * whether phase 2 is negative. */
static uint16_t entry_offset(Snes *s)
{
    return (s->cpu.p & CPU_FLAG_N) ? (uint16_t)(s->cpu.a | 0xFF00) : s->cpu.a;
}

static void finish_w1(Snes *s, bool negative2)
{
    uint8_t phase2 = scratch(s, CELL_PHASE2);
    s->cpu.x = phase2;
    s->cpu.a = scratch(s, CELL_AMP2);
    s->cpu.p |= CPU_FLAG_M;
    m_set_flag(s, CPU_FLAG_C, negative2);
    m_rts(s);
}

// $D1:EF63
void battle_wave_w1_from_table(Snes *s)
{
    uint16_t at = s->cpu.y;
    finish_w1(s, set_wave_w1(s, at, entry_offset(s), m_read16(s, RAM_BATTLE_WAVE_EDGES + at)));
}

// $D1:F07E
void battle_wave_w1(Snes *s)
{
    finish_w1(s, set_wave_w1(s, s->cpu.y, entry_offset(s), scratch16(s, CELL_BASE1)));
}

void battle_wave_w2(Snes *s)
{
    s->cpu.a = set_wave_w2(s, s->cpu.y, entry_offset(s));
    s->cpu.p |= CPU_FLAG_M;
    m_rts(s);
}

/* The bases from BG3's moving scroll: horizontal for window 1, vertical for
 * window 2. */
static void set_wave_bases(Snes *s)
{
    set_scratch16(s, CELL_BASE1, (uint16_t)(m_read16(s, RAM_BATTLE_BG3_SCROLL) + m_read16(s, RAM_BATTLE_BG3_SCROLL_OFFSET)));
    set_scratch16(s, CELL_BASE2,
                  (uint16_t)(m_read16(s, RAM_BATTLE_BG3_SCROLL + 2) + m_read16(s, RAM_BATTLE_BG3_SCROLL_OFFSET + 2)));
}

/* The wave's amplitude falls as the operand, added up in the band width cell,
 * grows: $60 less it. */
static uint8_t add_to_wave_steps(Snes *s)
{
    uint8_t steps = (uint8_t)(m_read8(s, STEPS_A) + operand(s, 0));
    m_write8(s, STEPS_A, steps);
    return (uint8_t)(0x60 - steps);
}

/* The phases from the frame count, as the original shifts it (8-bit, with A's
 * high byte in the index's). */
static void set_frame_phases(Snes *s, uint16_t a_high)
{
    uint8_t frame = m_read8(s, RAM_BATTLE_FRAME_COUNT);
    set_scratch16(s, CELL_PHASE2, (uint16_t)(a_high | (uint8_t)(frame << 2)));
    set_scratch16(s, CELL_PHASE1, (uint16_t)(a_high | (uint8_t)(frame << 1)));
}

// $D1:EEC4
/* Screen command $ED: waves on window 1 around the per-line edges at
 * $7E:E500, and on window 2 around BG3's vertical scroll at half the
 * amplitude, the phases following the frame count. */
static void wave_windows(Snes *s, uint16_t a_high)
{
    uint8_t amp = add_to_wave_steps(s);
    set_scratch(s, CELL_AMP1, amp);
    set_scratch(s, CELL_AMP2, (uint8_t)(amp >> 1));
    set_frame_phases(s, a_high);
    set_wave_bases(s);
    draw_waves(s, true);
}

void battle_screen_wave_windows(Snes *s)
{
    wave_windows(s, s->cpu.a & 0xFF00);
    set_a(s, 0);
    m_rtl(s);
}

/* Both windows waving around BG3's moving scroll with one amplitude. */
static void wave_around_scroll(Snes *s)
{
    uint8_t amp = add_to_wave_steps(s);
    set_scratch(s, CELL_AMP1, amp);
    set_scratch(s, CELL_AMP2, amp);
    set_wave_bases(s);
    draw_waves(s, false);
}

// $D1:EFD0
/* Screen command $F3: waves around BG3's scroll, the phases from a count of
 * calls that starts at $A8 (the count grows by one a call). */
void battle_screen_wave_counted(Snes *s)
{
    uint16_t a_high = s->cpu.a & 0xFF00;
    uint8_t steps = m_read8(s, STEPS_B);
    uint8_t phase = (uint8_t)((uint8_t)(steps + 0xA8) << 1);
    set_scratch16(s, CELL_PHASE1, (uint16_t)(a_high | phase));
    set_scratch16(s, CELL_PHASE2, (uint16_t)(a_high | (uint8_t)(phase << 1)));
    m_write8(s, STEPS_B, (uint8_t)(steps + 1));
    wave_around_scroll(s);
    set_a(s, 0);
    m_rtl(s);
}

// $D1:EFE3
/* Screen command $EB: the same, the phases following the frame count. */
void battle_screen_wave_framed(Snes *s)
{
    set_frame_phases(s, s->cpu.a & 0xFF00);
    wave_around_scroll(s);
    set_a(s, 0);
    m_rtl(s);
}

// $D1:F108
/* Screen command $80, operation 13: add the per-line deltas at $0A00 to the
 * edges of lines 48-111 and 112-175 of the coming frame's buffer (window 1 by
 * the first 64, window 2 by those at $0A40 on odd frames and $0A20 on even
 * ones). The additions run as one long ADC chain: the carry of each goes
 * into the next, starting with the carry the caller left. Returns the last
 * byte written. */
static uint8_t add_line_deltas(Snes *s, bool carry)
{
    bool odd = odd_frame(s);
    uint16_t w2_deltas = odd ? 0x40 : 0x20;
    uint8_t last = 0;
    for (uint16_t line = 0; line < 64; line++) {
        uint8_t d1 = m_read8(s, RAM_BATTLE_WINDOW_LINE_DELTAS + line);
        uint8_t d2 = m_read8(s, RAM_BATTLE_WINDOW_LINE_DELTAS + w2_deltas + line);
        for (int half = 0; half < 2; half++) {
            uint32_t entry = lines_at(odd, (uint32_t)(48 + 64 * half + line) * LINE_BYTES);
            for (int edge = 0; edge < 4; edge++) {
                uint16_t sum = (uint16_t)(m_read8(s, entry + edge) + (edge < 2 ? d1 : d2) + carry);
                carry = sum > 0xFF;
                last = (uint8_t)sum;
                m_write8(s, entry + edge, last);
            }
        }
    }
    return last;
}

void battle_screen_add_line_deltas(Snes *s)
{
    uint8_t last = add_line_deltas(s, m_flag(s, CPU_FLAG_C));
    m_set_a8(s, last);
    m_rtl(s);
}

// $D1:F5F5
/* Screen command $DB: the coming frame's window 2 takes the other buffer's
 * window 1, on every line. */
void battle_screen_copy_window1_to_window2(Snes *s)
{
    bool odd = odd_frame(s);
    for (uint16_t line = 0; line < LINES; line++)
        m_write16(s, lines_at(odd, line * LINE_BYTES + W2), m_read16(s, lines_at(!odd, line * LINE_BYTES + W1)));
    set_a(s, 0);
    m_rtl(s);
}

// $D1:FD7E
/* Screen command $CF: the same for 224 lines from twice the operand bytes in
 * (an odd operand shifts the entries by half), with the window turned inside
 * out: the left edge from the other's right one inverted, the right from its
 * left. */
static void copy_inverted(Snes *s, uint8_t op)
{
    bool odd = odd_frame(s);
    uint8_t at = (uint8_t)(op << 1);
    uint16_t end = (uint16_t)(at + 0x70);
    set_scratch16(s, 0, end);
    do {
        for (int block = 0; block < 8; block++) {
            uint32_t offset = (uint32_t)block * 0x70 + at;
            uint16_t v = (uint16_t)~m_read16(s, lines_at(!odd, offset + W1));
            m_write16(s, lines_at(odd, offset + W2), (uint16_t)(v << 8 | v >> 8));
        }
        at = (uint8_t)(at + LINE_BYTES);
    } while (at != (uint8_t)end);
}

void battle_screen_copy_window1_inverted(Snes *s)
{
    copy_inverted(s, operand(s, 0));
    set_a(s, 0);
    m_rtl(s);
}

// $D1:FB68
/* Screen command $E9: add the operand to the column band's left edge. */
void battle_screen_move_column_band(Snes *s)
{
    uint8_t left = (uint8_t)(m_read8(s, RAM_BATTLE_COLUMN_BAND_LEFT) + operand(s, 0));
    m_write8(s, RAM_BATTLE_COLUMN_BAND_LEFT, left);
    m_set_a8(s, left);
    m_rtl(s);
}

// $D1:FB72
/* Screen command $E8: grow the column band's half height by the operand and
 * draw it: window 1 on the lines $6A +- the half height covers the columns
 * from its left edge to 255 less it. The first line is computed from A with
 * its high byte, as the original's 16-bit shift does. */
static void draw_column_band(Snes *s, uint16_t a_high)
{
    uint8_t half = (uint8_t)(m_read8(s, RAM_BATTLE_COLUMN_BAND_HALF_HEIGHT) + operand(s, 0));
    m_write8(s, RAM_BATTLE_COLUMN_BAND_HALF_HEIGHT, half);
    uint8_t left = m_read8(s, RAM_BATTLE_COLUMN_BAND_LEFT);
    set_scratch(s, CELL_MASK, left);
    set_scratch(s, CELL_MASK + 1, (uint8_t)~left);
    uint8_t bottom = (uint8_t)(half + 0x6A);
    set_scratch(s, 0, bottom);
    uint16_t at = (uint16_t)((a_high | (uint8_t)(0x6A - half)) << 2);
    uint16_t end = (uint16_t)(bottom << 2);
    set_scratch16(s, 0, end);
    if (at == end)
        return;
    bool odd = odd_frame(s);
    uint16_t edges = scratch16(s, CELL_MASK);
    for (; at != end; at = (uint16_t)(at + LINE_BYTES))
        m_write16(s, lines_at(odd, (uint32_t)at + W1), edges);
}

void battle_screen_draw_column_band(Snes *s)
{
    draw_column_band(s, s->cpu.a & 0xFF00);
    set_a(s, 0);
    m_rtl(s);
}

/* The fills $D1:FBCE dispatches to: the value each line gets, which window,
 * and whether both. */
typedef struct {
    uint16_t handler;
    uint16_t edges;
    uint8_t window, words;
} Fill;
static const Fill fills[] = {
    { 0xFCF4, EDGES_EMPTY, W1, 2 },   /* both windows */
    { 0xFC38, EDGES_EMPTY, W1, 1 },
    { 0xFC96, EDGES_EMPTY, W2, 1 },
    { 0xFBDA, EDGES_FULL, W1, 1 },
};

/* Fill the coming frame's windows on every line. The fill's value stays in
 * A, which the original leaves 8-bit with its high byte. */
static void fill(Snes *s, const Fill *f)
{
    bool odd = odd_frame(s);
    for (uint16_t line = 0; line < LINES; line++)
        for (uint8_t w = 0; w < f->words; w++)
            m_write16(s, lines_at(odd, line * LINE_BYTES + f->window + 2u * w), f->edges);
    set_a(s, f->edges);
    s->cpu.x = 0;
}

// $D1:FBCE
/* Screen command $88: set the coming frame's windows on every line, by the
 * operation in X (twice the operand): 0 both windows empty, 1 window 1
 * empty, 2 window 2 empty, 3 window 1 full. Operations past the table jump
 * where the original's table points, as a call-out. */
void battle_screen_fill_windows(Snes *s)
{
    uint16_t target = screen_dispatch_target(s, 0xFBD2, s->cpu.x);
    for (size_t i = 0; i < sizeof fills / sizeof fills[0]; i++) {
        if (fills[i].handler == target) {
            fill(s, &fills[i]);
            m_rtl(s);
            return;
        }
    }
    m_jsr(s, 0xD1FBCE, 0xD10000 | target);
    m_rtl(s);
}

// $D1:FCF4
void battle_fill_windows_empty(Snes *s) { fill(s, &fills[0]); m_rts(s); }
// $D1:FC38
void battle_fill_window1_empty(Snes *s) { fill(s, &fills[1]); m_rts(s); }
// $D1:FC96
void battle_fill_window2_empty(Snes *s) { fill(s, &fills[2]); m_rts(s); }
// $D1:FBDA
void battle_fill_window1_full(Snes *s) { fill(s, &fills[3]); m_rts(s); }

// $D1:EBFF
/* Screen command $F4: a beam from the top of the screen to the first
 * animation position, as window quadrilateral X (the operand): its top edge
 * 32 columns wide from $B0 plus 40 times the sine of the frame count, its
 * bottom edge 32 columns wide around the position. $D1:F9AF rasterizes it,
 * called out: it writes the window table through the work RAM port with
 * 16-bit stores whose low bytes land on APU port 3, which no service
 * expresses (docs/TODO.md). The index takes A's high byte, as the original's
 * TAX does. */
void battle_screen_draw_beam(Snes *s)
{
    uint16_t a_high = s->cpu.a & 0xFF00;
    uint8_t frame = m_read8(s, RAM_BATTLE_FRAME_COUNT);
    m_write8(s, 0x004202, 0x28);
    m_write8(s, 0x004203, battle_sine(s, (uint16_t)(a_high | frame)));
    uint16_t quad = (uint16_t)(a_high | operand(s, 0));
    uint8_t swing = product_high(s);
    if (frame & 0x80)
        swing = (uint8_t)-swing;
    uint32_t q = RAM_BATTLE_WINDOW_QUADS + quad;
    uint8_t top_left = (uint8_t)(swing + 0xB0);
    m_write8(s, q + 0x0, top_left);
    m_write8(s, q + 0x2, 0);
    m_write8(s, q + 0x4, (uint8_t)(top_left + 0x20));
    m_write8(s, q + 0x6, 0);
    uint8_t bottom_left = (uint8_t)(m_read8(s, RAM_BATTLE_ANIMATION_POSITIONS) - 0x10);
    m_write8(s, q + 0x8, bottom_left);
    m_write8(s, q + 0xC, (uint8_t)(bottom_left + 0x20));
    uint8_t y = m_read8(s, RAM_BATTLE_ANIMATION_POSITIONS + 1);
    m_write8(s, q + 0xA, y);
    m_write8(s, q + 0xE, y);
    s->cpu.x = quad;
    m_jsl_dc(s, 0xD1EC5A, 0xD1F9AF, DC_A | DC_Y | DC_FLAGS);
    m_rtl(s);
}

/* ---- The beam's quadrilateral -----------------------------------------------------------
 *
 * The rasterizer ($D1:F9E7) stays with the original: it writes the window
 * table through the work RAM port with 16-bit stores to $217F, so each line's
 * edge also goes, as a low byte, to APU port 3 -- a write no service expresses
 * (docs/TODO.md). What it works from is ported: the corners, sorted by line,
 * and the four edges' slopes. They live in the battle page's rasterizer cells
 * (battle_low_scratch): corner i's X and Y at $12 + 2i and $13 + 2i, the
 * slopes at $03, $05, $07 and $09, and a slope's operands at $0B-$0F. */

/* The guard of the two the rasterizer calls: the battle page, and the data
 * bank the rasterizer sets for the work RAM port (its divisions go through
 * the data bank too). */
bool battle_quad_page(Snes *s) { return s->cpu.d == 0x0000 && m_io_bank(s->cpu.db); }

/* The rasterizer's cells, from $03 in the battle page's low scratch. */
#define QUAD_CELLS (RAM_BATTLE_LOW_SCRATCH + 0x03)

enum { CORNERS = 0x12 - 0x03, SLOPES = 0x00, SLOPE_TO_X = 0x08, SLOPE_FROM_X = 0x09, SLOPE_LINES = 0x0A,
       TOP_LINE = 0x0C };

static uint8_t quad(Snes *s, uint8_t cell) { return m_read8(s, QUAD_CELLS + cell); }
static void set_quad(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, QUAD_CELLS + cell, v); }
static uint16_t corner(Snes *s, int i) { return m_read16(s, QUAD_CELLS + CORNERS + 2u * (unsigned)i); }
static void set_corner(Snes *s, int i, uint16_t v) { m_write16(s, QUAD_CELLS + CORNERS + 2u * (unsigned)i, v); }
static uint8_t corner_y(Snes *s, int i) { return quad(s, (uint8_t)(CORNERS + 2 * i + 1)); }

// $D1:F8EB
/* Sort the four corners by line, top first: four bubble passes, a corner
 * moving past the next when its line isn't above it. */
static void sort_corners(Snes *s)
{
    for (int pass = 0; pass < 4; pass++) {
        for (int i = 0; i < 3; i++) {
            if (corner_y(s, i) >= corner_y(s, i + 1)) {
                uint16_t a = corner(s, i), b = corner(s, i + 1);
                set_corner(s, i + 1, a);
                set_corner(s, i, b);
            }
        }
    }
}

void battle_sort_quad_corners(Snes *s)
{
    sort_corners(s);
    m_set_a8(s, 0);
    m_rts(s);
}

// $D1:F972
/* An edge's slope in columns per line, 8.8 fixed point: the columns from its
 * start to its end over its lines, through the hardware divider (the
 * dividend's low byte is A's high byte, as the original's XBA leaves it). */
static uint16_t edge_slope(Snes *s, uint8_t a_high)
{
    uint8_t to = quad(s, SLOPE_TO_X), from = quad(s, SLOPE_FROM_X);
    bool left = to < from;
    uint8_t columns = left ? (uint8_t)(from - to) : (uint8_t)(to - from);
    m_write16(s, 0x004204, (uint16_t)(columns << 8 | a_high));
    m_write8(s, 0x004206, quad(s, SLOPE_LINES));
    uint16_t q = m_read16(s, 0x004214);
    return left ? (uint16_t)-q : q;
}

void battle_quad_edge_slope(Snes *s)
{
    s->cpu.y = edge_slope(s, (uint8_t)(s->cpu.a >> 8));
    s->cpu.a = 0;
    m_rts(s);
}

// $D1:F91D
/* The slopes of the four edges, from the top corner down the one side to the
 * second and third corners and from the second and third down to the
 * bottom one; the top line goes to $0F. The first slope's dividend takes A's
 * high byte, the rest 0. */
static void edge_slopes(Snes *s, uint8_t a_high)
{
    static const struct { uint8_t from, to; } edges[4] = { { 0, 2 }, { 2, 3 }, { 0, 1 }, { 1, 3 } };
    set_quad(s, TOP_LINE, corner_y(s, 0));
    for (int e = 0; e < 4; e++) {
        set_quad(s, SLOPE_LINES, (uint8_t)(corner_y(s, edges[e].to) - corner_y(s, edges[e].from)));
        set_quad(s, SLOPE_TO_X, (uint8_t)corner(s, edges[e].to));
        set_quad(s, SLOPE_FROM_X, (uint8_t)corner(s, edges[e].from));
        m_write16(s, QUAD_CELLS + SLOPES + 2u * (unsigned)e, edge_slope(s, a_high));
        a_high = 0;   /* the slope leaves A 0 */
    }
}

void battle_quad_edge_slopes(Snes *s)
{
    edge_slopes(s, (uint8_t)(s->cpu.a >> 8));
    s->cpu.a = 0;
    m_rts(s);
}

// $D1:F9AF
/* Rasterize window quadrilateral X (battle_window_quads) into the coming
 * frame's window table: its corners to the rasterizer's cells, sorted, then
 * $D1:F9E7 with data bank 0 (for the work RAM port), called out. */
void battle_draw_window_quad(Snes *s)
{
    uint16_t q = s->cpu.x;
    m_write8(s, RAM_BATTLE_WINDOW_QUAD_INDEX, (uint8_t)q);
    for (int i = 0; i < 8; i++)
        set_quad(s, (uint8_t)(CORNERS + i), m_read8(s, RAM_BATTLE_WINDOW_QUADS + 2u * (unsigned)i + q));
    sort_corners(s);
    m_push8(s, s->cpu.db);
    s->cpu.db = 0x00;
    s->cpu.a = 0;
    m_jsr_dc(s, 0xD1F9E2, 0xD1F9E7, DC_X | DC_Y | DC_FLAGS);
    s->cpu.db = m_pull8(s);
    m_rtl(s);
}
