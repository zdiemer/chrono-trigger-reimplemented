/* The equipment screen on the original's machine: its registered entries,
 * the bridges that run what game/menu/equipment.c decides between the calls
 * to what is still the original's, and their guards.
 *
 * Each step reads the cursor, asks equipment.c what the frame's input does,
 * and carries that out: the sounds and the member's card are siblings, run
 * as the original's JSRs reach them (menu_run); the slots' and the list's
 * drawing, the party's inventory, the lists' paging and sliding and the
 * frames the screen waits for are the original's, called out to; where a
 * step jumps on to another routine (the member list's move, the slots'
 * pointing, the list's step, back to the main menu), it stops there, and the
 * runtime runs that routine, a port or the original's.
 *
 * Shadow mode compares the machine at every call out, the flags a sibling
 * saves as it starts (on the stack under its own calls out) among it, and
 * some of the original's drawings may read the overflow and the carry. So
 * after each decision a bridge leaves the flags the original's own tests
 * left, worked out from the state (the leaves at the end of the file):
 * scaffolding, which the native build compiles out. */
#include "menu/equipment.h"

#include "charge.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/display.h"
#include "menu/members.h"
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
    YIELD = 0xC2821E,                /* the task waits for the next frame */
    SET_ITEMS_ASIDE = 0xC290FC,      /* the party's items set aside, where the menus keep them so ($0D7E) */
    PUT_ITEMS_BACK = 0xC29121,       /* ... and put back */
    MEMBER_SCROLL_BAR = 0xC2A170,    /* the member list's scroll bar set up */
    LIST_WHAT_FITS = 0xC28953,       /* the list of what fits the slot made */
    FRAME_THE_LIST = 0xC2EA81,       /* a scrolling list's frame laid out: Y places, rows shown */
    START_SCROLL_BAR = 0xC28B93,     /* the scroll bar's task started */
    DESCRIBE = 0xC2F2F3,             /* the description window drawn for the item in A */
    LOW_HP_MARKS = 0xC2813C,         /* each character's low-HP mark worked out */
    GIVE_BACK = 0xC28791,            /* menu_item_operand added to the party's items */
    TAKE = 0xC287D5,                 /* ... and taken from them */
    PREVIEW_ITEM = 0xC2A5C3,         /* the list's item on the cursor put in the record, the figures with it */
    TURN_PAGE = 0xC293BF,            /* the list's paging with the shoulder assignments */
    MOVE_IN_LIST = 0xC2EA53,         /* the cursor kept within a list's rows */
    START_LIST_SLIDE = 0xC2EB9B,     /* a list's slide started */
    LIST_SLIDE_FRAME = 0xC2EB1F,     /* a frame of a list's slide, and the wait for the next */
    START_MEMBER_SLIDE = 0xC29D5B,   /* the member list's slide started, if there is a member to bring in */
};

/* Where a step jumps on to: the member list's move and the slots' pointing,
 * the screen's set-up, the card, back to the members, on to the list (ports
 * of these files), back to the main menu, the refusal's sound and the cursor
 * sound as a step's last, and the stop of the list's task. */
enum {
    POINT_AT_MEMBER = 0xC29D17, POINT_AT_SLOT = 0xC29ECD, SET_UP_SCREEN = 0xC2A11A, MEMBER_CARD = 0xC2A22F,
    MAIN_MENU = 0xC29A98, BACK_TO_MEMBERS = 0xC2A2CD, ON_TO_THE_LIST = 0xC2A41A, REFUSAL_SOUND = 0xC2EACC,
    CURSOR_SOUND = 0xC2EAC2, STOP_TASKS = 0xC28255,
};

/* What they read (tools/dontcare_check.py checks each): most nothing but the
 * page and the data bank; the drawings that may reach the window language's
 * handlers, picked at run time, maybe the overflow and the carry; a row of
 * the list X as well; the refusal's sound A's high byte, and the tasks' stop
 * the tasks in A. */
