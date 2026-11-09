/* The sprite-object mode's per-line effects: the slots' tables, starting an
 * effect, the first plane's ripple and the irises (menu/line_effects.h says how
 * they fit together).
 *
 * An iris is a circle of window drawn line by line (scene/circle.c draws it,
 * as the scenes' irises do) into one of a slot's two tables, which it then
 * reshapes before staging it:
 *
 * - **squashed** to half its height: each line above and below the centre
 *   takes the circle's line twice as far out, and the lines past the half
 *   circle are closed;
 * - **stretched** into a capsule: the lower half moved down by the object's
 *   stretch and the gap filled with the centre line; with no radius, a bar of
 *   that many lines with both edges at the object.
 *
 * The object alternates between the slot's two tables by a frame count, so
 * the one shown is never the one being drawn. */
#include "menu/line_effects.h"
#include "menu/handlers.h"

#include "charge.h"
#include "cpu_io.h"
#include "menu/menu_data.h"
#include "menu/object.h"
#include "menu/object_commands.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "scene/scene.h"
#include "vars.h"
#include "wram.h"

/* ---- the slots ------------------------------------------------------------------------------- */

enum { FIRST_SLOT = 1, LINE_TABLE_BANK = 0x7E };

/* A slot's small table: two runs of 112 lines, each a line count (with the
 * repeat bit) and the values' address, and its end. */
enum { RUN_OF_112_LINES = 0xF0, TABLE_END = 0 };

uint16_t menu_line_values(uint8_t slot, unsigned which)
{
    static const uint16_t values[5][2] = {
        { (uint16_t)RAM_MENU_SLOT1_LINE_VALUES, (uint16_t)RAM_MENU_SLOT1_OTHER_LINE_VALUES },
        { (uint16_t)RAM_MENU_SLOT2_LINE_VALUES, (uint16_t)RAM_MENU_SLOT2_OTHER_LINE_VALUES },
        { (uint16_t)RAM_MENU_SLOT3_LINE_VALUES, (uint16_t)RAM_MENU_SLOT3_OTHER_LINE_VALUES },
        { (uint16_t)RAM_MENU_SLOT4_LINE_VALUES, (uint16_t)RAM_MENU_SLOT4_OTHER_LINE_VALUES },
        { (uint16_t)RAM_MENU_SLOT5_LINE_VALUES, (uint16_t)RAM_MENU_SLOT5_OTHER_LINE_VALUES },
    };
    return values[slot - FIRST_SLOT][which & 1];
}

void menu_program_line_channel(Platform *pf, uint32_t site, uint8_t channel, VidLineTarget target, uint32_t table)
{
    const VidLineEffect effect = { .target = target, .table = PF_GAME(table), .indirect = true,
                                   .indirect_bank = LINE_TABLE_BANK };
    vid_set_line_slot(pf, PF_AT(site), channel, &effect);
}

void menu_start_line_effect(GameState *g, Platform *pf, uint32_t site, uint8_t slot, VidLineTarget target)
{
    menu_lay_out_line_slot(g, slot);
    menu_program_line_channel(pf, site, slot, target, menu_line_slot_table(slot));
    menu_turn_on_line_slots(g, (uint8_t)(1u << slot));
}

void menu_lay_out_line_slot(GameState *g, uint8_t slot)
{
    unsigned table = slot - FIRST_SLOT;
    set_menu_object_line_slots_first_lines(g, table, RUN_OF_112_LINES);
    set_menu_object_line_slots_second_lines(g, table, RUN_OF_112_LINES);
    set_menu_object_line_slots_end(g, table, TABLE_END);
}

uint32_t menu_line_slot_table(uint8_t slot)
{
    return RAM_MENU_OBJECT_LINE_SLOTS + (uint32_t)(slot - FIRST_SLOT) * RAM_MENU_OBJECT_LINE_SLOTS_STRIDE;
}

void menu_turn_on_line_slots(GameState *g, uint8_t slots)
{
    set_menu_object_line_effects(g, (uint8_t)(menu_object_line_effects(g) | slots));
}

void menu_turn_off_line_slots(GameState *g, uint8_t slots)
{
    set_menu_object_line_effects(g, (uint8_t)(menu_object_line_effects(g) & ~slots));
}

void menu_show_line_values(GameState *g, uint8_t slot, uint16_t values)
{
    unsigned first = 2u * (slot - FIRST_SLOT);
    set_menu_object_hdma_pointers(g, first, values);
    set_menu_object_hdma_pointers(g, first + 1, (uint16_t)(values + MENU_LINE_HALF_BYTES));
}

/* ---- a table's lines ------------------------------------------------------------------------- */

/* A line's value in bank $7E, the line counted from the table's first: the
 * tables sit well inside the bank, and a line before or past one wraps round
 * it as the original's addresses do. */
static uint32_t in_line_bank(uint16_t at) { return (uint32_t)LINE_TABLE_BANK << 16 | at; }
static uint32_t line_address(uint16_t table, uint16_t line) { return in_line_bank((uint16_t)(table + 2 * line)); }
static uint16_t value_at(const GameState *g, uint16_t at)
{
    return (uint16_t)(wram_read8(g, in_line_bank(at)) | wram_read8(g, in_line_bank((uint16_t)(at + 1))) << 8);
}
static void set_value_at(GameState *g, uint16_t at, uint16_t v)
{
    wram_write8(g, in_line_bank(at), (uint8_t)v);
    wram_write8(g, in_line_bank((uint16_t)(at + 1)), (uint8_t)(v >> 8));
}
static uint16_t line_value(const GameState *g, uint16_t table, uint16_t line)
{
    return value_at(g, (uint16_t)line_address(table, line));
}
static void set_line_value(GameState *g, uint16_t table, uint16_t line, uint16_t v)
{
    set_value_at(g, (uint16_t)line_address(table, line), v);
}

/* A window line's two edges, left in the low byte and right in the high; a
 * line whose left edge is past its right has nothing open. */
static uint16_t window_edges(uint8_t left, uint8_t right) { return (uint16_t)(right << 8 | left); }
enum { CLOSED_LINE = 0x0001 };

