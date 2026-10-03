/* The items screen on the original's machine: its registered entries, the
 * bridges that run what game/menu/item_screen.c decides between the calls to
 * what is still the original's, and their guards.
 *
 * Each step reads the cursor, asks item_screen.c what the frame's input
 * does, and carries it out: the sounds are siblings, run as the original's
 * JSRs reach them (menu_run); the setting aside of the party's items, the
 * list's frame, paging and sliding, the scroll bar's and the second cursor's
 * tasks, the description, the restoring, the frames the screen waits for are
 * the original's, called out to. Where a step jumps on to another routine
 * (the second cursor's task, the description, the main menu, the stop of a
 * task), it stops there.
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
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/display.h"
#include "menu/items.h"
#include "menu/lists.h"
#include "menu/members.h"
#include "menu/tech_screen.h"
#include "menu/menu_data.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The calls out ----------------------------------------------------------------------------- */

/* The original's routines the steps call, by what they do. */
enum {
    YIELD = 0xC2821E,                /* the task waits for the next frame */
    STOP_TASKS = 0xC28255,           /* the tasks in A stopped */
    SET_ITEMS_ASIDE = 0xC290FC,      /* the party's items set aside, where the menus keep them so */
    PUT_ITEMS_BACK = 0xC29121,       /* ... and put back */
    FRAME_THE_LIST = 0xC2EA81,       /* a scrolling list's frame laid out: Y places, rows shown */
    START_SCROLL_BAR = 0xC28B93,     /* the scroll bar's task started */
    LOAD_SPRITE_COLOURS = 0xC2F5A7,  /* the members' sprites' colours loaded */
    TURN_PAGE = 0xC293BF,            /* a list's paging with the shoulder assignments */
    MOVE_IN_LIST = 0xC2EA53,         /* the cursor kept within a list's rows */
    START_LIST_SLIDE = 0xC2EB9B,     /* a list's slide started */
    LIST_SLIDE_FRAME = 0xC2EB1F,     /* a frame of a list's slide, and the wait for the next */
    DESCRIBE = 0xC2F2F3,             /* the description window drawn for the item in A */
    HOLD_A_CURSOR = 0xC289E4,        /* the second cursor's task started */
    TAKE = 0xC287D5,                 /* menu_item_operand taken from the party's items */
    RESET_RECORDS = 0xC2F566,        /* the members' sprite records reset */
    START_RECORDS = 0xC2F643,        /* the records' task started */
    CURSOR_ON_EVERY_MEMBER = 0xC28AD5, /* the cursors' task for an item used on every member */
    RESTORE = 0xC28F55,              /* the tonic's HP and MP restored, shown, and waited out */
    SHOW_AMOUNTS = 0xC28F6C,         /* the amounts a member gets shown, and waited out */
    WORK_OUT_FIGURES = 0xC29137,     /* the member's figures worked out again */
    MAIN_MENU = 0xC29A98,            /* back to the main menu */
};

/* What they read (tools/dontcare_check.py checks each): most nothing but the
 * page, the data bank and memory; the list's frame Y, the description and
 * the stop of tasks A, the pointing at a member A (its sound's). */
enum {
    READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_Y = DC_A | DC_X | DC_N | DC_V | DC_Z | DC_C,
    READS_A_LOW = DC_AH | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    READS_A = DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
};

static bool call(Snes *s, uint32_t site, uint32_t routine) { return menu_call(s, site, routine, READS_NOTHING); }
static bool yield(Snes *s, uint32_t site) { return menu_call(s, site, YIELD, READS_NOTHING); }
static bool tick(Snes *s, uint32_t site) { return menu_run(s, site, 0xC2EAC2, menu_play_cursor_sound); }
static bool refuse(Snes *s, uint32_t site) { return menu_run(s, site, 0xC2EACC, menu_play_refusal_sound); }

