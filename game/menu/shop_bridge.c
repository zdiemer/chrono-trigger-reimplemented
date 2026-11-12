/* What a shop's steps need of the game's state before they run: the menus'
 * zero cell clear, the members shown, the cursor on a list within it, the
 * party's gold within the most it may hold. The steps
 * (game/menu/shop_steps.c) go no further where one fails. */
#include "menu/shop.h"

#include "charge.h"
#include "cpu_io.h"
#include "menu/cursor.h"
#include "menu/description.h"
#include "menu/display.h"
#include "menu/equipment.h"
#include "menu/fades.h"
#include "menu/figures.h"
#include "menu/items.h"
#include "menu/lists.h"
#include "menu/members.h"
#include "menu/menu_data.h"
#include "menu/vram.h"
#include "menu/windows.h"
#include "port/vars_ram.h"
#include "vars.h"

// $C2:E001

static bool zero_is_zero(const GameState *g) { return menu_zero(g) == 0; }

static bool opens(const GameState *g) { return zero_is_zero(g) && menu_shop_can_open(g); }

static bool on_the_tabs(const GameState *g) { return zero_is_zero(g) && menu_shop_members_are_shown(g); }

static bool on_a_list(const GameState *g, bool selling)
{
    return zero_is_zero(g) && menu_shop_on_a_list(g, selling) && menu_shop_gold_within_the_most(g)
           && menu_cursor_kept_among(g, 0, SHOP_LIST_ROWS, false);
}

static bool quantity(const GameState *g) { return zero_is_zero(g) && menu_shop_members_are_shown(g) && menu_shop_gold_within_the_most(g); }

/* The equipping works on a member the player can choose among, the shop's
 * members shown and its list of what fits made for the member's slot. */
static bool equipping(const GameState *g)
{
    return zero_is_zero(g) && menu_shop_members_are_shown(g) && menu_members_reachable(g) >= 1
           && menu_members_reachable(g) <= menu_shop_member_count(g) && menu_member_worked_on(g) < menu_members_reachable(g)
           && menu_member_record_is_a_characters(g);
}
static bool on_the_members(const GameState *g)
{
    return zero_is_zero(g) && menu_shop_members_are_shown(g) && menu_members_reachable(g) >= 1
           && menu_members_reachable(g) <= menu_shop_member_count(g)
           && menu_cursor_kept_among(g, SHOP_FIRST_MEMBER, (uint8_t)menu_members_reachable(g), false);
}

/* The member chosen is the one under the cursor: the opening takes the member
 * worked on from it, so what was worked on before doesn't matter. */
static bool choosing_a_member(const GameState *g)
{
    return zero_is_zero(g) && menu_shop_members_are_shown(g) && menu_members_reachable(g) >= 1
           && menu_members_reachable(g) <= menu_shop_member_count(g) && menu_cursor_index(g) >= SHOP_FIRST_MEMBER
           && menu_cursor_index(g) < SHOP_FIRST_MEMBER + menu_members_reachable(g);
}

/* What each step needs, for shop_steps.c. */
bool menu_shop_can_be_opened(const GameState *g) { return opens(g); }
bool menu_shop_is_on_the_tabs(const GameState *g) { return on_the_tabs(g); }
bool menu_shop_is_on_a_list(const GameState *g, bool selling_from_it) { return on_a_list(g, selling_from_it); }
bool menu_shop_is_on_the_quantity(const GameState *g) { return quantity(g); }
bool menu_shop_is_on_the_members(const GameState *g) { return on_the_members(g); }
bool menu_shop_member_can_be_chosen(const GameState *g) { return choosing_a_member(g) && menu_figures_can_be_worked_out(g); }
bool menu_shop_member_can_be_equipped(const GameState *g) { return equipping(g) && menu_figures_can_be_worked_out(g); }
