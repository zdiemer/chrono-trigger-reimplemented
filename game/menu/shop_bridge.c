/* A shop on the original's machine: its checked legacy adapters, the steps that
 * run the shop's sequences (game/menu/shop_steps.c) between the calls that
 * are still the original's, and their guards.
 *
 * The sequences' ticks and sounds are siblings, run as the original's JSRs
 * reach them (menu_run): each saves P as it starts and calls the sound
 * driver, so the flags the original has at a sound are on the stack there,
 * where shadow mode compares them, and A's high byte goes to the driver. The
 * panel, the set-up and the equipping's own routines (a member chosen, its
 * slots, the shoulders' switch) are entries of their own, run the same way,
 * and so are the equipment screen's list of what fits and its move along. A
 * frame's wait waits in the menus' task switch, parked until its task goes
 * on (menu_wait_a_frame_from). The shop's way out, its fade in and the
 * refusal's sound, like the lists' openings, are C continuations.
 *
 * Before each of those calls the sequence's note leaves the registers and
 * flags the original's own instructions left there, worked out from the
 * state (the leaves at the end of the file): scaffolding, which the native
 * build compiles out. After a call out the machine is the original's again,
 * so a leave covers only what the original did since. */
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

/* ---- The calls out ------------------------------------------------------------------------------- */

enum {
    FADE_IN = 0xC283B2, /* the screen's fade in started */
    CURSOR_SOUND = 0xC2EAC2, REFUSAL_SOUND = 0xC2EACC,
};

/* The sounds by their number, loaded into A as the original's call does
 * (the load's flags are the P the sound saves). */
enum { DEAL_SOUND = 0x55 };

/* The quantity window's rows' upload. */
enum { UPLOAD_QUANTITY = 0xFBEA };

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

/* The guards' state halves, for the program as C. */
bool menu_shop_can_be_opened(const GameState *g) { return opens(g); }
bool menu_shop_is_on_the_tabs(const GameState *g) { return on_the_tabs(g); }
bool menu_shop_is_on_a_list(const GameState *g, bool selling_from_it) { return on_a_list(g, selling_from_it); }
bool menu_shop_is_on_the_quantity(const GameState *g) { return quantity(g); }
bool menu_shop_is_on_the_members(const GameState *g) { return on_the_members(g); }
bool menu_shop_member_can_be_chosen(const GameState *g) { return choosing_a_member(g) && menu_figures_can_be_worked_out(g); }
bool menu_shop_member_can_be_equipped(const GameState *g) { return equipping(g) && menu_figures_can_be_worked_out(g); }