/* The description drawn for an item, A holding it. */
static bool describe(Snes *s, uint32_t site, uint8_t item)
{
    m_set_a8(s, item);
    return menu_call(s, site, DESCRIBE, READS_A_LOW);
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

static void leave_buttons_tested(Snes *s);
static void leave_paging_answered(Snes *s);
static void leave_member_choice_started(Snes *s, ItemUseKind kind);
static void leave_member_looked_at(Snes *s);
static void leave_cursor_compared(Snes *s);
static void leave_load(Snes *s, uint8_t v);
static void leave_rows_queued(Snes *s, uint16_t record);
static void leave_place_compared(Snes *s);
static void leave_ends_compared(Snes *s);
static void leave_rows_redrawn(Snes *s);
static void leave_slide_frames(Snes *s, bool took_frames);
static void leave_effect_tested(Snes *s);
static void leave_use_found(Snes *s);


/* The tasks a step stops: the second cursor's (the fourth's bit). */
enum { HELD_CURSOR_TASK = 0x40 };

static void stop_tasks(Snes *s, uint8_t tasks)
{
    m_set_a8(s, tasks);
    m_stop_at_own(s, STOP_TASKS, READS_A);
}

/* ---- Opening the screen ------------------------------------------------------------------------ */

// $C2:AA40, $C2:ADD0
/* A tick, on to the list, and the screen set up: the party's items set
 * aside first, the list's frame, the scroll bar's task and the sprites'
 * colours between its parts. */
void menu_open_items(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    if (!tick(s, 0xC2AA40))
        return;
    menu_items_open(g);
    ORIGINAL_EFFECT(leave_load(s, ITEMS_ON_THE_LIST));
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    if (!call(s, 0xC2ADD3, SET_ITEMS_ASIDE))
        return;
    menu_items_lay_out(g, pf);
    s->cpu.y = MENU_ITEMS;
    if (!menu_call(s, 0xC2ADF9, FRAME_THE_LIST, READS_Y))
        return;
    menu_items_show_bar_at_the_list(g);
    if (!call(s, 0xC2AE02, START_SCROLL_BAR))
        return;
    menu_items_put_the_cursor_back(g);
    if (!call(s, 0xC2AE0A, LOAD_SPRITE_COLOURS))
        return;
    menu_items_show_screen(g);
    menu_restore_p(s);
    m_rts(s);
}

/* ---- The commands ------------------------------------------------------------------------------ */

// $C2:AA4A
/* A command chosen: a tick, then the list or the sort; going back, the main
 * menu; the cursor moved, a tick. */
void menu_choose_item_command(Snes *s)
{
    GameState *g = state_of(s);
    read_cursor(s);
    ItemsCommandChoice c = menu_items_choose_command(g);
    ORIGINAL_EFFECT(leave_buttons_tested(s));
    switch (c) {
    case COMMANDS_LEFT: m_stop_at(s, MAIN_MENU, READS_NOTHING); return;
    case COMMAND_POINTED_AT:
        ORIGINAL_EFFECT(leave_cursor_compared(s));
        if (menu_cursor_frame_start(g) != menu_cursor_index(g) && !tick(s, 0xC2AA5A))
            return;
        m_rts(s);
        return;
    case COMMAND_LIST_CHOSEN:
    case COMMAND_SORT_CHOSEN:
    case COMMAND_NOTHING_CHOSEN:
        if (!tick(s, 0xC2AA5E))
            return;
        menu_items_take_command(g, c);
        m_rts(s);
        return;
    }
}

/* ---- Pointing along the list ------------------------------------------------------------------- */

// $C2:AD2E
/* The item on the cursor noted and its description drawn. */
void menu_describe_listed_item(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    uint8_t item = menu_items_note_the_cursors_item(g);
    if (!describe(s, 0xC2AD4B, item))
        return;
    menu_restore_p(s);
    m_rts(s);
}

/* The item described, the cursor's row kept, a tick where the cursor moved,
 * and the list queued. */
static void point_at_listed_item(Snes *s)
{
    GameState *g = state_of(s);
    if (!menu_run(s, 0xC2AC59, 0xC2AD2E, menu_describe_listed_item))
        return;
    bool moved = menu_items_keep_the_cursors_row(g);
    ORIGINAL_EFFECT(leave_place_compared(s));
    if (moved && !tick(s, 0xC2AC65))
        return;
    menu_items_queue_rows(g);
    ORIGINAL_EFFECT(leave_rows_queued(s, MENU_UPLOAD_FRONT_LAYER));
    m_rts(s);
}

// $C2:AC59
void menu_point_at_listed_item(Snes *s) { point_at_listed_item(s); }

/* The list's slide, a row in: started (the original's), the row brought in
 * and drawn, a tick, the frames (each the original's), then the item on the
 * cursor described and the rows moved in their buffer. It leaves a 16-bit
 * accumulator. */
static void slide_the_list(Snes *s)
{
    GameState *g = state_of(s);
    menu_items_set_up_slide(g);
    if (!call(s, 0xC2AC8A, START_LIST_SLIDE))
        return;
    menu_items_bring_in_a_row(g);
    ORIGINAL_EFFECT(leave_rows_queued(s, MENU_UPLOAD_FRONT_LAYER));
    if (!tick(s, 0xC2ACB9))
        return;
    menu_set_widths(s, false, false);
    bool took_frames = menu_items_slide_takes_frames(g);
    if (took_frames)
        while (menu_items_slide_a_frame(g))
            if (!call(s, 0xC2ACDF, LIST_SLIDE_FRAME))
                return;
    menu_set_widths(s, true, false);
    ORIGINAL_EFFECT(leave_slide_frames(s, took_frames));
    menu_items_end_slide_frames(g);
    if (!menu_run(s, 0xC2ACEC, 0xC2AC59, menu_point_at_listed_item))
        return;
    menu_items_finish_slide(g);
    menu_set_widths(s, false, false);
    m_rts(s);
}

// $C2:AC3A
/* The cursor moved along the list: a page turned and drawn again; or kept
 * within the rows, the list slid a row where the push brings one. */
static void point_along_the_list(Snes *s)
{
    GameState *g = state_of(s);
    menu_set_widths(s, true, m_x8(s));
    menu_items_note_top_for_paging(g);
    if (!call(s, 0xC2AC42, TURN_PAGE))
        return;
    ORIGINAL_EFFECT(leave_paging_answered(s));
    if (menu_items_paged(g)) {
        menu_items_turn_page(g);
        ORIGINAL_EFFECT(leave_rows_redrawn(s));
        bool moved = menu_items_keep_the_cursors_row(g);
        ORIGINAL_EFFECT(leave_place_compared(s));
        if (moved && !tick(s, 0xC2AC65))
            return;
        menu_items_queue_rows(g);
        ORIGINAL_EFFECT(leave_rows_queued(s, MENU_UPLOAD_FRONT_LAYER));
        m_rts(s);
        return;
    }
    if (!call(s, 0xC2AC54, MOVE_IN_LIST))
        return;
    if (menu_items_slide_brings_a_row(g)) {
        slide_the_list(s);
        return;
    }
    ORIGINAL_EFFECT(leave_ends_compared(s));
    point_at_listed_item(s);
}

/* ---- The list ---------------------------------------------------------------------------------- */

// $C2:AA86
/* An item picked up: a tick, and the second cursor's task. Back: a tick,
 * the commands, and the description cleared. */
void menu_choose_on_item_list(Snes *s)
{
    GameState *g = state_of(s);
    menu_items_put_nothing_in_hand(g);
    read_cursor(s);
    ItemsListChoice c = menu_items_choose_on_the_list(g);
    ORIGINAL_EFFECT(leave_buttons_tested(s));
    switch (c) {
    case LIST_ITEM_PICKED_UP:
        if (!tick(s, 0xC2AA99))
            return;
        menu_items_pick_up(g);
        m_stop_at(s, HOLD_A_CURSOR, READS_NOTHING);
        return;
    case LIST_LEFT:
        if (!tick(s, 0xC2AAAD))
            return;
        menu_items_back_to_the_commands(g);
        m_set_a8(s, 0);
        m_stop_at_own(s, DESCRIBE, READS_A);
        return;
    case LIST_POINTED_AT: point_along_the_list(s); return;
    }
}

// $C2:AAC8
/* With an item picked up: put down elsewhere (a tick, the two swapped) or
 * back, a frame, the list drawn again; used, its use. Then the second
 * cursor's task stopped. */
void menu_move_picked_item(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    read_cursor(s);
    ItemsMoveChoice c = menu_items_choose_where(g);
    ORIGINAL_EFFECT(leave_buttons_tested(s));
    switch (c) {
    case MOVE_POINTED_AT: point_along_the_list(s); return;
    case MOVE_PUT_DOWN:
        if (!tick(s, 0xC2AAD5))
            return;
        menu_items_swap_with_the_picked(g);
        /* fall through */
    case MOVE_LEFT:
        if (!yield(s, 0xC2AAFF))
            return;
        menu_items_back_to_the_list(g);
        ORIGINAL_EFFECT(leave_rows_queued(s, MENU_UPLOAD_FRONT_LAYER));
        stop_tasks(s, HELD_CURSOR_TASK);
        return;
    case MOVE_ON_ITSELF:
        if (!tick(s, 0xC2AAD5))
            return;
        switch (menu_items_use_of_the_picked(g)) {
        case USING_FROM_THE_MENU:
            ORIGINAL_EFFECT(leave_effect_tested(s));
            if (!menu_run(s, 0xC2AB3E, 0xC2B0AB, menu_use_item))
                return;
            menu_items_back_to_the_list_unchanged(g);
            break;
        case USING_SHOWS_USERS:
            menu_items_start_showing_users(g);
            TIMED_AS(g, 0xC2AE91, menu_items_show_users(g, pf));
            break;
        case USING_DOES_NOTHING: menu_items_back_to_the_list_unchanged(g); break;
        }
        stop_tasks(s, HELD_CURSOR_TASK);
        return;
    }
}

/* ---- Sorting, and who can equip an item -------------------------------------------------------- */

// $C2:AB4A
/* The sort, a frame, and the list drawn again. */
void menu_sort_items(Snes *s)
{
    GameState *g = state_of(s);
    TIMED_AS(g, 0xC2AB62, menu_items_sort(g));
    if (!yield(s, 0xC2AB4D))
        return;
    menu_items_after_sorting(g);
    m_rts(s);
}

// $C2:AC1D
/* Who can equip the item shown until a button: then a tick, and back. */
void menu_close_item_users(Snes *s)
{
    GameState *g = state_of(s);
    MenuCursorRead r = menu_note_cursor_read(g);
    (void)r;
    bool closed = menu_items_users_closed(g);
    ORIGINAL_EFFECT(menu_leave_cursor_read(s, r));
    if (closed) {
        ORIGINAL_EFFECT(leave_load(s, (uint8_t)(menu_cursor_buttons(g) & 0xC0)));
        if (!tick(s, 0xC2AC2A))
            return;
        menu_items_close_users(g);
    }
    m_rts(s);
}

/* ---- The main menu's panel --------------------------------------------------------------------- */

// $C2:AD50
/* The panel: its window and colours, a frame, the rows, a frame, the colour
 * math and the sprites. */
void menu_show_items_panel(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    menu_save_p(s);
    menu_set_widths(s, false, false);
    menu_items_panel_lay_out(g, pf);
    if (!yield(s, 0xC2AD7F))
        return;
    menu_items_panel_draw_rows(g);
    menu_set_widths(s, true, false);
    if (!yield(s, 0xC2ADA1))
        return;
    menu_items_panel_show(g, pf);
    menu_restore_p(s);
    m_rts(s);
}

// $C2:ADC3
/* The main menu leaving the items: the party's items put back, the main
 * menu's line effects. */
void menu_put_items_panel_away(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    if (!call(s, 0xC2ADC6, PUT_ITEMS_BACK))
        return;
    menu_items_panel_put_away(state_of(s));
    menu_restore_p(s);
    m_rts(s);
}

/* ---- Using an item ----------------------------------------------------------------------------- */

/* The member window, opened: its window, the records reset, its lines, a
 * frame, the members' rows, the sprites held back while their task starts,
 * a frame, and let through. */
// $C2:AF42
void menu_open_member_window(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    menu_items_open_member_window(g, pf);
    if (!call(s, 0xC2AF50, RESET_RECORDS))
        return;
    menu_items_lay_out_member_window_lines(g);
    if (!yield(s, 0xC2AF5C))
        return;
    menu_items_draw_members(g, pf);
    menu_items_hide_member_sprites(g);
    if (!call(s, 0xC2AF6A, START_RECORDS) || !yield(s, 0xC2AF6D))
        return;
    menu_items_show_member_sprites(g);
    menu_restore_p(s);
    m_rts(s);
}

// $C2:B31C
/* One more used: one taken from the party's items, the row drawn again,
 * the description of what is left, the members' rows again. */
void menu_use_one_more(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    menu_items_name_one_used(g);
    if (!call(s, 0xC2B331, TAKE))
        return;
    (void)menu_items_used_up(g);
    if (!describe(s, 0xC2B347, menu_items_picked_item(g)))
        return;
    menu_items_draw_members(g, pf);
    menu_items_after_one_used(g);
    m_rts(s);
}

/* The member window closed with a tick: the records reset, the second
 * cursor's task stopped, the window's layer cleared and queued. */
static void leave_member_window(Snes *s)
{
    GameState *g = state_of(s);
    if (!tick(s, 0xC2B1C9))
        return;
    menu_items_close_member_window(g);
    if (!call(s, 0xC2B1D9, RESET_RECORDS))
        return;
    m_set_a8(s, HELD_CURSOR_TASK);
    if (!menu_call(s, 0xC2B1DE, STOP_TASKS, READS_A))
        return;
    menu_items_clear_member_window(g);
    m_rts(s);
}

/* The member window an item is used from, the same for a tonic and a tab,
 * at the original's sites of each: opened, then a frame at a time the cursor
 * among the members (the tonic's pointing is the techs screen's, $C2:B83E;
 * the tab's looks at the member's figure), and choosing uses the item where
 * it helps -- a tonic's restoring (the original's), or a tab's raise --
 * then one more of it used; or refuses. Going back closes the window. */
enum { TONIC, TAB };
static const struct {
    uint32_t open, wait, point, point_routine, refuse;
} SITES[] = {
    [TONIC] = { 0xC2B198, 0xC2B1A5, 0xC2B1AB, 0xC2B83E, 0xC2B1BA },
    [TAB] = { 0xC2B225, 0xC2B228, 0xC2B22E, 0xC2B24A, 0xC2B23D },
};
static void (*const POINTING[])(Snes *) = { [TONIC] = menu_point_at_member_to_restore, [TAB] = menu_point_at_tab_member };

static bool use_on_the_member(Snes *s, ItemUseKind kind)
{
    if (kind == USE_RAISES_A_FIGURE)
        return menu_run(s, 0xC2B242, 0xC2B2A7, menu_raise_figure) && menu_run(s, 0xC2B245, 0xC2B31C, menu_use_one_more);
    return call(s, 0xC2B1BF, RESTORE) && menu_run(s, 0xC2B1C2, 0xC2B31C, menu_use_one_more);
}

static void choose_members_to_use_on(Snes *s, ItemUseKind kind)
{
    GameState *g = state_of(s);
    unsigned w = kind == USE_RAISES_A_FIGURE ? TAB : TONIC;
    menu_items_start_member_choice(g);
    ORIGINAL_EFFECT(leave_member_choice_started(s, kind));
    if (!menu_run(s, SITES[w].open, 0xC2AF42, menu_open_member_window))
        return;
    if (kind != USE_RAISES_A_FIGURE && menu_items_restores_every_member(g) && !call(s, 0xC2B1A2, CURSOR_ON_EVERY_MEMBER))
        return;
    for (;;) {
        if (!yield(s, SITES[w].wait))
            return;
        read_cursor(s);
        if (!menu_run(s, SITES[w].point, SITES[w].point_routine, POINTING[w]))
            return;
        ItemsMemberChoice c = menu_items_choose_member(g);
        ORIGINAL_EFFECT(leave_buttons_tested(s));
        if (c == MEMBERS_LEFT) {
            leave_member_window(s);
            return;
        }
        if (c == MEMBER_POINTED_AT_TO_USE_ON)
            continue;
        if (!menu_items_use_helps(g)) {
            ORIGINAL_EFFECT(leave_load(s, menu_use_helps(g)));
            if (!refuse(s, SITES[w].refuse))
                return;
            continue;
        }
        if (!use_on_the_member(s, kind))
            return;
    }
}

// $C2:B17F
/* A tonic: its strength, then the members. */
void menu_use_restoring_item(Snes *s)
{
    menu_items_set_restore_strength(state_of(s));
    choose_members_to_use_on(s, USE_RESTORES);
}

// $C2:B107
/* An item by chance: its outcome drawn, then as a tonic. */
void menu_use_item_by_chance(Snes *s)
{
    GameState *g = state_of(s);
    menu_items_draw_a_chance(g);
    menu_use_restoring_item(s);
}

// $C2:B1F8
/* The party's rest, then the members. */
void menu_rest_the_party(Snes *s)
{
    menu_items_set_party_rest(state_of(s));
    choose_members_to_use_on(s, USE_RESTS_THE_PARTY);
}

// $C2:B275
/* A tab's member pointed at, a tick where it is another, and looked at. */
void menu_look_at_tab_member(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t before = menu_use_member(g);
    bool another = menu_items_point_at_a_member(g);
    ORIGINAL_EFFECT(menu_leave_compare(s, menu_use_member(g), before));
    if (another && !tick(s, 0xC2B288))
        return;
    menu_items_look_at_the_member(g);
    ORIGINAL_EFFECT(leave_member_looked_at(s));
    m_rts(s);
}

// $C2:B24A
/* The cursor taken round the members, and the member looked at. The
 * sprites' count of blocks a frame is what it leaves in A. */
enum { BLOCKS_A_FRAME = 2 };

void menu_point_at_tab_member(Snes *s)
{
    menu_save_p(s);
    menu_set_widths(s, true, true);
    menu_items_take_the_cursor_round(state_of(s));
    if (!menu_run(s, 0xC2B26B, 0xC2B275, menu_look_at_tab_member))
        return;
    menu_items_after_looking(state_of(s));
    ORIGINAL_EFFECT(m_set_a8(s, BLOCKS_A_FRAME));
    menu_restore_p(s);
    m_rts(s);
}

// $C2:B2A7
/* A tab used: its sparkle and amount shown (the original's), the figure
 * raised, and the member's figures worked out again (the original's). */
void menu_raise_figure(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, false, false);
    menu_items_show_the_raise(g);
    if (!call(s, 0xC2B2D6, SHOW_AMOUNTS))
        return;
    menu_set_widths(s, true, true);
    menu_items_raise_the_figure(g);
    if (!call(s, 0xC2B311, WORK_OUT_FIGURES))
        return;
    menu_restore_p(s);
    m_rts(s);
}

