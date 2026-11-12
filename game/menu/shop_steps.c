/* A shop's steps, as the menus' main task runs them each frame: the shop
 * opening, the tabs, the goods and the list for sale, the quantity, and the
 * equipping from the shop (the members, a member's slots, the list of what
 * fits them).
 *
 * Each step, and each routine a step goes on to that waits, is one sequence
 * of what game/menu/shop.c and shop_equipment.c do, with the menus' cursor,
 * ticks and waits, and the routines it goes on to and ends in (run and
 * ends), each run only on the state it handles (the end of this file). */
#include "menu/shop.h"

#include "charge.h"
#include "core/program.h"
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
#include "menu/menu.h"
#include "menu/menu_data.h"
#include "menu/vram.h"
#include "vars.h"

static bool run(GameState *g, Platform *pf, ShopRoutine r);
static bool ends(GameState *g, Platform *pf, ShopEnd end);

static bool cursor_moved(const GameState *g) { return menu_cursor_index(g) != menu_cursor_frame_start(g); }

/* ---- Opening ------------------------------------------------------------------------------------- */

// $C2:D52B
/* The shop set up, the held cursor on the tabs, and on to the fade in. */
bool menu_open_shop_with(GameState *g, Platform *pf)
{
    if (!run(g, pf, SHOP_SETS_UP))
        return false;
    menu_shop_hold_a_cursor_on_the_tabs(g);
    return ends(g, pf, SHOP_FADES_IN);
}

// $C2:DACA
/* The shop's windows and goods, the description of nothing, the colours and
 * the members, the panel for nothing, and the prices and layers. */
bool menu_set_up_shop_with(GameState *g, Platform *pf)
{
    menu_shop_open(g, pf);
    TIMED_AS(g, 0xC2E012, menu_shop_describe_nothing(g));
    menu_shop_open_colours(g);
    menu_shop_open_members(g);
    if (!run(g, pf, SHOP_SHOWS_THE_PANEL_AS_IT_OPENS))
        return false;
    menu_shop_open_last(g, pf);
    return true;
}

/* ---- The panel ----------------------------------------------------------------------------------- */

// $C2:DD7C
/* The panel's counts, then each member: loaded and posed, its figures with
 * the item worked out where it could use it, and drawn. */
bool menu_show_shop_item_panel_with(GameState *g, Platform *pf)
{
    (void)pf;
    menu_shop_start_panel(g);
    unsigned m = 0;
    bool usable = false;
    for (; menu_shop_panel_member(g, m, &usable); m++) {
        if (usable) {
            TIMED_AS(g, 0xC2916E, menu_work_out_figures(g));
        }
        menu_shop_panel_member_shown(g, m);
    }
    return true;
}

/* ---- The tabs ------------------------------------------------------------------------------------ */

// $C2:D546
/* A tab chosen opens its list, with a tick; going back leaves the shop; up
 * or down goes to the members; a tick where the cursor moved. */
bool menu_choose_shop_tab_with(GameState *g, Platform *pf)
{
    menu_shop_on_the_tabs_frame(g);
    menu_read_the_cursor_for_a_step(g);
    switch (menu_shop_choose_tab(g)) {
    case TAB_CHOSEN: {
        menu_shop_leave_the_tabs(g);
        menu_tick(g, pf);
        bool buying = menu_shop_buying_chosen(g);
        ShopStep list = buying ? SHOP_BUYING : SHOP_SELLING;
        menu_shop_go_to(g, list);
        return ends(g, pf, buying ? SHOP_OPENS_THE_GOODS : SHOP_OPENS_THE_LIST_FOR_SALE);
    }
    case SHOP_LEFT: return ends(g, pf, SHOP_LEAVES);
    case TO_THE_MEMBERS:
        menu_shop_stop_holding(g);
        TIMED_AS(g, 0xC2E017, menu_shop_go_down_to_the_members(g, pf));
        break;
    case TAB_POINTED_AT: break;
    }
    bool moved = cursor_moved(g);
    if (moved)
        menu_tick(g, pf);
    return true;
}

