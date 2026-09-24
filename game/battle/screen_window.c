/* Battle: the windows' per-line edges.
 *
 * The battle's two windows mask the layers and the color math, and their
 * edges can change on every line of the screen: the line effect
 * (battle_setup_window_hdma, $CD:0AEE) sends each line both windows' left and
 * right edges from a table. The table is kept twice, and the line effect
 * shows one buffer while the effects write the other for the coming frame:
 * the second buffer on odd frames, the first on even ones. An empty window
 * has its left edge right of its right one ($FF, 0); a full one runs from 0 to
 * $FF. Four lines of the top border come before the first buffer.
 *
 * The screen script's window commands shape the table: a band that opens
 * about the first animation position, a diagonal wipe, waves down both
 * windows, per-line nudges, a band about the middle of the battlefield,
 * copies between the windows and plain fills. A few of them (the waves, the
 * wipe) always write the first buffer, and start a line early, in the border. */
#include "battle/screen.h"

#include "cpu_io.h"
#include "original_effects.h"
#include "port/native.h"

/* ---- The line table --------------------------------------------------------------------------- */

typedef enum { WINDOW_1, WINDOW_2 } Window;

/* A window's edges on a line as one word, the left edge in the low byte. */
typedef uint16_t Edges;
enum { EDGES_EMPTY = 0x00FF, EDGES_FULL = 0xFF00 };

static Edges edges_between(uint8_t left, uint8_t right) { return (Edges)(right << 8 | left); }

enum {
    SCREEN_LINES = 224,
    /* the battlefield, above the menus: the lines the bands and waves cover */
    BATTLEFIELD_LINES = 212,
    TOP_BORDER_LINES = BATTLE_WINDOW_TOP_LINES_COUNT,
};

/* A line of the table, counted from the first buffer's first: the second
 * buffer's follow the first's (224-447), and the top border's four come
 * before them (-4 to -1). */
typedef int TableLine;

static Edges line_edges(const GameState *g, TableLine line, Window w)
{
    if (line < 0)
        return w == WINDOW_1 ? battle_window_top_lines_w1(g, TOP_BORDER_LINES + line)
                             : battle_window_top_lines_w2(g, TOP_BORDER_LINES + line);
    return w == WINDOW_1 ? battle_window_lines_w1(g, line) : battle_window_lines_w2(g, line);
}

static void set_line_edges(GameState *g, TableLine line, Window w, Edges e)
{
    if (line < 0) {
        if (w == WINDOW_1)
            set_battle_window_top_lines_w1(g, TOP_BORDER_LINES + line, e);
        else
            set_battle_window_top_lines_w2(g, TOP_BORDER_LINES + line, e);
    } else if (w == WINDOW_1) {
        set_battle_window_lines_w1(g, line, e);
    } else {
        set_battle_window_lines_w2(g, line, e);
    }
}

/* The original counts lines in bytes of the table, four a line, and leaves
 * such counts in its cells. */
enum { LINE_BYTES = 4 };

/* The buffer the coming frame shows, and the one on screen now. */
static bool odd_frame(const GameState *g) { return battle_frame_count(g) & 0x01; }
static TableLine coming_line(const GameState *g, unsigned line) { return (odd_frame(g) ? SCREEN_LINES : 0) + (int)line; }
static TableLine shown_line(const GameState *g, unsigned line) { return (odd_frame(g) ? 0 : SCREEN_LINES) + (int)line; }

/* Two step counts the effects keep between calls: the across one (the band's
 * half width, the waves' damping, the wipe's steps) and the down one (the
 * band's half height, the counted wave's calls). */
typedef enum { STEPS_ACROSS, STEPS_DOWN } StepCount;

static uint8_t steps(const GameState *g, StepCount c) { return battle_window_steps(g, c); }
static void set_steps(GameState *g, StepCount c, uint8_t v) { set_battle_window_steps(g, c, v); }

/* ---- Bands ------------------------------------------------------------------------------------- */

