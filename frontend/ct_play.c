/* ct_play: plays the game in a window, with the ported routines running natively
 * and the rest on the development system (docs/PLAYING.md).
 *
 *   ct_play [--rom] <rom> [--scale N] [--fullscreen] [--mute] [--native on|shadow|off] [--recomp only|on|off]
 *           [--audio snes|direct] [--save-dir dir]
 *           [--movie m.ctm [--movie-frames N]] [--record out.ctm] [--sram save.srm] [--load-zst in.zst]
 *   ct_play --rom <rom> --frames N [--screenshot out.png] [--hold BUTTONS@FIRST-LAST]...
 *           [--load-state f] [--save-state f] [--movie m.ctm] [--record out.ctm] [--wav out.wav]
 *
 * With --frames it runs headless (no window) for a fixed number of frames, for
 * checking a build without a display. --movie replays an input movie before the
 * keyboard takes over; --record writes everything played from power-on as a
 * movie that ct_dev replays identically. In a window, the APU's 32000 Hz output
 * plays through SDL audio (see audio_frame).
 *
 * Built with CT_PLAYER_NO_INTERPRETER (CMake CT_PLAYER_INTERPRETER=FALLBACK or
 * OFF, docs/PLAYING.md), it runs only native code, the generated blocks: an
 * instruction without generated code is a miss, logged to <rom name>.misses.txt
 * and run on the interpreter (1, FALLBACK), or, with the interpreter not linked
 * at all (2, OFF), the game stops there.
 */
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "media.h"
#include "movie.h"
#include "platform/audio.h"
#include "port/machine.h"
#include "port/recomp_table.h"
#include "registry.h"
#include "snes/render.h"
#include "snes/system.h"
#include "snes/zst.h"
#include "trace.h"

#ifndef CT_PLAYER_NO_INTERPRETER
#define CT_PLAYER_NO_INTERPRETER 0
#endif
#if CT_PLAYER_NO_INTERPRETER
#include "misses.h"
#endif

/* What this build does with the interpreter, for --help, the title and stderr. */
static const char *const build_mode[] = {
    "with the interpreter (every --native mode)",
    "generated code only, the interpreter as a logged fallback for misses (CT_PLAYER_INTERPRETER=FALLBACK)",
    "generated code only, without the interpreter (CT_PLAYER_INTERPRETER=OFF)",
};
static const char *const title_mode[] = { "", " [generated code, fallback]", " [generated code only]" };

/* Misses reported on stderr as they happen, at most. */
#define MISSES_REPORTED 20

/* NTSC: 21.477272 MHz master clock, 1364 clocks per line, 262 lines per frame. */
#define MASTER_CLOCK_HZ 21477272.0
#define FRAME_RATE (MASTER_CLOCK_HZ / (SNES_CLOCKS_PER_LINE * SNES_LINES_PER_FRAME))

/* Frames between checks for changed SRAM, which is then written out. */
#define SRAM_FLUSH_FRAMES 60
/* Frames between writes of the movie being recorded, so a crash loses little. */
#define RECORD_FLUSH_FRAMES 600
/* How long a quick save waits for native routines suspended in call-outs to
 * finish before giving up (see native_suspended). In the field that's rarely
 * needed; in battle, stretches of several seconds are common. */
#define SAVE_WAIT_FRAMES 1800

/* Keyboard to controller. One key per row; several keys may share a button. */
static const struct {
    SDL_Scancode key;
    const char *key_name;
    uint16_t button;
    const char *button_name;
} keymap[] = {
    { SDL_SCANCODE_UP, "Up", CT_BTN_UP, "Up" },
    { SDL_SCANCODE_DOWN, "Down", CT_BTN_DOWN, "Down" },
    { SDL_SCANCODE_LEFT, "Left", CT_BTN_LEFT, "Left" },
    { SDL_SCANCODE_RIGHT, "Right", CT_BTN_RIGHT, "Right" },
    { SDL_SCANCODE_X, "X", CT_BTN_A, "A" },
    { SDL_SCANCODE_Z, "Z", CT_BTN_B, "B" },
    { SDL_SCANCODE_S, "S", CT_BTN_X, "X" },
    { SDL_SCANCODE_A, "A", CT_BTN_Y, "Y" },
    { SDL_SCANCODE_Q, "Q", CT_BTN_L, "L" },
    { SDL_SCANCODE_W, "W", CT_BTN_R, "R" },
    { SDL_SCANCODE_RETURN, "Enter", CT_BTN_START, "Start" },
    { SDL_SCANCODE_RSHIFT, "Right Shift", CT_BTN_SELECT, "Select" },
};
#define KEYMAP_COUNT (int)(sizeof keymap / sizeof keymap[0])

static const struct {
    const char *name;
    uint16_t button;
} button_names[] = {
    { "B", CT_BTN_B }, { "Y", CT_BTN_Y }, { "SELECT", CT_BTN_SELECT }, { "START", CT_BTN_START },
    { "UP", CT_BTN_UP }, { "DOWN", CT_BTN_DOWN }, { "LEFT", CT_BTN_LEFT }, { "RIGHT", CT_BTN_RIGHT },
    { "A", CT_BTN_A }, { "X", CT_BTN_X }, { "L", CT_BTN_L }, { "R", CT_BTN_R },
};

/* Headless input: buttons held over a range of frames. */
typedef struct {
    uint16_t buttons;
    long first, last;
} Hold;

#define MAX_HOLDS 64

/* Input by frame since power-on: the replayed movie's frames, then whatever was
 * played after them. */
typedef struct {
    uint16_t *pads;
    uint32_t frames, cap;
    uint32_t replay_frames;    /* frames taken from the movie before the keyboard (or --hold) */
    const char *movie_name;    /* file name of the replayed movie, or NULL */
    const char *sram_name;     /* file name of the --sram save, or NULL */
    uint32_t sram_crc;         /* CRC-32 of the SRAM at power-on */
    const char *record_path;   /* movie being recorded, or NULL */
    uint32_t written_frames;   /* frames in the last write of record_path */
    uint32_t state_crc;        /* CRC of the quick save made during this recording */
    bool state_ours;
} Track;

typedef struct {
    Snes snes;
    Renderer renderer;
    NativeRuntime *natives;
    NativeMode native_mode;    /* the mode asked for */
    NativeMode runtime_mode;   /* the mode natives runs in now */
    NativeEntry *table;        /* ported routines, then generated blocks */
    int table_count;
    bool persist_sram;
    char sram_path[1024];
    char state_path[1024];
    uint8_t sram_saved[SNES_SRAM_SIZE];   /* SRAM as last loaded or written */
    Track track;
    bool mute;                            /* no audio device */
    bool watch;                           /* replay --movie at normal speed, with sound */
    const char *wav_path;                 /* headless: write the audio here */
#if CT_PLAYER_NO_INTERPRETER
    Misses *misses;                       /* instructions without generated code, as in misses_path */
    char misses_path[1024];
    unsigned long miss_steps;             /* misses this session */
    unsigned long miss_steps_written;
    char halt_reason[256];                /* why the game stopped (snes.halted) */
#endif
} Player;