/* ---- The lists ----------------------------------------------------------------------------------- */

/* The row the cursor is on taken and described; the panel where the place
 * is another; a tick where the cursor moved; the list queued. */
static bool take_the_row(GameState *g, Platform *pf, bool selling)
{
    menu_shop_take_the_row(g, selling);
    TIMED_AS(g, 0xC2F2F3, menu_describe_item(g, menu_item_operand(g)));
    bool another = menu_shop_another_place(g);
    if (another && !run(g, pf, selling ? SHOP_SHOWS_THE_PANEL_FOR_A_SALE : SHOP_SHOWS_THE_PANEL_FOR_A_GOOD))
        return false;
    bool moved = cursor_moved(g);
    if (moved)
        menu_tick(g, pf);
    menu_shop_queue_the_list(g);
    return true;
}

/* The list slid a row: started, the row coming in drawn, a tick, the frames
 * (each waiting for the next), the rows at rest, and the row taken. */
static bool slide_the_list(GameState *g, Platform *pf, bool selling)
{
    menu_shop_set_up_list_slide(g);
    TIMED_AS(g, 0xC2EB9B, menu_start_list_slide(g));
    menu_shop_bring_in_a_row(g, selling);
    menu_tick(g, pf);
    if (menu_shop_list_slide_takes_frames(g))
        while (menu_shop_slide_list_a_frame(g)) {
            TIMED_AS(g, 0xC2EB1F, menu_slide_list_a_frame(g));
            menu_task_waits(g, 1);
        }
    menu_shop_finish_list_slide(g);
    return take_the_row(g, pf, selling);
}

// $C2:D7CF, $C2:D8B2
/* Pointing along a list: a page turned, the rows drawn again and a frame's
 * wait; or the cursor kept on the list's rows, a push past them sliding the
 * list. Then the row. */
static bool point_along(GameState *g, Platform *pf, bool selling)
{
    uint8_t top = menu_shop_list_top(g, selling), to = top;
    TIMED_AS(g, 0xC293BF, to = menu_turn_page(g, top));
    if (to != top) {
        menu_shop_turn_page(g, selling, to);
        menu_task_waits(g, 1);
        return take_the_row(g, pf, selling);
    }
    TIMED_AS(g, 0xC2EA53, menu_keep_the_cursor_on_the_list(g));
    if (menu_shop_list_slide_brings_a_row(g, selling))
        return slide_the_list(g, pf, selling);
    return take_the_row(g, pf, selling);
}

// $C2:D5D9
/* Going back from a list to the tabs: the panel for nothing, a tick, the
 * tabs, and the description of nothing. */
static bool back_to_the_tabs(GameState *g, Platform *pf, bool selling)
{
    menu_shop_back_from_the_list(g, selling);
    if (!run(g, pf, SHOP_SHOWS_THE_PANEL_FOR_NOTHING))
        return false;
    menu_tick(g, pf);
    menu_shop_back_on_the_tabs(g, pf);
    TIMED_AS(g, 0xC2E012, menu_shop_describe_nothing(g));
    return true;
}

// $C2:D58B
/* Choosing a row of the goods opens the quantity window, with a tick, where
 * the party has room for one and gold to pay for it; else the refusal's
 * buzz. */
bool menu_choose_goods_with(GameState *g, Platform *pf)
{
    menu_read_the_cursor_for_a_step(g);
    switch (menu_shop_choose_on_the_list(g)) {
    case SHOP_ROW_CHOSEN: {
        if (!menu_shop_room_to_buy(g)) {
            return ends(g, pf, SHOP_REFUSES);
        }
        if (!menu_shop_gold_buys_some(g)) {
            return ends(g, pf, SHOP_REFUSES);
        }
        menu_tick(g, pf);
        menu_shop_open_quantity(g, pf, SHOP_BUYING_HOW_MANY);
        return true;
    }
    case SHOP_LIST_LEFT: return back_to_the_tabs(g, pf, false);
    case SHOP_ROW_POINTED_AT: return point_along(g, pf, false);
    }
    return false;
}

