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

/* The entries: a record of the ROM's (bank C2), or one built in bank $7E. */
void menu_queue_vram_upload(Snes *s);
void menu_queue_built_vram_upload(Snes *s);
bool menu_upload_queue_has_room(Snes *s);
bool menu_built_upload_is_apart(Snes *s);

#endif
