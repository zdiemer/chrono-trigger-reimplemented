/* The items screen on the original's machine: its checked legacy adapters, the
 * bridges that run what game/menu/item_screen.c decides between the calls to
 * what is still the original's, and their guards.
 *
 * Each step reads the cursor, asks item_screen.c what the frame's input
 * does, and carries it out: the sounds are siblings, run as the original's
 * JSRs reach them (menu_run); the list's frame is C, called as C; a step
 * that waits for the next frame waits in the menus' task switch, parked
 * until its task goes on (menu_wait_a_frame_from). Where a step jumps on to
 * the main menu, it calls that continuation as C behind its guard.
 *
 * The routines a step reaches with a JSR and that call out themselves (the
 * pointing along the list and its description, the use of an item and its
 * handlers, the member window, a tab's member) are entries of their own,
 * run through menu_run so that the original's frames stand under their calls
 * out as the original has them: shadow mode compares the stack there, the
 * flags each of them saves as it starts among it. So before running one, and
 * before a sound (which saves them too), a bridge leaves the flags the
 * original's own tests left, worked out from the state (the leaves at the end
 * of the file): scaffolding, which the native build compiles out. */
#include "menu/item_screen.h"

#include "charge.h"
#include "holds.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/description.h"
#include "menu/display.h"
#include "menu/figures.h"
#include "menu/healing.h"
#include "menu/held_cursors.h"
#include "menu/items.h"
#include "menu/lists.h"
#include "menu/main_menu.h"
#include "menu/members.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/sprites.h"
#include "menu/tech_screen.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The calls out ----------------------------------------------------------------------------- */

/* The original's routines the steps call, by what they do. */
enum {
    MAIN_MENU = 0xC29A98,            /* back to the main menu */
};

/* What they read (tools/dontcare_check.py checks each): most nothing but the
 * page, the data bank and memory; the stop of tasks A, the pointing at a
 * member A (its sound's). */
enum {
    READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_A = DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
};

static bool tick(Snes *s, uint32_t site) { return menu_run(s, site, 0xC2EAC2, menu_play_cursor_sound); }
static bool refuse(Snes *s, uint32_t site) { return menu_run(s, site, 0xC2EACC, menu_play_refusal_sound); }

static void charge_member_choice_started(Snes *s, ItemUseKind kind);
static void charge_member_looked_at(Snes *s);
static void charge_ends_compared(Snes *s);
static void charge_slide_frames(Snes *s, bool took_frames);
static void charge_use_found(Snes *s);

/* The tasks a step stops: the second cursor's (the fourth's bit). */
enum { HELD_CURSOR_TASK = 0x40 };

/* ---- What the steps ask, on the machine -------------------------------------------------------- */

/* The original's call at each place the steps tick or wait from. */
static const uint32_t TICKS_AT[] = {
    [ITEMS_AT_THE_OPENING] = 0xC2AA40,       [ITEMS_AT_A_COMMAND_POINTED] = 0xC2AA5A,
    [ITEMS_AT_A_COMMAND_CHOSEN] = 0xC2AA5E,  [ITEMS_AT_AN_ITEM_PICKED_UP] = 0xC2AA99,
    [ITEMS_AT_THE_LIST_LEFT] = 0xC2AAAD,     [ITEMS_AT_A_LISTED_ITEM_POINTED] = 0xC2AC65,
    [ITEMS_AT_THE_PICKED_ITEM_PUT] = 0xC2AAD5,       [ITEMS_AT_THE_MEMBER_WINDOW_LEFT] = 0xC2B1C9,
    [ITEMS_AT_A_TAB_MEMBER_POINTED] = 0xC2B288,      [ITEMS_AT_THE_USERS_CLOSED] = 0xC2AC2A,
    [ITEMS_AT_THE_SLIDE] = 0xC2ACB9,
};

static void read_cursor_on_the_machine(void *ctx) { menu_step_reads_the_cursor(ctx); }
static void read_cursor_held_on_the_machine(void *ctx) { menu_step_holds_and_reads_the_cursor(ctx); }
static bool tick_on_the_machine(void *ctx, ItemsPlace at) { return tick(ctx, TICKS_AT[at]); }
/* The steps wait from the sort, as an item in hand is put back, in the
 * member windows and as one opens; and buzz from the member windows. */
