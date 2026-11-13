/* Battle: the battle window laid out across a wider view (battle/wide_window.c). */
#ifndef CT_GAME_BATTLE_WIDE_WINDOW_H
#define CT_GAME_BATTLE_WIDE_WINDOW_H

#include <stdint.h>

#include "platform/platform.h"
#include "state.h"

/* What the window's rows in VRAM show, as the vblank last sent them. */
typedef enum { WINDOW_SHOWS_OTHER, WINDOW_SHOWS_PARTY, WINDOW_SHOWS_TECHS, WINDOW_SHOWS_ITEMS } WindowShows;

/* What the window held when its rows and its frames were last sent: the
 * program's own state, beside the game's memory, which nothing the game
 * decides reads. */
typedef struct {
    uint8_t shows;      /* WindowShows */
    uint8_t layout;     /* the settings' layout of the party's lines, when they were sent */
    uint8_t divider;    /* the frames' border between the windows at the left and the party's, 0 for none */
} BattleWideWindow;
/* Module storage supplied by the state owner, independent of mapped RAM. */
BattleWideWindow *battle_wide_window(GameState *g);

/* The rows the vblank sends, and the frames, noted as they go; the frames
 * also as a battle sets its tilemaps up. */
void battle_note_window_rows_sent(GameState *g, WindowShows shows);
void battle_note_frames_sent(GameState *g);
/* On a wider view, the window laid out across it for this frame. */
void battle_lay_out_the_window_across(GameState *g, Platform *pf);

#endif
