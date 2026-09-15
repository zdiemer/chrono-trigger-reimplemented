/* ct_play: plays the game in a window, with the ported routines running natively
 * and the rest on the development system (docs/PLAYING.md).
 *
 *   ct_play [--rom] <rom> [--scale N] [--fullscreen] [--mute] [--native on|off] [--save-dir dir]
 *   ct_play --rom <rom> --frames N [--screenshot out.png] [--hold BUTTONS@FIRST-LAST]...
 *           [--load-state f] [--save-state f] [--wav out.wav]
 *
 * With --frames it runs headless (no window) for a fixed number of frames, for
 * checking a build without a display. In a window, the APU's 32000 Hz output
 * plays through SDL audio (see audio_frame).
 */
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "media.h"
#include "movie.h"
#include "registry.h"
#include "snes/render.h"
#include "snes/system.h"

/* NTSC: 21.477272 MHz master clock, 1364 clocks per line, 262 lines per frame. */
#define MASTER_CLOCK_HZ 21477272.0
#define FRAME_RATE (MASTER_CLOCK_HZ / (SNES_CLOCKS_PER_LINE * SNES_LINES_PER_FRAME))

/* Frames between checks for changed SRAM, which is then written out. */
#define SRAM_FLUSH_FRAMES 60
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

typedef struct {
    Snes snes;
    Renderer renderer;
    NativeRuntime *natives;
    NativeMode native_mode;
    char sram_path[1024];
    char state_path[1024];
    uint8_t sram_saved[SNES_SRAM_SIZE];   /* SRAM as last loaded or written */
    bool mute;                            /* no audio device */
    const char *wav_path;                 /* headless: write the audio here */
} Player;

static void usage(FILE *out)
{
    fprintf(out,
            "usage: ct_play [--rom] <rom> [options]\n"
            "\n"
            "Options:\n"
            "  --scale N            window scale (integer, default 3)\n"
            "  --fullscreen         start fullscreen\n"
            "  --mute               no sound\n"
            "  --native on|off      run ported routines natively (default on) or interpret everything\n"
            "  --save-dir DIR       where <rom name>.srm and <rom name>.state go (default: the ROM's directory)\n"
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
            "  %-12s quick save / quick load (same build only)\n"
            "  %-12s toggle fullscreen\n"
            "  %-12s quit\n"
            "\n"
            "Saves are written to <rom name>.srm when the game changes them and on exit.\n",
            "P", "F5 / F7", "F11, Alt+Enter", "Esc");
}

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

/* dir/<rom file name without extension><ext>; dir defaults to the ROM's directory. */
static void side_path(char *out, size_t n, const char *rom_path, const char *dir, const char *ext)
{
    const char *name = rom_path;
    for (const char *p = rom_path; *p; p++) {
        if (*p == '/' || *p == '\\')
            name = p + 1;
    }
    const char *dot = strrchr(name, '.');
    int stem = dot && dot != name ? (int)(dot - name) : (int)strlen(name);
    if (dir)
        snprintf(out, n, "%s/%.*s%s", dir, stem, name, ext);
    else
        snprintf(out, n, "%.*s%.*s%s", (int)(name - rom_path), rom_path, stem, name, ext);
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
    if (!memcmp(pl->sram_saved, pl->snes.bus.sram, SNES_SRAM_SIZE))
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

static void natives_start(Player *pl)
{
    native_destroy(pl->natives);
    pl->snes.intercept = NULL;
    pl->snes.intercept_ctx = NULL;
    pl->natives = native_create(game_natives, game_native_count, pl->native_mode);
    native_attach(pl->natives, &pl->snes);
}

static int state_save(Player *pl, const char *path)
{
    if (snes_state_save(&pl->snes, path) != 0) {
        fprintf(stderr, "cannot save state %s\n", path);
        return -1;
    }
    fprintf(stderr, "saved state %s\n", path);
    return 0;
}

/* A state holds only the machine, so native routines suspended in call-outs when
 * it was saved can't resume; the frontend only saves when none are. Loading
 * restarts the native runtime, abandoning any routines suspended now. */
static int state_load(Player *pl, const char *path)
{
    if (snes_state_load(&pl->snes, path) != 0) {
        fprintf(stderr, "cannot load state %s (missing, or saved by a different build)\n", path);
        return -1;
    }
    natives_start(pl);
    fprintf(stderr, "loaded state %s\n", path);
    return 0;
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
        while (apu_take_samples(&snes->apu, discard, 256))
            ;
        if (a->dev) {
            if (a->playing)
                SDL_PauseAudioDevice(a->dev, 1);
            a->playing = false;
            audio_ring_clear(a);
        }
        return;
    }

    a->in_count += apu_take_samples(&snes->apu, a->in + a->in_count * 2,
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
    for (long frame = 0; frame < frames; frame++) {
        uint16_t pad = 0;
        for (int i = 0; i < nholds; i++) {
            if (frame >= holds[i].first && frame <= holds[i].last)
                pad |= holds[i].buttons;
        }
        snes_run_frame(&pl->snes, pad);
        ct_wav_write(&wav, audio, apu_take_samples(&pl->snes.apu, audio, DSP_OUT_FRAMES));
    }
    sram_flush(pl);
    int rc = 0;
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
        while (native_suspended(pl->natives) && waited < SAVE_WAIT_FRAMES) {
            snes_run_frame(&pl->snes, 0);
            waited++;
        }
        if (waited)
            fprintf(stderr, "waited %ld frames for native routines to finish\n", waited);
        if (native_suspended(pl->natives)) {
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

static void set_title(SDL_Window *window, bool paused, bool saving)
{
    SDL_SetWindowTitle(window, paused ? "ct_play (paused)" : saving ? "ct_play (saving state...)" : "ct_play");
}

static int run_window(Player *pl, int scale, bool fullscreen)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow("ct_play", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          RENDER_WIDTH * scale, RENDER_HEIGHT * scale,
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
    SDL_SetWindowMinimumSize(window, RENDER_WIDTH, RENDER_HEIGHT);
    SDL_RenderSetLogicalSize(sdl_renderer, RENDER_WIDTH, RENDER_HEIGHT);
    SDL_RenderSetIntegerScale(sdl_renderer, SDL_TRUE);
    static Audio audio;   /* large; keep it off the stack */
    if (!pl->mute)
        audio_open(&audio);

    const Uint64 ticks_per_second = SDL_GetPerformanceFrequency();
    const Uint64 period = (Uint64)(ticks_per_second / FRAME_RATE);
    Uint64 deadline = SDL_GetPerformanceCounter() + period;
    bool running = true, paused = false, save_pending = false;
    long save_wait = 0;
    long frames_since_flush = 0;

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
                    set_title(window, paused, save_pending);
                } else if (sc == SDL_SCANCODE_F5) {
                    save_pending = true;
                    save_wait = 0;
                    set_title(window, paused, save_pending);
                } else if (sc == SDL_SCANCODE_F7) {
                    save_pending = false;
                    state_load(pl, pl->state_path);
                    set_title(window, paused, save_pending);
                }
            }
        }

        if (!paused) {
            snes_run_frame(&pl->snes, read_pad());            SDL_UpdateTexture(texture, NULL, pl->renderer.pixels, RENDER_WIDTH * 4);
            if (save_pending) {
                if (!native_suspended(pl->natives)) {
                    save_pending = false;
                    state_save(pl, pl->state_path);
                } else if (++save_wait > SAVE_WAIT_FRAMES) {
                    save_pending = false;
                    fprintf(stderr, "quick save skipped: native routines stayed suspended; try again\n");
                }
                if (!save_pending)
                    set_title(window, paused, false);
            }
            if (++frames_since_flush >= SRAM_FLUSH_FRAMES) {
                frames_since_flush = 0;
                sram_flush(pl);
            }
        }
        audio_frame(&audio, &pl->snes, paused);
        SDL_RenderClear(sdl_renderer);
        SDL_RenderCopy(sdl_renderer, texture, NULL, NULL);
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
    return 0;
}

