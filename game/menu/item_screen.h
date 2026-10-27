/* The items screen and the main menu's panel for it: what the game does
 * (game/menu/item_screen.c) and the entries that run it on the original's
 * machine (game/menu/item_screen_bridge.c). */
#ifndef CT_GAME_MENU_ITEM_SCREEN_H
#define CT_GAME_MENU_ITEM_SCREEN_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/video.h"
#include "snes/system.h"
#include "state.h"

/* The screen's steps, as its main task runs them (menu_screen_step). */
typedef enum {
    ITEMS_OPENING = 0,
    ITEMS_ON_THE_COMMANDS = 1,
    ITEMS_ON_THE_LIST = 2,
    ITEMS_MOVING_ONE = 3,
    ITEMS_SORTING = 4,
    ITEMS_SHOWING_USERS = 5,
} ItemsStep;

/* ---- Opening it ------------------------------------------------------------------------------- */

/* The screen opens on the list: its window, the scroll bar's place and the
 * track its thumb runs along, and the list's frame of ten rows of the
 * party's items (the original's $C2:EA81 lays the frame out from them); then
 * the bar at the list's first place, the cursor on the row it was left on,
 * and the screen shown. */
void menu_items_open(GameState *g);
void menu_items_lay_out(GameState *g, Platform *pf);
void menu_items_show_bar_at_the_list(GameState *g);
void menu_items_put_the_cursor_back(GameState *g);
void menu_items_show_screen(GameState *g);

/* The list's frame: ten rows shown of the party's items. */
enum { MENU_ITEMS_ROWS = 10 };

/* ---- The commands above the list -------------------------------------------------------------- */

typedef enum { COMMAND_POINTED_AT, COMMAND_LIST_CHOSEN, COMMAND_SORT_CHOSEN, COMMAND_NOTHING_CHOSEN, COMMANDS_LEFT } ItemsCommandChoice;

/* What the frame's input does on the commands: choosing the list goes down
 * into it, choosing the sort sorts the party's items, going back leaves for
 * the main menu. */
ItemsCommandChoice menu_items_choose_command(const GameState *g);
void menu_items_take_command(GameState *g, ItemsCommandChoice c);

/* ---- The list --------------------------------------------------------------------------------- */

typedef enum { LIST_POINTED_AT, LIST_ITEM_PICKED_UP, LIST_LEFT } ItemsListChoice;

/* On the list nothing is picked up; choosing picks the item on the cursor
 * up (a second cursor stays on it) to put it down somewhere else; going back
 * returns to the commands. */
void menu_items_put_nothing_in_hand(GameState *g);
ItemsListChoice menu_items_choose_on_the_list(const GameState *g);
void menu_items_pick_up(GameState *g);
void menu_items_back_to_the_commands(GameState *g);

typedef enum { MOVE_POINTED_AT, MOVE_PUT_DOWN, MOVE_ON_ITSELF, MOVE_LEFT } ItemsMoveChoice;

/* With an item picked up: choosing another place swaps the two, choosing
 * the item itself uses it, going back puts it down where it was. */
ItemsMoveChoice menu_items_choose_where(const GameState *g);
void menu_items_swap_with_the_picked(GameState *g);
void menu_items_back_to_the_list(GameState *g);

/* What using the picked item does: an item the menu uses (a tonic, a tab),
 * a weapon or armour (who can equip it is shown), or nothing. */
typedef enum { USING_FROM_THE_MENU, USING_SHOWS_USERS, USING_DOES_NOTHING } ItemsUse;
ItemsUse menu_items_use_of_the_picked(GameState *g);
void menu_items_start_showing_users(GameState *g);
void menu_items_back_to_the_list_unchanged(GameState *g);

/* Who can equip the item: their names, the item's attack or defence, in a
 * window over the list; any button closes it. */
void menu_items_show_users(GameState *g, Platform *pf);
bool menu_items_users_closed(GameState *g);
void menu_items_close_users(GameState *g);

/* The party's items sorted, the list drawn again and the cursor back on the
 * commands. */
void menu_items_sort(GameState *g);
void menu_items_after_sorting(GameState *g);

/* ---- Pointing along the list ------------------------------------------------------------------ */

/* The list pages with the shoulder assignments (menu_turn_page); a page
 * turned to a new first row draws every row again. */