// $C2:D645
/* Choosing a row for sale opens the quantity window, with a tick, where the
 * party has some and room in its gold for one's price; else the refusal's
 * buzz. */
bool menu_choose_for_sale_with(GameState *g, Platform *pf)
{
    menu_read_the_cursor_for_a_step(g);
    switch (menu_shop_choose_on_the_list(g)) {
    case SHOP_ROW_CHOSEN: {
        if (!menu_shop_some_to_sell(g)) {
            return ends(g, pf, SHOP_REFUSES);
        }
        if (!menu_shop_room_for_the_gold(g)) {
            return ends(g, pf, SHOP_REFUSES);
        }
        menu_tick(g, pf);
        menu_shop_open_quantity(g, pf, SHOP_SELLING_HOW_MANY);
        return true;
    }
    case SHOP_LIST_LEFT: return back_to_the_tabs(g, pf, true);
    case SHOP_ROW_POINTED_AT: return point_along(g, pf, true);
    }
    return false;
}

/* ---- The quantity -------------------------------------------------------------------------------- */

// $C2:DA01
/* A push changes the quantity, with a tick where it changed. */
static bool change_the_quantity(GameState *g, Platform *pf)
{
    bool changed = menu_shop_change_quantity(g, pf);
    if (menu_shop_quantity_pushed(g)) {
        if (changed) {
            menu_tick(g, pf);
        }
    }
    return true;
}

/* The quantity taken or left: the deal made, with its sound, where taken;
 * otherwise the quantity changed by the push. */
static bool deal_or_change(GameState *g, Platform *pf, bool selling, bool *closed)
{
    *closed = false;
    menu_shop_hold_the_quantity(g);
    menu_read_the_cursor_for_a_step(g);
    switch (menu_shop_choose_quantity(g)) {
    case QUANTITY_TAKEN:
        if (selling)
            menu_shop_sold(g);
        else
            menu_shop_bought(g);
        if (!run(g, pf, selling ? SHOP_PLAYS_THE_DEAL_SOUND_FOR_SALE : SHOP_PLAYS_THE_DEAL_SOUND))
            return false;
        break;
    case QUANTITY_LEFT: break;
    case QUANTITY_CHANGED: return change_the_quantity(g, pf);
    }
    *closed = true;
    return true;
}

// $C2:D618
/* The quantity bought: the goods added to the party's items, the gold paid,
 * the goods counted again, the deal's sound. Bought or left, a tick, and
 * back on the list. */
bool menu_choose_how_many_to_buy_with(GameState *g, Platform *pf)
{
    bool closed;
    if (!deal_or_change(g, pf, false, &closed))
        return false;
    if (!closed)
        return true;
    menu_tick(g, pf);
    menu_shop_back_to_buying(g);
    return true;
}

// $C2:D690
/* The quantity sold: the items taken from the party's, the gold received,
 * the deal's sound. Sold or left, a tick, and back on the list, made
 * again. */
bool menu_choose_how_many_to_sell_with(GameState *g, Platform *pf)
{
    bool closed;
    if (!deal_or_change(g, pf, true, &closed))
        return false;
    if (!closed)
        return true;
    menu_shop_restart_the_bars_blink(g);
    menu_tick(g, pf);
    menu_shop_back_to_selling(g);
    TIMED_AS(g, 0xC2DECC, menu_shop_make_the_list_for_sale(g));
    menu_shop_draw_for_sale(g);
    return true;
}

/* ---- The equipping from the shop ---------------------------------------------------------------- */

// $C2:D6C3
/* On the members: choosing one opens its slots, with a tick; going back, or
 * up or down, goes back to the tabs, with a tick; a tick where the cursor
 * moved. */
