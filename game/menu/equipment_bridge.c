/* The equipment screen on the original's machine: its registered entries,
 * the bridges that run what game/menu/equipment.c decides between the calls
 * to what is still the original's, and their guards.
 *
 * Each step reads the cursor, asks equipment.c what the frame's input does,
 * and carries that out: the sounds, the member's card and the member list's
 * slide are siblings, run as the original's JSRs reach them (menu_run; the
 * slide where its own guard holds, menu_run_or_call); the trying on of what
 * is no item of the game's is the original's, called out to, and so are the
 * low-HP marks where theirs can't be worked out as C; the member list's scroll
 * bar, the marks otherwise and the list's frame are C, called as C; a step
 * that waits for the next frame
 * waits in the menus' task switch, parked until its task goes on
 * (menu_wait_a_frame_from); where a step jumps on to another routine (the
 * member list's move, the slots' pointing, the list's step, back to the
 * main menu), it stops there, and the runtime runs that routine, a port or
 * the original's.
 *
 * Shadow mode compares the machine at every call out, the flags a sibling
 * saves as it starts (on the stack under its own calls out) among it. So
 * after each decision a bridge leaves the flags the original's own tests
 * left, worked out from the state (the leaves at the end of the file):
 * scaffolding, which the native build compiles out. */
#include "menu/equipment.h"

#include "charge.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/description.h"
#include "menu/display.h"
#include "menu/figures.h"
#include "menu/held_cursors.h"
#include "menu/lists.h"
#include "menu/main_menu.h"
#include "menu/members.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/text.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"

/* ---- The calls out ----------------------------------------------------------------------------- */

/* The original's routines the steps call, by what they do. */
enum {
    LOW_HP_MARKS = 0xC2813C,         /* each character's low-HP mark worked out */
    TRY_ON = 0xC2A5C3,               /* the list's item on the cursor tried on (equipment.c), where it is no item of the game's */
};

/* Where a step jumps on to: the member list's move and the slots' pointing,
 * the screen's set-up, the card, back to the members, on to the list (ports
 * of these files), back to the main menu, the refusal's sound and the cursor
 * sound as a step's last. */
enum {
    POINT_AT_MEMBER = 0xC29D17, POINT_AT_SLOT = 0xC29ECD, SET_UP_SCREEN = 0xC2A11A, MEMBER_CARD = 0xC2A22F,
    MAIN_MENU = 0xC29A98, BACK_TO_MEMBERS = 0xC2A2CD, ON_TO_THE_LIST = 0xC2A41A, REFUSAL_SOUND = 0xC2EACC,
    CURSOR_SOUND = 0xC2EAC2,
};

/* What they read (tools/dontcare_check.py checks each): most nothing but the
 * page and the data bank; the refusal's sound A's high byte, and the cursor
 * sound as a step's last A and the carry. */
enum {
    READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_A_HIGH = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_A_AND_CARRY = DC_X | DC_Y | DC_N | DC_V | DC_Z,
};

static bool call(Snes *s, uint32_t site, uint32_t routine) { return menu_call(s, site, routine, READS_NOTHING); }
static bool tick(Snes *s, uint32_t site) { return menu_run(s, site, 0xC2EAC2, menu_play_cursor_sound); }

/* A sound by its number, loaded into A as the original's call does (the
 * load's flags are the P the sound saves). */
static bool play_sound(Snes *s, uint32_t site, uint8_t sound)
{
    m_set_a8(s, sound);
    ORIGINAL_EFFECT(menu_leave_load(s, sound));
    return menu_run(s, site, 0xC2EABA, menu_play_sound);
}

static void leave_member_step_tested(Snes *s);
static void leave_slot_step_tested(Snes *s, SlotChoice c);
static void leave_buttons_tested(Snes *s);
static void leave_inventory_changed(Snes *s);
static void leave_cursor_compared(Snes *s);
static void leave_member_pointed(Snes *s, uint8_t pointed_before);
static void leave_member_slide_started(Snes *s, MemberListSlide slide);
static void leave_slot_kept(Snes *s, uint8_t kept_before);
static void leave_list_sum(Snes *s, uint8_t shown_before);
static void leave_label_copied(Snes *s);
static void leave_list_drawn(Snes *s);
static void leave_list_opened(Snes *s, bool typing_started);
static void leave_member_switched(Snes *s);

static void leave_page_turned(Snes *s);
static void leave_row_found(Snes *s);
static void leave_row_noted(Snes *s, uint8_t row);
static void leave_list_rows_queued(Snes *s);

/* ---- What the sequences ask, on the machine ----------------------------------------------------- */

