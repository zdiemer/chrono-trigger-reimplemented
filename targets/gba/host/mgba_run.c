/* mgba_run: play a .gba in mGBA's core with no window and no sound, print
 * what the program writes to mGBA's debug log, and stop where it says it is
 * done (targets/gba/system/main.c's "ct_gba: done"), or after a number of the
 * GBA's frames (docs/PLATFORM.md, "The GBA build, as built").
 *
 *   mgba_run ROM.gba [MAX_FRAMES] [--frame-at ADDRESS] [--shot FRAME FILE.png]... [--samples FILE]
 *            [--wav FILE.wav] [--stream ADDRESS FILE.wav]
 *
 * It reports the GBA frames between the program's "ct_gba: playing" and
 * "ct_gba: done", and the processor's cycles a game frame from them (a GBA
 * frame is 280,896 cycles). With --frame-at, the address of the game frame
 * the program is in (main.c's ct_gba_frame), it writes the GBA's picture as
 * a PNG once the game's frame reaches each FRAME. With --samples, it notes
 * where the processor is every SAMPLE_CYCLES cycles while the movie plays
 * (to the driver's report of the frames played) and writes each address it was seen at with its count (run.py's
 * --profile names the functions). With --wav, it writes what mGBA plays
 * while the movie plays, in the GBA's time, gaps and all where the game's
 * frame was late; with --stream and the address of the sound's ring
 * (services/direct_sound.h, ct_gba_sound), every sample the program queued,
 * one after another, as the GBA plays them where the game keeps its time.
 * Nothing goes to a sound device. It is a host tool, built against libmgba
 * and zlib (targets/gba/host/run.py builds it). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include <mgba/core/blip_buf.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/core/timing.h>
#include <mgba/internal/arm/arm.h>

enum { GBA_FRAME_CYCLES = 280896, MAX_SHOTS = 32 };

static struct mCore *core;
static int done, playing_at = -1, done_at = -1, played;
static unsigned long game_frames;

/* The sampler: a count for each halfword of the cartridge's code, fast RAM
 * and work RAM, every SAMPLE_CYCLES cycles (prime, so it doesn't beat with
 * the picture's lines). */
enum { SAMPLE_CYCLES = 997, ROM_HALVES = 1 << 24, IWRAM_HALVES = 1 << 14, EWRAM_HALVES = 1 << 17 };
static uint32_t *rom_samples, *iwram_samples, *ewram_samples;
static unsigned long other_samples;
static struct mTimingEvent sampling;

static void sample(struct mTiming *timing, void *context, uint32_t late)
{
    (void)context;
    if (playing_at >= 0 && !played) {
        uint32_t pc = (uint32_t)((struct ARMCore *)core->cpu)->gprs[ARM_PC];
        switch (pc >> 24) {
        case 8: case 9: rom_samples[(pc - 0x08000000u) / 2 % ROM_HALVES]++; break;
        case 3: iwram_samples[pc % 0x8000u / 2]++; break;
        case 2: ewram_samples[pc % 0x40000u / 2]++; break;
        default: other_samples++; break;
        }
    }
    mTimingSchedule(timing, &sampling, SAMPLE_CYCLES - (int32_t)late);
}

static void write_region(FILE *f, const uint32_t *counts, uint32_t halves, uint32_t base)
{
    for (uint32_t i = 0; i < halves; i++)
        if (counts[i])
            fprintf(f, "%08x %u\n", (unsigned)(base + 2 * i), (unsigned)counts[i]);
}

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
    } else if (strstr(text, " lag frame(s)")) {
        played = 1;
    } else if (!strcmp(text, "ct_gba: done") || !strcmp(text, "ct_gba: stopped")) {
        done_at = (int)core->frameCounter(core);
        done = 1;
    }
}

/* A WAV file of 16-bit stereo, its lengths written when it is closed. */
typedef struct {
    FILE *f;
    uint32_t frames;
} Wav;

static void put_le(FILE *f, uint32_t v, int bytes)
{
    for (int i = 0; i < bytes; i++)
        fputc((int)(v >> (8 * i)) & 0xFF, f);
}

static void wav_head(FILE *f, uint32_t rate, uint32_t frames)
{
    fwrite("RIFF", 1, 4, f), put_le(f, 36 + frames * 4, 4), fwrite("WAVEfmt ", 1, 8, f);
    put_le(f, 16, 4), put_le(f, 1, 2), put_le(f, 2, 2), put_le(f, rate, 4), put_le(f, rate * 4, 4);
    put_le(f, 4, 2), put_le(f, 16, 2), fwrite("data", 1, 4, f), put_le(f, frames * 4, 4);
}

static int wav_open(Wav *w, const char *path, uint32_t rate)
{
    w->f = fopen(path, "wb");
    w->frames = 0;
    if (w->f)
        wav_head(w->f, rate, 0);
    return w->f ? 0 : -1;
}

