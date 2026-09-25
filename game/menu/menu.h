/* The menus: bank C2's two modes, and the text the dialog and the menus
 * share.
 *
 * Bank C2 runs two modes of its own. The menus proper ($C2:800E) are the
 * main menu, the save and load screens, shops and the rest of the windowed
 * screens: they draw text and windows into tilemaps in work RAM and queue them
 * for VRAM (text.c, vram.c, save_screen.c), and their vblank handler sends the
 * frame's display state, sprites, palette and queue (display.c). The
 * sprite-object mode ($C2:0000) is the one the Epoch's screens and the world
 * maps' scenes run in: a map of three planes behind a set of sprite objects,
 * each run by a handler or a command script (map.c, objects.c,
 * object_script.c, object_commands.c, top_layer.c, wave.c, epoch.c).
 * Each mode's main loop waits for its vblank handler (frame.c). The music the
 * menus switch to is sound.c, and the dialog's proportional font, set a
 * glyph at a time, typeset.c and glyphs.c. What they read from the ROM is menu_data.h. */
#ifndef CT_GAME_MENU_H
#define CT_GAME_MENU_H

#include "snes/system.h"

/* menu/frame.c */
void menu_wait_for_frame_loop(Snes *s);
void menu_wait_ready_loop(Snes *s);
bool menu_frame_wait_on_page(Snes *s);
bool menu_ready_wait_reaches_low_ram(Snes *s);

#include "menu/glyphs.h"

#endif
