/* mgba_run: play a .gba in mGBA's core with no window and no sound, print
 * what the program writes to mGBA's debug log, and stop where it says it is
 * done (targets/gba/main.c's "ct_gba: done"), or after a number of the
 * GBA's frames (docs/PLATFORM.md, "The GBA build, as built").
 *
 *   mgba_run ROM.gba [MAX_FRAMES]
 *
 * It reports the GBA frames between the program's "ct_gba: playing" and
 * "ct_gba: done", and the processor's cycles a game frame from them (a GBA
 * frame is 280,896 cycles). It is a host tool, built against libmgba
 * (targets/gba/run.py builds it). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mgba/core/core.h>
#include <mgba/core/log.h>

enum { GBA_FRAME_CYCLES = 280896 };

static struct mCore *core;
static int done, playing_at = -1, done_at = -1;
static unsigned long game_frames;

static void log_line(struct mLogger *logger, int category, enum mLogLevel level, const char *format, va_list args)
{
    (void)logger;
    const char *id = mLogCategoryId(category);
    if (!id || strcmp(id, "gba.debug") != 0) {
        if (level & (mLOG_FATAL | mLOG_ERROR)) {
            fprintf(stdout, "mgba: %s: ", mLogCategoryName(category));
            vfprintf(stdout, format, args);
            fputc('\n', stdout);
        }
        return;
    }
    char text[512];
    vsnprintf(text, sizeof text, format, args);
    printf("%s\n", text);
    fflush(stdout);
    if (!strncmp(text, "ct_gba: playing ", 16)) {
        playing_at = (int)core->frameCounter(core);
        game_frames = strtoul(text + 16, NULL, 10);
    } else if (!strcmp(text, "ct_gba: done") || !strcmp(text, "ct_gba: stopped")) {
        done_at = (int)core->frameCounter(core);
        done = 1;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: mgba_run ROM.gba [MAX_FRAMES]\n");
        return 2;
    }
    unsigned long max_frames = argc > 2 ? strtoul(argv[2], NULL, 10) : 2000000ul;
    static struct mLogger logger = { .log = log_line };
    mLogSetDefaultLogger(&logger);
    core = mCoreFind(argv[1]);
    if (!core || !core->init(core)) {
        fprintf(stderr, "mgba_run: no core for %s\n", argv[1]);
        return 2;
    }
    mCoreInitConfig(core, NULL);
    unsigned width, height;
    core->desiredVideoDimensions(core, &width, &height);
    color_t *video = calloc((size_t)width * height, sizeof *video);
    core->setVideoBuffer(core, video, width);
    if (!mCoreLoadFile(core, argv[1])) {
        fprintf(stderr, "mgba_run: cannot load %s\n", argv[1]);
        return 2;
    }
    core->reset(core);
    unsigned long frames = 0;
    while (!done && frames < max_frames) {
        core->runFrame(core);
        frames++;
        if (frames % 10000 == 0) {
            printf("mgba_run: %lu GBA frames\n", frames);
            fflush(stdout);
        }
    }
    int rc = 0;
    if (!done) {
        printf("mgba_run: no end after %lu GBA frames\n", frames);
        rc = 1;
    } else if (playing_at >= 0 && game_frames) {
        unsigned long gba = (unsigned long)(done_at - playing_at);
        printf("mgba_run: %lu game frames in %lu GBA frames: %.0f cycles a game frame, %.1f GBA frames each\n",
               game_frames, gba, (double)gba * GBA_FRAME_CYCLES / (double)game_frames, (double)gba / (double)game_frames);
    }
    core->deinit(core);
    free(video);
    return rc;
}