enum {
    READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    MAY_READ_V_C = DC_A | DC_X | DC_Y | DC_N | DC_Z,
    MAY_READ_V = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C,
    READS_X_MAY_READ_V = DC_A | DC_Y | DC_N | DC_Z | DC_C,
    READS_A_HIGH = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_A = DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_Y = DC_A | DC_X | DC_N | DC_V | DC_Z | DC_C,
    READS_A_LOW = DC_AH | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
};

static bool call(Snes *s, uint32_t site, uint32_t routine) { return menu_call(s, site, routine, READS_NOTHING); }
static bool yield(Snes *s, uint32_t site) { return menu_call(s, site, YIELD, READS_NOTHING); }
static bool tick(Snes *s, uint32_t site) { return menu_run(s, site, 0xC2EAC2, menu_play_cursor_sound); }

/* A sound by its number, loaded into A as the original's call does (the
 * load's flags are the P the sound saves). */
static bool play_sound(Snes *s, uint32_t site, uint8_t sound)
{
    m_set_a8(s, sound);
    ORIGINAL_EFFECT(menu_leave_load(s, sound));
    return menu_run(s, site, 0xC2EABA, menu_play_sound);
}

/* The cursor read as C, leaving what the original's reading leaves. */
static void read_cursor(Snes *s)
{
    GameState *g = state_of(s);
    MenuCursorRead r = menu_note_cursor_read(g);
    (void)r;
    TIMED_AS(g, 0xC2E984, menu_read_cursor(g));
    ORIGINAL_EFFECT(menu_leave_cursor_read(s, r));
}

static void leave_member_step_tested(Snes *s);
static void leave_slot_step_tested(Snes *s, SlotChoice c);
static void leave_buttons_tested(Snes *s);
static void leave_cursor_compared(Snes *s);
static void leave_member_pointed(Snes *s, uint8_t pointed_before);
static void leave_slot_kept(Snes *s, uint8_t kept_before);
static void leave_list_sum(Snes *s, uint8_t shown_before);
static void leave_label_copied(Snes *s);
static void leave_list_drawn(Snes *s);
static void leave_member_switched(Snes *s);
static void leave_panel_tested(Snes *s, uint8_t row_before);
static void leave_page_turned(Snes *s);
static void leave_row_found(Snes *s);
static void leave_list_rows_queued(Snes *s);

/* ---- Opening the screen ------------------------------------------------------------------------ */

// $C2:9C31
/* The screen's first step: a tick, on to choosing a member, and the screen
 * set up ($C2:A11A, which the original jumps to). */
void menu_open_equipment(Snes *s)
{
    if (!tick(s, 0xC29C31))
        return;
    menu_equip_open(state_of(s));
    m_stop_at(s, SET_UP_SCREEN, READS_NOTHING);
}

// $C2:A11A
/* The screen set up, the party's items set aside first and the scroll bar
 * last. It returns for the step. */
void menu_set_up_equipment(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, false, false);
    if (!call(s, 0xC2A11D, SET_ITEMS_ASIDE))
        return;
    menu_equip_set_up_screen(g, m_platform(s));
    menu_set_widths(s, true, false);
    if (!call(s, 0xC2A15D, MEMBER_SCROLL_BAR))
        return;
    menu_equip_show_screen(g);
    menu_restore_p(s);
    m_stop_dont_care(s, 0xC2A16F, READS_NOTHING);
    m_rts(s);
}

/* ---- Choosing a member ------------------------------------------------------------------------- */

// $C2:9C39
/* A member chosen: a tick, the slots' window and the member's slots drawn, a
 * frame, a tick, and the list of what fits the slot made, drawn and queued. */
void menu_choose_equipment_member(Snes *s)
{
    GameState *g = state_of(s);
    read_cursor(s);
    MemberChoice c = menu_equip_choose_member(g);
    ORIGINAL_EFFECT(leave_member_step_tested(s));
    switch (c) {
    case MEMBER_CHOSEN:
        if (!tick(s, 0xC29C52) || !menu_run(s, 0xC29C55, 0xC2A71E, menu_open_slots_window))
            return;
        TIMED_AS(g, 0xC2A321, menu_equip_show_slots_of_member(g, m_platform(s)));
        if (!yield(s, 0xC29C5B) || !tick(s, 0xC29C5E) || !call(s, 0xC29C61, LIST_WHAT_FITS))
            return;
        TIMED_AS(g, 0xC2A569, menu_equip_draw_list(g));
        menu_equip_queue_list_rows(g);
        m_rts(s);
        return;
    case MEMBER_LEFT_FOR_THE_MAIN_MENU: m_stop_at(s, MAIN_MENU, READS_NOTHING); return;
    case MEMBER_POINTED_AT: m_stop_at(s, POINT_AT_MEMBER, READS_NOTHING); return;
    }
}