bool menu_choose_shop_member_with(GameState *g, Platform *pf)
{
    menu_read_the_cursor_for_a_step(g);
    ShopMembersChoice c = menu_shop_choose_a_member(g);
    switch (c) {
    case SHOP_MEMBER_CHOSEN:
        menu_tick(g, pf);
        if (!run(g, pf, SHOP_OPENS_THE_MEMBER))
            return false;
        menu_shop_go_to(g, SHOP_ON_A_MEMBERS_SLOTS);
        return true;
    case SHOP_MEMBERS_LEFT:
        menu_tick(g, pf);
        menu_shop_back_up_to_the_tabs(g, pf);
        return true;
    case SHOP_MEMBER_POINTED_AT: {
        bool moved = cursor_moved(g);
        if (moved)
        menu_tick(g, pf);
    return true;
    }
    }
    return false;
}

// $C2:E0A5
/* A member chosen: its equipment window, a frame, and its slots. */
bool menu_open_shop_member_with(GameState *g, Platform *pf)
{
    menu_shop_choose_the_member(g, pf);
    menu_task_waits(g, 1);
    set_menu_cursor_index(g, 0);
    if (!run(g, pf, SHOP_SHOWS_THE_SLOTS_AS_THEY_OPEN))
        return false;
    menu_shop_open_the_members_slots(g);
    return true;
}

// $C2:E0F1
/* The slot on the cursor marked, the list's bar placed, the list of what
 * fits it made, and the strips' rows queued. */
bool menu_show_shop_member_slots_with(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0xC2E150, menu_shop_mark_the_slot(g));
    menu_shop_place_the_lists_bar(g);
    if (!run(g, pf, SHOP_MAKES_THE_LIST))
        return false;
    menu_queue_fixed_upload(g, MENU_UPLOAD_FRONT_LAYER_STRIPS);
    return true;
}

// $C2:E108
/* The shoulders switch to another member: a tick, the member shown, and a
 * frame. */
bool menu_switch_shop_member_with(GameState *g, Platform *pf)
{
    uint8_t member = menu_shop_member_by_the_shoulders(g), was = menu_member_worked_on(g);
    if (member == was)
        return true;
    menu_work_on_member(g, member);
    menu_tick(g, pf);
    menu_shop_switch_member(g, pf);
    menu_task_waits(g, 1);
    return true;
}

// $C2:D715
/* On the member's slots: choosing one with something to fit it goes into
 * its list, with a tick (the refusal's buzz where nothing fits); going back
 * returns to the members, with a tick; otherwise the shoulders' switch, the
 * slot on the cursor and its list, and a tick where the cursor moved. */
bool menu_choose_shop_slot_with(GameState *g, Platform *pf)
{
    menu_read_the_cursor_for_a_step(g);
    ShopSlotsChoice c = menu_shop_choose_a_slot(g);
    switch (c) {
    case SHOP_SLOT_WITH_NOTHING_TO_FIT:
        set_menu_cursor_index(g, menu_cursor_frame_start(g));
        return ends(g, pf, SHOP_REFUSES_A_SLOT);
    case SHOP_SLOT_CHOSEN:
        set_menu_cursor_index(g, menu_cursor_frame_start(g));
        menu_tick(g, pf);
        menu_shop_into_the_list(g);
        return true;
    case SHOP_SLOTS_LEFT:
        menu_tick(g, pf);
        menu_shop_back_to_the_members(g);
        return true;
    case SHOP_SLOT_POINTED_AT: {
        if (!run(g, pf, SHOP_SWITCHES_THE_MEMBER) || !run(g, pf, SHOP_SHOWS_THE_SLOTS))
            return false;
        bool moved = cursor_moved(g);
        if (moved)
        menu_tick(g, pf);
    return true;
    }
    }
    return false;
}

/* Where the move along the equipment list has slid it a row (its first row
 * moved, and no page turned), the original compares the cursor's places a
 * word at a time, as the slide leaves its arithmetic: each place with the
 * byte after it, where the cursor last moved from beside a byte the menus never write.
 * So the shop's cursor ticks again as its list's slide ends wherever those
 * two differ, whether or not the cursor moved (the equipment screen compares
 * them a byte at a time). */
