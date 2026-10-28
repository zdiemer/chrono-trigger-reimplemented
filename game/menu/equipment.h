/* The equipment screen and the main menu's panel for it: what the game does
 * (game/menu/equipment.c) and the entries that run it on the original's
 * machine (game/menu/equipment_bridge.c). */
#ifndef CT_GAME_MENU_EQUIPMENT_H
#define CT_GAME_MENU_EQUIPMENT_H

#include <stdbool.h>
#include <stdint.h>

#include "menu/members.h"
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

/* The list of what fits the screen's slot and the loaded member, the most
 * powerful first, and the guard's: whether the original's list can be made
 * as it makes it. */
enum { MENU_EQUIP_SLOTS = 4 };
void menu_list_what_fits(GameState *g);
bool menu_list_what_fits_can_be_made(GameState *g);
void menu_list_what_fits_entry(Snes *s);
bool menu_list_what_fits_runs(Snes *s);

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

/* The member list sliding a member along (its start set by
 * menu_equip_slide_members): the member coming in drawn beside the list and dimmed, then a
 * frame at a time the rows move four lines, the scroll bar's thumb with them;
 * then the three rows now in view put back in the list's place. A frame's
 * move returns whether frames are left. */
void menu_equip_start_member_slide(GameState *g, Platform *pf);
MemberListSlide menu_equip_slide_members(GameState *g);
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

/* The list's item on the cursor tried on in the slot, the member's figures
 * marked by how it moves them and the item described ($C2:A5C3); where the
 * figures can't be worked out with it (an item no list of the game's holds),
 * the original's. */
void menu_equip_try_on(GameState *g, Platform *pf);
bool menu_equip_can_try_on(const GameState *g);

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

/* The main menu leaving the equipment: its colours, the party's items and
 * the line effects back. */
void menu_equip_panel_put_away(GameState *g);

/* The list's rows' upload, queued as the list is drawn. */
enum { MENU_EQUIP_LIST_ROWS_UPLOAD = 0xFC1B };

/* What the steps can run on, for their guards: the screen's set-up, the
 * member list's slide as its start (menu_equip_slide_members) left it, the
 * panel. */
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
/* The member list's scroll bar worked out and its task started, where the
 * party has more members to reach than the list's three rows. */
void menu_equip_show_the_member_bar(GameState *g);
void menu_equip_draw_list(GameState *g);
uint16_t menu_equip_list_end(void);
void menu_equip_draw_list_row(GameState *g, uint16_t at);
void menu_equip_open_the_list(GameState *g);
void menu_equip_settle_the_list(GameState *g);
void menu_equip_start_list_closing(GameState *g, Platform *pf, MenuListSlide *w);
bool menu_equip_slide_list_out_a_frame(GameState *g, MenuListSlide *w);
void menu_equip_rest_the_list_bands(GameState *g);

/* Back on the slots from the list: the member's row and the slots' marks
 * drawn again (the card before them still the original's); then, once the
 * list window has closed and the list is drawn again, the slots at rest. */
void menu_equip_redraw_slots(GameState *g, Platform *pf);
void menu_equip_slots_at_rest(GameState *g);

/* The screen set up as it opens ($C2:A11A): the party's items set aside,
 * the windows and the member list, its scroll bar, and the screen shown. */
void menu_equip_set_up(GameState *g, Platform *pf);

/* ---- The screen's steps, one sequence each for the machine and for C ---------------------------- */

/* Each of the screen's steps -- the opening ($C2:9C31), choosing a member
 * ($C2:9C39), a slot ($C2:9C70) or an item ($C2:9CA5) -- and the routines
 * they go on to is one sequence, which the machine's entries
 * (equipment_bridge.c) and the program as C both run, asking the menus for
 * what differs through EquipmentSteps.
 *
 * The places the sequences tick, wait or set the machine's registers from. */