// $C2:9D17
/* The cursor among the members: the list goes round or slides, the rows are
 * coloured, a tick where it points at another member, and on to the card
 * ($C2:A22F, which the original jumps to). */
void menu_point_at_equipment_member(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, true, true);
    if (!menu_equip_member_list_scrolls(g))
        menu_equip_take_cursor_round_members(g);
    else if (menu_equip_push_slides_members(g) && !call(s, 0xC29D23, START_MEMBER_SLIDE))
        return;
    uint8_t pointed_before = menu_member_pointed(g);
    bool another = menu_equip_point_at_member(g);
    ORIGINAL_EFFECT(leave_member_pointed(s, pointed_before));
    if (another && !tick(s, 0xC29D54))
        return;
    menu_restore_p(s);
    m_stop_at(s, MEMBER_CARD, READS_NOTHING);
}

// $C2:9DAF
/* The member list's slide, as its start ($C2:9D5B, the original's) set it:
 * a frame at a time, waiting for the next between them. */
void menu_slide_equipment_members(Snes *s)
{
    GameState *g = state_of(s);
    menu_equip_start_member_slide(g, m_platform(s));
    while (menu_equip_slide_members_a_frame(g))
        if (!yield(s, 0xC29E3C))
            return;
    menu_equip_finish_member_slide(g);
    menu_set_widths(s, true, false);
    m_rts(s);
}

/* ---- Choosing a slot --------------------------------------------------------------------------- */

/* The tasks' stop the screen's way back from the list takes: the list's own
 * task (the fourth). */
enum { LIST_TASK = 0x10 };

// $C2:9C70
/* Back to the members: a tick and the list drawn again, then on to the
 * members' step's return ($C2:A2CD). A slot chosen: a tick, and on to the
 * list's step ($C2:A41A); refused, the refusal's sound. */
void menu_choose_equipment_slot(Snes *s)
{
    GameState *g = state_of(s);
    read_cursor(s);
    SlotChoice c = menu_equip_choose_slot(g);
    ORIGINAL_EFFECT(leave_slot_step_tested(s, c));
    switch (c) {
    case SLOT_LEFT_FOR_THE_MEMBERS:
        if (!tick(s, 0xC29C9C) || !menu_run(s, 0xC29C9F, 0xC2A85C, menu_close_slots_window))
            return;
        m_stop_at(s, BACK_TO_MEMBERS, READS_NOTHING);
        return;
    case SLOT_REFUSED: m_stop_at_own(s, REFUSAL_SOUND, READS_A_HIGH); return;
    case SLOT_CHOSEN:
        if (!tick(s, 0xC29C90))
            return;
        menu_equip_start_choosing_an_item(g);
        m_stop_at(s, ON_TO_THE_LIST, READS_NOTHING);
        return;
    case SLOT_POINTED_AT: m_stop_at(s, POINT_AT_SLOT, READS_NOTHING); return;
    }
}

// $C2:9ECD
/* The cursor among the slots: the slot taken, the members switched, a tick
 * where the slot changed, the marks, and the list of what fits drawn again
 * a frame later where the slot or the member changed. */
