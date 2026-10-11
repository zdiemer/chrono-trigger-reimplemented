/* Battle: the screen effects, shared between their files.
 *
 * A battle's spells and attacks dress the screen through the screen scripts:
 * two channels of commands, run by the frame work, that recolor palette rows,
 * shape the two windows line by line, shake and swing the layers and load an
 * effect's graphics. Most of the effects here are the bodies of those
 * commands; the rest run every frame (the HP gradient, pausing palette
 * animation) or once at the start of the battle (the fade tables).
 *
 *   screen_palette.c   palette rows: swapping, whitening and moving them, and
 *                      pausing palette animation with the battle
 *   screen_gradient.c  the HP gradient: a boss's colors that follow its HP
 *   screen_graphics.c  loading an effect's graphics and colors
 *   screen_window.c    the windows' per-line edges: bands, wipes, waves
 *   screen_sides.c     a window shape's straight sides, drawn down the table
 *   screen_beam.c      a beam from the top of the screen, and the window
 *                      quadrilaterals it is drawn with
 *   screen_triangles.c the window triangles' drawing, and a triangle's center
 *   screen_circles.c   the window circles (and squares), and their drawing
 *   screen_specials.c  the special effects of command $80
 *   screen_motion.c    jitter, swing and shake of the layers, and the
 *                      screen-effect settings (shake, palette presets)
 *   screen_setup.c     the fade tables, built at the start of a battle
 *
 * A command's operands follow its opcode in the script. While its body runs,
 * the script pointer is at its first operand, and the channel steps one byte
 * on when the body returns: a body that takes other than one operand says how
 * many it took (screen_command_took). */
#ifndef CT_GAME_BATTLE_SCREEN_H
#define CT_GAME_BATTLE_SCREEN_H

#include <stdbool.h>
#include <stdint.h>

#include "battle/battle.h"
#include "battle/positions.h"
#include "battle/screen_data.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "state.h"
#include "vars.h"

/* ---- The channels ------------------------------------------------------------------------ */

/* A screen script channel: 0 or 1, whose records are $10 bytes apart. */
typedef uint8_t ScreenChannel;
enum { SCREEN_CHANNELS = 2, SCREEN_CHANNEL_RECORD_BYTES = 0x10 };

/* ---- The running command ---------------------------------------------------------------- */

/* The running command's `n`th operand, from its first. */
static inline uint8_t screen_command_operand(const GameState *g, unsigned n)
{
    return battle_screen_script_byte(g, battle_screen_script(g) + n);
}

/* The command took `operands` operands: the channel's step after it lands on
 * the next command. The pointer moves within its bank. */
static inline void screen_command_took(GameState *g, int operands)
{
    uint32_t at = battle_screen_script(g);
    set_battle_screen_script(g, (at & 0xFF0000u) | (uint16_t)(at + operands - 1));
}

/* ---- Palette rows and the sine --------------------------------------------------------------- */

/* A palette row is 16 colors: rows 0-7 are the background's, 8-15 the
 * sprites' palettes 0-7. */
enum { SCREEN_ROW_COLORS = 16, SCREEN_SPRITE_ROWS = 8 };

/* An amplitude times the sine of an angle, in whole units: the sine table's
 * magnitudes are 1/256ths, and the second half of the turn is negative. The
 * result is a byte, as the effects use it (a column or a line offset). */
enum { SCREEN_HALF_TURN = 0x80, SCREEN_SINE_FRACTION_BITS = 8 };

static inline uint8_t screen_scaled_sine(const GameState *g, uint8_t angle, uint8_t amplitude)
{
    uint8_t v = (uint8_t)(battle_sine_magnitude(g, angle) * amplitude >> SCREEN_SINE_FRACTION_BITS);
    return (angle & SCREEN_HALF_TURN) ? (uint8_t)-v : v;
}

/* A signed amount times the sine of an angle, as a signed byte: the amount's
 * magnitude scaled by the sine's, its sign the product of the two signs. The
 * cosine is the sine a quarter turn on. */
enum { SCREEN_QUARTER_TURN = 0x40 };

static inline uint8_t screen_magnitude_of(uint8_t amount) { return (amount & SCREEN_HALF_TURN) ? (uint8_t)-amount : amount; }

static inline uint8_t screen_signed_sine(const GameState *g, uint8_t angle, uint8_t amount)
{
    uint8_t v = (uint8_t)(battle_sine_magnitude(g, angle) * screen_magnitude_of(amount) >> SCREEN_SINE_FRACTION_BITS);
    bool negative = ((angle & SCREEN_HALF_TURN) != 0) != ((amount & SCREEN_HALF_TURN) != 0);
    return negative ? (uint8_t)-v : v;
}