/* ---- the irises ---------------------------------------------------------------------------- */

/* The circle's inputs, which scene/circle.c reads: the squashed iris gives
 * them as bytes over what the cells held, the capsule as words. */
static void set_up_circle(GameState *g, MenuObject o, uint16_t table, bool as_words, uint8_t mode)
{
    MenuPlace place = menu_object_place(g, o);
    uint16_t x = menu_pixel(place.x), y = menu_pixel(place.y), radius = menu_object_slots_iris_radius(g, o);
    set_scene_circle_table(g, table);
    if (as_words) {
        set_scene_circle_center_x(g, x);
        set_scene_circle_center_y(g, y);
        set_scene_circle_radius(g, radius);
        set_scene_circle_table_bank(g, LINE_TABLE_BANK);
        charge_machine_accesses(g, 1);
    } else {
        set_scene_circle_center_x(g, (uint16_t)((scene_circle_center_x(g) & 0xFF00) | (uint8_t)x));
        set_scene_circle_center_y(g, (uint16_t)((scene_circle_center_y(g) & 0xFF00) | (uint8_t)y));
        set_scene_circle_radius(g, (uint16_t)((scene_circle_radius(g) & 0xFF00) | (uint8_t)radius));
        set_scene_circle_table_bank(g, LINE_TABLE_BANK);
    }
    scene_circle_draw(g, mode);
}

// $C6:E368, $C6:E391
/* An iris squashed to half its height, in the table this frame's count
 * picks: the circle drawn at the object with its radius, then each half
 * rebuilt from every other line of itself -- line n out from the centre
 * takes the circle's line 2n out -- and the lines past it closed. */
static void squashed_iris(GameState *g, MenuObject o, uint8_t slot)
{
    uint16_t frames = (uint16_t)(menu_object_slots_iris_frames(g, o) + 1);
    set_menu_object_slots_iris_frames(g, o, frames);
    uint16_t table = menu_line_values(slot, frames);
    menu_show_line_values(g, slot, table);
    set_up_circle(g, o, table, false, SCENE_CIRCLE_ROUND);

    uint16_t centre = menu_pixel(menu_object_place(g, o).y);
    uint16_t half = menu_object_slots_iris_radius(g, o) >> 1;
    for (uint16_t n = 0; n < half; n++)
        set_line_value(g, table, (uint16_t)(centre - 1 - n), line_value(g, table, (uint16_t)(centre - 2 - 2 * n)));
    for (uint16_t n = half; n <= 2 * half; n++)
        set_line_value(g, table, (uint16_t)(centre - 1 - n), CLOSED_LINE);
    for (uint16_t n = 0; n < half; n++)
        set_line_value(g, table, (uint16_t)(centre + n), line_value(g, table, (uint16_t)(centre + 1 + 2 * n)));
    for (uint16_t n = half; n <= 2 * half; n++)
        set_line_value(g, table, (uint16_t)(centre + n), CLOSED_LINE);
    charge_machine_accesses(g, 12);
}

/* A run of lines all set to one value, from `at` on. The original fills them
 * with an overlapping block move, whose time it takes. */
static void fill_lines(GameState *g, uint16_t at, uint16_t lines, uint16_t v)
{
    wram_fill16(g, in_line_bank(at), lines, v);
    charge_block_move(g, 2u * lines);
}

/* Where the capsule's centre line is: the line above the circle's centre,
 * as the original works it out, doubled in a word with the carry the
 * doubling leaves added in. An iris centred on the top line puts its centre
 * a byte before the table, so the capsule is built from words that straddle
 * its lines, as the game draws it (Zeal's skyway). */
static uint16_t capsule_centre_at(uint16_t table, uint16_t centre_y)
{
    uint16_t above = (uint16_t)(centre_y - 1);
    return (uint16_t)(table + (uint16_t)(above << 1) + (above >> 15));
}

// $C6:E498, $C6:E4BE
/* An iris stretched into a capsule, in the table this frame's count picks:
 * the circle, its lower half moved `stretch` lines down and the centre line
 * repeated over the gap; or with no radius, a bar of `stretch` lines closed
 * to a point at the object. */
static void capsule_iris(GameState *g, MenuObject o, uint8_t slot)
{
    uint16_t frames = menu_object_slots_capsule_frames(g, o);
    uint16_t table = menu_line_values(slot, frames);
    menu_show_line_values(g, slot, table);
    set_menu_object_slots_capsule_frames(g, o, (uint16_t)(frames + 1));
    set_up_circle(g, o, table, true, SCENE_CIRCLE_ROUND);

    /* The object owns the capsule's dimensions. The circle renderer leaves
     * its input cells as scaffolding, so do not read those cells back. */
    MenuPlace place = menu_object_place(g, o);
    uint16_t radius = menu_object_slots_iris_radius(g, o);
    uint16_t stretch = menu_object_slots_iris_stretch(g, o);
    if (radius == 0) {
        /* Both edges of every line at the object's X, whose low byte the
         * original repeats over both. */
        uint8_t x = (uint8_t)menu_pixel(place.x);
        charge_machine_accesses(g, 2);
        fill_lines(g, (uint16_t)line_address(table, menu_pixel(place.y)), stretch, window_edges(x, x));
        return;
    }
    uint16_t centre_at = capsule_centre_at(table, menu_pixel(place.y));
    for (uint16_t n = 0; n < radius; n++)
        set_value_at(g, (uint16_t)(centre_at + 2 * (stretch + n)), value_at(g, (uint16_t)(centre_at - 2 * n)));
    charge_machine_accesses(g, 2);
    charge_machine_accesses(g, 2);
    fill_lines(g, (uint16_t)(centre_at + 2), (uint16_t)(stretch - 1), value_at(g, centre_at));
}

/* ---- the entries ----------------------------------------------------------------------------- */

/* The scripts' routines run on the mode's page, their object a slot. The
 * starters reach the channel through the data bank, so it is on the I/O
 * registers; the irises reach their object through it, so it is on low RAM,
 * and the game draws them on the screen: a centre line and a radius under
 * 256, a squashed iris of a radius of 2 or more (a smaller one would copy the
 * whole bank), and a capsule stretched 2 lines or more. */
