/* Input history and the shared .ctm recorder. No files or devices. */
#include "track.h"
#include "movie.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

bool track_set(Track *t, uint32_t frame, uint16_t pad)
{
    if (frame == UINT32_MAX) return false;
    if (frame >= t->cap) {
        uint32_t cap = t->cap < UINT32_MAX / 2 ? t->cap * 2 : UINT32_MAX;
        if (cap <= frame) cap = frame + 1;
        if (SIZE_MAX / cap < sizeof *t->pads) return false;
        uint16_t *pads = realloc(t->pads, (size_t)cap * sizeof *pads);
        if (!pads) return false;
        t->pads = pads;
        t->cap = cap;
    }
    for (uint32_t fr = t->frames; fr < frame; fr++) t->pads[fr] = 0;
    t->pads[frame] = pad;
    if (frame >= t->frames) t->frames = frame + 1;
    return true;
}

char *track_text(Track *t, const char *source, size_t *len)
{
    if (!t->record_name || t->frames == t->written_frames)
        return NULL;
    char header[512];
    int n = snprintf(header, sizeof header, "# Recorded with %.40s from power-on", source);
    if (t->sram_name)
        n += snprintf(header + n, sizeof header - n, " with the save %.80s", t->sram_name);
    if (t->movie_name)
        n += snprintf(header + n, sizeof header - n, ", continuing %.80s", t->movie_name);
    n += snprintf(header + n, sizeof header - n, ".\n# frames %lu\n", (unsigned long)t->frames);
    if (t->sram_name)
        n += snprintf(header + n, sizeof header - n, "# sram crc32 %08X\n", (unsigned)t->sram_crc);
    snprintf(header + n, sizeof header - n, "# Replay: ct_native --rom <rom> --frames %lu --movie %.80s%s\n",
             (unsigned long)t->frames, t->record_name, t->sram_name ? " --sram <save>" : "");
    t->pending_frames = t->frames;
    char *text = ct_movie_format(t->pads, t->frames, header, len);
    if (!text || !t->frames) return text;
    /* A no-button event preserves trailing idle frames in every existing reader. */
    char tail[48];
    int n_tail = snprintf(tail, sizeof tail, "%lu 1 -\n", (unsigned long)t->frames - 1);
    char *grown = realloc(text, *len + (size_t)n_tail + 1);
    if (!grown) { free(text); return NULL; }
    memcpy(grown + *len, tail, (size_t)n_tail + 1);
    *len += (size_t)n_tail;
    return grown;
}
