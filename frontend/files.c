#include "files.h"
#include "file_store.h"
#include <stdio.h>
#include <stdlib.h>

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
    if (sram) {
        if (file_store_replace(f->sram, sram, SNES_SRAM_SIZE) == 0)
            player_sram_stored(pl);
        else
            fprintf(stderr, "cannot write cartridge save %s\n", f->sram);
    }
    size_t len;
    char *misses = player_misses_text(pl, &len);
    if (misses) {
        if (files_write(f->misses, misses, len, true) == 0)
            player_misses_stored(pl);
        free(misses);
    }
}
