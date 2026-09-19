/* Field mode: the per-line effects.
 *
 * The field runs eight per-line effects ($FD:C1EE programs them): the layers'
 * tile map registers, BG1-BG3 scroll, the screens and their window masks,
 * window 1's edges, the color math layers with the fixed color, and one edge
 * of window 2. Each reads a table in bank $7F (field_line_effect_tables) in
 * the indirect format the video service's line effects take: entries of a line
 * count and a 16-bit pointer to the values in bank $7F, ended by a 0. The
 * values hold for that many lines; a count with LINES_EACH set gives each line
 * values of its own, read on from the pointer.
 *
 * There are two sets of tables. Every frame the field builds the set the line
 * effects aren't reading and flips field_display_flags bit 0, which makes the
 * next vblank program the set just built ($FD:C2C1). What a set holds depends
 * on the dialog window: with none, every effect is the same over the whole
 * screen; with one open, it is a band dialog_window_state lines either side of
 * line 48 (the top window) or line 171 (the bottom one), inside which BG1 and
 * BG3 show the window's tile map, the screens show the window's layers, and
 * the color math shades it (field_dialog_shading). Either layer can wave
 * (field_line_wave_bg2, field_line_wave_bg3): its scroll table then points
 * into field_line_wave every 16 lines, which the builder refills every frame
 * from a wave table in the ROM.
 *
 * The builders' arithmetic is 8-bit, as the original's: a window taller than
 * the space it has wraps the counts, and the tables say so. The per-line window
 * tables ($7F:17C8 and $7F:1AC8, field_window_edge_lines) are filled by the
 * event screen effects; the builders only point at them.
 *
 * The table writes are RAM, not video registers: the line effects read the
 * tables when the frame starts, whatever wrote them. Each routine is a C
 * function of what it does; its entry, at the end of the file, moves the
 * original's registers in and out. The builders run with the field's direct
 * page ($0100), whose cells $D9-$EF they use as scratch (the sprite table's at
 * other times), and the data bank $7F. */
#include "field/line_effects.h"

#include "field/field_data.h"
#include "platform/video.h"
#include "port/machine.h"
#include "ram.h"

/* ---- Tables ------------------------------------------------------------------------- */

enum {
    LINES_EACH = 0x80,  /* a count with this bit: each line has values of its own */
    TABLE_END = 0x00,
    ENTRY_SIZE = 3,     /* a count, then a 16-bit pointer */
};

/* The eight effects, in the order of their tables within a set. */
enum {
    SLOT_BG_MAPS,
    SLOT_BG1_SCROLL,
    SLOT_BG2_SCROLL,
    SLOT_BG3_SCROLL,
    SLOT_SCREENS,
    SLOT_WINDOW1,
    SLOT_COLOR_MATH,
    SLOT_WINDOW2,
    SLOTS,
};

static uint32_t line_table(uint8_t set, uint8_t slot)
{
    return RAM_FIELD_LINE_EFFECT_TABLES + (uint32_t)(set * SLOTS + slot) * RAM_FIELD_LINE_EFFECT_TABLES_STRIDE;
}

/* Append an entry to the table being written at `*at`. */
static void put(Snes *s, uint32_t *at, uint8_t lines, uint16_t values)
{
    m_write8(s, *at, lines);
    m_write16(s, *at + 1, values);
    *at += ENTRY_SIZE;
}

static void end_table(Snes *s, uint32_t at) { m_write8(s, at, TABLE_END); }

/* The values the entries point at, as the 16-bit pointers the tables hold;
 * they are all in the tables' bank. */
enum { LINE_BANK = 0x7F };
static uint32_t in_line_bank(uint16_t values) { return (uint32_t)LINE_BANK << 16 | values; }
enum { LAYER_BG1, LAYER_BG2, LAYER_BG3 };
enum { WINDOW_TOP, WINDOW_BOTTOM };

static uint16_t maps_values(bool in_window)
{
    return RAM_ABS(RAM_FIELD_LINE_BG_MAPS + in_window * RAM_FIELD_LINE_BG_MAPS_STRIDE);
}
static uint16_t scroll_values(uint8_t layer, uint8_t set)
{
    return RAM_ABS(RAM_FIELD_LINE_SCROLL + (layer * 2 + set) * RAM_FIELD_LINE_SCROLL_STRIDE);
}
/* BG1's or BG3's scroll inside a window at `position`. */
static uint16_t window_scroll_values(uint8_t position, bool bg3)
{
    return RAM_ABS(RAM_FIELD_LINE_WINDOW_SCROLL + (position * 2 + bg3) * RAM_FIELD_LINE_WINDOW_SCROLL_STRIDE);
}
enum { SCREENS_OUTSIDE, SCREENS_WINDOW_BODY, SCREENS_WINDOW_EDGE };
static uint16_t screens_values(uint8_t which)
{
    return RAM_ABS(RAM_FIELD_LINE_SCREENS + which * RAM_FIELD_LINE_SCREENS_STRIDE);
}
static uint32_t color_math(uint8_t set, bool in_window)
{
    return RAM_FIELD_LINE_COLOR_MATH + (set + 2 * in_window) * RAM_FIELD_LINE_COLOR_MATH_STRIDE;
}
static uint16_t window1_edges_values(uint8_t set)
{
    return RAM_ABS(RAM_FIELD_LINE_WINDOW1_EDGES + set * RAM_FIELD_LINE_WINDOW1_EDGES_STRIDE);
}
static uint16_t shading_values(uint8_t set)
{
    return set ? RAM_ABS(RAM_FIELD_DIALOG_SHADING_COPY) : RAM_ABS(RAM_FIELD_DIALOG_SHADING);
}
static uint16_t window_edge_lines(uint8_t set)
{
    return set ? RAM_ABS(RAM_FIELD_WINDOW_EDGE_LINES_COPY) : RAM_ABS(RAM_FIELD_WINDOW_EDGE_LINES);
}