void menu_point_at_equipment_slot(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    TIMED_AS(g, 0xC2A38B, menu_equip_take_slot(g));
    ORIGINAL_EFFECT(leave_label_copied(s));
    if (!menu_run(s, 0xC29ED3, 0xC2A00B, menu_switch_equipment_member))
        return;
    uint8_t kept_before = menu_equip_kept_slot(g);
    bool moved = menu_equip_keep_slot(g);
    ORIGINAL_EFFECT(leave_slot_kept(s, kept_before));
    if (moved && !tick(s, 0xC29EE0))
        return;
    TIMED_AS(g, 0xC2A3E2, menu_equip_mark_slots(g));
    menu_equip_show_slots(g);
    uint8_t shown_before = menu_equip_shown(g);
    bool another = menu_equip_list_is_for_another_slot(g);
    ORIGINAL_EFFECT(leave_list_sum(s, shown_before));
    if (another && (!yield(s, 0xC29EFD) || !menu_run(s, 0xC29F00, 0xC2A45D, menu_redraw_equipment_list)))
        return;
    menu_restore_p(s);
    m_stop_dont_care(s, 0xC29F04, READS_NOTHING);
    m_rts(s);
}

// $C2:A00B
/* The members switched with the shoulder assignments: a tick, the card and
 * the member's row drawn again, the marks, and a frame. */
void menu_switch_equipment_member(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, true, true);
    bool switched = menu_equip_switch_member(g);
    ORIGINAL_EFFECT(leave_member_switched(s));
    if (switched) {
        if (!tick(s, 0xC2A043) || !menu_run(s, 0xC2A046, 0xC2A21F, menu_redraw_member_card))
            return;
        TIMED_AS(g, 0xC2A3E2, menu_equip_mark_slots(g));
        if (!yield(s, 0xC2A04C))
            return;
    }
    menu_restore_p(s);
    m_rts(s);
}

/* ---- Choosing an item -------------------------------------------------------------------------- */

/* The sound of an item put on. */
enum { PUT_ON_SOUND = 0xB5 };

/* Back to the slots: a tick, the cursor on the slot it left, the slots drawn
 * again, and the list's task stopped ($C2:8255). */
static void back_to_the_slots(Snes *s)
{
    GameState *g = state_of(s);
    if (!tick(s, 0xC29CE4))
        return;
    menu_equip_return_to_the_kept_slot(g);
    ORIGINAL_EFFECT(menu_leave_load(s, menu_cursor_index(g)));
    if (!menu_run(s, 0xC29CEC, 0xC2A64D, menu_back_to_equipment_slots))
        return;
    menu_equip_back_to_the_slots(g);
    m_set_a8(s, LIST_TASK);
    m_stop_at_own(s, STOP_TASKS, READS_A);
}

// $C2:9CA5
/* An item chosen: what comes off given back, the chosen item put on and
 * taken from the party's items, its sound, a weapon's byte forgotten, and
 * back to the slots. Otherwise the cursor moves along the list, the item on
 * it previewed, with a tick where it moved. */
void menu_choose_equipment_item(Snes *s)
{
    GameState *g = state_of(s);
    read_cursor(s);
    ItemChoice c = menu_equip_choose_item(g);
    ORIGINAL_EFFECT(leave_buttons_tested(s));
    switch (c) {
    case ITEM_CHOSEN:
        menu_equip_name_what_comes_off(g);
        if (!call(s, 0xC29CCE, GIVE_BACK))
            return;
        menu_equip_put_on_the_chosen_item(g);
        if (!call(s, 0xC29CD9, TAKE))
            return;
        if (!play_sound(s, 0xC29CDE, PUT_ON_SOUND))
            return;
        TIMED_AS(g, 0xC29CFC, menu_equip_forget_weapon(g));
        back_to_the_slots(s);
        return;
    case ITEM_LEFT_FOR_THE_SLOTS: back_to_the_slots(s); return;
    case ITEM_POINTED_AT:
        if (!menu_run_any_widths(s, 0xC29CAF, 0xC29F05, menu_point_along_equipment_list) || !call(s, 0xC29CB2, PREVIEW_ITEM))
            return;
        menu_set_widths(s, true, m_x8(s));
        ORIGINAL_EFFECT(leave_cursor_compared(s));
        if (menu_cursor_frame_start(g) != menu_cursor_index(g) && !tick(s, 0xC29CBD))
            return;
        m_rts(s);
        return;
    }
}

/* A row of the list is drawn below its last, out of view, as it slides in. */
enum { NEW_LIST_ROW_AT = 0x53C6 };

