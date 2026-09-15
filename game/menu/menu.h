/* Menu mode: the game's menu and settings screens (battle mode choice, name
 * entry, the main menu). */
#ifndef CT_GAME_MENU_H
#define CT_GAME_MENU_H

#include "snes/system.h"

/* menu/frame.c */
void menu_wait_for_frame_loop(Snes *s);
void menu_wait_ready_loop(Snes *s);

/* menu/glyphs.c */
bool menu_glyph_mode_is_valid(Snes *s);
void menu_draw_glyph_pixel(Snes *s);
void menu_draw_glyph(Snes *s);

#endif