enum { WAVE_LINES = 16, WAVE_ENTRY = RAM_FIELD_LINE_WAVE_STRIDE, WAVE_SET = 2 * WAVE_LINES * WAVE_ENTRY };
static uint32_t wave(uint8_t set) { return RAM_FIELD_LINE_WAVE + set * WAVE_SET; }

/* ---- Scratch ---------------------------------------------------------------------------
 *
 * Cells of the field's direct page the builders use on the way, which stay
 * written as the original leaves them. */
#define LINES_SCRATCH (RAM_PAGE_FIELD + 0xDB)      /* a part's line count */
#define POINTER_SCRATCH (RAM_PAGE_FIELD + 0xD9)    /* a pointer into a per-line table (16-bit), or a count */
#define WAVE_LINE (RAM_PAGE_FIELD + 0xEE)          /* the next line of the wave (16-bit when reset) */

/* ---- Parts of the builders ------------------------------------------------------------ */

enum { WHOLE_SCREEN_TOP = 100, WHOLE_SCREEN_BOTTOM = 112 };

/* The same values over the whole screen: two entries, since a count holds 127
 * lines at most. */
static void whole_screen(Snes *s, uint32_t at, uint16_t values)
{
    put(s, &at, WHOLE_SCREEN_TOP, values);
    put(s, &at, WHOLE_SCREEN_BOTTOM, values);
    end_table(s, at);
}

/* A waving layer over the whole screen: 13 entries of 16 lines, each reading
 * the 16 lines of the wave. */
enum { WAVE_SCREEN_PARTS = 13 };
static void wave_screen(Snes *s, uint32_t at, uint16_t wave_values)
{
    for (int i = 0; i < WAVE_SCREEN_PARTS; i++)
        put(s, &at, LINES_EACH | WAVE_LINES, wave_values);
    end_table(s, at);
}

// $FD:C4B5 (set 0), $FD:C52E (set 1)
/* BG2's scroll table: BG2 has no window band, so its scroll is the same over
 * the whole screen, or waves. Returns what the original leaves in A's low
 * byte: the last count it loaded. */
static uint8_t set_bg2_table(Snes *s, uint8_t set)
{
    uint32_t at = line_table(set, SLOT_BG2_SCROLL);
    if (m_read8(s, RAM_FIELD_LINE_WAVE_BG2) == 0) {
        whole_screen(s, at, scroll_values(LAYER_BG2, set));
        return TABLE_END;
    }
    wave_screen(s, at, RAM_ABS(wave(set)));
    return LINES_EACH | WAVE_LINES;
}

/* Window 2's edge table and the choice between its two per-line tables, 100
 * lines apart in bytes: bit 0 of the byte at $7F:0351 (event_variables, which
 * the original reads through the data bank $7F) picks the table set 0 reads,
 * and set 1 reads the other. */
enum { WINDOW2_SECOND_TABLE = 0xE0 };
typedef struct {
    uint16_t last_values;   /* the last pointer written, which the original leaves in X */
    bool which;             /* the bit, which it leaves in the carry */
} Window2Table;

// $FD:C439 (set 0), $FD:C477 (set 1)
static Window2Table set_window2_table(Snes *s, uint8_t set)
{
    bool bit = m_read8(s, RAM_EVENT_VARIABLES + 0x151) & 1;
    uint16_t first = window_edge_lines(0);
    uint16_t lines = bit != (set != 0) ? first : (uint16_t)(first + WINDOW2_SECOND_TABLE);
    uint32_t at = line_table(set, SLOT_WINDOW2);
    uint16_t second = (uint16_t)(lines + WHOLE_SCREEN_TOP);
    put(s, &at, LINES_EACH | WHOLE_SCREEN_TOP, lines);
    put(s, &at, LINES_EACH | WHOLE_SCREEN_BOTTOM, second);
    end_table(s, at);
    return (Window2Table){ second, bit };
}

/* A waving layer's scroll table with a window band in it: the wave's line
 * carries on across the parts, so each part starts where the last one left
 * the wave. The index of the next entry is the original's Y. */
typedef struct {
    uint32_t table;
    uint16_t next;      /* Y: the offset of the next entry */
    uint8_t set;
} WaveTable;

// $FD:CC58
static WaveTable start_wave_table(Snes *s, uint32_t table, uint8_t set)
{
    m_write16(s, WAVE_LINE, 0);
    return (WaveTable){ table, 0, set };
}

static void wave_put(Snes *s, WaveTable *w, uint8_t lines, uint16_t values)
{
    uint32_t at = w->table + w->next;
    put(s, &at, lines, values);
    w->next = (uint16_t)(w->next + ENTRY_SIZE);
}

// $FD:CC5E (set 0, BG3's table), $FD:CCB8 (set 1)
/* `lines` lines of the wave, 16 or fewer to an entry, each entry starting at
 * the wave's next line. Returns the last entry's pointer, and what the
 * original leaves in A's low byte: the wave's next line, or 0 where the lines
 * ended with a whole entry. */
typedef struct {
    uint16_t values;
    uint8_t a;
} WavePart;

static WavePart wave_part(Snes *s, WaveTable *w, uint8_t lines)
{
    for (;;) {
        uint8_t line = m_read8(s, WAVE_LINE);
        uint16_t values = (uint16_t)(RAM_ABS(wave(w->set)) + (line & (WAVE_LINES - 1)) * WAVE_ENTRY);
        if (lines < WAVE_LINES) {
            m_write8(s, POINTER_SCRATCH, lines);
            wave_put(s, w, LINES_EACH | lines, values);
            line = (uint8_t)(line + lines);
            m_write8(s, WAVE_LINE, line);
            return (WavePart){ values, line };
        }
        lines = (uint8_t)(lines - WAVE_LINES);
        m_write8(s, POINTER_SCRATCH, lines);
        wave_put(s, w, LINES_EACH | WAVE_LINES, values);
        m_write8(s, WAVE_LINE, (uint8_t)(line + WAVE_LINES));
        if (lines == 0)
            return (WavePart){ values, 0 };
    }
}

