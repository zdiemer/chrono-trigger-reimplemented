/* ct_native: the game with no emulator under it (docs/PLAYING.md, "The
 * native program").
 *
 *   ct_native --rom ct.sfc [--frames N] [--sram save.srm]
 *
 * It links game/'s program (core/program.c), the platform services' host
 * backends (runtime/platform/native/) and the coroutines (runtime/port/fiber.h),
 * and none of the CPU, the PPU, the APU, the code generated from the ROM or
 * game/registry.c. The game's state is held in the host's memory
 * (runtime/port/state_host.h) and its data read from the ROM file's bytes
 * (runtime/port/rom_image.h, the seam the asset pack replaces).
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

#include "core/program.h"
#include "platform/native/host.h"
#include "port/rom_image.h"
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
    uint32_t rom_first;
    unsigned long rom = rom_image_strays(&rom_first);
    if (rom)
        fprintf(stderr, "strays: %lu cartridge read(s) outside the image, the first at $%02X:%04X\n", rom,
                (unsigned)(rom_first >> 16), (unsigned)(rom_first & 0xFFFF));
}

int main(int argc, char **argv)
{
    const char *rom_path = NULL, *sram_path = NULL;
    unsigned long frames = 600;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--rom") && v) rom_path = v, i++;
        else if (!strcmp(a, "--sram") && v) sram_path = v, i++;
        else if (!strcmp(a, "--frames") && v) frames = strtoul(v, NULL, 10), i++;
        else {
            fprintf(stderr, "usage: ct_native --rom ct.sfc [--frames N] [--sram save.srm]\n");
            return 2;
        }
    }
    if (!rom_path) {
        fprintf(stderr, "ct_native: --rom is required\n");
        return 2;
    }
    long rom_len = 0, sram_len = 0;
    uint8_t *rom = read_file(rom_path, ROM_BYTES, &rom_len);
    if (!rom) {
        fprintf(stderr, "ct_native: cannot read %s\n", rom_path);
        return 2;
    }
    uint8_t *sram = sram_path ? read_file(sram_path, STATE_HOST_SRAM_BYTES, &sram_len) : NULL;
    if (sram_path && (!sram || sram_len != STATE_HOST_SRAM_BYTES)) {
        fprintf(stderr, "ct_native: %s is not an 8 KiB cartridge save\n", sram_path);
        return 2;
    }
    rom_image_set(rom, (uint32_t)rom_len);
    GameState *g = state_host_create(sram);
    static Platform pf;
    if (!g) {
        fprintf(stderr, "ct_native: out of memory\n");
        return 2;
    }

    pf.save.bytes = state_host_sram(g);
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
    free(rom);
    return rc;
}