static void usage(FILE *out)
{
    fprintf(out,
            "usage: ct_play [--rom] <rom> [options]\n"
            "\n"
            "Options:\n"
            "  --scale N            window scale (integer, default 3)\n"
            "  --fullscreen         start fullscreen\n"
            "  --aspect A           4:3 (default, as on a TV), 8:7 (the SNES pixel aspect) or square\n"
            "  --crop N|T,B         hide rows at the top and bottom as a TV's overscan did (default 8; 0 shows all)\n"
            "  --mute               no sound\n"
            "  --native MODE        on: run ported routines natively (default); off: interpret everything;\n"
            "                       shadow: interpret, and check each ported routine against the original\n"
            "  --recomp MODE        only: in on mode, run the blocks generated from the ROM without the ported\n"
            "                       routines, which keeps the original timing exactly (default); on: generated\n"
            "                       blocks and ported routines (the ported routines' timing is estimated);\n"
            "                       off: no generated blocks\n"
            "  --audio BACKEND      snes: the APU on the machine's timeline (default); direct: the sound driver's\n"
            "                       uploads and commands take no CPU time (APU-direct; needs --native on, and\n"
            "                       runs the ported routines, so --recomp on is implied)\n"
            "  --save-dir DIR       where <rom name>.srm and <rom name>.state go (default: the ROM's directory)\n"
            "\n"
            "Movies:\n"
            "  --movie F.ctm        replay this input movie from power-on, fast, then hand over to the keyboard\n"
            "  --movie-frames N     replay N frames of it (default: up to its last input)\n"
            "  --watch              replay the movie at normal speed with sound instead of fast (Tab still\n"
            "                       fast-forwards); the keyboard takes over when it ends\n"
            "  --record F.ctm       record input from power-on (including the replayed movie) to F.ctm,\n"
            "                       for ct_dev to replay. Starts without the .srm save (or with --sram), runs in\n"
            "                       shadow mode unless --native off is given, and quick saves go to <F>.state\n"
            "  --sram F.srm         start with this cartridge save instead of <rom name>.srm, and don't write it\n"
            "  --load-zst F.zst     start from a ZSNES 1.51 save state (.zst, .zs1-.zs9), including its cartridge\n"
            "                       save, which isn't written back; some of its state is approximated, so it's\n"
            "                       for exploring and coverage, and can't be recorded\n"
            "\n"
            "Headless check (no window):\n"
            "  --frames N           run N frames and exit\n"
            "  --screenshot F.png   write the last frame\n"
            "  --hold BTN[+BTN]@FIRST-LAST   hold buttons over frames FIRST-LAST (repeatable),\n"
            "                       for example --hold START@700-710\n"
            "  --load-state F       start from a state file\n"
            "  --save-state F       write a state file at the end\n"
            "  --wav F.wav          write the sound (32000 Hz stereo)\n"
            "\n"
            "Controls:\n");
    for (int i = 0; i < KEYMAP_COUNT; i++)
        fprintf(out, "  %-12s %s\n", keymap[i].key_name, keymap[i].button_name);
    fprintf(out,
            "  %-12s pause\n"
            "  %-12s fast forward (hold)\n"
            "  %-12s quick save / quick load (same build only)\n"
            "  %-12s toggle fullscreen\n"
            "  %-12s quit\n"
            "\n"
            "Saves are written to <rom name>.srm when the game changes them and on exit.\n",
            "P", "Tab", "F5 / F7", "F11, Alt+Enter", "Esc");
    fprintf(out, "\nThis build runs %s.\n", build_mode[CT_PLAYER_NO_INTERPRETER]);
    if (CT_PLAYER_NO_INTERPRETER)
        fprintf(out,
                "Only --native on works (with --recomp only or on). An instruction nothing was generated for is a\n"
                "miss: it is logged to <rom name>.misses.txt next to the saves, a coverage file to feed back into\n"
                "the build (CT_RECOMP_COVERAGE), and %s.\n",
                CT_PLAYER_NO_INTERPRETER == 1 ? "runs on the interpreter" : "stops the game");
}

/* ---- misses ------------------------------------------------------------------- */

#if CT_PLAYER_NO_INTERPRETER
/* The system found no native code for the instruction at PC. */
static bool on_miss(void *ctx, Snes *snes)
{
    Player *pl = ctx;
    const Cpu *c = &snes->cpu;
    size_t before = misses_count(pl->misses, NULL);
    misses_note(pl->misses, cpu_pc24(c), c->p, c->e);
    pl->miss_steps++;
    char what[200];
    if (CT_PLAYER_NO_INTERPRETER == 2) {
        misses_describe(snes, what, sizeof what);
        snprintf(pl->halt_reason, sizeof pl->halt_reason, "no generated code for %s", what);
        fprintf(stderr, "ct_play: stopped at frame %u: %s. This player has no interpreter to run it; the address is "
                        "logged to %s for the build's CT_RECOMP_COVERAGE.\n",
                (unsigned)snes->frame, what, pl->misses_path);
        return false;
    }
    if (misses_count(pl->misses, NULL) > before && before < MISSES_REPORTED) {
        misses_describe(snes, what, sizeof what);
        fprintf(stderr, "ct_play: frame %u: no generated code for %s; running it on the interpreter (logged to %s)\n",
                (unsigned)snes->frame, what, pl->misses_path);
    }
    return true;
}

/* Write the misses file if this session added misses since it was last written. */
static void misses_flush(Player *pl)
{
    if (pl->miss_steps == pl->miss_steps_written)
        return;
    if (misses_write(pl->misses, pl->misses_path, NULL) != 0) {
        fprintf(stderr, "cannot write %s\n", pl->misses_path);
        return;
    }
    pl->miss_steps_written = pl->miss_steps;
}
#endif

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(n > 0 ? (size_t)n : 1);
    if (n <= 0 || fread(data, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(data);
        return NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return data;
}

static const char *file_name(const char *path)
{
    const char *name = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\')
            name = p + 1;
    }
    return name;
}

/* dir/<name_path's file name without extension><ext>; dir defaults to dir_of's directory. */
static void side_path(char *out, size_t n, const char *name_path, const char *dir_of, const char *dir,
                      const char *ext)
{
    const char *name = file_name(name_path);
    const char *dot = strrchr(name, '.');
    int stem = dot && dot != name ? (int)(dot - name) : (int)strlen(name);
    if (dir)
        snprintf(out, n, "%s/%.*s%s", dir, stem, name, ext);
    else
        snprintf(out, n, "%.*s%.*s%s", (int)(file_name(dir_of) - dir_of), dir_of, stem, name, ext);
}