/* The first animation position, the band's center. */
static uint8_t position_across(const GameState *g) { return (uint8_t)battle_animation_positions(g, 0); }
static uint8_t position_down(const GameState *g) { return (uint8_t)(battle_animation_positions(g, 0) >> 8); }

enum { BAND_WIDEST = 8, BAND_TALLEST = 9 };

/* The original works out the band's edges in two cells, and leaves the end
 * of its lines there after a band down. */
static void leave_band(GameState *g, uint8_t low, uint8_t high)
{
    screen_leave_cell(g, 0x45, low);
    screen_leave_cell(g, 0x46, high);
}

// $D1:EDCD
/* Screen command $EE: open a band about the first animation position, a step
 * wider each frame: across (operand 0), window 1 on every line of the
 * battlefield covers the columns within the band's half width of the
 * position, up to 8; otherwise window 2 covers whole lines within its half
 * height, up to 9, down to the battlefield's last line. */
static void open_band_across(GameState *g)
{
    uint8_t center = position_across(g), half = steps(g, STEPS_ACROSS);
    uint8_t left = center >= half ? (uint8_t)(center - half) : 0;
    uint8_t right = center + half > 0xFF ? 0xFF : (uint8_t)(center + half);
    if (half < BAND_WIDEST)
        set_steps(g, STEPS_ACROSS, (uint8_t)(half + 1));
    for (unsigned line = 0; line < BATTLEFIELD_LINES; line++)
        set_line_edges(g, coming_line(g, line), WINDOW_1, edges_between(left, right));
    ORIGINAL_EFFECT(leave_band(g, left, right));
}

static void open_band_down(GameState *g)
{
    uint8_t center = position_down(g), half = steps(g, STEPS_DOWN);
    uint8_t top = center >= half ? (uint8_t)(center - half) : 0;
    uint8_t bottom = center + half >= BATTLEFIELD_LINES ? BATTLEFIELD_LINES : (uint8_t)(center + half);
    if (half < BAND_TALLEST)
        set_steps(g, STEPS_DOWN, (uint8_t)(half + 1));
    for (unsigned line = top; line < bottom; line++)
        set_line_edges(g, coming_line(g, line), WINDOW_2, EDGES_FULL);
    ORIGINAL_EFFECT(screen_leave_word(g, 0x45, (uint16_t)(bottom * LINE_BYTES)));
}

static void open_band(GameState *g, bool across)
{
    if (across)
        open_band_across(g);
    else
        open_band_down(g);
}

enum { BAND_MIDDLE_LINE = 0x6A };

// $D1:FB68
/* Screen command $E9: move the middle band's left edge by the operand (its
 * right edge mirrors it). */
static void move_middle_band(GameState *g, uint8_t by)
{
    set_battle_column_band_left(g, (uint8_t)(battle_column_band_left(g) + by));
}

// $D1:FB72
/* Screen command $E8: grow the middle band's half height by the operand and
 * draw it: window 1 on the lines within its half height of the battlefield's
 * middle line covers the columns from its left edge to as far from the right
 * of the screen. */
static void draw_middle_band(GameState *g, uint8_t grow_by)
{
    uint8_t half = (uint8_t)(battle_column_band_half_height(g) + grow_by);
    set_battle_column_band_half_height(g, half);
    uint8_t left = battle_column_band_left(g), right = (uint8_t)~left;
    uint8_t top = (uint8_t)(BAND_MIDDLE_LINE - half), bottom = (uint8_t)(BAND_MIDDLE_LINE + half);
    for (unsigned line = top; line < bottom; line++)
        set_line_edges(g, coming_line(g, line), WINDOW_1, edges_between(left, right));
    ORIGINAL_EFFECT(screen_leave_word(g, 0x45, (uint16_t)(bottom * LINE_BYTES)));
    ORIGINAL_EFFECT(screen_leave_word(g, 0x49, edges_between(left, right)));
}

