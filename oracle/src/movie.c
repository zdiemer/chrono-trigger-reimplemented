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

int ct_movie_parse(ct_movie *movie, const char *text, size_t len, const char *name) {
    memset(movie, 0, sizeof *movie);
    char line[512];
    int lineno = 0, cap = 0;
    size_t at = 0;
    while (at < len) {
        /* One line, as fgets would read it into line[]: a longer one continues
         * as the next. */
        size_t n = 0;
        while (at < len && n < sizeof line - 1) {
            char c = text[at++];
            line[n++] = c;
            if (c == '\n')
                break;
        }
        line[n] = 0;
        lineno++;
        unsigned long crc;
        if (sscanf(line, " # sram crc32 %lx", &crc) == 1) {
            movie->has_sram = 1;
            movie->sram_crc = (uint32_t)crc;
        }
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
            fprintf(stderr, "movie: %s:%d: expected '<start_frame> <frame_count> <buttons>'\n", name, lineno);
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

    /* Per-frame table, so lookups don't scan every event. */
    movie->pads = calloc(movie->length ? movie->length : 1, sizeof *movie->pads);
    for (int i = 0; i < movie->count; i++) {
        const ct_movie_event *e = &movie->events[i];
        for (uint32_t fr = e->start; fr < e->start + e->count; fr++)
            movie->pads[fr] |= e->buttons;
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
    char *text = NULL;
    size_t len = 0, cap = 0, n;
    char chunk[4096];
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) {
        if (len + n > cap) {
            cap = (len + n) * 2;
            text = realloc(text, cap);
        }
        memcpy(text + len, chunk, n);
        len += n;
    }
    int bad = ferror(f);
    fclose(f);
    int rc = -1;
    if (bad)
        fprintf(stderr, "movie: cannot read %s\n", path);
    else
        rc = ct_movie_parse(movie, text ? text : "", len, path);
    free(text);
    return rc;
}

void ct_movie_free(ct_movie *movie) {
    free(movie->events);
    free(movie->pads);
    memset(movie, 0, sizeof *movie);
}

uint16_t ct_movie_buttons(const ct_movie *movie, uint32_t frame) {
    return frame < movie->length ? movie->pads[frame] : 0;
}

/* A growing string. */
typedef struct {
    char *s;
    size_t len, cap;
    int failed;
} text_buf;

static void put(text_buf *b, const char *str) {
    size_t n = strlen(str);
    if (b->len + n + 1 > b->cap) {
        size_t cap = (b->len + n + 1) * 2;
        char *s = realloc(b->s, cap);
        if (!s) {
            b->failed = 1;
            return;
        }
        b->s = s;
        b->cap = cap;
    }
    memcpy(b->s + b->len, str, n + 1);
    b->len += n;
}

char *ct_movie_format(const uint16_t *pads, uint32_t frames, const char *header, size_t *len) {
    text_buf b = {0};
    put(&b, "");
    if (header)
        put(&b, header);
    put(&b, "# <start_frame> <frame_count> <buttons>\n");
    for (uint32_t fr = 0; fr < frames;) {
        uint32_t end = fr + 1;
        while (end < frames && pads[end] == pads[fr])
            end++;
        if (pads[fr]) {
            char num[48];
            snprintf(num, sizeof num, "%lu %lu ", (unsigned long)fr, (unsigned long)(end - fr));
            put(&b, num);
            const char *sep = "";
            for (size_t i = 0; i < sizeof button_names / sizeof button_names[0]; i++) {
                if (pads[fr] & button_names[i].bit) {
                    put(&b, sep);
                    put(&b, button_names[i].name);
                    sep = "+";
                }
            }
            put(&b, "\n");
        }
        fr = end;
    }
    if (b.failed) {
        free(b.s);
        return NULL;
    }
    *len = b.len;
    return b.s;
}

int ct_movie_write(const char *path, const uint16_t *pads, uint32_t frames, const char *header) {
    size_t len;
    char *text = ct_movie_format(pads, frames, header, &len);
    FILE *f = text ? fopen(path, "w") : NULL;
    int ok = f && fwrite(text, 1, len, f) == len;
    if (f && fclose(f) != 0)
        ok = 0;
    free(text);
    return ok ? 0 : -1;
}