/* The original's call at each place the sequences tick or wait from. */
static const uint32_t TICKS_AT[] = {
    [EQUIPMENT_AT_THE_OPENING] = 0xC29C31,        [EQUIPMENT_AT_THE_MEMBER_CHOSEN] = 0xC29C52,
    [EQUIPMENT_AT_THE_SLOTS_DRAWN] = 0xC29C5E,    [EQUIPMENT_AT_THE_MEMBER_POINTED] = 0xC29D54,
    [EQUIPMENT_AT_THE_SLOTS_LEFT] = 0xC29C9C,     [EQUIPMENT_AT_THE_SLOT_CHOSEN] = 0xC29C90,
    [EQUIPMENT_AT_THE_SLOT_MOVED] = 0xC29EE0,     [EQUIPMENT_AT_THE_MEMBER_SWITCHED] = 0xC2A043,
    [EQUIPMENT_AT_THE_ITEM_LEFT] = 0xC29CE4,      [EQUIPMENT_AT_THE_ITEM_POINTED] = 0xC29CBD,
    [EQUIPMENT_AT_THE_PAGE_TURNED] = 0xC29F26,    [EQUIPMENT_AT_THE_ROW_BROUGHT_IN] = 0xC29F8D,
};
static const struct {
    uint32_t site;
} WAITS_AT[] = {
    [EQUIPMENT_AT_THE_SLOTS_DRAWN] = { 0xC29C5B },          [EQUIPMENT_AT_THE_SLOTS_SLIDING] = { 0xC2A785 },
    [EQUIPMENT_AT_THE_SLOT_POINTED] = { 0xC29EFD },         [EQUIPMENT_AT_THE_MEMBER_SWITCHED] = { 0xC2A04C },
    [EQUIPMENT_AT_THE_SLOTS_GOING] = { 0xC2A85F },          [EQUIPMENT_AT_THE_SLOTS_GONE] = { 0xC2A88F },
    [EQUIPMENT_AT_THE_MEMBERS_SLIDING_BACK] = { 0xC2A945 }, [EQUIPMENT_AT_THE_LIST_SLIDING_IN] = { 0xC2A511 },
    [EQUIPMENT_AT_THE_LIST_SLIDING_OUT] = { 0xC2A6DC },     [EQUIPMENT_AT_THE_SLOTS_REDRAWN] = { 0xC2A661 },
    [EQUIPMENT_AT_THE_LIST_ROWS_SLIDING] = { 0xC29FB4 },     [EQUIPMENT_AT_THE_MEMBERS_SLIDING] = { 0xC29E3C },
};

static void read_cursor_on_the_machine(void *ctx) { menu_step_reads_the_cursor(ctx); }
static bool tick_on_the_machine(void *ctx, EquipmentPlace at) { return tick(ctx, TICKS_AT[at]); }
static bool wait_on_the_machine(void *ctx, EquipmentPlace at) { return menu_wait_a_frame_from(ctx, WAITS_AT[at].site); }

/* The widths the original's code sets at those places, and where a routine
 * starts by saving P before it sets them, the P saved. */
static void registers_on_the_machine(void *ctx, EquipmentPlace at)
{
    Snes *s = ctx;
    switch (at) {
    case EQUIPMENT_AT_THE_LIST_BEING_MADE:
        menu_save_p(s);
        menu_set_widths(s, false, false);
        break;
    case EQUIPMENT_AT_THE_LIST_FRAMED: menu_set_widths(s, true, true); break;
    case EQUIPMENT_AT_THE_ITEM_POINTED: menu_set_widths(s, true, m_x8(s)); break;
    case EQUIPMENT_AT_THE_LIST_ROWS_SLIDING: menu_set_widths(s, false, false); break;
    case EQUIPMENT_AT_THE_MEMBER_SLIDE_STARTING:
        menu_save_p(s);
        menu_set_widths(s, false, false);
        break;
    case EQUIPMENT_AT_THE_MEMBER_SLIDE_ENDED: menu_restore_p(s); break;
    default: menu_set_widths(s, true, false); break;
    }
}

/* The sibling routines the sequences reach, each run where the original
 * calls it from. */
static const struct {
    uint32_t site, routine;
    void (*entry)(Snes *s);
} RUNS[] = {
    [EQUIPMENT_SLIDES_MEMBERS] = { 0xC29D23, 0xC29D5B, menu_start_sliding_equipment_members },
    [EQUIPMENT_OPENS_THE_SLOTS_WINDOW] = { 0xC29C55, 0xC2A71E, menu_open_slots_window },
    [EQUIPMENT_SWITCHES_MEMBERS] = { 0xC29ED3, 0xC2A00B, menu_switch_equipment_member_entry },
    [EQUIPMENT_REDRAWS_THE_CARD] = { 0xC2A046, 0xC2A21F, menu_redraw_member_card_entry },
    [EQUIPMENT_REDRAWS_THE_LIST_FOR_THE_SLOT] = { 0xC29F00, 0xC2A45D, menu_redraw_equipment_list },
    [EQUIPMENT_REDRAWS_THE_LIST_ON_THE_SLOTS] = { 0xC2A667, 0xC2A45D, menu_redraw_equipment_list },
    [EQUIPMENT_CLOSES_THE_SLOTS_WINDOW] = { 0xC29C9F, 0xC2A85C, menu_close_slots_window },
    [EQUIPMENT_SLIDES_THE_LIST_IN] = { 0xC2A41D, 0xC2A4AF, menu_slide_equipment_list_in },
    [EQUIPMENT_SLIDES_THE_LIST_OUT] = { 0xC2A664, 0xC2A697, menu_slide_equipment_list_out },
    [EQUIPMENT_MAKES_THE_LIST] = { 0xC2A432, 0xC2A463, menu_make_equipment_list_entry },
    [EQUIPMENT_DRAWS_THE_CARD] = { 0xC2A650, MEMBER_CARD, menu_draw_member_card_entry },
    [EQUIPMENT_GOES_BACK_ON_THE_SLOTS] = { 0xC29CEC, 0xC2A64D, menu_back_to_equipment_slots },
    [EQUIPMENT_POINTS_ALONG_THE_LIST] = { 0xC29CAF, 0xC29F05, menu_point_along_equipment_list_entry },
};