static const struct {
    uint32_t site;
} WAITS_AT[] = {
    [ITEMS_AT_THE_SORT] = { 0xC2AB4D },          [ITEMS_AT_THE_LIST_PUT_BACK] = { 0xC2AAFF },
    [ITEMS_AT_THE_TONIC_S_MEMBERS] = { 0xC2B1A5 }, [ITEMS_AT_THE_TAB_S_MEMBERS] = { 0xC2B228 },
    [ITEMS_AT_THE_MEMBER_WINDOW_OPENING] = { 0xC2AF5C }, [ITEMS_AT_THE_MEMBER_WINDOW_DRAWN] = { 0xC2AF6D },
    [ITEMS_AT_A_SLIDE_FRAME] = { 0xC2ACDF },
};
static const uint32_t REFUSALS_AT[] = { [ITEMS_AT_THE_TONIC_S_MEMBERS] = 0xC2B1BA, [ITEMS_AT_THE_TAB_S_MEMBERS] = 0xC2B23D };
/* The sprites' blocks a frame the tab's pointing leaves in A. */
enum { BLOCKS_A_FRAME = 2 };

static bool wait_on_the_machine(void *ctx, ItemsPlace at)
{
    Snes *s = ctx;
    if (m_runs_retired_as_c(s) && (at == ITEMS_AT_THE_TONIC_S_MEMBERS || at == ITEMS_AT_THE_TAB_S_MEMBERS)) {
        (void)menu_run_called(s, WAITS_AT[at].site, MENU_TASK_WAITS_A_FRAME,
                              MENU_TASK_WAIT_READS_NOTHING, menu_task_waits_a_frame);
        return false;
    }
    return menu_wait_a_frame_from(s, WAITS_AT[at].site);
}

static bool describe_on_the_machine(void *ctx) { return menu_run(ctx, 0xC2AC59, 0xC2AD2E, menu_describe_listed_item); }

/* After the list's slide, the item on the cursor pointed at, where the
 * original calls the pointing. */
static bool point_after_sliding_on_the_machine(void *ctx)
{
    return menu_run(ctx, 0xC2ACEC, 0xC2AC59, menu_point_at_listed_item);
}

static bool back_to_the_main_menu_on_the_machine(void *ctx)
{
    menu_go_on_in(ctx, MAIN_MENU, menu_back_to_the_main_menu_entry, READS_NOTHING);
    return false;
}

/* The flags the original's tests leave, and the P the set-up saves and the
 * widths it and the pointing set. */