// $C2:9F05
/* The list's cursor moved: a page turned, the list drawn again and a tick;
 * or a row slid in, drawn, a tick, and the slide's frames, each waiting for
 * the next. The slide leaves a 16-bit accumulator. */
void menu_point_along_equipment_list(Snes *s)
{
    GameState *g = state_of(s);
    menu_set_widths(s, true, m_x8(s));
    menu_equip_note_list_top_for_paging(g);
    if (!call(s, 0xC29F0D, TURN_PAGE))
        return;
    if (menu_equip_list_paged(g)) {
        menu_equip_turn_list_page(g);
        ORIGINAL_EFFECT(leave_page_turned(s));
        TIMED_AS(g, 0xC2A569, menu_equip_draw_list(g));
        ORIGINAL_EFFECT(leave_list_drawn(s));
        if (!tick(s, 0xC29F26))
            return;
        menu_equip_queue_list_rows(g);
        m_rts(s);
        return;
    }
    if (!call(s, 0xC29F2F, MOVE_IN_LIST))
        return;
    if (!menu_equip_list_slide_brings_a_row(g)) {
        m_rts(s);
        return;
    }
    menu_equip_set_up_list_slide(g);
    if (!call(s, 0xC29F5B, START_LIST_SLIDE))
        return;
    menu_equip_bring_in_list_row(g);
    ORIGINAL_EFFECT(leave_row_found(s));
    TIMED_AS(g, 0xC2A59A, menu_equip_draw_list_row(g, NEW_LIST_ROW_AT));
    menu_equip_queue_list_rows(g);
    ORIGINAL_EFFECT(leave_list_rows_queued(s));
    if (!tick(s, 0xC29F8D))
        return;
    menu_set_widths(s, false, false);
    if (menu_equip_start_list_slide_frames(g)) {
        menu_equip_slide_list_a_frame(g);
        while (menu_equip_list_slide_frames_left(g)) {
            if (!call(s, 0xC29FB4, LIST_SLIDE_FRAME))
                return;
            menu_equip_slide_list_a_frame(g);
        }
    }
    menu_equip_finish_list_slide(g);
    m_rts(s);
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
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    menu_save_p(s);
    menu_set_widths(s, true, true);
    uint8_t row_before = menu_member_row(g);
    menu_equip_panel_lay_out_list(g, pf);
    menu_set_widths(s, false, false);
    if (!yield(s, 0xC2A0AC))
        return;
    menu_set_widths(s, true, false);
    menu_equip_panel_draw_members(g, pf);
    if (!yield(s, 0xC2A0BD))
        return;
    menu_equip_panel_set_colour_math(g, pf);
    ORIGINAL_EFFECT(leave_panel_tested(s, row_before));
    if (!menu_run(s, 0xC2A0C3, MEMBER_CARD, menu_draw_member_card))
        return;
    menu_equip_panel_dim(g);
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
    menu_equip_panel_colours_back(g);
    if (!call(s, 0xC2A110, PUT_ITEMS_BACK))
        return;
    menu_equip_panel_line_effects_back(g);
    menu_restore_p(s);
    m_rts(s);
}

/* ---- The windows ------------------------------------------------------------------------------- */

/* The description drawn for an item, A holding it. */
static bool describe(Snes *s, uint32_t site, uint16_t a)
{
    s->cpu.a = a;
    return menu_call(s, site, DESCRIBE, READS_A_LOW);
}

// $C2:A71E
/* The slots window sliding in, a frame at a time after a wait for each. */
void menu_open_slots_window(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, false, false);
    MenuWindowSlide w;
    menu_equip_start_slots_slide(g, m_platform(s), &w);
    menu_set_widths(s, true, false);
    do {
        if (!yield(s, 0xC2A785))
            return;
    } while (menu_equip_slide_slots_a_frame(g, &w));
    menu_restore_p(s);
    m_rts(s);
}

// $C2:A85C
/* The slots window going: a frame, the members' rows, a frame, then the
 * slide out, a frame at a time after a wait for each. */
