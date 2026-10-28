/* The Epoch: its two Mode 7 screens, and where it may fly
 * (game/menu/epoch.c). */
#ifndef CT_GAME_MENU_EPOCH_H
#define CT_GAME_MENU_EPOCH_H

#include "menu/object.h"
#include "platform/platform.h"
#include "snes/system.h"
#include "state.h"

/* The map view and the time dial as the program runs them: each from its
 * start until the menus' mode leaves it. False where one went no further. */
bool menu_epoch_map_view_shown(GameState *g, Platform *pf);
bool menu_epoch_time_dial_shown(GameState *g, Platform *pf);

/* The map view */
void menu_epoch_map_screen(Snes *s);
void menu_epoch_build_map(Snes *s);
void menu_epoch_sample_map_colours(Snes *s);
void menu_epoch_place_map_picture(Snes *s);
void menu_epoch_map_cursor(Snes *s);
void menu_epoch_map_matrix(Snes *s);
void menu_epoch_map_party_marker(Snes *s);
void menu_epoch_map_epoch_marker(Snes *s);
void menu_epoch_map_location_marker(Snes *s);
void menu_epoch_map_gradient(Snes *s);
void menu_epoch_load_map_graphics(Snes *s);

/* The time dial */
void menu_epoch_time_dial_screen(Snes *s);
void menu_epoch_time_dial_object(Snes *s);
void menu_epoch_build_gate_window(Snes *s);
void menu_epoch_multiply_signed(Snes *s);
void menu_epoch_gate_matrix_object(Snes *s);
void menu_epoch_time_dial_digit(Snes *s);
void menu_epoch_gate_graphics(Snes *s);
void menu_epoch_load_gate_stream(Snes *s);
void menu_epoch_upload_gate_pixels(Snes *s);
void menu_epoch_upload_gate_tiles(Snes *s);
void menu_epoch_upload_gate_rows(Snes *s);
void menu_epoch_expand_gate_nibbles(Snes *s);

/* Where the Epoch may fly */
void menu_epoch_outside_sides(Snes *s);
void menu_epoch_in_area(Snes *s);

/* Whether the Epoch, flying over the world map, is over the part of it it
 * may not come down in. */
bool menu_epoch_kept_off_the_ground(GameState *g, MenuObject epoch);
/* The same over world map `map` (counted from the first, MENU_FIRST_WORLD_MAP): the
 * dactyls' map's is the part the dactyl doesn't come down in either. */
bool menu_epoch_kept_off_the_ground_of(GameState *g, MenuObject o, unsigned map);

/* Guards */
bool menu_epoch_page_is_menu(Snes *s);
bool menu_epoch_object_runs(Snes *s);
bool menu_epoch_slot_runs(Snes *s);
bool menu_epoch_window_runs(Snes *s);
bool menu_epoch_matrix_runs(Snes *s);
bool menu_epoch_marker_runs(Snes *s);
bool menu_epoch_cursor_runs(Snes *s);
bool menu_epoch_dial_runs(Snes *s);
bool menu_epoch_spin_runs(Snes *s);
bool menu_epoch_digit_runs(Snes *s);
bool menu_epoch_on_world_map(Snes *s);

#endif
