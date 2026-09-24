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
    static const uint16_t values[2][2] = {
        { (uint16_t)RAM_MENU_OBJECT_WAVE_SCROLL, 0x8DF6 },
        { 0x9176, 0x9416 },
    };
    return values[slot - FIRST_SLOT][which & 1];
}

void menu_start_line_effect(GameState *g, Platform *pf, uint32_t site, uint8_t slot, VidLineTarget target)
{
    static const uint8_t steps[] = {
        VID_STEP_CONTROL,     VID_STEP_DEST,          VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
        VID_STEP_SOURCE_BANK, VID_STEP_INDIRECT_BANK, VID_STEP_END,
    };
    unsigned table = slot - FIRST_SLOT;
    set_menu_object_line_slots_first_lines(g, table, RUN_OF_112_LINES);
    set_menu_object_line_slots_second_lines(g, table, RUN_OF_112_LINES);
    set_menu_object_line_slots_end(g, table, TABLE_END);
    uint32_t at = RAM_MENU_OBJECT_LINE_SLOTS + table * RAM_MENU_OBJECT_LINE_SLOTS_STRIDE;
    const VidLineEffect effect = { .target = target, .table = PF_GAME(at), .indirect = true,
                                   .indirect_bank = LINE_TABLE_BANK };
    vid_set_line_effect(pf, PF_AT(site), slot, &effect, steps);
    set_menu_object_line_effects(g, (uint8_t)(menu_object_line_effects(g) | 1u << slot));
}

void menu_show_line_values(GameState *g, uint8_t slot, uint16_t values)
{
    unsigned first = 2u * (slot - FIRST_SLOT);
    set_menu_object_hdma_pointers(g, first, values);
    set_menu_object_hdma_pointers(g, first + 1, (uint16_t)(values + MENU_LINE_HALF_BYTES));
}

/* ---- a table's lines ------------------------------------------------------------------------- */

/* A line's value in bank $7E; the tables sit well inside it, and the copies
 * the irises make wrap round it as the original's do. */
static uint32_t in_bank(uint16_t at) { return (uint32_t)LINE_TABLE_BANK << 16 | at; }
static uint16_t line_at(const GameState *g, uint16_t at)
{
    return (uint16_t)(wram_read8(g, in_bank(at)) | wram_read8(g, in_bank((uint16_t)(at + 1))) << 8);
}
static void set_line_at(GameState *g, uint16_t at, uint16_t v)
{
    wram_write8(g, in_bank(at), (uint8_t)v);
    wram_write8(g, in_bank((uint16_t)(at + 1)), (uint8_t)(v >> 8));
}

/* Copy `bytes` bytes on from `from` to `to` a byte at a time, so where `to`
 * is just past `from` the first bytes repeat down the run. */
static void repeat_forward(GameState *g, uint16_t from, uint16_t to, uint16_t bytes)
{
    for (uint16_t i = 0; i < bytes; i++)
        wram_write8(g, in_bank((uint16_t)(to + i)), wram_read8(g, in_bank((uint16_t)(from + i))));
}

/* The address of line `line` of the table at `table`. */
static uint16_t line_of(uint16_t table, uint16_t line) { return (uint16_t)(table + 2 * line); }

/* A window line with nothing open. */
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

/* The squashed iris's working lines, for the original's cells. */
static void leave_squashed_cells(GameState *g, uint16_t centre, uint16_t half)
{
    set_menu_object_scratch_iris_lines_left(g, 0);
    set_menu_object_scratch_iris_half(g, half);
    set_menu_object_scratch_iris_upper_from(g, (uint16_t)(centre - 4));
    set_menu_object_scratch_iris_upper_to(g, (uint16_t)(centre - 2));
    set_menu_object_scratch_iris_lower_from(g, (uint16_t)(centre + 2));
    set_menu_object_scratch_iris_lower_to(g, centre);
}

