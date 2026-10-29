/* A shop: what the game does (game/menu/shop.c, and the equipping from it
 * in game/menu/shop_equipment.c), its steps as one sequence each for the
 * machine and for C (game/menu/shop_steps.c), and the entries that run them
 * on the original's machine (game/menu/shop_bridge.c). */
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

/* Back from a list: the cursor on its tab, the panel for nothing (its own
 * routine); then every character standing, the tabs' windows, the bar
 * stopped, the held cursor going on. */
void menu_shop_back_from_the_list(GameState *g, bool selling);
void menu_shop_back_on_the_tabs(GameState *g, Platform *pf);

/* ---- The lists ---------------------------------------------------------------------------------- */

/* The goods' list opens on its window and rows; once framed, the goods'
 * counts and the bar. The list for sale opens on its window, is made,
 * framed and settled (the cursor and the list's top kept within it, made
 * afresh as a sale changes it), then its rows and the bar. */
void menu_shop_open_buying(GameState *g, Platform *pf);
void menu_shop_open_buying_last(GameState *g);
void menu_shop_open_selling(GameState *g, Platform *pf);
void menu_shop_make_the_list_for_sale(GameState *g);
void menu_shop_open_selling_last(GameState *g);
void menu_shop_frame_list(GameState *g, uint8_t goods);

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
 * (the step marks the slot on the cursor and makes its list), the
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

/* The description of nothing (item 0), as the shop opens and comes back to
 * its tabs ($C2:E012). */
void menu_shop_describe_nothing(GameState *g);

/* The goods' list opened whole ($C2:DC7B): its window and rows, framed, the
 * goods' counts and the bar. The list for sale opened whole ($C2:DE98): its
 * window, the list made, its rows, the bar, and queued. */
void menu_shop_open_the_goods(GameState *g, Platform *pf);
void menu_shop_open_the_list_for_sale(GameState *g, Platform *pf);

/* ---- What the steps can run on (the guards') ----------------------------------------------------- */

bool menu_shop_can_open(const GameState *g);
bool menu_shop_members_are_shown(const GameState *g);
bool menu_shop_on_a_list(const GameState *g, bool selling);
bool menu_shop_gold_within_the_most(const GameState *g);

/* The guards' state halves (shop_bridge.c), for the program as C: the shop
 * opened or set up; on the tabs (the panel, a list opened, down to the
 * members); on the goods or the list for sale; on the quantity; on the
 * members; a member chosen; the member's slots and its list (the figures
 * workable with what the party has on, menu_figures_can_be_worked_out). */
bool menu_shop_can_be_opened(const GameState *g);
bool menu_shop_is_on_the_tabs(const GameState *g);
bool menu_shop_is_on_a_list(const GameState *g, bool selling);
bool menu_shop_is_on_the_quantity(const GameState *g);
bool menu_shop_is_on_the_members(const GameState *g);
bool menu_shop_member_can_be_chosen(const GameState *g);
bool menu_shop_member_can_be_equipped(const GameState *g);

/* ---- The shop's steps, one sequence each for the machine and for C ------------------------------ */

/* Each of the shop's steps ($C2:D52B to $C2:D778) and the routines they go
 * on to is one sequence (game/menu/shop_steps.c), which the machine's entries
 * (shop_bridge.c) and the program as C both run, asking for what differs
 * through ShopSteps.
 *
 * The places the sequences tick, wait or set the machine's registers from;
 * where buying and selling run the same code from two places, the selling's
 * follows the buying's. */