// $C2:B200
/* A tab: its figure, then the members it raises it for. */
void menu_use_tab(Snes *s)
{
    GameState *g = state_of(s);
    menu_set_widths(s, true, true);
    menu_items_set_figure_raised(g, menu_item_operand(g));
    menu_set_widths(s, true, false);
    choose_members_to_use_on(s, USE_RAISES_A_FIGURE);
}

// $C2:B0AB
/* An item the menu uses: what it does looked up, and its use. */
void menu_use_item(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, true, m_x8(s));
    ItemUseKind kind = menu_items_look_up_use(g, menu_item_operand(g));
    ORIGINAL_EFFECT(leave_use_found(s));
    bool done = true;
    switch (kind) {
    case USE_RESTORES: done = menu_run(s, 0xC2B0E9, 0xC2B17F, menu_use_restoring_item); break;
    case USE_RESTORES_BY_CHANCE: done = menu_run(s, 0xC2B0E9, 0xC2B107, menu_use_item_by_chance); break;
    case USE_RESTS_THE_PARTY: done = menu_run(s, 0xC2B0E9, 0xC2B1F8, menu_rest_the_party); break;
    case USE_RAISES_A_FIGURE: done = menu_run(s, 0xC2B0E9, 0xC2B200, menu_use_tab); break;
    case USE_DOES_NOTHING: break;
    }
    if (!done)
        return;
    menu_restore_p(s);
    m_rts(s);
}