// $D1:EEA5
/* Screen command $EC: a diagonal wipe that climbs a line each call. On the
 * first buffer's lines from $BF less the calls so far up to the top border,
 * window 2's left edge steps a column right each line up, from 0, its right
 * edge at 0. Past $C0 calls the lines start below the buffer's end, in the
 * second buffer. The call count is a word across both step counts. */
static void wipe_diagonally(GameState *g)
{
    uint8_t lowest = (uint8_t)(0xC0 - steps(g, STEPS_ACROSS));
    for (unsigned n = 0; n <= lowest; n++)
        set_line_edges(g, (TableLine)lowest - 1 - (TableLine)n, WINDOW_2, (Edges)n);
    uint16_t calls = (uint16_t)(steps(g, STEPS_ACROSS) | steps(g, STEPS_DOWN) << 8);
    calls++;
    set_steps(g, STEPS_ACROSS, (uint8_t)calls);
    set_steps(g, STEPS_DOWN, (uint8_t)(calls >> 8));
}

/* ---- Waves -------------------------------------------------------------------------------------- */

/* The waves shape both windows of the first buffer down 212 lines, a pair of
 * lines at a time (the first pair starting in the border): each window's
 * edges, as one word, are its base moved by its amplitude times the sine of
 * its phase, and the phases step once a pair. As the edges move as a word,
 * the left edge waves and the right one moves only when the left wraps. */
enum { WAVE_PAIRS = BATTLE_WAVE_EDGES_COUNT, HALF_TURN = 0x80, SINE_FRACTION_BITS = 8 };

typedef struct {
    uint8_t phase[2], amplitude[2];   /* by window */
    Edges base[2];
    bool base1_per_pair;              /* window 1's base from battle_wave_edges, pair by pair */
} Waves;

/* A wave's offset: its amplitude times the sine of the phase, in whole
 * columns, negative in the second half of the turn, and taken as a signed
 * byte. */
static int16_t wave_offset(const GameState *g, uint8_t phase, uint8_t amplitude)
{
    uint8_t columns = (uint8_t)(battle_sine_magnitude(g, phase) * amplitude >> SINE_FRACTION_BITS);
    if (phase & HALF_TURN)
        columns = (uint8_t)-columns;
    return columns < 0x80 ? (int16_t)columns : (int16_t)(columns - 0x100);
}

static void set_pair_edges(GameState *g, unsigned pair, Window w, Edges e)
{
    set_line_edges(g, 2 * (TableLine)pair - 1, w, e);
    set_line_edges(g, 2 * (TableLine)pair, w, e);
}

static Edges waved(Edges base, int16_t offset) { return (Edges)(base + offset); }

/* What the waves leave in their cells, and the multiplier its last product:
 * the phases where they stopped, the amplitudes and the bases. */
static void leave_waves(GameState *g, const Waves *w)
{
    screen_leave_word(g, 0x45, w->phase[WINDOW_2]);
    screen_leave_word(g, 0x47, w->phase[WINDOW_1]);
    screen_leave_cell(g, 0x4B, w->amplitude[WINDOW_1]);
    screen_leave_cell(g, 0x4D, w->amplitude[WINDOW_2]);
    screen_leave_word(g, 0x4F, w->base[WINDOW_1]);
    screen_leave_word(g, 0x51, w->base[WINDOW_2]);
    hw_set_multiplicand(g, battle_sine_magnitude(g, (uint8_t)(w->phase[WINDOW_2] - 1)));
    hw_multiply_by(g, w->amplitude[WINDOW_2]);
}

static void draw_waves(GameState *g, Waves w)
{
    for (unsigned pair = 0; pair < WAVE_PAIRS; pair++) {
        Edges base1 = w.base1_per_pair ? battle_wave_edges(g, pair) : w.base[WINDOW_1];
        set_pair_edges(g, pair, WINDOW_1, waved(base1, wave_offset(g, w.phase[WINDOW_1], w.amplitude[WINDOW_1])));
        set_pair_edges(g, pair, WINDOW_2,
                       waved(w.base[WINDOW_2], wave_offset(g, w.phase[WINDOW_2], w.amplitude[WINDOW_2])));
        w.phase[WINDOW_1]++;
        w.phase[WINDOW_2]++;
    }
    ORIGINAL_EFFECT(leave_waves(g, &w));
}