typedef enum {
    EQUIPMENT_AT_THE_OPENING,       /* the screen opening */
    EQUIPMENT_AT_THE_MEMBER_CHOSEN, /* a member chosen */
    EQUIPMENT_AT_THE_SLOTS_DRAWN,   /* ... the slots drawn, a frame on */
    EQUIPMENT_AT_THE_MEMBER_POINTED, /* another member pointed at */
    EQUIPMENT_AT_THE_SLOTS_SLIDING, /* the slots' window sliding in, a frame at a time */
    EQUIPMENT_AT_THE_SLOTS_LEFT,    /* back from the slots to the members */
    EQUIPMENT_AT_THE_SLOT_CHOSEN,   /* a slot chosen */
    EQUIPMENT_AT_THE_SLOT_MOVED,    /* the cursor on another slot */
    EQUIPMENT_AT_THE_SLOT_POINTED,  /* ... the list of what fits it drawn a frame on */
    EQUIPMENT_AT_THE_MEMBER_SWITCHED, /* the shoulder assignments' next member */
    EQUIPMENT_AT_THE_SLOTS_GOING,   /* the slots' window going: a frame, */
    EQUIPMENT_AT_THE_SLOTS_GONE,    /* ... and another, */
    EQUIPMENT_AT_THE_MEMBERS_SLIDING_BACK, /* ... then the member rows back down */
    EQUIPMENT_AT_THE_LIST_SLIDING_IN, /* the list's window sliding in */
    EQUIPMENT_AT_THE_LIST_SLIDING_OUT, /* ... and out */
    EQUIPMENT_AT_THE_LIST_BEING_MADE, /* the list of what fits made */
    EQUIPMENT_AT_THE_LIST_FRAMED,   /* ... and its frame laid out */
    EQUIPMENT_AT_THE_SLOTS_REDRAWN, /* back on the slots, a frame on */
    EQUIPMENT_AT_THE_ITEM_LEFT,     /* back from the list to the slots */
    EQUIPMENT_AT_THE_ITEM_POINTED,  /* the cursor along the list */
    EQUIPMENT_AT_THE_PAGE_TURNED,   /* the list paged */
    EQUIPMENT_AT_THE_ROW_BROUGHT_IN, /* a row brought in past the list's end */
    EQUIPMENT_AT_THE_LIST_ROWS_SLIDING, /* ... and the rows sliding a frame at a time */
    EQUIPMENT_AT_THE_MEMBER_SLIDE_STARTING, /* the member list's slide about to run */
    EQUIPMENT_AT_THE_MEMBER_SLIDE_ENDED, /* ... and run */
    EQUIPMENT_AT_THE_MEMBERS_SLIDING, /* the member rows sliding, a frame at a time */
    EQUIPMENT_AT_THE_MEMBERS_SLID,  /* ... and at rest */
} EquipmentPlace;

/* The routines the steps go on to and come back from (each a registered
 * entry, run on the machine where the original calls it). */
typedef enum {
    EQUIPMENT_SLIDES_MEMBERS,              /* the member list slid by a member ($C2:9D5B) */
    EQUIPMENT_SLIDES_THE_MEMBER_ROWS_UP,   /* its rows' slide ($C2:9DAF) */
    EQUIPMENT_SLIDES_THE_MEMBER_ROWS_DOWN,
    EQUIPMENT_OPENS_THE_SLOTS_WINDOW,      /* $C2:A71E */
    EQUIPMENT_CLOSES_THE_SLOTS_WINDOW,     /* $C2:A85C */
    EQUIPMENT_SWITCHES_MEMBERS,            /* $C2:A00B */
    EQUIPMENT_REDRAWS_THE_CARD,            /* the card and the member's row ($C2:A21F) */
    EQUIPMENT_DRAWS_THE_CARD,              /* $C2:A22F */
    EQUIPMENT_REDRAWS_THE_LIST_FOR_THE_SLOT, /* $C2:A45D, from the slots */
    EQUIPMENT_REDRAWS_THE_LIST_ON_THE_SLOTS, /* $C2:A45D, back on them */
    EQUIPMENT_MAKES_THE_LIST,              /* $C2:A463 */
    EQUIPMENT_SLIDES_THE_LIST_IN,          /* $C2:A4AF */
    EQUIPMENT_SLIDES_THE_LIST_OUT,         /* $C2:A697 */
    EQUIPMENT_GOES_BACK_ON_THE_SLOTS,      /* $C2:A64D */
    EQUIPMENT_POINTS_ALONG_THE_LIST,       /* $C2:9F05 */
    EQUIPMENT_WORKS_OUT_LOW_HP_MARKS,      /* $C2:813C */
    EQUIPMENT_PLAYS_THE_PUT_ON_SOUND,      /* $C2:EABA */
    EQUIPMENT_FORGETS_THE_WEAPON,          /* $C2:9CFC */
    EQUIPMENT_TRIES_IT_ON,                 /* the item on the cursor tried on */
} EquipmentRoutine;

