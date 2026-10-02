/* The window driver: SDL video, audio and input around the player, paced to
 * the console's frame rate. */
#include <SDL.h>

#include <stdio.h>
#include <string.h>

#include "audio_fill.h"
#include "driver.h"
#include "bindings.h"

/* NTSC: 21.477272 MHz master clock, 1364 clocks per line, 262 lines per frame. */
#define MASTER_CLOCK_HZ 21477272.0
#define FRAME_RATE (MASTER_CLOCK_HZ / (SNES_CLOCKS_PER_LINE * SNES_LINES_PER_FRAME))

/* Frames between checks for changed SRAM, which is then written out. */
#define SRAM_FLUSH_FRAMES 60

void window_print_controls(FILE *out)
{
    bindings_print(out);
}

/* ---- pads ------------------------------------------------------------------------ */

#define MAX_PADS 8

typedef struct {
    SDL_GameController *open[MAX_PADS];
    int count;
} Pads;

static void pad_added(Pads *p, int device)
{
    if (p->count == MAX_PADS || !SDL_IsGameController(device))
        return;
    SDL_GameController *c = SDL_GameControllerOpen(device);
    if (!c)
        return;
    for (int i = 0; i < p->count; i++) {
        if (p->open[i] == c) {   /* already open: SDL reports pads present at start too */
            SDL_GameControllerClose(c);
            return;
        }
    }
    p->open[p->count++] = c;
    fprintf(stderr, "gamepad: %s\n", SDL_GameControllerName(c));
}

static void pad_removed(Pads *p, SDL_JoystickID id)
{
    for (int i = 0; i < p->count; i++) {
        if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(p->open[i])) == id) {
            SDL_GameControllerClose(p->open[i]);
            p->open[i] = p->open[--p->count];
            return;
        }
    }
}

static void update_title(SDL_Window *window, const Player *pl, bool paused)
{
    char title[400];
    player_title(pl, paused, title, sizeof title);
    SDL_SetWindowTitle(window, title);
}

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

/* ---- audio ---------------------------------------------------------------------- */

typedef struct {
    SDL_AudioDeviceID dev;   /* 0: muted or no device */
    bool playing;
    AudioFill fill;
} Audio;

static void SDLCALL audio_callback(void *ctx, Uint8 *stream, int len)
{
    audio_fill_drain(ctx, (int16_t *)(void *)stream, (size_t)len / 4);
}

