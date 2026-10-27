/* The party exchange (game/menu/exchange.c): the screen where the player
 * swaps two of the party's members, and the main menu's panel for it.
 *
 * The bridges on the original's machine are game/menu/exchange_bridge.c; the
 * music the gate's screen changes to is game/menu/sound.c. */
#ifndef CT_GAME_MENU_EXCHANGE_H
#define CT_GAME_MENU_EXCHANGE_H

#include <stdbool.h>
#include <stdint.h>

#include "menu/windows.h"
#include "platform/video.h"
#include "snes/system.h"
#include "state.h"

/* The screen's rows: the active party's three, then the reserve's six, of
 * which four show at a time; the cursor's places are the active party's
 * rows, then the reserve's four shown. */
enum {
    EXCHANGE_ACTIVE_ROWS = 3, EXCHANGE_RESERVE_ROWS = 6, EXCHANGE_ROWS = 9, EXCHANGE_RESERVE_SHOWN = 4,
    EXCHANGE_FIRST_RESERVE_PLACE = EXCHANGE_ACTIVE_ROWS,
};

/* A row (menu_exchange_rows) holds a place of the party's record, with
 * EXCHANGE_ROW_STAYS where the story keeps its member in the active party;
 * an empty one is EXCHANGE_ROW_EMPTY. A cursor place's links to its
 * neighbours (menu_cursor_places) name the place in their low bits. */
enum { EXCHANGE_ROW_EMPTY = 0x80, EXCHANGE_ROW_STAYS = 0x40, EXCHANGE_LINKED_PLACE = 0x0F };

/* ---- Opening ----------------------------------------------------------------------------------- */

/* The main menu's option opens the exchange only where two members could
 * change places; otherwise the main menu takes its cursor back. */
bool menu_exchange_has_two_to_swap(const GameState *g);
void menu_exchange_refuse(GameState *g);

/* The gate's screen at the End of Time: the exchange laid out as its event
 * asks (the whole party, or the active party alone). */
void menu_exchange_lay_out_for_the_gate(GameState *g, uint8_t argument);

/* The cursor put on the first row from `from` that holds a member the
 * player may move, nobody chosen yet, and the screen on to its choosing. */
void menu_exchange_start_choosing(GameState *g, uint8_t from);

/* The screen drawn over the panel: its windows, the members' colours, the
 * reserve's scroll bar where it has more members than rows. Its cursor's
 * first move follows (the bridges run it, since a move can slide the
 * reserve), then menu_exchange_show_the_screen. */
void menu_exchange_draw_the_screen(GameState *g, Platform *pf);
void menu_exchange_show_the_screen(GameState *g);

/* The gate's swirl drawn behind the screen, then shown. */
void menu_exchange_show_the_swirl(GameState *g);

/* The screens the gate's screen goes on to show once its song has started. */
void menu_exchange_show_the_gate(GameState *g);

/* ---- The panel -------------------------------------------------------------------------------- */

/* The panel: the cursor's sprite held back while it draws; the records
 * emptied; its window (the main menu's, or the gate's and its picture, for
 * which the gate's sprite is then loaded: menu_exchange_gate_sprite_loaded).
 * Then its colours and the party's rows listed; a frame later the rows
 * drawn; a frame after that the records started and the members who stay
 * dimmed. */
void menu_exchange_panel_start(GameState *g, Platform *pf);
bool menu_exchange_panel_shows_the_gate(const GameState *g);
void menu_exchange_put_the_gate_sprite_on_its_record(GameState *g);
void menu_exchange_gate_sprite_loaded(GameState *g);
void menu_exchange_panel_colours(GameState *g);
void menu_exchange_panel_draw_the_rows(GameState *g, Platform *pf);
void menu_exchange_panel_finish(GameState *g, Platform *pf);

/* The panel put away as the main menu's cursor leaves it. */
void menu_exchange_panel_put_away(GameState *g);

/* ---- Choosing ---------------------------------------------------------------------------------- */

/* What the frame's input does on the screen. */
typedef enum { EXCHANGE_CHOSEN, EXCHANGE_LEFT, EXCHANGE_MOVED } ExchangeInput;
ExchangeInput menu_exchange_input(const GameState *g);

/* Whether the row under the cursor holds a member the player may choose. */
bool menu_exchange_on_someone_free(const GameState *g);

/* The member under the cursor chosen first: the cursor held on it, its row
 * kept aside where the reserve could scroll it out of view. */
void menu_exchange_choose_the_first(GameState *g);

/* The member under the cursor chosen second: the two change places in the
 * party, their rows and sprites with them. */
void menu_exchange_choose_the_second(GameState *g);

/* The choice let go, every member standing; and back to choosing the
 * first, the cursor where it was. */