/* How a step ends: on to the screen's set-up, to pointing at a member, to
 * the member's card, back to the main menu, to pointing at a slot, on to the
 * list of what fits, the refusal's buzz, back to the members, or the list's
 * tick as it opens. */
typedef enum {
    EQUIPMENT_SETS_UP_THE_SCREEN, EQUIPMENT_POINTS_AT_A_MEMBER, EQUIPMENT_SHOWS_THE_CARD, EQUIPMENT_GOES_BACK_TO_THE_MAIN_MENU,
    EQUIPMENT_POINTS_AT_A_SLOT, EQUIPMENT_GOES_ON_TO_THE_LIST, EQUIPMENT_REFUSES, EQUIPMENT_GOES_BACK_TO_THE_MEMBERS,
    EQUIPMENT_TICKS_ON_THE_LIST,
} EquipmentEnd;

/* Where the machine leaves what the original's tests and loads left
 * (scaffolding). */
typedef enum {
    EQUIPMENT_NOTE_MEMBER_STEP_TESTED, EQUIPMENT_NOTE_MEMBER_POINTED, EQUIPMENT_NOTE_SLOT_STEP_TESTED,
    EQUIPMENT_NOTE_LABEL_COPIED, EQUIPMENT_NOTE_SLOT_KEPT, EQUIPMENT_NOTE_LIST_SUM, EQUIPMENT_NOTE_MEMBER_SWITCHED,
    EQUIPMENT_NOTE_BUTTONS_TESTED, EQUIPMENT_NOTE_INVENTORY_CHANGED, EQUIPMENT_NOTE_CURSOR_LOADED,
    EQUIPMENT_NOTE_CURSOR_COMPARED, EQUIPMENT_NOTE_CURSOR_KEPT, EQUIPMENT_NOTE_PAGE_TURNED, EQUIPMENT_NOTE_LIST_DRAWN,
    EQUIPMENT_NOTE_ROW_NOTED, EQUIPMENT_NOTE_ROW_FOUND, EQUIPMENT_NOTE_LIST_ROWS_QUEUED, EQUIPMENT_NOTE_LIST_FRAMED,
    EQUIPMENT_NOTE_LIST_OPENED, EQUIPMENT_NOTE_LIST_WINDOW_SHOWN, EQUIPMENT_NOTE_LIST_BAR_PLACED,
    EQUIPMENT_NOTE_SLOTS_REDRAWN, EQUIPMENT_NOTE_MEMBER_SLIDE_STARTED,
} EquipmentNote;

/* What the sequences ask: the cursor read, a tick, a frame's wait, the
 * machine's registers as the original's code sets them, a routine run, a
 * note, the end. Each false where it went no further. */
typedef struct {
    void (*read_cursor)(void *ctx);
    bool (*tick)(void *ctx, EquipmentPlace at);
    bool (*wait)(void *ctx, EquipmentPlace at);
    void (*registers_at)(void *ctx, EquipmentPlace at);
    bool (*run)(void *ctx, EquipmentRoutine routine);
    void (*note)(void *ctx, EquipmentNote note, uint8_t value);
    bool (*ends)(void *ctx, EquipmentEnd end);
} EquipmentSteps;