void menu_items_turn_page(GameState *g, uint8_t top);

/* A push past the list's rows slides it a row along, where there is a row:
 * not before the first of the party's items nor past the last. The row
 * coming in is drawn below the list (above it for a slide up) and the rows
 * move over frames (each the original's $C2:EB1F), unless a held push
 * repeats fast; the second cursor moves with them. */
bool menu_items_slide_brings_a_row(const GameState *g);
void menu_items_set_up_slide(GameState *g);
void menu_items_bring_in_a_row(GameState *g);
bool menu_items_slide_takes_frames(GameState *g);
bool menu_items_slide_a_frame(GameState *g);
void menu_items_end_slide_frames(GameState *g);
void menu_items_finish_slide(GameState *g);

/* The item on the cursor noted (its description is drawn for it by the
 * original's $C2:F2F3), and the cursor's row kept: whether the cursor moved
 * this frame. */
uint8_t menu_items_note_the_cursors_item(GameState *g);
bool menu_items_keep_the_cursors_row(GameState *g);
void menu_items_queue_rows(GameState *g);

/* ---- Drawing the list ------------------------------------------------------------------------- */

/* A row of the list: the item's name, a colon and how many the party has,
 * and what kind of item it is, in the colours of an item the menu can use
 * where it is one. The ten rows from the list's first place, the cursor's
 * row on its own, and a row at `at` for the list's row `row`. */
void menu_items_draw_rows(GameState *g);
void menu_items_redraw_the_cursors_row(GameState *g);
void menu_items_draw_row(GameState *g, uint16_t at, uint8_t row);

/* ---- The main menu's panel -------------------------------------------------------------------- */

/* With the main menu's cursor on the items, the list is drawn beside it: its
 * window and colours, a frame, the rows (where the story shows them), a
 * frame, and the colour math and the sprites. The main menu leaving the
 * items puts their colours back. */
void menu_items_panel_lay_out(GameState *g, Platform *pf);
void menu_items_panel_draw_rows(GameState *g);
void menu_items_panel_show(GameState *g, Platform *pf);
void menu_items_panel_put_away(GameState *g);

/* ---- Using an item ---------------------------------------------------------------------------- */

/* What an item the menu can use does, from its data: restore HP or MP (one
 * member, or every member at once), restore by chance, rest the whole party,
 * raise one of a member's figures; or nothing. */
typedef enum { USE_RESTORES, USE_DOES_NOTHING, USE_RESTORES_BY_CHANCE, USE_RESTS_THE_PARTY, USE_RAISES_A_FIGURE } ItemUseKind;
ItemUseKind menu_items_look_up_use(GameState *g, uint8_t item);

/* What a tonic does, as its effect's bits say (menu_use_effect): it
 * restores HP, MP, and for every member at once. */
enum { MENU_USE_RESTORES_HP = 0x80, MENU_USE_RESTORES_MP = 0x40, MENU_USE_FOR_EVERY_MEMBER = 0x08 };

/* The tabs that raise power and magic (any other raises speed). */
enum { MENU_POWER_TAB = 0xCD, MENU_MAGIC_TAB = 0xCE };

/* The restoring's strength (a tonic's, by its effect), the chance's draw,
 * the party's rest; a tab's figure. */
void menu_items_set_restore_strength(GameState *g);
void menu_items_draw_a_chance(GameState *g);
void menu_items_set_party_rest(GameState *g);
void menu_items_set_figure_raised(GameState *g, uint8_t item);

/* The member window an item is used on: the cursor on its first row (the
 * second cursor there too), opened (its window and its lines, the members'
 * rows and their sprites, held back while their task starts and let through
 * a frame later); and closed again (its layer cleared once the records are
 * reset). */
void menu_items_start_member_choice(GameState *g);
void menu_items_open_member_window(GameState *g, Platform *pf);
void menu_items_lay_out_member_window_lines(GameState *g);
void menu_items_draw_members(GameState *g, Platform *pf);
void menu_items_hide_member_sprites(GameState *g);
void menu_items_show_member_sprites(GameState *g);
void menu_items_close_member_window(GameState *g);
void menu_items_clear_member_window(GameState *g);