enum { MODE_PAGE = 0x0000, SCREEN_LINES = 0x100 };

static bool running_a_slot(Snes *s)
{
    vars_peek(true);
    bool ok = menu_object_address_names_one(menu_object_current(state_of(s)));
    vars_peek(false);
    return ok;
}

bool menu_line_effect_can_start(Snes *s)
{
    return m_direct_page(s) == MODE_PAGE && m_io_bank(s->cpu.db) && running_a_slot(s);
}

static bool iris_takes(const GameState *g, MenuObject o, bool capsule)
{
    uint16_t y = menu_pixel(menu_object_place(g, o).y), radius = menu_object_slots_iris_radius(g, o);
    uint16_t stretch = menu_object_slots_iris_stretch(g, o);
    return y < SCREEN_LINES && radius < SCREEN_LINES
           && (capsule ? stretch >= 2 && stretch < SCREEN_LINES : radius >= 2);
}

static bool squashed_iris_takes(const GameState *g, MenuObject o) { return iris_takes(g, o, false); }
static bool capsule_iris_takes(const GameState *g, MenuObject o) { return iris_takes(g, o, true); }

static bool iris_can_draw(Snes *s, bool capsule)
{
    if (m_direct_page(s) != MODE_PAGE || !m_bank_reaches_low_ram(s) || !running_a_slot(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = iris_takes(g, menu_object_running(g), capsule);
    vars_peek(false);
    return ok;
}

bool menu_squashed_iris_can_draw(Snes *s) { return iris_can_draw(s, false); }
bool menu_capsule_iris_can_draw(Snes *s) { return iris_can_draw(s, true); }

// $C6:E568
/* Move slot 1's staged values on by four lines; whether the second address
 * ran past the end of the bank. */
enum { FOUR_LINES = 8 };

static bool advance_the_line_tables(GameState *g)
{
    uint16_t first = menu_object_hdma_pointers(g, 0), second = menu_object_hdma_pointers(g, 1);
    set_menu_object_hdma_pointers(g, 0, (uint16_t)(first + FOUR_LINES));
    set_menu_object_hdma_pointers(g, 1, (uint16_t)(second + FOUR_LINES));
    return second > 0xFFFF - FOUR_LINES;
}

/* The carry is the second address's running past the end. */
void menu_object_advance_line_tables(Snes *s)
{
    m_set_flag(s, CPU_FLAG_C, advance_the_line_tables(state_of(s)));
    m_rep(s, CPU_FLAG_M);
    m_rtl(s);
}

/* The starters are copies of one another in the ROM, one a slot and a target,
 * each programming the channel from its own code (`site`). */
static void start_from(Snes *s, uint32_t site, uint8_t slot, VidLineTarget target)
{
    menu_start_line_effect(state_of(s), m_platform(s), site, slot, target);
    m_rtl(s);
}

// $C6:E000
/* Window 1's edges line by line, on slot 1. */
void menu_object_window1_lines(Snes *s) { start_from(s, 0xC6E012, 1, VID_LINE_WINDOW1); }

// $C6:E318
void menu_object_window1_lines_2(Snes *s) { start_from(s, 0xC6E32A, 1, VID_LINE_WINDOW1); }

// $C6:E448
void menu_object_window1_lines_3(Snes *s) { start_from(s, 0xC6E45A, 1, VID_LINE_WINDOW1); }

// $C6:E340
/* Window 2's edges line by line, on slot 2. */
void menu_object_window2_lines(Snes *s) { start_from(s, 0xC6E352, 2, VID_LINE_WINDOW2); }

// $C6:E470
void menu_object_window2_lines_2(Snes *s) { start_from(s, 0xC6E482, 2, VID_LINE_WINDOW2); }

// $C6:E595
/* The first plane's vertical scroll line by line, on slot 3, the object's
 * count starting from nothing. */
static void start_the_bg1_scroll_lines(GameState *g, Platform *pf, MenuObject o)
{
    menu_start_line_effect(g, pf, 0xC6E5A7, 3, VID_LINE_SCROLL_BG1_V);
    set_menu_object_slots_phase(g, o, 0);
}

/* X comes back as the object. */
void menu_object_bg1_scroll_lines(Snes *s)
{
    GameState *g = state_of(s);
    start_the_bg1_scroll_lines(g, m_platform(s), menu_object_running(g));
    s->cpu.x = menu_object_current(g);
    m_rtl(s);
}

// $C6:E5C5
/* A frame of the first plane rippling, as the palace shakes itself apart at
 * Zeal's fall: each line of the screen scrolled up or down by up to four
 * pixels from the plane's scroll, by a sine that steps 8 of its 1024 steps a
 * line and moves on 8 steps a frame, so the wave rolls up the screen. Drawn
 * into the slot 3 table this frame's count picks (the first plane's scroll
 * lines started its line effect), which it then shows.
 *
 * A step's push is the sine's size over 64: rounded down in the first half
 * of the turn and away from zero in the second, where a size of nothing
 * comes out as a whole 256 lines up -- the original's sign extension of it,
 * kept, which the plane's wrapping shows as no push at all. */
enum { RIPPLE_SLOT = 3, RIPPLE_LINE_STEP = 8, RIPPLE_FRAME_STEP = 8, NOTHING_BELOW_ZERO = -256 };

static int16_t ripple_push(const GameState *g, uint16_t step)
{
    step &= MENU_SINE_STEPS - 1;
    int16_t size = menu_wave_size(g, step);
    if (step < MENU_SINE_STEPS / 2)
        return (int16_t)(size >> 6);
    if (size == 0)
        return NOTHING_BELOW_ZERO;
    return (int16_t)-((size * 4 + 255) >> 8);
}

/* Whether the phase came round past the top of its word. */
static bool ripple_the_first_plane(GameState *g, MenuObject o)
{
    uint16_t frames = (uint16_t)(menu_object_slots_ripple_frames(g, o) + 1);
    set_menu_object_slots_ripple_frames(g, o, frames);
    uint16_t table = menu_line_values(RIPPLE_SLOT, frames);
    menu_show_line_values(g, RIPPLE_SLOT, table);

    uint16_t phase = menu_object_slots_phase(g, o), scroll = menu_object_shadows_bg1_scroll_y(g);
    uint16_t step = phase;
    for (uint16_t line = 0; line < MENU_LINE_TABLE_LINES; line++, step += RIPPLE_LINE_STEP)
        set_line_value(g, table, line, (uint16_t)(scroll + ripple_push(g, step)));
    set_menu_object_slots_phase(g, o, (uint16_t)(phase + RIPPLE_FRAME_STEP));
    charge_machine_accesses(g, 2);
    charge_machine_io_accesses(g, 3);
    return phase > 0xFFFF - RIPPLE_FRAME_STEP;
}

/* On the mode's page, with the data bank $00, through which the original
 * reaches its object and the sine. */
bool menu_ripple_can_draw(Snes *s)
{
    return m_direct_page(s) == MODE_PAGE && m_bank_is_zero(s) && running_a_slot(s);
}

/* Back with 16-bit A, X the object, and the carry where the phase's step
 * ran past the top of its word. */
void menu_object_ripple_the_first_plane(Snes *s)
{
    GameState *g = state_of(s);
    MenuObject o = menu_object_running(g);
    bool came_round = ripple_the_first_plane(g, o);
    m_rep(s, CPU_FLAG_M);
    s->cpu.x = menu_object_current(g);
    m_set_flag(s, CPU_FLAG_C, came_round);
    m_rtl(s);
}

/* The irises come back with 16-bit registers: the squashed one with X clear
 * and the carry its radius's low bit, the capsule with X where its filling
 * ended and the carry set where it drew a circle. */
static void iris_widths(Snes *s) { m_rep(s, CPU_FLAG_M | CPU_FLAG_X); }

static void squashed_entry(Snes *s, uint8_t slot)
{
    GameState *g = state_of(s);
    MenuObject o = menu_object_running(g);
    squashed_iris(g, o, slot);
    iris_widths(s);
    s->cpu.x = 0;
    m_set_flag(s, CPU_FLAG_C, menu_object_slots_iris_radius(g, o) & 1);
    m_rtl(s);
}

/* Where the capsule's filling ended, as the original's block move leaves X:
 * on the last byte it wrote, in the table the frame count had picked. */
static uint16_t capsule_fill_end(const GameState *g, MenuObject o, uint8_t slot)
{
    uint16_t table = menu_line_values(slot, (uint16_t)(menu_object_slots_capsule_frames(g, o) - 1));
    uint16_t stretch = menu_object_slots_iris_stretch(g, o);
    if (menu_object_slots_iris_radius(g, o) == 0)
        return (uint16_t)(line_address(table, menu_pixel(menu_object_place(g, o).y)) + 2 * stretch - 1);
    return (uint16_t)(capsule_centre_at(table, menu_pixel(menu_object_place(g, o).y)) + 2 * stretch - 2);
}

static void capsule_entry(Snes *s, uint8_t slot)
{
    GameState *g = state_of(s);
    MenuObject o = menu_object_running(g);
    capsule_iris(g, o, slot);
    iris_widths(s);
    vars_peek(true);
    s->cpu.x = capsule_fill_end(g, o, slot);
    vars_peek(false);
    m_set_flag(s, CPU_FLAG_C, menu_object_slots_iris_radius(g, o) != 0);
    m_rtl(s);
}

void menu_object_squashed_iris_1(Snes *s) { squashed_entry(s, 1); }
void menu_object_squashed_iris_2(Snes *s) { squashed_entry(s, 2); }
void menu_object_capsule_iris_1(Snes *s) { capsule_entry(s, 1); }
void menu_object_capsule_iris_2(Snes *s) { capsule_entry(s, 2); }

/* ---- the curtain ------------------------------------------------------------------------------- */

/* A curtain (scene/curtain.c) on slot 2: window 1's right edge line by line,
 * from the object's table of edges, sweeping the way the object says. Each
 * frame's step sends the edges to one half of the table or the other, and the
 * slot's two runs of 112 lines show the half just sent. */
enum { CURTAIN_SLOT = 2, CURTAIN_HALF = 0x70 };

/* The half shown is reckoned from the object's own table, by the parity of
 * the curtain's steps. */
static uint16_t curtain_shown(const GameState *g, MenuObject o)
{
    uint16_t table = menu_object_slots_curtain_table(g, o);
    return (scene_curtain_steps(g) & 1) ? table : (uint16_t)(table + 2 * CURTAIN_HALF);
}

static void show_the_curtain(GameState *g, MenuObject o)
{
    uint16_t shown = curtain_shown(g, o);
    unsigned first = 2u * (CURTAIN_SLOT - FIRST_SLOT);
    set_menu_object_hdma_pointers(g, first, shown);
    set_menu_object_hdma_pointers(g, first + 1, (uint16_t)(shown + CURTAIN_HALF));
}

/* A step of it: the scene engine's step (its stir takes the caller's X, the
 * object), and the half it sent shown. */
// $C6:E27E
static void step_the_curtain(GameState *g, Platform *pf, MenuObject o, uint16_t salt)
{
    TIMED_AS(g, 0xC301E4, scene_curtain_step(g, pf, salt));
    show_the_curtain(g, o);
}

// $C6:E241
static void start_the_curtain(GameState *g, Platform *pf, MenuObject o)
{
    menu_start_line_effect(g, pf, 0xC6E253, CURTAIN_SLOT, VID_LINE_WINDOW1_RIGHT);
    scene_curtain_start(g, menu_object_slots_curtain_sweep(g, o), menu_object_slots_curtain_table(g, o), LINE_TABLE_BANK);
    step_the_curtain(g, pf, o, menu_object_current(g));
}

/* The slot can start, the object's table fits the curtain's, and the stir's
 * cells are reached through a data bank on low RAM. */
static bool curtain_takes(const GameState *g, MenuObject o)
{
    return scene_curtain_table_fits(LINE_TABLE_BANK, menu_object_slots_curtain_table(g, o));
}

static bool curtain_step_takes(const GameState *g, MenuObject o)
{
    (void)o;
    return scene_curtain_table_fits(scene_curtain_table_bank(g), scene_curtain_table(g));
}

static void as_the_curtain_leaves_it(Snes *s, MenuObject o)
{
    GameState *g = state_of(s);
    uint16_t shown = curtain_shown(g, o);
    uint32_t sum = (uint32_t)shown + CURTAIN_HALF;
    m_rep(s, CPU_FLAG_M);
    s->cpu.a = (uint16_t)sum;
    m_set_nz16(s, s->cpu.a);
    m_set_flag(s, CPU_FLAG_C, sum > 0xFFFF);
    m_set_flag(s, CPU_FLAG_V, !(shown & 0x8000) && (sum & 0x8000));
    s->cpu.x = menu_object_current(g);
}

void menu_object_start_curtain(Snes *s)
{
    GameState *g = state_of(s);
    MenuObject o = menu_object_running(g);
    start_the_curtain(g, m_platform(s), o);
    as_the_curtain_leaves_it(s, o);
    s->cpu.y = menu_object_slots_curtain_table(g, o);
    m_rtl(s);
}

void menu_object_step_curtain(Snes *s)
{
    GameState *g = state_of(s);
    MenuObject o = menu_object_running(g);
    step_the_curtain(g, m_platform(s), o, s->cpu.x);
    as_the_curtain_leaves_it(s, o);
    m_rtl(s);
}

bool menu_curtain_can_start(Snes *s)
{
    if (!menu_line_effect_can_start(s) || !m_bank_reaches_low_ram(s))
        return false;
    vars_peek(true);
    GameState *g = state_of(s);
    bool fits = curtain_takes(g, menu_object_running(g));
    vars_peek(false);
    return fits;
}

bool menu_curtain_can_step(Snes *s)
{
    if (!m_bank_reaches_low_ram(s))
        return false;
    vars_peek(true);
    bool fits = curtain_step_takes(state_of(s), 0);
    vars_peek(false);
    return fits;
}

/* ---- As C ------------------------------------------------------------------------------------- */

/* The routines as the program calls them from a script (menu/handlers.h). */
static bool advance_routine(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)pf, (void)self, (void)far;
    (void)advance_the_line_tables(g);
    return true;
}

#define STARTER(name, site, slot, target)                                            \
    static bool name##_routine(GameState *g, Platform *pf, MenuObject self, bool far) \
    {                                                                                \
        (void)self, (void)far;                                                       \
        menu_start_line_effect(g, pf, site, slot, target);                           \
        return true;                                                                 \
    }
STARTER(window1_lines, 0xC6E012, 1, VID_LINE_WINDOW1)
STARTER(window1_lines_2, 0xC6E32A, 1, VID_LINE_WINDOW1)
STARTER(window1_lines_3, 0xC6E45A, 1, VID_LINE_WINDOW1)
STARTER(window2_lines, 0xC6E352, 2, VID_LINE_WINDOW2)
STARTER(window2_lines_2, 0xC6E482, 2, VID_LINE_WINDOW2)

static bool bg1_scroll_lines_routine(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)far;
    start_the_bg1_scroll_lines(g, pf, self);
    return true;
}

