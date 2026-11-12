/* The menus' windows: the command lists every menu screen is drawn from
 * (game/menu/windows.c). */
#ifndef CT_GAME_MENU_WINDOWS_H
#define CT_GAME_MENU_WINDOWS_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/video.h"
#include "state.h"

/* Where something is drawn in a tilemap buffer in bank $7E: a tile's place,
 * two bytes a tile and 32 tiles a row. */
enum { MENU_TILE_BYTES = 2, MENU_TILEMAP_ROW_BYTES = 0x40 };

/* The front layer's tilemap buffer is where the screens draw their
 * windows; a tile of it by its place in the bank, as the screens name
 * their places. */
enum { MENU_FRONT_LAYER_AT = 0x2E00 };

/* A window's style: the tilemap word of its first tile, which says where its
 * twenty tiles start and the attribute (palette and priority) they all take. */
typedef uint16_t MenuWindowStyle;

/* A window's size in tiles, its frame included. */
typedef struct {
    uint8_t columns, rows;
} MenuWindowSize;

/* Run the window command list at `list` in bank $FF: draw what it says into
 * the menus' tilemaps, from the drawing origin it sets. */
void menu_draw_window_list(GameState *g, Platform *pf, uint16_t list);

/* The same as a screen draws its windows, its time the original's list
 * runner's ($C2:ED31). */
void menu_draw_windows(GameState *g, Platform *pf, uint16_t list);

/* Whether the list can be drawn from the drawing origin as it is: its
 * commands whole, in the tilemap buffers, and its strings convertible (for a
 * port's check before it draws the list). */
bool menu_window_list_drawable(const GameState *g, uint16_t list);
bool menu_window_list_drawable_from(const GameState *g, uint16_t list, uint16_t origin);

/* A tile's place as the lists give it: its column (0-31) and row from the
 * drawing origin. */
typedef struct {
    uint8_t column, row;
} MenuTilePlace;

/* A rectangle of tiles `size` big copied from one place to another, a row at
 * a time and each row from its left (a rectangle copied over itself reads
 * what it has already written), as a list's copy command does; two such
 * rectangles exchanged, as its swap command does; and whether a copy stays
 * in the tilemap buffers from the drawing origin `origin`. */
void menu_copy_tile_area(GameState *g, MenuTilePlace from, MenuTilePlace to, MenuWindowSize size);
void menu_swap_tile_areas(GameState *g, MenuTilePlace one, MenuTilePlace other, MenuWindowSize size);
bool menu_tile_area_can_be_copied(const GameState *g, uint16_t origin, MenuTilePlace from, MenuTilePlace to,
                                  MenuWindowSize size);

/* A window in `style` at `at` in bank $7E: its frame, and its inside. */
void menu_draw_window_frame(GameState *g, uint16_t at, MenuWindowSize size, MenuWindowStyle style);
void menu_fill_window_inside(GameState *g, uint16_t at, MenuWindowSize size, MenuWindowStyle style);

/* The window style of the design the player chose: its tiles, in
 * `attribute`. */
MenuWindowStyle menu_design_style(const GameState *g, uint8_t attribute);

/* The colours of the design the player chose, into the upper half of the
 * palette row `style`'s attribute names. */
void menu_load_window_colours(GameState *g, MenuWindowStyle style);

/* A graphic's tiles laid across a strip of tilemap two rows high, `columns`
 * wide: a label's, in palette row 4, from its first tile; or any graphic's,
 * in the attribute and from the tile `tile` names. The second returns the
 * place past the strip. */
void menu_lay_out_label(GameState *g, uint16_t at, uint16_t columns, uint8_t first);
uint16_t menu_lay_out_strip(GameState *g, uint16_t at, uint16_t columns, uint16_t tile);

bool menu_palette_changes_have_room(const GameState *g);

static inline unsigned menu_front_tile_of(uint16_t at) { return (uint16_t)(at - MENU_FRONT_LAYER_AT) / MENU_TILE_BYTES; }

#endif
