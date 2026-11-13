/* The menus: their screens laid out across a wider view (menu/panels_across.c). */
#ifndef CT_GAME_MENU_PANELS_ACROSS_H
#define CT_GAME_MENU_PANELS_ACROSS_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/platform.h"
#include "state.h"

enum { MENU_PANEL_ROWS = 28 };

/* Where each of the screen's rows of tiles was parted last frame, and how far
 * its two sides moved: the program's own state, beside the game's memory,
 * which the sprites over the screen follow. */
enum { MENU_PANEL_LAYERS = 3, MENU_PANEL_COLUMNS = 32 };
typedef struct {
    bool shown;
    uint16_t map;
    uint8_t row;
    uint16_t cell[MENU_PANEL_COLUMNS];
} MenuLayerRow;   /* a layer's row of tiles as a screen row showed it */
typedef struct {
    bool laid_out;
    int8_t moves;                      /* columns each side moved out */
    uint8_t cut[MENU_PANEL_ROWS];      /* the first column of the right side */
    MenuLayerRow rows[MENU_PANEL_ROWS][MENU_PANEL_LAYERS];   /* what the layers held, as the layout read them */
} MenuPanels;
/* Module storage supplied by the state owner, independent of mapped RAM. */
MenuPanels *menu_panels(GameState *g);

/* On a wider view, the screen's layers laid out across it, from what they
 * hold now and how the last picture showed them; and the sprites over the
 * screen moved with the columns under them, before the sprite table is sent.
 * Neither changes the game or takes its time. */
void menu_lay_out_the_screen_across(GameState *g, Platform *pf);
void menu_move_the_sprites_across(GameState *g);
/* The sprites' places dropped as the menus close, so that none outlives them. */
void menu_panels_closed(GameState *g);

#endif
