/* ct_native: the game with no emulator under it (docs/PLAYING.md, "The
 * native program").
 *
 *   ct_native --rom ct.sfc [--assets pack] [--frames N] [--sram save.srm] [--movie in.ctm]
 *             [--scene flags[,song]]...
 *
 * --scene starts the game in the scene engine after the power-on, with the
 * flags an event hands it ($24 Lavos falling, $26 the Epoch's dive, $2A to
 * $2F the endings and the staff roll) and the song the field would have had
 * playing, and goes on to the field after it: the story scenes a movie's
 * presses don't bring the native program to. Given again, the scenes run one
 * after another (up to four), as an ending's do, each taking up what the one
 * before left.
 *
 * It links game/'s program (core/program.c), the platform services' host
 * backends (runtime/platform/native/) and the coroutines (runtime/port/fiber.h),
 * and none of the CPU, the PPU, the APU, the code generated from the ROM or
 * game/registry.c. The game's state is held in the host's memory
 * (runtime/port/state_host.h) and its data read from the asset pack
 * (frontend/assets.h; game/data/assets.h, the store): <rom name>.assets beside
 * the ROM unless --assets names it, imported from the ROM where there is none
 * or it is of another catalogue. The ROM is read only to import it, and isn't
 * kept.
 *
 * It plays in the shared SDL window, or runs headless measurements with
 * --frames. It says how far the program's C carries it -- the frame and the
 * routine where the next step is still only a machine entry -- and what it
 * reached for that a host doesn't have (an address outside work RAM, save RAM
 * and the cartridge), and whether its picture, drawn every frame by the native
 * renderer, ends showing (tools/native_check.py holds the library to that:
 * reaching the last frame says nothing of the picture). --shot-every writes
 * frames as PNGs, --locations the places the run enters, --end-state work
 * RAM and save RAM at the end. Exit status 0 when it ran every frame, 1 where the
 * program stopped short, 2 on bad arguments or files, 3 on a fatal machine
 * entry. This file is the driver:
 * the loop, the files and the report are its, as frontend/headless.c's are
 * the player's. */
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "file_store.h"
#include "menu/saves.h"
#include "core/program.h"
#include "media.h"
#include "outcome_picture.h"
#include "movie.h"
#include "data/assets.h"
#include "platform/native/host.h"
#include "platform/native/render.h"
#include "port/state_host.h"
#include "port/no_machine.h"
#ifdef CT_HAVE_SDL
#define SDL_MAIN_HANDLED
#include "window.h"
#include "bindings.h"
#endif
#include "vars.h"
#include "sound_native.h"

enum { ROM_BYTES = 0x400000 };

static uint8_t *read_file(const char *path, long want, long *got)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    uint8_t *bytes = malloc((size_t)want);
    long n = bytes ? (long)fread(bytes, 1, (size_t)want, f) : 0;
    fclose(f);
    if (!bytes || n <= 0) {
        free(bytes);
        return NULL;
    }
    *got = n;
    return bytes;
}

static uint8_t read_game(void *ctx, uint32_t addr) { return state_host_read8(ctx, addr); }
static void write_game(void *ctx, uint32_t addr, uint8_t v) { state_host_write8(ctx, addr, v); }

static void run_main_thread(Platform *pf, void *ctx) { program_main_thread(ctx, pf); }
static void run_vblank(Platform *pf, void *ctx) { program_vblank(ctx, pf); }
static void run_line(Platform *pf, void *ctx) { program_line(ctx, pf); }
static uint32_t take_owed(void *ctx) { return state_host_take_owed(ctx); }

/* The picture each frame, drawn from the video service's state by the native
 * renderer, and how long it has been dark: nothing on it but black, which is
 * what a screen left blanked or at brightness 0 shows, and what a step that
 * draws nothing shows too. A movie whose picture goes dark and stays dark has
 * gone where the reference play didn't, which reaching the last frame doesn't
 * say. */
