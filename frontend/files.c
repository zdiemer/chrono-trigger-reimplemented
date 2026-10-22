#include "files.h"

#include "assets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t *files_read(const char *path, size_t *len, bool text, bool *missing)
{
    if (missing)
        *missing = false;
    FILE *f = fopen(path, text ? "r" : "rb");
    if (!f) {
        if (missing)
            *missing = true;
        return NULL;
    }
    uint8_t *data = NULL;
    size_t n = 0, cap = 0, got;
    do {
        if (n == cap) {
            cap = cap ? cap * 2 : 65536;
            uint8_t *grown = realloc(data, cap);
            if (!grown) {
                free(data);
                fclose(f);
                return NULL;
            }
            data = grown;
        }
        got = fread(data + n, 1, cap - n, f);
        n += got;
    } while (got);
    bool bad = ferror(f) != 0;
    fclose(f);
    if (bad) {
        free(data);
        return NULL;
    }
    *len = n;
    return data;
}

int files_write(const char *path, const void *data, size_t len, bool text)
{
    FILE *f = fopen(path, text ? "w" : "wb");
    int ok = f && fwrite(data, 1, len, f) == len;
    if (f && fclose(f) != 0)
        ok = 0;
    if (!ok) {
        fprintf(stderr, "cannot write %s\n", path);
        return -1;
    }
    return 0;
}

const char *files_name(const char *path)
{
    const char *name = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\')
            name = p + 1;
    }
    return name;
}

void files_side_path(char *out, size_t n, const char *name_path, const char *dir_of, const char *dir, const char *ext)
{
    const char *name = files_name(name_path);
    const char *dot = strrchr(name, '.');
    int stem = dot && dot != name ? (int)(dot - name) : (int)strlen(name);
    if (dir)
        snprintf(out, n, "%s/%.*s%s", dir, stem, name, ext);
    else
        snprintf(out, n, "%.*s%.*s%s", (int)(files_name(dir_of) - dir_of), dir_of, stem, name, ext);
}

uint8_t *files_asset_pack(const Files *f, const uint8_t *rom, size_t size, bool fresh, size_t *len)
{
    bool missing = true;
    uint8_t *pack = fresh ? NULL : files_read(f->assets, len, false, &missing);
    if (pack || !missing) {
        if (!pack)
            fprintf(stderr, "cannot read %s\n", f->assets);
        return pack;
    }
    const char *why = "";
    pack = asset_pack_import(rom, size, len, &why);
    if (!pack) {
        fprintf(stderr, "cannot import the game's resources: %s\n", why);
        return NULL;
    }
    if (files_write(f->assets, pack, *len, false) != 0) {
        free(pack);
        return NULL;
    }
    fprintf(stderr, "imported %u resources (%lu bytes) from the ROM into %s\n", (unsigned)asset_catalogue_count,
            (unsigned long)*len, f->assets);
    return pack;
}

int files_save_state(Player *pl, const char *path)
{
    size_t len;
    uint8_t *data = player_save_state(pl, &len);
    int rc = data ? files_write(path, data, len, false) : -1;
    free(data);
    if (rc != 0) {
        fprintf(stderr, "cannot save state %s\n", path);
        return -1;
    }
    fprintf(stderr, "saved state %s\n", path);
    return 0;
}

int files_load_state(Player *pl, const char *path)
{
    size_t len = 0;
    bool missing;
    char why[200] = "it can't be read";
    uint8_t *data = files_read(path, &len, false, &missing);
    if (missing)
        snprintf(why, sizeof why, "there is none");
    int rc = data ? player_load_state(pl, data, len, why, sizeof why) : pl->track.record_name ? PLAYER_NOT_OURS : -1;
    free(data);
    if (rc == PLAYER_NOT_OURS)
        fprintf(stderr, "not loading %s: while recording, only a quick save made in this recording loads\n", path);
    else if (rc != 0)
        fprintf(stderr, "cannot load state %s: %s\n", path, why);
    else
        fprintf(stderr, "loaded state %s\n", path);
    return rc == 0 ? 0 : -1;
}

int files_flush_movie(const Files *f, Player *pl)
{
    size_t len;
    char *text = player_movie_text(pl, &len);
    if (!text)
        return 0;
    int rc = files_write(f->movie, text, len, true);
    free(text);
    if (rc == 0)
        player_movie_stored(pl);
    return rc;
}

void files_after_frame(const Files *f, Player *pl)
{
    size_t len;
    uint8_t *state = player_take_quick_save(pl, &len);
    if (state) {
        if (files_write(f->state, state, len, false) == 0)
            fprintf(stderr, "saved state %s\n", f->state);
        else
            fprintf(stderr, "cannot save state %s\n", f->state);
        free(state);
    }
    if (player_movie_unwritten(pl) >= RECORD_FLUSH_FRAMES)
        files_flush_movie(f, pl);
}

void files_flush(const Files *f, Player *pl)
{
    const uint8_t *sram = player_sram_changed(pl);
    if (sram && files_write(f->sram, sram, SNES_SRAM_SIZE, false) == 0)
        player_sram_stored(pl);
    size_t len;
    char *misses = player_misses_text(pl, &len);
    if (misses) {
        if (files_write(f->misses, misses, len, true) == 0)
            player_misses_stored(pl);
        free(misses);
    }
}