typedef enum {
    SHOP_AT_THE_SET_UP_DRAWN,          /* the shop's windows drawn as it opens */
    SHOP_AT_THE_SET_UP_STANDING,       /* ... its members standing, before the panel */
    SHOP_AT_THE_PANEL_MEMBERS,         /* the panel's members, one by one */
    SHOP_AT_THE_TAB_CHOSEN,            /* a tab chosen */
    SHOP_AT_THE_TABS_POINTED,          /* the cursor moved on the tabs */
    SHOP_AT_THE_ROW_TAKEN,             /* the row on the cursor taken, buying */
    SHOP_AT_THE_ROW_TAKEN_FOR_SALE,    /* ... selling */
    SHOP_AT_THE_ROW_BROUGHT_IN,        /* a row slid in, buying */
    SHOP_AT_THE_ROW_BROUGHT_IN_FOR_SALE,
    SHOP_AT_THE_LIST_SLIDING,          /* the list's rows sliding, a frame at a time, buying */
    SHOP_AT_THE_LIST_SLIDING_FOR_SALE,
    SHOP_AT_THE_PAGE_TURNED,           /* the list paged, buying */
    SHOP_AT_THE_PAGE_TURNED_FOR_SALE,
    SHOP_AT_THE_POINTING,              /* the cursor along a list */
    SHOP_AT_THE_TABS_AGAIN,            /* back from a list to the tabs */
    SHOP_AT_THE_OFFER,                 /* the quantity window opening, buying */
    SHOP_AT_THE_OFFER_FOR_SALE,
    SHOP_AT_THE_QUANTITY_CLOSED,       /* back on the list from the quantity, buying */
    SHOP_AT_THE_QUANTITY_CLOSED_FOR_SALE,
    SHOP_AT_THE_QUANTITY_CHANGING,     /* the quantity's change by a push */
    SHOP_AT_THE_QUANTITY_CHANGED,      /* ... made */
    SHOP_AT_THE_MEMBER_CHOSEN,         /* a member chosen */
    SHOP_AT_THE_MEMBERS_LEFT,          /* back up from the members */
    SHOP_AT_THE_MEMBER_POINTED,        /* the cursor among the members */
    SHOP_AT_THE_MEMBERS_WINDOW_DRAWN,  /* the member's equipment window drawn, a frame on */
    SHOP_AT_THE_MEMBER_SWITCHING,      /* the shoulders' member taken */
    SHOP_AT_THE_MEMBER_SWITCHED,       /* ... and shown, a frame on */
    SHOP_AT_THE_SLOT_CHOSEN,           /* a slot chosen */
    SHOP_AT_THE_SLOTS_LEFT,            /* back from the slots to the members */
    SHOP_AT_THE_SLOT_POINTED,          /* the cursor among the slots */
    SHOP_AT_THE_ITEM_POINTED,          /* the cursor along the list of what fits */
    SHOP_AT_THE_ITEM_LEFT,             /* out of the list of what fits */
} ShopPlace;

/* The routines the steps go on to and come back from (each a registered
 * entry, or a sound, run on the machine where the original calls it). */
typedef enum {
    SHOP_SETS_UP,                      /* the shop set up ($C2:DACA) */
    SHOP_SHOWS_THE_PANEL_AS_IT_OPENS,  /* the panel ($C2:DD7C), as the shop opens */
    SHOP_SHOWS_THE_PANEL_FOR_A_GOOD,   /* ... for a row of the goods */
    SHOP_SHOWS_THE_PANEL_FOR_A_SALE,   /* ... for a row for sale */
    SHOP_SHOWS_THE_PANEL_FOR_NOTHING,  /* ... back on the tabs */
    SHOP_PLAYS_THE_DEAL_SOUND,         /* the deal's sound ($C2:EABA), bought */
    SHOP_PLAYS_THE_DEAL_SOUND_FOR_SALE, /* ... sold */
    SHOP_OPENS_THE_MEMBER,             /* a member chosen ($C2:E0A5) */
    SHOP_SHOWS_THE_SLOTS_AS_THEY_OPEN, /* the member's slots ($C2:E0F1), as they open */
    SHOP_SHOWS_THE_SLOTS,              /* ... on the slots' step */
    SHOP_SWITCHES_THE_MEMBER,          /* the shoulders' switch ($C2:E108) */
    SHOP_MAKES_THE_LIST,               /* the equipment screen's list of what fits ($C2:A463) */
    SHOP_POINTS_ALONG_THE_LIST,        /* ... its move along ($C2:9F05) */
    SHOP_PLAYS_THE_PUT_ON_SOUND,       /* the sound of an item put on ($C2:EABA) */
} ShopRoutine;

/* How a step ends: on to the fade in as the shop opens, out of the menus,
 * on to the goods' list or the list for sale, or the refusal's buzz (a
 * jump to it, or a slot's, the original's stop). */
typedef enum {
    SHOP_FADES_IN, SHOP_LEAVES, SHOP_OPENS_THE_GOODS, SHOP_OPENS_THE_LIST_FOR_SALE, SHOP_REFUSES,
    SHOP_REFUSES_A_SLOT,
} ShopEnd;

/* Where the machine leaves what the original's tests and loads left
 * (scaffolding); the value says what the note needs of the state before. */
