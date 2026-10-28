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

/* The end of a task, which the scheduler goes on from, and which reads none
 * of the registers. */
enum { END_OF_A_TASK = 0xC28216 };
enum { READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };


/* ---- The healing --------------------------------------------------------------------------------- */

// $C2:8E2D
void menu_heal_entry(Snes *s)
{
    GameState *g = state_of(s);
    menu_heal(g);
    m_rts(s);
}

/* ---- What the sequences ask, on the machine ----------------------------------------------------- */

/* What the showing hands back in A: the task's state, none, as its wait
 * read it, a word or a byte; a byte keeps the high byte of the task's start
 * it was loaded with. */
static void leave_waited(Snes *s)
{
    s->cpu.a = m_flag(s, CPU_FLAG_M) ? (uint16_t)(MENU_RAISE_AMOUNTS_TASK_START & 0xFF00) : 0;
}

/* For the original's code and shadow mode: the task shifts each kind's bit
 * out of the kinds to show as it takes it. */
static void leave_kinds_left(GameState *g, uint8_t kinds) { set_menu_amounts_kinds(g, kinds); }

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

static const struct {
    uint32_t site;
} WAITS_AT[] = { [HEALING_AT_THE_AMOUNTS_SHOWN] = { 0xC28FC3 }, [HEALING_AT_THE_RISING_WAIT] = { 0xC2904C } };

static bool wait_on_the_machine(void *ctx, HealingPlace at) { return menu_wait_a_frame_from(ctx, WAITS_AT[at].site); }

/* The colours' block move leaves the data bank on the menus' data; the
 * rise's frames run with an 8-bit accumulator, its waits with a 16-bit one. */
static void registers_on_the_machine(void *ctx, HealingPlace at)
{
    Snes *s = ctx;
    switch (at) {
    case HEALING_AT_THE_COLOURS: s->cpu.db = MENU_DATA_BANK; break;
    case HEALING_AT_THE_RISING_WAIT: menu_set_widths(s, false, true); break;
    default: menu_set_widths(s, true, true); break;
    }
}

/* Each kind rises from its own call; its stretches are the kind's, as the
 * original's call of it is. The kind's sound goes in A. */
static const uint32_t KIND_SITES[MENU_AMOUNT_KINDS] = { 0xC29029, 0xC29041 };

static bool run_on_the_machine(void *ctx, HealingRoutine r, uint8_t value)
{
    Snes *s = ctx;
    bool back = false;
    switch (r) {
    case HEALING_SHOWS_THE_AMOUNTS: return menu_run(s, 0xC28F67, 0xC28F6C, menu_show_amounts_entry);
    case HEALING_RAISES_A_KIND:
        m_set_a8(s, menu_amount_sound((MenuAmountKind)value));
        NATIVE_TIME_AS_SIBLING(s, 0xC29047, back = menu_run(s, KIND_SITES[value], 0xC29047, menu_raise_amount_kind));
        return back;
    case HEALING_SENDS_THE_KIND_S_SOUND: return menu_run(s, 0xC29047, 0xC2EAF9, menu_send_sound_on_other_voices);
    }
    return false;
}

static void note_on_the_machine(void *ctx, HealingNote note, uint8_t value)
{
    Snes *s = ctx;
    GameState *g = state_of(s);
    switch (note) {
    case HEALING_NOTE_WAITED: ORIGINAL_EFFECT(leave_waited(s)); break;
    case HEALING_NOTE_KINDS_LEFT: ORIGINAL_EFFECT(leave_kinds_left(g, value)); break;
    case HEALING_NOTE_A_FRAME_RISEN:
        ORIGINAL_EFFECT(leave_a_frame(g, menu_amounts_from(g), menu_amounts_last_digit(g), menu_amount_height(g, value)));
        break;
    case HEALING_NOTE_RISEN: m_set_a8(s, menu_amount_height(g, value)); break;
    }
}

/* The task ends through the scheduler's end of a task. */
static bool ends_on_the_machine(void *ctx)
{
    m_stop_at(ctx, END_OF_A_TASK, READS_NOTHING);
    return false;
}

static const HealingSteps on_the_machine = {
    wait_on_the_machine, registers_on_the_machine, run_on_the_machine, note_on_the_machine, ends_on_the_machine,
};

// $C2:8F6C
void menu_show_amounts_entry(Snes *s)
{
    if (menu_show_amounts_with(state_of(s), &on_the_machine, s))
        m_rts(s);
}

// $C2:8F55
void menu_heal_and_show_entry(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, false, false);
    if (!menu_heal_and_show_with(state_of(s), &on_the_machine, s))
        return;
    menu_restore_p(s);
    m_rts(s);
}

// $C2:9016
void menu_raise_amounts(Snes *s) { (void)menu_raise_amounts_with(state_of(s), &on_the_machine, s); }

// $C2:9047
void menu_raise_amount_kind(Snes *s)
{
    if (menu_raise_amount_kind_with(state_of(s), &on_the_machine, s))
        m_rts(s);
}

/* ---- The guards ------------------------------------------------------------------------------------ */

/* The healing runs on the menus' page with the data bank on work RAM's, for
 * at most the three active members, each loaded from a character's record. */
bool menu_healing_runs(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = menu_healing_can_run(state_of(s));
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
    bool ok = menu_amounts_can_show(state_of(s));
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
    bool ok = menu_amounts_kind_can_rise(state_of(s));
    vars_peek(false);
    return ok;
}