static int parse_hold(const char *s, Hold *h)
{
    const char *at = strchr(s, '@');
    if (!at)
        return -1;
    h->buttons = 0;
    for (const char *p = s; p < at;) {
        const char *end = p;
        while (end < at && *end != '+')
            end++;
        size_t len = (size_t)(end - p);
        uint16_t bit = 0;
        for (size_t i = 0; i < sizeof button_names / sizeof button_names[0]; i++) {
            if (strlen(button_names[i].name) == len && SDL_strncasecmp(p, button_names[i].name, len) == 0)
                bit = button_names[i].button;
        }
        if (!bit)
            return -1;
        h->buttons |= bit;
        p = end < at ? end + 1 : end;
    }
    char *rest;
    h->first = strtol(at + 1, &rest, 10);
    h->last = h->first;
    if (*rest == '-')
        h->last = strtol(rest + 1, &rest, 10);
    return *rest || h->last < h->first ? -1 : 0;
}

static void on_line(void *ctx, Snes *snes, uint16_t line)
{
    render_line(ctx, &snes->ppu, line);
}

/* ---- saves -------------------------------------------------------------------- */

static void sram_load(Player *pl)
{
    FILE *f = fopen(pl->sram_path, "rb");
    if (f) {
        uint8_t buf[SNES_SRAM_SIZE];
        size_t n = fread(buf, 1, sizeof buf, f);
        fclose(f);
        if (n == sizeof buf)
            memcpy(pl->snes.bus.sram, buf, sizeof buf);
        else
            fprintf(stderr, "ignoring %s: expected %d bytes\n", pl->sram_path, SNES_SRAM_SIZE);
    }
    memcpy(pl->sram_saved, pl->snes.bus.sram, SNES_SRAM_SIZE);
}

/* Write SRAM if it changed since it was last loaded or written. */
static void sram_flush(Player *pl)
{
    if (!pl->persist_sram || !memcmp(pl->sram_saved, pl->snes.bus.sram, SNES_SRAM_SIZE))
        return;
    FILE *f = fopen(pl->sram_path, "wb");
    int ok = f && fwrite(pl->snes.bus.sram, SNES_SRAM_SIZE, 1, f) == 1;
    if (f && fclose(f) != 0)
        ok = 0;
    if (!ok) {
        fprintf(stderr, "cannot write %s\n", pl->sram_path);
        return;
    }
    memcpy(pl->sram_saved, pl->snes.bus.sram, SNES_SRAM_SIZE);
}

/* In shadow mode, print what the runtime found if any routine mismatched. */
static void natives_report(Player *pl)
{
    if (pl->natives && pl->runtime_mode == NATIVE_SHADOW && native_failed(pl->natives)) {
        fprintf(stderr, "shadow mode found mismatches:\n");
        native_report(pl->natives, stderr);
    }
}

static void natives_start(Player *pl, NativeMode mode)
{
    natives_report(pl);
    native_destroy(pl->natives);
    pl->snes.intercept = NULL;
    pl->snes.intercept_ctx = NULL;
    pl->natives = native_create(pl->table, pl->table_count, mode);
    native_set_index(pl->natives, recomp_index, recomp_index_count);   /* used only with generated entries */
    pl->runtime_mode = mode;
    native_attach(pl->natives, &pl->snes);
}

/* Whether a state saved now would lose native routines suspended in call-outs.
 * In shadow mode the machine runs only the original code, so a save just drops
 * comparisons in progress. */
static bool natives_busy(const Player *pl)
{
    return pl->runtime_mode == NATIVE_ON && native_suspended(pl->natives);
}

static uint32_t file_crc(const char *path, bool *ok)
{
    size_t len;
    uint8_t *data = read_file(path, &len);
    *ok = data != NULL;
    uint32_t crc = data ? ct_crc32(data, len) : 0;
    free(data);
    return crc;
}

static int state_save(Player *pl, const char *path)
{
    if (snes_state_save(&pl->snes, path) != 0) {
        fprintf(stderr, "cannot save state %s\n", path);
        return -1;
    }
    if (pl->track.record_path && path == pl->state_path)
        pl->track.state_crc = file_crc(path, &pl->track.state_ours);
    fprintf(stderr, "saved state %s\n", path);
    return 0;
}

/* A state holds only the machine, so native routines suspended in call-outs when
 * it was saved can't resume; the frontend only saves when none are. Loading
 * restarts the native runtime, abandoning any routines suspended now. While
 * recording, only the quick save made during the recording loads, and the
 * recording goes back to its frame. */
static int state_load(Player *pl, const char *path)
{
    Track *t = &pl->track;
    if (t->record_path) {
        bool ok;
        uint32_t crc = file_crc(path, &ok);
        if (!ok || !t->state_ours || crc != t->state_crc) {
            fprintf(stderr, "not loading %s: while recording, only a quick save made in this recording loads\n",
                    path);
            return -1;
        }
    }
    if (snes_state_load(&pl->snes, path) != 0) {
        fprintf(stderr, "cannot load state %s (missing, or saved by a different build)\n", path);
        return -1;
    }
    natives_start(pl, pl->runtime_mode);
    if (t->record_path && pl->snes.frame < t->frames) {
        t->frames = pl->snes.frame;
        if (t->replay_frames > t->frames)
            t->replay_frames = t->frames;
    }
    fprintf(stderr, "loaded state %s\n", path);
    return 0;
}

/* ---- input track ---------------------------------------------------------------- */

static bool replaying(const Player *pl)
{
    return pl->snes.frame < pl->track.replay_frames;
}

/* Buttons for the frame about to run: the track's while replaying, else live. */
static uint16_t next_pad(Player *pl, uint16_t live)
{
    const Track *t = &pl->track;
    uint32_t fr = pl->snes.frame;
    if (fr < t->replay_frames)
        return fr < t->frames ? t->pads[fr] : 0;
    return live;
}

static void track_set(Track *t, uint32_t frame, uint16_t pad)
{
    if (frame >= t->cap) {
        t->cap = frame + 1 > t->cap * 2 ? frame + 1 : t->cap * 2;
        t->pads = realloc(t->pads, t->cap * sizeof *t->pads);
    }
    for (uint32_t fr = t->frames; fr < frame; fr++)
        t->pads[fr] = 0;
    t->pads[frame] = pad;
    if (frame >= t->frames)
        t->frames = frame + 1;
}