enum { DARK_LEVEL = 0x10 };
typedef struct {
    VidRenderer *renderer;
    uint32_t *pixels;
    unsigned long dark_since;    /* the frame the current dark run began; 0 while showing */
    unsigned long longest_from, longest;
} Picture;

static bool picture_is_dark(const uint32_t *px)
{
    for (unsigned i = 0; i < VID_RENDER_WIDTH * VID_RENDER_HEIGHT; i++) {
        uint32_t c = px[i];
        if (((c >> 16) & 0xFF) > DARK_LEVEL || ((c >> 8) & 0xFF) > DARK_LEVEL || (c & 0xFF) > DARK_LEVEL)
            return false;
    }
    return true;
}

static void picture_frame(Picture *p, Platform *pf, unsigned long frame)
{
    vid_render_frame(p->renderer, vid_host_picture(pf), p->pixels, NULL);
    if (!picture_is_dark(p->pixels)) {
        p->dark_since = 0;
        return;
    }
    if (!p->dark_since)
        p->dark_since = frame;
    if (frame - p->dark_since + 1 > p->longest) {
        p->longest = frame - p->dark_since + 1;
        p->longest_from = p->dark_since;
    }
}

/* --end-state: work RAM, then save RAM, as ct_dev --end-state writes them, for
 * tools/native_check.py to compare with the reference play's. */
enum { WORK_RAM = 0x7E0000, WORK_RAM_BYTES = 0x20000 };
enum { LOCATION_BITS = 0x1FF };   /* location_id's location number */
static int write_end_state(const char *path, GameState *g)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    for (uint32_t i = 0; i < WORK_RAM_BYTES; i++)
        fputc(state_host_read8(g, WORK_RAM + i), f);
    fwrite(state_host_sram(g), 1, STATE_HOST_SRAM_BYTES, f);
    return fclose(f) == 0 ? 0 : -1;
}

static void report_strays(const GameState *g, const Platform *pf)
{
    uint32_t first[STATE_HOST_STRAYS_KEPT];
    unsigned kept;
    unsigned long n = state_host_strays(g, first, &kept);
    if (n) {
        fprintf(stderr, "strays: %lu access(es) outside the host's memory, the first at", n);
        for (unsigned i = 0; i < kept; i++)
            fprintf(stderr, " $%02X:%04X", (unsigned)(first[i] >> 16), (unsigned)(first[i] & 0xFFFF));
        fprintf(stderr, "\n");
    }
    const VidHost *v = &pf->video;
    if (v->escapes)
        fprintf(stderr, "escapes: %lu video call(s) only the SNES has a meaning for, the first at $%02X:%04X\n",
                (unsigned long)v->escapes, (unsigned)(v->first_escape >> 16), (unsigned)(v->first_escape & 0xFFFF));
    const AssetFault *out = asset_outside_reads();
    if (out->count)
        fprintf(stderr, "strays: %lu cartridge read(s) outside the catalogue of resources, the first at $%02X:%04X\n",
                out->count, (unsigned)(out->first >> 16), (unsigned)(out->first & 0xFFFF));
}

/* <rom name>.assets beside the ROM. */
static void pack_beside(char *out, size_t n, const char *rom_path)
{
    const char *dot = strrchr(rom_path, '.');
    const char *slash = strrchr(rom_path, '/'), *back = strrchr(rom_path, '\\');
    if (back > slash)
        slash = back;
    int stem = dot && (!slash || dot > slash) ? (int)(dot - rom_path) : (int)strlen(rom_path);
    snprintf(out, n, "%.*s.assets", stem, rom_path);
}

/* The asset pack's bytes, read from `path` or, where there is none or it is
 * of another catalogue, imported from the ROM and written there. The pack
 * opened into *pack, or NULL after saying why. */
