/* Saving the game from the menus: the main menu's panel for it and the
 * save screens (game/menu/saving.c), and their steps on the original's
 * machine (game/menu/saving_bridge.c). */
#ifndef CT_GAME_MENU_SAVING_H
#define CT_GAME_MENU_SAVING_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/video.h"
#include "snes/system.h"
#include "state.h"

/* The panel put away, and a save point's save screen laid out over it. */
void menu_put_save_panel_away_now(GameState *g);
void menu_lay_out_save_screen(GameState *g, Platform *pf);

/* ---- The steps, one sequence for the machine and for C ---------------------------------------- */

/* The panel ($C2:D065), the save screens' steps, saving in a slot and the
 * slots' frame as the cursor moves among them ($C2:CFA2, save_screen.c) are
 * each one sequence, which the machine's entries (saving_bridge.c) and the
 * program as C both run; what differs is what they ask of the menus, through
 * SavingSteps.
 *
 * The places the sequences tick, wait or save from (the machine's table
 * gives the original's call at each). */
typedef enum {
    SAVING_AT_THE_SLOTS_DRAWN,        /* the panel: a frame after the slots */
    SAVING_AT_THE_SUMMARY_DRAWN,      /* ... and after the summary */
    SAVING_AT_THE_OPENING_TICK,       /* the main menu's save screen opened */
    SAVING_AT_THE_QUESTION_TICK,      /* a slot with a game chosen: the question */
    SAVING_AT_THE_ANSWER_MOVE_TICK,   /* the question's cursor moved */
    SAVING_AT_THE_NO_TICK,            /* "no", or going back */
    SAVING_AT_THE_SLOT_MOVE_TICK,     /* the slots' cursor moved to another slot */
    SAVING_AT_THE_SAVE_IN_AN_EMPTY_SLOT, /* saved at once */
    SAVING_AT_THE_SAVE_OVER,          /* saved over, the question answered "yes" */
    SAVING_AT_THE_SAVE_WRITTEN,       /* the save's wait, a frame after the game is written */
    SAVING_PLACES
} SavingPlace;

/* Where the machine leaves what the original's own tests left (scaffolding),
 * with the value the sequence had then. */
typedef enum {
    SAVING_NOTE_NOT_REFUSED,      /* the main menu's save screen may save here */
    SAVING_NOTE_LAID_OUT,         /* ... and is laid out, a step on */
    SAVING_NOTE_SLOT_HOLDS,       /* the slot chosen tested: whether it holds a game */
    SAVING_NOTE_QUESTION_ASKED,   /* the question put up */
    SAVING_NOTE_ANSWER_COMPARED,  /* the question's answer compared with "yes" */
    SAVING_NOTE_ANSWER_MOVED,     /* the question's cursor compared with where it was */
    SAVING_NOTE_QUESTION_PUT_AWAY, /* "no": the question put away */
    SAVING_NOTE_MOVED_AMONG,      /* the slots' cursor came from this slot */
} SavingNote;

/* How a step ends: on to choosing as the screen fades in, the menus left,
 * back to the main menu, on into saving in the slot chosen (as a jump or as
 * a call), on into the slots' frame, or a buzz where the game can't be saved
 * here. */
typedef enum {
    SAVING_FADES_IN, SAVING_LEAVES_THE_MENUS, SAVING_BACK_TO_THE_MAIN_MENU, SAVING_SAVES_IN_THE_SLOT_JUMPED,
    SAVING_SAVES_IN_THE_SLOT, SAVING_MOVES_AMONG_THE_SLOTS, SAVING_BUZZES,
} SavingEnd;

/* What the sequences ask: the cursor read, a tick, a frame's wait, the
 * panel shown, the save screen laid out over it ($C2:D0E5), the game saved
 * in the slot shown (the sound, the write, a frame, the summary read back, a
 * frame), the saved sound sent, the slot's summary read back, the slot
 * saved in shown ($C2:CF21), a note, and the step's end. Each false where it went no
 * further. */
typedef struct {
    void (*read_cursor)(void *ctx);
    bool (*tick)(void *ctx, SavingPlace at);
    bool (*wait)(void *ctx, SavingPlace at);
    bool (*panel)(void *ctx);
    bool (*lay_out)(void *ctx);
    bool (*save)(void *ctx, SavingPlace at);
    bool (*saved_sound)(void *ctx);
    bool (*summary_read_back)(void *ctx);
    bool (*show_the_slot_saved)(void *ctx);
    void (*note)(void *ctx, SavingNote note, uint8_t value);
    bool (*ends)(void *ctx, SavingEnd end);
} SavingSteps;

