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
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The calls out ----------------------------------------------------------------------------- */

/* The original's routines the steps call, by what they do. */
enum {
    MAIN_MENU = 0xC29A98,            /* back to the main menu */
};

/* The tasks a step stops: the second cursor's (the fourth's bit). */
enum { HELD_CURSOR_TASK = 0x40 };

/* The sprites' blocks a frame the tab's pointing leaves in A. */
enum { BLOCKS_A_FRAME = 2 };

/* An item's use found: the figures' first byte shifted up to its first set
 * bit of five (or past the five, counted out). */
enum { USE_BITS = 5, TOP_BIT = 0x80 };

/* ---- The guards -------------------------------------------------------------------------------- */

static bool queue_takes(const GameState *g, unsigned uploads) { return menu_upload_queue_takes(g, uploads); }

static bool list_step_runs(const GameState *g) { return menu_items_can_point_along_the_list(g); }

static bool move_step_runs(const GameState *g)
{
    return list_step_runs(g) && menu_item_picked(g) < MENU_ITEMS;
}

bool menu_items_can_move_the_picked(const GameState *g) { return move_step_runs(g) && menu_cursor_can_move(g); }

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

bool menu_items_use_can_run(const GameState *g) { return use_runs(g); }

/* The member window and one more used run with the cursor on its rows. */
static bool member_window_runs(const GameState *g)
{
    return queue_takes(g, 4) && menu_item_picked(g) < MENU_ITEMS && members_are_drawable(g)
           && menu_items_rows_can_be_drawn(g) && menu_item_list_row(g) < MENU_ITEMS_ROWS;
}

bool menu_items_member_window_can_run(const GameState *g) { return member_window_runs(g); }

static bool tab_member_is_pointable(const GameState *g)
{
    return members_are_drawable(g) && menu_use_effect(g) < FIGURES && menu_use_first_member(g) <= menu_cursor_index(g);
}

bool menu_items_tab_member_can_be_pointed(const GameState *g) { return tab_member_is_pointable(g); }

static bool figure_is_raisable(const GameState *g)
{
    return members_are_drawable(g) && menu_use_effect(g) < FIGURES && menu_use_member(g) < menu_active_members(g);
}

bool menu_items_figure_can_be_raised(const GameState *g) { return figure_is_raisable(g); }
