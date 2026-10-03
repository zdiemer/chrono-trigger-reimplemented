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
/* The twelve deferred uploads of the layers' edge runs: a row or a column
 * scrolling into view, each layer's. */
void field_upload_layer1_bottom_row(Snes *s);
void field_upload_layer2_bottom_row(Snes *s);
void field_upload_layer3_bottom_row(Snes *s);
void field_upload_layer1_top_row(Snes *s);
void field_upload_layer2_top_row(Snes *s);
void field_upload_layer3_top_row(Snes *s);
void field_upload_layer1_right_column(Snes *s);
void field_upload_layer2_right_column(Snes *s);
void field_upload_layer3_right_column(Snes *s);
void field_upload_layer1_left_column(Snes *s);
void field_upload_layer2_left_column(Snes *s);
void field_upload_layer3_left_column(Snes *s);
void field_init_video_mode(Snes *s);
void field_set_screens(Snes *s);
void field_vram_transfer(Snes *s);
void field_irq(Snes *s);
void field_irq_wait_hblank_loop(Snes *s);
void field_irq_after_hblank(Snes *s);
void field_fill_memory(Snes *s);
bool field_display_bank_is_io(Snes *s);
bool field_edge_runs_small(Snes *s);

/* A transfer to VRAM the field describes (field_transfer) and sends at once
 * ($C0:2DC8), its size 0 for 64 KiB; and a stretch of work RAM cleared
 * ($C0:2DF1), described as a transfer's source and size. The routines they
 * stand for, for the ports that run them as C. */
typedef struct {
    uint16_t vram;
    uint8_t mode;
    uint32_t source;
    uint16_t size;
} FieldTransfer;
void field_send_transfer(GameState *g, Platform *pf, FieldTransfer t);
/* The description alone, where the field describes a transfer it then
 * doesn't make. */
void field_describe_transfer(GameState *g, FieldTransfer t);
void field_clear_memory(GameState *g, Platform *pf, uint32_t from, uint16_t bytes);
enum { FIELD_VRAM_TRANSFER_ENTRY = 0xC02DC8, FIELD_FILL_MEMORY_ENTRY = 0xC02DF1 };

/* The last queued transfer's second size, which the queue's upload leaves in
 * Y for the menus' per-frame routine; `none` where the queue is empty. */
uint16_t field_queue_last_second_size(const GameState *g, uint16_t none);

/* field/window_corners.c */
void field_refresh_corners(GameState *g, Platform *pf);
/* Whether the windows' corners are to be refreshed: while a window's
 * selection is kept (field_window_tile_slot not the no-selection marker). */
bool field_windows_need_refresh(const GameState *g);
void field_redraw_after_picture(Snes *s);
bool field_redraw_can_run(Snes *s);
void field_refresh_window_corners(Snes *s);
void field_write_window_corners_0(Snes *s);
void field_write_window_corners_1(Snes *s);
void field_write_window_corners_2(Snes *s);
void field_write_window_corners_3(Snes *s);
bool field_window_corners_dp_is_ppu(Snes *s);

#endif