/* The window's input: going back closes it, choosing uses the item on the
 * member pointed at, where it helps them. A tonic for every member has a
 * cursor on each. */
typedef enum { MEMBER_POINTED_AT_TO_USE_ON, MEMBER_CHOSEN_TO_USE_ON, MEMBERS_LEFT } ItemsMemberChoice;
ItemsMemberChoice menu_items_choose_member(const GameState *g);
bool menu_items_use_helps(const GameState *g);
bool menu_items_restores_every_member(const GameState *g);

/* A tab's member: the cursor taken round the rows, the member pointed at
 * (whether another than before), then loaded and its figure looked at; and
 * the tab used on it: the raise shown, then the figure raised. */
void menu_items_take_the_cursor_round(GameState *g);
bool menu_items_point_at_a_member(GameState *g);
void menu_items_look_at_the_member(GameState *g);
void menu_items_after_looking(GameState *g);
void menu_items_show_the_raise(GameState *g);
void menu_items_raise_the_figure(GameState *g);

/* One more used: one taken from the party's items (its next chance drawn
 * first, for an item used by chance), the row drawn again, and where none
 * are left the item does nothing more; the picked item for its description;
 * and after the members' rows are drawn again, their sprites. */
void menu_items_use_one(GameState *g);
uint8_t menu_items_picked_item(const GameState *g);
void menu_items_after_one_used(GameState *g);

/* ---- What the steps can run on (the guards') -------------------------------------------------- */

bool menu_items_list_is_pointable(const GameState *g);
bool menu_items_rows_can_be_drawn(const GameState *g);

/* What the steps below take of the state, as their entries' guards do: the
 * opening, the list's step (and pointing at an item on it), the sort. */
bool menu_items_can_open(const GameState *g);
bool menu_items_can_point_along_the_list(const GameState *g);
bool menu_items_can_sort(const GameState *g);

/* An item's description drawn (item 0, an empty place's, a blank one);
 * the item on the cursor noted and its description drawn ($C2:AD2E). */
void menu_items_describe(GameState *g, uint8_t item);
void menu_items_describe_the_cursors_item(GameState *g);

/* ---- The steps, one sequence for the machine and for C ---------------------------------------- */

/* The screen's opening ($C2:AA40), its commands ($C2:AA4A), its list
 * ($C2:AA86) and the pointing along it ($C2:AC3A, $C2:AC59), and the sort
 * ($C2:AB4A) are each one sequence, which the machine's entries
 * (item_screen_bridge.c) and the program as C both run, asking the menus for
 * what differs through ItemsSteps.
 *
 * The places the sequences tick or wait from. */
typedef enum {
    ITEMS_AT_THE_OPENING,         /* the screen opening */
    ITEMS_AT_A_COMMAND_POINTED,   /* the cursor moved among the commands */
    ITEMS_AT_A_COMMAND_CHOSEN,    /* a command chosen */
    ITEMS_AT_AN_ITEM_PICKED_UP,   /* an item picked up off the list */
    ITEMS_AT_THE_LIST_LEFT,       /* back from the list to the commands */
    ITEMS_AT_A_LISTED_ITEM_POINTED, /* the cursor moved to another row */
    ITEMS_AT_THE_SORT,            /* the sort made, a frame's wait */
} ItemsPlace;

/* Where the machine does what only it needs (main_menu.h's MainMenuNote
 * says how): the flags the original's tests left, and the P a routine saves
 * and the widths it sets, at these points. */
typedef enum {
    ITEMS_NOTE_OPENED,              /* the screen opened, its set-up starting */
    ITEMS_NOTE_LIST_FRAMED,         /* the list's frame drawn */
    ITEMS_NOTE_SET_UP,              /* the set-up done */
    ITEMS_NOTE_BUTTONS_TESTED,      /* the cursor's buttons tested */
    ITEMS_NOTE_CURSOR_COMPARED,     /* the cursor compared with where it started */
    ITEMS_NOTE_PICKED_UP,           /* an item picked up, the step ending */
    ITEMS_NOTE_POINTING,            /* pointing along the list starting */
    ITEMS_NOTE_PAGING_ANSWERED,     /* the paging's answer: a page turned (1) or not */
    ITEMS_NOTE_ROWS_REDRAWN,        /* a page's rows drawn again */
    ITEMS_NOTE_PLACE_COMPARED,      /* the cursor's row kept and compared */
    ITEMS_NOTE_ROWS_QUEUED,         /* the list's rows queued */
    ITEMS_NOTE_CURSOR_KEPT,         /* the cursor kept on the list */
    ITEMS_NOTE_ENDS_COMPARED,       /* the list's ends compared, no slide */
} ItemsNote;

