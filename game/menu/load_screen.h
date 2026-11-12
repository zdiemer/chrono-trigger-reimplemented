#ifndef CT_GAME_MENU_LOAD_SCREEN_H
#define CT_GAME_MENU_LOAD_SCREEN_H

#include <stdbool.h>
#include <stdint.h>

#include "menu/vram.h"
#include "platform/video.h"
#include "state.h"

/* The slots' windows and rows as the load screen and the save screen draw
 * them (the windows' list, then the three slots' rows). */
void menu_draw_the_slots(GameState *g, Platform *pf);
void menu_draw_slot_rows(GameState *g);
/* A slot's window drawn afresh in the design its game was saved with
 * ($C2:D32C), as the save screen does once it has saved in the slot. */
void menu_redraw_slot_window(GameState *g, uint8_t slot);

/* ---- The steps ------------------------------------------------------------------------------------ */

/* The screen's steps, its drawing and its showing of the slot under the
 * cursor ($C2:E743, save_screen.c) are each one sequence, false where it
 * went no further. */

/* How a step ends: on to choosing as the screen fades in, or out of the
 * menus to the game loaded or begun. */
typedef enum { LOAD_FADES_IN, LOAD_LEAVES_FOR_THE_GAME } LoadScreenEnd;

/* The steps ($C2:E613 the opening, $C2:E61B choosing, $C2:E6AE which game a
 * New Game+ carries on from, $C2:E705 the erase question's answer), the
 * screen's drawing ($C2:E7C3) and the slot under the cursor shown
 * ($C2:E743, menu/save_screen.c). */
bool menu_open_load_screen_with(GameState *g, Platform *pf);
bool menu_choose_on_load_screen_with(GameState *g, Platform *pf);
bool menu_choose_game_to_carry_on_from_with(GameState *g, Platform *pf);
bool menu_answer_erase_with(GameState *g, Platform *pf);
bool menu_draw_load_screen_with(GameState *g, Platform *pf);
bool menu_show_save_slot_with(GameState *g, Platform *pf);
/* A slot's erasing ($C2:E8B7): its game forgotten, its row drawn empty and,
 * a frame on, its label queued and its window drawn again. */
bool menu_erase_slot_with(GameState *g, Platform *pf);

/* A New Game+'s question: which game it carries on from, the slot's header
 * given way to its label ($C2:E841). */
void menu_ask_which_game_to_carry_on_from(GameState *g);

/* The sound driver told whether to play in stereo, as the screen leaves for
 * the game. */
void menu_send_the_sound_output(GameState *g, Platform *pf);

/* The state each step needs before it runs: the drawing (it reads which slot was saved last from save memory), choosing
 * and the carrying on, the slot under the cursor shown, the erase question's
 * answer. */
bool menu_load_screen_can_draw(const GameState *g, Platform *pf);
bool menu_load_screen_can_choose(const GameState *g);
bool menu_load_screen_slot_can_show(const GameState *g);
bool menu_erase_can_be_answered(const GameState *g);
bool menu_slot_can_be_erased(const GameState *g);

/* From the menus' main task: the four steps; false where the program
 * went no further. */
bool menu_open_load_screen(GameState *g, Platform *pf);
bool menu_choose_on_load_screen(GameState *g, Platform *pf);
bool menu_choose_game_to_carry_on_from(GameState *g, Platform *pf);
bool menu_answer_erase(GameState *g, Platform *pf);
/* And the slot's erasing the answer's yes runs. */
bool menu_erase_slot(GameState *g, Platform *pf);

void menu_forget_slot(GameState *g, Platform *pf, uint8_t slot);
MenuUpload menu_slot_label_upload(uint8_t slot);

#endif