static void wav_put(Wav *w, int16_t left, int16_t right)
{
    put_le(w->f, (uint16_t)left, 2), put_le(w->f, (uint16_t)right, 2);
    w->frames++;
}

static void wav_close(Wav *w, uint32_t rate)
{
    if (!w->f)
        return;
    fseek(w->f, 0, SEEK_SET);
    wav_head(w->f, rate, w->frames);
    fclose(w->f);
}

/* The sound's ring as services/direct_sound.h lays it out, and its rate. */
enum { RING = 1024, RING_LEFT = 12, RING_RIGHT = RING_LEFT + RING, STREAM_RATE = 13379, WAV_RATE = 32768 };

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
        fprintf(stderr, "usage: mgba_run ROM.gba [MAX_FRAMES] [--frame-at ADDRESS] [--shot FRAME FILE.png]... "
                        "[--samples FILE]\n");
        return 2;
    }
    unsigned long max_frames = 2000000ul, frame_at = 0;
    unsigned long shot_frame[MAX_SHOTS];
    const char *shot_path[MAX_SHOTS];
    const char *samples_path = NULL, *wav_path = NULL, *stream_path = NULL;
    unsigned long stream_at = 0;
    unsigned shots = 0, next_shot = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--frame-at") && i + 1 < argc)
            frame_at = strtoul(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "--samples") && i + 1 < argc)
            samples_path = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc)
            wav_path = argv[++i];
        else if (!strcmp(argv[i], "--stream") && i + 2 < argc) {
            stream_at = strtoul(argv[++i], NULL, 16);
            stream_path = argv[++i];
        }
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
    if (samples_path) {
        rom_samples = calloc(ROM_HALVES, sizeof *rom_samples);
        iwram_samples = calloc(IWRAM_HALVES, sizeof *iwram_samples);
        ewram_samples = calloc(EWRAM_HALVES, sizeof *ewram_samples);
        if (!rom_samples || !iwram_samples || !ewram_samples) {
            fprintf(stderr, "mgba_run: no memory for the samples\n");
            return 2;
        }
        sampling = (struct mTimingEvent){ .callback = sample, .name = "mgba_run samples", .priority = 0x100 };
        mTimingSchedule(core->timing, &sampling, SAMPLE_CYCLES);
    }
    Wav wav = { 0 }, stream = { 0 };
    struct blip_t *left = core->getAudioChannel(core, 0), *right = core->getAudioChannel(core, 1);
    if (wav_path) {
        blip_set_rates(left, core->frequency(core), WAV_RATE);
        blip_set_rates(right, core->frequency(core), WAV_RATE);
        if (wav_open(&wav, wav_path, WAV_RATE)) {
            fprintf(stderr, "mgba_run: cannot write %s\n", wav_path);
            return 2;
        }
    }
    if (stream_path && wav_open(&stream, stream_path, STREAM_RATE)) {
        fprintf(stderr, "mgba_run: cannot write %s\n", stream_path);
        return 2;
    }
    uint32_t streamed = 0;
    unsigned long frames = 0;
    while (!done && frames < max_frames) {
        core->runFrame(core);
        frames++;
        if (wav_path) {
            int16_t l[2048], r[2048];
            int n = blip_samples_avail(left);
            n = n < 2048 ? n : 2048;
            blip_read_samples(left, l, n, 0);
            blip_read_samples(right, r, n, 0);
            if (playing_at >= 0 && !played)
                for (int k = 0; k < n; k++)
                    wav_put(&wav, l[k], r[k]);
        }
        if (stream_path) {
            uint32_t queued = core->busRead32(core, (uint32_t)stream_at);
            for (; streamed != queued; streamed++) {
                int8_t l = (int8_t)core->busRead8(core, (uint32_t)(stream_at + RING_LEFT + streamed % RING));
                int8_t r = (int8_t)core->busRead8(core, (uint32_t)(stream_at + RING_RIGHT + streamed % RING));
                wav_put(&stream, (int16_t)(l * 256), (int16_t)(r * 256));
            }
        }
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
    if (samples_path) {
        FILE *f = fopen(samples_path, "w");
        if (!f) {
            fprintf(stderr, "mgba_run: cannot write %s\n", samples_path);
            rc = 2;
        } else {
            write_region(f, rom_samples, ROM_HALVES, 0x08000000u);
            write_region(f, iwram_samples, IWRAM_HALVES, 0x03000000u);
            write_region(f, ewram_samples, EWRAM_HALVES, 0x02000000u);
            fprintf(f, "other %lu\n", other_samples);
            fclose(f);
        }
    }
    wav_close(&wav, WAV_RATE);
    wav_close(&stream, STREAM_RATE);
    core->deinit(core);
    free(video);
    return rc;
}
