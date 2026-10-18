/* Menu mode: everything that drives the display (docs/PLATFORM.md, P2). */
#ifndef CT_GAME_MENU_DISPLAY_H
#define CT_GAME_MENU_DISPLAY_H

#include "platform/video.h"
#include "snes/system.h"
#include "state.h"

/* Build the per-line colour math table the menus show from the next frame,
 * from the records the screen left (menu_math_records), and put line-effect
 * slot 0 on it. */
void menu_build_math_table(GameState *g, Platform *pf);

/* Send the queued VRAM uploads now, emptying the queue; send the working
 * palette now if it has changed. */
void menu_send_upload_queue(GameState *g, Platform *pf);
void menu_send_palette(GameState *g, Platform *pf);

/* Ask the next vblank to copy a per-line table into the place its channel
 * reads (menu_block_move_source, _dest and _size say what goes where): a
 * table changed between frames goes in whole at once, never half. */
void menu_ask_for_the_block_move(GameState *g);

/* Work RAM at $7E:`at`: `bytes` of it cleared, or filled from bank $FF's
 * `from` (a transfer through the work RAM port; 0 bytes is 64 KiB). */
void menu_clear_memory(GameState *g, Platform *pf, uint16_t at, uint16_t bytes);
void menu_copy_rom_to_memory(GameState *g, Platform *pf, uint16_t from, uint16_t at, uint16_t bytes);

/* The mode at $C2:800E (the menus proper) */
void menu_mode_start(Snes *s);
void menu_mode_return(Snes *s);
void menu_mode_restart(Snes *s);
void menu_nmi_enter(Snes *s);
void menu_nmi_work(Snes *s);
bool menu_nmi_work_runs(Snes *s);
void menu_vblank_upload(Snes *s);
void menu_upload_vram_queue(Snes *s);
void menu_upload_palette(Snes *s);
void menu_build_math_line_table(Snes *s);

/* The mode at $C2:0000 (sprite objects): its own state started over (its
 * direct page cleared and the shadows of its display set as the screen
 * starts), its vblank handler taken, and its upload queue emptied. */
void menu_object_reset_the_mode(GameState *g);
void menu_object_take_the_interrupts(GameState *g);
void menu_object_reset_the_upload_queue(GameState *g);

void menu_object_mode_start(Snes *s);
void menu_object_reset_the_mode_entry(Snes *s);
void menu_object_take_the_interrupts_entry(Snes *s);
void menu_object_reset_the_upload_queue_entry(Snes *s);
void menu_object_init_video(Snes *s);
void menu_object_nmi(Snes *s);
void menu_object_upload_vram_queue(Snes *s);

/* Loading a screen's graphics: all of VRAM cleared; graphics sent from the
 * loader's buffer in bank $7F; the loaded palette sent to CGRAM. */
void menu_clear_all_of_vram(Platform *pf);
void menu_send_loaded_graphics(Platform *pf, uint16_t vram, uint16_t from, uint16_t bytes);
void menu_send_the_loaded_palette(Platform *pf);

void menu_clear_vram(Snes *s);
void menu_upload_vram(Snes *s);
void menu_init_video(Snes *s);
void menu_init_video_memory(Snes *s);
void menu_fill_memory(Snes *s);
void menu_copy_to_memory(Snes *s);

/* The window line table: built, and the per-line slot pointed at it; its
 * entry, which leaves its counts in the menus' first cells (FC04 runs on
 * the menus' page from every caller). */
void menu_put_window_line_table(GameState *g, Platform *pf);
void menu_leave_window_line_table_counts(GameState *g);
void menu_window_build_line_table(Snes *s);

/* Guards */
bool menu_display_bank_is_io(Snes *s);
bool menu_vram_queue_is_whole(Snes *s);
bool menu_object_queue_is_sendable(Snes *s);
bool menu_window_line_table_is_buildable(Snes *s);
bool menu_memory_stretch_misses_the_stack(Snes *s);
bool menu_vblank_work_runs(Snes *s);
bool menu_math_list_fits(Snes *s);

#endif