/* The waves die down as their operands add up: the amplitude is $60 less the
 * sum so far. */
enum { WAVE_AMPLITUDE = 0x60 };

static uint8_t damped_amplitude(GameState *g, uint8_t damping)
{
    uint8_t damped = (uint8_t)(steps(g, STEPS_ACROSS) + damping);
    set_steps(g, STEPS_ACROSS, damped);
    return (uint8_t)(WAVE_AMPLITUDE - damped);
}

/* The bases: channel 0's layer position and offset, across for window 1 and
 * down for window 2. */
static void bases_from_layer(const GameState *g, Waves *w)
{
    w->base[WINDOW_1] = (Edges)(battle_screen_layer_x(g, 0) + battle_screen_layer_offset_x(g, 0));
    w->base[WINDOW_2] = (Edges)(battle_screen_layer_y(g, 0) + battle_screen_layer_offset_y(g, 0));
}

/* Phases from the frame count: window 1's moves two steps a frame, window
 * 2's four. */
static void phases_from_frame(const GameState *g, Waves *w)
{
    uint8_t frame = (uint8_t)battle_frame_count(g);
    w->phase[WINDOW_1] = (uint8_t)(frame << 1);
    w->phase[WINDOW_2] = (uint8_t)(frame << 2);
}

// $D1:EEC4
/* Screen command $ED: waves on window 1 about the per-line edges of
 * battle_wave_edges, and on window 2 about the layer's vertical position at
 * half the amplitude, flowing with the frame count. The operand damps them. */
static void wave_about_edges(GameState *g, uint8_t damping)
{
    Waves w = { .base1_per_pair = true };
    w.amplitude[WINDOW_1] = damped_amplitude(g, damping);
    w.amplitude[WINDOW_2] = w.amplitude[WINDOW_1] >> 1;
    phases_from_frame(g, &w);
    bases_from_layer(g, &w);
    draw_waves(g, w);
}

/* Both windows waving about the layer's position with one amplitude. */
static void wave_about_layer(GameState *g, uint8_t damping, Waves w)
{
    w.amplitude[WINDOW_1] = w.amplitude[WINDOW_2] = damped_amplitude(g, damping);
    bases_from_layer(g, &w);
    draw_waves(g, w);
}

/* The counted wave's first phase: its calls so far, from $A8, two steps a
 * call. */
enum { COUNTED_WAVE_START = 0xA8 };

// $D1:EFD0
/* Screen command $F3: waves about the layer's position, flowing with a count
 * of the command's calls rather than the frames. */
static void wave_counted(GameState *g, uint8_t damping)
{
    uint8_t calls = steps(g, STEPS_DOWN);
    Waves w = { 0 };
    w.phase[WINDOW_1] = (uint8_t)((uint8_t)(calls + COUNTED_WAVE_START) << 1);
    w.phase[WINDOW_2] = (uint8_t)(w.phase[WINDOW_1] << 1);
    set_steps(g, STEPS_DOWN, (uint8_t)(calls + 1));
    wave_about_layer(g, damping, w);
}

// $D1:EFE3
/* Screen command $EB: the same, flowing with the frame count. */
static void wave_with_frames(GameState *g, uint8_t damping)
{
    Waves w = { 0 };
    phases_from_frame(g, &w);
    wave_about_layer(g, damping, w);
}

/* ---- Nudges and copies ----------------------------------------------------------------------- */

/* The lines the nudges move: two runs of 64, from line 48 and from line 112. */
enum { NUDGED_LINES = 64, FIRST_NUDGED = 48, SECOND_NUDGED = 112 };

/* Window 2's nudges are a second table, which starts at another place on odd
 * frames than on even ones; both overlap window 1's. */
enum { WINDOW_2_NUDGES_ODD = 0x40, WINDOW_2_NUDGES_EVEN = 0x20 };

