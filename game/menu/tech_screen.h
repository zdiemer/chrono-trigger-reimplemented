/* The techs screen and the main menu's panel for it: what the game does
 * (game/menu/tech_screen.c) and the entries that run it on the original's
 * machine (game/menu/tech_screen_bridge.c). */
#ifndef CT_GAME_MENU_TECH_SCREEN_H
#define CT_GAME_MENU_TECH_SCREEN_H

#include <stdbool.h>
#include <stdint.h>

#include "menu/members.h"
#include "menu/techs.h"
#include "platform/video.h"
#include "snes/system.h"
#include "state.h"

/* The screen's steps, as its main task runs them (menu_screen_step). */
typedef enum {
    TECHS_OPENING = 0,
    TECHS_LAYING_OUT = 1,
    TECHS_ON_THE_MEMBERS = 2,
    TECHS_ON_THE_LIST = 3,
    TECHS_ON_THE_TARGETS = 4,
} TechsStep;

/* ---- Opening it ------------------------------------------------------------------------------- */

/* The screen's windows, the cursor on the member it was left on, the list
 * and the description to be made afresh; then (once the original has checked
 * which triple techs the party's rocks give) those noted for the list, and
 * the member list at rest; then the sprites cleared but the cursor. A frame
 * later, the member list's own windows. */
void menu_techs_open(GameState *g, Platform *pf);
void menu_techs_note_rock_techs(GameState *g);
void menu_techs_show(GameState *g);
void menu_techs_lay_out_members(GameState *g, Platform *pf);

/* ---- The members ------------------------------------------------------------------------------ */

typedef enum { TECHS_MEMBER_POINTED_AT, TECHS_MEMBER_CHOSEN, TECHS_MEMBERS_LEFT } TechsMemberChoice;

/* What the frame's input does among the members: choosing goes into the
 * member's list, going back leaves for the main menu. */
TechsMemberChoice menu_techs_choose_member(const GameState *g);

/* Which list the screen shows, noted before the input moves anything; and
 * whether it is another now, so the cursor ticks. */
uint8_t menu_techs_note_list_shown(GameState *g);
bool menu_techs_list_changed(GameState *g, uint8_t before);

/* Left and right switch the kind of tech the list shows (round from the
 * last to the first); then, once the cursor has moved within the members,
 * whether the push slides the member list a member along. */
void menu_techs_switch_kind(GameState *g);
bool menu_techs_push_slides_members(GameState *g);

/* The member the cursor is on noted, the member's list of the kind made and
 * drawn afresh where it is another, and the kind's tab marked. */
void menu_techs_point_at_member(GameState *g);
void menu_techs_show_member_list(GameState *g);
void menu_techs_mark_kind(GameState *g);

/* The member list slides where there is a member that way: it moves on (or
 * back) a member and the slide is set up. */
MemberListSlide menu_techs_slide_members(GameState *g);

/* The slide: the member coming in drawn above or below the rows and the
 * rows copied out of view beside it; twelve frames of the view moving four
 * lines a frame (whether frames are left after this one); then the rows
 * copied back, the view at rest, and the three members' sprites placed. */
void menu_techs_start_member_slide(GameState *g, Platform *pf);
bool menu_techs_slide_members_a_frame(GameState *g);
void menu_techs_finish_member_slide(GameState *g);

/* Choosing a member: whether its list has a tech; then into the list, the
 * cursor on its row. */
bool menu_techs_member_has_techs(const GameState *g);
void menu_techs_go_to_the_list(GameState *g);

/* The member whose list it is stands to the right of the list (the last
 * record), its sprite loaded where it isn't already that character's. */
void menu_techs_show_the_lister(GameState *g);
bool menu_techs_put_the_lister_on_its_record(GameState *g);

/* ---- The member rows -------------------------------------------------------------------------- */

/* The rows of the members the list shows, each with its sprite (and empty
 * rows as the original draws them past the last); then the list's frame and
 * the scroll bar beside them. */
