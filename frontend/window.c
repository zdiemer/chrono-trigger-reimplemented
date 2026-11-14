/* The window driver: SDL video, audio and input around the player, paced to
 * the console's frame rate. */
#include <SDL.h>

#include <stdio.h>
#include <string.h>

#include "audio_fill.h"
#include "window.h"
#include "platform/native/render.h"
#include "bindings.h"

/* NTSC: 21.477272 MHz master clock, 1364 clocks per line, 262 lines per frame. */
#define MASTER_CLOCK_HZ 21477272.0
#define FRAME_RATE (MASTER_CLOCK_HZ / (1364 * 262))

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

static void update_title(SDL_Window *window, const WindowClient *client, bool paused)
{
    char title[400];
    client->title(client->ctx, paused, title, sizeof title);
    SDL_SetWindowTitle(window, title);
}

static int display_width(const Display *d) { return d->width ? d->width : VID_RENDER_WIDTH; }

/* The rows the pixel shape shows, from `look.crop` down, and their pixels' shape. */
static PixelLook display_look(const PresentSettings *p) { return present_pixel_look(p->pixels, VID_RENDER_HEIGHT); }
static int display_rows(PixelLook look) { return VID_RENDER_HEIGHT - 2 * look.crop; }

/* ---- presentation ---------------------------------------------------------------- */

/* The renderer's frame, in a texture the GPU scales (frontend/presentation.h):
 * `picture` holds it as drawn; `scaled` is where sharp-bilinear first draws
 * it at a whole-number scale, made again when that scale changes. */
typedef struct {
    SDL_Renderer *renderer;
    SDL_Texture *picture, *scaled;
    int scaled_w, scaled_h;
} Screen;

static SDL_Rect sdl_rect(PresentRect r) { return (SDL_Rect){ r.x, r.y, r.w, r.h }; }

static bool screen_open(Screen *s, SDL_Renderer *renderer, int width)
{
    *s = (Screen){ renderer, NULL, NULL, 0, 0 };
    s->picture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, width,
                                   VID_RENDER_HEIGHT);
    if (s->picture)
        SDL_SetTextureScaleMode(s->picture, SDL_ScaleModeNearest);
    return s->picture != NULL;
}

static void screen_close(Screen *s)
{
    if (s->scaled)
        SDL_DestroyTexture(s->scaled);
    if (s->picture)
        SDL_DestroyTexture(s->picture);
    s->scaled = s->picture = NULL;
}

/* The texture sharp-bilinear draws into, w x h, made where it isn't; NULL
 * where the renderer can't draw into a texture. */