typedef enum { W1_LEFT, W1_RIGHT, W2_LEFT, W2_RIGHT, LINE_EDGES } Edge;

static uint8_t edge(const GameState *g, TableLine line, Edge e)
{
    switch (e) {
    case W1_LEFT: return battle_window_lines_w1_left(g, line);
    case W1_RIGHT: return battle_window_lines_w1_right(g, line);
    case W2_LEFT: return battle_window_lines_w2_left(g, line);
    default: return battle_window_lines_w2_right(g, line);
    }
}

static void set_edge(GameState *g, TableLine line, Edge e, uint8_t v)
{
    switch (e) {
    case W1_LEFT: set_battle_window_lines_w1_left(g, line, v); break;
    case W1_RIGHT: set_battle_window_lines_w1_right(g, line, v); break;
    case W2_LEFT: set_battle_window_lines_w2_left(g, line, v); break;
    default: set_battle_window_lines_w2_right(g, line, v); break;
    }
}

// $D1:F108
/* Screen command $80, operation 13: nudge the edges of the coming frame's
 * lines 48-175 by the per-line amounts in battle_window_line_deltas, the
 * same amount on the two runs of lines. The original adds them as one long
 * chain, each addition carrying its overflow into the next, so an edge after
 * one that went past 255 moves one further: with a negative amount (a byte
 * above $7F), nearly every edge but the first does. */
static void nudge_edges(GameState *g)
{
    uint8_t second_table = odd_frame(g) ? WINDOW_2_NUDGES_ODD : WINDOW_2_NUDGES_EVEN;
    bool overflowed = false;
    for (unsigned n = 0; n < NUDGED_LINES; n++) {
        uint8_t by[2] = { battle_window_line_deltas(g, n), battle_window_line_deltas(g, second_table + n) };
        const unsigned runs[2] = { FIRST_NUDGED, SECOND_NUDGED };
        for (unsigned r = 0; r < 2; r++) {
            TableLine line = coming_line(g, runs[r] + n);
            for (Edge e = W1_LEFT; e < LINE_EDGES; e++) {
                unsigned moved = edge(g, line, e) + by[e >= W2_LEFT] + overflowed;
                overflowed = moved > 0xFF;
                set_edge(g, line, e, (uint8_t)moved);
            }
        }
    }
}

// $D1:F5F5
/* Screen command $DB: the coming frame's window 2 takes window 1 as it is on
 * the screen now, on every line. */
static void copy_window_1_across(GameState *g)
{
    for (unsigned line = 0; line < SCREEN_LINES; line++)
        set_line_edges(g, coming_line(g, line), WINDOW_2, line_edges(g, shown_line(g, line), WINDOW_1));
}

/* Window 1's edges turned inside out: the left edge where the right one
 * stops, the right where the left starts. */
static Edges inside_out(Edges e) { return (Edges)(((uint16_t)~e & 0xFF) << 8 | (uint16_t)~e >> 8); }

// $D1:FD7E
/* Screen command $CF: as $DB, with window 1 turned inside out. The operand
 * shifts the lines taken, which the guard keeps to no shift. */
static void copy_window_1_inside_out(GameState *g)
{
    for (unsigned line = 0; line < SCREEN_LINES; line++)
        set_line_edges(g, coming_line(g, line), WINDOW_2, inside_out(line_edges(g, shown_line(g, line), WINDOW_1)));
    ORIGINAL_EFFECT(screen_leave_word(g, 0x45, 28 * LINE_BYTES));
}

/* ---- Fills --------------------------------------------------------------------------------------- */

typedef enum { BOTH_EMPTY, WINDOW_1_EMPTY, WINDOW_2_EMPTY, WINDOW_1_FULL, FILLS } Fill;