#define IRIS(name, draw, slot)                                                       \
    static bool name##_routine(GameState *g, Platform *pf, MenuObject self, bool far) \
    {                                                                                \
        (void)pf, (void)far;                                                         \
        draw(g, self, slot);                                                         \
        return true;                                                                 \
    }
IRIS(squashed_iris_1, squashed_iris, 1)
IRIS(squashed_iris_2, squashed_iris, 2)
IRIS(capsule_iris_1, capsule_iris, 1)
IRIS(capsule_iris_2, capsule_iris, 2)

static bool ripple_routine(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)pf, (void)far;
    (void)ripple_the_first_plane(g, self);
    return true;
}

static bool start_the_curtain_routine(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)far;
    start_the_curtain(g, pf, self);
    return true;
}

/* The step's stir takes the caller's X: the call command's opcode doubled,
 * as the interpreter's dispatch leaves it (the far call's). */
enum { FAR_CALL_X = 2 * 0x4E, NEAR_CALL_X = 2 * 0x34 };

static bool step_the_curtain_routine(GameState *g, Platform *pf, MenuObject self, bool far)
{
    step_the_curtain(g, pf, self, far ? FAR_CALL_X : NEAR_CALL_X);
    return true;
}

/* The map scenes' irises alternate their drawing table. The round iris
 * selects the table before counting the frame; the flattened iris selects
 * it afterwards and uses the circle service's half-height lower edge. */
