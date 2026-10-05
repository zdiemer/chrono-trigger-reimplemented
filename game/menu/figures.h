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
bool menu_member_figures_are_notable(Snes *s);
bool menu_everyones_figures_are_notable(Snes *s);

#endif
