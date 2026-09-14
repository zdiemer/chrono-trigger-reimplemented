#include "movie.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        if (sscanf(p, "%lu %lu %127s", &start, &count, buttons) != 3 || parse_buttons(buttons, &mask) != 0) {
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
    }
    fclose(f);
    return 0;
}

void ct_movie_free(ct_movie *movie) {
    free(movie->events);
    memset(movie, 0, sizeof *movie);
}

uint16_t ct_movie_buttons(const ct_movie *movie, uint32_t frame) {
    uint16_t mask = 0;
    for (int i = 0; i < movie->count; i++) {
        const ct_movie_event *e = &movie->events[i];
        if (frame >= e->start && frame - e->start < e->count)
            mask |= e->buttons;
    }
    return mask;
}
