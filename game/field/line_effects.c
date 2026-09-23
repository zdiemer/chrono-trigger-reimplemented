/* Field mode: the per-line effects.
 *
 * The field runs eight per-line effects: the layers' tile map addresses, BG1-BG3
 * scroll, the screens and their window masks, window 1's edges, the color math
 * layers with the fixed color, and one edge of window 2. Each reads a table in
 * bank $7F (field_line_effect_tables) in the indirect format the video
 * service's line effects take: entries of a line count and a 16-bit pointer to
 * the values in bank $7F, ended by a 0. The values hold for that many lines; a
 * count with LINES_EACH set gives each line values of its own, read on from the
 * pointer.
 *
 * There are two sets of tables. Every frame the field builds the set the line
 * effects aren't reading and flips field_display_flags bit 0, which makes the
 * next vblank program the set just built. What a set holds depends on the
 * dialog window: with none, every effect is the same over the whole screen;
 * with one open, it is a band dialog_window_state lines either side of line 48
 * (the top window) or line 171 (the bottom one), inside which BG1 and BG3 show
 * the window's tile map, the screens show the window's layers, and the color
 * math shades it (field_dialog_shading). Either layer can wave
 * (field_line_wave_bg2, field_line_wave_bg3): its scroll table then points into
 * field_line_wave every 16 lines, which the builder refills every frame from a
 * wave table in the ROM.
 *
 * The builders' arithmetic is 8-bit, as the original's: a window taller than
 * the space it has wraps the counts, and the tables say so. The per-line window
 * tables (field_window_edge_lines and its copy) are filled by the event screen
 * effects and the window shapes (field/window_shape.c); the builders only point
 * at them.
 *
 * The tables hold addresses in bank $7F as their data, which is the format the
 * line effects read: those are the one place the port spells where a variable
 * is (game/ram.h), as values it writes, never to reach memory. */
#include "field/line_effects.h"

#include "cpu_io.h"
#include "field/bridge.h"
#include "field/field_data.h"
#include "field/scratch.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"

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

/* A table being written: which one, and the offset of its next entry. */
typedef struct {
    unsigned table;
    uint16_t next;
} TableWriter;

static TableWriter table_of(uint8_t set, uint8_t slot) { return (TableWriter){ (unsigned)(set * SLOTS + slot), 0 }; }

static void put(GameState *g, TableWriter *t, uint8_t lines, uint16_t values)
{
    set_field_line_effect_tables_byte(g, t->table, t->next, lines);
    set_field_line_effect_tables_byte(g, t->table, t->next + 1u, (uint8_t)values);
    set_field_line_effect_tables_byte(g, t->table, t->next + 2u, (uint8_t)(values >> 8));
    t->next = (uint16_t)(t->next + ENTRY_SIZE);
}

static void end_table(GameState *g, TableWriter t) { set_field_line_effect_tables_byte(g, t.table, t.next, TABLE_END); }

/* ---- Where the values are, as the tables point at them ----------------------------------- */

enum { LAYER_BG1, LAYER_BG2, LAYER_BG3 };
enum { WINDOW_TOP, WINDOW_BOTTOM };
enum { SCREENS_OUTSIDE, SCREENS_WINDOW_BODY, SCREENS_WINDOW_EDGE };
enum { WAVE_LINES = 16, WAVE_SET_LINES = 2 * WAVE_LINES, WAVE_ENTRY = RAM_FIELD_LINE_WAVE_STRIDE };

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
static uint16_t screens_values(uint8_t which)
{
    return RAM_ABS(RAM_FIELD_LINE_SCREENS + which * RAM_FIELD_LINE_SCREENS_STRIDE);
}
static uint16_t color_math_values(uint8_t set, bool in_window)
{
    return RAM_ABS(RAM_FIELD_LINE_COLOR_MATH + (set + 2 * in_window) * RAM_FIELD_LINE_COLOR_MATH_STRIDE);
}
static uint16_t window1_edges_values(uint8_t set)
{
    return RAM_ABS(RAM_FIELD_LINE_WINDOW1_EDGES + set * RAM_FIELD_LINE_WINDOW1_EDGES_STRIDE);
}
static uint16_t shading_values(uint8_t set)
{
    return set ? RAM_ABS(RAM_FIELD_DIALOG_SHADING_COPY) : RAM_ABS(RAM_FIELD_DIALOG_SHADING);
}
static uint16_t window_edge_lines_values(uint8_t set)
{
    return set ? RAM_ABS(RAM_FIELD_WINDOW_EDGE_LINES_COPY) : RAM_ABS(RAM_FIELD_WINDOW_EDGE_LINES);
}
static uint16_t wave_values(uint8_t set, unsigned line)
{
    return (uint16_t)(RAM_ABS(RAM_FIELD_LINE_WAVE) + (set * WAVE_SET_LINES + line) * WAVE_ENTRY);
}

/* The two color math entries a set has: outside the window and inside it. */
static unsigned color_math(uint8_t set, bool in_window) { return set + 2u * in_window; }

/* ---- Parts of the builders ------------------------------------------------------------ */

enum { WHOLE_SCREEN_TOP = 100, WHOLE_SCREEN_BOTTOM = 112 };

/* The same values over the whole screen: two entries, since a count holds 127
 * lines at most. */
static void whole_screen(GameState *g, TableWriter t, uint16_t values)
{
    put(g, &t, WHOLE_SCREEN_TOP, values);
    put(g, &t, WHOLE_SCREEN_BOTTOM, values);
    end_table(g, t);
}

