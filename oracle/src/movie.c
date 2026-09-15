#include "movie.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Longest movie accepted: about 46 hours at 60 frames per second. */
#define MAX_FRAMES 10000000u

static const struct {
    const char *name;
    uint16_t bit;
} button_names[] = {
    {"B", CT_BTN_B},       {"Y", CT_BTN_Y},       {"SELECT", CT_BTN_SELECT}, {"START", CT_BTN_START},
    {"UP", CT_BTN_UP},     {"DOWN", CT_BTN_DOWN}, {"LEFT", CT_BTN_LEFT},     {"RIGHT", CT_BTN_RIGHT},
    {"A", CT_BTN_A},       {"X", CT_BTN_X},       {"L", CT_BTN_L},           {"R", CT_BTN_R},
};

static int parse_buttons(const char *s, uint16_t *out) {
    *out = 0;
    if (strcmp(s, "-") == 0)
        return 0;
    char buf[128];
    if (strlen(s) >= sizeof buf)
        return -1;
    strcpy(buf, s);
    for (char *tok = strtok(buf, "+"); tok; tok = strtok(NULL, "+")) {
        int found = 0;
        for (size_t i = 0; i < sizeof button_names / sizeof button_names[0]; i++) {
            if (strcmp(tok, button_names[i].name) == 0) {
                *out |= button_names[i].bit;
                found = 1;
            }
        }
        if (!found)
            return -1;
    }
    return 0;
}

int ct_movie_load(ct_movie *movie, const char *path) {
    memset(movie, 0, sizeof *movie);
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "movie: cannot open %s\n", path);
        return -1;
    }
    char line[512];
    int lineno = 0, cap = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash)
            *hash = 0;
        char *p = line;
        while (isspace((unsigned char)*p))
            p++;
        if (!*p)
            continue;

        unsigned long start, count;
        char buttons[128];
        uint16_t mask;
        if (sscanf(p, "%lu %lu %127s", &start, &count, buttons) != 3 || parse_buttons(buttons, &mask) != 0 ||
            start > MAX_FRAMES || count > MAX_FRAMES - start) {
            fprintf(stderr, "movie: %s:%d: expected '<start_frame> <frame_count> <buttons>'\n", path, lineno);
            fclose(f);
            ct_movie_free(movie);
            return -1;
        }
        if (movie->count == cap) {
            cap = cap ? cap * 2 : 64;
            movie->events = realloc(movie->events, cap * sizeof *movie->events);
        }
        movie->events[movie->count++] = (ct_movie_event){(uint32_t)start, (uint32_t)count, mask};
        if (start + count > movie->length)
            movie->length = (uint32_t)(start + count);
    }
    fclose(f);

    /* Per-frame table, so lookups don't scan every event. */
    movie->pads = calloc(movie->length ? movie->length : 1, sizeof *movie->pads);
    for (int i = 0; i < movie->count; i++) {
        const ct_movie_event *e = &movie->events[i];
        for (uint32_t fr = e->start; fr < e->start + e->count; fr++)
            movie->pads[fr] |= e->buttons;
    }
    return 0;
}

void ct_movie_free(ct_movie *movie) {
    free(movie->events);
    free(movie->pads);
    memset(movie, 0, sizeof *movie);
}

uint16_t ct_movie_buttons(const ct_movie *movie, uint32_t frame) {
    return frame < movie->length ? movie->pads[frame] : 0;
}

static void write_buttons(FILE *f, uint16_t mask) {
    const char *sep = "";
    for (size_t i = 0; i < sizeof button_names / sizeof button_names[0]; i++) {
        if (mask & button_names[i].bit) {
            fprintf(f, "%s%s", sep, button_names[i].name);
            sep = "+";
        }
    }
}

int ct_movie_write(const char *path, const uint16_t *pads, uint32_t frames, const char *header) {
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    if (header)
        fputs(header, f);
    fprintf(f, "# <start_frame> <frame_count> <buttons>\n");
    for (uint32_t fr = 0; fr < frames;) {
        uint32_t end = fr + 1;
        while (end < frames && pads[end] == pads[fr])
            end++;
        if (pads[fr]) {
            fprintf(f, "%lu %lu ", (unsigned long)fr, (unsigned long)(end - fr));
            write_buttons(f, pads[fr]);
            fputc('\n', f);
        }
        fr = end;
    }
    return fclose(f) == 0 ? 0 : -1;
}
