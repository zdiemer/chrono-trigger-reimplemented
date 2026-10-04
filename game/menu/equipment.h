/* The equipment screen and the main menu's panel for it: what the game does
 * (game/menu/equipment.c) and the entries that run it on the original's
 * machine (game/menu/equipment_bridge.c). */
#ifndef CT_GAME_MENU_EQUIPMENT_H
#define CT_GAME_MENU_EQUIPMENT_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/video.h"
#include "snes/system.h"
#include "state.h"

/* ---- Opening it, and the main menu's panel ---------------------------------------------------- */

/* The opening step leads on to choosing a member. */
void menu_equip_open(GameState *g);

/* The screen set up: its windows and colours, the member list's hidden rows
 * cleared, the cursor on the member the main menu had. */
void menu_equip_set_up_screen(GameState *g, Platform *pf);
/* The screen's colours and windows queued once the scroll bar is up. */
void menu_equip_show_screen(GameState *g);

/* The main menu's panel for the screen: the member list and the card,
 * dimmed, beside the main menu. Its parts come between the frames it waits
 * for and the card's drawing. */
void menu_equip_panel_lay_out_list(GameState *g, Platform *pf);
void menu_equip_panel_draw_members(GameState *g, Platform *pf);
void menu_equip_panel_set_colour_math(GameState *g, Platform *pf);
void menu_equip_panel_dim(GameState *g);

/* A panel's colours: the screen's first 24 dimmed (each channel halved) into
 * the palette from colour `first`. */
void menu_equip_dim_colours(GameState *g, unsigned first);

/* ---- Choosing a member ------------------------------------------------------------------------ */

typedef enum { MEMBER_POINTED_AT, MEMBER_CHOSEN, MEMBER_LEFT_FOR_THE_MAIN_MENU } MemberChoice;

/* What the frame's input does: choosing the member (or pushing right, to
 * its slots) moves on to the slots, with their list of what fits to be drawn
 * again; going back leaves for the main menu; otherwise the cursor points at
 * a member. */
MemberChoice menu_equip_choose_member(GameState *g);

/* Pointing at a member: with more members than the list shows, a push past
 * the list's top or bottom slides it a member along; with fewer, the cursor
 * goes round. Then the list's rows are coloured for the cursor, and whether
 * the cursor points at another member than before (the card is drawn for it
 * either way). */
bool menu_equip_member_list_scrolls(const GameState *g);
bool menu_equip_push_slides_members(const GameState *g);
void menu_equip_take_cursor_round_members(GameState *g);
bool menu_equip_point_at_member(GameState *g);

/* The member list sliding a member along (its start set by the original's
 * $C2:9D5B): the member coming in drawn beside the list and dimmed, then a
 * frame at a time the rows move four lines, the scroll bar's thumb with them;
 * then the three rows now in view put back in the list's place. A frame's
 * move returns whether frames are left. */
void menu_equip_start_member_slide(GameState *g, Platform *pf);
bool menu_equip_slide_members_a_frame(GameState *g);
void menu_equip_finish_member_slide(GameState *g);

/* ---- Choosing a slot -------------------------------------------------------------------------- */

typedef enum { SLOT_POINTED_AT, SLOT_CHOSEN, SLOT_REFUSED, SLOT_LEFT_FOR_THE_MEMBERS } SlotChoice;

/* What the frame's input does: going back (or pushing left) returns to the
 * members; choosing a slot moves on to its list of what fits, or is refused
 * when nothing fits; otherwise the cursor points at a slot. */
SlotChoice menu_equip_choose_slot(GameState *g);
void menu_equip_start_choosing_an_item(GameState *g);

/* Pointing at a slot: the shoulder assignments switch to the next or the
 * previous member (whether they did), the slot kept (whether the cursor moved
 * to another), the slots shown, and whether the list of what fits is for
 * another slot or member than the one drawn. */
bool menu_equip_switch_member(GameState *g);
bool menu_equip_keep_slot(GameState *g);
void menu_equip_show_slots(GameState *g);
bool menu_equip_list_is_for_another_slot(GameState *g);

/* ---- Choosing an item ------------------------------------------------------------------------- */

typedef enum { ITEM_POINTED_AT, ITEM_CHOSEN, ITEM_LEFT_FOR_THE_SLOTS } ItemChoice;
ItemChoice menu_equip_choose_item(const GameState *g);

