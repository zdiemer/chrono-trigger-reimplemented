/* mgba_run: play the GBA build (ct.gba) in mGBA's core with no window and
 * nothing played aloud, driving it from outside as `ct_native --drive` is driven
 * (docs/PLATFORM.md, "The GBA build, as built"; targets/gba/README.md).
 *
 *   mgba_run ROM.gba [--sav FILE.sav] --movie MOVIE.ctm --frames N [--shot FRAME FILE.png]... [--samples FILE]
 *            [--wav FILE.wav] [--stream FILE.wav] [--watch FROM TO DIR]
 *   mgba_run ROM.gba [--sav FILE.sav] --drive
 *
 * The program names its drive box in its debug log as it starts
 * (targets/gba/system/drive_box.h); the runner arms it then, and from then on
 * puts each frame's pads in it, from the movie or from the commands on stdin.
 * --sav is the cartridge's SRAM to start from, held in memory and never
 * written back.
 *
 * With --movie, it plays the movie's first N frames, writes the picture as a
 * PNG once each FRAME is played, asks the program for its report and prints
 * it with the GBA frames the movie took. With --samples, it notes where the
 * processor is every SAMPLE_CYCLES cycles as the movie plays and writes each
 * address it was seen at with its count (run.py's --profile names the
 * functions). With --wav, it writes what mGBA plays as the movie plays, in
 * the GBA's time, gaps and all where the game's frame was late; with
 * --stream, every sample the program queued (the sound's ring, which it
 * names in its log, services/direct_sound.h), one after another, as the GBA
 * plays them where the game keeps its time. With --watch, it writes every
 * picture the GBA shows while it plays the game's frames FROM to TO, as
 * DIR/wGBAFRAME_GAMEFRAME.png (the GBA's frame, and the game's frame it had
 * taken), at the GBA's own speed: what a player sees, the picture's work
 * running past the vertical blank and all, where --shot waits for each
 * frame's picture to be finished.
 *
 * With --drive, a command a line on stdin and a reply a line on stdout, as
 * ct_native's (docs/MOVIES.md, "Routes as a test"); the program's own lines
 * go to stderr:
 *   run N PAD [PAD2]              N frames with the pads held  -> ok FRAMES
 *   movie PATH TO                 the movie's frames up to TO  -> ok FRAMES
 *   peek ADDRESS LENGTH           the game's memory, in hex    -> ok HEX
 *   wram PATH                     work RAM, 128 KiB, to a file -> ok
 *   shot PATH                     the GBA's picture as a PNG   -> ok
 *   end                           the program's report         -> ok FRAMES END_CRC SAVE_RAM_CRC
 *   save SLOT, load SLOT          the whole machine, in memory -> ok FRAMES
 *   savefile PATH, loadfile PATH  the same, in a file          -> ok FRAMES
 *   sav PATH                      the cartridge's SRAM, a .sav -> ok
 *   frame                         -> ok FRAMES
 *   quit                          -> ok
 * The CRCs are as `ct_native --end-state` writes the state: of work RAM and
 * save RAM, and of save RAM alone. It is a host tool, built against libmgba
 * and zlib with frontend/movie.c (targets/gba/host/run.py builds it). */
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include <mgba/core/blip_buf.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/core/timing.h>
#include <mgba/internal/arm/arm.h>
#include <mgba-util/vfs.h>

#include "movie.h"
#include "system/drive_box.h"

enum {
    GBA_FRAME_CYCLES = 280896,
    MAX_SHOTS = 32,
    SLOTS = 256,
    STALL = 20000,            /* GBA frames with no answer before the run is given up */
    WORK_RAM = 0x7E0000, WORK_RAM_BYTES = 0x20000,
};
#define GBA_SRAM 0x0E000000u
enum { GBA_SRAM_BYTES = 0x8000 };