void menu_close_slots_window(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    menu_save_p(s);
    menu_set_widths(s, false, false);
    if (!yield(s, 0xC2A85F))
        return;
    menu_equip_clear_slot_labels(g, pf);
    if (!yield(s, 0xC2A88F))
        return;
    MenuWindowSlide w;
    menu_equip_start_members_slide(g, &w);
    menu_set_widths(s, true, false);
    do {
        if (!yield(s, 0xC2A945))
            return;
    } while (menu_equip_slide_members_back_a_frame(g, &w));
    menu_restore_p(s);
    m_rts(s);
}

// $C2:A2CD
/* Back on the members, the member scroll bar set up (the original's)
 * between. */
void menu_back_to_equipment_members(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    menu_equip_back_on_the_members(g, m_platform(s));
    if (!call(s, 0xC2A2ED, MEMBER_SCROLL_BAR))
        return;
    menu_equip_rest_the_member_rows(g);
    menu_restore_p(s);
    m_rts(s);
}

// $C2:A4AF
/* The list window sliding in: a frame at a time until its band is grown. */
void menu_slide_equipment_list_in(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, false, false);
    MenuListSlide w;
    menu_equip_start_list_slide(g, m_platform(s), &w);
    menu_set_widths(s, true, false);
    while (menu_equip_slide_list_in_a_frame(g, &w))
        if (!yield(s, 0xC2A511))
            return;
    menu_restore_p(s);
    m_rts(s);
}

// $C2:A697
/* The list window sliding out, and its bands at rest. */
void menu_slide_equipment_list_out(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, false, false);
    MenuListSlide w;
    menu_equip_start_list_closing(g, m_platform(s), &w);
    menu_set_widths(s, true, false);
    while (menu_equip_slide_list_out_a_frame(g, &w))
        if (!yield(s, 0xC2A6DC))
            return;
    menu_equip_rest_the_list_bands(g);
    menu_restore_p(s);
    m_rts(s);
}

/* The list of what fits made (the original's), laid out, its frame (the
 * original's), its bar, the bar's task (the original's), and drawn. The
 * drawing leaves the pen past the list in A. */
static void make_the_list(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, false, false);
    if (!call(s, 0xC2A466, LIST_WHAT_FITS))
        return;
    menu_equip_lay_out_list(g);
    menu_set_widths(s, true, true);
    s->cpu.y = menu_equip_choices(g);
    if (!menu_call(s, 0xC2A494, FRAME_THE_LIST, READS_Y))
        return;
    menu_equip_set_list_bar(g);
    if (!call(s, 0xC2A4A3, START_SCROLL_BAR))
        return;
    TIMED_AS(g, 0xC2A569, menu_equip_draw_list(g));
    ORIGINAL_EFFECT(leave_list_drawn(s));
    menu_restore_p(s);
    m_rts(s);
}

// $C2:A463
void menu_make_equipment_list(Snes *s) { make_the_list(s); }

// $C2:A45D
/* The list drawn again, its bar a row lower. */
void menu_redraw_equipment_list(Snes *s)
{
    menu_equip_place_list_bar(state_of(s), true);
    ORIGINAL_EFFECT(menu_leave_load(s, 0x62));
    make_the_list(s);
}

// $C2:A41A
/* On to the list: the window slides in, its frame and bar, the list made
 * and drawn, the item the original happens to point at described, the cursor
 * on the list, and the cursor's sound last (its own, which the original
 * jumps to). */
void menu_open_equipment_list(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    if (!menu_run(s, 0xC2A41D, 0xC2A4AF, menu_slide_equipment_list_in))
        return;
    menu_equip_show_list_window(g, m_platform(s));
    ORIGINAL_EFFECT(menu_leave_load(s, 0x42));
    if (!menu_run(s, 0xC2A432, 0xC2A463, menu_make_equipment_list))
        return;
    menu_equip_open_the_list(g);
    if (!describe(s, 0xC2A44A, (uint16_t)((s->cpu.a & 0xFF00) | menu_equip_item_described_on_opening(g))))
        return;
    menu_equip_settle_the_list(g);
    menu_restore_p(s);
    m_stop_at_own(s, CURSOR_SOUND, READS_A);
}