static void audio_lock(void *ctx, bool locked)
{
    Audio *a = ctx;
    if (locked)
        SDL_LockAudioDevice(a->dev);
    else
        SDL_UnlockAudioDevice(a->dev);
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
    want.userdata = &a->fill;
    a->dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!a->dev) {
        fprintf(stderr, "no audio: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }
    audio_fill_init(&a->fill, have.freq, have.samples, audio_lock, a);
}

static void audio_close(Audio *a)
{
    if (!a->dev)
        return;
    SDL_CloseAudioDevice(a->dev);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    a->dev = 0;
}

/* Once per loop iteration: move the samples the last frame produced to the
 * device. While paused, the device is stopped and the buffers emptied. */
static void audio_frame(Audio *a, Player *pl, bool paused)
{
    if (!a->dev) {
        player_discard_audio(pl);
        return;
    }
    bool play = audio_fill_frame(&a->fill, pl, paused);
    if (play != a->playing)
        SDL_PauseAudioDevice(a->dev, !play);
    a->playing = play;
}

/* ---- the loop ------------------------------------------------------------------- */

int run_window(Player *pl, const WindowOptions *o)
{
    const Display *display = &o->display;
    bool fullscreen = o->fullscreen;
    int scale = o->scale;
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
    if (!o->mute)
        audio_open(&audio);

    /* Gamepads: SDL reports the ones already plugged in as added, too. Pads
     * are read by position (bindings.c), not by the labels on their buttons. */
#ifdef SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
#endif
    static Pads pads;
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
        fprintf(stderr, "no gamepads: %s\n", SDL_GetError());

    const Uint64 ticks_per_second = SDL_GetPerformanceFrequency();
    const Uint64 period = (Uint64)(ticks_per_second / FRAME_RATE);
    Uint64 deadline = SDL_GetPerformanceCounter() + period;
    bool running = true, paused = false, halt_shown = false;
    bool pad_was_held[ACT_COUNT] = { false };
    long frames_since_flush = 0;
    update_title(window, pl, paused);

    while (running) {
        /* The actions of this iteration's key presses, then of pad inputs
         * newly held (sticks and triggers have no press events). */
        Action actions[64];
        int nactions = 0;
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            } else if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                SDL_Scancode sc = ev.key.keysym.scancode;
                bool alt = (ev.key.keysym.mod & KMOD_ALT) != 0;
                Action a = alt && sc == SDL_SCANCODE_RETURN ? ACT_FULLSCREEN : bindings_key_action(sc);
                if (nactions < 64)
                    actions[nactions++] = a;
            } else if (ev.type == SDL_CONTROLLERDEVICEADDED) {
                pad_added(&pads, ev.cdevice.which);
            } else if (ev.type == SDL_CONTROLLERDEVICEREMOVED) {
                pad_removed(&pads, ev.cdevice.which);
            }
        }
        for (int a = ACT_PAUSE; a < ACT_COUNT; a++) {
            bool now = bindings_held((Action)a, pads.open, pads.count, false);
            if (now && !pad_was_held[a] && a != ACT_FAST_FORWARD && nactions < 64)
                actions[nactions++] = (Action)a;
            pad_was_held[a] = now;
        }
        for (int i = 0; i < nactions; i++) {
            switch (actions[i]) {
            case ACT_QUIT:
                running = false;
                break;
            case ACT_FULLSCREEN:
                fullscreen = !fullscreen;
                SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                break;
            case ACT_PAUSE:
                paused = !paused;
                break;
            case ACT_QUICK_SAVE:
                player_request_save(pl);
                break;
            case ACT_QUICK_LOAD:
                player_cancel_save(pl);
                files_load_state(pl, o->files->state);
                break;
            default:
                continue;
            }
            update_title(window, pl, paused);
        }

        bool fast = false;
        if (pl->snes.halted && !halt_shown) {
            /* Without the interpreter, a miss stops the game (the stderr message says more). */
            halt_shown = true;
            files_flush(o->files, pl);
            update_title(window, pl, paused);
            char msg[1600], load[64], quit[64];
            snprintf(msg, sizeof msg,
                     "The game stopped at frame %u: %s.\n\nThe address is logged to %s. %s loads the quick "
                     "save; %s quits.",
                     (unsigned)pl->snes.frame, player_halt_reason(pl), player_misses_name(pl),
                     bindings_name(ACT_QUICK_LOAD, load, sizeof load, "QuickLoad"),
                     bindings_name(ACT_QUIT, quit, sizeof quit, "closing the window"));
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ct_play stopped", msg, window);
        } else if (!pl->snes.halted) {
            halt_shown = false;
        }
        if (!paused && !pl->snes.halted) {
            /* Replay and fast forward run frames unpaced for up to a display frame. */
            Uint64 budget_end = SDL_GetPerformanceCounter() + period;
            do {
                bool was_saving = player_saving(pl);
                player_step(pl, bindings_buttons(pads.open, pads.count));
                files_after_frame(o->files, pl);
                if (pl->snes.halted)
                    break;
                if (was_saving && !player_saving(pl))
                    update_title(window, pl, paused);
                if (++frames_since_flush >= SRAM_FLUSH_FRAMES) {
                    frames_since_flush = 0;
                    files_flush(o->files, pl);
                    update_title(window, pl, paused);
                }
                fast = (player_replaying(pl) && !pl->watch) ||
                       bindings_held(ACT_FAST_FORWARD, pads.open, pads.count, true);
            } while (fast && SDL_GetPerformanceCounter() < budget_end);
            if (fast)
                deadline = SDL_GetPerformanceCounter();
            SDL_UpdateTexture(texture, NULL, player_pixels(pl), RENDER_WIDTH * 4);
        }
        /* Fast frames aren't played: the device stays silent until normal speed. */
        audio_frame(&audio, pl, paused || fast || pl->snes.halted);
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

    files_flush(o->files, pl);
    audio_close(&audio);
    while (pads.count)
        SDL_GameControllerClose(pads.open[--pads.count]);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return files_flush_movie(o->files, pl) != 0;
}