typedef enum {
    SHOP_NOTE_GOODS_TAKEN, SHOP_NOTE_LOADED, SHOP_NOTE_PANEL_DONE, SHOP_NOTE_BUTTONS_TESTED, SHOP_NOTE_TAB_CHOSEN,
    SHOP_NOTE_STEP_SET, SHOP_NOTE_FRONT_LAYER_QUEUED, SHOP_NOTE_STRIPS_QUEUED, SHOP_NOTE_CURSOR_COMPARED,
    SHOP_NOTE_CURSOR_COMPARED_AS_WORDS, SHOP_NOTE_CURSOR_COMPARED_WITH_THE_START, SHOP_NOTE_PLACE_ADDED,
    SHOP_NOTE_PLACE_COMPARED, SHOP_NOTE_SLIDE_TESTED, SHOP_NOTE_CURSOR_KEPT, SHOP_NOTE_BACK_FROM_THE_LIST,
    SHOP_NOTE_ROOM_REFUSED, SHOP_NOTE_GOLD_REFUSED, SHOP_NOTE_OFFER_TO_BUY, SHOP_NOTE_SALE_REFUSED,
    SHOP_NOTE_OFFER_TO_SELL, SHOP_NOTE_INVENTORY_CHANGED, SHOP_NOTE_GOLD_WORD, SHOP_NOTE_QUANTITY_QUEUED,
    SHOP_NOTE_QUANTITY_COMPARED, SHOP_NOTE_TOTAL_ADDED, SHOP_NOTE_PUSHED_UP_OR_DOWN, SHOP_NOTE_MEMBERS_COMPARED,
    SHOP_NOTE_BAR_PLACE_LOADED,
} ShopNote;

/* What the sequences ask: the cursor read, a tick, a frame's wait, the
 * machine's registers as the original's code sets them, a routine run, a
 * note, the end. Each false where it went no further. */
typedef struct {
    void (*read_cursor)(void *ctx);
    bool (*tick)(void *ctx, ShopPlace at);
    bool (*wait)(void *ctx, ShopPlace at);
    void (*registers_at)(void *ctx, ShopPlace at);
    bool (*run)(void *ctx, ShopRoutine routine);
    void (*note)(void *ctx, ShopNote note, uint32_t value);
    bool (*ends)(void *ctx, ShopEnd end);
} ShopSteps;

typedef bool ShopSequence(GameState *g, Platform *pf, const ShopSteps *n, void *ctx);
ShopSequence menu_open_shop_with, menu_set_up_shop_with, menu_show_shop_item_panel_with, menu_choose_shop_tab_with,
    menu_choose_goods_with, menu_choose_for_sale_with, menu_choose_how_many_to_buy_with,
    menu_choose_how_many_to_sell_with, menu_choose_shop_member_with, menu_open_shop_member_with,
    menu_show_shop_member_slots_with, menu_switch_shop_member_with, menu_choose_shop_slot_with,
    menu_choose_shop_equipment_with;

/* The shop's steps as C, from the menus' main task; false where the program
 * went no further. */
bool menu_open_shop(GameState *g, Platform *pf);
bool menu_choose_shop_tab(GameState *g, Platform *pf);
bool menu_choose_goods(GameState *g, Platform *pf);
bool menu_choose_for_sale(GameState *g, Platform *pf);
bool menu_choose_how_many_to_sell(GameState *g, Platform *pf);
bool menu_choose_how_many_to_buy(GameState *g, Platform *pf);
bool menu_choose_shop_member(GameState *g, Platform *pf);
bool menu_choose_shop_slot(GameState *g, Platform *pf);
bool menu_choose_shop_equipment(GameState *g, Platform *pf);

/* ---- The entries (game/menu/shop_bridge.c) ------------------------------------------------------- */

void menu_open_shop_entry(Snes *s);
void menu_set_up_shop(Snes *s);
void menu_show_shop_item_panel(Snes *s);
void menu_choose_shop_tab_entry(Snes *s);
void menu_frame_shop_list(Snes *s);
void menu_open_goods_list(Snes *s);
void menu_list_for_sale(Snes *s);
void menu_open_for_sale_list(Snes *s);
void menu_choose_goods_entry(Snes *s);
void menu_choose_for_sale_entry(Snes *s);
void menu_choose_how_many_to_buy_entry(Snes *s);
void menu_choose_how_many_to_sell_entry(Snes *s);

void menu_choose_shop_member_entry(Snes *s);
void menu_open_shop_member(Snes *s);
void menu_show_shop_member_slots(Snes *s);
void menu_switch_shop_member(Snes *s);
void menu_choose_shop_slot_entry(Snes *s);
void menu_choose_shop_equipment_entry(Snes *s);
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
bool menu_shop_member_opens(Snes *s);
bool menu_shop_goes_down(Snes *s);

#endif
