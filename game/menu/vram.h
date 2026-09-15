/* Menu: queueing VRAM uploads for the vblank handler. */
#ifndef CT_GAME_MENU_VRAM_H
#define CT_GAME_MENU_VRAM_H

#include "snes/system.h"

/* menu/vram.c */
void menu_queue_vram_upload(Snes *s);
void menu_queue_vram_upload_7e(Snes *s);

#endif