typedef enum { SCENE_ROUND_IRIS, SCENE_FLATTENED_IRIS } SceneIris;

// $C2:7911, $C2:7981
static uint8_t draw_scene_iris(GameState *g, MenuObject self, SceneIris shape)
{
    uint16_t frames = menu_object_slots_iris_frames(g, self);
    uint8_t before = (uint8_t)frames, after = (uint8_t)(before + 1);
    uint8_t phase = shape == SCENE_ROUND_IRIS ? before : after;
    uint8_t slot = shape == SCENE_ROUND_IRIS ? 1 : 2;
    uint16_t table = menu_line_values(slot, phase);
    set_menu_object_slots_iris_frames(g, self, (uint16_t)((frames & 0xFF00) | after));
    menu_show_line_values(g, slot, table);
    set_up_circle(g, self, table, false,
                  shape == SCENE_ROUND_IRIS ? SCENE_CIRCLE_ROUND : SCENE_CIRCLE_SQUASHED);
    return phase;
}

// $C2:7AAF
/* Advance the moving scroll table by thirty-two lines, wrapping at the
 * end of its stored values. The two staged halves keep their own places. */
static bool advance_scene_scroll(GameState *g)
{
    enum { STEP_BYTES = 32 * 2 };
    uint16_t first = menu_line_values(1, 0), end = (uint16_t)(first + 2 * MENU_LINE_HALF_BYTES);
    bool wrapped = false;
    for (unsigned half = 0; half < 2; half++) {
        uint16_t at = (uint16_t)(menu_object_hdma_pointers(g, half) + STEP_BYTES);
        wrapped = at >= end;
        set_menu_object_hdma_pointers(g, half, wrapped ? first : at);
    }
    return wrapped;
}