static bool run_on_the_machine(void *ctx, EquipmentRoutine r)
{
    Snes *s = ctx;
    GameState *g = state_of(s);
    switch (r) {
    case EQUIPMENT_WORKS_OUT_LOW_HP_MARKS:
        if (!menu_low_hp_marks_are_workable(s))
            return call(s, 0xC2A653, LOW_HP_MARKS);
        TIMED_AS(g, LOW_HP_MARKS, menu_work_out_low_hp_marks(g));
        return true;
    case EQUIPMENT_PLAYS_THE_PUT_ON_SOUND: return play_sound(s, 0xC29CDE, MENU_EQUIP_PUT_ON_SOUND);
    case EQUIPMENT_FORGETS_THE_WEAPON: TIMED_AS(g, 0xC29CFC, menu_equip_forget_weapon(g)); return true;
    case EQUIPMENT_TRIES_IT_ON: {
        /* As C where the figures can be worked out with the item. */
        vars_peek(true);
        bool tried_on_by_the_port = menu_equip_can_try_on(g);
        vars_peek(false);
        if (!tried_on_by_the_port)
            return call(s, 0xC29CB2, TRY_ON);
        menu_equip_try_on(g, m_platform(s));
        return true;
    }
    case EQUIPMENT_SLIDES_THE_MEMBER_ROWS_UP:
        return menu_run_or_call(s, 0xC29D82, 0xC29DAF, READS_NOTHING, menu_slide_equipment_members);
    case EQUIPMENT_SLIDES_THE_MEMBER_ROWS_DOWN:
        return menu_run_or_call(s, 0xC29DAA, 0xC29DAF, READS_NOTHING, menu_slide_equipment_members);
    case EQUIPMENT_POINTS_ALONG_THE_LIST: return menu_run_any_widths(s, RUNS[r].site, RUNS[r].routine, RUNS[r].entry);
    default: return menu_run(s, RUNS[r].site, RUNS[r].routine, RUNS[r].entry);
    }
}

static void note_on_the_machine(void *ctx, EquipmentNote note, uint8_t value)
{
    Snes *s = ctx;
    (void)value;
    switch (note) {
    case EQUIPMENT_NOTE_MEMBER_STEP_TESTED: ORIGINAL_EFFECT(leave_member_step_tested(s)); break;
    case EQUIPMENT_NOTE_MEMBER_POINTED: ORIGINAL_EFFECT(leave_member_pointed(s, value)); break;
    case EQUIPMENT_NOTE_SLOT_STEP_TESTED: ORIGINAL_EFFECT(leave_slot_step_tested(s, (SlotChoice)value)); break;
    case EQUIPMENT_NOTE_LABEL_COPIED: ORIGINAL_EFFECT(leave_label_copied(s)); break;
    case EQUIPMENT_NOTE_SLOT_KEPT: ORIGINAL_EFFECT(leave_slot_kept(s, value)); break;
    case EQUIPMENT_NOTE_LIST_SUM: ORIGINAL_EFFECT(leave_list_sum(s, value)); break;
    case EQUIPMENT_NOTE_MEMBER_SWITCHED: ORIGINAL_EFFECT(leave_member_switched(s)); break;
    case EQUIPMENT_NOTE_BUTTONS_TESTED: ORIGINAL_EFFECT(leave_buttons_tested(s)); break;
    case EQUIPMENT_NOTE_INVENTORY_CHANGED: ORIGINAL_EFFECT(leave_inventory_changed(s)); break;
    case EQUIPMENT_NOTE_CURSOR_LOADED: ORIGINAL_EFFECT(menu_leave_load(s, menu_cursor_index(state_of(s)))); break;
    case EQUIPMENT_NOTE_CURSOR_COMPARED: ORIGINAL_EFFECT(leave_cursor_compared(s)); break;
    case EQUIPMENT_NOTE_CURSOR_KEPT: ORIGINAL_EFFECT(menu_leave_cursor_kept_on_the_list(s)); break;
    case EQUIPMENT_NOTE_PAGE_TURNED: ORIGINAL_EFFECT(leave_page_turned(s)); break;
    case EQUIPMENT_NOTE_LIST_DRAWN: ORIGINAL_EFFECT(leave_list_drawn(s)); break;
    case EQUIPMENT_NOTE_ROW_NOTED: ORIGINAL_EFFECT(leave_row_noted(s, value)); break;
    case EQUIPMENT_NOTE_ROW_FOUND: ORIGINAL_EFFECT(leave_row_found(s)); break;
    case EQUIPMENT_NOTE_LIST_ROWS_QUEUED: ORIGINAL_EFFECT(leave_list_rows_queued(s)); break;
    case EQUIPMENT_NOTE_LIST_FRAMED: ORIGINAL_EFFECT(menu_leave_the_list_framed(s, menu_equip_choices(state_of(s)))); break;
    case EQUIPMENT_NOTE_LIST_OPENED: ORIGINAL_EFFECT(leave_list_opened(s, value != 0)); break;
    case EQUIPMENT_NOTE_LIST_WINDOW_SHOWN: ORIGINAL_EFFECT(menu_leave_load(s, 0x42)); break;
    case EQUIPMENT_NOTE_LIST_BAR_PLACED: ORIGINAL_EFFECT(menu_leave_load(s, 0x62)); break;
    case EQUIPMENT_NOTE_SLOTS_REDRAWN: ORIGINAL_EFFECT(menu_leave_load(s, 0x2E)); break;
    case EQUIPMENT_NOTE_MEMBER_SLIDE_STARTED: ORIGINAL_EFFECT(leave_member_slide_started(s, (MemberListSlide)value)); break;
    }
}

