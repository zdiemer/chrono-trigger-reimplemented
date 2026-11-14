/* mgba_run: play a .gba in mGBA's core with no window and no sound, print
 * what the program writes to mGBA's debug log, and stop where it says it is
 * done (targets/gba/main.c's "ct_gba: done"), or after a number of the
 * GBA's frames (docs/PLATFORM.md, "The GBA build, as built").
 *
 *   mgba_run ROM.gba [MAX_FRAMES] [--frame-at ADDRESS] [--shot FRAME FILE.png]...
 *
 * It reports the GBA frames between the program's "ct_gba: playing" and
 * "ct_gba: done", and the processor's cycles a game frame from them (a GBA
 * frame is 280,896 cycles). With --frame-at, the address of the game frame
 * the program is in (main.c's ct_gba_frame), it writes the GBA's picture as
 * a PNG once the game's frame reaches each FRAME. It is a host tool, built
 * against libmgba and zlib (targets/gba/run.py builds it). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include <mgba/core/core.h>
#include <mgba/core/log.h>

enum { GBA_FRAME_CYCLES = 280896, MAX_SHOTS = 32 };

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

static void put32(unsigned char *at, uint32_t v)
{
    at[0] = (unsigned char)(v >> 24), at[1] = (unsigned char)(v >> 16), at[2] = (unsigned char)(v >> 8), at[3] = (unsigned char)v;
}

static void chunk(FILE *f, const char *kind, const unsigned char *data, uint32_t length)
{
    unsigned char head[8];
    put32(head, length);
    memcpy(head + 4, kind, 4);
    fwrite(head, 1, 8, f);
    fwrite(data, 1, length, f);
    uLong crc = crc32(crc32(0, (const Bytef *)kind, 4), data, length);
    unsigned char tail[4];
    put32(tail, (uint32_t)crc);
    fwrite(tail, 1, 4, f);
}

/* The picture as an 8-bit RGB PNG. */
static int write_png(const char *path, const color_t *pixels, unsigned width, unsigned height)
{
    size_t row = 1 + (size_t)width * 3;
    unsigned char *raw = malloc(row * height);
    uLongf packed_length = compressBound((uLong)(row * height));
    unsigned char *packed = malloc(packed_length);
    FILE *f = fopen(path, "wb");
    int rc = -1;
    if (raw && packed && f) {
        for (unsigned y = 0; y < height; y++) {
            raw[y * row] = 0;
            for (unsigned x = 0; x < width; x++) {
                uint32_t c = pixels[y * width + x];
                unsigned char *p = &raw[y * row + 1 + x * 3];
                p[0] = (unsigned char)c, p[1] = (unsigned char)(c >> 8), p[2] = (unsigned char)(c >> 16);
            }
        }
        if (compress2(packed, &packed_length, raw, (uLong)(row * height), 9) == Z_OK) {
            static const unsigned char signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
            unsigned char header[13];
            put32(header, width);
            put32(header + 4, height);
            header[8] = 8, header[9] = 2, header[10] = header[11] = header[12] = 0;
            fwrite(signature, 1, 8, f);
            chunk(f, "IHDR", header, 13);
            chunk(f, "IDAT", packed, (uint32_t)packed_length);
            chunk(f, "IEND", NULL, 0);
            rc = 0;
        }
    }
    if (f)
        fclose(f);
    free(raw);
    free(packed);
    return rc;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: mgba_run ROM.gba [MAX_FRAMES] [--frame-at ADDRESS] [--shot FRAME FILE.png]...\n");
        return 2;
    }
    unsigned long max_frames = 2000000ul, frame_at = 0;
    unsigned long shot_frame[MAX_SHOTS];
    const char *shot_path[MAX_SHOTS];
    unsigned shots = 0, next_shot = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--frame-at") && i + 1 < argc)
            frame_at = strtoul(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "--shot") && i + 2 < argc && shots < MAX_SHOTS) {
            shot_frame[shots] = strtoul(argv[++i], NULL, 10);
            shot_path[shots++] = argv[++i];
        } else
            max_frames = strtoul(argv[i], NULL, 10);
    }
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
        if (frame_at && next_shot < shots) {
            unsigned long game = core->busRead32(core, (uint32_t)frame_at);
            while (next_shot < shots && game >= shot_frame[next_shot]) {
                if (write_png(shot_path[next_shot], video, width, height) == 0)
                    printf("mgba_run: game frame %lu as %s\n", game, shot_path[next_shot]);
                next_shot++;
            }
        }
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