/* A waving layer over the whole screen: 13 entries of 16 lines, each reading
 * the 16 lines of the wave. */
enum { WAVE_SCREEN_PARTS = 13 };
static void wave_screen(GameState *g, TableWriter t, uint16_t wave)
{
    for (int i = 0; i < WAVE_SCREEN_PARTS; i++)
        put(g, &t, LINES_EACH | WAVE_LINES, wave);
    end_table(g, t);
}

/* Whether a layer waves, as the tables' layout sees it: the setting's low
 * byte. The values sent each frame look at the whole word. */
static bool bg2_waves(const GameState *g) { return (uint8_t)field_line_wave_bg2(g) != 0; }
static bool bg3_waves(const GameState *g) { return (uint8_t)field_line_wave_bg3(g) != 0; }

// $FD:C4B5 (set 0), $FD:C52E (set 1)
/* BG2's scroll table: BG2 has no window band, so its scroll is the same over
 * the whole screen, or waves. */
static void set_bg2_table(GameState *g, uint8_t set)
{
    TableWriter t = table_of(set, SLOT_BG2_SCROLL);
    if (!bg2_waves(g))
        whole_screen(g, t, scroll_values(LAYER_BG2, set));
    else
        wave_screen(g, t, wave_values(set, 0));
}

/* Window 2's edge table: set 0 reads one of the two per-line window tables
 * and set 1 the other, which one by bit 0 of event variable $A8's high byte
 * (the original reads it through the data bank $7F). */
enum { WINDOW2_VARIABLE = 0xA8, WINDOW2_SECOND_TABLE = 0xE0 };
typedef struct {
    uint16_t last_values;   /* where its last entry points */
    bool which;             /* the bit that chose */
} Window2Table;

static bool window2_choice(const GameState *g) { return (event_variables(g, WINDOW2_VARIABLE) >> 8) & 1; }

// $FD:C439 (set 0), $FD:C477 (set 1)
static Window2Table set_window2_table(GameState *g, uint8_t set)
{
    bool bit = window2_choice(g);
    uint16_t first = window_edge_lines_values(0);
    uint16_t lines = bit != (set != 0) ? first : (uint16_t)(first + WINDOW2_SECOND_TABLE);
    TableWriter t = table_of(set, SLOT_WINDOW2);
    uint16_t second = (uint16_t)(lines + WHOLE_SCREEN_TOP);
    put(g, &t, LINES_EACH | WHOLE_SCREEN_TOP, lines);
    put(g, &t, LINES_EACH | WHOLE_SCREEN_BOTTOM, second);
    end_table(g, t);
    return (Window2Table){ second, bit };
}

/* The wave's line, which the waving parts of a table carry on from each
 * other. The builders keep it in the field page's cell the objects' motion
 * keeps its direction in (field_direction), which nothing moving uses while
 * the tables are built. */
static uint8_t wave_line(const GameState *g) { return (uint8_t)field_direction(g); }
static void set_wave_line(GameState *g, uint8_t line)
{
    set_field_direction(g, (uint16_t)((field_direction(g) & 0xFF00) | line));
}

// $FD:CC58
static void start_wave_table(GameState *g) { set_field_direction(g, 0); }

/* For the original's code and shadow mode (field/scratch.h): the lines a
 * wave part had left when it wrote its last entry, which it keeps at $D9. */
static void leave_lines_left(GameState *g, uint8_t lines) { original_cell8(g, 0xD9, lines); }

// $FD:CC5E (set 0, BG3's table), $FD:CCB8 (set 1)
/* `lines` lines of the wave, 16 or fewer to an entry, each entry starting at
 * the wave's line. Returns where the last entry points. */
static uint16_t wave_part(GameState *g, TableWriter *t, uint8_t set, uint8_t lines)
{
    for (;;) {
        uint8_t line = wave_line(g);
        uint16_t values = wave_values(set, line & (WAVE_LINES - 1));
        if (lines < WAVE_LINES) {
            put(g, t, LINES_EACH | lines, values);
            set_wave_line(g, (uint8_t)(line + lines));
            leave_lines_left(g, lines);
            return values;
        }
        lines = (uint8_t)(lines - WAVE_LINES);
        put(g, t, LINES_EACH | WAVE_LINES, values);
        set_wave_line(g, (uint8_t)(line + WAVE_LINES));
        if (lines == 0) {
            leave_lines_left(g, lines);
            return values;
        }
    }
}

// $FD:CCB2
/* Lines that don't wave (the window band) still move the wave's line on.
 * Returns the line. */
static uint8_t skip_wave_lines(GameState *g, uint8_t lines)
{
    uint8_t line = (uint8_t)(wave_line(g) + lines);
    set_wave_line(g, line);
    return line;
}

/* One line of the wave: BG3's scroll moved by the wave's offset. The lines'
 * sums carry into each other from the first, whose carry is window 2's table
 * choice: a line whose sum wraps the scroll's range moves the next a pixel,
 * and so does the choice the first. The port adds as the original does. */
typedef struct {
    uint16_t scroll;
    bool carry;
} WaveLine;

static WaveLine wave_line_scroll(uint16_t offset, uint16_t scroll, bool carry)
{
    uint32_t sum = (uint32_t)offset + scroll + carry;
    return (WaveLine){ (uint16_t)sum, sum > 0xFFFF };
}

