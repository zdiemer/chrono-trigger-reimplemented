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
 * It is a measurement, not yet a player: it runs the program's frames
 * headless and says how far the program's C carries it -- the frame and the
 * routine where the next step is still only a machine entry -- and what it
 * reached for that a host doesn't have (an address outside work RAM, save RAM
 * and the cartridge). Exit status 0 when it ran every frame, 1 where the
 * program stopped short, 2 on bad arguments or files. This file is the driver:
 * the loop, the files and the report are its, as frontend/headless.c's are
 * the player's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "core/program.h"
#include "movie.h"
#include "data/assets.h"
#include "platform/native/host.h"
#include "port/state_host.h"
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

int main(int argc, char **argv)
{
    const char *rom_path = NULL, *sram_path = NULL, *pack_path = NULL, *movie_path = NULL;
    unsigned long frames = 600;
    unsigned scenes = 0;
    uint8_t scene[STATE_HOST_FIRST_SCENES], song[STATE_HOST_FIRST_SCENES];
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--rom") && v) rom_path = v, i++;
        else if (!strcmp(a, "--sram") && v) sram_path = v, i++;
        else if (!strcmp(a, "--assets") && v) pack_path = v, i++;
        else if (!strcmp(a, "--movie") && v) movie_path = v, i++;
        else if (!strcmp(a, "--frames") && v) frames = strtoul(v, NULL, 10), i++;
        else if (!strcmp(a, "--scene") && v && scenes < STATE_HOST_FIRST_SCENES) {
            char *rest;
            scene[scenes] = (uint8_t)strtol(v, &rest, 0);
            song[scenes] = *rest == ',' ? (uint8_t)strtol(rest + 1, NULL, 0) : 0;
            scenes++;
            i++;
        }
        else {
            fprintf(stderr, "usage: ct_native --rom ct.sfc [--assets pack] [--frames N] [--sram save.srm] [--movie in.ctm] "
                            "[--scene flags[,song]]\n");
            return 2;
        }
    }
    if (!rom_path) {
        fprintf(stderr, "ct_native: --rom is required\n");
        return 2;
    }
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
    long sram_len = 0;
    uint8_t *sram = sram_path ? read_file(sram_path, STATE_HOST_SRAM_BYTES, &sram_len) : NULL;
    if (sram_path && (!sram || sram_len != STATE_HOST_SRAM_BYTES)) {
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
    tm_host_start(&pf, &(TmHostProgram){ run_main_thread, run_vblank, run_line, g }, 1);
    int rc = 0;
    unsigned long frame = 0;
    while (frame < frames) {
        if (movie_path)
            in_host_set(&pf, 0, ct_movie_buttons(&movie, (uint32_t)frame));
        tm_host_frame(&pf);
        sound_native_frame(sound);
        frame++;
        uint32_t routine;
        const char *what;
        if (state_host_stopped(g, &routine, &what)) {
            fprintf(stderr, "frame %lu: the program's C goes no further: $%02X:%04X, %s, runs only on the machine\n",
                    frame, (unsigned)(routine >> 16), (unsigned)(routine & 0xFFFF), what);
            rc = 1;
            break;
        }
    }
    fprintf(stderr, "ct_native: %lu frame(s); the screen set up %u time(s), %s\n", frame,
            (unsigned)pf.video.screens_applied, pf.video.display.blank ? "blanked" : "showing");
    report_strays(g, &pf);
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
