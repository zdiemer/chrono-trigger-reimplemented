/* The equipment screen on the original's machine: its checked legacy adapters,
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
 * main menu), it calls the sibling as C where its guard holds.
 *
 * Shadow mode compares the machine at every call out, the flags a sibling
 * saves as it starts (on the stack under its own calls out) among it. So
 * after each decision a bridge leaves the flags the original's own tests
 * left, worked out from the state (the leaves at the end of the file):
 * scaffolding, which the native build compiles out. */
#include "menu/equipment.h"

#include "charge.h"
#include "core/program.h"
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

/* What they read (the calling convention checks each): most nothing but the
 * page and the data bank; the refusal's sound A's high byte, and the cursor
 * sound as a step's last A and the carry. */
enum {
    READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_A_HIGH = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_A_AND_CARRY = DC_X | DC_Y | DC_N | DC_V | DC_Z,
};

static bool refuse_call(Snes *s, uint32_t site, uint32_t routine)
{
    program_breaks_a_contract(state_of(s), routine, "an equipment operation outside its contract");
    m_push16(s, (uint16_t)(site + MENU_JSR_SIZE - 1));
    m_stop_at(s, routine, READS_NOTHING);
    return false;
}
static bool tick(Snes *s, uint32_t site) { return menu_run(s, site, 0xC2EAC2, menu_play_cursor_sound); }

/* A sound by its number, loaded into A as the original's call does (the
 * load's flags are the P the sound saves). */
static bool play_sound(Snes *s, uint32_t site, uint8_t sound)
{
    m_set_a8(s, sound);
    (void)0;
    return menu_run(s, site, 0xC2EABA, menu_play_sound);
}

static void charge_member_step_tested(Snes *s);
static void charge_slot_step_tested(Snes *s, SlotChoice c);
static void charge_list_opened(Snes *s, bool typing_started);

static void charge_row_noted(Snes *s, uint8_t row);

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
            return refuse_call(s, 0xC2A653, LOW_HP_MARKS);
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
            return refuse_call(s, 0xC29CB2, TRY_ON);
        menu_equip_try_on(g, m_platform(s));
        return true;
    }
    case EQUIPMENT_SLIDES_THE_MEMBER_ROWS_UP:
        return menu_run_or_call(s, 0xC29D82, 0xC29DAF, READS_NOTHING, menu_slide_equipment_members);
    case EQUIPMENT_SLIDES_THE_MEMBER_ROWS_DOWN:
        return menu_run_or_call(s, 0xC29DAA, 0xC29DAF, READS_NOTHING, menu_slide_equipment_members);
    case EQUIPMENT_POINTS_ALONG_THE_LIST:
        /* Reading this frame's cursor can select another page or row. Its
         * drawing contract must hold for that new selection. */
        return menu_run_checked(s, 0xC29CAF, 0xC29F05, 0, menu_point_along_equipment_list_entry);
    default: return menu_run(s, RUNS[r].site, RUNS[r].routine, RUNS[r].entry);
    }
}

static void note_on_the_machine(void *ctx, EquipmentNote note, uint8_t value)
{
    Snes *s = ctx;
    (void)value;
    switch (note) {
    case EQUIPMENT_NOTE_MEMBER_STEP_TESTED: charge_member_step_tested(s); break;
    case EQUIPMENT_NOTE_MEMBER_POINTED: charge_machine_accesses(state_of(s), 1); break;
    case EQUIPMENT_NOTE_SLOT_STEP_TESTED: charge_slot_step_tested(s, (SlotChoice)value); break;
    case EQUIPMENT_NOTE_LABEL_COPIED: (void)0; break;
    case EQUIPMENT_NOTE_SLOT_KEPT: charge_machine_accesses(state_of(s), 1); break;
    case EQUIPMENT_NOTE_LIST_SUM: charge_machine_accesses(state_of(s), 1); break;
    case EQUIPMENT_NOTE_MEMBER_SWITCHED: charge_machine_accesses(state_of(s), 1); break;
    case EQUIPMENT_NOTE_BUTTONS_TESTED: charge_machine_accesses(state_of(s), 1); break;
    case EQUIPMENT_NOTE_INVENTORY_CHANGED: (void)0; break;
    case EQUIPMENT_NOTE_CURSOR_LOADED: charge_machine_accesses(state_of(s), 1); break;
    case EQUIPMENT_NOTE_CURSOR_COMPARED: charge_machine_accesses(state_of(s), 2); break;
    case EQUIPMENT_NOTE_CURSOR_KEPT: menu_charge_cursor_kept_on_the_list(s); break;
    case EQUIPMENT_NOTE_PAGE_TURNED: charge_machine_accesses(state_of(s), 1); break;
    case EQUIPMENT_NOTE_LIST_DRAWN: (void)0; break;
    case EQUIPMENT_NOTE_ROW_NOTED: charge_row_noted(s, value); break;
    case EQUIPMENT_NOTE_ROW_FOUND: (void)0; break;
    case EQUIPMENT_NOTE_LIST_ROWS_QUEUED: (void)0; break;
    case EQUIPMENT_NOTE_LIST_FRAMED: charge_machine_accesses(state_of(s), 3); break;
    case EQUIPMENT_NOTE_LIST_OPENED: charge_list_opened(s, value != 0); break;
    case EQUIPMENT_NOTE_LIST_WINDOW_SHOWN: (void)0; break;
    case EQUIPMENT_NOTE_LIST_BAR_PLACED: (void)0; break;
    case EQUIPMENT_NOTE_SLOTS_REDRAWN: (void)0; break;
    case EQUIPMENT_NOTE_MEMBER_SLIDE_STARTED: charge_machine_accesses(state_of(s), 1); break;
    }
}