void menu_techs_draw_member_rows(GameState *g, Platform *pf);

/* A member row for the member loaded last, from the drawing origin: its
 * face, name, HP and MP, the MP in the short colour where it has less than
 * the tech on the cursor needs. */
void menu_techs_draw_member_row(GameState *g, Platform *pf);

/* ---- The list --------------------------------------------------------------------------------- */

/* The list's frame, then the list's members' rows: the member's and the
 * tech's other members', each with what it costs them. */
void menu_techs_show_the_list(GameState *g, Platform *pf);

/* On the list the screen's line effects are those at rest (a slide changes
 * them); then what the frame's input does: choosing uses the tech, going back
 * returns to the members. */
void menu_techs_hold_line_effects(GameState *g);
typedef enum { TECH_POINTED_AT, TECH_CHOSEN, TECHS_LEFT } TechsListChoice;
TechsListChoice menu_techs_choose_on_the_list(const GameState *g);

/* Going back to the members: their rows drawn again from their records. */
void menu_techs_back_to_the_members(GameState *g);

/* Choosing a tech uses it from the menu, or is refused: the member waits
 * (only the active members use techs here), the tech doesn't heal, or the
 * member's MP falls short of its cost. */
typedef enum { TECH_USED, TECH_REFUSED_WAITING, TECH_REFUSED_NOT_HEALING, TECH_REFUSED_SHORT_OF_MP } TechsUse;
TechsUse menu_techs_use(GameState *g);
/* What it restores, and the cursor onto the window of members it is used on;
 * whether it is used on every member at once (each gets a cursor). */
void menu_techs_choose_targets(GameState *g);
bool menu_techs_heal_every_member(const GameState *g);
void menu_techs_hold_the_description(GameState *g);

/* Pointing along the list: a page turned with the shoulder buttons, drawn
 * again; or a push past its rows slides it a row where there is one. */
uint8_t menu_techs_list_top_for_paging(GameState *g);
void menu_techs_turn_page(GameState *g, uint8_t top);
bool menu_techs_list_slide_brings_a_tech(const GameState *g);
void menu_techs_set_up_list_slide(GameState *g);
void menu_techs_bring_in_a_tech(GameState *g);
bool menu_techs_list_slide_takes_frames(GameState *g);
bool menu_techs_slide_list_a_frame(GameState *g);
void menu_techs_finish_list_slide(GameState *g);

/* The list's eight rows drawn from its place `from`, each a tech's name or
 * blanks past the last. */
void menu_techs_draw_list_rows(GameState *g, uint8_t from);

/* The cursor's row kept (whether it moved); the tech on it shown: its
 * members' rows, its cost; a window for a tech still being learned. */
bool menu_techs_keep_the_cursors_row(GameState *g);
void menu_techs_show_the_pointed_tech(GameState *g, Platform *pf);
void menu_techs_show_learning(GameState *g, Platform *pf);

/* ---- The description -------------------------------------------------------------------------- */

/* The description window (menu/description.h) shows the pointed tech's, or
 * (on the members) the kind's caption, typed where it shows another. */
void menu_techs_describe_tech(GameState *g);
void menu_techs_describe_kind(GameState *g);

/* ---- Using a tech ----------------------------------------------------------------------------- */

/* The window of members it is used on: the user's row atop the list, then
 * (a frame later) the active members' rows and sprites. */
void menu_techs_open_targets(GameState *g, Platform *pf);
void menu_techs_show_targets(GameState *g, Platform *pf);

typedef enum { TARGET_POINTED_AT, TARGET_CHOSEN, TARGETS_LEFT } TechsTargetChoice;
TechsTargetChoice menu_techs_choose_target(const GameState *g);

/* Whether the tech's user can still pay for it, which stops it helping. */
void menu_techs_check_the_users_mp(GameState *g);
bool menu_techs_use_helps(const GameState *g);

/* Used: the user's sprite strikes its casting pose for the restoring (the
 * original's, which waits it out); after, the pose dropped and the cost
 * taken from the user's MP. */