/* Putting the chosen item on: what the member has on in the slot goes back
 * to the party's items, and the chosen item, the one the figures were drawn
 * with, goes on and is taken from them. A weapon changed clears the story's
 * byte for the character. */
void menu_equip_put_on_the_chosen_item(GameState *g);
void menu_equip_forget_weapon(GameState *g);

/* Back to the slots, the cursor on the one it left; then the list of what
 * fits to be drawn again for it. */
void menu_equip_return_to_the_kept_slot(GameState *g);
void menu_equip_back_to_the_slots(GameState *g);

/* The list of what fits, pointed along: a page on or back with the shoulder
 * assignments (its paging, the original's, says where to), or, pushed past
 * the rows it shows, a slide a row along, the row coming in drawn below the
 * list and the rows moved over frames, unless a held push repeats fast. Its
 * rows are queued once drawn. */
void menu_equip_queue_list_rows(GameState *g);
void menu_equip_turn_list_page(GameState *g, uint8_t top);
uint8_t menu_equip_list_row(const GameState *g);
bool menu_equip_list_slide_brings_a_row(const GameState *g, uint8_t row);
void menu_equip_set_up_list_slide(GameState *g);
void menu_equip_bring_in_list_row(GameState *g, uint8_t row);
bool menu_equip_start_list_slide_frames(GameState *g);
void menu_equip_slide_list_a_frame(GameState *g);
bool menu_equip_list_slide_frames_left(GameState *g);
void menu_equip_finish_list_slide(GameState *g);

/* The list's rows moved a row along once it has slid: the way it slid. */
void menu_equip_move_list_rows(GameState *g);

/* The main menu leaving the equipment: its colours and line effects back. */
void menu_equip_panel_colours_back(GameState *g);
void menu_equip_panel_line_effects_back(GameState *g);

/* The list's rows' upload, queued as the list is drawn. */
enum { MENU_EQUIP_LIST_ROWS_UPLOAD = 0xFC1B };

/* What the steps can run on, for their guards: the screen's set-up, the
 * member list's slide as its start (the original's) left it, the panel. */
bool menu_equip_screen_can_be_set_up(const GameState *g);
bool menu_equip_member_slide_can_run(const GameState *g);
bool menu_equip_panel_can_be_shown(const GameState *g);

/* ---- The windows (game/menu/equipment_windows.c) ---------------------------------------------- */

/* What the member has on, a row a slot from `at` (the card's and the slots
 * window's). */
void menu_equip_draw_equipped(GameState *g, uint16_t at);

/* The slots window: the member's slots shown once chosen (its row at the
 * list's top, the cursor on the first slot); the slot the cursor is on
 * taken, its label drawn; the slots' marks, the cursor's slot undimmed. */
void menu_equip_show_slots_of_member(GameState *g, Platform *pf);
void menu_equip_take_slot(GameState *g);
void menu_equip_mark_slots(GameState *g);
void menu_equip_mark_slots_at(GameState *g, uint16_t at);

/* A window's slide, a frame at a time: the slots window's band heights,
 * frames and the member rows' wait and step; the list window's two lines
 * and its step. The bridges keep it between the frames they wait. */
typedef struct {
    uint8_t top, middle, bottom;
    uint8_t frame, rows_from;
    uint16_t rows_step;
} MenuWindowSlide;

typedef struct {
    uint16_t list_line, slots_line;
    int16_t step;
} MenuListSlide;

/* The slots window sliding in (the member rows moving up), and out again
 * once the slots are left (after the members' rows are drawn again); each
 * frame returns whether frames are left. Then the members at rest: the
 * member loaded and drawn on the card, and (once the original's member
 * scroll bar is set up) the rows' table at rest. */
void menu_equip_start_slots_slide(GameState *g, Platform *pf, MenuWindowSlide *w);
bool menu_equip_slide_slots_a_frame(GameState *g, MenuWindowSlide *w);
void menu_equip_clear_slot_labels(GameState *g, Platform *pf);
void menu_equip_start_members_slide(GameState *g, MenuWindowSlide *w);
bool menu_equip_slide_members_back_a_frame(GameState *g, MenuWindowSlide *w);
void menu_equip_back_on_the_members(GameState *g, Platform *pf);
void menu_equip_rest_the_member_rows(GameState *g);

/* The list window: sliding in, its frame and scroll bar; the list of what
 * fits laid out (once the original's $C2:8953 has made it), its bar (after
 * the original's frame), drawn; the band table copied whole, the item the
 * original describes as it opens, the cursor on the list; and sliding out,
 * the bands back at rest. A row of the list for menu_item_operand. */
