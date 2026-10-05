/* A shop: what the game does (game/menu/shop.c, and the equipping from it
 * in game/menu/shop_equipment.c) and the entries that run it on the
 * original's machine (game/menu/shop_bridge.c). */
#ifndef CT_GAME_MENU_SHOP_H
#define CT_GAME_MENU_SHOP_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/video.h"
#include "snes/system.h"
#include "state.h"

/* The shop's steps, as its main task runs them (menu_screen_step). */
typedef enum {
    SHOP_OPENING = 0,
    SHOP_ON_THE_TABS = 1,
    SHOP_BUYING = 2,
    SHOP_SELLING = 3,
    SHOP_SELLING_HOW_MANY = 4,
    SHOP_BUYING_HOW_MANY = 5,
    SHOP_ON_THE_MEMBERS = 6,
    SHOP_ON_A_MEMBERS_SLOTS = 7,
    SHOP_ON_WHAT_FITS = 8,
} ShopStep;

/* The cursor's places: the list's rows, the two tabs, the quantity, the
 * members along the bottom. */
enum { SHOP_LIST_ROWS = 8, SHOP_BUY_TAB = 8, SHOP_SELL_TAB = 9, SHOP_QUANTITY = 10, SHOP_FIRST_MEMBER = 12 };

/* Each member's strip of the bottom window, its icon over its figures: two
 * tile rows of four tiles, eight bytes a member along the row from the
 * first's. */
enum { SHOP_MEMBER_STRIPS_AT = 0x3404, SHOP_MEMBER_STRIP_BYTES = 8 };

void menu_shop_go_to(GameState *g, ShopStep step);

/* ---- Opening it --------------------------------------------------------------------------------- */

/* The shop's windows and goods, the cursor on the buying tab; around the
 * description of nothing (the original's), its colours and the members
 * standing; around the panel (its own), the prices and its layers. Then
 * the second cursor holds on the tabs. */
void menu_shop_open(GameState *g, Platform *pf);
void menu_shop_open_colours(GameState *g);
void menu_shop_open_members(GameState *g);
void menu_shop_open_last(GameState *g, Platform *pf);
void menu_shop_hold_a_cursor_on_the_tabs(GameState *g);

/* ---- The panel ---------------------------------------------------------------------------------- */

/* The panel for the item on the cursor (menu_item_operand): how many the
 * party has and has on; then each member, posed, its attack and defence as
 * they are and with the item (worked out by the original where it could use
 * it) drawn. False past the last member; the panel's done after it. */
void menu_shop_start_panel(GameState *g);
bool menu_shop_panel_member(GameState *g, unsigned member, bool *usable);
void menu_shop_panel_member_shown(GameState *g, unsigned member);
void menu_shop_leave_panel_done(GameState *g, unsigned members);

/* ---- The tabs ----------------------------------------------------------------------------------- */

typedef enum { TAB_POINTED_AT, TAB_CHOSEN, SHOP_LEFT, TO_THE_MEMBERS } ShopTabChoice;
void menu_shop_on_the_tabs_frame(GameState *g);
ShopTabChoice menu_shop_choose_tab(const GameState *g);

/* The held cursor stops: going to the members, or a tab chosen (the panel
 * then forgets its place); which tab. */
void menu_shop_stop_holding(GameState *g);
void menu_shop_leave_the_tabs(GameState *g);
bool menu_shop_buying_chosen(const GameState *g);

/* Back from a list: the cursor on its tab, the panel for nothing (drawn
 * by the bridge); then every character standing, the tabs' windows, the
 * bar stopped, the held cursor going on. */
void menu_shop_back_from_the_list(GameState *g, bool selling);
void menu_shop_back_on_the_tabs(GameState *g, Platform *pf);

/* ---- The lists ---------------------------------------------------------------------------------- */

/* The goods' list opens on its window and rows; once framed (the
 * original's), the goods' counts and the bar. The list for sale opens on
 * its window, is made, framed and settled, then its rows and the bar. */