/* The steps' ends are the original's jumps, which the port stops at; the
 * card and the list's opening put back the P their routines saved first. */
static bool ends_on_the_machine(void *ctx, EquipmentEnd end)
{
    Snes *s = ctx;
    switch (end) {
    case EQUIPMENT_SETS_UP_THE_SCREEN: m_stop_at(s, SET_UP_SCREEN, READS_NOTHING); break;
    case EQUIPMENT_POINTS_AT_A_MEMBER: m_stop_at(s, POINT_AT_MEMBER, READS_NOTHING); break;
    case EQUIPMENT_SHOWS_THE_CARD:
        menu_restore_p(s);
        m_stop_at(s, MEMBER_CARD, READS_NOTHING);
        break;
    case EQUIPMENT_GOES_BACK_TO_THE_MAIN_MENU: m_stop_at(s, MAIN_MENU, READS_NOTHING); break;
    case EQUIPMENT_POINTS_AT_A_SLOT: m_stop_at(s, POINT_AT_SLOT, READS_NOTHING); break;
    case EQUIPMENT_GOES_ON_TO_THE_LIST: m_stop_at(s, ON_TO_THE_LIST, READS_NOTHING); break;
    case EQUIPMENT_REFUSES: m_stop_at_own(s, REFUSAL_SOUND, READS_A_HIGH); break;
    case EQUIPMENT_GOES_BACK_TO_THE_MEMBERS: m_stop_at(s, BACK_TO_MEMBERS, READS_NOTHING); break;
    case EQUIPMENT_TICKS_ON_THE_LIST:
        menu_restore_p(s);
        m_stop_at_own(s, CURSOR_SOUND, READS_A_AND_CARRY);
        break;
    }
    return false;
}

static const EquipmentSteps on_the_machine = {
    read_cursor_on_the_machine, tick_on_the_machine, wait_on_the_machine, registers_on_the_machine,
    run_on_the_machine,         note_on_the_machine, ends_on_the_machine,
};

static void step(Snes *s, bool (*run)(GameState *, Platform *, const EquipmentSteps *, void *))
{
    if (run(state_of(s), m_platform(s), &on_the_machine, s))
        m_rts(s);
}

/* A routine that saves P and sets the widths as it starts, and puts P back
 * as it returns. */
static void saving_p(Snes *s, bool a8, bool x8, bool (*run)(GameState *, Platform *, const EquipmentSteps *, void *))
{
    menu_save_p(s);
    menu_set_widths(s, a8, x8);
    if (!run(state_of(s), m_platform(s), &on_the_machine, s))
        return;
    menu_restore_p(s);
    m_rts(s);
}

/* ---- Opening the screen ------------------------------------------------------------------------ */

// $C2:9C31
void menu_open_equipment_entry(Snes *s) { step(s, menu_open_equipment_with); }

// $C2:A11A
/* The screen set up; it returns for the step. */
void menu_set_up_equipment(Snes *s)
{
    menu_save_p(s);
    menu_equip_set_up(state_of(s), m_platform(s));
    menu_set_widths(s, true, false);
    menu_restore_p(s);
    m_stop_dont_care(s, 0xC2A16F, READS_NOTHING);
    m_rts(s);
}

/* ---- Choosing a member ------------------------------------------------------------------------- */

// $C2:9C39
void menu_choose_equipment_member_entry(Snes *s) { step(s, menu_choose_equipment_member_with); }

// $C2:9D17
/* It saves P as it starts. */
void menu_point_at_equipment_member(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, true);
    step(s, menu_point_at_equipment_member_with);
}

// $C2:9D5B
void menu_start_sliding_equipment_members(Snes *s) { step(s, menu_start_sliding_equipment_members_with); }

// $C2:9DAF
void menu_slide_equipment_members(Snes *s) { step(s, menu_slide_equipment_members_with); }

/* ---- Choosing a slot --------------------------------------------------------------------------- */

// $C2:9C70
void menu_choose_equipment_slot_entry(Snes *s) { step(s, menu_choose_equipment_slot_with); }

// $C2:9ECD
/* It saves P as it starts, and goes on past its last call as its caller's
 * own code. */
void menu_point_at_equipment_slot(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    if (!menu_point_at_equipment_slot_with(state_of(s), m_platform(s), &on_the_machine, s))
        return;
    menu_restore_p(s);
    m_stop_dont_care(s, 0xC29F04, READS_NOTHING);
    m_rts(s);
}

// $C2:A00B
void menu_switch_equipment_member_entry(Snes *s) { saving_p(s, true, true, menu_switch_equipment_member_with); }

/* ---- Choosing an item -------------------------------------------------------------------------- */

// $C2:9CA5
void menu_choose_equipment_item_entry(Snes *s) { step(s, menu_choose_equipment_item_with); }

// $C2:9F05
/* The slide leaves a 16-bit accumulator. */
void menu_point_along_equipment_list_entry(Snes *s)
{
    menu_set_widths(s, true, m_x8(s));
    step(s, menu_point_along_equipment_list_with);
}

// $C2:9FDE
/* It leaves a 16-bit accumulator. */
void menu_move_equipment_list_rows(Snes *s)
{
    menu_equip_move_list_rows(state_of(s));
    menu_set_widths(s, false, m_x8(s));
    m_rts(s);
}

// $C2:9CFC
void menu_forget_weapon(Snes *s)
{
    menu_equip_forget_weapon(state_of(s));
    m_rts(s);
}

/* ---- The main menu's panel --------------------------------------------------------------------- */