static int track_write(Player *pl)
{
    Track *t = &pl->track;
    if (!t->record_path || t->frames == t->written_frames)
        return 0;
    char header[512];
    int n = snprintf(header, sizeof header, "# Recorded with ct_play from power-on");
    if (t->sram_name)
        n += snprintf(header + n, sizeof header - n, " with the save %s", t->sram_name);
    if (t->movie_name)
        n += snprintf(header + n, sizeof header - n, ", continuing %s", t->movie_name);
    n += snprintf(header + n, sizeof header - n, ".\n# frames %lu\n", (unsigned long)t->frames);
    if (t->sram_name)
        n += snprintf(header + n, sizeof header - n, "# sram crc32 %08X\n", (unsigned)t->sram_crc);
    snprintf(header + n, sizeof header - n, "# Replay: ct_dev --rom <rom> --frames %lu --movie %s%s --native shadow\n",
             (unsigned long)t->frames, file_name(t->record_path), t->sram_name ? " --sram <save>" : "");
    if (ct_movie_write(t->record_path, t->pads, t->frames, header) != 0) {
        fprintf(stderr, "cannot write %s\n", t->record_path);
        return -1;
    }
    t->written_frames = t->frames;
    return 0;
}

/* Runs one frame with the given live buttons (used once replay is over). */
static void step(Player *pl, uint16_t live)
{
    Track *t = &pl->track;
    if (!replaying(pl) && pl->runtime_mode != pl->native_mode)
        natives_start(pl, pl->native_mode);
    uint32_t fr = pl->snes.frame;
    uint16_t pad = next_pad(pl, live);
    snes_run_frame(&pl->snes, pad);
    if (t->record_path) {
        track_set(t, fr, pad);
        if (t->frames - t->written_frames >= RECORD_FLUSH_FRAMES)
            track_write(pl);
    }
}

/* ---- audio --------------------------------------------------------------------- */

/* The APU produces 32000 Hz as the emulation runs, paced by video. The device
 * plays at its own rate from its own clock, so samples are resampled into a
 * ring buffer the device callback drains, and the resampling ratio is nudged
 * (at most AUDIO_MAX_ADJUST) to keep the buffer near its target fill instead
 * of dropping or repeating anything. */
#define AUDIO_MAX_ADJUST 0.005
#define AUDIO_RING_FRAMES 16384
#define AUDIO_HISTORY 4096   /* input frames kept for the resampler (> one video frame) */

typedef struct {
    SDL_AudioDeviceID dev;   /* 0: muted or no device */
    int rate;
    bool playing;

    /* Ring buffer shared with the callback (guarded by SDL_LockAudioDevice). */
    int16_t ring[AUDIO_RING_FRAMES * 2];
    size_t head, count;

    /* Resampler input: frames from the APU not yet passed, and the read position. */
    int16_t in[(AUDIO_HISTORY + DSP_OUT_FRAMES) * 2];
    size_t in_count;
    double pos;
    double fill_avg;
    size_t target;
} Audio;

static void SDLCALL audio_callback(void *ctx, Uint8 *stream, int len)
{
    Audio *a = ctx;
    int16_t *out = (int16_t *)(void *)stream;
    size_t frames = (size_t)len / 4;
    size_t n = frames < a->count ? frames : a->count;
    size_t tail = (a->head + AUDIO_RING_FRAMES - a->count) % AUDIO_RING_FRAMES;
    for (size_t i = 0; i < n; i++) {
        size_t at = (tail + i) % AUDIO_RING_FRAMES;
        out[i * 2] = a->ring[at * 2];
        out[i * 2 + 1] = a->ring[at * 2 + 1];
    }
    a->count -= n;
    memset(out + n * 2, 0, (frames - n) * 4);   /* underrun: silence */
}

