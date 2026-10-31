/* A member's figures as the menus work them out, and the party's items set
 * aside where the story holds them (game/menu/figures.c). */
#ifndef CT_GAME_MENU_FIGURES_H
#define CT_GAME_MENU_FIGURES_H

#include <stdbool.h>
#include <stdint.h>

#include "snes/system.h"
#include "state.h"

/* The figures of the member loaded last (menu_load_member) worked out from
 * its stats and what it wears, into menu_figures_worked_out and
 * menu_figures_greatest_hp; HP over the greatest comes down to it. */
void menu_work_out_figures(GameState *g);

/* The same as a screen shows them: the evade doubled where the member wears
 * the accessory that doubles it. */
void menu_work_out_figures_shown(GameState *g);

/* How a screen marks a figure of the member equipping, by how the item tried
 * on moves it from what the member has (menu_figure_marks): the text's colour
 * where it stays, brighter where it rises, darker where it falls. */
typedef enum { FIGURE_AS_IT_IS = 0x00, FIGURE_RAISED = 0x08, FIGURE_LOWERED = 0x1C } MenuFigureMark;
static inline MenuFigureMark menu_figure_mark(uint16_t was, uint16_t is)
{
    return is == was ? FIGURE_AS_IT_IS : is > was ? FIGURE_RAISED : FIGURE_LOWERED;
}

/* Trying an item on, as the equipment screen and the shop do with the item
 * on the cursor: it goes into the slot the screen has open in the member's
 * status record, and is the item the screen then describes
 * (menu_item_operand). Then the member's figures are worked out with it, and
 * each figure the screen's window shows is marked by how the item moves it
 * (menu_figure_marks, in the window's order): the equipment screen's nine,
 * or the shop's seven stats, which the shop's record also takes on. */
typedef enum { FIGURES_ON_THE_EQUIPMENT_SCREEN, FIGURES_IN_THE_SHOP } MenuFiguresWindow;
void menu_try_on(GameState *g, uint8_t item);
void menu_mark_the_figures_tried_on(GameState *g, MenuFiguresWindow window);

/* The figures shown, then written back to the member's record and its
 * copy: the figures, the greatest HP, and the HP. */
void menu_note_member_figures(GameState *g);

/* Every character's figures worked out and written back, as the menus open. */
void menu_note_everyones_figures(GameState *g);

/* The party's items set aside where the story holds the party to what it
 * has, so that the screens' lists show none of them; and put back. */
void menu_set_items_aside(GameState *g);
void menu_put_items_back(GameState *g);

/* The registered entries ($C2:916E, $C2:9137, $C2:99B2, $C2:90FC, $C2:9121)
 * and their guards. */
void menu_work_out_figures_entry(Snes *s);
void menu_note_member_figures_entry(Snes *s);
void menu_note_everyones_figures_entry(Snes *s);
void menu_set_items_aside_entry(Snes *s);
void menu_put_items_back_entry(Snes *s);
bool menu_figures_are_workable(Snes *s);
/* Its state half: what the member has on all items of the game's. */
bool menu_figures_can_be_worked_out(const GameState *g);
bool menu_member_figures_are_notable(Snes *s);
bool menu_everyones_figures_are_notable(Snes *s);
/* Its state half: every character's equipment items of the game's. */
bool menu_everyones_figures_can_be_noted(const GameState *g);

#endif