static inline uint8_t screen_signed_cosine(const GameState *g, uint8_t angle, uint8_t amount)
{
    return screen_signed_sine(g, (uint8_t)(angle + SCREEN_QUARTER_TURN), amount);
}

/* What the original's sine routines leave in the multiplier: the sine's
 * magnitude times the amount's (scaffolding, for ORIGINAL_EFFECT). */
void screen_leave_sine_product(GameState *g, uint8_t angle, uint8_t amount);

/* ---- The animation positions and the effect object ------------------------------------------ */

/* Keep the first two animation positions inside the battlefield, as the
 * original does before a script takes one up (screen_layers.c); or another
 * pair of them (positions 2 * pair and 2 * pair + 1, a pair below
 * SCREEN_POSITION_PAIRS). */
enum { SCREEN_PAIR_POSITIONS = 2, SCREEN_POSITION_PAIRS = BATTLE_ANIMATION_POSITIONS_COUNT / SCREEN_PAIR_POSITIONS };
void screen_keep_positions_on_the_battlefield(GameState *g);
void screen_keep_pair_on_the_battlefield(GameState *g, uint8_t pair);
BattlePoint screen_kept_on_the_battlefield(BattlePoint p);
uint8_t screen_position_across(const GameState *g, uint8_t position);
uint8_t screen_position_down(const GameState *g, uint8_t position);
bool screen_names_a_position(uint8_t position);

/* How far the effect object is above the battlefield's middle line, as a
 * byte. */
uint8_t screen_effect_object_rise(const GameState *g);

/* ---- The cells the original works in -------------------------------------------------------
 *
 * The effects' routines keep values in the battle page's scratch cells as they
 * work ($00-$21 and $45-$5B); a port keeps them in C and writes the final
 * values once, as scaffolding (ORIGINAL_EFFECT): nothing but the original's
 * next use of a cell, which writes it first, and shadow mode read them. `cell`
 * is the cell's offset on the page, as the original spells it. A few cells are
 * also a calling convention -- a caller's settings, a callee's input, a
 * result the original's code reads -- which only the entry wrappers and the
 * bridges read or write, through screen_cell and screen_set_cell, each saying
 * which. */
enum { SCREEN_LOW_CELLS_END = BATTLE_LOW_SCRATCH_COUNT, SCREEN_CELLS = 0x45 };

static inline uint8_t screen_cell(const GameState *g, uint8_t cell)
{
    return cell < SCREEN_LOW_CELLS_END ? battle_low_scratch(g, cell) : battle_scratch(g, (unsigned)(cell - SCREEN_CELLS));
}

static inline void screen_set_cell(GameState *g, uint8_t cell, uint8_t v)
{
    if (cell < SCREEN_LOW_CELLS_END)
        set_battle_low_scratch(g, cell, v);
    else
        set_battle_scratch(g, (unsigned)(cell - SCREEN_CELLS), v);
}

static inline uint16_t screen_cell_word(const GameState *g, uint8_t cell)
{
    return (uint16_t)(screen_cell(g, cell) | screen_cell(g, (uint8_t)(cell + 1)) << 8);
}

static inline void screen_set_cell_word(GameState *g, uint8_t cell, uint16_t v)
{
    screen_set_cell(g, cell, (uint8_t)v);
    screen_set_cell(g, (uint8_t)(cell + 1), (uint8_t)(v >> 8));
}

/* The scaffolding's writes, by the same cells. */
static inline void screen_leave_cell(GameState *g, uint8_t cell, uint8_t v) { screen_set_cell(g, cell, v); }
static inline void screen_leave_word(GameState *g, uint8_t cell, uint16_t v) { screen_set_cell_word(g, cell, v); }

/* ---- What the special effects ($80) share with the other effects ------------------------- */

/* The palette effects (screen_palette.c): background rows 2-7 and sprite
 * palettes 1-3 white; sprite palettes 5-7 white; the band of background rows
 * 2-4 moved up to rows 5-7 and back. */
void screen_whiten_scene(GameState *g);
void screen_whiten_high_sprite_palettes(GameState *g);
void screen_move_band_up(GameState *g);
void screen_move_band_down(GameState *g);
/* The window lines nudged by their per-line amounts (screen_window.c). */
void screen_nudge_window_edges(GameState *g);

/* ---- At the battle's start ------------------------------------------------------------------ */

/* The HP gradient's phase, found from where the background starts
 * (screen_gradient.c); the first eight palette animation slots' kinds kept
 * aside (screen_palette.c). */
void battle_start_gradient_phase(GameState *g);
void battle_keep_palette_kinds(GameState *g);

/* ---- The window quadrilaterals (screen_beam.c) -------------------------------------------- */

/* Two quadrilaterals of four corners each, shaped by the scripts and drawn
 * into the coming frame's window table as the window of their own number. */
