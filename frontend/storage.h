/* Desktop file names and byte storage for the native player. */
#ifndef CT_STORAGE_H
#define CT_STORAGE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char sram[1024];      /* <rom name>.srm */
    char state[1024];     /* quick saves: <rom name>.state, or <movie name>.state while recording */
    char assets[1024];    /* the asset pack (frontend/assets.h): <rom name>.assets */
    char palettes[1024];  /* the pack's palettes as text (frontend/palettes.h): beside it, .palettes */
    char fonts[1024];     /* the pack's fonts as text (frontend/fonts.h): beside it, .fonts */
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
bool files_absolute(char *out, size_t size, const char *path);
const char *files_name(const char *path);

/* The asset pack at f->assets: read it, or where there is none (or `fresh`),
 * import it from the ROM image `rom` and write it there first. Its bytes,
 * malloc'd, *len of them, or NULL after printing why not. */
uint8_t *files_asset_pack(const Files *f, const uint8_t *rom, size_t size, bool fresh, size_t *len);

#endif