// $C2:A051
/* The panel: its window and tables, a frame, the time and the rows, a
 * frame, the colour math and the card, then dimmed. */
void menu_show_equipment_panel(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, true);
    MainMenuOnTheMachine m = { s, 0, 0 };
    if (!menu_show_equipment_panel_with(state_of(s), m_platform(s), &menu_main_menu_on_the_machine, &m))
        return;
    menu_restore_p(s);
    m_rts(s);
}

// $C2:A0E7
/* The colours dimmed into the palette where Y points. */
void menu_dim_panel_colours(Snes *s)
{
    menu_equip_dim_colours(state_of(s), (unsigned)(s->cpu.y - RAM_ABS(RAM_MENU_PALETTE)) / 2u);
    m_rts(s);
}

// $C2:A101
/* The panel's colours put back as the main menu leaves the equipment, and
 * the party's items with them. */
void menu_show_panel_colours(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    menu_equip_panel_put_away(g);
    menu_restore_p(s);
    m_rts(s);
}

/* ---- The windows ------------------------------------------------------------------------------- */

// $C2:A71E
void menu_open_slots_window(Snes *s) { saving_p(s, false, false, menu_open_the_slots_window_with); }

// $C2:A85C
void menu_close_slots_window(Snes *s) { saving_p(s, false, false, menu_close_the_slots_window_with); }

// $C2:A2CD
/* Back on the members, the member scroll bar set up again between. */
void menu_back_to_equipment_members(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    menu_equip_back_to_the_members(state_of(s), m_platform(s));
    menu_restore_p(s);
    m_rts(s);
}

// $C2:A4AF
void menu_slide_equipment_list_in(Snes *s) { saving_p(s, false, false, menu_slide_the_equipment_list_in_with); }

// $C2:A697
void menu_slide_equipment_list_out(Snes *s) { saving_p(s, false, false, menu_slide_the_equipment_list_out_with); }

/* The list's making saves P as it starts (its first step), and puts it
 * back. */
static void made(Snes *s, bool (*run)(GameState *, Platform *, const EquipmentSteps *, void *))
{
    if (!run(state_of(s), m_platform(s), &on_the_machine, s))
        return;
    menu_restore_p(s);
    m_rts(s);
}

// $C2:A463
void menu_make_equipment_list_entry(Snes *s) { made(s, menu_make_the_equipment_list_with); }

// $C2:A45D
void menu_redraw_equipment_list(Snes *s) { made(s, menu_redraw_the_equipment_list_with); }

// $C2:A41A
/* It saves P as it starts; its end puts it back. */
void menu_open_equipment_list(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    step(s, menu_open_the_equipment_list_with);
}

// $C2:A64D
void menu_back_to_equipment_slots(Snes *s) { saving_p(s, false, false, menu_back_on_the_equipment_slots_with); }

// $C2:A321
void menu_show_member_slots(Snes *s)
{
    menu_equip_show_slots_of_member(state_of(s), m_platform(s));
    m_rts(s);
}

// $C2:A38B
/* It leaves what its label's copy leaves. */
void menu_take_equipment_slot(Snes *s)
{
    menu_equip_take_slot(state_of(s));
    ORIGINAL_EFFECT(leave_label_copied(s));
    m_rts(s);
}

// $C2:A3E2
void menu_mark_equipment_slots(Snes *s)
{
    menu_equip_mark_slots(state_of(s));
    m_rts(s);
}

// $C2:A569
/* It leaves the pen past the list in A. */
void menu_draw_equipment_list(Snes *s)
{
    menu_equip_draw_list(state_of(s));
    ORIGINAL_EFFECT(leave_list_drawn(s));
    m_rts(s);
}

// $C2:A59A
/* A row at X. */
void menu_draw_equipment_list_row(Snes *s)
{
    menu_equip_draw_list_row(state_of(s), s->cpu.x);
    m_rts(s);
}

// $C2:A6F0
/* What the member has on, from X; it leaves the row past the last in A. */
enum { EQUIPPED_ROWS_BYTES = 4 * 0x80 };

static void leave_equipped_drawn(Snes *s, uint16_t at) { s->cpu.a = (uint16_t)(at + EQUIPPED_ROWS_BYTES); }

void menu_draw_equipped_items(Snes *s)
{
    uint16_t at = s->cpu.x;
    menu_equip_draw_equipped(state_of(s), at);
    ORIGINAL_EFFECT(leave_equipped_drawn(s, at));
    m_rts(s);
}

/* ---- What the original's tests leave ----------------------------------------------------------- */


/* The original takes a choice of member by testing the push right and then
 * the buttons (whose overflow is going back's bit), and counts the step on,
 * as the tick it plays saves the flags. */
static void leave_member_step_tested(Snes *s)
{
    const GameState *g = state_of(s);
    if (!menu_pushed(g, MENU_PUSHED_RIGHT))
        menu_leave_bit_test(s, (uint8_t)s->cpu.a, menu_cursor_buttons(g));
    menu_leave_load(s, menu_screen_step(g));
}

/* A slot's step tests the push left and then the buttons; going back loads
 * the members' step, choosing loads how many items fit. */
static void leave_slot_step_tested(Snes *s, SlotChoice c)
{
    const GameState *g = state_of(s);
    if (!menu_pushed(g, MENU_PUSHED_LEFT))
        menu_leave_bit_test(s, (uint8_t)s->cpu.a, menu_cursor_buttons(g));
    if (c == SLOT_LEFT_FOR_THE_MEMBERS)
        menu_leave_load(s, menu_screen_step(g));
    else if (c == SLOT_CHOSEN)
        menu_leave_load(s, menu_equip_choices(g));
}