// $C2:A64D
/* Back on the slots: the card again (its own), the low-HP marks (the
 * original's), the member's row and the marks, a frame, the list window
 * sliding out and the list drawn again (both their own), and the slots at
 * rest. */
void menu_back_to_equipment_slots(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    menu_save_p(s);
    menu_set_widths(s, false, false);
    if (!menu_run(s, 0xC2A650, MEMBER_CARD, menu_draw_member_card) || !call(s, 0xC2A653, LOW_HP_MARKS))
        return;
    menu_equip_redraw_slots(g, pf);
    if (!yield(s, 0xC2A661))
        return;
    ORIGINAL_EFFECT(menu_leave_load(s, 0x2E));
    if (!menu_run(s, 0xC2A664, 0xC2A697, menu_slide_equipment_list_out)
        || !menu_run(s, 0xC2A667, 0xC2A45D, menu_redraw_equipment_list))
        return;
    menu_equip_slots_at_rest(g);
    menu_restore_p(s);
    m_rts(s);
}

// $C2:A321
void menu_show_member_slots(Snes *s)
{
    menu_equip_show_slots_of_member(state_of(s), m_platform(s));
    m_rts(s);
}

// $C2:A38B
void menu_take_equipment_slot(Snes *s)
{
    menu_equip_take_slot(state_of(s));
    m_rts(s);
}

// $C2:A3E2
void menu_mark_equipment_slots(Snes *s)
{
    menu_equip_mark_slots(state_of(s));
    m_rts(s);
}