static bool cursor_moved_or_slid(const GameState *g)
{
    return cursor_moved(g) || menu_cursor_left_from(g) != menu_byte_the_mode_before_left(g);
}

// $C2:D778
/* On the list of what fits: choosing an item puts it on, with its sound;
 * then, or going back, a tick and out of the list. Otherwise the cursor's
 * move along the list (the equipment screen's), the item on it tried on,
 * and a tick where the cursor moved. */
bool menu_choose_shop_equipment_with(GameState *g, Platform *pf)
{
    menu_read_the_cursor_for_a_step(g);
    if (!menu_asked(g, MENU_CHOOSE) && !menu_asked(g, MENU_GO_BACK)) {
        uint8_t top = menu_equip_list_top(g);
        bool pages = menu_page_turns(g, top);
        if (!run(g, pf, SHOP_POINTS_ALONG_THE_LIST))
            return false;
        bool slid = !pages && menu_equip_list_top(g) != top;
        TIMED_AS(g, 0xC2E162, menu_shop_try_on(g, pf));
        bool moved = slid ? cursor_moved_or_slid(g) : cursor_moved(g);
        if (moved)
        menu_tick(g, pf);
    return true;
    }
    if (menu_asked(g, MENU_CHOOSE)) {
        menu_equip_put_on_the_chosen_item(g);
        if (!run(g, pf, SHOP_PLAYS_THE_PUT_ON_SOUND))
            return false;
        menu_shop_after_putting_on(g);
    }
    menu_tick(g, pf);
    menu_shop_out_of_the_list(g, pf);
    return true;
}

/* ---- The steps' services ------------------------------------------------------------------------- */

static bool not_taken(GameState *g, uint32_t routine)
{
    program_breaks_a_contract(g, routine, "a shop the port doesn't take");
    return false;
}

/* A routine, run only on the state it handles, timed as its own. */
static bool sequence(GameState *g, Platform *pf, uint32_t routine, bool takes, ShopSequence *run)
{
    if (!takes)
        return not_taken(g, routine);
    bool done = false;
    TIMED_AS(g, routine, done = run(g, pf));
    return done;
}

static bool sound(GameState *g, Platform *pf, uint8_t sound)
{
    TIMED_AS(g, 0xC2EABA, menu_sound_played(g, pf, sound));
    return true;
}

/* The deal's sound, and an item's put on. */
enum { DEAL_SOUND = 0x55 };

static bool run(GameState *g, Platform *pf, ShopRoutine r)
{
    switch (r) {
    case SHOP_SETS_UP: return sequence(g, pf, 0xC2DACA, menu_shop_can_be_opened(g), menu_set_up_shop_with);
    case SHOP_SHOWS_THE_PANEL_AS_IT_OPENS:
    case SHOP_SHOWS_THE_PANEL_FOR_A_GOOD:
    case SHOP_SHOWS_THE_PANEL_FOR_A_SALE:
    case SHOP_SHOWS_THE_PANEL_FOR_NOTHING:
        return sequence(g, pf, 0xC2DD7C, menu_shop_is_on_the_tabs(g), menu_show_shop_item_panel_with);
    case SHOP_PLAYS_THE_DEAL_SOUND:
    case SHOP_PLAYS_THE_DEAL_SOUND_FOR_SALE: return sound(g, pf, DEAL_SOUND);
    case SHOP_PLAYS_THE_PUT_ON_SOUND: return sound(g, pf, MENU_EQUIP_PUT_ON_SOUND);
    case SHOP_OPENS_THE_MEMBER: return sequence(g, pf, 0xC2E0A5, menu_shop_member_can_be_chosen(g), menu_open_shop_member_with);
    case SHOP_SHOWS_THE_SLOTS_AS_THEY_OPEN:
    case SHOP_SHOWS_THE_SLOTS:
        return sequence(g, pf, 0xC2E0F1, menu_shop_member_can_be_equipped(g), menu_show_shop_member_slots_with);
    case SHOP_SWITCHES_THE_MEMBER:
        return sequence(g, pf, 0xC2E108, menu_shop_member_can_be_equipped(g), menu_switch_shop_member_with);
    case SHOP_MAKES_THE_LIST: return menu_make_equipment_list(g, pf);
    case SHOP_POINTS_ALONG_THE_LIST: return menu_point_along_equipment_list(g, pf);
    }
    return false;
}

