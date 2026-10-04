/* The healing and its rising numbers on the original's machine: the
 * registered entries, the task the numbers rise in, and what they leave the
 * original's code.
 *
 * The numbers rise in a task of the menus' own, which the showing starts and
 * then waits out, a frame at a time, through the original's task switch
 * ($C2:821E); the task raises each kind in a routine of its own ($C2:9047),
 * which plays the kind's sound and yields between its frames, and ends
 * through the scheduler's end of a task ($C2:8216), where the port stops. */
#include "menu/healing.h"

#include "charge.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/item_screen.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/members.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"

/* The original's task switch, which waits for the next frame, and the end
 * of a task, which the scheduler goes on from; neither reads the
 * registers. */
enum { YIELD = 0xC2821E, END_OF_A_TASK = 0xC28216 };
enum { READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

/* Where the numbers' task starts. */
enum { RAISE_AMOUNTS = 0x9016 };

/* ---- The healing --------------------------------------------------------------------------------- */

/* The healing goes through the members it heals as the member the menus
 * work on, and leaves that one past the last, where the screen's next
 * loading of a member takes it up. */
static void move_past_the_members_healed(GameState *g)
{
    bool every = (menu_use_effect(g) & MENU_USE_FOR_EVERY_MEMBER) != 0;
    menu_work_on_member(g, every ? menu_active_members(g) : (uint8_t)(menu_use_member(g) + 1));
}

/* For the original's code and shadow mode: the healing counts the members
 * it heals down in the menus' first cell. */
static void leave_members_healed(GameState *g) { set_menu_scratch_restoring_members_left(g, 0); }

// $C2:8E2D
void menu_heal_entry(Snes *s)
{
    GameState *g = state_of(s);
    menu_heal(g);
    move_past_the_members_healed(g);
    ORIGINAL_EFFECT(leave_members_healed(g));
    m_rts(s);
}

/* ---- Showing the amounts ------------------------------------------------------------------------- */

/* What the showing hands back in A: the task's state, none, as its wait
 * read it, a word or a byte; a byte keeps the high byte of the task's start
 * it was loaded with. */
static void leave_waited(Snes *s)
{
    s->cpu.a = m_flag(s, CPU_FLAG_M) ? (uint16_t)(RAISE_AMOUNTS & 0xFF00) : 0;
}

// $C2:8F6C
/* The amounts made ready and their task started; then the wait for it to
 * finish, a frame at a time. */
void menu_show_amounts(Snes *s)
{
    GameState *g = state_of(s);
    menu_ready_the_amounts(g);
    menu_start_task(g, MENU_AMOUNTS_TASK, RAISE_AMOUNTS);
    do
        if (!menu_call(s, 0xC28FC3, YIELD, READS_NOTHING))
            return;
    while (menu_tasks_state(g, MENU_AMOUNTS_TASK) != 0);
    ORIGINAL_EFFECT(leave_waited(s));
    m_rts(s);
}

/* The healing's numbers' colours, which the showing uses. */
static void colour_the_amounts(GameState *g)
{
    for (unsigned i = 0; i < MENU_AMOUNT_COLOURS; i++)
        set_menu_palette(g, MENU_FIRST_AMOUNT_COLOUR + i, menu_rom_colour(g, MENU_ROM_AMOUNT_COLOURS, i));
    ORIGINAL_EFFECT(charge_block_move(g, 2 * MENU_AMOUNT_COLOURS));
}

// $C2:8F55
/* The healing, then its numbers in their colours, waited out. */
void menu_heal_and_show(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, false, false);
    TIMED_AS(g, 0xC28E2D, menu_heal(g));
    move_past_the_members_healed(g);
    ORIGINAL_EFFECT(leave_members_healed(g));
    colour_the_amounts(g);
    s->cpu.db = MENU_DATA_BANK;   /* as the colours' block move leaves it */
    if (!menu_run(s, 0xC28F67, 0xC28F6C, menu_show_amounts))
        return;
    menu_restore_p(s);
    m_rts(s);
}

/* ---- The task ------------------------------------------------------------------------------------ */

/* For the original's code and shadow mode: the task shifts each kind's bit
 * out of the kinds to show as it takes it. */
static void leave_kinds_left(GameState *g, uint8_t kinds) { set_menu_amounts_kinds(g, kinds); }