static void note_on_the_machine(void *ctx, ItemsNote note, uint8_t value)
{
    Snes *s = ctx;
    (void)value;
    switch (note) {
    case ITEMS_NOTE_OPENED:
        (void)0;
        menu_save_p(s);
        menu_set_widths(s, true, m_x8(s));
        break;
    case ITEMS_NOTE_LIST_FRAMED: charge_machine_accesses(state_of(s), 2); break;
    case ITEMS_NOTE_SET_UP: menu_restore_p(s); break;
    case ITEMS_NOTE_BUTTONS_TESTED: charge_machine_accesses(state_of(s), 1); break;
    case ITEMS_NOTE_CURSOR_COMPARED: charge_machine_accesses(state_of(s), 3); break;
    case ITEMS_NOTE_PICKED_UP: menu_set_widths(s, false, false); break;
    case ITEMS_NOTE_POINTING: menu_set_widths(s, true, m_x8(s)); break;
    case ITEMS_NOTE_PAGING_ANSWERED: (void)0; break;
    case ITEMS_NOTE_ROWS_REDRAWN: charge_machine_accesses(state_of(s), 2); break;
    case ITEMS_NOTE_PLACE_COMPARED: charge_machine_accesses(state_of(s), 3); break;
    case ITEMS_NOTE_ROWS_QUEUED: (void)0; break;
    case ITEMS_NOTE_CURSOR_KEPT: menu_charge_cursor_kept_on_the_list(s); break;
    case ITEMS_NOTE_ENDS_COMPARED: charge_ends_compared(s); break;
    case ITEMS_NOTE_SLIDE_TICKED:
    case ITEMS_NOTE_SLIDE_DONE: menu_set_widths(s, false, false); break;
    case ITEMS_NOTE_SLIDE_FRAMES_DONE:
        menu_set_widths(s, true, false);
        charge_slide_frames(s, value != 0);
        break;
    case ITEMS_NOTE_EFFECT_TESTED: (void)0; break;
    case ITEMS_NOTE_USE_FOUND: charge_use_found(s); break;
    case ITEMS_NOTE_MEMBER_CHOICE_STARTED: charge_member_choice_started(s, (ItemUseKind)value); break;
    case ITEMS_NOTE_HELP_LOADED: charge_machine_accesses(state_of(s), 1); break;
    case ITEMS_NOTE_HELD_CURSOR_STOPPED:
        (void)0;
        /* Clearing the closed window returns with word-wide registers. */
        menu_set_widths(s, false, false);
        break;
    case ITEMS_NOTE_FIGURES_NOTED: (void)0; break;
    case ITEMS_NOTE_MEMBER_COMPARED: charge_machine_accesses(state_of(s), 1); break;
    case ITEMS_NOTE_MEMBER_LOOKED_AT: charge_member_looked_at(s); break;
    case ITEMS_NOTE_BLOCKS_SET: (void)0; break;
    case ITEMS_NOTE_ONE_TAKEN: (void)0; break;
    case ITEMS_NOTE_USERS_BUTTONS_LOADED:
        charge_machine_accesses(state_of(s), 1);
        break;
    }
}

static bool refuse_on_the_machine(void *ctx, ItemsPlace at) { return refuse(ctx, REFUSALS_AT[at]); }

/* The widths the original's code sets at those places. */
static void registers_on_the_machine(void *ctx, ItemsPlace at)
{
    Snes *s = ctx;
    switch (at) {
    case ITEMS_AT_THE_TAB_S_MEMBERS: menu_set_widths(s, true, false); break;
    default: menu_set_widths(s, true, true); break;
    }
}

/* The routines the use reaches, each run where the original calls it. */
static const struct {
    uint32_t site, routine;
    void (*entry)(Snes *s);
} RUNS[] = {
    [ITEMS_USES_THE_ITEM] = { 0xC2AB3E, 0xC2B0AB, menu_use_item },
    [ITEMS_USES_A_RESTORING_ITEM] = { 0xC2B0E9, 0xC2B17F, menu_use_restoring_item },
    [ITEMS_USES_AN_ITEM_BY_CHANCE] = { 0xC2B0E9, 0xC2B107, menu_use_item_by_chance },
    [ITEMS_RESTS_THE_PARTY] = { 0xC2B0E9, 0xC2B1F8, menu_rest_the_party },
    [ITEMS_USES_A_TAB] = { 0xC2B0E9, 0xC2B200, menu_use_tab },
    [ITEMS_OPENS_THE_MEMBER_WINDOW_FOR_A_TONIC] = { 0xC2B198, 0xC2AF42, menu_open_member_window },
    [ITEMS_OPENS_THE_MEMBER_WINDOW_FOR_A_TAB] = { 0xC2B225, 0xC2AF42, menu_open_member_window },
    [ITEMS_POINTS_AT_A_MEMBER_TO_RESTORE] = { 0xC2B1AB, 0xC2B83E, menu_point_at_member_to_restore_entry },
    [ITEMS_POINTS_AT_A_TAB_MEMBER] = { 0xC2B22E, 0xC2B24A, menu_point_at_tab_member },
    [ITEMS_LOOKS_AT_THE_TAB_MEMBER] = { 0xC2B26B, 0xC2B275, menu_look_at_tab_member },
    [ITEMS_HEALS_AND_SHOWS] = { 0xC2B1BF, 0xC28F55, menu_heal_and_show_entry },
    [ITEMS_RAISES_THE_FIGURE] = { 0xC2B242, 0xC2B2A7, menu_raise_figure },
    [ITEMS_SHOWS_THE_RAISE_S_AMOUNTS] = { 0xC2B2D6, 0xC28F6C, menu_show_amounts_entry },
    [ITEMS_USES_ONE_MORE_TONIC] = { 0xC2B1C2, 0xC2B31C, menu_use_one_more },
    [ITEMS_USES_ONE_MORE_TAB] = { 0xC2B245, 0xC2B31C, menu_use_one_more },
};

