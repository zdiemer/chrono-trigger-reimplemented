/* What the techs screen's steps need of the game's state before they run:
 * the cursor on the list, the tech's user one of the members reached, the
 * members a restoring is used on each a character's, room in the upload
 * queue for what a step queues. The steps (game/menu/tech_screen.c) go no
 * further where one fails. */
#include "menu/tech_screen.h"

#include "charge.h"
#include "cpu_io.h"
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

/* ---- The steps --------------------------------------------------------------------------------- */

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

/* The members a restoring points among: the active ones, each a
 * character's. */
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
