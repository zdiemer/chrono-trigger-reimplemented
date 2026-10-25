/* The sprite-object mode's objects that change the screen rather than move on
 * it (game/menu/screen_objects.c): the fades, the mosaic, the palette's loads
 * and fades, and the tile streamer, each a handler a script's command starts
 * (menu/object_script.c), and the screen's display bytes they work on. */
#ifndef CT_GAME_MENU_SCREEN_OBJECTS_H
#define CT_GAME_MENU_SCREEN_OBJECTS_H

#include <stdbool.h>
#include <stdint.h>

#include "menu/object.h"
#include "snes/system.h"
#include "state.h"

/* The handlers, by the addresses in bank C2 a slot names them with. */
#define MENU_HANDLER_AT(routine) ((uint16_t)(routine))
enum {
    MENU_TILE_STREAMER = MENU_HANDLER_AT(0xC21CF5),
    MENU_PALETTE_LOADER = MENU_HANDLER_AT(0xC21DD4),
    MENU_PALETTE_FADE = MENU_HANDLER_AT(0xC21E38),
    MENU_FADE_OUT = MENU_HANDLER_AT(0xC220A2),
    MENU_FADE_IN = MENU_HANDLER_AT(0xC22105),
    MENU_MOSAIC_CLEARS = MENU_HANDLER_AT(0xC22194),
    MENU_MOSAIC_GROWS = MENU_HANDLER_AT(0xC221F8),
};

/* The screen's display, as the mode's vblank handler sends it: whether it is
 * off, and how bright. */
bool menu_screen_is_off(const GameState *g);
uint8_t menu_screen_display(const GameState *g);

/* Ask the vblank handler to send the palette with the next frame. */
void menu_send_the_palette(GameState *g);

/* Every colour of the palette black, and no row loading or fading. */
void menu_clear_the_palette(GameState *g);

/* The handlers' logic: each runs its object a frame and says whether it is
 * done. */
bool menu_stream_tiles(GameState *g, MenuObject self);
bool menu_load_palette_row(GameState *g, MenuObject self);
bool menu_fade_palette_row(GameState *g, MenuObject self);
bool menu_fade_out(GameState *g, MenuObject self);
bool menu_fade_in(GameState *g, MenuObject self);
bool menu_clear_mosaic(GameState *g, MenuObject self);
bool menu_grow_mosaic(GameState *g, MenuObject self);

/* The guards' state halves: whether each takes the object as it stands. */
bool menu_tile_streamer_takes(const GameState *g, MenuObject self);
bool menu_palette_loader_takes(const GameState *g, MenuObject self);
bool menu_palette_fade_takes(const GameState *g, MenuObject self);
bool menu_fade_out_takes(const GameState *g, MenuObject self);
bool menu_fade_in_takes(const GameState *g, MenuObject self);
bool menu_mosaic_takes(const GameState *g, MenuObject self);

/* The registered entries and their guards. */
void menu_stream_tiles_entry(Snes *s);
void menu_load_palette_row_entry(Snes *s);
void menu_fade_palette_row_entry(Snes *s);
void menu_fade_out_entry(Snes *s);
void menu_fade_in_entry(Snes *s);
void menu_clear_mosaic_entry(Snes *s);
void menu_grow_mosaic_entry(Snes *s);
void menu_clear_the_palette_entry(Snes *s);
bool menu_tile_streamer_runs(Snes *s);
bool menu_palette_loader_runs(Snes *s);
bool menu_palette_fade_runs(Snes *s);
bool menu_fade_out_runs(Snes *s);
bool menu_fade_in_runs(Snes *s);
bool menu_mosaic_runs(Snes *s);
bool menu_palette_can_be_cleared(Snes *s);

#endif