static uint8_t *load_pack(const char *path, const char *rom_path, AssetPack **pack)
{
    long len = 0;
    const char *why = "";
    uint8_t *bytes = read_file(path, 0x1000000, &len);
    *pack = bytes ? asset_pack_open(bytes, (size_t)len, &why) : NULL;
    if (*pack)
        return bytes;
    if (bytes)
        fprintf(stderr, "ct_native: %s: %s; importing it again\n", path, why);
    free(bytes);
    long rom_len = 0;
    uint8_t *rom = read_file(rom_path, ROM_BYTES + 0x200, &rom_len);
    if (!rom) {
        fprintf(stderr, "ct_native: cannot read %s\n", rom_path);
        return NULL;
    }
    size_t size, pack_len = 0;
    const uint8_t *image = asset_rom_image(rom, (size_t)rom_len, &size);
    bytes = asset_pack_import(image, size, &pack_len, &why);
    free(rom);
    FILE *f = bytes ? fopen(path, "wb") : NULL;
    int written = f && fwrite(bytes, 1, pack_len, f) == pack_len;
    if (f && fclose(f) != 0)
        written = 0;
    if (!written) {
        fprintf(stderr, "ct_native: cannot import %s: %s\n", path, bytes ? "it can't be written" : why);
        free(bytes);
        return NULL;
    }
    fprintf(stderr, "ct_native: imported %u resources from the ROM into %s\n", (unsigned)asset_catalogue_count, path);
    *pack = asset_pack_open(bytes, pack_len, &why);
    if (!*pack) {
        free(bytes);
        return NULL;
    }
    return bytes;
}

/* A byte written out whole, in any base strtol takes, ending at `end`. */
static bool byte_from(const char *text, char **end, uint8_t *out)
{
    long v = strtol(text, end, 0);
    if (*end == text || v < 0 || v > 0xFF)
        return false;
    *out = (uint8_t)v;
    return true;
}

/* --scene's flags, and the song after a comma (0, none, where there is none). */
static bool scene_and_song(const char *text, uint8_t *scene, uint8_t *song)
{
    char *rest;
    if (!byte_from(text, &rest, scene))
        return false;
    *song = 0;
    if (*rest == '\0')
        return true;
    return *rest == ',' && byte_from(rest + 1, &rest, song) && *rest == '\0';
}

/* This desktop driver owns the movie, file names and diagnostics in both
 * modes. Its step runs exactly one frame; the SDL driver owns the live loop. */
typedef struct {
    GameState *game;
    Platform *pf;
    SoundNative *sound;
    Picture *picture;
    ct_movie *movie;
    const char *sram_path, *shot_dir;
    unsigned long frame, frames, shot_every;
    bool locations, outcomes, persist, save_pending;
    uint16_t location;
    uint8_t saved[STATE_HOST_SRAM_BYTES];
    int result;
} NativeDriver;

/* saves_save_game writes this marker after the entire slot and its checksum.
 * Metadata clears on a fresh start never write it. The driver persists at
 * frame end, after the save routine's remaining completion count update. */
static void native_save_written(void *ctx, uint16_t offset)
{
    NativeDriver *d = ctx;
    if (offset == SAVES_LAST_SLOT)
        d->save_pending = true;
}

static int native_flush(void *ctx)
{
    NativeDriver *d = ctx;
    if (!d->persist || !d->save_pending)
        return 0;
    const uint8_t *bytes = sv_host_bytes(d->pf, NULL);
    if (!memcmp(bytes, d->saved, sizeof d->saved)) {
        d->save_pending = false;
        return 0;
    }
    if (file_store_replace(d->sram_path, bytes, sizeof d->saved) != 0) {
        fprintf(stderr, "ct_native: cannot write cartridge save %s\n", d->sram_path);
        d->result = 2;
        return 1;
    }
    memcpy(d->saved, bytes, sizeof d->saved);
    d->save_pending = false;
    fprintf(stderr, "ct_native: saved %s at frame %lu\n", d->sram_path, d->frame);
    return 0;
}

static void machine_failure(void *ctx, const char *routine)
{
    NativeDriver *d = ctx;
    fprintf(stderr, "ct_native: frame %lu: %s reached, which only the machine has\n", d->frame + 1, routine);
    fflush(stderr);
}