static struct mCore *core;
static FILE *log_out;
static uint32_t box;                       /* the drive box's address; 0 until named */
static uint32_t ring;                      /* the sound's ring's address; 0 until named */
static uint32_t ring_samples = 1, ring_left, ring_right;   /* its length, and where each side starts in it */
static int reported, sampling;
static char end_crcs[40];                  /* the report's "END SAVE" */
static unsigned width, height;
static color_t *video;

/* The sampler: a count for each halfword of the cartridge's code, fast RAM
 * and work RAM, every SAMPLE_CYCLES cycles (prime, so it doesn't beat with
 * the picture's lines). */
enum { SAMPLE_CYCLES = 997, ROM_HALVES = 1 << 24, IWRAM_HALVES = 1 << 14, EWRAM_HALVES = 1 << 17 };
static uint32_t *rom_samples, *iwram_samples, *ewram_samples;
static unsigned long other_samples;
static struct mTimingEvent sampling_event;

static void sample(struct mTiming *timing, void *context, uint32_t late)
{
    (void)context;
    if (sampling) {
        uint32_t pc = (uint32_t)((struct ARMCore *)core->cpu)->gprs[ARM_PC];
        switch (pc >> 24) {
        case 8: case 9: rom_samples[(pc - 0x08000000u) / 2 % ROM_HALVES]++; break;
        case 3: iwram_samples[pc % 0x8000u / 2]++; break;
        case 2: ewram_samples[pc % 0x40000u / 2]++; break;
        default: other_samples++; break;
        }
    }
    mTimingSchedule(timing, &sampling_event, SAMPLE_CYCLES - (int32_t)late);
}