/* The steps continue into their siblings as C; the
 * card and the list's opening put back the P their routines saved first. */
static bool ends_on_the_machine(void *ctx, EquipmentEnd end)
{
    Snes *s = ctx;
    switch (end) {
    case EQUIPMENT_SETS_UP_THE_SCREEN: menu_go_on_in(s, SET_UP_SCREEN, menu_set_up_equipment, READS_NOTHING); break;
    case EQUIPMENT_POINTS_AT_A_MEMBER: menu_go_on_in(s, POINT_AT_MEMBER, menu_point_at_equipment_member, READS_NOTHING); break;
    case EQUIPMENT_SHOWS_THE_CARD:
        menu_restore_p(s);
        menu_go_on_in(s, MEMBER_CARD, menu_draw_member_card_entry, READS_NOTHING);
        break;
    case EQUIPMENT_GOES_BACK_TO_THE_MAIN_MENU: menu_go_on_in(s, MAIN_MENU, menu_back_to_the_main_menu_entry, READS_NOTHING); break;
    case EQUIPMENT_POINTS_AT_A_SLOT: menu_go_on_in(s, POINT_AT_SLOT, menu_point_at_equipment_slot, READS_NOTHING); break;
    case EQUIPMENT_GOES_ON_TO_THE_LIST: menu_go_on_in(s, ON_TO_THE_LIST, menu_open_equipment_list, READS_NOTHING); break;
    case EQUIPMENT_REFUSES: TIMED_AS(state_of(s), REFUSAL_SOUND, menu_play_refusal_sound(s)); break;
    case EQUIPMENT_GOES_BACK_TO_THE_MEMBERS: menu_go_on_in(s, BACK_TO_MEMBERS, menu_back_to_equipment_members, READS_NOTHING); break;
    case EQUIPMENT_TICKS_ON_THE_LIST:
        menu_restore_p(s);
        TIMED_AS(state_of(s), CURSOR_SOUND, menu_play_cursor_sound(s));
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

// $C2:A6F0
/* What the member has on, from X; it leaves the row past the last in A. */
enum { EQUIPPED_ROWS_BYTES = 4 * 0x80 };

static void charge_member_step_tested(Snes *s)
{
    GameState *g = state_of(s);
    charge_machine_accesses(g, 1 + !menu_pushed(g, MENU_PUSHED_RIGHT));
}

static void charge_slot_step_tested(Snes *s, SlotChoice c)
{
    GameState *g = state_of(s);
    charge_machine_accesses(g, !menu_pushed(g, MENU_PUSHED_LEFT) +
                              (c == SLOT_LEFT_FOR_THE_MEMBERS || c == SLOT_CHOSEN));
}

/* The slot's label copied to the other layer by a block move, which leaves
 * A at $FFFF and the indexes past the copy. */
enum { LABEL_COPY_FROM_END = 0x30C0, LABEL_COPY_TO_END = 0x38C0 };

/* The description ($C2:F2F3) leaves A clear where it started typing, and
 * the original's last load before the cursor's sound is the cursor's place
 * on the list. */
static void charge_list_opened(Snes *s, bool typing_started)
{
    charge_machine_accesses(state_of(s), 1 + (typing_started ? 1 : 0));
}

/* The panel's tests before the card: the cursor's row compared with the
 * active members (before it was brought onto one), the scroll's furthest
 * worked out without overflowing, and the clock's digits' place loaded. */
enum { CLOCK_DIGITS = 0x0402 };

static void charge_row_noted(Snes *s, uint8_t row)
{
    (void)row;
    GameState *g = state_of(s);
    if (menu_list_scrolls_a_row(g))
        charge_machine_accesses(g, 1);
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

static bool list_is_drawable_from(const GameState *g, unsigned place);

/* Paging redraws eight rows; scrolling draws just the row coming into view.
 * Validate the names those operations consume, leaving stale rows beyond
 * the end of the list alone. */
static bool list_move_is_drawable(const GameState *g, uint8_t row)
{
    uint8_t top = menu_equip_list_top(g), to = menu_page_destination(g, top);
    if (to != top)
        return list_is_drawable_from(g, to);
    if (!menu_equip_list_slide_brings_a_row(g, row))
        return true;
    uint8_t shifted = menu_pushed(g, MENU_PUSHED_DOWN) ? (uint8_t)(top + 1) : (uint8_t)(top - 1);
    uint8_t place = (uint8_t)(shifted + row);
    return place < MENU_LIST_ITEMS_COUNT && menu_list_items(g, place) < MENU_ITEM_NAME_PLACES_COUNT;
}

static bool list_is_pointable(const GameState *g)
{
    uint8_t row = (uint8_t)(menu_cursor_index(g) - FIRST_LIST_PLACE);
    return menu_equip_choices(g) <= MENU_LIST_ITEMS_COUNT && row < LIST_ROWS
           && menu_equip_list_top(g) + LIST_ROWS + 1u <= MENU_LIST_ITEMS_COUNT && queue_takes(g, 2)
           && list_move_is_drawable(g, row);
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

static bool weapon_is_forgettable(const GameState *g) { return menu_member_row(g) < MENU_MEMBERS_COUNT; }

bool menu_equipment_panel_shows(Snes *s) { return on_the_screen(s) && peek(s, menu_equipment_panel_can_show); }

/* The dimmed colours go whole into the palette where Y points, from its
 * first colour on. */
enum { DIMMED_BYTES = 24 * 2 };

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
 * one of the list's, every item there one of the game's. The drawing stops
 * at the list's end, the empty item, and draws its blank row for the rest,
 * so what lies past the end (an earlier, longer list's leftovers, or
 * whatever the inventory screen left there) is never looked at. */
static bool list_is_drawable_from(const GameState *g, unsigned place)
{
    if (menu_equip_slot(g) >= SLOT_PLACES || menu_equip_choices(g) > MENU_LIST_ITEMS_COUNT)
        return false;
    for (unsigned row = 0; row < LIST_ROWS_DRAWN; row++, place++) {
        if (place >= MENU_LIST_ITEMS_COUNT)
            return false;
        uint8_t item = menu_list_items(g, place);
        if (item == 0)
            return true;
        if (item >= MENU_ITEM_NAME_PLACES_COUNT)
            return false;
    }
    return true;
}

static bool list_is_drawable(const GameState *g)
{
    return list_is_drawable_from(g, menu_equip_list_top(g));
}

/* The list made afresh and drawn from its top: the list must have room for
 * what fits, and every item a slot takes has a name. */
static bool list_can_be_made(const GameState *g)
{
    return menu_list_what_fits_can_be_made(g)
        && menu_slot_past_items(g, menu_equip_slot(g)) <= MENU_ITEM_NAME_PLACES_COUNT;
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

bool menu_equipment_members_come_back(Snes *s) { return on_the_screen(s) && peek(s, members_come_back); }
bool menu_equipment_list_opens(Snes *s) { return on_the_screen(s) && peek(s, list_opens); }
bool menu_equipment_list_can_be_made(Snes *s) { return menu_on_page_work_ram(s) && peek(s, list_can_be_made); }
bool menu_equipment_list_slides(Snes *s) { return menu_on_page_work_ram(s) && peek(s, slots_show); }
bool menu_equipment_slots_come_back(Snes *s) { return on_the_screen(s) && peek(s, slots_come_back); }
bool menu_equipment_slots_window_slides(Snes *s) { return on_the_screen(s) && peek(s, window_slides); }

/* The guards' state halves, for the program as C. */
bool menu_equip_slots_window_can_slide(const GameState *g) { return window_slides(g); }
bool menu_equip_can_point_at_a_slot(const GameState *g) { return slot_is_pointable(g); }
bool menu_equip_members_can_come_back(const GameState *g) { return members_come_back(g); }
bool menu_equip_list_can_open(const GameState *g) { return list_opens(g); }
bool menu_equip_list_can_be_made(const GameState *g) { return list_can_be_made(g); }
bool menu_equip_list_can_slide(const GameState *g) { return slots_show(g); }
bool menu_equip_list_can_be_pointed_along(const GameState *g) { return list_is_pointable(g); }
bool menu_equip_slots_can_come_back(const GameState *g) { return slots_come_back(g); }
bool menu_equip_can_choose_an_item(const GameState *g) { return item_step_runs(g) && menu_cursor_can_move(g); }
bool menu_equip_weapon_can_be_forgotten(const GameState *g) { return weapon_is_forgettable(g); }