static void audio_open(Audio *a)
{
    memset(a, 0, sizeof *a);
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "no audio: %s\n", SDL_GetError());
        return;
    }
    SDL_AudioSpec want = { 0 }, have;
    want.freq = DSP_SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_callback;
    want.userdata = a;
    a->dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!a->dev) {
        fprintf(stderr, "no audio: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }
    a->rate = have.freq;
    /* Two device buffers plus about two video frames. */
    a->target = (size_t)have.samples * 2 + (size_t)have.freq / 30;
    if (a->target > AUDIO_RING_FRAMES / 4)
        a->target = AUDIO_RING_FRAMES / 4;
    a->fill_avg = (double)a->target;
    a->pos = 1.0;
}

static void audio_close(Audio *a)
{
    if (!a->dev)
        return;
    SDL_CloseAudioDevice(a->dev);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    a->dev = 0;
}

static void audio_ring_clear(Audio *a)
{
    SDL_LockAudioDevice(a->dev);
    a->count = 0;
    SDL_UnlockAudioDevice(a->dev);
    a->in_count = 0;
    a->pos = 1.0;
    a->fill_avg = (double)a->target;
}

/* Once per loop iteration: move the samples the last frame produced to the
 * device. While paused, the device is stopped and the buffers emptied. */
static void audio_frame(Audio *a, Snes *snes, bool paused)
{
    if (!a->dev || paused) {
        int16_t discard[256 * 2];
        while (au_take_samples(m_platform(snes), discard, 256))
            ;
        if (a->dev) {
            if (a->playing)
                SDL_PauseAudioDevice(a->dev, 1);
            a->playing = false;
            audio_ring_clear(a);
        }
        return;
    }

    a->in_count += au_take_samples(m_platform(snes), a->in + a->in_count * 2,
                                   sizeof a->in / sizeof a->in[0] / 2 - a->in_count);

    SDL_LockAudioDevice(a->dev);
    size_t fill = a->count;
    SDL_UnlockAudioDevice(a->dev);
    a->fill_avg += (fill - a->fill_avg) * 0.05;
    double err = (a->fill_avg - (double)a->target) / (double)a->target;
    err = err < -1 ? -1 : err > 1 ? 1 : err;
    double step = (double)DSP_SAMPLE_RATE / a->rate * (1.0 + AUDIO_MAX_ADJUST * err);

    /* Cubic Hermite over in[i-1..i+2] at pos = i + f. */
    int16_t out[4096 * 2];
    size_t n = 0;
    size_t i;
    while ((i = (size_t)a->pos) + 2 < a->in_count && n < 4096) {
        double f = a->pos - (double)i;
        for (int ch = 0; ch < 2; ch++) {
            double y0 = a->in[(i - 1) * 2 + ch], y1 = a->in[i * 2 + ch];
            double y2 = a->in[(i + 1) * 2 + ch], y3 = a->in[(i + 2) * 2 + ch];
            double c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3;
            double c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
            double v = ((c3 * f + c2) * f + c1) * f + y1;
            out[n * 2 + ch] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        n++;
        a->pos += step;
    }
    /* Keep in[i-1] onward. */
    i = (size_t)a->pos;
    size_t drop = i > 1 ? i - 1 : 0;
    if (drop > a->in_count)
        drop = a->in_count;
    memmove(a->in, a->in + drop * 2, (a->in_count - drop) * 2 * sizeof a->in[0]);
    a->in_count -= drop;
    a->pos -= (double)drop;

    SDL_LockAudioDevice(a->dev);
    /* Past four times the target (a device far slower than its rate), drop
     * rather than let latency grow. */
    for (size_t k = 0; k < n && a->count < 4 * a->target; k++) {
        a->ring[a->head * 2] = out[k * 2];
        a->ring[a->head * 2 + 1] = out[k * 2 + 1];
        a->head = (a->head + 1) % AUDIO_RING_FRAMES;
        a->count++;
    }
    fill = a->count;
    SDL_UnlockAudioDevice(a->dev);

    if (!a->playing && fill >= a->target) {   /* start once there's a cushion */
        SDL_PauseAudioDevice(a->dev, 0);
        a->playing = true;
    }
}

/* ---- headless ------------------------------------------------------------------ */

static int run_headless(Player *pl, long frames, const Hold *holds, int nholds, const char *screenshot,
                        const char *save_state)
{
    static ct_wav wav;
    static int16_t audio[DSP_OUT_FRAMES * 2];
    if (pl->wav_path && ct_wav_open(&wav, pl->wav_path, DSP_SAMPLE_RATE) != 0) {
        fprintf(stderr, "cannot open %s\n", pl->wav_path);
        return 1;
    }
    long frame;
    for (frame = 0; frame < frames && !pl->snes.halted; frame++) {
        uint16_t pad = 0;
        for (int i = 0; i < nholds; i++) {
            if (frame >= holds[i].first && frame <= holds[i].last)
                pad |= holds[i].buttons;
        }
        step(pl, pad);
        ct_wav_write(&wav, audio, au_take_samples(m_platform(&pl->snes), audio, DSP_OUT_FRAMES));
    }
    sram_flush(pl);
    int rc = track_write(pl) != 0;
    if (pl->snes.halted) {
        frames = frame - 1;
        rc = 1;
    }
    if (ct_wav_close(&wav) != 0) {
        fprintf(stderr, "cannot write %s\n", pl->wav_path);
        rc = 1;
    }
    if (screenshot && ct_png_write(screenshot, pl->renderer.pixels, RENDER_WIDTH, RENDER_HEIGHT) != 0) {
        fprintf(stderr, "cannot write %s\n", screenshot);
        rc = 1;
    }
    if (save_state) {
        long waited = 0;
        while (natives_busy(pl) && waited < SAVE_WAIT_FRAMES) {
            snes_run_frame(&pl->snes, 0);
            waited++;
        }
        if (waited)
            fprintf(stderr, "waited %ld frames for native routines to finish\n", waited);
        if (natives_busy(pl)) {
            fprintf(stderr, "cannot save state: native routines stayed suspended\n");
            rc = 1;
        } else if (state_save(pl, save_state) != 0) {
            rc = 1;
        }
    }
    fprintf(stderr, "ran %ld frames (frame counter %u)\n", frames, (unsigned)pl->snes.frame);
    return rc;
}

/* ---- window -------------------------------------------------------------------- */

static uint16_t read_pad(void)
{
    const Uint8 *keys = SDL_GetKeyboardState(NULL);
    uint16_t pad = 0;
    for (int i = 0; i < KEYMAP_COUNT; i++) {
        if (keys[keymap[i].key])
            pad |= keymap[i].button;
    }
    /* Alt+Enter toggles fullscreen; don't also press what Enter is mapped to. */
    if (SDL_GetModState() & KMOD_ALT) {
        for (int i = 0; i < KEYMAP_COUNT; i++) {
            if (keymap[i].key == SDL_SCANCODE_RETURN)
                pad &= (uint16_t)~keymap[i].button;
        }
    }
    /* A real d-pad can't press opposite directions together, and games don't expect it. */
    if ((pad & (CT_BTN_UP | CT_BTN_DOWN)) == (CT_BTN_UP | CT_BTN_DOWN))
        pad &= (uint16_t)~(CT_BTN_UP | CT_BTN_DOWN);
    if ((pad & (CT_BTN_LEFT | CT_BTN_RIGHT)) == (CT_BTN_LEFT | CT_BTN_RIGHT))
        pad &= (uint16_t)~(CT_BTN_LEFT | CT_BTN_RIGHT);
    return pad;
}

static const char *halt_reason(const Player *pl)
{
#if CT_PLAYER_NO_INTERPRETER
    return pl->halt_reason;
#else
    (void)pl;
    return "an instruction without native code";
#endif
}

static const char *misses_path(const Player *pl)
{
#if CT_PLAYER_NO_INTERPRETER
    return pl->misses_path;
#else
    (void)pl;
    return "the miss report";
#endif
}

static void update_title(SDL_Window *window, const Player *pl, bool paused, bool saving)
{
    char title[400];
    int n = snprintf(title, sizeof title, "ct_play%s", title_mode[CT_PLAYER_NO_INTERPRETER]);
#if CT_PLAYER_NO_INTERPRETER
    if (pl->snes.halted)
        n += snprintf(title + n, sizeof title - n, " (stopped: %s)", pl->halt_reason);
    else if (pl->miss_steps)
        n += snprintf(title + n, sizeof title - n, " (%lu misses)", pl->miss_steps);
#endif
    if (replaying(pl))
        n += snprintf(title + n, sizeof title - n, " (replaying %u/%u)", (unsigned)pl->snes.frame,
                      (unsigned)pl->track.replay_frames);
    else if (pl->track.record_path)
        n += snprintf(title + n, sizeof title - n, " (recording, frame %u)", (unsigned)pl->snes.frame);
    if (paused)
        n += snprintf(title + n, sizeof title - n, " (paused)");
    if (saving)
        n += snprintf(title + n, sizeof title - n, " (saving state...)");
    if (pl->runtime_mode == NATIVE_SHADOW && native_failed(pl->natives))
        snprintf(title + n, sizeof title - n, " [shadow mismatch]");
    SDL_SetWindowTitle(window, title);
}

/* How the 256x224 picture is shown: rows cropped at the top and bottom (TVs
 * hid some of them as overscan; Chrono Trigger leaves its bottom rows black),
 * and the width a row is stretched to relative to its height. */
typedef struct {
    int crop_top, crop_bottom;
    double pixel_aspect;   /* displayed pixel width / height */
} Display;

static int display_rows(const Display *d) { return RENDER_HEIGHT - d->crop_top - d->crop_bottom; }

/* The destination rectangle for the picture in an output of w x h: as large as
 * fits with the right aspect, a whole number of output rows per picture row
 * where the output is tall enough, centered. */
static SDL_Rect display_rect(const Display *d, int w, int h)
{
    int rows = display_rows(d);
    double pic_w = RENDER_WIDTH * d->pixel_aspect, pic_h = rows;
    double scale = w / pic_w < h / pic_h ? w / pic_w : h / pic_h;
    if (scale >= 1.0)
        scale = (double)(int)scale;
    SDL_Rect r;
    r.w = (int)(pic_w * scale + 0.5);
    r.h = (int)(pic_h * scale + 0.5);
    r.x = (w - r.w) / 2;
    r.y = (h - r.h) / 2;
    return r;
}

static int run_window(Player *pl, int scale, bool fullscreen, const Display *display)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_Window *window = SDL_CreateWindow("ct_play", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          (int)(RENDER_WIDTH * display->pixel_aspect * scale + 0.5),
                                          display_rows(display) * scale,
                                          SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    SDL_Renderer *sdl_renderer = window ? SDL_CreateRenderer(window, -1, 0) : NULL;
    SDL_Texture *texture = sdl_renderer ? SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_RGB888,
                                                            SDL_TEXTUREACCESS_STREAMING, RENDER_WIDTH,
                                                            RENDER_HEIGHT)
                                        : NULL;
    if (!texture) {
        fprintf(stderr, "cannot create window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(window, (int)(RENDER_WIDTH * display->pixel_aspect + 0.5), display_rows(display));
    const SDL_Rect src = { 0, display->crop_top, RENDER_WIDTH, display_rows(display) };
    static Audio audio;   /* large; keep it off the stack */
    if (!pl->mute)
        audio_open(&audio);

    const Uint64 ticks_per_second = SDL_GetPerformanceFrequency();
    const Uint64 period = (Uint64)(ticks_per_second / FRAME_RATE);
    Uint64 deadline = SDL_GetPerformanceCounter() + period;
    bool running = true, paused = false, save_pending = false, halt_shown = false;
    long save_wait = 0;
    long frames_since_flush = 0;
    update_title(window, pl, paused, save_pending);

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            } else if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                SDL_Scancode sc = ev.key.keysym.scancode;
                bool alt = (ev.key.keysym.mod & KMOD_ALT) != 0;
                if (sc == SDL_SCANCODE_ESCAPE) {
                    running = false;
                } else if (sc == SDL_SCANCODE_F11 || (alt && sc == SDL_SCANCODE_RETURN)) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                } else if (sc == SDL_SCANCODE_P || sc == SDL_SCANCODE_PAUSE) {
                    paused = !paused;
                } else if (sc == SDL_SCANCODE_F5) {
                    save_pending = true;
                    save_wait = 0;
                } else if (sc == SDL_SCANCODE_F7) {
                    save_pending = false;
                    state_load(pl, pl->state_path);
                }
                update_title(window, pl, paused, save_pending);
            }
        }

        bool fast = false;
        if (pl->snes.halted && !halt_shown) {
            /* Without the interpreter, a miss stops the game (the stderr message says more). */
            halt_shown = true;
#if CT_PLAYER_NO_INTERPRETER
            misses_flush(pl);
#endif
            update_title(window, pl, paused, save_pending);
            char msg[1600];
            snprintf(msg, sizeof msg,
                     "The game stopped at frame %u: %s.\n\nThis player runs without the interpreter. The address "
                     "is logged to %s; a build with it in CT_RECOMP_COVERAGE generates the code. F7 loads the quick "
                     "save; Esc quits.",
                     (unsigned)pl->snes.frame, halt_reason(pl), misses_path(pl));
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ct_play stopped", msg, window);
        } else if (!pl->snes.halted) {
            halt_shown = false;
        }
        if (!paused && !pl->snes.halted) {
            /* Replay and fast forward run frames unpaced for up to a display frame. */
            const Uint8 *keys = SDL_GetKeyboardState(NULL);
            Uint64 budget_end = SDL_GetPerformanceCounter() + period;
            do {
                step(pl, read_pad());
                if (pl->snes.halted)
                    break;
                if (save_pending) {
                    if (!natives_busy(pl)) {
                        save_pending = false;
                        state_save(pl, pl->state_path);
                    } else if (++save_wait > SAVE_WAIT_FRAMES) {
                        save_pending = false;
                        fprintf(stderr, "quick save skipped: native routines stayed suspended; try again\n");
                    }
                }
                if (++frames_since_flush >= SRAM_FLUSH_FRAMES) {
                    frames_since_flush = 0;
                    sram_flush(pl);
#if CT_PLAYER_NO_INTERPRETER
                    misses_flush(pl);
#endif
                    update_title(window, pl, paused, save_pending);
                }
                fast = (replaying(pl) && !pl->watch) || keys[SDL_SCANCODE_TAB];
            } while (fast && SDL_GetPerformanceCounter() < budget_end);
            if (fast)
                deadline = SDL_GetPerformanceCounter();
            SDL_UpdateTexture(texture, NULL, pl->renderer.pixels, RENDER_WIDTH * 4);
        }
        /* Fast frames aren't played: the device stays silent until normal speed. */
        audio_frame(&audio, &pl->snes, paused || fast || pl->snes.halted);
        SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, 255);
        SDL_RenderClear(sdl_renderer);
        int out_w, out_h;
        SDL_GetRendererOutputSize(sdl_renderer, &out_w, &out_h);
        SDL_Rect dst = display_rect(display, out_w, out_h);
        SDL_RenderCopy(sdl_renderer, texture, &src, &dst);
        SDL_RenderPresent(sdl_renderer);

        /* Pace to the console's frame rate. When the host falls more than a few
         * frames behind, drop the debt instead of running fast to catch up. */
        Uint64 now = SDL_GetPerformanceCounter();
        if (now > deadline + 4 * period) {
            deadline = now + period;
            continue;
        }
        while (now < deadline) {
            Uint64 left_ms = (deadline - now) * 1000 / ticks_per_second;
            if (left_ms >= 2)
                SDL_Delay((Uint32)(left_ms - 1));
            else
                SDL_Delay(0);
            now = SDL_GetPerformanceCounter();
        }
        deadline += period;
    }

    sram_flush(pl);
    audio_close(&audio);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return track_write(pl) != 0;
}