/* An item's step tests the buttons. */
/* The inventory's routines run with 8-bit index registers, which leaves
 * Y's high byte clear. */
static void leave_inventory_changed(Snes *s) { s->cpu.y &= 0x00FF; }

static void leave_buttons_tested(Snes *s)
{
    menu_leave_bit_test(s, (uint8_t)s->cpu.a, menu_cursor_buttons(state_of(s)));
}

/* Where the list's cursor started the frame, compared with where it is. */
static void leave_cursor_compared(Snes *s)
{
    const GameState *g = state_of(s);
    menu_leave_compare(s, menu_cursor_frame_start(g), menu_cursor_index(g));
}

/* The slide's start leaves its step of the list's place in the flags it
 * saves under the slide: the place a member up as the step down to it left
 * them, or as the place's own step on in memory did, after a compare with
 * the furthest it goes that left the carry clear. */
static void leave_member_slide_started(Snes *s, MemberListSlide slide)
{
    menu_leave_load(s, menu_member_scroll(state_of(s)));
    if (slide == MEMBERS_SLIDE_DOWN)
        m_set_flag(s, CPU_FLAG_C, false);
}

/* The member pointed at, compared with the one before; the sum of the row
 * and the scroll it came from never overflows; and the rows' colouring
 * queued their upload last. */
static void leave_member_pointed(Snes *s, uint8_t pointed_before)
{
    const GameState *g = state_of(s);
    menu_leave_compare(s, menu_member_pointed(g), pointed_before);
    m_set_flag(s, CPU_FLAG_V, false);
    menu_leave_member_rows_coloured(s);
}

/* The slot compared with the one kept before. */
static void leave_slot_kept(Snes *s, uint8_t kept_before)
{
    menu_leave_compare(s, menu_cursor_index(state_of(s)), kept_before);
}

/* The slot's and the member's sum the list is recorded by, compared with
 * the one recorded before; the sum never overflows. */
static void leave_list_sum(Snes *s, uint8_t shown_before)
{
    const GameState *g = state_of(s);
    menu_leave_compare(s, menu_equip_shown(g), shown_before);
    m_set_flag(s, CPU_FLAG_V, false);
}

/* The slot's label copied to the other layer by a block move, which leaves
 * A at $FFFF and the indexes past the copy. */
enum { LABEL_COPY_FROM_END = 0x30C0, LABEL_COPY_TO_END = 0x38C0 };

static void leave_label_copied(Snes *s)
{
    s->cpu.a = 0xFFFF;
    s->cpu.x = LABEL_COPY_FROM_END;
    s->cpu.y = LABEL_COPY_TO_END;
}

/* The list drawn: the pen past its last row in A. */
static void leave_list_drawn(Snes *s) { s->cpu.a = menu_equip_list_end(); }

/* The description ($C2:F2F3) leaves A clear where it started typing, and
 * the original's last load before the cursor's sound is the cursor's place
 * on the list. */
static void leave_list_opened(Snes *s, bool typing_started)
{
    if (typing_started)
        menu_leave_typing_started(s);
    m_set_a8(s, menu_cursor_index(state_of(s)));
}

/* The member switched to is compared with the list's last row as the list
 * is laid out for it: from the last row on the list scrolls (the row loaded),
 * before it the comparison is short. */
static void leave_member_switched(Snes *s)
{
    const GameState *g = state_of(s);
    uint8_t member = menu_member_worked_on(g);
    if (member >= MENU_MEMBER_ROWS - 1)
        menu_leave_load(s, MENU_MEMBER_ROWS - 1);
    else
        menu_leave_load(s, (uint8_t)(member - (MENU_MEMBER_ROWS - 1)));
    m_set_flag(s, CPU_FLAG_C, member >= MENU_MEMBER_ROWS - 1);
    m_set_flag(s, CPU_FLAG_V, false);
}

/* The panel's tests before the card: the cursor's row compared with the
 * active members (before it was brought onto one), the scroll's furthest
 * worked out without overflowing, and the clock's digits' place loaded. */
enum { CLOCK_DIGITS = 0x0402 };

void menu_leave_equipment_panel_tested(Snes *s, uint8_t row_before)
{
    const GameState *g = state_of(s);
    menu_leave_load(s, (uint8_t)(CLOCK_DIGITS >> 8));
    m_set_flag(s, CPU_FLAG_C, row_before >= menu_active_members(g));
    m_set_flag(s, CPU_FLAG_V, false);
}

/* A page turned: the paging's answer compared with none, then the new first
 * row loaded. */
static void leave_page_turned(Snes *s)
{
    menu_leave_load(s, menu_equip_list_top(state_of(s)));
    m_set_flag(s, CPU_FLAG_C, true);
}

/* The row the cursor is on, which the original notes in the item cell for
 * the slide where the push came past the list's edge (a scrolling list's). */
static void leave_row_noted(Snes *s, uint8_t row)
{
    GameState *g = state_of(s);
    if (menu_list_scrolls_a_row(g))
        set_menu_item_operand(g, row);
}

/* The row coming in found by its place plus the list's first row, a sum
 * that never carries or overflows, for the row's drawing, which may read
 * the overflow. */
static void leave_row_found(Snes *s)
{
    m_set_flag(s, CPU_FLAG_C, false);
    m_set_flag(s, CPU_FLAG_V, false);
}