// $FD:CCB2
/* Lines that don't wave (the window band) still move the wave's line on. */
static uint8_t skip_wave_lines(Snes *s, uint8_t lines)
{
    uint8_t line = (uint8_t)(m_read8(s, WAVE_LINE) + lines);
    m_write8(s, WAVE_LINE, line);
    return line;
}

/* What a builder leaves in the registers its callers read: X (8-bit by then),
 * A's high byte, the carry and the overflow flag, which come from the last
 * piece of arithmetic on the way. */
typedef struct {
    uint8_t x;
    uint8_t a_low, a_high;
    bool carry;
    bool overflow;
} Exit;

// $FD:C5A7 (set 0), $FD:C6F7 (set 1)
/* The values the set's tables point at that change every frame: the layers'
 * scroll, the wave when a layer waves (its phase moves on a line each frame),
 * and the color math. `carry` and `overflow` are the flags on the way in: the
 * wave's additions carry from one line into the next, starting with the carry
 * set_window2_table left. */
static Exit send_scroll(Snes *s, uint8_t set, uint16_t x, bool carry, bool overflow)
{
    for (uint8_t layer = LAYER_BG1; layer <= LAYER_BG2; layer++) {
        uint32_t from = RAM_FIELD_LAYER_SCROLL + layer * RAM_FIELD_LAYER_SCROLL_STRIDE;
        uint32_t to = in_line_bank(scroll_values(layer, set));
        m_write16(s, to, m_read16(s, from));
        m_write16(s, to + 2, m_read16(s, from + 2));
    }
    const uint32_t bg3 = RAM_FIELD_LAYER_SCROLL + LAYER_BG3 * RAM_FIELD_LAYER_SCROLL_STRIDE;
    uint16_t bg3_h = m_read16(s, bg3), bg3_v = m_read16(s, bg3 + 2);
    uint8_t a_high;
    if (m_read16(s, RAM_FIELD_LINE_WAVE_BG3) == 0 && m_read16(s, RAM_FIELD_LINE_WAVE_BG2) == 0) {
        uint32_t to = in_line_bank(scroll_values(LAYER_BG3, set));
        m_write16(s, to, bg3_h);
        m_write16(s, to + 2, bg3_v);
        a_high = (uint8_t)(bg3_v >> 8);
    } else {
        uint8_t phase = (uint8_t)(m_read8(s, RAM_FIELD_LINE_WAVE_PHASE) + 1);
        m_write8(s, RAM_FIELD_LINE_WAVE_PHASE, phase);
        uint16_t start = phase & 0x3E;
        uint32_t lines = wave(set);
        for (uint16_t i = 0; i < WAVE_LINES; i++) {
            uint16_t offset = field_wave_offset(s, (uint16_t)(start + i * WAVE_ENTRY));
            uint32_t sum = (uint32_t)offset + bg3_h + carry;
            uint16_t h = (uint16_t)sum;
            overflow = (~(offset ^ bg3_h) & (offset ^ h) & 0x8000) != 0;
            carry = sum > 0xFFFF;
            m_write16(s, lines + i * WAVE_ENTRY, h);
            m_write16(s, lines + i * WAVE_ENTRY + 2, bg3_v);
        }
        /* The second 16 lines repeat the first, so an entry can read 16 lines
         * from any of them. */
        for (uint16_t i = 0; i < WAVE_LINES * WAVE_ENTRY; i++)
            m_write8(s, lines + WAVE_LINES * WAVE_ENTRY + i, m_read8(s, lines + i));
        x = RAM_ABS(lines + WAVE_LINES * WAVE_ENTRY);   /* where the copy left it */
        a_high = 0xFF;
    }
    uint32_t math = color_math(set, false);
    m_write8(s, math + 1, m_read8(s, RAM_FIELD_FIXED_COLOR));
    uint8_t layers = m_read8(s, RAM_FIELD_FIXED_COLOR_MATH_LAYERS);
    m_write8(s, math, layers);
    m_write8(s, color_math(set, true), layers);
    return (Exit){ (uint8_t)x, layers, a_high, carry, overflow };
}

// $FD:C401 (set 0), $FD:C95D (set 1)
/* The same while field_display_flags bit 7 is set (the field's setup builds
 * both sets that way): the scroll from field_bg1_scroll_position and
 * field_auto_scroll_position, the color math layers from
 * field_color_math_layers_override, and no wave. */
static Exit send_scroll_override(Snes *s, uint8_t set, uint16_t x, bool carry, bool overflow)
{
    static const uint32_t from[3] = {
        RAM_FIELD_BG1_SCROLL_POSITION,
        RAM_FIELD_AUTO_SCROLL_POSITION,
        RAM_FIELD_AUTO_SCROLL_POSITION + RAM_FIELD_AUTO_SCROLL_POSITION_STRIDE,
    };
    uint16_t v = 0;
    for (uint8_t layer = LAYER_BG1; layer <= LAYER_BG3; layer++) {
        uint32_t to = in_line_bank(scroll_values(layer, set));
        v = m_read16(s, from[layer] + 2);
        m_write16(s, to, m_read16(s, from[layer]));
        m_write16(s, to + 2, v);
    }
    uint32_t math = color_math(set, false);
    m_write8(s, math + 1, m_read8(s, RAM_FIELD_FIXED_COLOR));
    uint8_t layers = m_read8(s, RAM_FIELD_COLOR_MATH_LAYERS_OVERRIDE);
    m_write8(s, math, layers);
    m_write8(s, color_math(set, true), layers);
    return (Exit){ (uint8_t)x, layers, (uint8_t)(v >> 8), carry, overflow };
}