static bool run_on_the_machine(void *ctx, ItemsRoutine r)
{
    /* Closing a member window leaves word-wide registers; the item-use
     * dispatcher restores its saved widths after the handler returns. */
    switch (r) {
    case ITEMS_USES_A_RESTORING_ITEM:
    case ITEMS_USES_AN_ITEM_BY_CHANCE:
    case ITEMS_RESTS_THE_PARTY:
    case ITEMS_USES_A_TAB:
        return menu_run_any_widths(ctx, RUNS[r].site, RUNS[r].routine, RUNS[r].entry);
    default: return menu_run(ctx, RUNS[r].site, RUNS[r].routine, RUNS[r].entry);
    }
}

static const ItemsSteps on_the_machine = {
    read_cursor_on_the_machine,    read_cursor_held_on_the_machine,
    tick_on_the_machine,
    wait_on_the_machine,           describe_on_the_machine,
    point_after_sliding_on_the_machine, back_to_the_main_menu_on_the_machine,
    note_on_the_machine,           refuse_on_the_machine,
    registers_on_the_machine,      run_on_the_machine,
};

static void step(Snes *s, bool (*run)(GameState *, Platform *, const ItemsSteps *, void *))
{
    if (run(state_of(s), m_platform(s), &on_the_machine, s))
        m_rts(s);
}

/* A routine that saves P and sets the widths as it starts, and puts P back
 * as it returns. */
static void saving_p(Snes *s, bool (*run)(GameState *, Platform *, const ItemsSteps *, void *))
{
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    if (!run(state_of(s), m_platform(s), &on_the_machine, s))
        return;
    menu_restore_p(s);
    m_rts(s);
}

/* ---- The steps --------------------------------------------------------------------------------- */

// $C2:AA40, $C2:ADD0
void menu_open_items_entry(Snes *s) { step(s, menu_open_items_with); }

// $C2:AA4A
void menu_choose_item_command_entry(Snes *s) { step(s, menu_choose_item_command_with); }

// $C2:AD2E
/* The item on the cursor noted and its description drawn. */
void menu_describe_listed_item(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    menu_items_describe_the_cursors_item(state_of(s));
    (void)0;
    menu_restore_p(s);
    m_rts(s);
}

// $C2:AC59
void menu_point_at_listed_item(Snes *s) { step(s, menu_point_at_listed_item_with); }

// $C2:AA86
void menu_choose_on_item_list_entry(Snes *s) { step(s, menu_choose_on_item_list_with); }

// $C2:AAC8
void menu_move_picked_item_entry(Snes *s) { step(s, menu_move_picked_item_with); }

/* ---- Sorting, and who can equip an item -------------------------------------------------------- */

// $C2:AB4A
void menu_sort_items_entry(Snes *s) { step(s, menu_sort_items_with); }

// $C2:AC1D
void menu_close_item_users_entry(Snes *s) { step(s, menu_close_item_users_with); }

/* ---- The main menu's panel --------------------------------------------------------------------- */

// $C2:AD50
void menu_show_items_panel(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, false, false);
    MainMenuOnTheMachine m = { s, 0, 0 };
    if (!menu_show_items_panel_with(state_of(s), m_platform(s), &menu_main_menu_on_the_machine, &m))
        return;
    menu_restore_p(s);
    m_rts(s);
}

/* ---- Using an item ----------------------------------------------------------------------------- */

/* The member window, opened: its window, the records reset, its lines, a
 * frame, the members' rows, the sprites held back while their task starts,
 * a frame, and let through. */
// $C2:AF42
void menu_open_member_window(Snes *s) { saving_p(s, menu_open_member_window_with); }

// $C2:B31C
void menu_use_one_more(Snes *s)
{
    menu_items_use_one_more(state_of(s), m_platform(s));
    (void)0;
    m_rts(s);
}

// $C2:B17F
void menu_use_restoring_item(Snes *s) { step(s, menu_use_restoring_item_with); }