/* The row's upload queued, its record's place loaded last. */
static void leave_list_rows_queued(Snes *s)
{
    menu_leave_queued_upload(s, MENU_EQUIP_LIST_ROWS_UPLOAD);
    menu_leave_load(s, (uint8_t)(MENU_EQUIP_LIST_ROWS_UPLOAD >> 8));
}

/* ---- The guards -------------------------------------------------------------------------------- */

/* The steps run on the menus' page with the data bank on work RAM's, play
 * their sounds there, and queue whole uploads with room for what a step
 * queues. */
static bool queue_takes(const GameState *g, unsigned uploads) { return menu_upload_queue_takes(g, uploads); }

static bool on_the_screen(Snes *s) { return menu_on_page_work_ram(s) && menu_sounds_on_page(s); }

/* The members the screen reaches: at least one and no more than the list's
 * places, each a character whose card can be drawn. */
static bool members_are_drawable(const GameState *g)
{
    uint16_t reachable = menu_members_reachable(g);
    if (reachable == 0 || reachable > MENU_MEMBERS_COUNT || menu_active_members(g) > reachable)
        return false;
    for (uint8_t member = 0; member < reachable; member++)
        if (!menu_member_card_can_be_drawn_for(g, member))
            return false;
    return menu_member_on_the_cursor(g) < MENU_MEMBERS_COUNT && (uint8_t)menu_screen_argument(g) < MENU_MEMBERS_COUNT;
}

static bool peek(Snes *s, bool (*test)(const GameState *))
{
    vars_peek(true);
    bool ok = test(state_of(s));
    vars_peek(false);
    return ok;
}

bool menu_equipment_opens(Snes *s) { return on_the_screen(s); }

bool menu_equipment_sets_up(Snes *s) { return on_the_screen(s) && peek(s, menu_equip_can_set_up); }

bool menu_equipment_member_step_runs(Snes *s)
{
    return on_the_screen(s) && menu_cursor_is_movable(s) && peek(s, menu_equip_can_choose_a_member);
}

bool menu_equipment_member_is_pointable(Snes *s) { return on_the_screen(s) && peek(s, menu_equip_can_point_at_a_member); }

static bool members_slide(const GameState *g) { return queue_takes(g, 3) && menu_equip_member_slide_can_run(g); }

bool menu_equipment_members_slide(Snes *s) { return on_the_screen(s) && peek(s, members_slide); }
bool menu_equip_members_can_slide(const GameState *g) { return members_slide(g); }

/* The slide's start reads the push and the list's place; the slide it starts
 * is run where its own guard holds, and is the original's call otherwise. */
bool menu_equipment_members_slide_starts(Snes *s) { return on_the_screen(s); }

bool menu_equipment_slot_step_runs(Snes *s) { return on_the_screen(s) && menu_cursor_is_movable(s); }

/* Pointing at a slot: the members it may switch to drawable, room for the
 * uploads (the card's, the marks' layer), the slot's sum. */
static bool slot_is_pointable(const GameState *g) { return members_are_drawable(g) && queue_takes(g, 3); }

bool menu_equipment_slot_is_pointable(Snes *s) { return on_the_screen(s) && peek(s, slot_is_pointable); }
bool menu_equipment_member_switches(Snes *s) { return on_the_screen(s) && peek(s, slot_is_pointable); }

/* Choosing an item: the member's record one of the characters', the slot's
 * place in it, the list's place of the cursor's row, and the list's; and the
 * menus' zero cell 0, as the menus keep it, which the original's figures
 * tried on are marked from (menu_equip_can_try_on) and its windows' text
 * drawn by. */
enum { RECORD_BYTES = 0x50, FIRST_LIST_PLACE = 4, LIST_ROWS = 9 };

static bool list_is_pointable(const GameState *g)
{
    uint8_t row = (uint8_t)(menu_cursor_index(g) - FIRST_LIST_PLACE);
    return menu_equip_choices(g) <= MENU_LIST_ITEMS_COUNT && row < LIST_ROWS
           && menu_equip_list_top(g) + LIST_ROWS + 1u <= MENU_LIST_ITEMS_COUNT && queue_takes(g, 2);
}

static bool item_step_runs(const GameState *g)
{
    return menu_zero(g) == 0 && menu_member_record_is_a_characters(g) && menu_equip_record_place(g) < RECORD_BYTES
           && menu_member_row(g) < MENU_MEMBERS_COUNT && list_is_pointable(g);
}

bool menu_equipment_item_step_runs(Snes *s)
{
    return on_the_screen(s) && menu_cursor_is_movable(s) && peek(s, item_step_runs);
}

bool menu_equipment_list_is_pointable(Snes *s) { return on_the_screen(s) && peek(s, list_is_pointable); }


bool menu_equipment_list_rows_move(Snes *s) { return menu_on_page_work_ram(s); }

static bool weapon_is_forgettable(const GameState *g) { return menu_member_row(g) < MENU_MEMBERS_COUNT; }

bool menu_equipment_weapon_is_forgettable(Snes *s) { return menu_on_page_work_ram(s) && peek(s, weapon_is_forgettable); }

bool menu_equipment_panel_shows(Snes *s) { return on_the_screen(s) && peek(s, menu_equipment_panel_can_show); }

/* The dimmed colours go whole into the palette where Y points, from its
 * first colour on. */
enum { DIMMED_BYTES = 24 * 2 };