/* ---- The builders --------------------------------------------------------------------- */

enum { DISPLAY_FLAGS_OVERRIDE = 0x80 };

// $FD:C2EB (set 0), $FD:C847 (set 1)
/* No dialog window: every effect the same over the whole screen. Window 1
 * reads its per-line table; BG2 and BG3 may wave. `overflow` is the flag on
 * the way in, which nothing here changes unless a layer waves. */
static Exit build_no_window(Snes *s, uint8_t set, bool overflow)
{
    whole_screen(s, line_table(set, SLOT_BG_MAPS), maps_values(false));
    whole_screen(s, line_table(set, SLOT_BG1_SCROLL), scroll_values(LAYER_BG1, set));
    set_bg2_table(s, set);
    if (m_read8(s, RAM_FIELD_LINE_WAVE_BG3) == 0)
        whole_screen(s, line_table(set, SLOT_BG3_SCROLL), scroll_values(LAYER_BG3, set));
    else
        wave_screen(s, line_table(set, SLOT_BG3_SCROLL), RAM_ABS(wave(set)));
    whole_screen(s, line_table(set, SLOT_SCREENS), screens_values(SCREENS_OUTSIDE));
    uint32_t at = line_table(set, SLOT_WINDOW1);
    uint16_t edges = window_edge_lines(set);
    put(s, &at, LINES_EACH | WHOLE_SCREEN_TOP, edges);
    put(s, &at, LINES_EACH | WHOLE_SCREEN_BOTTOM, (uint16_t)(edges + 2 * WHOLE_SCREEN_TOP));
    end_table(s, at);
    whole_screen(s, line_table(set, SLOT_COLOR_MATH), RAM_ABS(color_math(set, false)));
    Window2Table w2 = set_window2_table(s, set);
    if (m_read8(s, RAM_FIELD_DISPLAY_FLAGS) & DISPLAY_FLAGS_OVERRIDE)
        return send_scroll_override(s, set, w2.last_values, w2.which, overflow);
    return send_scroll(s, set, w2.last_values, w2.which, overflow);
}

/* The dialog window: a band `half` lines either side of its middle. All of it
 * is 8-bit, as in the original. */
typedef struct {
    uint8_t half;
    uint8_t band;       /* 2 * half */
    uint8_t edge;       /* lines of the window's frame at its top and at its bottom */
    uint8_t body;       /* the lines between */
} Band;

enum { FRAME_LINES = 10 };

static Band dialog_band(uint8_t half)
{
    Band b = { half, (uint8_t)(half * 2), (uint8_t)(half - 1), 2 };
    if (half > FRAME_LINES) {
        b.edge = FRAME_LINES;
        b.body = (uint8_t)(half * 2 - 2 * FRAME_LINES);
    }
    return b;
}

/* How a table divides the screen around the band: the parts' line counts, the
 * one at `band` being the window's. */
typedef struct {
    uint8_t lines[6];
    uint8_t count;
    uint8_t band;
} Layout;

static void band_table(Snes *s, uint32_t at, const Layout *l, uint16_t outside, uint16_t inside)
{
    for (uint8_t i = 0; i < l->count; i++)
        put(s, &at, l->lines[i], i == l->band ? inside : outside);
    end_table(s, at);
}

/* A waving layer's scroll with the band in it: the wave's line carries on
 * across the parts, the band's lines included. */
static void wave_band_table(Snes *s, uint32_t table, uint8_t set, const Layout *l, uint16_t inside)
{
    WaveTable w = start_wave_table(s, table, set);
    for (uint8_t i = 0; i < l->count; i++) {
        if (i == l->band) {
            wave_put(s, &w, l->lines[i], inside);
            skip_wave_lines(s, l->lines[i]);
        } else {
            wave_part(s, &w, l->lines[i]);
        }
    }
    end_table(s, w.table + w.next);
}

/* The screens: the window's frame at the band's top and bottom, its body
 * between. */
static void screens_table(Snes *s, uint32_t at, const Layout *l, const Band *b)
{
    for (uint8_t i = 0; i < l->count; i++) {
        if (i == l->band) {
            put(s, &at, b->edge, screens_values(SCREENS_WINDOW_EDGE));
            put(s, &at, b->body, screens_values(SCREENS_WINDOW_BODY));
            put(s, &at, b->edge, screens_values(SCREENS_WINDOW_EDGE));
        } else {
            put(s, &at, l->lines[i], screens_values(SCREENS_OUTSIDE));
        }
    }
    end_table(s, at);
}

/* The color math: inside the band, one line of shading per line, read from
 * the middle of field_dialog_shading out, and its last line the window's own. */
enum { SHADING_MIDDLE = 0x28 };
static void color_math_table(Snes *s, uint32_t at, uint8_t set, const Layout *l, const Band *b)
{
    uint16_t outside = RAM_ABS(color_math(set, false));
    uint16_t first = (uint16_t)(((uint8_t)(SHADING_MIDDLE - b->half) & 0x3F) * RAM_FIELD_DIALOG_SHADING_STRIDE);
    for (uint8_t i = 0; i < l->count; i++) {
        if (i == l->band) {
            put(s, &at, (uint8_t)(b->band - 1) | LINES_EACH, (uint16_t)(shading_values(set) + first));
            put(s, &at, 1, RAM_ABS(color_math(set, true)));
        } else {
            put(s, &at, l->lines[i], outside);
        }
    }
    end_table(s, at);
}

/* The subtraction's overflow flag, which the builders leave behind. */
static bool sub_overflows(uint8_t a, uint8_t b)
{
    uint8_t r = (uint8_t)(a - b);
    return ((a ^ b) & (a ^ r) & 0x80) != 0;
}