static const struct {
    Edges edges;
    bool window_1, window_2;
    uint32_t routine;
} fills[FILLS] = {
    [BOTH_EMPTY] = { EDGES_EMPTY, true, true, 0xD1FCF4 },
    [WINDOW_1_EMPTY] = { EDGES_EMPTY, true, false, 0xD1FC38 },
    [WINDOW_2_EMPTY] = { EDGES_EMPTY, false, true, 0xD1FC96 },
    [WINDOW_1_FULL] = { EDGES_FULL, true, false, 0xD1FBDA },
};

// $D1:FCF4 $D1:FC38 $D1:FC96 $D1:FBDA
/* Empty or fill the coming frame's windows on every line. */
static void fill_windows(GameState *g, Fill f)
{
    for (unsigned line = 0; line < SCREEN_LINES; line++) {
        if (fills[f].window_1)
            set_line_edges(g, coming_line(g, line), WINDOW_1, fills[f].edges);
        if (fills[f].window_2)
            set_line_edges(g, coming_line(g, line), WINDOW_2, fills[f].edges);
    }
}

/* ---- The entries ------------------------------------------------------------------------ */

/* The guards. The band down stays above the position's reach of the
 * battlefield's end, and the middle band within the battlefield's top half,
 * or the original's line loop runs round all of work RAM; the inside-out copy
 * takes the lines as they are (an operand of 0 or $80); the fill is one of
 * four; the wave's line pair is one of the 106. None of the others' reaches a
 * game makes. */
bool battle_screen_band_fits(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool fits = screen_command_operand(g, 0) == 0 || position_down(g) <= BATTLEFIELD_LINES + steps(g, STEPS_DOWN);
    vars_peek(false);
    return battle_screen_page_a_clear(s) && fits;
}

bool battle_screen_middle_band_fits(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool fits = (uint8_t)(battle_column_band_half_height(g) + screen_command_operand(g, 0)) <= BAND_MIDDLE_LINE;
    vars_peek(false);
    return battle_screen_page_a_clear(s) && fits;
}

enum { NO_SHIFT = 0x7F };

bool battle_screen_copy_unshifted(Snes *s)
{
    vars_peek(true);
    bool unshifted = (screen_command_operand(state_of(s), 0) & NO_SHIFT) == 0;
    vars_peek(false);
    return battle_screen_page_a_clear(s) && unshifted;
}

bool battle_screen_fill_is_one(Snes *s) { return battle_screen_page_a_clear(s) && s->cpu.x < 2 * FILLS && s->cpu.x % 2 == 0; }

enum { PAIR_BYTES = 2 * LINE_BYTES };

bool battle_wave_pair_is_one(Snes *s)
{
    return battle_screen_page_a_clear(s) && s->cpu.y % PAIR_BYTES == 0 && s->cpu.y / PAIR_BYTES < WAVE_PAIRS;
}

/* The commands return with A 0, as the original's last TDC leaves it. */
static void command_done(Snes *s)
{
    s->cpu.a = 0;
    m_rtl(s);
}

void battle_screen_open_band(Snes *s)
{
    GameState *g = state_of(s);
    open_band(g, screen_command_operand(g, 0) == 0);
    command_done(s);
}

void battle_screen_wipe_from_left(Snes *s)
{
    wipe_diagonally(state_of(s));
    command_done(s);
}

void battle_screen_wave_windows(Snes *s)
{
    GameState *g = state_of(s);
    wave_about_edges(g, screen_command_operand(g, 0));
    command_done(s);
}

void battle_screen_wave_counted(Snes *s)
{
    GameState *g = state_of(s);
    wave_counted(g, screen_command_operand(g, 0));
    command_done(s);
}

void battle_screen_wave_framed(Snes *s)
{
    GameState *g = state_of(s);
    wave_with_frames(g, screen_command_operand(g, 0));
    command_done(s);
}

/* $D1:EF63, $D1:F07E and $D1:F0E9, the waves' steps as the original calls
 * them for each pair of lines: the offset in A, taken as negative by the N
 * flag its making left; the pair in Y; the phases, amplitudes and bases in
 * the cells the waves leave them in. The first two go on to start window 2's
 * product in the multiplier, and return the carry set for window 2's phase in
 * the second half of the turn. */
