/* The desktop's storage: where the bytes the player persists (player.h) are
 * kept, and what they are called. Shared by ct_play's two drivers; a host
 * without files keeps them its own way. */
#ifndef CT_FRONTEND_FILES_H
#define CT_FRONTEND_FILES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "player.h"

typedef struct {
    char sram[1024];      /* <rom name>.srm */
    char state[1024];     /* quick saves: <rom name>.state, or <movie name>.state while recording */
    char misses[1024];    /* <rom name>.misses.txt */
    const char *movie;    /* the movie being recorded, or NULL */
} Files;

/* A whole file, malloc'd (at least one byte, so an empty file isn't NULL), or
 * NULL if it can't be read; *missing says it couldn't be opened. `text` reads
 * with the host's line ends. */
uint8_t *files_read(const char *path, size_t *len, bool text, bool *missing);
/* Write a whole file. Returns 0, or -1 after printing why not. */
int files_write(const char *path, const void *data, size_t len, bool text);

/* dir/<name_path's file name without extension><ext>; dir defaults to dir_of's directory. */
void files_side_path(char *out, size_t n, const char *name_path, const char *dir_of, const char *dir, const char *ext);
const char *files_name(const char *path);

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
