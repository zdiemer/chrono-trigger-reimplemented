/* Opening the menus: their setup, the party as they list it, and the
 * settings unpacked (game/menu/opening.c). */
#ifndef CT_GAME_MENU_OPENING_H
#define CT_GAME_MENU_OPENING_H

#include "snes/system.h"
#include "state.h"

/* The party's nine places: three active, six waiting; a place without a
 * member holds a negative byte. The character in a place. */
enum { MENU_PARTY_PLACES = 9, MENU_NO_MEMBER = 0x80 };
uint8_t menu_member_in_place(const GameState *g, unsigned place);

/* The party's members as the menus list them (menu_members): the active
 * ones, then the waiting ones. */
void menu_list_party(GameState *g);

/* The player's settings unpacked into a cell each. */
void menu_unpack_settings(GameState *g);

/* The settings screen's cells packed back into the player's settings. */
void menu_pack_settings(GameState *g);

void menu_open(Snes *s);
void menu_list_party_entry(Snes *s);
void menu_unpack_settings_entry(Snes *s);
bool menu_opening_runs(Snes *s);

#endif