/* The steps' ends: the fade in's task started, the menus closed, a list
 * opened where the state is one it handles, or the buzz. */
static bool ends(GameState *g, Platform *pf, ShopEnd end)
{
    switch (end) {
    case SHOP_FADES_IN: TIMED_AS(g, 0xC283B2, menu_start_the_fade_in(g)); return true;
    case SHOP_LEAVES: menu_close(g, pf, false); return true;
    case SHOP_OPENS_THE_GOODS:
        if (!menu_shop_is_on_the_tabs(g))
            return not_taken(g, 0xC2DC7B);
        TIMED_AS(g, 0xC2DC7B, menu_shop_open_the_goods(g, pf));
        return true;
    case SHOP_OPENS_THE_LIST_FOR_SALE:
        if (!menu_shop_is_on_the_tabs(g))
            return not_taken(g, 0xC2DE98);
        TIMED_AS(g, 0xC2DE98, menu_shop_open_the_list_for_sale(g, pf));
        return true;
    case SHOP_REFUSES:
    case SHOP_REFUSES_A_SLOT: menu_buzz(g, pf); return true;
    }
    return false;
}

/* A step, run only on the state it handles. */
static bool step(GameState *g, Platform *pf, bool takes, uint32_t routine, ShopSequence *run)
{
    if (!takes)
        return not_taken(g, routine);
    return run(g, pf);
}

static bool movable(const GameState *g) { return menu_cursor_can_move(g); }

bool menu_open_shop(GameState *g, Platform *pf) { return step(g, pf, menu_shop_can_be_opened(g), 0xC2D52B, menu_open_shop_with); }

bool menu_choose_shop_tab(GameState *g, Platform *pf)
{
    return step(g, pf, movable(g) && menu_shop_is_on_the_tabs(g), 0xC2D546, menu_choose_shop_tab_with);
}

bool menu_choose_goods(GameState *g, Platform *pf)
{
    return step(g, pf, movable(g) && menu_shop_is_on_a_list(g, false), 0xC2D58B, menu_choose_goods_with);
}

bool menu_choose_for_sale(GameState *g, Platform *pf)
{
    return step(g, pf, movable(g) && menu_shop_is_on_a_list(g, true), 0xC2D645, menu_choose_for_sale_with);
}

bool menu_choose_how_many_to_sell(GameState *g, Platform *pf)
{
    return step(g, pf, movable(g) && menu_shop_is_on_the_quantity(g), 0xC2D690, menu_choose_how_many_to_sell_with);
}

bool menu_choose_how_many_to_buy(GameState *g, Platform *pf)
{
    return step(g, pf, movable(g) && menu_shop_is_on_the_quantity(g), 0xC2D618, menu_choose_how_many_to_buy_with);
}

bool menu_choose_shop_member(GameState *g, Platform *pf)
{
    return step(g, pf, movable(g) && menu_shop_is_on_the_members(g), 0xC2D6C3, menu_choose_shop_member_with);
}

bool menu_choose_shop_slot(GameState *g, Platform *pf)
{
    return step(g, pf, movable(g) && menu_shop_member_can_be_equipped(g), 0xC2D715, menu_choose_shop_slot_with);
}

bool menu_choose_shop_equipment(GameState *g, Platform *pf)
{
    return step(g, pf, movable(g) && menu_shop_member_can_be_equipped(g), 0xC2D778, menu_choose_shop_equipment_with);
}
