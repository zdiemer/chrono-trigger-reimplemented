/* The jet bike race's display work in the scene program (docs/PLATFORM.md, P2). */
#ifndef CT_GAME_SCENE_RACE_H
#define CT_GAME_SCENE_RACE_H

#include "snes/system.h"

/* All of these are code in the program at $7E:3000, guarded by a hash of their
 * bytes. */
void scene_setup_line_effects(Snes *s);
void scene_race_clear_buffers(Snes *s);
void scene_race_setup_maps(Snes *s);
void scene_race_fill_map(Snes *s);
void scene_race_set_vram_0000(Snes *s);
void scene_race_set_vram_0000_again(Snes *s);
void scene_race_begin_tile_uploads(Snes *s);
void scene_race_set_vram_7100(Snes *s);
void scene_race_set_vram_5000(Snes *s);
void scene_race_clear_scratch(Snes *s);
void scene_race_set_vram_6800(Snes *s);
void scene_race_set_vram_6400(Snes *s);
void scene_race_move_camera(Snes *s);
void scene_race_upload_column_rom(Snes *s);
void scene_race_upload_column_ram(Snes *s);
void scene_race_upload_tilemap(Snes *s);
void scene_race_upload_strip(Snes *s);
void scene_race_upload_plane(Snes *s);

bool scene_setup_line_effects_present(Snes *s);
bool scene_race_clear_buffers_present(Snes *s);
bool scene_race_setup_maps_present(Snes *s);
bool scene_race_fill_map_present(Snes *s);
bool scene_race_load_graphics_present(Snes *s);
bool scene_race_move_camera_present(Snes *s);
bool scene_race_upload_column_present(Snes *s);
bool scene_race_upload_tilemap_present(Snes *s);
bool scene_race_upload_strip_present(Snes *s);
bool scene_race_upload_plane_present(Snes *s);

#endif