// $C2:7911
void menu_scene_round_iris_entry(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t phase = draw_scene_iris(g, menu_object_running(g), SCENE_ROUND_IRIS);
    s->cpu.x = menu_object_current(g);
    m_set_flag(s, CPU_FLAG_C, (phase & 1) != 0);
    m_rts(s);
}

// $C2:7981
void menu_scene_flattened_iris_entry(Snes *s)
{
    GameState *g = state_of(s);
    (void)draw_scene_iris(g, menu_object_running(g), SCENE_FLATTENED_IRIS);
    s->cpu.x = menu_object_current(g);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rep(s, CPU_FLAG_M);
    m_rts(s);
}

// $C2:7957
void menu_scene_window2_lines_entry(Snes *s)
{
    menu_start_line_effect(state_of(s), m_platform(s), 0xC2796B, 2, VID_LINE_WINDOW2);
    m_rts(s);
}

// $C2:7AAF
void menu_scene_advance_scroll_entry(Snes *s)
{
    m_set_flag(s, CPU_FLAG_C, advance_scene_scroll(state_of(s)));
    m_rep(s, CPU_FLAG_M);
    m_rts(s);
}

// $C2:7ADA, $C2:7AEB
/* Stage either the moving scroll pattern or the uniform scroll table. */
static void show_scene_scroll(GameState *g, unsigned which)
{
    menu_show_line_values(g, 1, menu_line_values(1, which));
}

// $C2:7ADA
void menu_scene_show_moving_scroll_entry(Snes *s)
{
    show_scene_scroll(state_of(s), 0);
    m_rep(s, CPU_FLAG_M);
    m_rts(s);
}

// $C2:7AEB
void menu_scene_show_uniform_scroll_entry(Snes *s)
{
    show_scene_scroll(state_of(s), 1);
    m_rep(s, CPU_FLAG_M);
    m_rts(s);
}

static bool scene_round_iris(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)pf, (void)far;
    (void)draw_scene_iris(g, self, SCENE_ROUND_IRIS);
    return true;
}

static bool scene_flattened_iris(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)pf, (void)far;
    (void)draw_scene_iris(g, self, SCENE_FLATTENED_IRIS);
    return true;
}

static bool scene_advance_scroll(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)pf, (void)self, (void)far;
    (void)advance_scene_scroll(g);
    return true;
}

static bool scene_moving_scroll(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)pf, (void)self, (void)far;
    show_scene_scroll(g, 0);
    return true;
}

static bool scene_uniform_scroll(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)pf, (void)self, (void)far;
    show_scene_scroll(g, 1);
    return true;
}

STARTER(scene_window2_lines, 0xC2796B, 2, VID_LINE_WINDOW2)

/* A travelling wave shifts the previous frame up one line and appends a
 * new sine sample. Horizontal and vertical waves use separate line slots. */
typedef enum { SCENE_WAVE_HORIZONTAL = 1, SCENE_WAVE_VERTICAL = 2 } SceneWave;
enum { SCENE_WAVE_PHASE_STEP = MENU_SINE_STEPS / 64 };

// $C2:7C56, $C2:7C97
static void start_scene_wave(GameState *g, Platform *pf, MenuObject self, SceneWave wave)
{
    bool horizontal = wave == SCENE_WAVE_HORIZONTAL;
    menu_start_line_effect(g, pf, horizontal ? 0xC27C68 : 0xC27CA9, (uint8_t)wave,
                           horizontal ? VID_LINE_SCROLL_BG2_H : VID_LINE_SCROLL_BG2_V);
    set_menu_object_slots_scene_wave_frames(g, self, 0);
    uint16_t first = menu_line_values((uint8_t)wave, 0), other = menu_line_values((uint8_t)wave, 1);
    unsigned bytes = (unsigned)(other - first) + 2u * MENU_LINE_TABLE_LINES;
    wram_fill8(g, in_line_bank(first), bytes, 0);
    charge_block_move(g, bytes - 1);
}

// $C2:7CD8, $C2:7D5E
static void advance_scene_wave(GameState *g, MenuObject self, SceneWave wave)
{
    uint8_t frames = (uint8_t)(menu_object_slots_scene_wave_frames(g, self) + 1);
    set_menu_object_slots_scene_wave_frames(g, self, frames);
    uint16_t to = menu_line_values((uint8_t)wave, frames), from = menu_line_values((uint8_t)wave, frames ^ 1u);
    unsigned retained = 2u * (MENU_LINE_TABLE_LINES - 1);
    menu_show_line_values(g, (uint8_t)wave, to);
    wram_copy(g, in_line_bank((uint16_t)(from + 2)), in_line_bank(to), retained);
    charge_block_move(g, retained);

    uint16_t phase = (uint16_t)(menu_object_slots_scene_wave_phase(g, self) + SCENE_WAVE_PHASE_STEP);
    set_menu_object_slots_scene_wave_phase(g, self, phase);
    int16_t sine = menu_sine(g, phase);
    uint8_t magnitude = (uint8_t)(sine < 0 ? -sine : sine);
    uint8_t amplitude = menu_object_slots_scene_wave_amplitude(g, self);
    int16_t sample = (int16_t)((uint16_t)magnitude * amplitude >> 8);
    uint16_t output = (uint16_t)(to + retained);
    set_value_at(g, output, (uint16_t)(sine < 0 ? -sample : sample));
    (charge_machine_accesses(g, 3), charge_machine_io_accesses(g, 4));
}

static void start_scene_wave_on_the_machine(Snes *s, SceneWave wave)
{
    GameState *g = state_of(s);
    start_scene_wave(g, m_platform(s), menu_object_running(g), wave);
    s->cpu.x = (uint16_t)(menu_line_values((uint8_t)wave, 1) + 2u * MENU_LINE_TABLE_LINES - 1);
    m_rep(s, CPU_FLAG_M);
    m_rts(s);
}