static void native_step(void *ctx, uint16_t buttons)
{
    NativeDriver *d = ctx;
    uint16_t second = 0;
    /* A movie owns input through its last event; live play then takes over. */
    if (d->movie && d->frame < d->movie->length) {
        buttons = ct_movie_buttons(d->movie, (uint32_t)d->frame);
        second = ct_movie_buttons2(d->movie, (uint32_t)d->frame);
    }
    in_host_set(d->pf, 0, buttons);
    in_host_set(d->pf, 1, second);
    tm_host_frame(d->pf);
    sound_native_frame(d->sound);
    d->frame++;
    picture_frame(d->picture, d->pf, d->frame);
    if (d->outcomes && outcome_picture_due(d->frame, d->frames))
        outcome_picture(d->picture->pixels, VID_RENDER_WIDTH, VID_RENDER_HEIGHT, d->frame);
    if (d->locations && location_id(d->game) != d->location) {
        d->location = location_id(d->game);
        fprintf(stderr, "location: frame %lu $%03X\n", d->frame, d->location & LOCATION_BITS);
    }
    if (d->shot_every && d->frame % d->shot_every == 0) {
        char path[1024];
        snprintf(path, sizeof path, "%s/frame_%06lu.png", d->shot_dir, d->frame);
        if (ct_png_write(path, d->picture->pixels, VID_RENDER_WIDTH, VID_RENDER_HEIGHT) != 0)
            fprintf(stderr, "ct_native: cannot write %s\n", path);
    }
    uint32_t routine;
    const char *what;
    if (state_host_stopped(d->game, &routine, &what)) {
        fprintf(stderr, "frame %lu: the program's C goes no further: $%02X:%04X, %s, runs only on the machine\n",
                d->frame, (unsigned)(routine >> 16), (unsigned)(routine & 0xFFFF), what);
        d->result = 1;
    }
    /* Persist only complete frames, immediately after the game saves. */
    native_flush(d);
}

static bool native_done(void *ctx)
{
    NativeDriver *d = ctx;
    return d->result || d->frame >= d->frames;
}

#ifdef CT_HAVE_SDL
static const uint32_t *native_pixels(void *ctx) { return ((NativeDriver *)ctx)->picture->pixels; }
static size_t native_audio(void *ctx, int16_t *out, size_t frames) { return sound_native_take_audio(ctx, out, frames); }
static void native_discard(void *ctx) { sound_native_discard_audio(ctx); }
static const char *native_halt(void *ctx) { (void)ctx; return NULL; } /* stops are printed, then exit 1 */
static bool native_fast(void *ctx) { (void)ctx; return false; }
static void native_title(void *ctx, bool paused, char *out, size_t size)
{
    NativeDriver *d = ctx;
    snprintf(out, size, "ct_native - frame %lu%s%s", d->frame, paused ? " [paused]" : "",
             d->movie && d->frame < d->movie->length ? " [movie]" : "");
}
#endif

static void usage(FILE *out)
{
    fprintf(out, "usage: ct_native --rom ct.sfc [--assets pack] [--frames N] [--sram save.srm] [--movie in.ctm]\n"
                 "                 [--headless | --window] [--scale N] [--fullscreen] [--mute]\n"
                 "                 [--shot-every N --shot-dir DIR] [--end-state out.bin] [--locations] [--outcomes] [--scene flags[,song]]...\n"
                 "--frames selects headless unless --window is given; --headless defaults to 600 frames.\n"
                 "Window play writes cartridge saves; headless runs only read them. No quick save/load.\n");
#ifdef CT_HAVE_SDL
    window_print_controls(out);
#else
    fprintf(out, "Built without SDL: headless only.\n");
#endif
}

static bool count_from(const char *text, unsigned long *out)
{
    char *end;
    errno = 0;
    if (*text < '0' || *text > '9')
        return false;
    *out = strtoul(text, &end, 10);
    return !errno && !*end;
}

