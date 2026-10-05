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

/* Queue an upload for the next vblank (the queue holds 32, 8 bytes each). */
enum { MENU_UPLOAD_BYTES = 8 };
void menu_queue_upload(GameState *g, MenuUpload u);

/* Queue one of the menus' own uploads by its record in bank C2
 * (menu_data.h's MENU_UPLOAD_*), as the original's queueing ($C2:8385) does,
 * its time that routine's. */
void menu_queue_fixed_upload(GameState *g, uint16_t record);

/* The sprite-object mode's queue (its own, on its direct page): uploads from
 * anywhere in memory, each record holding how the VRAM address steps as it
 * goes -- a word at a time along a tilemap row, or a tilemap row at a time
 * down a column -- as the byte its vblank handler hands the video service
 * (vid_vram_port_decode). A run of uploads is queued in one hold of the queue,
 * so the vblank handler never sends half of it; the first's place in the
 * queue (the byte offset of its record) comes back. A record queued at an
 * end past $F8 lies past the byte's reach, as the original's page-relative
 * writes put it. */
typedef enum { MENU_PORT_WORD_BY_WORD = 0x80, MENU_PORT_ROW_BY_ROW = 0x81 } MenuVramPort;
enum { MENU_OBJECT_UPLOAD_BYTES = 8 };
typedef struct {
    uint32_t source;
    uint16_t vram, size;
    MenuVramPort port;
} MenuObjectUpload;
uint8_t menu_queue_object_uploads(GameState *g, const MenuObjectUpload *uploads, unsigned count);
/* One upload, a word at a time. */
void menu_queue_object_upload(GameState *g, uint32_t source, uint16_t vram, uint16_t size);
/* Whether the queue's end is on a record (the game's always is). */
bool menu_object_queue_is_whole(const GameState *g);

/* For the original's code and shadow mode: the record of an upload a
 * routine queued as C where the original builds it in work RAM first
 * (menu_built_upload); nothing reads it after. */
void menu_leave_built_upload(GameState *g, MenuUpload u);

/* What queueing the upload whose record is at `record_at` leaves in the
 * registers (A's high byte $FF, X past the record, Y past the queue's new
 * entry, the data bank on work RAM's), for a bridge that queued it as C and
 * goes on to a call that sees them (scaffolding: ORIGINAL_EFFECT). */
void menu_leave_queued_upload(Snes *s, uint16_t record_at);

/* ... and A's high byte all ones, as any block move leaves it. */
void menu_leave_a_high_byte_set(Snes *s);

/* The entries: a record of the ROM's (bank C2), or one built in bank $7E. */
void menu_queue_vram_upload(Snes *s);
void menu_queue_built_vram_upload(Snes *s);
bool menu_upload_queue_has_room(Snes *s);
bool menu_built_upload_is_apart(Snes *s);

#endif