static void advance_scene_wave_on_the_machine(Snes *s, SceneWave wave)
{
    GameState *g = state_of(s);
    MenuObject self = menu_object_running(g);
    advance_scene_wave(g, self, wave);
    /* The script observes the sine helper's result and half-turn comparison.
     * Reconstructing those outputs is not another sample of game work. */
    uint32_t mark = charge_uncounted_begin(g);
    uint16_t phase = menu_object_slots_scene_wave_phase(g, self);
    s->cpu.x = (uint16_t)menu_sine(g, phase);
    m_set_flag(s, CPU_FLAG_C, (phase & (MENU_SINE_STEPS - 1)) >= MENU_SINE_STEPS / 2);
    charge_uncounted_end(g, mark);
    m_rep(s, CPU_FLAG_M);
    m_rts(s);
}

// $C2:7C56
void menu_scene_start_horizontal_wave(Snes *s) { start_scene_wave_on_the_machine(s, SCENE_WAVE_HORIZONTAL); }
// $C2:7C97
void menu_scene_start_vertical_wave(Snes *s) { start_scene_wave_on_the_machine(s, SCENE_WAVE_VERTICAL); }
// $C2:7CD8
void menu_scene_advance_horizontal_wave(Snes *s) { advance_scene_wave_on_the_machine(s, SCENE_WAVE_HORIZONTAL); }
// $C2:7D5E
void menu_scene_advance_vertical_wave(Snes *s) { advance_scene_wave_on_the_machine(s, SCENE_WAVE_VERTICAL); }

static bool scene_start_horizontal_wave(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)far;
    start_scene_wave(g, pf, self, SCENE_WAVE_HORIZONTAL);
    return true;
}

static bool scene_start_vertical_wave(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)far;
    start_scene_wave(g, pf, self, SCENE_WAVE_VERTICAL);
    return true;
}

static bool scene_advance_horizontal_wave(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)pf, (void)far;
    advance_scene_wave(g, self, SCENE_WAVE_HORIZONTAL);
    return true;
}

static bool scene_advance_vertical_wave(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)pf, (void)far;
    advance_scene_wave(g, self, SCENE_WAVE_VERTICAL);
    return true;
}

/* A fixed-colour line takes one byte, so each half of its line table
 * advances by a half-screen of bytes rather than window-edge words. */
static void show_scene_color_lines(GameState *g, uint8_t slot, uint16_t first)
{
    unsigned pair = 2u * (slot - FIRST_SLOT);
    set_menu_object_hdma_pointers(g, pair, first);
    set_menu_object_hdma_pointers(g, pair + 1, (uint16_t)(first + MENU_LINE_TABLE_LINES / 2));
}

static uint8_t scene_gray(uint8_t intensity)
{
    return vid_line_fixed_color(VID_ALL_CHANNELS, intensity);
}

// $C6:E072
/* A long white-to-black ramp, with eight lines per shade and a full
 * screen of constant colour at either end for the scene to scroll into. */
static void start_scene_gray_ramp(GameState *g, Platform *pf)
{
    enum { SHADES = 30, LINES_PER_SHADE = 8 };
    uint16_t first = menu_line_values(1, 0);
    menu_start_line_effect(g, pf, 0xC6E086, 1, VID_LINE_FIXED_COLOR);
    show_scene_color_lines(g, 1, first);
    wram_fill8(g, in_line_bank(first), MENU_LINE_TABLE_LINES, scene_gray(31));
    uint16_t at = (uint16_t)(first + MENU_LINE_TABLE_LINES);
    for (unsigned shade = SHADES; shade; shade--) {
        wram_fill8(g, in_line_bank(at), LINES_PER_SHADE, scene_gray((uint8_t)shade));
        at = (uint16_t)(at + LINES_PER_SHADE);
    }
    wram_fill8(g, in_line_bank(at), MENU_LINE_TABLE_LINES, scene_gray(0));
    charge_block_move(g, 2u * (MENU_LINE_TABLE_LINES - 1) + SHADES * (LINES_PER_SHADE - 1));
    charge_machine_accesses(g, 2);
    charge_machine_accesses(g, 2);
    charge_machine_accesses(g, 2);
}

enum { SCENE_GRAY_BAND_BYTES = 48 };

/* The narrow band has eight two-line shades followed by eight four-line
 * shades. Its moving destination is a WRAM stream: crossing a bank goes
 * on through work RAM, wrapping to its beginning after the last bank. */
static void paint_scene_gray_band(GameState *g, uint16_t first)
{
    uint32_t at = in_line_bank(first);
    for (unsigned shade = 16; shade; shade--) {
        unsigned lines = shade > 8 ? 2 : 4;
        for (unsigned line = 0; line < lines; line++)
            wram_write8(g, 0x7E0000u | (at++ & 0x1FFFFu), scene_gray((uint8_t)shade));
    }
    charge_machine_io_accesses(g, 3);
}

// $C6:E170
/* Begin with white above black, then soften their boundary with the band. */
static void start_scene_gray_band(GameState *g, Platform *pf)
{
    uint16_t first = menu_line_values(2, 0), middle = (uint16_t)(first + MENU_LINE_TABLE_LINES / 2);
    menu_start_line_effect(g, pf, 0xC6E182, 2, VID_LINE_FIXED_COLOR);
    show_scene_color_lines(g, 2, first);
    /* Each original fill includes its seed and a half-screen of copies;
     * the second fill replaces the first fill's shared final byte. */
    wram_fill8(g, in_line_bank(first), MENU_LINE_TABLE_LINES / 2 + 1, scene_gray(31));
    wram_fill8(g, in_line_bank(middle), MENU_LINE_TABLE_LINES / 2 + 1, scene_gray(0));
    charge_block_move(g, MENU_LINE_TABLE_LINES);
    paint_scene_gray_band(g, middle);
}

// $C6:E1FF
static bool move_scene_gray_band(GameState *g, MenuObject self)
{
    uint32_t at = menu_line_values(2, 0) + (uint32_t)menu_object_slots_scene_gray_offset(g, self);
    paint_scene_gray_band(g, (uint16_t)at);
    return at > UINT16_MAX;
}