/* ---- The pure entries -------------------------------------------------------------------------- */

// $C2:AE43
/* It puts the cursor's cell back as a word, which leaves the byte after it
 * in A's high byte. */
void menu_draw_item_rows(Snes *s)
{
    menu_items_draw_rows(state_of(s));
    ORIGINAL_EFFECT(leave_rows_redrawn(s));
    m_rts(s);
}

// $C2:AE65
/* A row of the list at X, for the cursor's row. */
void menu_draw_item_row(Snes *s)
{
    GameState *g = state_of(s);
    menu_items_draw_row(g, s->cpu.x, menu_cursor_index(g));
    m_rts(s);
}

// $C2:AB62
void menu_sort_party_items(Snes *s)
{
    menu_items_sort(state_of(s));
    m_rts(s);
}

// $C2:AE91
void menu_show_item_users(Snes *s)
{
    menu_items_show_users(state_of(s), m_platform(s));
    m_rts(s);
}

/* ---- What the original's tests leave ----------------------------------------------------------- */

static void leave_load(Snes *s, uint8_t v) { menu_leave_load(s, v); }

/* The paging's answer (in A) compared with none. */
static void leave_paging_answered(Snes *s) { menu_leave_compare(s, (uint8_t)s->cpu.a, 0); }

/* A member loaded leaves 0 in A, a word. */
static void leave_member_looked_at(Snes *s)
{
    const GameState *g = state_of(s);
    if (menu_member_can_be_loaded(g, menu_use_member(g)))
        s->cpu.a &= 0x00FF;
}