enum { WAVE_PHASE_STEP = 0x3E };   /* the phase's even steps into the offsets' table */

// $FD:C5A7 (set 0), $FD:C6F7 (set 1)
/* The values the set's tables point at that change every frame: the layers'
 * scroll, the wave when a layer waves (its phase moves on a line each
 * frame), and the color math. */
static void send_scroll(GameState *g, uint8_t set, bool carry_into_first_line)
{
    for (uint8_t layer = LAYER_BG1; layer <= LAYER_BG2; layer++) {
        set_field_line_scroll_h(g, layer * 2u + set, field_layer_scroll_h(g, layer));
        set_field_line_scroll_v(g, layer * 2u + set, field_layer_scroll_v(g, layer));
    }
    uint16_t bg3_h = field_layer_scroll_h(g, LAYER_BG3), bg3_v = field_layer_scroll_v(g, LAYER_BG3);
    if (field_line_wave_bg3(g) == 0 && field_line_wave_bg2(g) == 0) {
        set_field_line_scroll_h(g, LAYER_BG3 * 2u + set, bg3_h);
        set_field_line_scroll_v(g, LAYER_BG3 * 2u + set, bg3_v);
    } else {
        uint8_t phase = (uint8_t)(field_line_wave_phase(g) + 1);
        set_field_line_wave_phase(g, phase);
        uint16_t start = phase & WAVE_PHASE_STEP;
        bool carry = carry_into_first_line;
        for (unsigned i = 0; i < WAVE_LINES; i++) {
            WaveLine w = wave_line_scroll(field_wave_offset(g, (uint16_t)(start + i * WAVE_ENTRY)), bg3_h, carry);
            carry = w.carry;
            set_field_line_wave_h(g, set * WAVE_SET_LINES + i, w.scroll);
            set_field_line_wave_v(g, set * WAVE_SET_LINES + i, bg3_v);
        }
        /* The second 16 lines repeat the first, so an entry can read 16 lines
         * from any of them. */
        for (unsigned i = 0; i < WAVE_LINES; i++) {
            unsigned line = set * WAVE_SET_LINES + i;
            set_field_line_wave_h(g, line + WAVE_LINES, field_line_wave_h(g, line));
            set_field_line_wave_v(g, line + WAVE_LINES, field_line_wave_v(g, line));
        }
    }
    set_field_line_color_math_fixed_color(g, color_math(set, false), field_fixed_color(g));
    uint8_t layers = field_fixed_color_math_layers(g);
    set_field_line_color_math_layers(g, color_math(set, false), layers);
    set_field_line_color_math_layers(g, color_math(set, true), layers);
}

// $FD:C401 (set 0), $FD:C95D (set 1)
/* The same while field_display_flags bit 7 is set (the field's setup builds
 * both sets that way): the scroll from field_bg1_scroll_position and
 * field_auto_scroll_position, the color math layers from
 * field_color_math_layers_override, and no wave. */
static void send_scroll_override(GameState *g, uint8_t set)
{
    set_field_line_scroll_h(g, LAYER_BG1 * 2u + set, field_bg1_scroll_position_h(g));
    set_field_line_scroll_v(g, LAYER_BG1 * 2u + set, field_bg1_scroll_position_v(g));
    for (uint8_t layer = LAYER_BG2; layer <= LAYER_BG3; layer++) {
        uint16_t v = field_auto_scroll_position_y(g, layer - 1u);
        set_field_line_scroll_h(g, layer * 2u + set, field_auto_scroll_position_x(g, layer - 1u));
        set_field_line_scroll_v(g, layer * 2u + set, v);
    }
    set_field_line_color_math_fixed_color(g, color_math(set, false), field_fixed_color(g));
    uint8_t layers = field_color_math_layers_override(g);
    set_field_line_color_math_layers(g, color_math(set, false), layers);
    set_field_line_color_math_layers(g, color_math(set, true), layers);
}

enum { DISPLAY_FLAGS_OVERRIDE = 0x80 };

static bool overridden(const GameState *g) { return field_display_flags(g) & DISPLAY_FLAGS_OVERRIDE; }

/* ---- The builders --------------------------------------------------------------------- */

// $FD:C2EB (set 0), $FD:C847 (set 1)
/* No dialog window: every effect the same over the whole screen. Window 1
 * reads its per-line table; BG2 and BG3 may wave. */