static void write_region(FILE *f, const uint32_t *counts, uint32_t halves, uint32_t base)
{
    for (uint32_t i = 0; i < halves; i++)
        if (counts[i])
            fprintf(f, "%08x %u\n", (unsigned)(base + 2 * i), (unsigned)counts[i]);
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


static void log_line(struct mLogger *logger, int category, enum mLogLevel level, const char *format, va_list args)
{
    (void)logger;
    const char *id = mLogCategoryId(category);
    if (!id || strcmp(id, "gba.debug") != 0) {
        if (level & (mLOG_FATAL | mLOG_ERROR)) {
            fprintf(log_out, "mgba: %s: ", mLogCategoryName(category));
            vfprintf(log_out, format, args);
            fputc('\n', log_out);
        }
        return;
    }
    char text[512];
    vsnprintf(text, sizeof text, format, args);
    fprintf(log_out, "%s\n", text);
    fflush(log_out);
    unsigned at, end, sram;
    unsigned samples, left, right;
    if (sscanf(text, "ct_gba: sound ring at %8X, %u samples, left at +%u, right at +%u", &at, &samples, &left,
               &right) == 4)
        ring = at, ring_samples = samples, ring_left = left, ring_right = right;
    if (sscanf(text, "ct_gba: drive box at %8X", &at) == 1) {
        /* armed before the program looks, as it logs the line */
        box = at;
        core->busWrite32(core, box + (uint32_t)offsetof(DriveBox, armed), DRIVE_ARMED);
    } else if (sscanf(text, "ct_gba: end state crc32 %8X, save RAM crc32 %8X", &end, &sram) == 2) {
        snprintf(end_crcs, sizeof end_crcs, "%08X %08X", end, sram);
    } else if (!strcmp(text, "ct_gba: reported")) {
        reported = 1;
    }
}

static uint32_t box_get(size_t field) { return core->busRead32(core, box + (uint32_t)field); }
static void box_put(size_t field, uint32_t v) { core->busWrite32(core, box + (uint32_t)field, v); }
static uint32_t frames_played(void) { return box_get(offsetof(DriveBox, taken)); }

/* ---- The sound, recorded ----------------------------------------------------------------- */

/* A WAV file of 16-bit stereo, its lengths written when it is closed. */
typedef struct {
    FILE *f;
    uint32_t rate, frames;
} Wav;

static void put_le(FILE *f, uint32_t v, int bytes)
{
    for (int i = 0; i < bytes; i++)
        fputc((int)(v >> (8 * i)) & 0xFF, f);
}

static void wav_head(const Wav *w)
{
    fwrite("RIFF", 1, 4, w->f), put_le(w->f, 36 + w->frames * 4, 4), fwrite("WAVEfmt ", 1, 8, w->f);
    put_le(w->f, 16, 4), put_le(w->f, 1, 2), put_le(w->f, 2, 2), put_le(w->f, w->rate, 4);
    put_le(w->f, w->rate * 4, 4), put_le(w->f, 4, 2), put_le(w->f, 16, 2);
    fwrite("data", 1, 4, w->f), put_le(w->f, w->frames * 4, 4);
}

static int wav_open(Wav *w, const char *path, uint32_t rate)
{
    *w = (Wav){ fopen(path, "wb"), rate, 0 };
    if (w->f)
        wav_head(w);
    return w->f ? 0 : -1;
}

static void wav_put(Wav *w, int16_t left, int16_t right)
{
    put_le(w->f, (uint16_t)left, 2), put_le(w->f, (uint16_t)right, 2);
    w->frames++;
}

static int wav_close(Wav *w)
{
    if (!w->f)
        return 0;
    fseek(w->f, 0, SEEK_SET);
    wav_head(w);
    return fclose(w->f) ? -1 : 0;
}

/* The ring's rate (services/direct_sound.h). */
enum { STREAM_RATE = 13379, WAV_RATE = 32768 };

static Wav heard, streamed;
static int recording;
static uint32_t stream_taken;

static void record_frame(void)
{
    if (heard.f) {
        struct blip_t *left = core->getAudioChannel(core, 0), *right = core->getAudioChannel(core, 1);
        int16_t l[2048], r[2048];
        int n = blip_samples_avail(left);
        n = n < 2048 ? n : 2048;
        blip_read_samples(left, l, n, 0);
        blip_read_samples(right, r, n, 0);
        for (int k = 0; recording && k < n; k++)
            wav_put(&heard, l[k], r[k]);
    }
    if (streamed.f && ring) {
        uint32_t queued = core->busRead32(core, ring);
        for (; stream_taken != queued; stream_taken++) {
            int8_t l = (int8_t)core->busRead8(core, ring + ring_left + stream_taken % ring_samples);
            int8_t r = (int8_t)core->busRead8(core, ring + ring_right + stream_taken % ring_samples);
            if (recording)
                wav_put(&streamed, (int16_t)(l * 256), (int16_t)(r * 256));
        }
    }
}

static int start_recording(const char *wav_path, const char *stream_path)
{
    if (wav_path) {
        blip_set_rates(core->getAudioChannel(core, 0), core->frequency(core), WAV_RATE);
        blip_set_rates(core->getAudioChannel(core, 1), core->frequency(core), WAV_RATE);
        if (wav_open(&heard, wav_path, WAV_RATE))
            return -1;
    }
    return stream_path && wav_open(&streamed, stream_path, STREAM_RATE) ? -1 : 0;
}

static unsigned long gba_frames;
static unsigned long watch_from, watch_to;
static const char *watch_dir;

static void gba_frame(void)
{
    core->runFrame(core);
    gba_frames++;
    record_frame();
    uint32_t taken = watch_dir ? frames_played() : 0;
    if (watch_dir && taken >= watch_from && taken <= watch_to) {
        char path[1024];
        snprintf(path, sizeof path, "%s/w%07lu_%06lu.png", watch_dir, gba_frames, (unsigned long)taken);
        write_png(path, video, width, height);
    }
}

/* The program between frames with `frame` played and nothing asked of it. */
static int settled(uint32_t frame)
{
    return box_get(offsetof(DriveBox, waiting)) && box_get(offsetof(DriveBox, taken)) == frame &&
           box_get(offsetof(DriveBox, command)) == DRIVE_IDLE;
}

typedef uint32_t PadsAt(const void *ctx, uint32_t frame);
static uint32_t held(const void *ctx, uint32_t frame)
{
    (void)frame;
    return *(const uint32_t *)ctx;
}
static uint32_t from_movie(const void *ctx, uint32_t frame)
{
    return (uint32_t)ct_movie_buttons(ctx, frame) | (uint32_t)ct_movie_buttons2(ctx, frame) << 16;
}

/* Play up to frame `to`, each frame's pads put in the ring ahead of it. */
static int play_to(uint32_t to, PadsAt *pads, const void *ctx)
{
    unsigned long idle = 0;
    uint32_t written = frames_played();
    while (!settled(to)) {
        uint32_t taken = frames_played(), was = written;
        while (written < to && written - taken < DRIVE_RING) {
            box_put(offsetof(DriveBox, pads) + 4 * (written % DRIVE_RING), pads(ctx, written));
            written++;
        }
        if (written != was)
            box_put(offsetof(DriveBox, written), written);
        gba_frame();
        idle = frames_played() == taken ? idle + 1 : 0;
        if (idle > STALL)
            return -1;
    }
    return 0;
}

static int ask(uint32_t command, uint32_t address, uint32_t length)
{
    box_put(offsetof(DriveBox, address), address);
    box_put(offsetof(DriveBox, length), length);
    box_put(offsetof(DriveBox, command), command);
    for (unsigned long n = 0; box_get(offsetof(DriveBox, command)) != DRIVE_IDLE; n++) {
        if (n > STALL)
            return -1;
        gba_frame();
    }
    return 0;
}

/* The game's memory from a bus address, as ct_native's peek reads it. */
static int read_game(uint32_t address, uint8_t *out, uint32_t length)
{
    while (length) {
        uint32_t part = length < DRIVE_BYTES ? length : DRIVE_BYTES;
        if (ask(DRIVE_READ, address, part))
            return -1;
        for (uint32_t i = 0; i < part; i++)
            out[i] = (uint8_t)core->busRead8(core, box + (uint32_t)offsetof(DriveBox, bytes) + i);
        address += part, out += part, length -= part;
    }
    return 0;
}

static int report(void)
{
    reported = 0;
    end_crcs[0] = 0;
    sampling = 0;
    return ask(DRIVE_REPORT, 0, 0) || !reported || !end_crcs[0] ? -1 : 0;
}

/* The picture the program presented last, which the GBA's next frame shows. */
static int shot(const char *path)
{
    gba_frame();
    return write_png(path, video, width, height);
}

static void *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *bytes = n > 0 ? malloc((size_t)n) : NULL;
    if (bytes && fread(bytes, 1, (size_t)n, f) != (size_t)n) {
        free(bytes);
        bytes = NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return bytes;
}

static int write_file(const char *path, const void *bytes, size_t size)
{
    FILE *f = fopen(path, "wb");
    int ok = f && fwrite(bytes, 1, size, f) == size;
    if (f && fclose(f))
        ok = 0;
    return ok ? 0 : -1;
}

/* ---- --drive -------------------------------------------------------------------------------- */

static int number(const char *word, int base, unsigned long *out)
{
    char *end;
    if (!word || !*word || *word == '-')
        return 0;
    *out = strtoul(word, &end, base);
    return !*end;
}

static void *slot[SLOTS];

static int save_state(void **out)
{
    size_t size = core->stateSize(core);
    void *bytes = realloc(*out, size);
    if (!bytes)
        return -1;
    *out = bytes;
    return core->saveState(core, bytes) ? 0 : -1;
}

static void drive(void)
{
    static char line[8192];
    static uint8_t ram[WORK_RAM_BYTES];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        char *cmd = strtok(line, " \t"), *path = cmd ? strtok(NULL, "") : NULL;   /* a path is the rest of the line */
        while (path && (*path == ' ' || *path == '\t'))
            path++;
        char words[256] = "";
        if (path)
            snprintf(words, sizeof words, "%s", path);
        char *a = strtok(words, " \t"), *b = a ? strtok(NULL, " \t") : NULL;
        char *c = b ? strtok(NULL, " \t") : NULL, *extra = c ? strtok(NULL, " \t") : NULL;
        unsigned long n, pad, pad2 = 0;
        if (!cmd)
            printf("err command\n");
        else if (!strcmp(cmd, "run") && !extra && number(a, 10, &n) && number(b, 10, &pad) && pad < 0x10000 &&
                 (!c || (number(c, 10, &pad2) && pad2 < 0x10000))) {
            uint32_t pads = (uint32_t)(pad | pad2 << 16);
            if (play_to(frames_played() + (uint32_t)n, held, &pads))
                printf("err frame %u\n", frames_played());
            else
                printf("ok %u\n", frames_played());
        } else if (!strcmp(cmd, "movie") && path && *path) {
            char *to = strrchr(path, ' ');
            ct_movie movie;
            int ok = to && number(to + 1, 10, &n) && n >= frames_played();
            if (to)
                *to = 0;
            ok = ok && ct_movie_load(&movie, path) == 0;
            if (ok) {
                ok = play_to((uint32_t)n, from_movie, &movie) == 0;
                ct_movie_free(&movie);
            }
            printf(ok ? "ok %u\n" : "err movie %u\n", frames_played());
        } else if (!strcmp(cmd, "peek") && !c && number(a, 16, &n) && number(b, 10, &pad) && pad <= WORK_RAM_BYTES) {
            if (read_game((uint32_t)n, ram, (uint32_t)pad))
                printf("err peek\n");
            else {
                printf("ok ");
                for (unsigned long i = 0; i < pad; i++)
                    printf("%02x", ram[i]);
                printf("\n");
            }
        } else if (!strcmp(cmd, "wram") && path && *path) {
            int ok = read_game(WORK_RAM, ram, WORK_RAM_BYTES) == 0 && write_file(path, ram, sizeof ram) == 0;
            printf(ok ? "ok\n" : "err file\n");
        } else if (!strcmp(cmd, "shot") && path && *path) {
            printf(shot(path) ? "err file\n" : "ok\n");
        } else if (!strcmp(cmd, "end") && !a) {
            if (report())
                printf("err report\n");
            else
                printf("ok %u %s\n", frames_played(), end_crcs);
        } else if ((!strcmp(cmd, "save") || !strcmp(cmd, "load")) && !b && number(a, 10, &n) && n < SLOTS) {
            if (!strcmp(cmd, "save"))
                printf(save_state(&slot[n]) ? "err memory\n" : "ok %u\n", frames_played());
            else if (!slot[n])
                printf("err empty\n");
            else
                printf(core->loadState(core, slot[n]) ? "ok %u\n" : "err restore\n", frames_played());
        } else if (!strcmp(cmd, "savefile") && path && *path) {
            void *bytes = NULL;
            int ok = save_state(&bytes) == 0 && write_file(path, bytes, core->stateSize(core)) == 0;
            free(bytes);
            printf(ok ? "ok %u\n" : "err file\n", frames_played());
        } else if (!strcmp(cmd, "loadfile") && path && *path) {
            size_t size = 0;
            void *bytes = read_file(path, &size);
            int ok = bytes && size == core->stateSize(core) && core->loadState(core, bytes);
            free(bytes);
            printf(ok ? "ok %u\n" : "err file\n", frames_played());
        } else if (!strcmp(cmd, "sav") && path && *path) {
            static uint8_t sram[GBA_SRAM_BYTES];
            for (uint32_t i = 0; i < GBA_SRAM_BYTES; i++)
                sram[i] = (uint8_t)core->busRead8(core, GBA_SRAM + i);
            printf(write_file(path, sram, sizeof sram) ? "err file\n" : "ok\n");
        } else if (!strcmp(cmd, "frame") && !a)
            printf("ok %u\n", frames_played());
        else if (!strcmp(cmd, "quit") && !a) {
            printf("ok\n");
            fflush(stdout);
            break;
        } else
            printf("err command\n");
        fflush(stdout);
    }
    for (unsigned i = 0; i < SLOTS; i++)
        free(slot[i]);
}

