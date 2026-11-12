/* The desktop's storage: where the bytes the player persists (player.h) are
 * kept, and what they are called. Shared by ct_play's two drivers; a host
 * without files keeps them its own way. */
#ifndef CT_FRONTEND_FILES_H
#define CT_FRONTEND_FILES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "player.h"

#include "storage.h"

/* After each frame: write a quick save the player took, and the movie being
 * recorded every RECORD_FLUSH_FRAMES. */
void files_after_frame(const Files *f, Player *pl);
/* Write what changed: the SRAM (if the player persists it), the misses log. */
void files_flush(const Files *f, Player *pl);
/* Write the movie being recorded if it grew. Returns 0, or -1. */
int files_flush_movie(const Files *f, Player *pl);

/* State files: --save-state and --load-state, and the quick save. */
int files_save_state(Player *pl, const char *path);
int files_load_state(Player *pl, const char *path);

#endif