int main(int argc, char **argv)
{
    const char *rom_path = NULL, *save_dir = NULL, *screenshot = NULL;
    const char *load_state_path = NULL, *save_state_path = NULL;
    static Player pl;   /* large; keep it off the stack */
    pl.native_mode = NATIVE_ON;
    long frames = -1, scale = 3;
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
        else if (!strcmp(a, "--save-dir")) save_dir = v;
        else if (!strcmp(a, "--frames")) frames = strtol(v, NULL, 10);
        else if (!strcmp(a, "--screenshot")) screenshot = v;
        else if (!strcmp(a, "--load-state")) load_state_path = v;
        else if (!strcmp(a, "--save-state")) save_state_path = v;
        else if (!strcmp(a, "--wav")) pl.wav_path = v;
        else if (!strcmp(a, "--hold")) {
            if (nholds == MAX_HOLDS || parse_hold(v, &holds[nholds]) != 0) {
                fprintf(stderr, "bad --hold %s (expected BUTTON[+BUTTON]@FIRST-LAST)\n", v);
                return 2;
            }
            nholds++;
        }
        else if (!strcmp(a, "--native")) {
            if (!strcmp(v, "off")) pl.native_mode = NATIVE_OFF;
            else if (!strcmp(v, "on")) pl.native_mode = NATIVE_ON;
            else {
                fprintf(stderr, "--native must be on or off\n");
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

    size_t rom_len;
    uint8_t *rom = read_file(rom_path, &rom_len);
    if (!rom) {
        fprintf(stderr, "cannot read %s\n", rom_path);
        return 1;
    }
    size_t header = rom_len % 0x8000 == 0x200 ? 0x200 : 0;

    side_path(pl.sram_path, sizeof pl.sram_path, rom_path, save_dir, ".srm");
    side_path(pl.state_path, sizeof pl.state_path, rom_path, save_dir, ".state");

    snes_init(&pl.snes, rom + header, rom_len - header);
    render_init(&pl.renderer);
    pl.snes.on_line = on_line;
    pl.snes.on_line_ctx = &pl.renderer;
    snes_power_on(&pl.snes);
    sram_load(&pl);
    natives_start(&pl);
    if (load_state_path && state_load(&pl, load_state_path) != 0)
        return 1;

    int rc;
    if (frames >= 0)
        rc = run_headless(&pl, frames, holds, nholds, screenshot, save_state_path);
    else
        rc = run_window(&pl, (int)scale, fullscreen);

    native_destroy(pl.natives);
    free(rom);
    return rc;
}