enum { SCREEN_WINDOW_QUADS = 2, SCREEN_QUAD_CORNERS = 4 };
void screen_set_quad_corner(GameState *g, unsigned quad, unsigned corner, BattlePoint c);
void screen_draw_window_quad(GameState *g, unsigned quad);
/* Whether corners drawn this frame stay in the window table: on an odd frame
 * the coming buffer is the table's second, and a quadrilateral reaching below
 * the screen's last line would be drawn on past its end, as the original
 * does into the window's registers' images after it. No quadrilateral the
 * game draws goes there; the commands and entries that draw one ask this. */
bool screen_quad_fits_the_table(const GameState *g, const BattlePoint c[SCREEN_QUAD_CORNERS]);

/* ---- The window triangles (screen_triangles.c) --------------------------------------------- */

/* A triangle's three corners. */
enum { SCREEN_TRIANGLE_CORNERS = 3 };
typedef struct {
    BattlePoint corner[SCREEN_TRIANGLE_CORNERS];
} ScreenTriangle;

/* Draw a triangle into the coming frame's window table as window `window`
 * (0 or 1), from its top corner's line down to its bottom one's. */
void screen_draw_window_triangle(GameState *g, unsigned window, ScreenTriangle t);
/* Draw the right triangle on two corners, its right angle where one's
 * column meets the other's line and away from the screen's top, as window
 * `window`, and the window above it, between its top corners, up to the top
 * of the screen. */
void screen_hang_window_triangle(GameState *g, unsigned window, BattlePoint a, BattlePoint b);
/* Whether the triangle's lines stay in the window table (screen_sides.h). */
bool screen_triangle_fits_the_table(const GameState *g, ScreenTriangle t);
/* A triangle's corners as the script keeps them (screen_shapes.c), and
 * setting them. */
ScreenTriangle screen_triangle(const GameState *g, uint8_t shape);
void screen_set_triangle(GameState *g, uint8_t shape, ScreenTriangle t);
/* Where a triangle's medians cross, as the original works it out: its center. */
BattlePoint screen_triangle_center(GameState *g, ScreenTriangle t);
/* Whether the original's working out of the center is plain division for
 * this triangle (screen_triangles.c says when it isn't). */
bool screen_triangle_center_divides(ScreenTriangle t);

/* Set a screen effect from its five settings bytes (screen_motion.c says
 * what they mean). */
enum { SCREEN_EFFECT_SETTINGS = 5 };
void battle_set_screen_effect_to(GameState *g, const uint8_t settings[SCREEN_EFFECT_SETTINGS]);

/* The first setting names the effect in its high bits, one of six; a caller
 * that gives fewer than all five settings (the animation scripts' command)
 * counts the ones it gives in its low bits. */
enum { SCREEN_EFFECT_CHOICES = 6, SCREEN_EFFECT_SETTINGS_GIVEN = 0x0F };
static inline unsigned screen_effect_choice(uint8_t first_setting) { return first_setting >> 4; }
static inline bool screen_effect_known(uint8_t first_setting) { return screen_effect_choice(first_setting) < SCREEN_EFFECT_CHOICES; }
static inline uint8_t screen_effect_settings_given(uint8_t first_setting) { return first_setting & SCREEN_EFFECT_SETTINGS_GIVEN; }

/* A command's body returns with A 0, as the original's last TDC leaves it. */
static inline void screen_command_done(Snes *s)
{
    s->cpu.a = 0;
    m_rtl(s);
}

/* ---- The guards (registry .guard) ------------------------------------------------------ */

/* Every entry here runs on the battle's page with the data bank at $7E, as all
 * of their callers have them (battle/bridge.h); most also with A's high byte
 * 0, which the original's indexing takes along into its tables. The screen
 * script's dispatch calls its commands' bodies so. */
bool battle_screen_page(Snes *s);
bool battle_screen_page_a_clear(Snes *s);
/* ... running a channel (screen_motion.c); the jitter with the carry set, as
 * the dispatch leaves it; the positions saved to one of two slots; a screen
 * effect one of the six. */
bool battle_screen_channel_command(Snes *s);
bool battle_screen_dispatched_jitter(Snes *s);
bool battle_screen_saves_positions(Snes *s);
bool battle_screen_effect_is_one(Snes *s);
/* ... the HP gradient's phase and rows within its tables (screen_gradient.c). */
bool battle_gradient_page(Snes *s);
bool battle_gradient_row_is_one(Snes *s);
/* ... the window commands' shapes within the window table (screen_window.c). */
bool battle_screen_band_fits(Snes *s);
bool battle_screen_middle_band_fits(Snes *s);
bool battle_screen_copy_unshifted(Snes *s);
bool battle_screen_fill_is_one(Snes *s);
bool battle_wave_pair_is_one(Snes *s);
bool battle_screen_nudge_dispatched(Snes *s);
/* ... a window quadrilateral one of the two (screen_beam.c); $D1:F91D and
 * $D1:F972 run with the data bank the rasterizer sets for the work RAM port. */