bool menu_panel_colours_dim(Snes *s)
{
    uint16_t at = s->cpu.y;
    uint16_t palette = RAM_ABS(RAM_MENU_PALETTE);
    if (!menu_on_page_work_ram(s) || at < palette || (at - palette) % 2 != 0
        || (unsigned)(at - palette) + DIMMED_BYTES > RAM_MENU_PALETTE_COUNT * 2u)
        return false;
    vars_peek(true);
    bool ok = menu_zero(state_of(s)) == 0;
    vars_peek(false);
    return ok;
}

bool menu_panel_colours_show(Snes *s) { return menu_on_page_work_ram(s); }

/* ---- The windows' guards ----------------------------------------------------------------------- */

/* The slots: one of four, the cursor's for its label; the items a member has
 * on, each one of the game's. */
enum { SLOT_PLACES = 4, EQUIPPED_AT_0 = 0x27, EQUIPPED_BYTES = 4, LIST_ROWS_DRAWN = 8 };

static bool equipped_are_items(const GameState *g)
{
    for (unsigned i = 0; i < EQUIPPED_BYTES; i++)
        if (menu_status_record_bytes_byte(g, EQUIPPED_AT_0 + i) >= MENU_ITEM_NAME_PLACES_COUNT)
            return false;
    return true;
}

/* The list of what fits drawn from its first row: every place it reaches
 * one of the list's, every item one of the game's. */
static bool list_is_drawable(const GameState *g)
{
    if (menu_equip_slot(g) >= SLOT_PLACES || menu_equip_list_top(g) + LIST_ROWS_DRAWN > MENU_LIST_ITEMS_COUNT)
        return false;
    for (unsigned i = 0; i < LIST_ROWS_DRAWN; i++)
        if (menu_list_items(g, menu_equip_list_top(g) + i) >= MENU_ITEM_NAME_PLACES_COUNT)
            return false;
    return menu_equip_choices(g) <= MENU_LIST_ITEMS_COUNT;
}

static bool member_shows(const GameState *g)
{
    return menu_member_row(g) < MENU_MEMBER_ROWS && menu_member_card_can_be_drawn_for(g, menu_member_on_the_cursor(g));
}

static bool members_come_back(const GameState *g) { return queue_takes(g, 1) && member_shows(g) && menu_window_list_drawable(g, 0xBDEF); }
static bool list_opens(const GameState *g) { return queue_takes(g, 4) && list_is_drawable(g) && menu_window_list_drawable(g, 0xBEEC); }
static bool slots_come_back(const GameState *g) { return queue_takes(g, 4) && member_shows(g) && list_is_drawable(g) && equipped_are_items(g); }
static bool window_slides(const GameState *g) { return queue_takes(g, 3) && member_shows(g) && menu_member_rows_can_be_drawn(g); }
static bool slots_show(const GameState *g) { return queue_takes(g, 3) && member_shows(g); }
static bool slots_mark(const GameState *g) { return menu_equip_slot(g) < SLOT_PLACES && menu_cursor_index(g) < SLOT_PLACES; }

bool menu_equipment_members_come_back(Snes *s) { return on_the_screen(s) && peek(s, members_come_back); }
bool menu_equipment_list_opens(Snes *s) { return on_the_screen(s) && peek(s, list_opens); }
bool menu_equipment_list_is_drawable(Snes *s) { return menu_on_page_work_ram(s) && peek(s, list_is_drawable); }
bool menu_equipment_list_slides(Snes *s) { return menu_on_page_work_ram(s) && peek(s, slots_show); }
bool menu_equipment_slots_come_back(Snes *s) { return on_the_screen(s) && peek(s, slots_come_back); }
bool menu_equipment_slots_window_slides(Snes *s) { return on_the_screen(s) && peek(s, window_slides); }

/* The guards' state halves, for the program as C. */
bool menu_equip_slots_window_can_slide(const GameState *g) { return window_slides(g); }
bool menu_equip_can_point_at_a_slot(const GameState *g) { return slot_is_pointable(g); }
bool menu_equip_members_can_come_back(const GameState *g) { return members_come_back(g); }
bool menu_equip_list_can_open(const GameState *g) { return list_opens(g); }
bool menu_equip_list_can_be_drawn(const GameState *g) { return list_is_drawable(g); }
bool menu_equip_list_can_slide(const GameState *g) { return slots_show(g); }
bool menu_equip_list_can_be_pointed_along(const GameState *g) { return list_is_pointable(g); }
bool menu_equip_slots_can_come_back(const GameState *g) { return slots_come_back(g); }
bool menu_equip_can_choose_an_item(const GameState *g) { return item_step_runs(g) && menu_cursor_can_move(g); }
bool menu_equip_weapon_can_be_forgotten(const GameState *g) { return weapon_is_forgettable(g); }
bool menu_equipment_slots_are_showable(Snes *s) { return on_the_screen(s) && peek(s, slots_show); }
bool menu_equipment_slots_are_markable(Snes *s) { return menu_on_page_work_ram(s) && peek(s, slots_mark); }
bool menu_equipped_items_are_drawable(Snes *s) { return menu_on_page_work_ram(s) && peek(s, equipped_are_items); }

// $C2:8953
/* The list made as the original's callers make it: what it leaves in the
 * registers they don't read, its flags and widths its caller's (it saves
 * and restores them). */
void menu_list_what_fits_entry(Snes *s)
{
    menu_list_what_fits(state_of(s));
    m_rts(s);
}

bool menu_list_what_fits_runs(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = menu_list_what_fits_can_be_made(state_of(s));
    vars_peek(false);
    return ok;
}