/* ---- --movie -------------------------------------------------------------------------------- */

typedef struct {
    unsigned long frame;
    const char *path;
} Shot;

static int by_frame(const void *a, const void *b)
{
    unsigned long x = ((const Shot *)a)->frame, y = ((const Shot *)b)->frame;
    return x < y ? -1 : x > y;
}

static int start_samples(void)
{
    rom_samples = calloc(ROM_HALVES, sizeof *rom_samples);
    iwram_samples = calloc(IWRAM_HALVES, sizeof *iwram_samples);
    ewram_samples = calloc(EWRAM_HALVES, sizeof *ewram_samples);
    if (!rom_samples || !iwram_samples || !ewram_samples)
        return -1;
    sampling_event = (struct mTimingEvent){ .callback = sample, .name = "mgba_run samples", .priority = 0x100 };
    mTimingSchedule(core->timing, &sampling_event, SAMPLE_CYCLES);
    return 0;
}

static int write_samples(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    write_region(f, rom_samples, ROM_HALVES, 0x08000000u);
    write_region(f, iwram_samples, IWRAM_HALVES, 0x03000000u);
    write_region(f, ewram_samples, EWRAM_HALVES, 0x02000000u);
    fprintf(f, "other %lu\n", other_samples);
    return fclose(f) ? -1 : 0;
}