static SDL_Texture *scaled_texture(Screen *s, int w, int h)
{
    if (s->scaled && s->scaled_w == w && s->scaled_h == h)
        return s->scaled;
    if (s->scaled)
        SDL_DestroyTexture(s->scaled);
    s->scaled = NULL;
    if (!SDL_RenderTargetSupported(s->renderer))
        return NULL;
    s->scaled = SDL_CreateTexture(s->renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (s->scaled) {
        SDL_SetTextureScaleMode(s->scaled, SDL_ScaleModeLinear);
        s->scaled_w = w;
        s->scaled_h = h;
    }
    return s->scaled;
}

enum { SCANLINE_DARKNESS = 96, MOST_SCANLINES = VID_RENDER_HEIGHT };

/* The frame on an output of out_w x out_h: black around it, the picture
 * where the presentation lays it out, scaled as it says, the filter over it. */
static void screen_draw(Screen *s, const Display *d, const PresentSettings *p, int out_w, int out_h)
{
    PixelLook look = display_look(p);
    int width = display_width(d), rows = display_rows(look);
    const SDL_Rect src = { 0, look.crop, width, rows };
    PresentLayout l = present_layout(p->scaling, width, rows, look.aspect, out_w, out_h);
    SDL_Rect place = sdl_rect(l.place);
    SDL_SetRenderDrawColor(s->renderer, 0, 0, 0, 255);
    SDL_RenderClear(s->renderer);
    SDL_Texture *scaled = l.smooth ? scaled_texture(s, width * l.across, rows * l.down) : NULL;
    if (scaled) {
        SDL_SetRenderTarget(s->renderer, scaled);
        SDL_RenderCopy(s->renderer, s->picture, &src, NULL);
        SDL_SetRenderTarget(s->renderer, NULL);
        SDL_RenderCopy(s->renderer, scaled, NULL, &place);
    } else {
        SDL_RenderCopy(s->renderer, s->picture, &src, &place);
    }
    if (p->filter == FILTER_SCANLINES) {
        PresentRect lines[MOST_SCANLINES];
        SDL_Rect rects[MOST_SCANLINES];
        int n = present_scanlines(l.place, rows, lines, MOST_SCANLINES);
        for (int i = 0; i < n; i++)
            rects[i] = sdl_rect(lines[i]);
        SDL_SetRenderDrawBlendMode(s->renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(s->renderer, 0, 0, 0, SCANLINE_DARKNESS);
        SDL_RenderFillRects(s->renderer, rects, n);
        SDL_SetRenderDrawBlendMode(s->renderer, SDL_BLENDMODE_NONE);
    }
}

int window_present_picture(const uint32_t *pixels, const Display *display, const PresentSettings *present, int out_w,
                           int out_h, uint32_t *out)
{
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(out, out_w, out_h, 32, out_w * 4,
                                                              SDL_PIXELFORMAT_RGB888);
    SDL_Renderer *renderer = surface ? SDL_CreateSoftwareRenderer(surface) : NULL;
    Screen screen = { 0 };
    int rc = -1;
    if (renderer && screen_open(&screen, renderer, display_width(display))) {
        SDL_UpdateTexture(screen.picture, NULL, pixels, display_width(display) * 4);
        screen_draw(&screen, display, present, out_w, out_h);
        SDL_RenderPresent(renderer);
        rc = 0;
    } else {
        fprintf(stderr, "cannot draw the presentation: %s\n", SDL_GetError());
    }
    screen_close(&screen);
    if (renderer)
        SDL_DestroyRenderer(renderer);
    if (surface)
        SDL_FreeSurface(surface);
    return rc;
}

/* The window filling the screen as the settings say: borderless, over the
 * desktop at its own resolution, or exclusive, the display switched to the
 * desktop's mode for the window alone; or not at all. */
static void apply_fullscreen(SDL_Window *window, const PresentSettings *p)
{
    Uint32 flags = 0;
    if (p->fullscreen && p->fullscreen_mode == FULLSCREEN_EXCLUSIVE) {
        SDL_DisplayMode desktop;
        if (SDL_GetDesktopDisplayMode(SDL_GetWindowDisplayIndex(window), &desktop) == 0)
            SDL_SetWindowDisplayMode(window, &desktop);
        flags = SDL_WINDOW_FULLSCREEN;
    } else if (p->fullscreen) {
        flags = SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    if (SDL_SetWindowFullscreen(window, flags) != 0)
        fprintf(stderr, "fullscreen: %s\n", SDL_GetError());
}

/* ---- quitting -------------------------------------------------------------------- */

/* Whether the player confirms quitting: the game stops while a small prompt
 * asks, where Escape (so a second press of the default quit key) quits and
 * Return plays on. The window's close button quits without asking. */
static bool confirm_quit(SDL_Window *window, const char *name)
{
    const char *text = "Quit? Play since your last save will be lost.\n\n"
                       "Press Escape to quit, or Return to keep playing.";
    const SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Keep playing" },
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 1, "Quit" },
    };
    const SDL_MessageBoxData box = { SDL_MESSAGEBOX_WARNING, window, name, text, 2, buttons, NULL };
    int chosen = 0;
    if (SDL_ShowMessageBox(&box, &chosen) != 0)
        return true;   /* no prompt to be had: quit, as before */
    return chosen == 1;
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
    want.freq = AU_SAMPLE_RATE;
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
    fprintf(stderr, "audio: %d Hz stereo, %u sample device buffer\n", have.freq, (unsigned)have.samples);
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
static void audio_frame(Audio *a, const WindowClient *client, bool paused)
{
    if (!a->dev) {
        client->audio.discard(client->audio.ctx);
        return;
    }
    bool play = audio_fill_frame(&a->fill, &client->audio, paused);
    if (play != a->playing)
        SDL_PauseAudioDevice(a->dev, !play);
    a->playing = play;
}

/* ---- the loop ------------------------------------------------------------------- */

int window_run(const WindowClient *client, WindowOptions *o)
{
    const Display *display = &o->display;
    PresentSettings *present = &o->present;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    int window_w, window_h;
    PixelLook look = display_look(present);
    present_window_size(o->scale, display_width(display), display_rows(look), look.aspect, &window_w, &window_h);
    SDL_Window *window = SDL_CreateWindow(client->name, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, window_w,
                                          window_h, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *sdl_renderer = window ? SDL_CreateRenderer(window, -1, 0) : NULL;
    Screen screen = { 0 };
    if (!sdl_renderer || !screen_open(&screen, sdl_renderer, display_width(display))) {
        fprintf(stderr, "cannot create window: %s\n", SDL_GetError());
        if (sdl_renderer)
            SDL_DestroyRenderer(sdl_renderer);
        if (window)
            SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetWindowMinimumSize(window, (int)(display_width(display) * look.aspect + 0.5), display_rows(look));
    if (present->fullscreen)
        apply_fullscreen(window, present);
    static Audio audio;   /* large; keep it off the stack */
    memset(&audio, 0, sizeof audio);
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
    update_title(window, client, paused);

    SDL_UpdateTexture(screen.picture, NULL, client->pixels(client->ctx), display_width(display) * 4);
    while (running) {
        /* The actions of this iteration's key presses, then of pad inputs
         * newly held (sticks and triggers have no press events). */
        Action actions[64];
        int nactions = 0;
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;   /* the window's close button: at once */
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
                audio_frame(&audio, client, true);
                running = !confirm_quit(window, client->name);
                bindings_swallow_held_keys();   /* the Return that played on isn't Start */
                deadline = SDL_GetPerformanceCounter() + period;   /* the prompt's wait isn't owed */
                break;
            case ACT_FULLSCREEN:
                present->fullscreen = !present->fullscreen;
                apply_fullscreen(window, present);
                break;
            case ACT_SCALING:
                present->scaling = (Scaling)((present->scaling + 1) % SCALINGS);
                fprintf(stderr, "scaling: %s\n", present_scaling_name(present->scaling));
                break;
            case ACT_FILTER:
                present->filter = (Filter)((present->filter + 1) % FILTERS);
                fprintf(stderr, "filter: %s\n", present_filter_name(present->filter));
                break;
            case ACT_PIXELS:
                present->pixels = (PixelShape)((present->pixels + 1) % PIXEL_SHAPES);
                fprintf(stderr, "pixels: %s\n", present_pixel_shape_name(present->pixels));
                break;
            case ACT_PAUSE:
                paused = !paused;
                break;
            case ACT_QUICK_SAVE:
                if (client->quick_save)
                    client->quick_save(client->ctx);
                break;
            case ACT_QUICK_LOAD:
                if (client->quick_load) {
                    audio_frame(&audio, client, true);
                    client->quick_load(client->ctx);
                    SDL_UpdateTexture(screen.picture, NULL, client->pixels(client->ctx), display_width(display) * 4);
                }
                break;
            default:
                continue;
            }
            update_title(window, client, paused);
        }

        if (!running || client->done(client->ctx))
            break;
        bool fast = false;
        const char *halt = client->halt(client->ctx);
        if (halt && !halt_shown) {
            halt_shown = true;
            client->flush(client->ctx);
            update_title(window, client, paused);
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, client->name, halt, window);
        } else if (!halt) {
            halt_shown = false;
        }
        if (!paused && !halt) {
            /* Fast forward runs frames unpaced for up to a display frame. */
            Uint64 budget_end = SDL_GetPerformanceCounter() + period;
            do {
                client->step(client->ctx, bindings_buttons(pads.open, pads.count));
                if (client->halt(client->ctx) || client->done(client->ctx))
                    break;
                if (++frames_since_flush >= SRAM_FLUSH_FRAMES) {
                    frames_since_flush = 0;
                    client->flush(client->ctx);
                }
                fast = bindings_held(ACT_FAST_FORWARD, pads.open, pads.count, true);
            } while (fast && SDL_GetPerformanceCounter() < budget_end);
            if (fast)
                deadline = SDL_GetPerformanceCounter();
            SDL_UpdateTexture(screen.picture, NULL, client->pixels(client->ctx), display_width(display) * 4);
            update_title(window, client, paused);
        }
        /* Fast frames aren't played: the device stays silent until normal speed. */
        audio_frame(&audio, client, paused || fast || client->halt(client->ctx));
        int out_w, out_h;
        SDL_GetRendererOutputSize(sdl_renderer, &out_w, &out_h);
        screen_draw(&screen, display, present, out_w, out_h);
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

    int rc = client->flush(client->ctx);
    audio_close(&audio);
    while (pads.count)
        SDL_GameControllerClose(pads.open[--pads.count]);
    screen_close(&screen);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return rc != 0;
}