// $C2:B107
void menu_use_item_by_chance(Snes *s) { step(s, menu_use_item_by_chance_with); }

// $C2:B1F8
void menu_rest_the_party(Snes *s) { step(s, menu_rest_the_party_with); }

// $C2:B275
void menu_look_at_tab_member(Snes *s) { step(s, menu_look_at_tab_member_with); }

// $C2:B24A
void menu_point_at_tab_member(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, true);
    if (!menu_point_at_tab_member_with(state_of(s), m_platform(s), &on_the_machine, s))
        return;
    menu_restore_p(s);
    m_rts(s);
}

// $C2:B2A7
void menu_raise_figure(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, false, false);
    if (!menu_raise_figure_with(state_of(s), m_platform(s), &on_the_machine, s))
        return;
    menu_restore_p(s);
    m_rts(s);
}

// $C2:B200
void menu_use_tab(Snes *s) { step(s, menu_use_tab_with); }

// $C2:B0AB
void menu_use_item(Snes *s) { saving_p(s, menu_use_item_with); }

/* The member choice waits for the player's next input without retaining
 * the screen step's C stack. Its return frames remain on the task stack. */
static void member_choice_continues(Snes *s, ItemUseKind kind)
{
    MenuItemChoiceStep step = menu_item_member_choice_frame_with(state_of(s), kind, &on_the_machine, s);
    if (step == MENU_ITEM_CHOICE_WAITING) {
        (void)wait_on_the_machine(s, kind == USE_RAISES_A_FIGURE ? ITEMS_AT_THE_TAB_S_MEMBERS : ITEMS_AT_THE_TONIC_S_MEMBERS);
    } else if (step == MENU_ITEM_CHOICE_CLOSED) {
        m_rts(s);
        native_hold(s, HOLD_MENU_TASKS);
    }
}

// $C2:B1A8
void menu_tonic_member_choice_continues(Snes *s) { member_choice_continues(s, USE_RESTORES); }
// $C2:B22B
void menu_tab_member_choice_continues(Snes *s) { member_choice_continues(s, USE_RAISES_A_FIGURE); }

// $C2:B0EC
void menu_item_use_finishes(Snes *s)
{
    menu_restore_p(s);
    m_rts(s);
    native_hold(s, HOLD_MENU_TASKS);
}

// $C2:AB41
void menu_picked_item_use_finishes(Snes *s)
{
    GameState *g = state_of(s);
    menu_items_back_to_the_list_unchanged(g);
    TIMED_AS(g, 0xC28255, menu_stop_tasks(g, HELD_CURSOR_TASK));
    m_rts(s);
    native_hold(s, HOLD_MENU_TASKS);
}

static void charge_member_looked_at(Snes *s)
{
    GameState *g = state_of(s);
    (void)menu_member_can_be_loaded(g, menu_use_member(g));
}

static void charge_ends_compared(Snes *s)
{
    GameState *g = state_of(s);
    if (menu_list_scrolls_a_row(g))
        charge_machine_accesses(g, 2);
}

static void charge_slide_frames(Snes *s, bool took_frames)
{
    if (took_frames)
        charge_machine_accesses(state_of(s), 4);
}

static void charge_member_choice_started(Snes *s, ItemUseKind kind)
{
    if (kind == USE_RESTORES || kind == USE_RAISES_A_FIGURE)
        charge_machine_accesses(state_of(s), 1);
}

/* An item's use found: the figures' first byte shifted up to its first set
 * bit of five (or past the five, counted out). */
enum { USE_BITS = 5, TOP_BIT = 0x80 };

static void charge_use_found(Snes *s)
{
    charge_machine_accesses(state_of(s), 2);
    charge_machine_rom_accesses(state_of(s), 1);
}

/* ---- The guards -------------------------------------------------------------------------------- */

static bool queue_takes(const GameState *g, unsigned uploads) { return menu_upload_queue_takes(g, uploads); }

static bool on_the_screen(Snes *s) { return menu_on_page_work_ram(s) && menu_sounds_on_page(s); }

static bool peek(Snes *s, bool (*test)(const GameState *))
{
    vars_peek(true);
    bool ok = test(state_of(s));
    vars_peek(false);
    return ok;
}