void menu_shop_open_buying(GameState *g, Platform *pf);
void menu_shop_open_buying_last(GameState *g);
void menu_shop_open_selling(GameState *g, Platform *pf);
void menu_shop_list_for_sale(GameState *g);
void menu_shop_settle_for_sale(GameState *g);
void menu_shop_open_selling_last(GameState *g);
void menu_shop_frame_list(GameState *g);

/* A list's rows, from its top. */
void menu_shop_draw_rows(GameState *g, bool selling);
void menu_shop_draw_for_sale(GameState *g);

typedef enum { SHOP_ROW_POINTED_AT, SHOP_ROW_CHOSEN, SHOP_LIST_LEFT } ShopListChoice;
ShopListChoice menu_shop_choose_on_the_list(const GameState *g);

/* Pointing along: the top noted for the paging (the original's), and a
 * page turned where it moved; else (the cursor moved by the original) a
 * push past the rows slides the list a row where there is one: its line
 * table's move set up, the row coming in drawn, the frames (whether the
 * slide takes any), the rows at rest. Then the row taken, the panel to be
 * drawn again where its place is another, and the list queued. */
uint8_t menu_shop_list_top(const GameState *g, bool selling);
void menu_shop_turn_page(GameState *g, bool selling, uint8_t top);
bool menu_shop_list_slide_brings_a_row(GameState *g, bool selling);
void menu_shop_set_up_list_slide(GameState *g);
void menu_shop_bring_in_a_row(GameState *g, bool selling);
bool menu_shop_list_slide_takes_frames(GameState *g);
bool menu_shop_slide_list_a_frame(GameState *g);
void menu_shop_finish_list_slide(GameState *g);
void menu_shop_take_the_row(GameState *g, bool selling);
bool menu_shop_another_place(GameState *g);
void menu_shop_queue_the_list(GameState *g);

/* ---- Buying and selling ------------------------------------------------------------------------- */

/* A row chosen: what the party can take of it (room in its ninety-nine,
 * then gold for one to buy; one it has, then room in its gold to sell),
 * refused where none; then the quantity window opens on one. */
uint8_t menu_shop_held_and_worn(const GameState *g);
bool menu_shop_room_to_buy(GameState *g);
bool menu_shop_gold_buys_some(GameState *g);
bool menu_shop_some_to_sell(GameState *g);
bool menu_shop_room_for_the_gold(GameState *g);
void menu_shop_open_quantity(GameState *g, Platform *pf, ShopStep step);

typedef enum { QUANTITY_CHANGED, QUANTITY_TAKEN, QUANTITY_LEFT } ShopQuantityChoice;
void menu_shop_hold_the_quantity(GameState *g);
ShopQuantityChoice menu_shop_choose_quantity(const GameState *g);

/* Up, down, left and right change the quantity by ten or one, within one
 * and the most the party can take; the price with it. Whether it changed. */
bool menu_shop_change_quantity(GameState *g, Platform *pf);
bool menu_shop_quantity_pushed(const GameState *g);

/* The deal: the goods the quantity window shows added to the party's items,
 * the gold paid and the goods counted again; or the items taken and the
 * gold received, at most the most the party holds. */
void menu_shop_pay(GameState *g);
void menu_shop_bought(GameState *g);
void menu_shop_sold(GameState *g);

/* Back on the list, the window closed: on the goods; on the list for sale,
 * the bar's blink started over first, its rows drawn once it is made again. */
void menu_shop_back_to_buying(GameState *g);
void menu_shop_restart_the_bars_blink(GameState *g);
void menu_shop_back_to_selling(GameState *g);

/* ---- The equipping from the shop (game/menu/shop_equipment.c) ---------------------------------- */

/* Down from the tabs to the members, those the player can't choose among
 * dimmed; on them, choosing one, or going back up (a push up or down too). */
void menu_shop_go_down_to_the_members(GameState *g, Platform *pf);
typedef enum { SHOP_MEMBER_POINTED_AT, SHOP_MEMBER_CHOSEN, SHOP_MEMBERS_LEFT } ShopMembersChoice;
ShopMembersChoice menu_shop_choose_a_member(const GameState *g);
void menu_shop_back_up_to_the_tabs(GameState *g, Platform *pf);

