/* Menu mode: the Epoch's two screens, and the End of Time's gate
 * (game/menu/epoch.c). */
#ifndef CT_GAME_MENU_EPOCH_H
#define CT_GAME_MENU_EPOCH_H

#include "port/native.h"

/* The world map view */
void menu_epoch_map_screen(Snes *s);
void menu_epoch_build_map(Snes *s);
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

/* The objects both screens' scripts move */
void menu_epoch_gate_scroll_hdma(Snes *s);
void menu_epoch_gate_drift(Snes *s);
void menu_epoch_gate_scroll_slow(Snes *s);
void menu_epoch_gate_scroll_wrap(Snes *s);
void menu_epoch_place_marker_random(Snes *s);
void menu_epoch_place_marker_edge(Snes *s);
void menu_epoch_set_marker_velocity(Snes *s);

bool menu_epoch_page_is_menu(Snes *s);
bool menu_epoch_bank_is_menu(Snes *s);

/* The End of Time's gate screen */
void menu_start_gate_screen(Snes *s);

#endif
