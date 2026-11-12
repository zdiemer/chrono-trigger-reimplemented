/* Menu mode: everything that drives the display (docs/PLATFORM.md, P2). */
#ifndef CT_GAME_MENU_DISPLAY_H
#define CT_GAME_MENU_DISPLAY_H

#include "platform/video.h"
#include <stdbool.h>
#include <stdint.h>
#include "state.h"
#include "vars.h"

/* Build the per-line colour math table the menus show from the next frame,
 * from the records the screen left (menu_math_records), and put line-effect
 * slot 0 on it. */
void menu_build_math_table(GameState *g, Platform *pf);

/* Send the queued VRAM uploads now, emptying the queue; send the working
 * palette now if it has changed. */
void menu_send_upload_queue(GameState *g, Platform *pf);
/* What the vblank needs to send the frame: the upload queue's entries whole
 * and from memory, and a block move the frame asked for a buffer's. And the
 * queue sent where it is whole, the program going no further otherwise. */
bool menu_upload_queue_is_whole(const GameState *g);
bool menu_block_move_is_a_buffer(const GameState *g);
bool menu_upload_queue_sent(GameState *g, Platform *pf);
void menu_send_palette(GameState *g, Platform *pf);

/* Ask the next vblank to copy a per-line table into the place its channel
 * reads (menu_block_move_source, _dest and _size say what goes where): a
 * table changed between frames goes in whole at once, never half. */
void menu_ask_for_the_block_move(GameState *g);

/* Work RAM at $7E:`at`: `bytes` of it cleared, or filled from bank $FF's
 * `from` (a transfer through the work RAM port; 0 bytes is 64 KiB). */
void menu_clear_memory(GameState *g, Platform *pf, uint16_t at, uint16_t bytes);
void menu_copy_rom_to_memory(GameState *g, Platform *pf, uint16_t from, uint16_t at, uint16_t bytes);

/* The mode at $C2:800E (the menus proper): asked for `asked_for` with
 * `asked_with` (the screen's argument), until a screen closes them, or no
 * further where a step meets a state the port doesn't handle. Its vblank and line handlers, and the
 * menus closed from inside the task that closes them (which never comes
 * back). */
typedef enum { MENUS_WENT_NO_FURTHER, MENUS_CLOSED, MENUS_START_THE_GAME_AGAIN } MenuModeEnd;
MenuModeEnd menu_mode_runs(GameState *g, Platform *pf, uint8_t asked_for, uint16_t asked_with);
void menu_vblank(GameState *g, Platform *pf);
void menu_line(GameState *g, Platform *pf);
void menu_close(GameState *g, Platform *pf, bool for_the_game);

/* The mode at $C2:0000 (sprite objects): its own state started over (its
 * scratch cleared and the shadows of its display set as the screen
 * starts), its vblank handler taken, and its upload queue emptied. */
void menu_object_reset_the_mode(GameState *g);
void menu_object_take_the_interrupts(GameState *g);
void menu_object_reset_the_upload_queue(GameState *g);

/* The mode's start ($C2:000F): the screen blanked and everything
 * stopped, then the screen, the mode's own state, its handlers and its upload
 * queue set up and its palette cleared. The world map is set up after it
 * (overworld/setup.c). */
void menu_object_mode_starts(GameState *g, Platform *pf);
/* The mode's screen set up ($C2:0043), as it starts and as the world map
 * comes back from the menus. */
void menu_object_set_up_the_screen(GameState *g, Platform *pf);

/* The mode's vblank handler, and its line handler, which only returns. */
enum { MENU_OBJECT_VBLANK_HANDLER = 0xC2015E };
void menu_object_vblank(GameState *g, Platform *pf);
void menu_object_line(GameState *g, Platform *pf);

/* Whether the mode's upload queue can be sent: whole, from memory. */
bool menu_object_queue_can_be_sent(const GameState *g);

/* Loading a screen's graphics: all of VRAM cleared; graphics sent from the
 * loader's buffer in bank $7F; the loaded palette sent to CGRAM. */
void menu_clear_all_of_vram(Platform *pf);
void menu_send_loaded_graphics(Platform *pf, uint16_t vram, uint16_t from, uint16_t bytes);
void menu_send_the_loaded_palette(Platform *pf);

/* The loader's screen set up, with the console stopped, and all of VRAM and
 * the screen's buffers in work RAM cleared ($C2:940D). */
void menu_video_set_up(GameState *g, Platform *pf);
/* The window line table: built, and the per-line slot pointed at it
 * ($FF:FC04). */
void menu_put_window_line_table(GameState *g, Platform *pf);

#endif