/* A member chosen: loaded and its equipment window drawn; a frame later
 * (the bridge marks the slot on the cursor and makes its list), the
 * window's effects, nothing described, the others dimmed. */
void menu_shop_choose_the_member(GameState *g, Platform *pf);
void menu_shop_open_the_members_slots(GameState *g);
void menu_shop_mark_the_slot(GameState *g);
void menu_shop_place_the_lists_bar(GameState *g);

/* On the slots: the shoulders switch the member (to the one returned,
 * which is the same where they weren't pressed); choosing a slot with
 * something to fit it goes into its list; going back returns to the
 * members. */
uint8_t menu_shop_member_by_the_shoulders(const GameState *g);
void menu_shop_switch_member(GameState *g, Platform *pf);
typedef enum { SHOP_SLOT_POINTED_AT, SHOP_SLOT_CHOSEN, SHOP_SLOT_WITH_NOTHING_TO_FIT, SHOP_SLOTS_LEFT } ShopSlotsChoice;
ShopSlotsChoice menu_shop_choose_a_slot(const GameState *g);
void menu_shop_into_the_list(GameState *g);
void menu_shop_back_to_the_members(GameState *g);

/* On the list: the item on the cursor tried on, the figures compared;
 * chosen, put on; going back (or once it is on), out of the list. */
void menu_shop_try_on(GameState *g, Platform *pf);
void menu_shop_compare_figures(GameState *g, Platform *pf);
void menu_shop_after_putting_on(GameState *g);
void menu_shop_out_of_the_list(GameState *g, Platform *pf);

/* ---- What the steps can run on (the guards') ----------------------------------------------------- */

bool menu_shop_can_open(const GameState *g);
bool menu_shop_members_are_shown(const GameState *g);
bool menu_shop_on_a_list(const GameState *g, bool selling);
bool menu_shop_gold_within_the_most(const GameState *g);

/* ---- The entries (game/menu/shop_bridge.c) ------------------------------------------------------- */

void menu_open_shop(Snes *s);
void menu_set_up_shop(Snes *s);
void menu_show_shop_item_panel(Snes *s);
void menu_choose_shop_tab(Snes *s);
void menu_frame_shop_list(Snes *s);
void menu_open_goods_list(Snes *s);
void menu_list_for_sale(Snes *s);
void menu_open_for_sale_list(Snes *s);
void menu_choose_goods(Snes *s);
void menu_choose_for_sale(Snes *s);
void menu_choose_how_many_to_buy(Snes *s);
void menu_choose_how_many_to_sell(Snes *s);

void menu_choose_shop_member(Snes *s);
void menu_open_shop_member(Snes *s);
void menu_show_shop_member_slots(Snes *s);
void menu_switch_shop_member(Snes *s);
void menu_choose_shop_slot(Snes *s);
void menu_choose_shop_equipment(Snes *s);
void menu_frame_shop_quantity(Snes *s);
void menu_describe_nothing(Snes *s);
void menu_go_down_to_shop_members(Snes *s);
void menu_try_on_shop_equipment(Snes *s);
void menu_compare_shop_figures(Snes *s);
void menu_mark_shop_slot(Snes *s);

bool menu_shop_opens(Snes *s);
bool menu_shop_sets_up(Snes *s);
bool menu_shop_tabs_step_runs(Snes *s);
bool menu_shop_buying_step_runs(Snes *s);
bool menu_shop_selling_step_runs(Snes *s);
bool menu_shop_quantity_step_runs(Snes *s);
bool menu_shop_list_opens(Snes *s);
bool menu_shop_list_frames(Snes *s);
bool menu_shop_panel_shows(Snes *s);
bool menu_shop_members_step_runs(Snes *s);
bool menu_shop_slots_step_runs(Snes *s);
bool menu_shop_equipping_runs(Snes *s);
bool menu_shop_goes_down(Snes *s);

#endif
