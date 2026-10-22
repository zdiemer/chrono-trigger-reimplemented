/* The party's members as the menus' screens list them (game/menu/members.c):
 * a member's record loaded where the status windows draw it from, the member
 * list down a screen's left side, and the chosen member's card beside it. */
#ifndef CT_GAME_MENU_MEMBERS_H
#define CT_GAME_MENU_MEMBERS_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/video.h"
#include "snes/system.h"
#include "state.h"

/* The list shows three members at a time. */
enum { MENU_MEMBER_ROWS = 3 };

/* Which way a push slides a member list that shows fewer members than the
 * party has (the techs screen's, the equipment screen's): up a member, down a
 * member, or not at all, where it has slid as far as it goes that way. */
typedef enum { NO_MEMBER_SLIDE, MEMBERS_SLIDE_UP, MEMBERS_SLIDE_DOWN } MemberListSlide;

/* Which way a push past the rows slides such a list, scrolled `scroll`
 * members down: a push down, down a member where the party has one past the
 * rows (`one_past_the_rows`); a push up, up a member where the list has slid.
 * The screen moves its scroll and sets the slide up. */
MemberListSlide menu_member_slide_for_push(const GameState *g, uint8_t scroll, bool one_past_the_rows);

/* Load the member at `member` in the menus' list of the party into the status
 * record the windows draw from (menu_status_record): false, with nothing
 * loaded, for a place past the last member. */
bool menu_load_member(GameState *g, uint8_t member);

/* The same for a character, whether or not it is a member: false, with
 * nothing loaded, for a byte with its top bit set (no character). */
bool menu_load_character(GameState *g, uint8_t character);

/* The member the list's cursor is on: its row plus how far the list has
 * scrolled. */
uint8_t menu_member_on_the_cursor(const GameState *g);

/* The member the menus work on (and load), as their screens keep it in the
 * screen's argument's low byte. */
uint8_t menu_member_worked_on(const GameState *g);
void menu_work_on_member(GameState *g, uint8_t member);
/* ... or as the whole argument, its high byte cleared, where the original
 * stores the member as a word. */
void menu_work_on_member_only(GameState *g, uint8_t member);

/* The character whose record the menus loaded last (menu_member_record_at
 * says where it is), and whether that is one of the characters' records, for
 * a screen that writes a change back to it. */
uint8_t menu_member_character(const GameState *g);
bool menu_member_record_is_a_characters(const GameState *g);

/* Each character's low-HP mark, a tenth of the HP its restoring stops at,
 * worked out as the menus open and after the equipment screen changes what a
 * member wears: a member row draws the member's HP marked once it is down to
 * its mark. */
void menu_work_out_low_hp_marks(GameState *g);

/* What a member row's window list reads besides the record, noted for the
 * record loaded: the character's face and name, and whether its HP is low. */
void menu_note_member_face_and_hp(GameState *g);

/* The loaded member's row of the list, drawn from the drawing origin; and
 * every row the list shows, from its first member. */
void menu_draw_member_row(GameState *g, Platform *pf);
void menu_draw_member_rows(GameState *g, Platform *pf);

/* The list's rows coloured: the cursor's row keeps its colours and the
 * others are dimmed. A row coloured on its own, at `at` (its first tile on
 * the list's front layer), in the colour of the cursor's row or dimmed. */
void menu_colour_member_rows(GameState *g);
void menu_colour_member_row(GameState *g, uint16_t at, bool dimmed);
/* What the original's colouring of a row on its own leaves in the menus'
 * cells (scaffolding: ORIGINAL_EFFECT). */
void menu_leave_member_row_coloured(GameState *g, uint16_t at, bool dimmed);

/* The card's parts on either side of the original's figures and stats,
 * which the card's bridge calls out to between them: the time played and the
 * member on the cursor loaded; then the card's window, and after the stats
 * the large portrait and the member's element. */
void menu_start_member_card(GameState *g);
void menu_draw_member_card_window(GameState *g, Platform *pf);
void menu_finish_member_card(GameState *g);

/* Whether a place of the list loads a character or is past the last member;
 * whether the card can be drawn for the member on the cursor (or another),
 * and the list's rows from its first (or another): every member they load is
 * a character or the end of the list. For the guards of the screens that
 * draw them. */
bool menu_member_can_be_loaded(const GameState *g, uint8_t member);
bool menu_member_card_can_be_drawn(const GameState *g);
bool menu_member_card_can_be_drawn_for(const GameState *g, uint8_t member);
bool menu_member_rows_can_be_drawn(const GameState *g);
bool menu_member_rows_can_be_drawn_from(const GameState *g, uint8_t first);

/* What the rows' colouring leaves in the registers, its upload queued last,
 * for a bridge that coloured them as C and goes on to a call that sees them
 * (scaffolding: ORIGINAL_EFFECT). */
void menu_leave_member_rows_coloured(Snes *s);

/* The entries (game/registry.c). */
void menu_work_out_low_hp_marks_entry(Snes *s);
bool menu_low_hp_marks_are_workable(Snes *s);
void menu_load_member_entry(Snes *s);
void menu_note_member_face(Snes *s);
void menu_draw_member_row_entry(Snes *s);
void menu_draw_empty_member_row(Snes *s);
void menu_draw_member_rows_entry(Snes *s);
void menu_colour_member_rows_entry(Snes *s);
void menu_colour_member_row_entry(Snes *s);
void menu_show_member_element(Snes *s);
void menu_draw_member_card(Snes *s);
void menu_redraw_member_card(Snes *s);

bool menu_member_is_loadable(Snes *s);
bool menu_member_face_is_noteable(Snes *s);
bool menu_member_row_is_drawable(Snes *s);
bool menu_empty_member_row_is_drawable(Snes *s);
bool menu_member_rows_are_drawable(Snes *s);
bool menu_member_rows_are_colourable(Snes *s);
bool menu_member_row_is_colourable(Snes *s);
bool menu_member_element_is_showable(Snes *s);
bool menu_member_card_is_drawable(Snes *s);

#endif
