/* The menus' windows: the command lists every menu screen is drawn from
 * (game/menu/windows.c). */
#ifndef CT_GAME_MENU_WINDOWS_H
#define CT_GAME_MENU_WINDOWS_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/video.h"
#include "snes/system.h"
#include "state.h"

/* Where something is drawn in a tilemap buffer in bank $7E: a tile's place,
 * two bytes a tile and 32 tiles a row. */
enum { MENU_TILE_BYTES = 2, MENU_TILEMAP_ROW_BYTES = 0x40 };

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
 * port's guard, before it draws the list as C). */
bool menu_window_list_drawable(const GameState *g, uint16_t list);
bool menu_window_list_drawable_from(const GameState *g, uint16_t list, uint16_t origin);

/* A tile's place as the lists give it: its column (0-31) and row from the
 * drawing origin. */
typedef struct {
    uint8_t column, row;
} MenuTilePlace;

/* A rectangle of tiles `size` big copied from one place to another, a row at
 * a time and each row from its left (a rectangle copied over itself reads
 * what it has already written), as a list's copy command does; and whether
 * it stays in the tilemap buffers from the drawing origin `origin`. */
void menu_copy_tile_area(GameState *g, MenuTilePlace from, MenuTilePlace to, MenuWindowSize size);
bool menu_tile_area_can_be_copied(const GameState *g, uint16_t origin, MenuTilePlace from, MenuTilePlace to,
                                  MenuWindowSize size);

/* A window in `style` at `at` in bank $7E: its frame, and its inside. */
void menu_draw_window_frame(GameState *g, uint16_t at, MenuWindowSize size, MenuWindowStyle style);
void menu_fill_window_inside(GameState *g, uint16_t at, MenuWindowSize size, MenuWindowStyle style);

/* What the original's frame and inside leave in the drawing's cells, for a
 * window a screen drew whole at `at` (scaffolding: ORIGINAL_EFFECT). */
void menu_leave_window_cells(GameState *g, uint16_t at, MenuWindowSize size, MenuWindowStyle style);

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

/* The entries: the interpreter, each command's handler (with its guard), and
 * the drawing other screens call. */
#define MENU_WINDOW_HANDLERS(X)                     \
    X(menu_window_fill, 0x00)                       \
    X(menu_window_copy_area, 0x02)                  \
    X(menu_window_draw, 0x04)                       \
    X(menu_window_draw_string, 0x06)                \
    X(menu_window_set_origin, 0x08)                 \
    X(menu_window_copy_bytes, 0x0E)                 \
    X(menu_window_set_line_effect, 0x0C)            \
    X(menu_window_swap_areas, 0x10)                 \
    X(menu_window_draw_frame_only, 0x12)            \
    X(menu_window_fill_inside_only, 0x14)
#define MENU_WINDOW_DECLARE_HANDLER(name, command) \
    void name(Snes *s);                            \
    bool name##_accepts(Snes *s);
MENU_WINDOW_HANDLERS(MENU_WINDOW_DECLARE_HANDLER)
#undef MENU_WINDOW_DECLARE_HANDLER

void menu_run_window_list(Snes *s);
void menu_window_frame(Snes *s);
void menu_window_inside(Snes *s);
void menu_window_colours(Snes *s);
void menu_window_in_design(Snes *s);

void menu_lay_out_label_strip(Snes *s);
void menu_lay_out_graphic_strip(Snes *s);

bool menu_window_list_is_drawable(Snes *s);
bool menu_strip_is_placeable(Snes *s);
bool menu_window_frame_is_drawable(Snes *s);
bool menu_window_inside_is_drawable(Snes *s);
bool menu_window_colours_are_loadable(Snes *s);
bool menu_palette_change_counts(Snes *s);
bool menu_palette_changes_have_room(const GameState *g);
bool menu_window_in_design_is_drawable(Snes *s);

#endif
