/* The sprite-object mode's per-line effects: the slots' tables, starting an
 * effect, and the irises (menu/line_effects.h says how they fit together).
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
#include "menu/object.h"
#include "original_effects.h"
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
static void set_up_circle(GameState *g, MenuObject o, uint16_t table, bool as_words)
{
    MenuPlace place = menu_object_place(g, o);
    uint16_t x = menu_pixel(place.x), y = menu_pixel(place.y), radius = menu_object_slots_iris_radius(g, o);
    set_scene_circle_table(g, table);
    if (as_words) {
        set_scene_circle_center_x(g, x);
        set_scene_circle_center_y(g, y);
        set_scene_circle_radius(g, radius);
        set_scene_circle_table_bank(g, LINE_TABLE_BANK);
        ORIGINAL_EFFECT(set_menu_circle_bank_high(g, 0));
    } else {
        set_scene_circle_center_x(g, (uint16_t)((scene_circle_center_x(g) & 0xFF00) | (uint8_t)x));
        set_scene_circle_center_y(g, (uint16_t)((scene_circle_center_y(g) & 0xFF00) | (uint8_t)y));
        set_scene_circle_radius(g, (uint16_t)((scene_circle_radius(g) & 0xFF00) | (uint8_t)radius));
        set_scene_circle_table_bank(g, LINE_TABLE_BANK);
    }
    scene_circle_draw(g, 0);
}

/* The squashed iris's working addresses, for the original's cells: where its
 * copies stood when they had run. */
static void leave_squashed_cells(GameState *g, uint16_t table, uint16_t centre, uint16_t half)
{
    uint16_t centre_at = (uint16_t)line_address(table, centre);
    set_menu_object_scratch_iris_lines_left(g, 0);
    set_menu_object_scratch_iris_half(g, half);
    set_menu_object_scratch_iris_upper_from(g, (uint16_t)(centre_at - 4));
    set_menu_object_scratch_iris_upper_to(g, (uint16_t)(centre_at - 2));
    set_menu_object_scratch_iris_lower_from(g, (uint16_t)(centre_at + 2));
    set_menu_object_scratch_iris_lower_to(g, centre_at);
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
    set_up_circle(g, o, table, false);

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
    ORIGINAL_EFFECT(leave_squashed_cells(g, table, centre, half));
}

/* A run of lines all set to one value, from `at` on. The original fills them
 * with an overlapping block move, whose time it takes. */
static void fill_lines(GameState *g, uint16_t at, uint16_t lines, uint16_t v)
{
    wram_fill16(g, in_line_bank(at), lines, v);
    ORIGINAL_EFFECT(charge_block_move(g, 2u * lines));
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
    set_up_circle(g, o, table, true);

    uint16_t stretch = menu_object_slots_iris_stretch(g, o);
    if (menu_object_slots_iris_radius(g, o) == 0) {
        /* Both edges of every line at the object's X, whose low byte the
         * original repeats over both. */
        MenuPlace place = menu_object_place(g, o);
        uint8_t x = (uint8_t)menu_pixel(place.x);
        ORIGINAL_EFFECT(set_menu_object_scratch_iris_centre(g, stretch));
        fill_lines(g, (uint16_t)line_address(table, menu_pixel(place.y)), stretch, window_edges(x, x));
        return;
    }
    uint16_t centre_at = capsule_centre_at(table, scene_circle_center_y(g));
    for (uint16_t n = 0; n < scene_circle_radius(g); n++)
        set_value_at(g, (uint16_t)(centre_at + 2 * (stretch + n)), value_at(g, (uint16_t)(centre_at - 2 * n)));
    ORIGINAL_EFFECT(set_menu_object_scratch_iris_centre(g, centre_at));
    ORIGINAL_EFFECT(set_menu_object_scratch_iris_lines_left(g, 0));
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
    return (uint16_t)(capsule_centre_at(table, scene_circle_center_y(g)) + 2 * stretch - 2);
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

/* Back, A has the second half's address as the last add left it, in 16-bit A,
 * with its flags, and X the object; the start leaves Y the object's table,
 * which it loaded. */
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

// $C6:E27E
/* A frame's step of a curtain the object started; the stir takes X as the
 * caller has it. */
void menu_object_step_curtain(Snes *s)
{
    GameState *g = state_of(s);
    MenuObject o = menu_object_running(g);
    step_the_curtain(g, m_platform(s), o, s->cpu.x);
    as_the_curtain_leaves_it(s, o);
    m_rtl(s);
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

MENU_SCRIPT_ROUTINES(menu_line_effect_routines,
    { 0xC6E568, "slot 1's values moved on", NULL, advance_routine },
    { 0xC6E000, "window 1's edges line by line", NULL, window1_lines_routine },
    { 0xC6E318, "window 1's edges line by line", NULL, window1_lines_2_routine },
    { 0xC6E448, "window 1's edges line by line", NULL, window1_lines_3_routine },
    { 0xC6E340, "window 2's edges line by line", NULL, window2_lines_routine },
    { 0xC6E470, "window 2's edges line by line", NULL, window2_lines_2_routine },
    { 0xC6E595, "the first plane's scroll line by line", NULL, bg1_scroll_lines_routine },
    { 0xC6E368, "a squashed iris on slot 1", squashed_iris_takes, squashed_iris_1_routine },
    { 0xC6E391, "a squashed iris on slot 2", squashed_iris_takes, squashed_iris_2_routine },
    { 0xC6E498, "a capsule iris on slot 1", capsule_iris_takes, capsule_iris_1_routine },
    { 0xC6E4BE, "a capsule iris on slot 2", capsule_iris_takes, capsule_iris_2_routine },
    { 0xC6E241, "a curtain started", curtain_takes, start_the_curtain_routine },
    { 0xC6E27E, "a curtain's step", curtain_step_takes, step_the_curtain_routine },
);