/* Where the two windows lie. The top one: 7 lines, then (41 - half) above the
 * band and (100 - half) below it, then 30. The bottom one: 100 + 29 lines, then
 * (42 - half) above the band and (40 - half) below it. */
enum {
    TOP_START = 7, TOP_ABOVE = 0x29, TOP_BELOW = 0x64, TOP_END = 0x1E,
    TOP_WAVE_BELOW = 0x82,      /* what a waving BG3's table has below the top window */
    TOP_WINDOW1_END = 0x47,     /* and window 1's last part */
    BOTTOM_START = 0x64, BOTTOM_NEXT = 0x1D, BOTTOM_EDGE = 0x29,
    BOTTOM_WAVE_START = 0x58,   /* a waving BG3's table divides the first 129 lines 88 + 41 */
};

// $FD:C995 (set 0), $FD:CD0C (set 1): the top window
// $FD:CFCF (set 0), $FD:D27E (set 1): the bottom window
static Exit build_window(Snes *s, uint8_t set, uint8_t position)
{
    const Band b = dialog_band(m_read8(s, RAM_DIALOG_WINDOW_STATE));
    Layout plain, waving, screens;
    if (position == WINDOW_TOP) {
        const uint8_t above = (uint8_t)(TOP_ABOVE - b.half), below = (uint8_t)(TOP_BELOW - b.half);
        plain = (Layout){ { TOP_START, above, b.band, below, TOP_END }, 5, 2 };
        waving = (Layout){ { TOP_START, above, b.band, (uint8_t)(TOP_WAVE_BELOW - b.half), TOP_END }, 5, 2 };
        /* The original's screens table has the lines above the band again
         * after it; that only moves where its last part ends. */
        screens = (Layout){ { TOP_START, above, b.band, above, below, TOP_END }, 6, 2 };
    } else {
        const uint8_t above = (uint8_t)(BOTTOM_EDGE - b.half + 1), below = (uint8_t)(BOTTOM_EDGE - b.half - 1);
        plain = (Layout){ { BOTTOM_START, BOTTOM_NEXT, above, b.band, below }, 5, 3 };
        waving = (Layout){ { BOTTOM_WAVE_START, BOTTOM_EDGE, above, b.band, below }, 5, 3 };
        screens = plain;
    }

    band_table(s, line_table(set, SLOT_BG_MAPS), &plain, maps_values(false), maps_values(true));
    band_table(s, line_table(set, SLOT_BG1_SCROLL), &plain, scroll_values(LAYER_BG1, set),
               window_scroll_values(position, false));
    set_bg2_table(s, set);
    uint16_t bg3_inside = window_scroll_values(position, true);
    if (m_read8(s, RAM_FIELD_LINE_WAVE_BG3) == 0)
        band_table(s, line_table(set, SLOT_BG3_SCROLL), &plain, scroll_values(LAYER_BG3, set), bg3_inside);
    else
        wave_band_table(s, line_table(set, SLOT_BG3_SCROLL), set, &waving, bg3_inside);
    screens_table(s, line_table(set, SLOT_SCREENS), &screens, &b);

    /* Window 1 follows its per-line table (two bytes a line) outside the band,
     * reading on past the band's lines, and covers the whole line inside it.
     * The original keeps some of the counts it computes in one scratch cell
     * (the top window's two either side of the band, the bottom window's one
     * above it) and the pointer of the last part in another. */
    uint32_t at = line_table(set, SLOT_WINDOW1);
    uint16_t lines = window_edge_lines(set);
    const uint8_t last = (uint8_t)(plain.count - 1);
    for (uint8_t i = 0; i < plain.count; i++) {
        uint8_t n = plain.lines[i];
        if (i == plain.band) {
            put(s, &at, n, window1_edges_values(set));
        } else {
            if (i == plain.band - 1 || (position == WINDOW_TOP && i == plain.band + 1))
                m_write8(s, LINES_SCRATCH, n);
            if (i == last) {
                m_write16(s, POINTER_SCRATCH, lines);
                if (position == WINDOW_TOP)
                    n = TOP_WINDOW1_END;    /* its last part is longer than the other tables' */
            }
            put(s, &at, LINES_EACH | n, lines);
        }
        lines = (uint16_t)(lines + 2 * n);
    }
    end_table(s, at);

    color_math_table(s, line_table(set, SLOT_COLOR_MATH), set, &plain, &b);

    Window2Table w2 = set_window2_table(s, set);
    bool overflow = position == WINDOW_TOP ? sub_overflows(TOP_BELOW, b.half) : sub_overflows(BOTTOM_EDGE, b.half);
    return send_scroll(s, set, w2.last_values, w2.which, overflow);
}

/* ---- The game's entries --------------------------------------------------------------- */

enum { WINDOW_NONE, WINDOW_AT_TOP, WINDOW_AT_BOTTOM, WINDOW_POSITIONS };
enum { DISPLAY_FLAGS_SECOND_SET = 0x01 };

/* The builder for a window position (the entries of $FD:C2C1's two tables):
 * a window whose state is 0 isn't open. */
static Exit build_for(Snes *s, uint8_t set, uint8_t position, bool overflow)
{
    if (position == WINDOW_NONE || m_read8(s, RAM_DIALOG_WINDOW_STATE) == 0)
        return build_no_window(s, set, overflow);
    return build_window(s, set, position == WINDOW_AT_TOP ? WINDOW_TOP : WINDOW_BOTTOM);
}

// $FD:C2C1
/* Build the table set the line effects aren't reading (field_display_flags
 * bit 0 clear: set 1, then set the bit; set: set 0, then clear it), for where
 * the dialog window is. */
