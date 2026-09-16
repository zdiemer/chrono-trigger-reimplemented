/* Scenes: the routines that drive the display (docs/PLATFORM.md, P2). */
#ifndef CT_GAME_SCENE_DISPLAY_H
#define CT_GAME_SCENE_DISPLAY_H

#include "snes/system.h"

/* Bank C3 */
void scene_frame_work(Snes *s);

/* The program at $7E:3000, each entry guarded by a hash of its bytes */
void scene_init_video(Snes *s);
void scene_upload_sprites(Snes *s);
void scene_clear_sprite_table(Snes *s);
void scene_upload_palette(Snes *s);
void scene_transfer(Snes *s);
void scene_set_vram_4000(Snes *s);
void scene_set_vram_5000(Snes *s);
void scene_set_vram_0000(Snes *s);
void scene_set_vram_6400(Snes *s);
void scene_set_vram_6000(Snes *s);
void scene_upload_tiles_through_port(Snes *s);
void scene_set_mode7_matrix(Snes *s);
void scene_show_layers(Snes *s);
void scene_set_mode_2(Snes *s);
void scene_expand_tile_rows(Snes *s);
void scene_upload_map_and_scroll(Snes *s);

bool scene_init_video_present(Snes *s);
bool scene_upload_sprites_present(Snes *s);
bool scene_clear_sprite_table_present(Snes *s);
bool scene_upload_palette_present(Snes *s);
bool scene_transfer_present(Snes *s);
bool scene_load_graphics_present(Snes *s);
bool scene_upload_tiles_through_port_present(Snes *s);
bool scene_set_mode7_matrix_present(Snes *s);
bool scene_show_layers_present(Snes *s);
bool scene_set_mode_2_present(Snes *s);
bool scene_expand_tile_rows_present(Snes *s);
bool scene_upload_map_and_scroll_present(Snes *s);

#endif