/* The program's cartridge with a movie's decisions put far past its end
 * (system/drive_box.h), for the program to replay. */
static int load_with_decisions(const char *rom_path, const char *decisions_path)
{
    size_t rom_size, size;
    uint8_t *rom = read_file(rom_path, &rom_size);
    uint8_t *decisions = read_file(decisions_path, &size);
    uint32_t at = DRIVE_DECISIONS_AT - 0x08000000u;
    uint8_t *image = rom && decisions && rom_size <= at ? calloc(1, at + size) : NULL;
    if (image) {
        memcpy(image, rom, rom_size);
        memcpy(image + at, decisions, size);
    }
    free(rom);
    free(decisions);
    struct VFile *vf = image ? VFileFromMemory(image, at + size) : NULL;   /* kept: the core reads it as it plays */
    if (!vf || !core->loadROM(core, vf))
        return -1;
    return 0;
}

static int play_movie(const char *path, unsigned long frames, Shot *shots, unsigned count, const char *samples_path,
                      const char *wav_path, const char *stream_path)
{
    ct_movie movie;
    if (ct_movie_load(&movie, path))
        return 2;
    if (samples_path && start_samples()) {
        fprintf(stderr, "mgba_run: no memory for the samples\n");
        return 2;
    }
    if (start_recording(wav_path, stream_path)) {
        fprintf(stderr, "mgba_run: cannot write the sound's files\n");
        return 2;
    }
    qsort(shots, count, sizeof *shots, by_frame);
    int rc = 0;
    unsigned long began = gba_frames, shooting = 0;
    sampling = 1;
    recording = 1;
    for (unsigned i = 0; i <= count && !rc; i++) {
        unsigned long to = i < count && shots[i].frame < frames ? shots[i].frame : frames;
        if (play_to((uint32_t)to, from_movie, &movie)) {
            printf("mgba_run: the program stopped taking frames at frame %u\n", frames_played());
            rc = 1;
        } else if (i < count) {
            unsigned long before = gba_frames;
            if (shot(shots[i].path) == 0)
                printf("mgba_run: game frame %lu as %s\n", to, shots[i].path);
            shooting += gba_frames - before;
        }
    }
    recording = 0;
    unsigned long gba = gba_frames - began - shooting;
    ct_movie_free(&movie);
    if (!rc && report()) {
        printf("mgba_run: no report from the program\n");
        rc = 1;
    }
    if (!rc && frames)
        printf("mgba_run: %lu game frames in %lu GBA frames: %.0f cycles a game frame, %.1f GBA frames each\n", frames,
               gba, (double)gba * GBA_FRAME_CYCLES / (double)frames, (double)gba / (double)frames);
    if (wav_close(&heard) || wav_close(&streamed)) {
        fprintf(stderr, "mgba_run: cannot write the sound's files\n");
        rc = 2;
    }
    if (samples_path && write_samples(samples_path)) {
        fprintf(stderr, "mgba_run: cannot write %s\n", samples_path);
        rc = 2;
    }
    return rc;
}