void menu_exchange_let_go(GameState *g);
void menu_exchange_back_to_the_first(GameState *g);

/* The cursor's move. Pushed past the reserve's first or last row, the
 * reserve scrolls a row with a slide of six frames (true where it starts
 * one): a frame of the slide (true while frames are left), and its end.
 * Then the cursor settles on a member it may stand on, who walks while the
 * one chosen first holds a pose. A slide shows the reserve's column from a
 * tile row of the tilemap below the screen, which moves a row a frame
 * toward the column's own, the sprites and the second cursor `step` lines
 * a frame with it (up as the reserve scrolls back). */
typedef struct {
    MenuTilePlace from;
    int8_t step;
    uint8_t frames_left;
} ExchangeSlide;
bool menu_exchange_scrolls(GameState *g, Platform *pf, ExchangeSlide *slide);
bool menu_exchange_slide_a_frame(GameState *g, ExchangeSlide *slide);
void menu_exchange_end_the_slide(GameState *g);
void menu_exchange_settle_the_cursor(GameState *g);

/* ---- The steps, one sequence for the machine and for C ---------------------------------------- */

/* The main menu's exchange's steps, the gate's choosing of the first, the
 * screen's layout, the cursor's move and the panel are each one sequence,
 * which the machine's entries (exchange_bridge.c) and the program as C both
 * run; what differs is what they ask of the menus, through ExchangeSteps.
 *
 * The places the sequences tick, wait or move the cursor from (the
 * machine's table gives the original's call at each). */
typedef enum {
    EXCHANGE_AT_THE_OPENING_TICK,       /* the main menu's exchange opened */
    EXCHANGE_AT_THE_FIRST_MOVE,         /* the screen laid out: the cursor's first move */
    EXCHANGE_AT_THE_FIRST_S_MOVE,       /* choosing the first: the cursor moved */
    EXCHANGE_AT_THE_FIRST_S_MOVE_TICK,  /* ... and come to rest elsewhere */
    EXCHANGE_AT_THE_FIRST_CHOSEN_TICK,  /* the first chosen */
    EXCHANGE_AT_THE_SECOND_S_MOVE,      /* choosing the second: the cursor moved */
    EXCHANGE_AT_THE_SECOND_S_MOVE_TICK, /* ... and come to rest elsewhere */
    EXCHANGE_AT_THE_SECOND_CHOSEN_TICK, /* the second chosen, or the choice let go */
    EXCHANGE_AT_THE_GATE_S_MOVE,        /* the gate's choosing of the first: the cursor moved */
    EXCHANGE_AT_THE_GATE_S_MOVE_TICK,   /* ... and come to rest elsewhere */
    EXCHANGE_AT_THE_SLIDE,              /* a frame of the reserve's slide */
    EXCHANGE_AT_THE_SLIDE_TICK,         /* the slide's end */
    EXCHANGE_AT_THE_PANEL_LISTED,       /* the panel: a frame after its rows are listed */
    EXCHANGE_AT_THE_ROWS_DRAWN,         /* ... and after they are drawn */
    EXCHANGE_PLACES
} ExchangePlace;

/* The register widths the original's code sets between the steps' calls,
 * the machine's alone (a no-op as C). */
typedef enum { EXCHANGE_BYTE_WIDE, EXCHANGE_INDEX_WIDE, EXCHANGE_WIDE } ExchangeWidths;

/* The reserve's slide runs inside two routines the original's cursor move
 * calls, each saving P: the machine keeps their frames round the slide's
 * frames (the slide begun back or on, ended, and left). */
typedef enum {
    EXCHANGE_SLIDE_BEGINS_BACK, EXCHANGE_SLIDE_BEGINS_ON, EXCHANGE_SLIDE_ENDED, EXCHANGE_SLIDE_LEFT,
} ExchangeSlideFrame;

/* Where the machine leaves what the original's own tests left (scaffolding). */
typedef enum {
    EXCHANGE_NOTE_REFUSED,          /* not two to swap */
    EXCHANGE_NOTE_CHOOSING_STARTED, /* the cursor on the first free row */
    EXCHANGE_NOTE_BAR_WORKED_OUT,   /* the screen drawn, its scroll bar worked out */
    EXCHANGE_NOTE_SCREEN_SHOWN,     /* the screen queued */
    EXCHANGE_NOTE_INPUT_TESTED,     /* the frame's buttons tested */
    EXCHANGE_NOTE_ROW_TESTED,       /* the row under the cursor tested for someone who stays */
    EXCHANGE_NOTE_SWAPPED,          /* the two members swapped */
    EXCHANGE_NOTE_CURSOR_COMPARED,  /* the cursor compared with where the frame found it */
} ExchangeNote;