/* A step tests the buttons as the cursor's reading left them. */
static void leave_buttons_tested(Snes *s)
{
    menu_leave_bit_test(s, (uint8_t)s->cpu.a, menu_cursor_buttons(state_of(s)));
}

/* Where the cursor started the frame, loaded and compared with where it is. */
static void leave_cursor_compared(Snes *s)
{
    const GameState *g = state_of(s);
    m_set_a8(s, menu_cursor_frame_start(g));
    menu_leave_compare(s, menu_cursor_frame_start(g), menu_cursor_index(g));
}

/* The cursor's row kept, compared with where it started the frame. */
static void leave_place_compared(Snes *s)
{
    const GameState *g = state_of(s);
    m_set_a8(s, menu_cursor_index(g));
    menu_leave_compare(s, menu_cursor_index(g), menu_cursor_frame_start(g));
}

/* The list's rows queued: what the queue's block move leaves. */
static void leave_rows_queued(Snes *s, uint16_t record) { menu_leave_queued_upload(s, record); }

/* A page's rows drawn: the cursor's cell put back as a word, its byte after
 * it in A's high byte. */
static void leave_rows_redrawn(Snes *s)
{
    const GameState *g = state_of(s);
    s->cpu.a = (uint16_t)(menu_cursor_index(g) | menu_cursor_left_from(g) << 8);
}