int main(int argc, char **argv)
{
    static const char usage[] = "usage: mgba_run ROM.gba [--sav FILE.sav] (--movie MOVIE.ctm --frames N "
                                "[--shot FRAME FILE.png]... [--samples FILE] [--wav FILE.wav] [--stream FILE.wav] [--decisions FILE] "
                                "[--watch FROM TO DIR] | --drive)\n";
    const char *movie = NULL, *sav = NULL, *samples_path = NULL, *wav_path = NULL, *stream_path = NULL;
    const char *decisions_path = NULL;
    unsigned long frames = 0;
    int driving = 0;
    Shot shots[MAX_SHOTS];
    unsigned count = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--movie") && i + 1 < argc)
            movie = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--sav") && i + 1 < argc)
            sav = argv[++i];
        else if (!strcmp(argv[i], "--samples") && i + 1 < argc)
            samples_path = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc)
            wav_path = argv[++i];
        else if (!strcmp(argv[i], "--stream") && i + 1 < argc)
            stream_path = argv[++i];
        else if (!strcmp(argv[i], "--decisions") && i + 1 < argc)
            decisions_path = argv[++i];
        else if (!strcmp(argv[i], "--drive"))
            driving = 1;
        else if (!strcmp(argv[i], "--watch") && i + 3 < argc) {
            watch_from = strtoul(argv[++i], NULL, 10);
            watch_to = strtoul(argv[++i], NULL, 10);
            watch_dir = argv[++i];
        }
        else if (!strcmp(argv[i], "--shot") && i + 2 < argc && count < MAX_SHOTS) {
            shots[count].frame = strtoul(argv[++i], NULL, 10);
            shots[count++].path = argv[++i];
        } else
            argc = 0;
    }
    if (argc < 2 || !movie == !driving) {
        fputs(usage, stderr);
        return 2;
    }
    log_out = driving ? stderr : stdout;
    static struct mLogger logger = { .log = log_line };
    mLogSetDefaultLogger(&logger);
    core = mCoreFind(argv[1]);
    if (!core || !core->init(core)) {
        fprintf(stderr, "mgba_run: no core for %s\n", argv[1]);
        return 2;
    }
    mCoreInitConfig(core, NULL);
    core->desiredVideoDimensions(core, &width, &height);
    video = calloc((size_t)width * height, sizeof *video);
    core->setVideoBuffer(core, video, width);
    if (decisions_path ? load_with_decisions(argv[1], decisions_path) : !mCoreLoadFile(core, argv[1])) {
        fprintf(stderr, "mgba_run: cannot load %s\n", argv[1]);
        return 2;
    }
    if (sav) {
        size_t size = 0;
        void *bytes = read_file(sav, &size);
        struct VFile *vf = bytes ? VFileMemChunk(bytes, size) : NULL;
        free(bytes);
        if (!vf || !core->loadSave(core, vf)) {
            fprintf(stderr, "mgba_run: cannot load the save %s\n", sav);
            return 2;
        }
    }
    core->reset(core);
    for (unsigned long n = 0; !box; n++) {
        if (n > STALL) {
            fprintf(stderr, "mgba_run: the program never named its drive box\n");
            return 1;
        }
        gba_frame();
    }
    int rc = 0;
    if (driving)
        drive();
    else
        rc = play_movie(movie, frames, shots, count, samples_path, wav_path, stream_path);
    core->deinit(core);
    free(video);
    return rc;
}
