/* Field mode: the display setup its vblank work drives. */
#ifndef CT_GAME_FIELD_DISPLAY_H
#define CT_GAME_FIELD_DISPLAY_H

#include <stdbool.h>

#include "platform/platform.h"
#include "snes/system.h"
#include "state.h"

/* What the vblank handler sends every frame (field/nmi.c): the sprite table,
 * the queued sprite tile transfers, and the layers' tile map edges their
 * upload flags ask for (FIELD_EDGE_UPLOADS of them, in the flags' order),
 * whose parts' sizes must fit an edge run for the port to send them. */
enum { FIELD_EDGE_UPLOADS = 12 };
void field_upload_sprite_table(Platform *pf);
void field_upload_queued_transfers(GameState *g, Platform *pf);
void field_upload_layer_edge(GameState *g, Platform *pf, unsigned upload);
bool field_layer_edges_fit(const GameState *g);

void field_upload_sprites(Snes *s);
void field_upload_vram_queue(Snes *s);
/* The twelve deferred uploads of tile graphics ($C0:8445-$C0:87BF). */
void field_upload_layer1_tiles(Snes *s);
void field_upload_layer2_tiles(Snes *s);
void field_upload_layer3_tiles(Snes *s);
void field_upload_layer1_tiles_alt(Snes *s);
void field_upload_layer2_tiles_alt(Snes *s);
void field_upload_layer3_tiles_alt(Snes *s);
void field_upload_layer1_columns(Snes *s);
void field_upload_layer2_columns(Snes *s);
void field_upload_layer3_columns(Snes *s);
void field_upload_layer1_columns_alt(Snes *s);
void field_upload_layer2_columns_alt(Snes *s);
void field_upload_layer3_columns_alt(Snes *s);
void field_init_video_mode(Snes *s);
void field_set_screens(Snes *s);
void field_vram_transfer(Snes *s);
void field_patch_window_tiles(Snes *s);
void field_write_window_tiles_0(Snes *s);
void field_write_window_tiles_1(Snes *s);
void field_write_window_tiles_2(Snes *s);
void field_write_window_tiles_3(Snes *s);
void field_irq(Snes *s);
void field_irq_wait_hblank_loop(Snes *s);
void field_irq_after_hblank(Snes *s);
void field_fill_memory(Snes *s);
bool field_display_bank_is_io(Snes *s);
bool field_edge_runs_small(Snes *s);
bool field_window_tiles_dp_is_ppu(Snes *s);

#endif