// $C6:E072
void menu_scene_start_gray_ramp(Snes *s)
{
    start_scene_gray_ramp(state_of(s), m_platform(s));
    s->cpu.x = (uint16_t)(menu_line_values(1, 0) + 2u * MENU_LINE_TABLE_LINES + 30u * 8 - 1);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rep(s, CPU_FLAG_M);
    m_rtl(s);
}

// $C6:E170
void menu_scene_start_gray_band(Snes *s)
{
    start_scene_gray_band(state_of(s), m_platform(s));
    s->cpu.x = (uint16_t)(menu_line_values(2, 0) + MENU_LINE_TABLE_LINES / 2);
    m_rtl(s);
}

// $C6:E1FF
void menu_scene_move_gray_band(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t current = menu_object_current(g);
    bool wrapped = move_scene_gray_band(g, menu_object_running(g));
    s->cpu.x = current;
    m_set_flag(s, CPU_FLAG_C, wrapped);
    m_rtl(s);
}

static bool scene_start_gray_ramp(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)self, (void)far;
    start_scene_gray_ramp(g, pf);
    return true;
}

static bool scene_start_gray_band(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)self, (void)far;
    start_scene_gray_band(g, pf);
    return true;
}

static bool scene_move_gray_band(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)pf, (void)far;
    (void)move_scene_gray_band(g, self);
    return true;
}

// $C2:7A56
/* Both background planes read the same travelling scroll pattern. A second
 * table holds the unshifted view so the script can switch the effect off
 * without reprogramming its channels. */
static void start_scene_scroll_pattern(GameState *g, Platform *pf)
{
    enum { PATTERN_BYTES = 2 * MENU_LINE_TABLE_LINES };
    menu_lay_out_line_slot(g, 1);
    uint32_t table = menu_line_slot_table(1);
    menu_program_line_channel(pf, 0xC27A68, 1, VID_LINE_SCROLL_BG1_H, table);
    menu_program_line_channel(pf, 0xC27A6E, 2, VID_LINE_SCROLL_BG2_H, table);
    menu_turn_on_line_slots(g, (1u << 1) | (1u << 2));
    uint16_t first = menu_line_values(1, 0), other = menu_line_values(1, 1);
    for (unsigned i = 0; i < PATTERN_BYTES; i++)
        wram_write8(g, in_line_bank((uint16_t)(first + i)), menu_scene_scroll_pattern_byte(g, i));
    uint16_t scroll = menu_object_shadows_bg1_scroll_x(g);
    wram_fill16(g, in_line_bank(other), MENU_LINE_TABLE_LINES, scroll);
    /* The overlapping word fill includes one final low byte. */
    wram_write8(g, in_line_bank((uint16_t)(other + PATTERN_BYTES)), (uint8_t)scroll);
    charge_block_move(g, PATTERN_BYTES * 2u - 1);
}

// $C2:7A56
void menu_scene_start_scroll_pattern(Snes *s)
{
    start_scene_scroll_pattern(state_of(s), m_platform(s));
    s->cpu.x = (uint16_t)(menu_line_values(1, 1) + 2u * MENU_LINE_TABLE_LINES - 1);
    m_rep(s, CPU_FLAG_M);
    m_rts(s);
}

static bool scene_start_scroll_pattern(GameState *g, Platform *pf, MenuObject self, bool far)
{
    (void)self, (void)far;
    start_scene_scroll_pattern(g, pf);
    return true;
}

MENU_SCRIPT_ROUTINES(menu_line_effect_routines,
    { 0xC27A56, "the shared scroll pattern", NULL, scene_start_scroll_pattern },
    { 0xC6E072, "start gray ramp", NULL, scene_start_gray_ramp },
    { 0xC6E170, "start gray band", NULL, scene_start_gray_band },
    { 0xC6E1FF, "move gray band", NULL, scene_move_gray_band },

    { 0xC27C56, "start horizontal wave", NULL, scene_start_horizontal_wave },
    { 0xC27C97, "start vertical wave", NULL, scene_start_vertical_wave },
    { 0xC27CD8, "advance horizontal wave", NULL, scene_advance_horizontal_wave },
    { 0xC27D5E, "advance vertical wave", NULL, scene_advance_vertical_wave },

    { 0xC27911, "round iris", NULL, scene_round_iris },
    { 0xC27957, "window2 lines", NULL, scene_window2_lines_routine },
    { 0xC27981, "flattened iris", NULL, scene_flattened_iris },
    { 0xC27AAF, "advance scroll", NULL, scene_advance_scroll },
    { 0xC27ADA, "show moving scroll", NULL, scene_moving_scroll },
    { 0xC27AEB, "show uniform scroll", NULL, scene_uniform_scroll },

    { 0xC6E568, "slot 1's values moved on", NULL, advance_routine },
    { 0xC6E000, "window 1's edges line by line", NULL, window1_lines_routine },
    { 0xC6E318, "window 1's edges line by line", NULL, window1_lines_2_routine },
    { 0xC6E448, "window 1's edges line by line", NULL, window1_lines_3_routine },
    { 0xC6E340, "window 2's edges line by line", NULL, window2_lines_routine },
    { 0xC6E470, "window 2's edges line by line", NULL, window2_lines_2_routine },
    { 0xC6E595, "the first plane's scroll line by line", NULL, bg1_scroll_lines_routine },
    { 0xC6E5C5, "the first plane rippling", NULL, ripple_routine },
    { 0xC6E368, "a squashed iris on slot 1", squashed_iris_takes, squashed_iris_1_routine },
    { 0xC6E391, "a squashed iris on slot 2", squashed_iris_takes, squashed_iris_2_routine },
    { 0xC6E498, "a capsule iris on slot 1", capsule_iris_takes, capsule_iris_1_routine },
    { 0xC6E4BE, "a capsule iris on slot 2", capsule_iris_takes, capsule_iris_2_routine },
    { 0xC6E241, "a curtain started", curtain_takes, start_the_curtain_routine },
    { 0xC6E27E, "a curtain's step", curtain_step_takes, step_the_curtain_routine },
);