// $C6:E368, $C6:E391
/* An iris squashed to half its height, in the table this frame's count
 * picks: the circle drawn at the object with its radius, then each half
 * rebuilt from every other line of itself and the lines past it closed. */
static void squashed_iris(GameState *g, MenuObject o, uint8_t slot)
{
    uint16_t frames = (uint16_t)(menu_object_slots_iris_frames(g, o) + 1);
    set_menu_object_slots_iris_frames(g, o, frames);
    uint16_t table = menu_line_values(slot, frames);
    menu_show_line_values(g, slot, table);
    set_up_circle(g, o, table, false);

    uint16_t centre = line_of(table, menu_pixel(menu_object_place(g, o).y));
    uint16_t half = menu_object_slots_iris_radius(g, o) >> 1;
    uint16_t from = (uint16_t)(centre - 4), to = (uint16_t)(centre - 2);
    for (uint16_t i = 0; i < half; i++, from -= 4, to -= 2)
        set_line_at(g, to, line_at(g, from));
    for (uint16_t i = 0; i <= half; i++, to -= 2)
        set_line_at(g, to, CLOSED_LINE);
    from = (uint16_t)(centre + 2);
    to = centre;
    for (uint16_t i = 0; i < half; i++, from += 4, to += 2)
        set_line_at(g, to, line_at(g, from));
    for (uint16_t i = 0; i <= half; i++, to += 2)
        set_line_at(g, to, CLOSED_LINE);
    ORIGINAL_EFFECT(leave_squashed_cells(g, centre, half));
}

// $C6:E498, $C6:E4BE
/* An iris stretched into a capsule, in the table this frame's count picks.
 * Returns where the gap's filling ended, which the original leaves in X. */
static uint16_t capsule_iris(GameState *g, MenuObject o, uint8_t slot)
{
    uint16_t frames = menu_object_slots_capsule_frames(g, o);
    uint16_t table = menu_line_values(slot, frames);
    menu_show_line_values(g, slot, table);
    set_menu_object_slots_capsule_frames(g, o, (uint16_t)(frames + 1));
    set_up_circle(g, o, table, true);

    uint16_t stretch = menu_object_slots_iris_stretch(g, o);
    if (menu_object_slots_iris_radius(g, o) == 0) {
        /* A bar: the object's X as both edges, down `stretch` lines. */
        MenuPlace place = menu_object_place(g, o);
        uint16_t row = line_of(table, menu_pixel(place.y));
        ORIGINAL_EFFECT(set_menu_object_scratch_iris_centre(g, stretch));
        set_line_at(g, row, menu_pixel(place.x));
        uint16_t bytes = (uint16_t)(2 * stretch - 1);
        repeat_forward(g, row, (uint16_t)(row + 1), bytes);
        return (uint16_t)(row + bytes);
    }
    /* The upper half, mirrored below the centre `stretch` lines down, and the
     * centre line repeated over the gap. */
    uint16_t centre = line_of(table, (uint16_t)(scene_circle_center_y(g) - 1));
    uint16_t up = centre, down = line_of(centre, stretch);
    for (uint16_t i = 0; i < scene_circle_radius(g); i++, up -= 2, down += 2)
        set_line_at(g, down, line_at(g, up));
    ORIGINAL_EFFECT(set_menu_object_scratch_iris_centre(g, centre));
    ORIGINAL_EFFECT(set_menu_object_scratch_iris_lines_left(g, 0));
    uint16_t bytes = (uint16_t)(2 * stretch - 2);
    repeat_forward(g, centre, (uint16_t)(centre + 2), bytes);
    return (uint16_t)(centre + bytes);
}

/* ---- the entries ----------------------------------------------------------------------------- */

/* The scripts' routines run on the mode's page, their object a slot. The
 * starters reach the channel through the data bank, so it is on the I/O
 * registers; the irises reach their object through it, so it is on low RAM,
 * and the game draws them on the screen: a centre line and a radius under
 * 256, a squashed iris of a radius of 2 or more (a smaller one would copy the
 * whole bank), and a capsule stretched 2 lines or more, whose centre is below
 * the top line. */
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

