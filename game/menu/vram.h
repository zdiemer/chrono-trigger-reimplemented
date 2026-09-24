/* The menus: queueing uploads to VRAM for the vblank handler
 * (game/menu/vram.c). */
#ifndef CT_GAME_MENU_VRAM_H
#define CT_GAME_MENU_VRAM_H

#include <stdint.h>

#include "snes/system.h"
#include "state.h"

/* An upload to VRAM: `size` bytes (0 for 64 KiB) from `source`, a 24-bit
 * address, to the VRAM word at `vram`. Records of this shape sit in the ROM
 * for the menus' fixed uploads (menu/menu_data.h) or are built in work RAM by
 * the code that queues them; `spare` is the record's eighth byte, which the
 * queue copies with it and nothing reads. */
typedef struct {
    uint16_t vram;
    uint32_t source;
    uint16_t size;
    uint8_t spare;
} MenuUpload;

/* Queue an upload for the next vblank (the queue holds 32). */
void menu_queue_upload(GameState *g, MenuUpload u);

/* The sprite-object mode's queue (its own, on its direct page): uploads from
 * anywhere in memory, whose VRAM port steps as `port` says (MENU_PORT_WORDS a
 * word at a time, MENU_PORT_ROWS a tilemap row at a time). Queueing holds the
 * queue from the vblank handler and gives the queue's end, the byte offset of
 * the next record; the records' fields are set by offset; releasing it sets
 * the new end. menu_queue_object_upload does all of it for one upload a word
 * at a time. */
enum { MENU_PORT_WORDS = 0x80, MENU_PORT_ROWS = 0x81 };
enum { MENU_OBJECT_UPLOAD_BYTES = 8 };
uint8_t menu_hold_object_queue(GameState *g);
void menu_release_object_queue(GameState *g, uint8_t end);
void menu_set_object_upload_bank(GameState *g, uint8_t at, uint8_t bank);
void menu_set_object_upload_port(GameState *g, uint8_t at, uint8_t port);
void menu_set_object_upload_source(GameState *g, uint8_t at, uint16_t source);
void menu_set_object_upload_vram(GameState *g, uint8_t at, uint16_t vram);
void menu_set_object_upload_size(GameState *g, uint8_t at, uint16_t size);
void menu_queue_object_upload(GameState *g, uint32_t source, uint16_t vram, uint16_t size);
/* Whether the queue's end is on a record (the game's always is). */
bool menu_object_queue_is_whole(const GameState *g);

/* The entries: a record of the ROM's (bank C2), or one built in bank $7E. */
void menu_queue_vram_upload(Snes *s);
void menu_queue_built_vram_upload(Snes *s);
bool menu_upload_queue_has_room(Snes *s);
bool menu_built_upload_is_apart(Snes *s);

#endif
