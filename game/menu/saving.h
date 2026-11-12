#ifndef CT_GAME_MENU_SAVING_H
#define CT_GAME_MENU_SAVING_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/video.h"
#include "state.h"

/* The panel put away, and a save point's save screen laid out over it. */
void menu_put_save_panel_away(GameState *g);
void menu_lay_out_save_screen(GameState *g, Platform *pf);

/* ---- The steps --------------------------------------------------------------------------------- */

/* The panel ($C2:D065), the save screens' steps, saving in a slot and the
 * slots' frame as the cursor moves among them ($C2:CFA2, save_screen.c) are
 * each one sequence, false where it went no further. */

/* How a step ends: on to choosing as the screen fades in, the menus left,
 * back to the main menu, on into saving in the slot chosen (as a jump or as
 * a call), on into the slots' frame, or a buzz where the game can't be saved
 * here. */
typedef enum {
    SAVING_FADES_IN, SAVING_LEAVES_THE_MENUS, SAVING_BACK_TO_THE_MAIN_MENU, SAVING_SAVES_IN_THE_SLOT_JUMPED,
    SAVING_SAVES_IN_THE_SLOT, SAVING_MOVES_AMONG_THE_SLOTS, SAVING_BUZZES,
} SavingEnd;

/* The panel ($C2:D065); a save point's save screen's steps ($C2:D4F8,
 * $C2:D506); the main menu's ($C2:CEDC, $C2:CEFD); the question's
 * ($C2:CF61, and $C2:CF92, which no code moves to); saving in the slot
 * ($C2:CF0A); the save up to its last wait ($C2:CF37); and the slots' frame
 * ($C2:CFA2, save_screen.c). */
bool menu_show_save_panel_with(GameState *g, Platform *pf);
bool menu_open_save_screen_with(GameState *g, Platform *pf);
bool menu_choose_on_save_screen_with(GameState *g, Platform *pf);
bool menu_open_saving_with(GameState *g, Platform *pf);
bool menu_choose_slot_to_save_in_with(GameState *g, Platform *pf);
bool menu_answer_whether_to_save_over_with(GameState *g, Platform *pf);
bool menu_close_the_question_on_a_button_with(GameState *g, Platform *pf);
bool menu_save_in_the_slot_with(GameState *g, Platform *pf);
bool menu_save_the_game_with(GameState *g, Platform *pf);
bool menu_move_among_the_slots_with(GameState *g, Platform *pf);

/* The slot saved in shown: its window drawn afresh, and its tilemap and the
 * slots' chapters sent ($C2:CF21). */
void menu_show_the_slot_saved(GameState *g);

/* The saved sound sent to the driver. */
void menu_send_the_saved_sound(Platform *pf);

/* What the steps need of the state: the panel
 * and a save point's screen, the main menu's screen's opening, the slot shown
 * saved in, the slot saved shown. */
bool menu_save_panel_can_show(const GameState *g);
bool menu_saving_can_open(const GameState *g);
bool menu_save_screen_can_lay_out(const GameState *g);
bool menu_slot_can_be_saved(const GameState *g);
bool menu_slot_saved_can_show(const GameState *g);

/* The panel, from the main menu's (false where the program went no
 * further), and the save screens' steps, from the menus' main task. */
bool menu_show_save_panel(GameState *g, Platform *pf);
bool menu_open_save_screen(GameState *g, Platform *pf);
bool menu_choose_on_save_screen(GameState *g, Platform *pf);
bool menu_open_saving(GameState *g, Platform *pf);
bool menu_choose_slot_to_save_in(GameState *g, Platform *pf);
bool menu_answer_whether_to_save_over(GameState *g, Platform *pf);
bool menu_close_the_question_on_a_button(GameState *g, Platform *pf);

#endif