static bool iris_can_draw(Snes *s, bool capsule)
{
    if (m_direct_page(s) != MODE_PAGE || !m_bank_reaches_low_ram(s) || !running_a_slot(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    MenuObject o = menu_object_running(g);
    uint16_t y = menu_pixel(menu_object_place(g, o).y), radius = menu_object_slots_iris_radius(g, o);
    uint16_t stretch = menu_object_slots_iris_stretch(g, o);
    bool ok = y < SCREEN_LINES && radius < SCREEN_LINES
              && (capsule ? stretch >= 2 && stretch < SCREEN_LINES && y >= 1 : radius >= 2);
    vars_peek(false);
    return ok;
}

bool menu_squashed_iris_can_draw(Snes *s) { return iris_can_draw(s, false); }
bool menu_capsule_iris_can_draw(Snes *s) { return iris_can_draw(s, true); }

// $C6:E568
/* Move slot 1's staged values on by four lines. The carry is the second
 * address's running past the end of the bank. */
void menu_object_advance_line_tables(Snes *s)
{
    enum { FOUR_LINES = 8 };
    GameState *g = state_of(s);
    uint16_t first = menu_object_hdma_pointers(g, 0), second = menu_object_hdma_pointers(g, 1);
    set_menu_object_hdma_pointers(g, 0, (uint16_t)(first + FOUR_LINES));
    set_menu_object_hdma_pointers(g, 1, (uint16_t)(second + FOUR_LINES));
    m_set_flag(s, CPU_FLAG_C, second > 0xFFFF - FOUR_LINES);
    m_rep(s, CPU_FLAG_M);
    m_rtl(s);
}

/* The starters' copies each program the channel at the same distance from
 * their start. */
enum { CHANNEL_WRITE_OFFSET = 0x12 };
static uint32_t channel_write_site(const Snes *s) { return ((uint32_t)s->cpu.pb << 16 | s->cpu.pc) + CHANNEL_WRITE_OFFSET; }

// $C6:E000, $C6:E318, $C6:E448
/* Window 1's edges line by line, on slot 1. */
void menu_object_window1_lines(Snes *s)
{
    menu_start_line_effect(state_of(s), m_platform(s), channel_write_site(s), 1, VID_LINE_WINDOW1);
    m_rtl(s);
}

// $C6:E340, $C6:E470
/* Window 2's edges line by line, on slot 2. */
void menu_object_window2_lines(Snes *s)
{
    menu_start_line_effect(state_of(s), m_platform(s), channel_write_site(s), 2, VID_LINE_WINDOW2);
    m_rtl(s);
}

// $C6:E595
/* The first plane's vertical scroll line by line, on slot 3, the object's
 * count starting from nothing. X comes back as the object. */
void menu_object_bg1_scroll_lines(Snes *s)
{
    GameState *g = state_of(s);
    menu_start_line_effect(g, m_platform(s), channel_write_site(s), 3, VID_LINE_SCROLL_BG1_V);
    set_menu_object_slots_phase(g, menu_object_running(g), 0);
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

static void capsule_entry(Snes *s, uint8_t slot)
{
    GameState *g = state_of(s);
    MenuObject o = menu_object_running(g);
    uint16_t end = capsule_iris(g, o, slot);
    iris_widths(s);
    s->cpu.x = end;
    m_set_flag(s, CPU_FLAG_C, menu_object_slots_iris_radius(g, o) != 0);
    m_rtl(s);
}

void menu_object_squashed_iris_1(Snes *s) { squashed_entry(s, 1); }
void menu_object_squashed_iris_2(Snes *s) { squashed_entry(s, 2); }
void menu_object_capsule_iris_1(Snes *s) { capsule_entry(s, 1); }
void menu_object_capsule_iris_2(Snes *s) { capsule_entry(s, 2); }