void menu_equip_start_list_slide(GameState *g, Platform *pf, MenuListSlide *w);
bool menu_equip_slide_list_in_a_frame(GameState *g, MenuListSlide *w);
void menu_equip_show_list_window(GameState *g, Platform *pf);
void menu_equip_place_list_bar(GameState *g, bool again);
void menu_equip_lay_out_list(GameState *g);
void menu_equip_set_list_bar(GameState *g);
void menu_equip_draw_list(GameState *g);
uint16_t menu_equip_list_end(void);
void menu_equip_draw_list_row(GameState *g, uint16_t at);
void menu_equip_open_the_list(GameState *g);
uint8_t menu_equip_item_described_on_opening(const GameState *g);
void menu_equip_settle_the_list(GameState *g);
void menu_equip_start_list_closing(GameState *g, Platform *pf, MenuListSlide *w);
bool menu_equip_slide_list_out_a_frame(GameState *g, MenuListSlide *w);
void menu_equip_rest_the_list_bands(GameState *g);

/* Back on the slots from the list: the member's row and the slots' marks
 * drawn again (the card before them still the original's); then, once the
 * list window has closed and the list is drawn again, the slots at rest. */
void menu_equip_redraw_slots(GameState *g, Platform *pf);
void menu_equip_slots_at_rest(GameState *g);

/* ---- The entries (game/menu/equipment_bridge.c) ----------------------------------------------- */

void menu_open_equipment(Snes *s);
void menu_set_up_equipment(Snes *s);
void menu_choose_equipment_member(Snes *s);
void menu_point_at_equipment_member(Snes *s);
void menu_slide_equipment_members(Snes *s);
void menu_choose_equipment_slot(Snes *s);
void menu_point_at_equipment_slot(Snes *s);
void menu_switch_equipment_member(Snes *s);
void menu_choose_equipment_item(Snes *s);
void menu_point_along_equipment_list(Snes *s);
void menu_move_equipment_list_rows(Snes *s);
void menu_forget_weapon(Snes *s);
void menu_show_equipment_panel(Snes *s);
void menu_dim_panel_colours(Snes *s);
void menu_show_panel_colours(Snes *s);
void menu_back_to_equipment_members(Snes *s);
void menu_open_equipment_list(Snes *s);
void menu_redraw_equipment_list(Snes *s);
void menu_make_equipment_list(Snes *s);
void menu_slide_equipment_list_in(Snes *s);
void menu_slide_equipment_list_out(Snes *s);
void menu_back_to_equipment_slots(Snes *s);
void menu_open_slots_window(Snes *s);
void menu_close_slots_window(Snes *s);
void menu_show_member_slots(Snes *s);
void menu_take_equipment_slot(Snes *s);
void menu_mark_equipment_slots(Snes *s);
void menu_draw_equipment_list(Snes *s);
void menu_draw_equipment_list_row(Snes *s);
void menu_draw_equipped_items(Snes *s);

bool menu_equipment_members_come_back(Snes *s);
bool menu_equipment_list_opens(Snes *s);
bool menu_equipment_list_is_drawable(Snes *s);
bool menu_equipment_list_slides(Snes *s);
bool menu_equipment_slots_come_back(Snes *s);
bool menu_equipment_slots_window_slides(Snes *s);
bool menu_equipment_slots_are_showable(Snes *s);
bool menu_equipment_slots_are_markable(Snes *s);
bool menu_equipped_items_are_drawable(Snes *s);

bool menu_equipment_opens(Snes *s);
bool menu_equipment_sets_up(Snes *s);
bool menu_equipment_member_step_runs(Snes *s);
bool menu_equipment_member_is_pointable(Snes *s);
bool menu_equipment_members_slide(Snes *s);
bool menu_equipment_slot_step_runs(Snes *s);
bool menu_equipment_slot_is_pointable(Snes *s);
bool menu_equipment_member_switches(Snes *s);
bool menu_equipment_item_step_runs(Snes *s);
bool menu_equipment_list_is_pointable(Snes *s);
bool menu_equipment_list_rows_move(Snes *s);
bool menu_equipment_weapon_is_forgettable(Snes *s);
bool menu_equipment_panel_shows(Snes *s);
bool menu_panel_colours_dim(Snes *s);
bool menu_panel_colours_show(Snes *s);

#endif