int main(int argc, char **argv)
{
    const char *rom_path = NULL, *sram_path = NULL, *pack_path = NULL, *movie_path = NULL, *shot_dir = NULL;
    const char *end_state = NULL;
    unsigned long frames = 600, shot_every = 0, scale = 3;
    bool frames_given = false, fullscreen = false, mute = false;
    int display_mode = 0;   /* auto, headless, window */
#ifdef CT_HAVE_SDL
    SDL_SetMainReady();
    bindings_reset();
    char bind_error[80];
    bindings_set("F5 = None", bind_error, sizeof bind_error);
    bindings_set("F7 = None", bind_error, sizeof bind_error);
#endif
    bool locations = false, outcomes = false;
    unsigned scenes = 0;
    uint8_t scene[STATE_HOST_FIRST_SCENES], song[STATE_HOST_FIRST_SCENES];
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--rom") && v) rom_path = v, i++;
        else if (!strcmp(a, "--sram") && v) sram_path = v, i++;
        else if (!strcmp(a, "--assets") && v) pack_path = v, i++;
        else if (!strcmp(a, "--movie") && v) movie_path = v, i++;
        else if (!strcmp(a, "--frames") && v && count_from(v, &frames)) frames_given = true, i++;
        else if (!strcmp(a, "--headless")) display_mode = 1;
        else if (!strcmp(a, "--window")) display_mode = 2;
        else if (!strcmp(a, "--scale") && v && count_from(v, &scale) && scale >= 1 && scale <= 10) i++;
        else if (!strcmp(a, "--fullscreen")) fullscreen = true;
        else if (!strcmp(a, "--mute")) mute = true;
        else if (!strcmp(a, "--help")) { usage(stdout); return 0; }
        else if (!strcmp(a, "--shot-every") && v && count_from(v, &shot_every)) i++;
        else if (!strcmp(a, "--shot-dir") && v) shot_dir = v, i++;
        else if (!strcmp(a, "--end-state") && v) end_state = v, i++;
        else if (!strcmp(a, "--locations")) locations = true;
        else if (!strcmp(a, "--outcomes")) outcomes = true;
        else if (!strcmp(a, "--scene") && v && scenes < STATE_HOST_FIRST_SCENES && scene_and_song(v, &scene[scenes], &song[scenes]))
            scenes++, i++;
        else {
            usage(stderr);
            return 2;
        }
    }
    if (!rom_path || (shot_every && !shot_dir)) {
        fprintf(stderr, "ct_native: %s\n", rom_path ? "--shot-every needs --shot-dir" : "--rom is required");
        return 2;
    }
    bool windowed = display_mode == 2 || (display_mode == 0 && !frames_given);
#ifndef CT_HAVE_SDL
    if (display_mode == 2) {
        fprintf(stderr, "ct_native: --window needs a build with SDL2\n");
        return 2;
    }
    windowed = false;
    (void)fullscreen;
    (void)mute;