int main(int argc, char **argv)
{
    const char *rom_path = NULL, *save_dir = NULL, *screenshot = NULL;
    const char *load_state_path = NULL, *save_state_path = NULL, *movie_path = NULL, *sram_file = NULL;
    const char *zst_path = NULL;
    int movie_sram = 0;
    uint32_t movie_sram_crc = 0;
    static Player pl;   /* large; keep it off the stack */
    bool native_given = false, recomp = true, recomp_only = true, recomp_given = false, audio_direct = false;
    pl.native_mode = NATIVE_ON;
    long frames = -1, scale = 3, movie_frames = -1;
    /* A TV showed the picture at 4:3 and hid about 8 rows at the top and bottom. */
    Display display = { .crop_top = 8, .crop_bottom = 8, .pixel_aspect = 4.0 / 3.0 * RENDER_HEIGHT / RENDER_WIDTH };
    bool fullscreen = false;
    static Hold holds[MAX_HOLDS];
    int nholds = 0;

    SDL_SetMainReady();
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage(stdout);
            return 0;
        }
        if (!strcmp(a, "--fullscreen")) {
            fullscreen = true;
            continue;
        }
        if (!strcmp(a, "--watch")) {
            pl.watch = true;
            continue;
        }
        if (!strcmp(a, "--mute")) {
            pl.mute = true;
            continue;
        }
        if (a[0] != '-') {
            rom_path = a;
            continue;
        }
        if (!v) {
            fprintf(stderr, "missing value for %s\n", a);
            return 2;
        }
        if (!strcmp(a, "--rom")) rom_path = v;
        else if (!strcmp(a, "--scale")) scale = strtol(v, NULL, 10);
        else if (!strcmp(a, "--aspect")) {
            if (!strcmp(v, "4:3")) display.pixel_aspect = 4.0 / 3.0 * RENDER_HEIGHT / RENDER_WIDTH;
            else if (!strcmp(v, "8:7")) display.pixel_aspect = 8.0 / 7.0;
            else if (!strcmp(v, "square")) display.pixel_aspect = 1.0;
            else {
                fprintf(stderr, "--aspect must be 4:3, 8:7 or square\n");
                return 2;
            }
        }
        else if (!strcmp(a, "--crop")) {
            char *end;
            long top = strtol(v, &end, 10), bottom = top;
            if (*end == ',')
                bottom = strtol(end + 1, &end, 10);
            if (*end || top < 0 || bottom < 0 || top + bottom > RENDER_HEIGHT - 16) {
                fprintf(stderr, "--crop must be ROWS or TOP,BOTTOM\n");
                return 2;
            }
            display.crop_top = (int)top;
            display.crop_bottom = (int)bottom;
        }
        else if (!strcmp(a, "--save-dir")) save_dir = v;
        else if (!strcmp(a, "--frames")) frames = strtol(v, NULL, 10);
        else if (!strcmp(a, "--screenshot")) screenshot = v;
        else if (!strcmp(a, "--load-state")) load_state_path = v;
        else if (!strcmp(a, "--load-zst")) zst_path = v;
        else if (!strcmp(a, "--save-state")) save_state_path = v;
        else if (!strcmp(a, "--wav")) pl.wav_path = v;
        else if (!strcmp(a, "--movie")) movie_path = v;
        else if (!strcmp(a, "--movie-frames")) movie_frames = strtol(v, NULL, 10);
        else if (!strcmp(a, "--record")) pl.track.record_path = v;
        else if (!strcmp(a, "--sram")) sram_file = v;
        else if (!strcmp(a, "--hold")) {
            if (nholds == MAX_HOLDS || parse_hold(v, &holds[nholds]) != 0) {
                fprintf(stderr, "bad --hold %s (expected BUTTON[+BUTTON]@FIRST-LAST)\n", v);
                return 2;
            }
            nholds++;
        }
        else if (!strcmp(a, "--audio")) {
            if (!strcmp(v, "snes") || !strcmp(v, "direct")) {
                audio_direct = strcmp(v, "direct") == 0;
            } else {
                fprintf(stderr, "--audio must be snes or direct\n");
                return 2;
            }
        }
        else if (!strcmp(a, "--recomp")) {
            recomp_given = true;
            if (!strcmp(v, "off")) recomp = false, recomp_only = false;
            else if (!strcmp(v, "on")) recomp = true, recomp_only = false;
            else if (!strcmp(v, "only")) recomp = true, recomp_only = true;
            else {
                fprintf(stderr, "--recomp must be only, on or off\n");
                return 2;
            }
        }
        else if (!strcmp(a, "--native")) {
            native_given = true;
            if (!strcmp(v, "off")) pl.native_mode = NATIVE_OFF;
            else if (!strcmp(v, "on")) pl.native_mode = NATIVE_ON;
            else if (!strcmp(v, "shadow")) pl.native_mode = NATIVE_SHADOW;
            else {
                fprintf(stderr, "--native must be on, shadow or off\n");
                return 2;
            }
        }
        else {
            fprintf(stderr, "unknown argument %s\n", a);
            usage(stderr);
            return 2;
        }
        i++;
    }
    if (!rom_path || scale < 1 || scale > 16) {
        usage(stderr);
        return 2;
    }
    if (CT_PLAYER_NO_INTERPRETER && (pl.native_mode != NATIVE_ON || !recomp)) {
        fprintf(stderr, "this player runs %s: it needs --native on and generated blocks (--recomp only or on)\n",
                build_mode[CT_PLAYER_NO_INTERPRETER]);
        return 2;
    }
    Track *t = &pl.track;
    if (audio_direct) {
        /* APU-direct runs in the sound driver's ported routines and changes timing */
        if (pl.native_mode != NATIVE_ON || t->record_path || (recomp_given && recomp_only)) {
            fprintf(stderr, "--audio direct needs --native on with the ported routines (not --recomp only), "
                            "and can't record: it changes the original's timing\n");
            return 2;
        }
        recomp_only = false;
    }
    if (t->record_path) {
        /* A movie replays the same only where the original code's timing is kept,
         * from power-on with the SRAM ct_dev starts with. */
        if (native_given && pl.native_mode == NATIVE_ON && !recomp_only) {
            fprintf(stderr, "--record needs --native shadow or off, or on with --recomp only: ported routines "
                            "change timing, so the movie wouldn't replay the same\n");
            return 2;
        }
        if (load_state_path || zst_path) {
            fprintf(stderr, "--record starts from power-on and can't be combined with --load-state or --load-zst\n");
            return 2;
        }
        if (!native_given && !CT_PLAYER_NO_INTERPRETER)
            pl.native_mode = NATIVE_SHADOW;
    }

    size_t rom_len;
    uint8_t *rom = read_file(rom_path, &rom_len);
    if (!rom) {
        fprintf(stderr, "cannot read %s\n", rom_path);
        return 1;
    }
    size_t header = rom_len % 0x8000 == 0x200 ? 0x200 : 0;

    side_path(pl.sram_path, sizeof pl.sram_path, rom_path, rom_path, save_dir, ".srm");
    if (t->record_path)
        side_path(pl.state_path, sizeof pl.state_path, t->record_path, rom_path, save_dir, ".state");
    else
        side_path(pl.state_path, sizeof pl.state_path, rom_path, rom_path, save_dir, ".state");

    if (movie_path) {
        ct_movie movie;
        if (ct_movie_load(&movie, movie_path) != 0)
            return 1;
        uint32_t n = movie_frames >= 0 ? (uint32_t)movie_frames : movie.length;
        for (uint32_t fr = 0; fr < n; fr++)
            track_set(t, fr, ct_movie_buttons(&movie, fr));
        t->replay_frames = n;
        t->movie_name = file_name(movie_path);
        movie_sram = movie.has_sram;
        movie_sram_crc = movie.sram_crc;
        ct_movie_free(&movie);
    }

    snes_init(&pl.snes, rom + header, rom_len - header);
    if (recomp && recomp_native_count > 0 && ct_crc32(rom + header, rom_len - header) != recomp_rom_crc) {
        fprintf(stderr, "warning: generated blocks are for ROM CRC-32 %08X, not this one; not using them\n",
                (unsigned)recomp_rom_crc);
        recomp = false;
    }
    int nrecomp = recomp ? recomp_native_count : 0;