// $C2:9016
/* Each kind that rises, its own way, then the task's end. */
void menu_raise_amounts(Snes *s)
{
    static const uint32_t sites[MENU_AMOUNT_KINDS] = { 0xC29029, 0xC29041 };
    GameState *g = state_of(s);
    uint8_t kinds = (uint8_t)menu_amounts_kinds(g);
    for (unsigned k = 0; k < MENU_AMOUNT_KINDS; k++) {
        ORIGINAL_EFFECT(leave_kinds_left(g, (uint8_t)(kinds >> (k + 1))));
        if (!(kinds & 1u << k))
            continue;
        set_menu_amounts_step(g, menu_amounts_path_start((MenuAmountKind)k));
        set_menu_amounts_last_digit(g, (uint8_t)(menu_amounts_digits((MenuAmountKind)k) - 1));
        set_menu_amounts_from(g, (uint8_t)(2 * menu_amounts_shown_from((MenuAmountKind)k)));
        menu_set_widths(s, true, true);
        m_set_a8(s, menu_amount_sound((MenuAmountKind)k));
        if (!menu_run(s, sites[k], 0xC29047, menu_raise_amount_kind))
            return;
    }
    menu_set_widths(s, true, true);
    m_stop_at(s, END_OF_A_TASK, READS_NOTHING);
}

/* What a frame leaves in the menus' cells: where its loop over the members
 * got to, the height, the last member's number as it read it, and of the
 * last number it drew, how far it got along its digits (where it drew none
 * they stay as they were). */
enum { NOT_A_NUMBER = 0x8000, PAST_THE_UNITS = 0xFF, HALF_A_DIGIT = 4 };

static void leave_a_frame(GameState *g, uint8_t from, uint8_t last_digit, uint8_t height)
{
    unsigned listed = menu_amounts_listed(g);
    set_menu_amounts_place(g, (uint16_t)(2 * listed));
    set_menu_amounts_left(g, 0);
    set_menu_scratch_amount_height(g, height);
    set_menu_text_work_number_low(g, menu_heal_shown(g, from / 2 + listed - 1));
    for (unsigned i = listed; i-- > 0;) {
        uint16_t number = menu_heal_shown(g, from / 2 + i);
        if (number & NOT_A_NUMBER)
            continue;
        unsigned first = last_digit;
        while (first > 0 && !(number >> (4 * first) & 0x0F))
            first--;
        set_menu_scratch_amount_digit(g, PAST_THE_UNITS);
        set_menu_scratch_amount_offset(g, (uint8_t)(HALF_A_DIGIT * (first + 1)));
        set_menu_scratch_amount_drawn(g, (uint8_t)(first + 1));
        return;
    }
}

// $C2:9047
/* A kind's numbers rising: its sound, then a frame at a time along its
 * path, yielding between frames, until the path ends. The kind comes in the
 * menus' cells: where its path is, its last digit and where its amounts
 * start. */
void menu_raise_amount_kind(Snes *s)
{
    GameState *g = state_of(s);
    if (!menu_run(s, 0xC29047, 0xC2EAF9, menu_send_sound_on_other_voices))
        return;
    uint8_t step = menu_amounts_step(g), last_digit = menu_amounts_last_digit(g), from = menu_amounts_from(g);
    for (;;) {
        menu_set_widths(s, true, true);
        set_menu_amounts_step(g, (uint8_t)(step + 1));
        if (!menu_raise_numbers_a_frame(g, (uint8_t)(from / 2), (uint8_t)(last_digit + 1), step)) {
            m_set_a8(s, menu_amount_height(g, step));
            m_rts(s);
            return;
        }
        ORIGINAL_EFFECT(leave_a_frame(g, from, last_digit, menu_amount_height(g, step)));
        step++;
        menu_set_widths(s, false, true);
        if (!menu_call(s, 0xC2904C, YIELD, READS_NOTHING))
            return;
    }
}

/* ---- The guards ------------------------------------------------------------------------------------ */

/* The healing runs on the menus' page with the data bank on work RAM's, for
 * at most the three active members, each loaded from a character's record. */
bool menu_healing_runs(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    GameState *g = state_of(s);
    bool every = (menu_use_effect(g) & MENU_USE_FOR_EVERY_MEMBER) != 0;
    bool ok = every ? menu_active_members(g) <= MP_SHOWN : menu_use_member(g) < MP_SHOWN;
    vars_peek(false);
    return ok;
}

/* The showing's numbers are for at most the three active members; and the
 * task is free to start. */
bool menu_amounts_can_be_shown(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = menu_active_members(state_of(s)) <= MP_SHOWN;
    vars_peek(false);
    return ok;
}

/* A kind's rise: one of the two kinds as the task sets them up, for the
 * members the showing listed. */
bool menu_amounts_can_rise(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    GameState *g = state_of(s);
    bool ok = false;
    for (unsigned k = 0; k < MENU_AMOUNT_KINDS; k++)
        ok = ok || (menu_amounts_from(g) == 2 * menu_amounts_shown_from((MenuAmountKind)k)
                    && menu_amounts_last_digit(g) == menu_amounts_digits((MenuAmountKind)k) - 1);
    ok = ok && menu_amounts_listed(g) >= 1 && menu_amounts_listed(g) <= MP_SHOWN;
    vars_peek(false);
    return ok;
}