/* A push past the list's rows at its ends: the cursor's place in the
 * party's items less one, compared with the first past the ends. Elsewhere
 * the list's keeping of the cursor leaves its own. */
static void leave_ends_compared(Snes *s)
{
    const GameState *g = state_of(s);
    if (!(menu_list_scrolls(g) && menu_pushed(g, MENU_PUSHED_PAST_AN_EDGE)))
        return;
    uint8_t before = (uint8_t)(menu_cursor_index(g) + menu_item_list_top(g) - 1);
    m_set_a8(s, before);
    menu_leave_compare(s, before, 0xF0);
}

/* The slide's frames over: the second cursor's shift taken down by the
 * step a last time (that subtraction's carry and overflow) and the frames
 * counted out; or, skipped, the fast repeat's test. */
static void leave_slide_frames(Snes *s, bool took_frames)
{
    if (!took_frames) {
        m_set_flag(s, CPU_FLAG_N, true);
        m_set_flag(s, CPU_FLAG_Z, false);
        return;
    }
    const GameState *g = state_of(s);
    uint16_t step = menu_scroll_step(g), r = menu_held_cursor_shift(g);
    uint16_t shift_before = (uint16_t)(r + step);
    m_set_flag(s, CPU_FLAG_C, shift_before >= step);
    m_set_flag(s, CPU_FLAG_V, ((shift_before ^ step) & (shift_before ^ r) & 0x8000) != 0);
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, true);
}

