/* Menu mode: everything that drives the display (docs/PLATFORM.md, P2). */
#ifndef CT_GAME_MENU_DISPLAY_H
#define CT_GAME_MENU_DISPLAY_H

#include "snes/system.h"

/* The mode at $C2:800E (the menus proper) */
void menu_mode_start(Snes *s);
void menu_mode_return(Snes *s);
void menu_mode_restart(Snes *s);
void menu_nmi_work(Snes *s);
void menu_vblank_upload(Snes *s);
void menu_upload_vram_queue(Snes *s);
void menu_upload_palette(Snes *s);
void menu_build_math_line_table(Snes *s);

/* The mode at $C2:0000 (sprite objects) */
void menu_object_init_video(Snes *s);
void menu_object_nmi(Snes *s);
void menu_object_upload_vram_queue(Snes *s);
void menu_object_upload_sprite_tiles(Snes *s);

/* Loading a screen's graphics */
void menu_clear_vram(Snes *s);
void menu_upload_vram(Snes *s);
void menu_upload_loaded_palette(Snes *s);
void menu_init_video(Snes *s);
void menu_init_video_memory(Snes *s);
void menu_fill_memory(Snes *s);
void menu_copy_to_memory(Snes *s);

/* Window commands */
void menu_window_set_line_effect(Snes *s);
bool menu_window_line_effect_is_known(Snes *s);
void menu_window_build_line_table(Snes *s);

/* Guards */
bool menu_display_bank_is_io(Snes *s);
bool menu_sprite_tiles_page_is_io(Snes *s);

#endif
