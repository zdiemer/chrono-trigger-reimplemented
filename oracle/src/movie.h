/* .ctm input movies. Format: docs/ORACLE.md. */
#ifndef CT_MOVIE_H
#define CT_MOVIE_H

#include <stdint.h>

/* JOY1 ($4218/$4219) bit order. */
enum {
    CT_BTN_R = 1 << 4,
    CT_BTN_L = 1 << 5,
    CT_BTN_X = 1 << 6,
    CT_BTN_A = 1 << 7,
    CT_BTN_RIGHT = 1 << 8,
    CT_BTN_LEFT = 1 << 9,
    CT_BTN_DOWN = 1 << 10,
    CT_BTN_UP = 1 << 11,
    CT_BTN_START = 1 << 12,
    CT_BTN_SELECT = 1 << 13,
    CT_BTN_Y = 1 << 14,
    CT_BTN_B = 1 << 15,
};

typedef struct {
    uint32_t start, count;
    uint16_t buttons;
} ct_movie_event;

typedef struct {
    ct_movie_event *events;
    int count;
} ct_movie;

/* Returns 0 on success; prints a diagnostic and returns -1 on error. */
int ct_movie_load(ct_movie *movie, const char *path);
void ct_movie_free(ct_movie *movie);

/* Buttons held on a frame: the OR of every event covering it. */
uint16_t ct_movie_buttons(const ct_movie *movie, uint32_t frame);

#endif
