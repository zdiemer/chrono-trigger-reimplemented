/* The load screen: its steps, its drawing, and a slot's erasing
 * (game/menu/load_screen.c), and its steps on the original's machine
 * (game/menu/load_screen_bridge.c). */
#ifndef CT_GAME_MENU_LOAD_SCREEN_H
#define CT_GAME_MENU_LOAD_SCREEN_H

#include <stdbool.h>
#include <stdint.h>

#include "menu/vram.h"
#include "platform/video.h"
#include "snes/system.h"
#include "state.h"

/* The slots' windows and rows as the load screen and the save screen draw
 * them (the windows' list, then the three slots' rows). */
void menu_draw_the_slots(GameState *g, Platform *pf);
void menu_draw_slot_rows(GameState *g);
void menu_draw_slot_rows_entry(Snes *s);
/* A slot's window drawn afresh in the design its game was saved with
 * ($C2:D32C), as the save screen does once it has saved in the slot. */
void menu_redraw_slot_window(GameState *g, uint8_t slot);

/* ---- The steps, one sequence for the machine and for C ---------------------------------------- */

/* The screen's steps, its drawing and its showing of the slot under the
 * cursor ($C2:E743, save_screen.c) are each one sequence, which the
 * machine's entries (load_screen_bridge.c) and the program as C both run;
 * what differs is what they ask of the menus, through LoadScreenSteps.
 *
 * The places the sequences tick or show the slot from (the machine's table
 * gives the original's call at each). */
typedef enum {
    LOAD_AT_THE_OPENING_SHOW,      /* the slot the screen opens on shown */
    LOAD_AT_THE_ERASE_ASKED_TICK,  /* Select and Start held over a slot */
    LOAD_AT_THE_CHOOSING_SHOW,     /* choosing: no choice this frame, the slot shown */
    LOAD_AT_THE_CHOOSING_MOVE_TICK, /* ... and the cursor moved */
    LOAD_AT_THE_CHOSEN_TICK,       /* a row chosen */
    LOAD_AT_THE_CARRY_ON_SHOW,     /* which game a New Game+ carries on from: the slot shown */
    LOAD_AT_THE_CARRY_ON_MOVE_TICK, /* ... and the cursor moved */
    LOAD_AT_THE_CARRY_ON_CHOSEN_TICK, /* ... a slot chosen */
    LOAD_AT_THE_CARRY_ON_BACK_TICK, /* ... going back */
    LOAD_AT_THE_ERASE_MOVE_TICK,   /* the erase question: the cursor moved */
    LOAD_AT_THE_ERASE_ANSWERED_TICK, /* ... answered */
    LOAD_AT_THE_FOURTH_ROW_FLIP_TICK, /* the fourth row's choice flipped as the slot is shown */
    LOAD_PLACES
} LoadScreenPlace;

/* Where the machine leaves what the original's own tests left (scaffolding),
 * with the value the sequence had then. */
typedef enum {
    LOAD_NOTE_START_SLOT,          /* the slot the screen opens on, picked */
    LOAD_NOTE_ERASE_ASKED,         /* erasing asked: the step's number set */
    LOAD_NOTE_NOT_ERASING,         /* the erase buttons tested and not held, or over the fourth row */
    LOAD_NOTE_MOVED,               /* the cursor moved since the frame began */
    LOAD_NOTE_LOADED,              /* a slot's game loaded */
    LOAD_NOTE_NEW_GAME_SET_UP,     /* a new game's state set up */
    LOAD_NOTE_NEW_GAME_PLUS,       /* New Game+ picked on the fourth row */
    LOAD_NOTE_BACK_TO_CHOOSING,    /* back from the carrying on to choosing */
    LOAD_NOTE_ANSWER_MOVED,        /* the erase question's cursor moved */
    LOAD_NOTE_ANSWERED,            /* the erase question answered and closed */
    LOAD_NOTE_ERASE_YES,           /* ... with a yes */
    LOAD_NOTE_CHOOSING_AGAIN,      /* ... the step set back to choosing */
    LOAD_NOTE_FOURTH_ROW_COMPARED, /* the slot shown compared with the fourth row (the game finished) */
    LOAD_NOTE_FOURTH_ROW_FLIPPED,  /* the fourth row's choice flipped to this place */
    LOAD_NOTE_ERASING,             /* a slot's erasing starting */
    LOAD_NOTE_SLOT_FORGOTTEN,      /* ... its game forgotten: the slot */
    LOAD_NOTE_ERASED,              /* ... its row drawn again */
} LoadScreenNote;

/* How a step ends: on to choosing as the screen fades in, or out of the
 * menus to the game loaded or begun. */
typedef enum { LOAD_FADES_IN, LOAD_LEAVES_FOR_THE_GAME } LoadScreenEnd;

/* What the sequences ask: the cursor read, a tick, the slot under the cursor
 * shown, the screen drawn, a New Game+'s question asked, the slot shown's
 * game loaded or carried over by a New Game+, a slot erased, the empty
 * slot's label drawn, a frame's wait, the sound output told to the driver, the machine's
 * register widths where the original's code changes them before a call (a
 * no-op as C), a note, and the step's end. Each false where it went no
 * further. */
