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

/* ---- The entries (game/menu/exchange_bridge.c) ------------------------------------------------- */

void menu_open_exchange(Snes *s);
void menu_lay_out_the_exchange(Snes *s);
void menu_move_the_exchange_cursor(Snes *s);
void menu_choose_first_to_exchange(Snes *s);
void menu_choose_second_to_exchange(Snes *s);
void menu_open_exchange_at_the_gate(Snes *s);
void menu_choose_first_at_the_gate(Snes *s);
void menu_show_exchange_panel(Snes *s);
void menu_put_exchange_panel_away(Snes *s);

bool menu_exchange_opens(Snes *s);
bool menu_exchange_lays_out(Snes *s);
bool menu_exchange_cursor_moves(Snes *s);
bool menu_exchange_step_runs(Snes *s);
bool menu_exchange_opens_at_the_gate(Snes *s);
bool menu_exchange_panel_shows(Snes *s);
bool menu_exchange_panel_goes(Snes *s);

#endif