/* How a step ends: a buzz and back to the main menu where there aren't two
 * to swap, on into the screen's layout, back to the main menu, or the menus
 * left (from the gate's screen). */
typedef enum {
    EXCHANGE_BUZZES, EXCHANGE_LAYS_OUT_THE_SCREEN, EXCHANGE_BACK_TO_THE_MAIN_MENU, EXCHANGE_LEAVES_THE_MENUS,
} ExchangeEnd;

/* What the sequences ask: the cursor read, a tick, a frame's wait, the
 * cursor's move ($C2:C805), the gate's sprite loaded onto its record
 * (the panel at the gate), the place's song back (leaving the gate's
 * screen), the machine's frames round the slide, the register widths, a
 * note, and the step's end. Each false where it went no further. */
typedef struct {
    void (*read_cursor)(void *ctx);
    bool (*tick)(void *ctx, ExchangePlace at);
    bool (*wait)(void *ctx, ExchangePlace at);
    bool (*move_the_cursor)(void *ctx, ExchangePlace at);
    bool (*gate_sprite)(void *ctx);
    bool (*place_song_back)(void *ctx);
    void (*slide)(void *ctx, ExchangeSlideFrame frame);
    void (*widths)(void *ctx, ExchangeWidths widths);
    void (*note)(void *ctx, ExchangeNote note);
    bool (*ends)(void *ctx, ExchangeEnd end);
} ExchangeSteps;

/* The steps ($C2:C627 the opening, $C2:C65D the first, $C2:C6B9 the second,
 * $C2:C7E0 the gate's first), the layout ($C2:CAF3), the cursor's move
 * ($C2:C805) and the panel ($C2:C9E5), through `n`. */
bool menu_open_exchange_with(GameState *g, Platform *pf, const ExchangeSteps *n, void *ctx);
bool menu_choose_first_to_exchange_with(GameState *g, Platform *pf, const ExchangeSteps *n, void *ctx);
bool menu_choose_second_to_exchange_with(GameState *g, Platform *pf, const ExchangeSteps *n, void *ctx);
bool menu_choose_first_at_the_gate_with(GameState *g, Platform *pf, const ExchangeSteps *n, void *ctx);
bool menu_lay_out_the_exchange_with(GameState *g, Platform *pf, const ExchangeSteps *n, void *ctx);
bool menu_move_the_exchange_cursor_with(GameState *g, Platform *pf, const ExchangeSteps *n, void *ctx);
bool menu_show_exchange_panel_with(GameState *g, Platform *pf, const ExchangeSteps *n, void *ctx);

/* What the steps take of the state, as their entries' guards do: the
 * opening, the layout, the cursor's move, a choosing step, the panel, and
 * the gate's screen's opening. */
bool menu_exchange_can_open(const GameState *g);
bool menu_exchange_can_lay_out(const GameState *g);
bool menu_exchange_cursor_can_move(const GameState *g);
bool menu_exchange_step_can_run(const GameState *g);
bool menu_exchange_panel_can_show(const GameState *g);
bool menu_exchange_can_open_at_the_gate(const GameState *g);

/* As C: the panel, from the main menu's (false where the program went no
 * further), and the steps, from the menus' main task. */
bool menu_show_exchange_panel(GameState *g, Platform *pf);
bool menu_open_exchange(GameState *g, Platform *pf);
bool menu_choose_first_to_exchange(GameState *g, Platform *pf);
bool menu_choose_second_to_exchange(GameState *g, Platform *pf);
bool menu_choose_first_at_the_gate(GameState *g, Platform *pf);

/* The machine's side (exchange_bridge.c). */
typedef struct {
    Snes *s;
} ExchangeOnTheMachine;
extern const ExchangeSteps menu_exchange_on_the_machine;

/* ---- The entries (game/menu/exchange_bridge.c) ------------------------------------------------- */

void menu_open_exchange_entry(Snes *s);
void menu_lay_out_the_exchange(Snes *s);
void menu_move_the_exchange_cursor(Snes *s);
void menu_choose_first_to_exchange_entry(Snes *s);
void menu_choose_second_to_exchange_entry(Snes *s);
void menu_open_exchange_at_the_gate(Snes *s);
void menu_choose_first_at_the_gate_entry(Snes *s);
void menu_show_exchange_panel_entry(Snes *s);
void menu_put_exchange_panel_away(Snes *s);

bool menu_exchange_opens(Snes *s);
bool menu_exchange_lays_out(Snes *s);
bool menu_exchange_cursor_moves(Snes *s);
bool menu_exchange_step_runs(Snes *s);
bool menu_exchange_opens_at_the_gate(Snes *s);
bool menu_exchange_panel_shows(Snes *s);
bool menu_exchange_panel_goes(Snes *s);

#endif