/* The member window opened after the first row's place is loaded: a
 * tonic's strength picked by testing its effect (the overflow its MP bit), a
 * tab's figure by comparing the item with the power tab's and the magic
 * tab's. */
static void leave_member_choice_started(Snes *s, ItemUseKind kind)
{
    const GameState *g = state_of(s);
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, false);
    if (kind == USE_RESTORES) {
        m_set_flag(s, CPU_FLAG_V, (menu_use_effect(g) & MENU_USE_RESTORES_MP) != 0);
    } else if (kind == USE_RAISES_A_FIGURE) {
        uint8_t item = menu_item_operand(g);
        m_set_flag(s, CPU_FLAG_C, item >= (item == MENU_POWER_TAB ? MENU_POWER_TAB : MENU_MAGIC_TAB));
    }
}

/* An item's use found: the figures' first byte shifted up to its first set
 * bit of five (or past the five, counted out). */
enum { USE_BITS = 5, TOP_BIT = 0x80 };

static void leave_use_found(Snes *s)
{
    const GameState *g = state_of(s);
    uint8_t bits = menu_item_table_byte(g, menu_item_figures_at(g));
    uint8_t shifted = 0;
    bool found = false;
    for (unsigned i = 0; i < USE_BITS && !found; i++) {
        found = (bits & (TOP_BIT >> i)) != 0;
        shifted = (uint8_t)(bits << (i + 1));
    }
    m_set_flag(s, CPU_FLAG_C, true);
    m_set_flag(s, CPU_FLAG_N, found && (shifted & TOP_BIT));
    m_set_flag(s, CPU_FLAG_Z, !found || shifted == 0);
}

/* An item the menu can use: its kind's bit tested. */
static void leave_effect_tested(Snes *s)
{
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, false);
    m_set_flag(s, CPU_FLAG_C, true);
}

/* ---- The guards -------------------------------------------------------------------------------- */