void menu_techs_cast(GameState *g);
uint8_t menu_techs_after_casting(GameState *g);

/* Back from the window to the list: its cursors stopped (the original's),
 * then the window's lines cleared. */
void menu_techs_back_from_the_targets(GameState *g);
void menu_techs_close_targets(GameState *g);

/* The members a healing is used on, shared with the items screen's tonics:
 * the cursor taken round the rows where it is on one member; the member it
 * is on noted (whether another than before); then whether the restoring helps
 * any member it reaches (HP or MP short of the most). */
void menu_restoring_take_the_cursor_round(GameState *g);
bool menu_restoring_point_at_member(GameState *g);
void menu_restoring_look_for_help(GameState *g);
bool menu_restoring_every_member(const GameState *g);

/* ---- The main menu's panel -------------------------------------------------------------------- */

/* With the main menu's cursor on the techs, the member rows are drawn
 * beside it (the original loads their colours and starts their sprites
 * between), the kinds' tabs, the colours, and the colour math. The main menu
 * leaving the techs takes its own windows back. */
void menu_techs_panel_lay_out(GameState *g, Platform *pf);
void menu_techs_panel_show(GameState *g, Platform *pf);
void menu_techs_panel_put_away(GameState *g, Platform *pf);

/* ---- What the steps can run on (the guards') -------------------------------------------------- */

bool menu_techs_members_are_drawable(const GameState *g);
bool menu_techs_list_is_pointable(const GameState *g);
bool menu_techs_member_is_listable(const GameState *g);

/* ---- The entries (game/menu/tech_screen_bridge.c) --------------------------------------------- */

void menu_open_techs(Snes *s);
void menu_lay_out_tech_members(Snes *s);
void menu_choose_tech_member(Snes *s);
void menu_choose_tech(Snes *s);
void menu_choose_tech_target(Snes *s);
void menu_move_among_tech_members(Snes *s);
void menu_slide_tech_members(Snes *s);
void menu_slide_tech_member_rows(Snes *s);
void menu_slide_tech_list(Snes *s);
void menu_point_at_tech_target(Snes *s);
void menu_point_at_member_to_restore(Snes *s);
void menu_note_member_to_restore(Snes *s);
void menu_show_techs_panel(Snes *s);
void menu_put_techs_panel_away(Snes *s);
void menu_show_tech_lister(Snes *s);
void menu_load_tech_lister_sprite(Snes *s);
void menu_draw_tech_member_rows(Snes *s);
void menu_show_tech_list(Snes *s);
void menu_describe_pointed_tech(Snes *s);
void menu_describe_tech_kind(Snes *s);
void menu_describe_tech(Snes *s);
void menu_open_tech_targets(Snes *s);
void menu_show_member_tech_list(Snes *s);
void menu_draw_tech_rows(Snes *s);
void menu_show_pointed_tech(Snes *s);
void menu_draw_tech_member_row(Snes *s);

bool menu_techs_screen_opens(Snes *s);
bool menu_tech_members_step_runs(Snes *s);
bool menu_tech_list_step_runs(Snes *s);
bool menu_tech_targets_step_runs(Snes *s);
bool menu_tech_members_move(Snes *s);
bool menu_tech_members_slide(Snes *s);
bool menu_tech_list_slides(Snes *s);
bool menu_tech_target_is_pointable(Snes *s);
bool menu_member_to_restore_is_pointable(Snes *s);
bool menu_techs_panel_shows(Snes *s);
bool menu_techs_panel_goes(Snes *s);
bool menu_tech_lister_is_showable(Snes *s);
bool menu_tech_member_rows_are_drawable(Snes *s);
bool menu_tech_list_is_showable(Snes *s);
bool menu_tech_is_describable(Snes *s);
bool menu_tech_targets_open(Snes *s);
bool menu_tech_rows_are_drawable(Snes *s);
bool menu_member_tech_list_is_showable(Snes *s);
bool menu_tech_member_row_is_drawable(Snes *s);

#endif