// $C2:A569
void menu_draw_equipment_list(Snes *s)
{
    menu_equip_draw_list(state_of(s));
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
/* What the member has on, from X. */
void menu_draw_equipped_items(Snes *s)
{
    menu_equip_draw_equipped(state_of(s), s->cpu.x);
    m_rts(s);
}

/* ---- What the original's tests leave ----------------------------------------------------------- */

/* The pushes the steps test first (menu_cursor_moves). */
enum { PUSHED_RIGHT = 0x01, PUSHED_LEFT = 0x02 };

/* The original takes a choice of member by testing the push right and then
 * the buttons (whose overflow is going back's bit), and counts the step on,
 * as the tick it plays saves the flags. */
static void leave_member_step_tested(Snes *s)
{
    const GameState *g = state_of(s);
    if (!(menu_cursor_moves(g) & PUSHED_RIGHT))
        menu_leave_bit_test(s, (uint8_t)s->cpu.a, menu_cursor_buttons(g));
    menu_leave_load(s, menu_screen_step(g));
}

/* A slot's step tests the push left and then the buttons; going back loads
 * the members' step, choosing loads how many items fit. */
static void leave_slot_step_tested(Snes *s, SlotChoice c)
{
    const GameState *g = state_of(s);
    if (!(menu_cursor_moves(g) & PUSHED_LEFT))
        menu_leave_bit_test(s, (uint8_t)s->cpu.a, menu_cursor_buttons(g));
    if (c == SLOT_LEFT_FOR_THE_MEMBERS)
        menu_leave_load(s, menu_screen_step(g));
    else if (c == SLOT_CHOSEN)
        menu_leave_load(s, menu_equip_choices(g));
}

/* An item's step tests the buttons. */
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

/* The member switched to is compared with the list's last row as the list
 * is laid out for it: from the last row on the list scrolls (the row loaded),
 * before it the comparison is short. */
static void leave_member_switched(Snes *s)
{
    const GameState *g = state_of(s);
    uint8_t member = (uint8_t)menu_screen_argument(g);
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

static void leave_panel_tested(Snes *s, uint8_t row_before)
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
enum { QUEUE_BYTES = 0x100 };

static bool queue_takes(const GameState *g, unsigned uploads)
{
    unsigned queued = menu_vram_queue_bytes(g);
    return queued % MENU_UPLOAD_BYTES == 0 && queued + MENU_UPLOAD_BYTES * uploads <= QUEUE_BYTES;
}

static bool on_the_screen(Snes *s) { return menu_on_page_work_ram(s) && menu_sounds_on_page(s); }

/* The members the screen reaches: no more than the list's places, each a
 * character whose card can be drawn. */
static bool members_are_drawable(const GameState *g)
{
    uint16_t reachable = menu_members_reachable(g);
    if (reachable > MENU_MEMBERS_COUNT || menu_active_members(g) > reachable)
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

static bool screen_sets_up(const GameState *g) { return queue_takes(g, 1) && menu_equip_screen_can_be_set_up(g); }

bool menu_equipment_sets_up(Snes *s) { return on_the_screen(s) && peek(s, screen_sets_up); }

static bool member_step_runs(const GameState *g) { return queue_takes(g, 1); }

bool menu_equipment_member_step_runs(Snes *s)
{
    return on_the_screen(s) && menu_cursor_is_movable(s) && peek(s, member_step_runs);
}

/* Pointing at a member: the cursor on one of the list's rows, the rows'
 * upload. */
static bool member_is_pointable(const GameState *g)
{
    return menu_cursor_index(g) < MENU_MEMBER_ROWS && menu_members_reachable(g) <= MENU_MEMBERS_COUNT
           && menu_member_scroll(g) < MENU_MEMBERS_COUNT && queue_takes(g, 1);
}

bool menu_equipment_member_is_pointable(Snes *s) { return on_the_screen(s) && peek(s, member_is_pointable); }

static bool members_slide(const GameState *g) { return queue_takes(g, 3) && menu_equip_member_slide_can_run(g); }

bool menu_equipment_members_slide(Snes *s) { return on_the_screen(s) && peek(s, members_slide); }

bool menu_equipment_slot_step_runs(Snes *s) { return on_the_screen(s) && menu_cursor_is_movable(s); }

/* Pointing at a slot: the members it may switch to drawable, room for the
 * uploads (the card's, the marks' layer), the slot's sum. */
static bool slot_is_pointable(const GameState *g) { return members_are_drawable(g) && queue_takes(g, 3); }

bool menu_equipment_slot_is_pointable(Snes *s) { return on_the_screen(s) && peek(s, slot_is_pointable); }
bool menu_equipment_member_switches(Snes *s) { return on_the_screen(s) && peek(s, slot_is_pointable); }

/* Choosing an item: the member's record one of the characters', the slot's
 * place in it, the list's place of the cursor's row, and the list's. */
enum { RECORD_BYTES = 0x50, FIRST_LIST_PLACE = 4, LIST_ROWS = 9 };

static bool record_is_a_characters(const GameState *g)
{
    uint16_t at = (uint16_t)(menu_member_record_at(g) - RAM_ABS(RAM_CHARACTER_RECORDS));
    return at % RAM_CHARACTER_RECORDS_STRIDE == 0 && at / RAM_CHARACTER_RECORDS_STRIDE < CHARACTER_RECORDS_COUNT;
}

static bool list_is_pointable(const GameState *g)
{
    uint8_t row = (uint8_t)(menu_cursor_index(g) - FIRST_LIST_PLACE);
    return menu_equip_choices(g) <= MENU_LIST_ITEMS_COUNT && row < LIST_ROWS
           && menu_equip_list_top(g) + LIST_ROWS + 1u <= MENU_LIST_ITEMS_COUNT && queue_takes(g, 2);
}

static bool item_step_runs(const GameState *g)
{
    return record_is_a_characters(g) && menu_equip_record_place(g) < RECORD_BYTES
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

/* The panel: room for its uploads and the palette's changes besides what
 * it draws. */
static bool panel_shows(const GameState *g)
{
    return queue_takes(g, 4) && menu_palette_changes_have_room(g) && menu_equip_panel_can_be_shown(g);
}

bool menu_equipment_panel_shows(Snes *s) { return on_the_screen(s) && peek(s, panel_shows); }

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
bool menu_equipment_slots_are_showable(Snes *s) { return on_the_screen(s) && peek(s, slots_show); }
bool menu_equipment_slots_are_markable(Snes *s) { return menu_on_page_work_ram(s) && peek(s, slots_mark); }
bool menu_equipped_items_are_drawable(Snes *s) { return menu_on_page_work_ram(s) && peek(s, equipped_are_items); }