static int16_t offset_in_a(const Snes *s)
{
    uint8_t v = (uint8_t)s->cpu.a;
    return (s->cpu.p & CPU_FLAG_N) ? (int16_t)(v - 0x100) : (int16_t)v;
}

static unsigned pair_in_y(const Snes *s) { return s->cpu.y / PAIR_BYTES; }

static uint8_t cell(const GameState *g, uint8_t at) { return battle_scratch(g, (unsigned)(at - SCREEN_CELLS)); }
static uint16_t cell_word(const GameState *g, uint8_t at) { return (uint16_t)(cell(g, at) | cell(g, (uint8_t)(at + 1)) << 8); }

static void hand_on_to_window_2(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t phase = cell(g, 0x45);
    hw_set_multiplicand(g, battle_sine_magnitude(g, phase));
    hw_multiply_by(g, cell(g, 0x4D));
    s->cpu.a = 0;
    m_set_flag(s, CPU_FLAG_C, phase & HALF_TURN);
    m_rts(s);
}

void battle_wave_w1_from_table(Snes *s)
{
    GameState *g = state_of(s);
    unsigned pair = pair_in_y(s);
    set_pair_edges(g, pair, WINDOW_1, waved(battle_wave_edges(g, pair), offset_in_a(s)));
    hand_on_to_window_2(s);
}

void battle_wave_w1(Snes *s)
{
    GameState *g = state_of(s);
    set_pair_edges(g, pair_in_y(s), WINDOW_1, waved(cell_word(g, 0x4F), offset_in_a(s)));
    hand_on_to_window_2(s);
}

void battle_wave_w2(Snes *s)
{
    GameState *g = state_of(s);
    set_pair_edges(g, pair_in_y(s), WINDOW_2, waved(cell_word(g, 0x51), offset_in_a(s)));
    m_rts(s);
}

/* The nudges' guard has the carry clear, as the operation's dispatch leaves
 * it. */
bool battle_screen_nudge_dispatched(Snes *s) { return battle_screen_page_a_clear(s) && !m_flag(s, CPU_FLAG_C); }

void battle_screen_add_line_deltas(Snes *s)
{
    nudge_edges(state_of(s));
    s->cpu.a = 0;
    m_rtl(s);
}

void battle_screen_copy_window1_to_window2(Snes *s)
{
    copy_window_1_across(state_of(s));
    command_done(s);
}

void battle_screen_copy_window1_inverted(Snes *s)
{
    copy_window_1_inside_out(state_of(s));
    command_done(s);
}

void battle_screen_move_column_band(Snes *s)
{
    GameState *g = state_of(s);
    move_middle_band(g, screen_command_operand(g, 0));
    command_done(s);
}

void battle_screen_draw_column_band(Snes *s)
{
    GameState *g = state_of(s);
    draw_middle_band(g, screen_command_operand(g, 0));
    command_done(s);
}

/* A fill returns with its edges in A, which its callers read the high byte
 * of. */
static void filled(Snes *s, Fill f)
{
    fill_windows(state_of(s), f);
    s->cpu.a = fills[f].edges;
}

// $D1:FBCE
/* Screen command $88: fill the windows as the operand says (X, doubled). */
void battle_screen_fill_windows(Snes *s)
{
    Fill f = (Fill)(s->cpu.x / 2);
    NATIVE_TIME_AS(s, fills[f].routine, filled(s, f));
    m_rtl(s);
}

void battle_fill_windows_empty(Snes *s)
{
    filled(s, BOTH_EMPTY);
    m_rts(s);
}

void battle_fill_window1_empty(Snes *s)
{
    filled(s, WINDOW_1_EMPTY);
    m_rts(s);
}

void battle_fill_window2_empty(Snes *s)
{
    filled(s, WINDOW_2_EMPTY);
    m_rts(s);
}

void battle_fill_window1_full(Snes *s)
{
    filled(s, WINDOW_1_FULL);
    m_rts(s);
}