static Exit build_line_effect_tables(Snes *s, bool overflow)
{
    uint8_t set = (m_read8(s, RAM_FIELD_DISPLAY_FLAGS) & DISPLAY_FLAGS_SECOND_SET) ? 0 : 1;
    Exit e = build_for(s, set, m_read8(s, RAM_DIALOG_WINDOW_POSITION), overflow);
    m_write8(s, RAM_FIELD_DISPLAY_FLAGS, (uint8_t)(m_read8(s, RAM_FIELD_DISPLAY_FLAGS) ^ DISPLAY_FLAGS_SECOND_SET));
    return e;
}


// $FD:D52D
/* The dialog window's shading, a line at a time from the window's middle out:
 * the fixed color added to BG1 from intensity 8 down to 1, then subtracted
 * from 1 up to 9, in steps of (40 / 8) lines (computed with the hardware
 * divider, the remainder's lines subtracting 0), and a copy of it for table
 * set 1 (a block move of 256 bytes up to $7F:1600, which overlaps its own
 * source, so the copy's last $38 bytes repeat its first). Returns the
 * overflow flag of the last line count added up. */
enum {
    SHADING_ADD = 0x01, SHADING_SUBTRACT = 0x81,
    FIXED_COLOR_ALL = 0xE0,         /* the fixed color's three channels, intensity in the low bits */
    SHADING_LINES = 0x28, SHADING_STEPS = 8,
    SHADING_COPY_BYTES = 0x100,
};

static bool fill_dialog_shading(Snes *s)
{
    m_write8(s, 0x004204, SHADING_LINES);   /* the divider: dividend, divisor */
    m_write8(s, 0x004205, 0x00);
    m_write8(s, 0x004206, SHADING_STEPS);
    uint8_t step = (uint8_t)(m_read8(s, 0x004214) * 2);        /* the quotient, in bytes */
    uint8_t rest = (uint8_t)(m_read8(s, 0x004216) * 2);        /* the remainder */
    const uint32_t shading = RAM_FIELD_DIALOG_SHADING;
    const uint32_t lines_end = RAM_PAGE_FIELD + 0xDB, step_cell = RAM_PAGE_FIELD + 0xDD;
    const uint32_t rest_cell = RAM_PAGE_FIELD + 0xDF, color_cell = RAM_PAGE_FIELD + 0xE1;
    m_write8(s, lines_end, step);
    m_write8(s, step_cell, step);
    m_write8(s, rest_cell, rest);

    uint8_t x = 0, end = step, color = FIXED_COLOR_ALL | 8;
    bool overflow = false;
    /* Each run fills up to `end`, which then moves on by `add`. */
#define ADVANCE(add)                                                                                                   \
    do {                                                                                                               \
        uint8_t r = (uint8_t)(end + (add));                                                                            \
        overflow = (~(end ^ (add)) & (end ^ r) & 0x80) != 0;                                                           \
        end = r;                                                                                                       \
        m_write8(s, lines_end, end);                                                                                   \
    } while (0)
    for (;;) {
        m_write8(s, color_cell, color);
        for (; x != end; x = (uint8_t)(x + 2)) {
            m_write8(s, shading + x + 1, color);
            m_write8(s, shading + x, SHADING_ADD);
        }
        ADVANCE(step);
        if ((uint8_t)(color - 1) == FIXED_COLOR_ALL)
            break;
        color = (uint8_t)(color - 1);
    }
    ADVANCE(rest);
    for (; x != end; x = (uint8_t)(x + 2)) {
        m_write8(s, shading + x + 1, FIXED_COLOR_ALL);
        m_write8(s, shading + x, SHADING_SUBTRACT);
    }
    color = FIXED_COLOR_ALL;
    m_write8(s, color_cell, color);
    for (;;) {
        ADVANCE(step);
        if (color == (FIXED_COLOR_ALL | 9))
            break;
        color = (uint8_t)(color + 1);
        m_write8(s, color_cell, color);
        for (; x != end; x = (uint8_t)(x + 2)) {
            m_write8(s, shading + x + 1, color);
            m_write8(s, shading + x, SHADING_SUBTRACT);
        }
    }
#undef ADVANCE
    for (uint16_t i = 0; i < SHADING_COPY_BYTES; i++)
        m_write8(s, RAM_FIELD_DIALOG_SHADING_COPY + i, m_read8(s, shading + i));
    return overflow;
}

/* The inside of the dialog window, which the field's setup fills once: BG1 and
 * BG3 read the window's tile map ($1C) and scroll it into place, window 1
 * spans the whole line, the window's body shows BG1 and BG3 and its frame BG1,
 * and the color math there is black. */
enum { WINDOW_MAP = 0x1C, FULL_LINE_EDGES = 0xFF00 };