static void build_no_window(GameState *g, uint8_t set)
{
    whole_screen(g, table_of(set, SLOT_BG_MAPS), maps_values(false));
    whole_screen(g, table_of(set, SLOT_BG1_SCROLL), scroll_values(LAYER_BG1, set));
    set_bg2_table(g, set);
    if (!bg3_waves(g))
        whole_screen(g, table_of(set, SLOT_BG3_SCROLL), scroll_values(LAYER_BG3, set));
    else
        wave_screen(g, table_of(set, SLOT_BG3_SCROLL), wave_values(set, 0));
    whole_screen(g, table_of(set, SLOT_SCREENS), screens_values(SCREENS_OUTSIDE));
    TableWriter t = table_of(set, SLOT_WINDOW1);
    uint16_t edges = window_edge_lines_values(set);
    put(g, &t, LINES_EACH | WHOLE_SCREEN_TOP, edges);
    put(g, &t, LINES_EACH | WHOLE_SCREEN_BOTTOM, (uint16_t)(edges + 2 * WHOLE_SCREEN_TOP));
    end_table(g, t);
    whole_screen(g, table_of(set, SLOT_COLOR_MATH), color_math_values(set, false));
    Window2Table w2 = set_window2_table(g, set);
    if (overridden(g))
        send_scroll_override(g, set);
    else
        send_scroll(g, set, w2.which);
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

static void band_table(GameState *g, TableWriter t, const Layout *l, uint16_t outside, uint16_t inside)
{
    for (uint8_t i = 0; i < l->count; i++)
        put(g, &t, l->lines[i], i == l->band ? inside : outside);
    end_table(g, t);
}

/* A waving layer's scroll with the band in it: the wave's line carries on
 * across the parts, the band's lines included. */
static void wave_band_table(GameState *g, TableWriter t, uint8_t set, const Layout *l, uint16_t inside)
{
    start_wave_table(g);
    for (uint8_t i = 0; i < l->count; i++) {
        if (i == l->band) {
            put(g, &t, l->lines[i], inside);
            skip_wave_lines(g, l->lines[i]);
        } else {
            wave_part(g, &t, set, l->lines[i]);
        }
    }
    end_table(g, t);
}

/* The screens: the window's frame at the band's top and bottom, its body
 * between. */
static void screens_table(GameState *g, TableWriter t, const Layout *l, const Band *b)
{
    for (uint8_t i = 0; i < l->count; i++) {
        if (i == l->band) {
            put(g, &t, b->edge, screens_values(SCREENS_WINDOW_EDGE));
            put(g, &t, b->body, screens_values(SCREENS_WINDOW_BODY));
            put(g, &t, b->edge, screens_values(SCREENS_WINDOW_EDGE));
        } else {
            put(g, &t, l->lines[i], screens_values(SCREENS_OUTSIDE));
        }
    }
    end_table(g, t);
}

/* The color math: inside the band, one line of shading per line, read from
 * the middle of field_dialog_shading out, and its last line the window's own. */
enum { SHADING_MIDDLE = 0x28 };
static void color_math_table(GameState *g, TableWriter t, uint8_t set, const Layout *l, const Band *b)
{
    uint16_t outside = color_math_values(set, false);
    uint16_t first = (uint16_t)(((uint8_t)(SHADING_MIDDLE - b->half) & 0x3F) * RAM_FIELD_DIALOG_SHADING_STRIDE);
    for (uint8_t i = 0; i < l->count; i++) {
        if (i == l->band) {
            put(g, &t, (uint8_t)(b->band - 1) | LINES_EACH, (uint16_t)(shading_values(set) + first));
            put(g, &t, 1, color_math_values(set, true));
        } else {
            put(g, &t, l->lines[i], outside);
        }
    }
    end_table(g, t);
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

/* For the original's code and shadow mode (field/scratch.h): window 1's
 * table keeps a count of lines beside the band ($DB) and its last part's
 * pointer ($D9). */
static void leave_window1_counts(GameState *g, uint8_t beside_band, uint16_t last_part)
{
    original_cell8(g, 0xDB, beside_band);
    original_cell16(g, 0xD9, last_part);
}

/* Window 1 follows its per-line table (two bytes a line) outside the band,
 * reading on past the band's lines, and covers the whole line inside it. The
 * top window's last part is longer than the other tables'. */
static void window1_table(GameState *g, uint8_t set, uint8_t position, const Layout *plain)
{
    TableWriter t = table_of(set, SLOT_WINDOW1);
    uint16_t lines = window_edge_lines_values(set);
    uint8_t beside_band = 0;
    const uint8_t last = (uint8_t)(plain->count - 1);
    for (uint8_t i = 0; i < plain->count; i++) {
        uint8_t n = plain->lines[i];
        if (i == plain->band) {
            put(g, &t, n, window1_edges_values(set));
        } else {
            if (i == plain->band - 1 || (position == WINDOW_TOP && i == plain->band + 1))
                beside_band = n;
            if (i == last) {
                leave_window1_counts(g, beside_band, lines);
                if (position == WINDOW_TOP)
                    n = TOP_WINDOW1_END;
            }
            put(g, &t, LINES_EACH | n, lines);
        }
        lines = (uint16_t)(lines + 2 * n);
    }
    end_table(g, t);
}

// $FD:C995 (set 0), $FD:CD0C (set 1): the top window
// $FD:CFCF (set 0), $FD:D27E (set 1): the bottom window
static void build_window(GameState *g, uint8_t set, uint8_t position)
{
    const Band b = dialog_band(dialog_window_state(g));
    Layout plain, waving, screens;
    if (position == WINDOW_TOP) {
        const uint8_t above = (uint8_t)(TOP_ABOVE - b.half), below = (uint8_t)(TOP_BELOW - b.half);
        plain = (Layout){ { TOP_START, above, b.band, below, TOP_END }, 5, 2 };
        waving = (Layout){ { TOP_START, above, b.band, (uint8_t)(TOP_WAVE_BELOW - b.half), TOP_END }, 5, 2 };
        /* The screens table has the lines above the band again after it; that
         * only moves where its last part ends. */
        screens = (Layout){ { TOP_START, above, b.band, above, below, TOP_END }, 6, 2 };
    } else {
        const uint8_t above = (uint8_t)(BOTTOM_EDGE - b.half + 1), below = (uint8_t)(BOTTOM_EDGE - b.half - 1);
        plain = (Layout){ { BOTTOM_START, BOTTOM_NEXT, above, b.band, below }, 5, 3 };
        waving = (Layout){ { BOTTOM_WAVE_START, BOTTOM_EDGE, above, b.band, below }, 5, 3 };
        screens = plain;
    }

    band_table(g, table_of(set, SLOT_BG_MAPS), &plain, maps_values(false), maps_values(true));
    band_table(g, table_of(set, SLOT_BG1_SCROLL), &plain, scroll_values(LAYER_BG1, set),
               window_scroll_values(position, false));
    set_bg2_table(g, set);
    uint16_t bg3_inside = window_scroll_values(position, true);
    if (!bg3_waves(g))
        band_table(g, table_of(set, SLOT_BG3_SCROLL), &plain, scroll_values(LAYER_BG3, set), bg3_inside);
    else
        wave_band_table(g, table_of(set, SLOT_BG3_SCROLL), set, &waving, bg3_inside);
    screens_table(g, table_of(set, SLOT_SCREENS), &screens, &b);
    window1_table(g, set, position, &plain);
    color_math_table(g, table_of(set, SLOT_COLOR_MATH), set, &plain, &b);
    Window2Table w2 = set_window2_table(g, set);
    send_scroll(g, set, w2.which);
}

enum { WINDOW_NONE, WINDOW_AT_TOP, WINDOW_AT_BOTTOM, WINDOW_POSITIONS };
enum { DISPLAY_FLAGS_SECOND_SET = 0x01 };

/* Whether the dialog window takes a band: it is at the top or the bottom,
 * and open. */
static bool window_open(const GameState *g, uint8_t position)
{
    return position != WINDOW_NONE && dialog_window_state(g) != 0;
}

/* The builder for a window position. */
static void build_for(GameState *g, uint8_t set, uint8_t position)
{
    if (!window_open(g, position))
        build_no_window(g, set);
    else
        build_window(g, set, position == WINDOW_AT_TOP ? WINDOW_TOP : WINDOW_BOTTOM);
}

/* The set the line effects aren't reading, which is the one built. */
static uint8_t set_to_build(const GameState *g) { return (field_display_flags(g) & DISPLAY_FLAGS_SECOND_SET) ? 0 : 1; }

// $FD:C2C1
/* Every frame: build the table set the line effects aren't reading, for
 * where the dialog window is, and make it the one they read next. */
static void build_line_effect_tables(GameState *g)
{
    build_for(g, set_to_build(g), dialog_window_position(g));
    set_field_display_flags(g, (uint8_t)(field_display_flags(g) ^ DISPLAY_FLAGS_SECOND_SET));
}

// $FD:D52D
/* The dialog window's shading, a line at a time from the window's middle out:
 * the fixed color added to BG1 from intensity 8 down to 1, then subtracted
 * from 1 up to 9, in steps of (40 / 8) lines (the remainder's lines
 * subtracting 0), and a copy of it for table set 1. The copy is a block move
 * of 256 bytes up into its own source, so its last $38 bytes repeat its
 * first. The division is the hardware divider's, whose operands and result
 * the game's state keeps. */
enum {
    SHADING_ADD = 0x01, SHADING_SUBTRACT = 0x81,
    FIXED_COLOR_ALL = 0xE0,         /* the fixed color's three channels, intensity in the low bits */
    SHADING_LINES = 0x28, SHADING_STEPS = 8,
    SHADING_COPY_BYTES = 0x100,
};

/* A run of shading lines, up to line `end`. */
static void shade(GameState *g, uint8_t *line, uint8_t end, uint8_t mode, uint8_t color)
{
    for (; *line != end; *line = (uint8_t)(*line + 1)) {
        set_field_dialog_shading_fixed_color(g, *line, color);
        set_field_dialog_shading_layers(g, *line, mode);
    }
}

/* For the original's code and shadow mode (field/scratch.h): the shading's
 * last run's end ($DB), its step and remainder in bytes ($DD, $DF), and its
 * last color ($E1). */
static void leave_shading(GameState *g, uint8_t end, uint8_t step, uint8_t rest, uint8_t color)
{
    original_cell8(g, 0xDB, end);
    original_cell8(g, 0xDD, step);
    original_cell8(g, 0xDF, rest);
    original_cell8(g, 0xE1, color);
}

static void fill_dialog_shading(GameState *g)
{
    hw_set_dividend(g, SHADING_LINES);
    hw_divide_by(g, SHADING_STEPS);
    uint8_t step = (uint8_t)hw_quotient(g), rest = (uint8_t)hw_remainder(g);
    uint8_t line = 0, end = step;
    uint8_t color = FIXED_COLOR_ALL | 8;
    for (;; color--) {
        shade(g, &line, end, SHADING_ADD, color);
        end = (uint8_t)(end + step);
        if ((uint8_t)(color - 1) == FIXED_COLOR_ALL)
            break;
    }
    end = (uint8_t)(end + rest);
    shade(g, &line, end, SHADING_SUBTRACT, FIXED_COLOR_ALL);
    for (color = FIXED_COLOR_ALL;; ) {
        end = (uint8_t)(end + step);
        if (color == (FIXED_COLOR_ALL | 9))
            break;
        color = (uint8_t)(color + 1);
        shade(g, &line, end, SHADING_SUBTRACT, color);
    }
    leave_shading(g, (uint8_t)(end * 2), (uint8_t)(step * 2), (uint8_t)(rest * 2), color);
    for (unsigned i = 0; i < SHADING_COPY_BYTES / 2; i++) {
        uint8_t mode = field_dialog_shading_layers(g, i);
        set_field_dialog_shading_copy(g, i, (uint16_t)(field_dialog_shading_fixed_color(g, i) << 8 | mode));
    }
}

/* The inside of the dialog window, which the field's setup fills once: BG1 and
 * BG3 read the window's tile map and scroll it into place, window 1 spans the
 * whole line, the window's body shows BG1 and BG3 and its frame BG1, and the
 * color math there is black. */
enum { WINDOW_MAP = 0x1C, FULL_LINE_EDGES = 0xFF00 };
enum { SCREENS_WINDOW_BODY_LAYERS = 0x05, SCREENS_WINDOW_EDGE_LAYERS = 0x01 };   /* BG1 and BG3; BG1 */

static uint32_t bytes4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return (uint32_t)a | (uint32_t)b << 8 | (uint32_t)c << 16 | (uint32_t)d << 24;
}

// $FD:C124
static void init_line_effect_values(GameState *g)
{
    uint8_t maps[3];
    for (unsigned layer = 0; layer < 3; layer++)
        maps[layer] = (uint8_t)((field_layer_map_base(g, layer) >> 8) | field_layer_map_size(g, layer));
    set_field_line_bg_maps(g, 0, bytes4(maps[0], maps[1], maps[2], 0));
    set_field_line_bg_maps(g, 1, bytes4(WINDOW_MAP, (uint8_t)((field_layer_map_base(g, 1) >> 8) | 1), WINDOW_MAP, 0));

    static const uint16_t window_v[4] = { 0x0037, 0xFFF1, 0xFFBC, 0xFF76 };
    for (unsigned i = 0; i < 4; i++) {
        set_field_line_window_scroll_h(g, i, 0);
        set_field_line_window_scroll_v(g, i, window_v[i]);
    }
    for (uint8_t set = 0; set < 2; set++) {
        set_field_line_window1_edges(g, set, FULL_LINE_EDGES);
        set_field_line_color_math_fixed_color(g, color_math(set, true), FIXED_COLOR_ALL);
    }
    set_field_line_screens(g, SCREENS_OUTSIDE, bytes4(field_main_screen(g), field_sub_screen(g), 0, 0));
    set_field_line_screens(g, SCREENS_WINDOW_BODY, bytes4(SCREENS_WINDOW_BODY_LAYERS, 0, 0, 0));
    set_field_line_screens(g, SCREENS_WINDOW_EDGE, bytes4(SCREENS_WINDOW_EDGE_LAYERS, 0, 0, 0));
    set_dialog_window_state(g, 0);
    fill_dialog_shading(g);
}

// $FD:C1EE
/* Program the field's eight line effects from the set field_display_flags'
 * low nibble picks: the layers' tile map addresses, BG1-BG3 scroll, the
 * screens and window masks, window 1's edges, the color math layers with the
 * fixed color, and one edge of window 2 -- its left one, or its right one while
 * field_window2_edge bit 0 is set. The effects are turned on elsewhere; the
 * field NMI copies the enable shadow field_line_effect_enable every frame. */
static uint8_t setup_line_effects(GameState *g, Platform *pf)
{
    static const VidLineTarget targets[SLOTS] = {
        [SLOT_BG_MAPS] = VID_LINE_BG_MAPS,       [SLOT_BG1_SCROLL] = VID_LINE_SCROLL_BG1,
        [SLOT_BG2_SCROLL] = VID_LINE_SCROLL_BG2, [SLOT_BG3_SCROLL] = VID_LINE_SCROLL_BG3,
        [SLOT_SCREENS] = VID_LINE_SCREENS,       [SLOT_WINDOW1] = VID_LINE_WINDOW1,
        [SLOT_COLOR_MATH] = VID_LINE_MATH_LAYERS, [SLOT_WINDOW2] = VID_LINE_WINDOW2_LEFT,
    };
    static const uint32_t sites[SLOTS] = { 0xFDC1F9, 0xFDC1FD, 0xFDC201, 0xFDC205,
                                           0xFDC209, 0xFDC20D, 0xFDC211, 0xFDC215 };
    /* The register writes of one effect, as the SNES backend makes them. */
    static const uint8_t steps[] = {
        VID_STEP_CONTROL,     VID_STEP_DEST,      VID_STEP_SOURCE_BANK, VID_STEP_INDIRECT_BANK,
        VID_STEP_SOURCE_LO,   VID_STEP_SOURCE_HI, VID_STEP_END,
    };
    bool right_edge = field_window2_edge(g) & 0x01;
    uint8_t set = (field_display_flags(g) & 0x0F) != 0;
    for (uint8_t slot = 0; slot < SLOTS; slot++) {
        const VidLineEffect e = {
            .target = slot == SLOT_WINDOW2 && right_edge ? VID_LINE_WINDOW2_RIGHT : targets[slot],
            .table = PF_GAME(RAM_FIELD_LINE_EFFECT_TABLES + (uint32_t)(set * SLOTS + slot) * RAM_FIELD_LINE_EFFECT_TABLES_STRIDE),
            .indirect = true,
            .indirect_bank = RAM_FIELD_LINE_EFFECT_TABLES >> 16,
        };
        vid_set_line_effect(pf, PF_AT(sites[slot]), slot, &e, steps);
    }
    return set;
}

/* ---- Where it meets the original ------------------------------------------------------
 *
 * The translation between the original's registers and the C, which a build
 * for the 65816 replaces with its own calling convention. The builders'
 * callers read what the original leaves in X, A, the carry and the overflow,
 * which follow from the tables' last writes; they are worked out here from
 * what the builders left. */

static void set_widths(Snes *s, bool m8, bool x8)
{
    s->cpu.p = (uint8_t)((s->cpu.p & ~(CPU_FLAG_M | CPU_FLAG_X)) | (m8 ? CPU_FLAG_M : 0) | (x8 ? CPU_FLAG_X : 0));
}

static bool word_sum_overflows(uint16_t a, uint16_t b, uint16_t sum) { return (~(a ^ b) & (a ^ sum) & 0x8000) != 0; }
static bool byte_difference_overflows(uint8_t a, uint8_t b)
{
    uint8_t r = (uint8_t)(a - b);
    return ((a ^ b) & (a ^ r) & 0x80) != 0;
}

/* What sending the scroll leaves in the registers: X the last pointer of
 * window 2's table (or, where a layer waves, where the wave's copy ended), A
 * the color math layers with the last scroll's high byte ($FF after a wave),
 * the carry and overflow of the wave's last line's sum (or the table choice
 * and what came in). */
typedef struct {
    uint16_t x;
    uint8_t a_low, a_high;
    bool carry, overflow;
} Leaves;

static Leaves leaves_after_send(const GameState *g, uint8_t set, bool override, uint16_t x, bool carry,
                                bool overflow)
{
    if (override)
        return (Leaves){ x, field_color_math_layers_override(g), (uint8_t)(field_auto_scroll_position_y(g, 1) >> 8),
                         carry, overflow };
    Leaves l = { x, field_fixed_color_math_layers(g), (uint8_t)(field_layer_scroll_v(g, LAYER_BG3) >> 8), carry,
                 overflow };
    if (field_line_wave_bg3(g) == 0 && field_line_wave_bg2(g) == 0)
        return l;
    l.x = wave_values(set, WAVE_LINES);
    l.a_high = 0xFF;
    uint16_t start = field_line_wave_phase(g) & WAVE_PHASE_STEP;
    uint16_t offset = field_wave_offset(g, (uint16_t)(start + (WAVE_LINES - 1) * WAVE_ENTRY));
    uint16_t scroll = field_layer_scroll_h(g, LAYER_BG3);
    uint16_t last = field_line_wave_h(g, set * WAVE_SET_LINES + WAVE_LINES - 1);
    bool carry_in = (uint16_t)(last - offset - scroll) != 0;
    l.carry = (uint32_t)offset + scroll + carry_in > 0xFFFF;
    l.overflow = word_sum_overflows(offset, scroll, last);
    return l;
}

/* The same after a whole builder: the overflow in, which only a window's
 * lines change. */
static Leaves leaves_after_build(const GameState *g, uint8_t set, uint8_t position, bool overflow)
{
    if (window_open(g, position)) {
        uint8_t half = dialog_window_state(g);
        overflow = position == WINDOW_AT_TOP ? byte_difference_overflows(TOP_BELOW, half)
                                             : byte_difference_overflows(BOTTOM_EDGE, half);
    }
    uint16_t lines = window_edge_lines_values(0);
    bool which = window2_choice(g);
    uint16_t last = (uint16_t)((which != (set != 0) ? lines : (uint16_t)(lines + WINDOW2_SECOND_TABLE)) + WHOLE_SCREEN_TOP);
    return leaves_after_send(g, set, !window_open(g, position) && overridden(g), last, which, overflow);
}

static void leave(Snes *s, Leaves l, uint8_t a_low)
{
    s->cpu.x = (uint8_t)l.x;
    s->cpu.y &= 0xFF;
    s->cpu.a = (uint16_t)(l.a_high << 8 | a_low);
    m_set_flag(s, CPU_FLAG_C, l.carry);
    m_set_flag(s, CPU_FLAG_V, l.overflow);
    set_widths(s, true, true);
}

/* The guards: the routines reach the field's page through the direct page and
 * the tables through the data bank $7F, which their callers set. $FD:C2C1
 * runs with the caller's data bank, through which the builders read
 * dialog_window_state once ($FD:C995), and dispatches on
 * dialog_window_position through a table of three; a position past it jumps
 * into bytes that aren't code, which stays the original's. */
enum { LINE_BANK = 0x7F };
bool field_line_effects_page_is_field(Snes *s) { return s->cpu.d == FIELD_PAGE; }
bool field_line_effects_bank_is_7f(Snes *s) { return s->cpu.db == LINE_BANK; }
bool field_line_effects_page_and_bank(Snes *s) { return s->cpu.d == FIELD_PAGE && s->cpu.db == LINE_BANK; }
bool field_line_effects_page_reaches_ram(Snes *s) { return field_on_page_low_bank(s); }
bool field_line_effects_can_build(Snes *s)
{
    if (!field_line_effects_page_reaches_ram(s))
        return false;
    vars_peek(true);
    bool ok = dialog_window_position(state_of(s)) < WINDOW_POSITIONS;
    vars_peek(false);
    return ok;
}

void field_build_line_effect_tables(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t set = set_to_build(g), position = dialog_window_position(g);
    build_line_effect_tables(g);
    leave(s, leaves_after_build(g, set, position, m_flag(s, CPU_FLAG_V)), 0x01);
    m_rtl(s);
}

/* The builders, each an entry of its own: they return with 8-bit index
 * registers, whatever they were entered with. */
static void builder_entry(Snes *s, uint8_t set, uint8_t position)
{
    GameState *g = state_of(s);
    build_for(g, set, position);
    Leaves l = leaves_after_build(g, set, position, m_flag(s, CPU_FLAG_V));
    leave(s, l, l.a_low);
    m_rts(s);
}
void field_build_no_window_0(Snes *s) { builder_entry(s, 0, WINDOW_NONE); }
void field_build_no_window_1(Snes *s) { builder_entry(s, 1, WINDOW_NONE); }
void field_build_top_window_0(Snes *s) { builder_entry(s, 0, WINDOW_AT_TOP); }
void field_build_top_window_1(Snes *s) { builder_entry(s, 1, WINDOW_AT_TOP); }
void field_build_bottom_window_0(Snes *s) { builder_entry(s, 0, WINDOW_AT_BOTTOM); }
void field_build_bottom_window_1(Snes *s) { builder_entry(s, 1, WINDOW_AT_BOTTOM); }

/* The shading's block move leaves A $FFFF and the data bank its destination's;
 * the overflow is that of its last run's end. */
static void after_shading(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t end = original_cell_value8(g, 0xDB), step = original_cell_value8(g, 0xDD);
    uint8_t before = (uint8_t)(end - step);
    m_set_flag(s, CPU_FLAG_V, (~(before ^ step) & (before ^ end) & 0x80) != 0);
    s->cpu.a = 0xFFFF;
}

void field_init_line_effect_values(Snes *s)
{
    init_line_effect_values(state_of(s));
    after_shading(s);
    m_rtl(s);
}

void field_fill_dialog_shading(Snes *s)
{
    fill_dialog_shading(state_of(s));
    after_shading(s);
    s->cpu.db = LINE_BANK;
    m_rts(s);
}

static void window2_entry(Snes *s, uint8_t set)
{
    Window2Table w = set_window2_table(state_of(s), set);
    s->cpu.x = w.last_values;
    m_set_flag(s, CPU_FLAG_C, w.which);
    m_rts(s);
}
void field_set_window2_table_0(Snes *s) { window2_entry(s, 0); }
void field_set_window2_table_1(Snes *s) { window2_entry(s, 1); }

/* A's low byte is left the last count the table was written with. */
static void bg2_entry(Snes *s, uint8_t set)
{
    GameState *g = state_of(s);
    set_bg2_table(g, set);
    m_set_a8(s, !bg2_waves(g) ? TABLE_END : (uint8_t)(LINES_EACH | WAVE_LINES));
    m_rts(s);
}
void field_set_bg2_table_0(Snes *s) { bg2_entry(s, 0); }
void field_set_bg2_table_1(Snes *s) { bg2_entry(s, 1); }

/* Entered with window 2's table's last pointer in X and its choice in the
 * carry, which the wave's first line takes in. */
static void send_scroll_entry(Snes *s, uint8_t set)
{
    GameState *g = state_of(s);
    bool carry = m_flag(s, CPU_FLAG_C);
    send_scroll(g, set, carry);
    Leaves l = leaves_after_send(g, set, false, s->cpu.x, carry, m_flag(s, CPU_FLAG_V));
    leave(s, l, field_fixed_color_math_layers(g));
    m_rts(s);
}
void field_send_scroll_0(Snes *s) { send_scroll_entry(s, 0); }
void field_send_scroll_1(Snes *s) { send_scroll_entry(s, 1); }

void field_start_wave_table(Snes *s)
{
    start_wave_table(state_of(s));
    s->cpu.y = 0;
    m_rts(s);
}

/* $FD:CC5E writes set 0's BG3 table, $FD:CCB8 set 1's; A is the lines and Y
 * the entry's offset in the table. A's high byte is left the last pointer's,
 * and its low byte the wave's line, or 0 where the lines ended with a whole
 * entry. */
static void wave_part_entry(Snes *s, uint8_t set)
{
    GameState *g = state_of(s);
    uint8_t lines = (uint8_t)s->cpu.a;
    TableWriter t = { table_of(set, SLOT_BG3_SCROLL).table, s->cpu.y };
    uint16_t values = wave_part(g, &t, set, lines);
    bool whole = lines >= WAVE_LINES && lines % WAVE_LINES == 0;
    s->cpu.y = t.next;
    s->cpu.a = (uint16_t)((values & 0xFF00) | (whole ? 0 : wave_line(g)));
    m_rts(s);
}
void field_put_wave_part_0(Snes *s) { wave_part_entry(s, 0); }
void field_put_wave_part_1(Snes *s) { wave_part_entry(s, 1); }

void field_skip_wave_lines(Snes *s)
{
    m_set_a8(s, skip_wave_lines(state_of(s), (uint8_t)s->cpu.a));
    m_rts(s);
}

/* The original reaches the transfer registers through the direct page, which
 * it loads through A and restores; X keeps the last table it programmed. */
void field_setup_line_effects(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t set = setup_line_effects(g, m_platform(s));
    s->cpu.x = RAM_ABS(RAM_FIELD_LINE_EFFECT_TABLES + (uint32_t)(set * SLOTS + SLOT_WINDOW2) * RAM_FIELD_LINE_EFFECT_TABLES_STRIDE);
    s->cpu.a = (uint16_t)(M_DP_DMA | (field_display_flags(g) & 0x0F));
    m_rtl(s);
}
