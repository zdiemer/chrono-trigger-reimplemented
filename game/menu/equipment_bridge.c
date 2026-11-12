/* What the equipment screen's steps need of the game's state before they
 * run: a member list the screen can draw, room in the upload queue for what a
 * step queues, a list of what fits whose every row names one of the game's
 * items. The steps (game/menu/equipment.c) go no further where one fails. */
#include "menu/equipment.h"

#include "charge.h"
#include "core/program.h"
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
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"

/* ---- The steps --------------------------------------------------------------------------------- */

/* The steps queue whole uploads, with room for what a step queues. */
static bool queue_takes(const GameState *g, unsigned uploads) { return menu_upload_queue_takes(g, uploads); }

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

static bool members_slide(const GameState *g) { return queue_takes(g, 3) && menu_equip_member_slide_can_run(g); }

bool menu_equip_members_can_slide(const GameState *g) { return members_slide(g); }

/* Pointing at a slot: the members it may switch to drawable, room for the
 * uploads (the card's, the marks' layer), the slot's sum. */
static bool slot_is_pointable(const GameState *g) { return members_are_drawable(g) && queue_takes(g, 3); }

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

static bool weapon_is_forgettable(const GameState *g) { return menu_member_row(g) < MENU_MEMBERS_COUNT; }

/* ---- The windows ------------------------------------------------------------------------------- */

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

/* What each step needs, for equipment.c. */
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