// $FD:C124
static bool init_line_effect_values(Snes *s)
{
    const uint32_t maps = RAM_FIELD_LINE_BG_MAPS, window_maps = maps + RAM_FIELD_LINE_BG_MAPS_STRIDE;
    for (uint8_t layer = 0; layer < 3; layer++) {
        uint8_t base = m_read8(s, RAM_FIELD_LAYER_MAP_BASE + layer * RAM_FIELD_LAYER_MAP_BASE_STRIDE + 1);
        m_write8(s, maps + layer, base | m_read8(s, RAM_FIELD_LAYER_MAP_SIZE + layer));
    }
    m_write8(s, maps + 3, 0);
    m_write8(s, window_maps, WINDOW_MAP);
    m_write8(s, window_maps + 1, m_read8(s, RAM_FIELD_LAYER_MAP_BASE + RAM_FIELD_LAYER_MAP_BASE_STRIDE + 1) | 1);
    m_write8(s, window_maps + 2, WINDOW_MAP);
    m_write8(s, window_maps + 3, 0);

    static const uint16_t window_v[4] = { 0x0037, 0xFFF1, 0xFFBC, 0xFF76 };
    for (uint8_t i = 0; i < 4; i++) {
        uint32_t at = RAM_FIELD_LINE_WINDOW_SCROLL + i * RAM_FIELD_LINE_WINDOW_SCROLL_STRIDE;
        m_write16(s, at, 0);
        m_write16(s, at + 2, window_v[i]);
    }
    for (uint8_t set = 0; set < 2; set++) {
        m_write16(s, RAM_FIELD_LINE_WINDOW1_EDGES + set * RAM_FIELD_LINE_WINDOW1_EDGES_STRIDE, FULL_LINE_EDGES);
        m_write8(s, color_math(set, true) + 1, FIXED_COLOR_ALL);
    }

    static const uint8_t screens[3][2] = {
        [SCREENS_WINDOW_BODY] = { 0x05, 0x00 },     /* BG1 and BG3 */
        [SCREENS_WINDOW_EDGE] = { 0x01, 0x00 },     /* BG1 */
    };
    for (uint8_t which = 0; which < 3; which++) {
        uint32_t at = RAM_FIELD_LINE_SCREENS + which * RAM_FIELD_LINE_SCREENS_STRIDE;
        m_write8(s, at, which == SCREENS_OUTSIDE ? m_read8(s, RAM_FIELD_MAIN_SCREEN) : screens[which][0]);
        m_write8(s, at + 1, which == SCREENS_OUTSIDE ? m_read8(s, RAM_FIELD_SUB_SCREEN) : screens[which][1]);
        m_write8(s, at + 2, 0);     /* no window masks */
        m_write8(s, at + 3, 0);
    }
    m_write8(s, RAM_DIALOG_WINDOW_STATE, 0);
    return fill_dialog_shading(s);
}

// $FD:C1EE
/* Program the field's eight line effects from the set field_display_flags'
 * low nibble picks: the layers' tile map addresses, BG1-BG3 scroll, the
 * screens and window masks, window 1's edges, the color math layers with the
 * fixed color, and one edge of window 2 -- its left one, or its right one while
 * field_window2_edge bit 0 is set. The effects are turned on elsewhere; the
 * field NMI copies the enable shadow field_line_effect_enable every frame.
 *
 * The original writes all eight controls, then all eight destinations, and so
 * on, through the direct page on the DMA registers; each call below programs
 * one effect. Nothing observes the order: the routine makes no calls, and
 * every register ends up the same. Returns the set. */
static uint8_t setup_line_effects(Snes *s)
{
    /* The register writes of one effect, in the original's order within it. */
    static const uint8_t steps[] = {
        VID_STEP_CONTROL,     VID_STEP_DEST,      VID_STEP_SOURCE_BANK, VID_STEP_INDIRECT_BANK,
        VID_STEP_SOURCE_LO,   VID_STEP_SOURCE_HI, VID_STEP_END,
    };
    static const VidLineTarget targets[SLOTS] = {
        [SLOT_BG_MAPS] = VID_LINE_BG_MAPS,       [SLOT_BG1_SCROLL] = VID_LINE_SCROLL_BG1,
        [SLOT_BG2_SCROLL] = VID_LINE_SCROLL_BG2, [SLOT_BG3_SCROLL] = VID_LINE_SCROLL_BG3,
        [SLOT_SCREENS] = VID_LINE_SCREENS,       [SLOT_WINDOW1] = VID_LINE_WINDOW1,
        [SLOT_COLOR_MATH] = VID_LINE_MATH_LAYERS, [SLOT_WINDOW2] = VID_LINE_WINDOW2_LEFT,
    };
    static const uint32_t sites[SLOTS] = { 0xFDC1F9, 0xFDC1FD, 0xFDC201, 0xFDC205,
                                           0xFDC209, 0xFDC20D, 0xFDC211, 0xFDC215 };
    bool right_edge = m_read8(s, RAM_FIELD_WINDOW2_EDGE) & 0x01;
    uint8_t set = (m_read8(s, RAM_FIELD_DISPLAY_FLAGS) & 0x0F) != 0;
    for (uint8_t slot = 0; slot < SLOTS; slot++) {
        const VidLineEffect e = {
            .target = slot == SLOT_WINDOW2 && right_edge ? VID_LINE_WINDOW2_RIGHT : targets[slot],
            .table = PF_GAME(line_table(set, slot)),
            .indirect = true,
            .indirect_bank = LINE_BANK,
        };
        vid_set_line_effect(m_platform(s), PF_AT(sites[slot]), slot, &e, steps);
    }
    return set;
}

/* ---- The machine at the entries ---------------------------------------------------------
 *
 * From here on: the translation between the original's registers and the C,
 * which a build for the 65816 replaces with its own calling convention. */

static void set_widths(Snes *s, bool m8, bool x8)
{
    s->cpu.p = (uint8_t)((s->cpu.p & ~(CPU_FLAG_M | CPU_FLAG_X)) | (m8 ? CPU_FLAG_M : 0) | (x8 ? CPU_FLAG_X : 0));
}

/* The guards: the routines reach the field's page through the direct page and
 * the tables through the data bank $7F, which their callers set. $FD:C2C1
 * runs with the caller's data bank, through which the builders read
 * dialog_window_state once ($FD:C995), and dispatches on
 * dialog_window_position through a table of three; a position past it jumps
 * into bytes that aren't code, which stays the original's. */
bool field_line_effects_page_is_field(Snes *s) { return s->cpu.d == RAM_PAGE_FIELD; }
bool field_line_effects_bank_is_7f(Snes *s) { return s->cpu.db == LINE_BANK; }
bool field_line_effects_page_and_bank(Snes *s) { return s->cpu.d == RAM_PAGE_FIELD && s->cpu.db == LINE_BANK; }
bool field_line_effects_page_reaches_ram(Snes *s)
{
    return s->cpu.d == RAM_PAGE_FIELD && (m_io_bank(s->cpu.db) || s->cpu.db == 0x7E);
}
bool field_line_effects_can_build(Snes *s)
{
    return field_line_effects_page_reaches_ram(s) &&
           snes_bus_peek(&s->bus, RAM_DIALOG_WINDOW_POSITION) < WINDOW_POSITIONS;
}

