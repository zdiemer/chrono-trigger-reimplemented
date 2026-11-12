/* The techs screen on the original's machine: its checked legacy adapters, the
 * bridges that run what game/menu/tech_screen.c decides between the calls to
 * what is still the original's, and their guards.
 *
 * Each step reads the cursor, asks tech_screen.c what the frame's input
 * does, and carries it out: the sounds are siblings, run as the original's
 * JSRs reach them (menu_run); the list's frame is C, called as C; a step
 * that waits for the next frame waits in the menus' task switch, parked
 * until its task goes on (menu_wait_a_frame_from). A refused choice jumps
 * to the refusal's sound, as the original's does, and going back from the
 * members calls the main menu's continuation as C behind its guard.
 *
 * The routines a step reaches with a JSR that call out themselves (the moves
 * among the members and the member list's slide, the list's slide, the
 * lister's sprite, the descriptions, the window a tech is used from and the
 * pointing in it) are entries of their own, run through menu_run so that the original's frames stand under
 * their calls out: shadow mode compares the stack there, the flags each saves
 * as it starts among it. So before running one, and before a sound (which
 * saves them too), a bridge leaves the flags the original's own instructions
 * left, worked out from the state (the leaves at the end of the file):
 * scaffolding, which the native build compiles out. */
#include "menu/tech_screen.h"

#include "charge.h"
#include "cpu_io.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/description.h"
#include "menu/healing.h"
#include "menu/held_cursors.h"
#include "menu/item_screen.h"
#include "menu/items.h"
#include "menu/lists.h"
#include "menu/main_menu.h"
#include "menu/members.h"
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/party.h"
#include "menu/sprites.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The calls out ----------------------------------------------------------------------------- */

/* The original's routines the steps call, by what they do. */
enum { WAIT_FRAMES = 0xC2822B, /* the task waits for the frames in A */
    MAIN_MENU = 0xC29A98, /* back to the main menu */ };

/* The widths and P the original's code sets at those places, and the lister's
 * record's page its sprite's load runs on. */
enum { LISTER_PAGE = 0x19C0 };

/* The healing's targets worked out (its last shifts leave the carry clear),
 * then the effect's every-member bit tested. */
enum { EVERY_MEMBER_BIT = 0x08 };

/* Slid: the line effects' bit loaded over what the rows' copies left in the
 * accumulator's high byte (the block moves' $FFFF up the list, the copy's
 * last source row down it), and no overflow from the copies' sums. */
enum { LINE_EFFECTS_MOVED_BIT = 0x80 };

/* ---- The guards -------------------------------------------------------------------------------- */

enum { LIST_ROWS_SHOWN = 8 };

static bool queue_takes(const GameState *g, unsigned uploads) { return menu_upload_queue_takes(g, uploads); }

static bool list_step_runs(const GameState *g)
{
    return queue_takes(g, 4) && menu_techs_list_is_pointable(g) && menu_cursor_index(g) < 8;
}

bool menu_techs_lister_can_show(const GameState *g) { return list_step_runs(g); }

static bool targets_step_runs(const GameState *g)
{
    return queue_takes(g, 4) && menu_techs_list_is_pointable(g) && menu_use_first_member(g) <= menu_cursor_index(g)
           && menu_tech_user(g) < (uint8_t)menu_members_reachable(g);
}

bool menu_techs_targets_can_run(const GameState *g) { return targets_step_runs(g); }

/* A member row: the loaded record a character's, its window list drawable
 * from the drawing origin. */
enum { MEMBER_ROW_LIST = 0xC01F };

static bool restoring_is_pointable(const GameState *g)
{
    uint8_t active = menu_active_members(g);
    if (active == 0 || active > MENU_MEMBER_ROWS || menu_use_first_member(g) > menu_cursor_index(g))
        return false;
    for (uint8_t member = 0; member < active; member++)
        if (menu_members(g, member) >= CHARACTER_RECORDS_COUNT)
            return false;
    return true;
}

bool menu_restoring_can_point(const GameState *g) { return restoring_is_pointable(g); }