#endif
    if (display_mode == 1)
        windowed = false;
    if (windowed && !frames_given)
        frames = ULONG_MAX;
    static char beside[1024];
    if (!pack_path) {
        pack_beside(beside, sizeof beside, rom_path);
        pack_path = beside;
    }
    AssetPack *pack;
    uint8_t *pack_bytes = load_pack(pack_path, rom_path, &pack);
    if (!pack_bytes)
        return 2;
    ct_movie movie;
    if (movie_path && ct_movie_load(&movie, movie_path) != 0)
        return 2;
    static AssetStore store;
    asset_pack_store(pack, &store);
    assets_use(&store);
    static char default_sram[1024];
    if (windowed && !sram_path) {
        pack_beside(default_sram, sizeof default_sram, rom_path);
        char *ext = strrchr(default_sram, '.');
        if (ext)
            strcpy(ext, ".srm");
        sram_path = default_sram;
    }
    long sram_len = 0;
    errno = 0;
    uint8_t *sram = sram_path ? read_file(sram_path, STATE_HOST_SRAM_BYTES + 1, &sram_len) : NULL;
    bool new_save = windowed && !sram && errno == ENOENT;
    if (sram_path && !new_save && (!sram || sram_len != STATE_HOST_SRAM_BYTES)) {
        fprintf(stderr, "ct_native: %s is not an 8 KiB cartridge save\n", sram_path);
        return 2;
    }
    GameState *g = state_host_create(sram);
    static Platform pf;
    if (!g) {
        fprintf(stderr, "ct_native: out of memory\n");
        return 2;
    }

    pf.save.bytes = state_host_sram(g);
    for (unsigned n = 0; n < scenes; n++)
        program_starts_in_a_scene(g, scene[n], song[n]);
    vid_host_start(&pf, read_game, write_game, g);
    SoundNative *sound = sound_native_create(g, &pf);
    if (!sound) {
        fprintf(stderr, "ct_native: out of memory\n");
        return 2;
    }
    static uint32_t pixels[VID_RENDER_WIDTH * VID_RENDER_HEIGHT];
    Picture picture = { vid_render_create(), pixels, 0, 0, 0 };
    if (!picture.renderer) {
        fprintf(stderr, "ct_native: out of memory\n");
        return 2;
    }
    tm_host_start(&pf, &(TmHostProgram){ run_main_thread, run_vblank, run_line, g, take_owed }, 1);
    NativeDriver driver = {
        .game = g, .pf = &pf, .sound = sound, .picture = &picture,
        .movie = movie_path ? &movie : NULL, .sram_path = sram_path,
        .shot_dir = shot_dir, .frames = frames, .shot_every = shot_every,
        .locations = locations, .outcomes = outcomes, .persist = windowed, .location = 0xFFFF
    };
    memcpy(driver.saved, state_host_sram(g), sizeof driver.saved);
    pf.save.ctx = &driver;
    pf.save.written = native_save_written;
    no_machine_report_with(machine_failure, &driver);
    int rc = 0;
#ifdef CT_HAVE_SDL
    if (windowed) {
        WindowOptions options = {
            .scale = (int)scale, .fullscreen = fullscreen, .mute = mute,
            .display = { 8, 8, 4.0 / 3.0 * VID_RENDER_HEIGHT / VID_RENDER_WIDTH }
        };
        WindowClient client = {
            .ctx = &driver, .name = "ct_native", .step = native_step, .pixels = native_pixels,
            .audio = { sound, native_audio, native_discard }, .title = native_title,
            .halt = native_halt, .done = native_done, .fast = native_fast, .flush = native_flush
        };
        rc = window_run(&client, &options);
    } else
#endif
    {
        while (!native_done(&driver)) {
            native_step(&driver, 0);
            sound_native_discard_audio(sound);
        }
    }
    if (driver.result)
        rc = driver.result;
    unsigned long frame = driver.frame;
    no_machine_report_with(NULL, NULL);
    fprintf(stderr, "ct_native: %lu frame(s); the screen set up %u time(s), %s\n", frame,
            (unsigned)pf.video.screens_applied, pf.video.display.blank ? "blanked" : "showing");
    fprintf(stderr, "time: %u lag frame(s), the main thread's work owed the original's time\n",
            (unsigned)tm_host_lag_frames(&pf));
    if (picture.dark_since)
        fprintf(stderr, "picture: dark since frame %lu (%lu frame(s), to the end)\n", picture.dark_since,
                frame - picture.dark_since + 1);
    else
        fprintf(stderr, "picture: showing at the end\n");
    if (picture.longest)
        fprintf(stderr, "picture: longest dark run %lu frame(s), from frame %lu\n", picture.longest, picture.longest_from);
    report_strays(g, &pf);
    if (end_state && write_end_state(end_state, g) != 0) {
        fprintf(stderr, "ct_native: cannot write %s\n", end_state);
        rc = 2;
    }
    vid_render_destroy(picture.renderer);
    sound_native_destroy(sound);
    vid_host_stop(&pf);
    state_host_destroy(g);
    free(sram);
    if (movie_path)
        ct_movie_free(&movie);
    assets_use(NULL);
    asset_pack_close(pack);
    free(pack_bytes);
    return rc;
}