bool battle_quad_is_one(Snes *s);
bool battle_beam_quad_is_one(Snes *s);
bool battle_quad_page(Snes *s);
bool battle_quad_corners_fit(Snes *s);
/* ... the triangle's drawing on the battle's page, its lines within the
 * window table (screen_triangles.c). */
bool battle_triangle_corners_fit(Snes *s);
/* ... a special effect of the 29, running a channel (screen_specials.c). */
bool battle_special_effect_ready(Snes *s);
/* ... a cut through the middle that fits the screen, a window carried over
 * one of the two (screen_divide.c). */
bool battle_cut_fits(Snes *s);
bool battle_carried_window_is_one(Snes *s);
/* ... a run of a circle's edges within the window table and the worked edges
 * (screen_shapes.c). */
bool battle_circle_run_fits(Snes *s);

/* ... the screen script's channels, its dispatch and its commands
 * (screen_script.c). */
bool battle_screen_channel_ready(Snes *s);
bool battle_screen_dispatch_ready(Snes *s);
bool battle_screen_command_ready(Snes *s);
bool battle_screen_triangle_ready(Snes *s);

/* ---- The entries (game/registry.c) --------------------------------------------------------- */

/* screen_script.c */
void battle_run_screen_scripts(Snes *s);
void battle_run_screen_channel(Snes *s);
void battle_run_screen_command(Snes *s);
void battle_screen_command(Snes *s);
void battle_screen_draw_triangle(Snes *s);

/* screen_palette.c */
void battle_screen_swap_palette_rows(Snes *s);
void battle_screen_whiten_palette(Snes *s);
void battle_screen_whiten_high_rows(Snes *s);
void battle_screen_move_rows_up(Snes *s);
void battle_screen_move_rows_down(Snes *s);
void battle_pause_palette_animation(Snes *s);
void battle_save_palette_kinds(Snes *s);

/* screen_gradient.c */
void battle_step_hp_gradient(Snes *s);
void battle_find_gradient_phase(Snes *s);
void battle_gradient_row_rotation(Snes *s);
void battle_write_gradient_row(Snes *s);

/* screen_graphics.c */
void battle_load_effect_graphics(Snes *s);

/* screen_window.c */
void battle_screen_open_band(Snes *s);
void battle_screen_wipe_from_left(Snes *s);
void battle_screen_wave_windows(Snes *s);
void battle_screen_wave_counted(Snes *s);
void battle_screen_wave_framed(Snes *s);
void battle_screen_add_line_deltas(Snes *s);
void battle_screen_copy_window1_to_window2(Snes *s);
void battle_screen_copy_window1_inverted(Snes *s);
void battle_screen_move_column_band(Snes *s);
void battle_screen_draw_column_band(Snes *s);
void battle_screen_fill_windows(Snes *s);
void battle_wave_w1_from_table(Snes *s);
void battle_wave_w1(Snes *s);
void battle_wave_w2(Snes *s);
void battle_fill_windows_empty(Snes *s);
void battle_fill_window1_empty(Snes *s);
void battle_fill_window2_empty(Snes *s);
void battle_fill_window1_full(Snes *s);

/* screen_beam.c */
void battle_screen_draw_beam(Snes *s);
void battle_sort_quad_corners(Snes *s);
void battle_quad_edge_slope(Snes *s);
void battle_quad_edge_slopes(Snes *s);
void battle_rasterize_window_quad(Snes *s);
void battle_draw_window_quad(Snes *s);

/* screen_triangles.c */
void battle_draw_window_triangle(Snes *s);

/* screen_specials.c */
void battle_screen_special_effect(Snes *s);

/* screen_divide.c */
void battle_cut_window(Snes *s);
void battle_carry_window_over(Snes *s);

/* screen_between.c */
void battle_measure_direction(Snes *s);

/* screen_shapes.c */
void battle_copy_circle_lines(Snes *s);

/* screen_motion.c */
void battle_screen_random_byte(Snes *s);
void battle_screen_jitter(Snes *s);
void battle_screen_swing(Snes *s);
void battle_screen_save_positions(Snes *s);
void battle_set_screen_effect(Snes *s);
void battle_stop_shake(Snes *s);
void battle_start_shake(Snes *s);
void battle_set_palette_preset(Snes *s);

/* screen_setup.c */
void battle_build_fade_tables(Snes *s);
void battle_set_the_screen_up(Snes *s);

#endif