enum { QUEUE_BYTES = 0x100 };

static bool queue_takes(const GameState *g, unsigned uploads)
{
    unsigned queued = menu_vram_queue_bytes(g);
    return queued % MENU_UPLOAD_BYTES == 0 && queued + MENU_UPLOAD_BYTES * uploads <= QUEUE_BYTES;
}

static bool on_the_screen(Snes *s) { return menu_on_page_work_ram(s) && menu_sounds_on_page(s); }

static bool peek(Snes *s, bool (*test)(const GameState *))
{
    vars_peek(true);
    bool ok = test(state_of(s));
    vars_peek(false);
    return ok;
}

static bool opens(const GameState *g)
{
    return queue_takes(g, 1) && menu_palette_changes_have_room(g) && menu_window_list_drawable(g, MENU_WINDOWS_ITEMS_SCREEN)
           && menu_item_list_row(g) < MENU_CURSOR_PLACES_COUNT;
}

bool menu_items_screen_opens(Snes *s) { return on_the_screen(s) && peek(s, opens); }

bool menu_item_command_step_runs(Snes *s) { return on_the_screen(s) && menu_cursor_is_movable(s); }

static bool list_step_runs(const GameState *g)
{
    return queue_takes(g, 2) && menu_items_list_is_pointable(g);
}

bool menu_item_list_step_runs(Snes *s) { return on_the_screen(s) && menu_cursor_is_movable(s) && peek(s, list_step_runs); }

static bool move_step_runs(const GameState *g)
{
    return list_step_runs(g) && menu_item_picked(g) < MENU_ITEMS;
}

bool menu_item_move_step_runs(Snes *s) { return on_the_screen(s) && menu_cursor_is_movable(s) && peek(s, move_step_runs); }

static bool sort_step_runs(const GameState *g) { return queue_takes(g, 1) && menu_items_rows_can_be_drawn(g); }

bool menu_items_sort_step_runs(Snes *s) { return on_the_screen(s) && peek(s, sort_step_runs); }

bool menu_item_users_step_runs(Snes *s) { return on_the_screen(s) && menu_cursor_is_movable(s); }

static bool panel_shows(const GameState *g)
{
    return queue_takes(g, 3) && menu_items_rows_can_be_drawn(g) && menu_window_list_drawable(g, MENU_WINDOWS_ITEMS_PANEL);
}

bool menu_items_panel_shows(Snes *s) { return on_the_screen(s) && peek(s, panel_shows); }
bool menu_items_panel_goes(Snes *s) { return menu_on_page_work_ram(s); }

bool menu_item_is_usable(Snes *s) { return on_the_screen(s) && peek(s, move_step_runs); }

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

/* The member window and one more used run with the cursor on its rows. */
static bool member_window_runs(const GameState *g)
{
    return queue_takes(g, 4) && menu_item_picked(g) < MENU_ITEMS && members_are_drawable(g)
           && menu_items_rows_can_be_drawn(g) && menu_item_list_row(g) < MENU_ITEMS_ROWS;
}

bool menu_member_window_opens(Snes *s) { return on_the_screen(s) && peek(s, member_window_runs); }

static bool tab_member_is_pointable(const GameState *g)
{
    return members_are_drawable(g) && menu_use_effect(g) < FIGURES && menu_use_first_member(g) <= menu_cursor_index(g);
}

bool menu_tab_member_is_pointable(Snes *s) { return on_the_screen(s) && peek(s, tab_member_is_pointable); }

static bool figure_is_raisable(const GameState *g)
{
    return members_are_drawable(g) && menu_use_effect(g) < FIGURES && menu_use_member(g) < menu_active_members(g);
}

bool menu_figure_is_raisable(Snes *s) { return menu_on_page_work_ram(s) && peek(s, figure_is_raisable); }
bool menu_one_more_is_usable(Snes *s) { return on_the_screen(s) && peek(s, member_window_runs); }

bool menu_item_rows_are_drawable(Snes *s) { return menu_on_page_work_ram(s) && peek(s, menu_items_rows_can_be_drawn); }
bool menu_item_row_is_drawable(Snes *s) { return menu_on_page_work_ram(s) && peek(s, menu_items_rows_can_be_drawn); }
bool menu_party_items_are_sortable(Snes *s) { return menu_on_page_work_ram(s); }
bool menu_item_users_are_showable(Snes *s) { return menu_on_page_work_ram(s) && peek(s, sort_step_runs); }
