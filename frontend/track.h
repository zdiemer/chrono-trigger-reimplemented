#ifndef CT_TRACK_H
#define CT_TRACK_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
/* Input by frame since power-on: the replayed movie's frames, then whatever was
 * played after them. */
typedef struct {
    uint16_t *pads;
    uint32_t frames, cap;
    uint32_t replay_frames;    /* frames taken from the movie before live input */
    const char *movie_name;    /* file name of the replayed movie, or NULL */
    const char *sram_name;     /* file name of the --sram save, or NULL */
    uint32_t sram_crc;         /* CRC-32 of the SRAM at power-on */
    const char *record_name;   /* file name of the movie being recorded, or NULL */
    uint32_t written_frames;   /* frames in the movie last handed out */
    uint32_t pending_frames;   /* frames in the movie handed out now */
    uint32_t state_crc;        /* CRC of the quick save made during this recording */
    bool state_ours;
} Track;

bool track_set(Track *t, uint32_t frame, uint16_t pad);
char *track_text(Track *t, const char *source, size_t *len);
#endif