#if CT_PLAYER_NO_INTERPRETER
    if (!nrecomp) {
        fprintf(stderr, "this player runs %s, and it has no generated blocks for this ROM (%s): build it with the "
                        "ROM as CT_ROM\n",
                build_mode[CT_PLAYER_NO_INTERPRETER], recomp_native_count ? "they are for another ROM" : "none");
        return 1;
    }
#if CT_PLAYER_NO_INTERPRETER == 1
    pl.snes.interpreter = cpu_step;   /* for misses only */
#endif
    pl.snes.native_only = true;
    pl.snes.on_miss = on_miss;
    pl.snes.on_miss_ctx = &pl;
    side_path(pl.misses_path, sizeof pl.misses_path, rom_path, rom_path, save_dir, ".misses.txt");
    pl.misses = misses_create(&pl.snes);
    if (misses_load(pl.misses, pl.misses_path) < 0)
        fprintf(stderr, "warning: cannot read %s; starting a new one\n", pl.misses_path);
    fprintf(stderr, "ct_play: %s; instructions without generated code are logged to %s\n",
            build_mode[CT_PLAYER_NO_INTERPRETER], pl.misses_path);
#else
    pl.snes.interpreter = cpu_step;
#endif
    /* In on mode, generated blocks alone keep the original's timing exactly;
     * ported routines charge estimated time (docs/RECOMP.md, M2). Shadow and off
     * modes don't depend on timing, so they keep the ported routines. Without
     * generated blocks, the ported routines run. */
    int nported = recomp_only && nrecomp > 0 && pl.native_mode == NATIVE_ON ? 0 : game_native_count;
    pl.table = malloc(sizeof *pl.table * (size_t)(nported + nrecomp + 1));
    memcpy(pl.table, game_natives, sizeof *pl.table * (size_t)nported);
    if (nrecomp)
        memcpy(pl.table + nported, recomp_natives, sizeof *pl.table * (size_t)nrecomp);
    pl.table_count = nported + nrecomp;
    if (t->record_path && pl.native_mode == NATIVE_ON && nported) {
        fprintf(stderr, "--record with --native on needs the generated blocks, which this build or ROM lacks\n");
        return 2;
    }
    render_init(&pl.renderer);
    pl.snes.on_line = on_line;
    pl.snes.on_line_ctx = &pl.renderer;
    snes_power_on(&pl.snes);
    /* --sram and recordings start from a fixed save and never write one back. */
    pl.persist_sram = !t->record_path && !sram_file && !zst_path;
    if (sram_file) {
        size_t sram_len;
        uint8_t *sram = read_file(sram_file, &sram_len);
        if (!sram || sram_len != SNES_SRAM_SIZE) {
            fprintf(stderr, "cannot read %s as a %d-byte SRAM file\n", sram_file, SNES_SRAM_SIZE);
            return 1;
        }
        memcpy(pl.snes.bus.sram, sram, SNES_SRAM_SIZE);
        free(sram);
        t->sram_name = file_name(sram_file);
    }
    t->sram_crc = ct_crc32(pl.snes.bus.sram, SNES_SRAM_SIZE);
    if (movie_sram && (!sram_file || t->sram_crc != movie_sram_crc)) {
        fprintf(stderr, "%s starts from SRAM with CRC-32 %08X; pass that save with --sram\n", movie_path,
                (unsigned)movie_sram_crc);
        return 1;
    }
    if (pl.persist_sram)
        sram_load(&pl);
    /* Replaying for a recording runs uninterrupted interpretation, which keeps the
     * same timing as shadow mode and is several times faster. */
    natives_start(&pl, t->replay_frames && pl.native_mode == NATIVE_SHADOW ? NATIVE_OFF : pl.native_mode);
    if (audio_direct)
        au_select_backend(m_platform(&pl.snes), AU_BACKEND_DIRECT);
    if (load_state_path && state_load(&pl, load_state_path) != 0)
        return 1;
    if (zst_path) {
        char why[160];
        if (zst_import_file(&pl.snes, zst_path, why, sizeof why) != 0) {
            fprintf(stderr, "cannot import %s: %s\n", zst_path, why);
            return 1;
        }
        memcpy(pl.sram_saved, pl.snes.bus.sram, SNES_SRAM_SIZE);
        pl.snes.frame = 0;
        natives_start(&pl, pl.runtime_mode);
        fprintf(stderr, "imported ZSNES state %s (some state approximated; not for validation)\n", zst_path);
    }
    if (t->record_path)
        fprintf(stderr, "recording to %s in %s mode; quick saves go to %s\n", t->record_path,
                pl.native_mode == NATIVE_SHADOW ? "shadow" : pl.native_mode == NATIVE_ON ? "on" : "off",
                pl.state_path);

    int rc;
    if (frames >= 0)
        rc = run_headless(&pl, frames, holds, nholds, screenshot, save_state_path);
    else
        rc = run_window(&pl, (int)scale, fullscreen, &display);

    if (t->record_path)
        fprintf(stderr, "recorded %lu frames to %s\n", (unsigned long)t->frames, t->record_path);
    natives_report(&pl);
    native_destroy(pl.natives);
#if CT_PLAYER_NO_INTERPRETER
    misses_flush(&pl);
    if (pl.miss_steps) {
        uint64_t total;
        size_t distinct = misses_count(pl.misses, &total);
        fprintf(stderr, "ct_play: %lu instructions this session had no generated code; %s now lists %llu at %lu "
                        "addresses (feed it back with CT_RECOMP_COVERAGE)\n",
                pl.miss_steps, pl.misses_path, (unsigned long long)total, (unsigned long)distinct);
    }
    misses_destroy(pl.misses);
#endif
    free(t->pads);
    free(rom);
    return rc;
}