/* What the sequences ask: the cursor read, a tick, a frame's wait, the
 * item on the cursor described, the list slid a row, the way back to the
 * main menu, a note. Each false where it went no further. */
typedef struct {
    void (*read_cursor)(void *ctx);
    bool (*tick)(void *ctx, ItemsPlace at);
    bool (*wait)(void *ctx, ItemsPlace at);
    bool (*describe)(void *ctx);
    bool (*slide_the_list)(void *ctx);
    bool (*back_to_the_main_menu)(void *ctx);
    void (*note)(void *ctx, ItemsNote note, uint8_t value);
} ItemsSteps;

bool menu_open_items_with(GameState *g, Platform *pf, const ItemsSteps *n, void *ctx);
bool menu_choose_item_command_with(GameState *g, Platform *pf, const ItemsSteps *n, void *ctx);
bool menu_choose_on_item_list_with(GameState *g, Platform *pf, const ItemsSteps *n, void *ctx);
bool menu_point_along_the_item_list_with(GameState *g, Platform *pf, const ItemsSteps *n, void *ctx);
bool menu_point_at_listed_item_with(GameState *g, Platform *pf, const ItemsSteps *n, void *ctx);
bool menu_sort_items_with(GameState *g, Platform *pf, const ItemsSteps *n, void *ctx);

/* As C, from the menus' main task; false where the program went no
 * further. */
bool menu_open_items(GameState *g, Platform *pf);
bool menu_choose_item_command(GameState *g, Platform *pf);
bool menu_choose_on_item_list(GameState *g, Platform *pf);
bool menu_sort_items(GameState *g, Platform *pf);

/* ---- The entries (game/menu/item_screen_bridge.c) --------------------------------------------- */

void menu_open_items_entry(Snes *s);
void menu_choose_item_command_entry(Snes *s);
void menu_choose_on_item_list_entry(Snes *s);
void menu_move_picked_item(Snes *s);
void menu_sort_items_entry(Snes *s);
void menu_close_item_users(Snes *s);
void menu_point_at_listed_item(Snes *s);
void menu_describe_listed_item(Snes *s);
void menu_show_items_panel(Snes *s);
void menu_put_items_panel_away(Snes *s);
void menu_use_item(Snes *s);
void menu_use_restoring_item(Snes *s);
void menu_use_item_by_chance(Snes *s);
void menu_rest_the_party(Snes *s);
void menu_use_tab(Snes *s);
void menu_open_member_window(Snes *s);
void menu_point_at_tab_member(Snes *s);
void menu_look_at_tab_member(Snes *s);
void menu_raise_figure(Snes *s);
void menu_use_one_more(Snes *s);
void menu_draw_item_rows(Snes *s);
void menu_draw_item_row(Snes *s);
void menu_sort_party_items(Snes *s);
void menu_show_item_users(Snes *s);

bool menu_items_screen_opens(Snes *s);
bool menu_item_command_step_runs(Snes *s);
bool menu_item_list_step_runs(Snes *s);
bool menu_item_move_step_runs(Snes *s);
bool menu_items_sort_step_runs(Snes *s);
bool menu_item_users_step_runs(Snes *s);
bool menu_listed_item_is_pointable(Snes *s);
bool menu_items_panel_shows(Snes *s);
bool menu_items_panel_goes(Snes *s);
bool menu_item_is_usable(Snes *s);
bool menu_item_use_runs(Snes *s);
bool menu_member_window_opens(Snes *s);
bool menu_tab_member_is_pointable(Snes *s);
bool menu_figure_is_raisable(Snes *s);
bool menu_one_more_is_usable(Snes *s);
bool menu_item_rows_are_drawable(Snes *s);
bool menu_item_row_is_drawable(Snes *s);
bool menu_party_items_are_sortable(Snes *s);
bool menu_item_users_are_showable(Snes *s);

#endif