typedef struct {
    void (*read_cursor)(void *ctx);
    bool (*tick)(void *ctx, LoadScreenPlace at);
    bool (*show_the_slot)(void *ctx, LoadScreenPlace at);
    bool (*draw)(void *ctx);
    bool (*ask_which_to_carry_on_from)(void *ctx);
    bool (*load)(void *ctx);
    bool (*carry_on)(void *ctx);
    bool (*erase)(void *ctx);
    bool (*empty_label)(void *ctx);
    bool (*wait_a_frame)(void *ctx);
    bool (*sound_output_sent)(void *ctx);
    void (*byte_wide)(void *ctx);
    void (*note)(void *ctx, LoadScreenNote note, uint8_t value);
    bool (*ends)(void *ctx, LoadScreenEnd end);
} LoadScreenSteps;

/* The steps ($C2:E613 the opening, $C2:E61B choosing, $C2:E6AE which game a
 * New Game+ carries on from, $C2:E705 the erase question's answer), the
 * screen's drawing ($C2:E7C3) and the slot under the cursor shown
 * ($C2:E743, menu/save_screen.c), through `n`. */
bool menu_open_load_screen_with(GameState *g, Platform *pf, const LoadScreenSteps *n, void *ctx);
bool menu_choose_on_load_screen_with(GameState *g, Platform *pf, const LoadScreenSteps *n, void *ctx);
bool menu_choose_game_to_carry_on_from_with(GameState *g, Platform *pf, const LoadScreenSteps *n, void *ctx);
bool menu_answer_erase_with(GameState *g, Platform *pf, const LoadScreenSteps *n, void *ctx);
bool menu_draw_load_screen_with(GameState *g, Platform *pf, const LoadScreenSteps *n, void *ctx);
bool menu_show_save_slot_with(GameState *g, Platform *pf, const LoadScreenSteps *n, void *ctx);
/* A slot's erasing ($C2:E8B7): its game forgotten, its row drawn empty and,
 * a frame on, its label queued and its window drawn again. */
bool menu_erase_slot_with(GameState *g, Platform *pf, const LoadScreenSteps *n, void *ctx);

/* A New Game+'s question: which game it carries on from, the slot's header
 * given way to its label ($C2:E841). */
void menu_ask_which_game_to_carry_on_from(GameState *g);

/* The sound driver told whether to play in stereo, as the screen leaves for
 * the game. */
void menu_send_the_sound_output(GameState *g, Platform *pf);

/* What the steps take of the state, as their entries' guards do: the
 * drawing (it reads which slot was saved last from save memory), choosing
 * and the carrying on, the slot under the cursor shown, the erase question's
 * answer. */
bool menu_load_screen_can_draw(const GameState *g, Platform *pf);
bool menu_load_screen_can_choose(const GameState *g);
bool menu_load_screen_slot_can_show(const GameState *g);
bool menu_erase_can_be_answered(const GameState *g);
bool menu_slot_can_be_erased(const GameState *g);

/* As C, from the menus' main task: the four steps; false where the program
 * went no further. */
bool menu_open_load_screen(GameState *g, Platform *pf);
bool menu_choose_on_load_screen(GameState *g, Platform *pf);
bool menu_choose_game_to_carry_on_from(GameState *g, Platform *pf);
bool menu_answer_erase(GameState *g, Platform *pf);
/* And the slot's erasing the answer's yes runs. */
bool menu_erase_slot(GameState *g, Platform *pf);

/* What erasing a slot does: the slot's game forgotten, and its chapter
 * label's upload. The original builds the upload's record, seven bytes, where
 * the summary is drawn from (load_screen_bridge.c). */
enum { MENU_LABEL_RECORD_BYTES = 7 };
void menu_forget_slot(GameState *g, Platform *pf, uint8_t slot);
MenuUpload menu_slot_label_upload(uint8_t slot);

/* The machine's side (load_screen_bridge.c): its answers to the sequences,
 * which the slot's showing ($C2:E743, save_screen.c) runs with too. */
typedef struct {
    Snes *s;
} LoadScreenOnTheMachine;
extern const LoadScreenSteps menu_load_screen_on_the_machine;

/* The entries ($C2:E613, $C2:E61B, $C2:E6AE, $C2:E705; $C2:E7C3, $C2:E841,
 * $C2:E8B7, $C2:D32C), and their guards. */
void menu_open_load_screen_entry(Snes *s);
void menu_choose_on_load_screen_entry(Snes *s);
void menu_choose_game_to_carry_on_from_entry(Snes *s);
void menu_answer_erase_entry(Snes *s);
void menu_draw_load_screen(Snes *s);
void menu_ask_which_to_carry_on_from(Snes *s);
void menu_erase_slot_entry(Snes *s);
void menu_draw_slot_window(Snes *s);

bool menu_load_screen_runs(Snes *s);
bool menu_erase_is_answerable(Snes *s);
bool menu_load_screen_draws(Snes *s);
bool menu_slot_window_is_drawable(Snes *s);

#endif