void field_build_line_effect_tables(Snes *s)
{
    Exit e = build_line_effect_tables(s, m_flag(s, CPU_FLAG_V));
    s->cpu.x = e.x;
    s->cpu.a = (uint16_t)(e.a_high << 8 | 0x01);
    m_set_flag(s, CPU_FLAG_C, e.carry);
    m_set_flag(s, CPU_FLAG_V, e.overflow);
    set_widths(s, true, true);
    m_rtl(s);
}

/* The builders, each an entry of its own: they return with 8-bit index
 * registers, whatever they were entered with. */
static void builder_entry(Snes *s, uint8_t set, uint8_t position)
{
    Exit e = build_for(s, set, position, m_flag(s, CPU_FLAG_V));
    s->cpu.x = e.x;
    s->cpu.y &= 0xFF;
    s->cpu.a = (uint16_t)(e.a_high << 8 | e.a_low);
    m_set_flag(s, CPU_FLAG_C, e.carry);
    m_set_flag(s, CPU_FLAG_V, e.overflow);
    set_widths(s, true, true);
    m_rts(s);
}
void field_build_no_window_0(Snes *s) { builder_entry(s, 0, WINDOW_NONE); }
void field_build_no_window_1(Snes *s) { builder_entry(s, 1, WINDOW_NONE); }
void field_build_top_window_0(Snes *s) { builder_entry(s, 0, WINDOW_AT_TOP); }
void field_build_top_window_1(Snes *s) { builder_entry(s, 1, WINDOW_AT_TOP); }
void field_build_bottom_window_0(Snes *s) { builder_entry(s, 0, WINDOW_AT_BOTTOM); }
void field_build_bottom_window_1(Snes *s) { builder_entry(s, 1, WINDOW_AT_BOTTOM); }

void field_init_line_effect_values(Snes *s)
{
    m_set_flag(s, CPU_FLAG_V, init_line_effect_values(s));
    s->cpu.a = 0xFFFF;      /* the shading copy's block move leaves it */
    m_rtl(s);
}

void field_fill_dialog_shading(Snes *s)
{
    m_set_flag(s, CPU_FLAG_V, fill_dialog_shading(s));
    s->cpu.a = 0xFFFF;
    s->cpu.db = LINE_BANK;    /* the block move leaves its destination bank */
    m_rts(s);
}

static void window2_entry(Snes *s, uint8_t set)
{
    Window2Table w = set_window2_table(s, set);
    s->cpu.x = w.last_values;
    m_set_flag(s, CPU_FLAG_C, w.which);
    m_rts(s);
}
void field_set_window2_table_0(Snes *s) { window2_entry(s, 0); }
void field_set_window2_table_1(Snes *s) { window2_entry(s, 1); }

static void bg2_entry(Snes *s, uint8_t set)
{
    m_set_a8(s, set_bg2_table(s, set));
    m_rts(s);
}
void field_set_bg2_table_0(Snes *s) { bg2_entry(s, 0); }
void field_set_bg2_table_1(Snes *s) { bg2_entry(s, 1); }

static void send_scroll_entry(Snes *s, uint8_t set)
{
    Exit e = send_scroll(s, set, s->cpu.x, m_flag(s, CPU_FLAG_C), m_flag(s, CPU_FLAG_V));
    s->cpu.x = e.x;
    s->cpu.y &= 0xFF;
    s->cpu.a = (uint16_t)(e.a_high << 8 | m_read8(s, RAM_FIELD_FIXED_COLOR_MATH_LAYERS));
    m_set_flag(s, CPU_FLAG_C, e.carry);
    m_set_flag(s, CPU_FLAG_V, e.overflow);
    set_widths(s, true, true);
    m_rts(s);
}
void field_send_scroll_0(Snes *s) { send_scroll_entry(s, 0); }
void field_send_scroll_1(Snes *s) { send_scroll_entry(s, 1); }

void field_start_wave_table(Snes *s)
{
    start_wave_table(s, 0, 0);
    s->cpu.y = 0;
    m_rts(s);
}

/* $FD:CC5E writes set 0's BG3 table, $FD:CCB8 set 1's; Y is the entry's offset
 * in it, and A's high byte is left with the last pointer's. */
static void wave_part_entry(Snes *s, uint8_t set)
{
    WaveTable w = { line_table(set, SLOT_BG3_SCROLL), s->cpu.y, set };
    WavePart p = wave_part(s, &w, (uint8_t)s->cpu.a);
    s->cpu.y = w.next;
    s->cpu.a = (uint16_t)((p.values & 0xFF00) | p.a);
    m_rts(s);
}
void field_put_wave_part_0(Snes *s) { wave_part_entry(s, 0); }
void field_put_wave_part_1(Snes *s) { wave_part_entry(s, 1); }

void field_skip_wave_lines(Snes *s)
{
    m_set_a8(s, skip_wave_lines(s, (uint8_t)s->cpu.a));
    m_rts(s);
}

/* The original reaches the DMA registers through the direct page, which it
 * loads through A (M_DP_DMA) and restores; X keeps the last table it programmed. */
void field_setup_line_effects(Snes *s)
{
    uint8_t set = setup_line_effects(s);
    s->cpu.x = RAM_ABS(line_table(set, SLOT_WINDOW2));
    s->cpu.a = (uint16_t)(M_DP_DMA | (m_read8(s, RAM_FIELD_DISPLAY_FLAGS) & 0x0F));
    m_rtl(s);
}