typedef bool EquipmentSequence(GameState *g, Platform *pf, const EquipmentSteps *n, void *ctx);
EquipmentSequence menu_open_equipment_with, menu_choose_equipment_member_with, menu_point_at_equipment_member_with,
    menu_open_the_slots_window_with, menu_close_the_slots_window_with, menu_choose_equipment_slot_with,
    menu_point_at_equipment_slot_with, menu_switch_equipment_member_with, menu_choose_equipment_item_with,
    menu_point_along_equipment_list_with, menu_open_the_equipment_list_with, menu_slide_the_equipment_list_in_with,
    menu_slide_the_equipment_list_out_with, menu_make_the_equipment_list_with, menu_redraw_the_equipment_list_with,
    menu_back_on_the_equipment_slots_with, menu_start_sliding_equipment_members_with,
    menu_slide_equipment_members_with;

/* Back on the members from the slots ($C2:A2CD): the member list again, its
 * scroll bar, the rows at rest. */
void menu_equip_back_to_the_members(GameState *g, Platform *pf);

/* The sound of an item put on. */
enum { MENU_EQUIP_PUT_ON_SOUND = 0xB5 };

/* What they take of the state, as their entries' guards do: the set-up,
 * choosing a member, pointing at one. */
bool menu_equip_can_set_up(const GameState *g);
bool menu_equip_can_choose_a_member(const GameState *g);
bool menu_equip_can_point_at_a_member(const GameState *g);
/* And the guards' state halves of the rest (equipment_bridge.c): the slots'
 * window sliding in and out, pointing at a slot or switching members,
 * back on the members, the list's opening, drawing, sliding and pointing,
 * back on the slots, choosing an item, the weapon's byte forgotten. */
bool menu_equip_slots_window_can_slide(const GameState *g);
bool menu_equip_members_can_slide(const GameState *g);
bool menu_equip_can_point_at_a_slot(const GameState *g);
bool menu_equip_members_can_come_back(const GameState *g);
bool menu_equip_list_can_open(const GameState *g);
bool menu_equip_list_can_be_drawn(const GameState *g);
bool menu_equip_list_can_slide(const GameState *g);
bool menu_equip_list_can_be_pointed_along(const GameState *g);
bool menu_equip_slots_can_come_back(const GameState *g);
bool menu_equip_can_choose_an_item(const GameState *g);
bool menu_equip_weapon_can_be_forgotten(const GameState *g);

/* As C, from the menus' main task; false where the program went no
 * further. */
bool menu_open_equipment(GameState *g, Platform *pf);
bool menu_choose_equipment_member(GameState *g, Platform *pf);
bool menu_choose_equipment_slot(GameState *g, Platform *pf);
bool menu_choose_equipment_item(GameState *g, Platform *pf);

/* ---- The entries (game/menu/equipment_bridge.c) ----------------------------------------------- */

void menu_open_equipment_entry(Snes *s);
void menu_set_up_equipment(Snes *s);
void menu_choose_equipment_member_entry(Snes *s);
void menu_point_at_equipment_member(Snes *s);
void menu_start_sliding_equipment_members(Snes *s);
void menu_slide_equipment_members(Snes *s);
void menu_choose_equipment_slot_entry(Snes *s);
void menu_point_at_equipment_slot(Snes *s);
void menu_switch_equipment_member_entry(Snes *s);
void menu_choose_equipment_item_entry(Snes *s);
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
bool menu_equipment_members_slide_starts(Snes *s);
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

/* For the entries' leaves (scaffolding): whether the list's opening starts
 * typing a description, the item it describes being another than the one
 * shown, which decides what the original's description leaves in A. */
bool menu_equip_opening_starts_typing(const GameState *g);

#endif
