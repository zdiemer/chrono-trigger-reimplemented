#include "track.h"
#include "movie.h"
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    Track track = { .record_name = "test.ctm", .sram_name = "test.srm", .sram_crc = 0x12345678 };
    for (uint32_t i = 0; i < 1000; i++)
        if (!track_set(&track, i, i >= 20 && i < 30 ? CT_BTN_A | CT_BTN_UP : 0)) return 1;
    size_t len;
    char *text = track_text(&track, "ct_native", &len);
    ct_movie movie;
    if (!text || ct_movie_parse(&movie, text, len, "recording") || movie.length != 1000 ||
        !movie.has_sram || movie.sram_crc != track.sram_crc) return 1;
    for (uint32_t i = 0; i < track.frames; i++)
        if (ct_movie_buttons(&movie, i) != track.pads[i]) return 1;
    ct_movie_free(&movie);
    free(text);
    track.written_frames = track.pending_frames;
    if (track_text(&track, "ct_native", &len)) return 1;
    /* Rewinding must shorten the output, including a trailing idle run. */
    track.frames = 25;
    text = track_text(&track, "ct_play", &len);
    if (!text || ct_movie_parse(&movie, text, len, "rewind") || movie.length != 25) return 1;
    ct_movie_free(&movie);
    free(text);
    free(track.pads);
    puts("recorder_test: OK");
    return 0;
}