bool menu_items_screen_opens(Snes *s) { return on_the_screen(s) && peek(s, menu_items_can_open); }

bool menu_item_command_step_runs(Snes *s) { return on_the_screen(s) && menu_cursor_is_movable(s); }

static bool list_step_runs(const GameState *g) { return menu_items_can_point_along_the_list(g); }

bool menu_item_list_step_runs(Snes *s) { return on_the_screen(s) && menu_cursor_is_movable(s) && peek(s, list_step_runs); }

static bool move_step_runs(const GameState *g)
{
    return list_step_runs(g) && menu_item_picked(g) < MENU_ITEMS;
}

bool menu_item_move_step_runs(Snes *s) { return on_the_screen(s) && menu_cursor_is_movable(s) && peek(s, move_step_runs); }
bool menu_items_can_move_the_picked(const GameState *g) { return move_step_runs(g) && menu_cursor_can_move(g); }

bool menu_items_sort_step_runs(Snes *s) { return on_the_screen(s) && peek(s, menu_items_can_sort); }

bool menu_item_users_step_runs(Snes *s) { return on_the_screen(s) && menu_cursor_is_movable(s); }

bool menu_items_panel_shows(Snes *s) { return on_the_screen(s) && peek(s, menu_items_panel_can_show); }
bool menu_items_panel_goes(Snes *s) { return menu_on_page_work_ram(s); }

bool menu_listed_item_is_pointable(Snes *s) { return on_the_screen(s) && peek(s, list_step_runs); }

/* The members an item is used on: as many active as the window has rows,
 * each a character whose row and sprite can be drawn; a tab's figure one of
 * the tables'. */
enum { WINDOW_ROWS = 3, FIGURES = 7 };

static bool members_are_drawable(const GameState *g)
{
    uint8_t active = menu_active_members(g);
    if (active == 0 || active > WINDOW_ROWS || menu_active_members(g) > menu_members_reachable(g))
        return false;
    for (uint8_t member = 0; member < active; member++)
        if (!menu_member_card_can_be_drawn_for(g, member))
            return false;
    return menu_window_list_drawable(g, MENU_WINDOWS_ITEMS_MEMBERS);
}

static bool use_runs(const GameState *g)
{
    return queue_takes(g, 4) && menu_item_picked(g) < MENU_ITEMS && members_are_drawable(g) && menu_items_list_is_pointable(g);
}

bool menu_item_use_runs(Snes *s) { return on_the_screen(s) && peek(s, use_runs); }
bool menu_items_use_can_run(const GameState *g) { return use_runs(g); }

/* The member window and one more used run with the cursor on its rows. */
static bool member_window_runs(const GameState *g)
{
    return queue_takes(g, 4) && menu_item_picked(g) < MENU_ITEMS && members_are_drawable(g)
           && menu_items_rows_can_be_drawn(g) && menu_item_list_row(g) < MENU_ITEMS_ROWS;
}

bool menu_member_window_opens(Snes *s) { return on_the_screen(s) && peek(s, member_window_runs); }
bool menu_items_member_window_can_run(const GameState *g) { return member_window_runs(g); }

static bool tab_member_is_pointable(const GameState *g)
{
    return members_are_drawable(g) && menu_use_effect(g) < FIGURES && menu_use_first_member(g) <= menu_cursor_index(g);
}

bool menu_tab_member_is_pointable(Snes *s) { return on_the_screen(s) && peek(s, tab_member_is_pointable); }
bool menu_items_tab_member_can_be_pointed(const GameState *g) { return tab_member_is_pointable(g); }

static bool figure_is_raisable(const GameState *g)
{
    return members_are_drawable(g) && menu_use_effect(g) < FIGURES && menu_use_member(g) < menu_active_members(g);
}

bool menu_figure_is_raisable(Snes *s) { return menu_on_page_work_ram(s) && peek(s, figure_is_raisable); }
bool menu_items_figure_can_be_raised(const GameState *g) { return figure_is_raisable(g); }
bool menu_one_more_is_usable(Snes *s) { return on_the_screen(s) && peek(s, member_window_runs); }
