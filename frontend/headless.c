/* The headless driver: a fixed number of frames as fast as they run, input
 * from --hold, and the picture, sound and state written at the end. */
#include <stdio.h>

#include "driver.h"
#include "media.h"

int run_headless(Player *pl, const HeadlessOptions *o)
{
    static ct_wav wav;
    static int16_t audio[DSP_OUT_FRAMES * 2];
    if (o->wav && ct_wav_open(&wav, o->wav, DSP_SAMPLE_RATE) != 0) {
        fprintf(stderr, "cannot open %s\n", o->wav);
        return 1;
    }
    long frames = o->frames, frame;
    for (frame = 0; frame < frames && !pl->snes.halted; frame++) {
        uint16_t pad = 0;
        for (int i = 0; i < o->nholds; i++) {
            if (frame >= o->holds[i].first && frame <= o->holds[i].last)
                pad |= o->holds[i].buttons;
        }
        player_step(pl, pad);
        ct_wav_write(&wav, audio, player_take_audio(pl, audio, DSP_OUT_FRAMES));
    }
    player_sram_flush(pl);
    int rc = player_movie_flush(pl) != 0;
    if (pl->snes.halted) {
        frames = frame - 1;
        rc = 1;
    }
    if (ct_wav_close(&wav) != 0) {
        fprintf(stderr, "cannot write %s\n", o->wav);
        rc = 1;
    }
    if (o->screenshot && ct_png_write(o->screenshot, player_pixels(pl), RENDER_WIDTH, RENDER_HEIGHT) != 0) {
        fprintf(stderr, "cannot write %s\n", o->screenshot);
        rc = 1;
    }
    if (o->save_state) {
        long waited = 0;
        while (player_busy(pl) && waited < SAVE_WAIT_FRAMES) {
            player_idle_frame(pl);
            waited++;
        }
        if (waited)
            fprintf(stderr, "waited %ld frames for native routines to finish\n", waited);
        if (player_busy(pl)) {
            fprintf(stderr, "cannot save state: native routines stayed suspended\n");
            rc = 1;
        } else if (player_save_state(pl, o->save_state) != 0) {
            rc = 1;
        }
    }
    fprintf(stderr, "ran %ld frames (frame counter %u)\n", frames, (unsigned)pl->snes.frame);
    return rc;
}