/* The panel ($C2:D065); a save point's save screen's steps ($C2:D4F8,
 * $C2:D506); the main menu's ($C2:CEDC, $C2:CEFD); the question's
 * ($C2:CF61, and $C2:CF92, which no code moves to); saving in the slot
 * ($C2:CF0A); the save up to its last wait ($C2:CF37); and the slots' frame
 * ($C2:CFA2, save_screen.c), through `n`. */
bool menu_show_save_panel_with(GameState *g, Platform *pf, const SavingSteps *n, void *ctx);
bool menu_open_save_screen_with(GameState *g, Platform *pf, const SavingSteps *n, void *ctx);
bool menu_choose_on_save_screen_with(GameState *g, Platform *pf, const SavingSteps *n, void *ctx);
bool menu_open_saving_with(GameState *g, Platform *pf, const SavingSteps *n, void *ctx);
bool menu_choose_slot_to_save_in_with(GameState *g, Platform *pf, const SavingSteps *n, void *ctx);
bool menu_answer_whether_to_save_over_with(GameState *g, Platform *pf, const SavingSteps *n, void *ctx);
bool menu_close_the_question_on_a_button_with(GameState *g, Platform *pf, const SavingSteps *n, void *ctx);
bool menu_save_in_the_slot_with(GameState *g, Platform *pf, const SavingSteps *n, void *ctx);
bool menu_save_the_game_with(GameState *g, Platform *pf, const SavingSteps *n, void *ctx);
bool menu_move_among_the_slots_with(GameState *g, Platform *pf, const SavingSteps *n, void *ctx);

/* The slot saved in shown: its window drawn afresh, and its tilemap and the
 * slots' chapters sent ($C2:CF21). */
void menu_show_the_slot_saved_now(GameState *g);

/* The saved sound sent to the driver. */
void menu_send_the_saved_sound(Platform *pf);

/* What the steps take of the state, as their entries' guards do: the panel
 * and a save point's screen, the main menu's screen's opening, the slot shown
 * saved in, the slot saved shown. */
bool menu_save_panel_can_show(const GameState *g);
bool menu_saving_can_open(const GameState *g);
bool menu_save_screen_can_lay_out(const GameState *g);
bool menu_slot_can_be_saved(const GameState *g);
bool menu_slot_saved_can_show(const GameState *g);

/* As C: the panel, from the main menu's (false where the program went no
 * further), and the save screens' steps, from the menus' main task. */
bool menu_show_save_panel(GameState *g, Platform *pf);
bool menu_open_save_screen(GameState *g, Platform *pf);
bool menu_choose_on_save_screen(GameState *g, Platform *pf);
bool menu_open_saving(GameState *g, Platform *pf);
bool menu_choose_slot_to_save_in(GameState *g, Platform *pf);
bool menu_answer_whether_to_save_over(GameState *g, Platform *pf);
bool menu_close_the_question_on_a_button(GameState *g, Platform *pf);

/* The machine's side (saving_bridge.c): its answers to the sequences, which
 * the slots' frame ($C2:CFA2, save_screen.c) runs with too. */
typedef struct {
    Snes *s;
} SavingOnTheMachine;
extern const SavingSteps menu_saving_on_the_machine;

/* The entries ($C2:D065 the panel, $C2:D0DB, $C2:D0E5; a save point's save
 * screen's steps $C2:D4F8 and $C2:D506; the main menu's save screen's
 * $C2:CEDC and $C2:CEFD, and the two steps both share, $C2:CF61 and
 * $C2:CF92; saving in the slot $C2:CF0A, $C2:CF37 and $C2:CF21), and their
 * guards. */
void menu_show_save_panel_entry(Snes *s);
void menu_put_save_panel_away(Snes *s);
void menu_lay_out_save_screen_entry(Snes *s);
void menu_open_save_screen_entry(Snes *s);
void menu_choose_on_save_screen_entry(Snes *s);
void menu_open_saving_entry(Snes *s);
void menu_choose_slot_to_save_in_entry(Snes *s);
void menu_answer_whether_to_save_over_entry(Snes *s);
void menu_close_the_question_on_a_button_entry(Snes *s);
void menu_save_in_the_slot(Snes *s);
void menu_save_the_game(Snes *s);
void menu_show_the_slot_saved(Snes *s);
bool menu_save_panel_shows(Snes *s);
bool menu_save_screen_lays_out(Snes *s);
bool menu_saving_opens(Snes *s);
bool menu_slot_can_be_saved_in(Snes *s);
bool menu_slot_saved_shows(Snes *s);

#endif
