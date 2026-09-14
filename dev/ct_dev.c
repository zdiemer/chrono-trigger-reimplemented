/* ct_dev: runs the ROM on the development system (interpreter CPU and modeled
 * hardware) from an input movie, writing the same .cttr traces as ct_oracle so
 * the two can be diffed with tools/trace.py.
 *
 *   ct_dev --rom roms/ct-usa.sfc --frames 1800 [--movie m.ctm] [--trace out.cttr]
 *          [--shot-every N --shot-dir dir] [--coverage out.txt] [--status-every N]
 *          [--native off|shadow|on]
 *
 * --native shadow runs the original code and checks each call of a ported
 * routine against its native version; --native on runs the ported routines.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "media.h"
#include "movie.h"
#include "natives.h"
#include "snes/render.h"
#include "snes/system.h"
#include "trace.h"

#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define make_dir(p) mkdir(p, 0755)
#endif

static void on_line(void *ctx, Snes *snes, uint16_t line)
{
    render_line(ctx, &snes->ppu, line);
}

/* Executed (address, M, X) triples, for seeding the disassembler. */
#define COVERAGE_BITS 26   /* 24-bit address + M + X */

typedef struct {
    uint8_t *seen;   /* bitmap over COVERAGE_BITS */
} Coverage;

static void on_insn(void *ctx, uint32_t pc, uint8_t p, bool e)
{
    Coverage *cov = ctx;
    bool m = e || (p & CPU_FLAG_M), x = e || (p & CPU_FLAG_X);
    uint32_t key = pc << 2 | (uint32_t)m << 1 | (uint32_t)x;
    cov->seen[key >> 3] |= (uint8_t)(1 << (key & 7));
}

static int write_coverage(const Coverage *cov, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fprintf(f, "# executed instructions: BB:AAAA m x (8 or 16)\n");
    size_t n = (size_t)1 << (COVERAGE_BITS - 3);
    for (size_t i = 0; i < n; i++) {
        if (!cov->seen[i])
            continue;
        for (int b = 0; b < 8; b++) {
            if (!(cov->seen[i] & (1 << b)))
                continue;
            uint32_t key = (uint32_t)(i << 3 | b);
            uint32_t pc = key >> 2;
            fprintf(f, "%02X:%04X %d %d\n", pc >> 16, pc & 0xFFFF, (key & 2) ? 8 : 16, (key & 1) ? 8 : 16);
        }
    }
    return fclose(f);
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

int main(int argc, char **argv)
{
    const char *rom_path = NULL, *movie_path = NULL, *trace_path = NULL, *coverage_path = NULL;
    const char *shot_dir = NULL;
    NativeMode native_mode = NATIVE_OFF;
    long frames = -1, status_every = 0, shot_every = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!v) {
            fprintf(stderr, "missing value for %s\n", a);
            return 2;
        }
        if (!strcmp(a, "--rom")) rom_path = v;
        else if (!strcmp(a, "--frames")) frames = strtol(v, NULL, 10);
        else if (!strcmp(a, "--movie")) movie_path = v;
        else if (!strcmp(a, "--trace")) trace_path = v;
        else if (!strcmp(a, "--coverage")) coverage_path = v;
        else if (!strcmp(a, "--status-every")) status_every = strtol(v, NULL, 10);
        else if (!strcmp(a, "--shot-every")) shot_every = strtol(v, NULL, 10);
        else if (!strcmp(a, "--shot-dir")) shot_dir = v;
        else if (!strcmp(a, "--native")) {
            if (!strcmp(v, "off")) native_mode = NATIVE_OFF;
            else if (!strcmp(v, "shadow")) native_mode = NATIVE_SHADOW;
            else if (!strcmp(v, "on")) native_mode = NATIVE_ON;
            else {
                fprintf(stderr, "--native must be off, shadow or on\n");
                return 2;
            }
        }
        else {
            fprintf(stderr, "unknown argument %s\n", a);
            return 2;
        }
        i++;
    }
    if (!rom_path || frames < 0 || (shot_every > 0 && !shot_dir)) {
        fprintf(stderr, "usage: ct_dev --rom <rom> --frames N [--movie m.ctm] [--trace out.cttr] "
                        "[--shot-every N --shot-dir dir] [--coverage out.txt] [--status-every N] "
                        "[--native off|shadow|on]\n");
        return 2;
    }

    size_t rom_len;
    uint8_t *rom = read_file(rom_path, &rom_len);
    if (!rom) {
        fprintf(stderr, "cannot read %s\n", rom_path);
        return 1;
    }
    size_t header = rom_len % 0x8000 == 0x200 ? 0x200 : 0;
    const uint8_t *image = rom + header;
    size_t image_len = rom_len - header;

    ct_movie movie = { 0 };
    if (movie_path && ct_movie_load(&movie, movie_path) != 0)
        return 1;

    static Snes snes;   /* large; keep it off the stack */
    static Renderer renderer;
    snes_init(&snes, image, image_len);
    render_init(&renderer);
    snes.on_line = on_line;
    snes.on_line_ctx = &renderer;
    snes_power_on(&snes);
    NativeRuntime *natives = native_create(game_natives, game_native_count, native_mode);
    native_attach(natives, &snes);
    if (shot_dir)
        make_dir(shot_dir);

    Coverage cov = { 0 };
    if (coverage_path) {
        cov.seen = calloc((size_t)1 << (COVERAGE_BITS - 3), 1);
        snes.cpu.on_insn = on_insn;
        snes.cpu.on_insn_ctx = &cov;
    }

    static const ct_region_desc regions[] = {
        { CT_REGION_WRAM, "WRAM", SNES_WRAM_SIZE },
        { CT_REGION_VRAM, "VRAM", 0x10000 },
        { CT_REGION_SRAM, "SRAM", SNES_SRAM_SIZE },
        { CT_REGION_CGRAM, "CGRAM", 0x200 },
        { CT_REGION_OAM, "OAM", 0x220 },
        { CT_REGION_WRAMW, "WRAMW", SNES_WRAM_SIZE },
    };
    static uint8_t wram_written[SNES_WRAM_SIZE];
    snes.bus.wram_written = wram_written;
    ct_trace_writer *trace = NULL;
    if (trace_path) {
        trace = ct_trace_open(trace_path, ct_crc32(image, image_len), "ct_dev; interpreter", regions,
                              (int)(sizeof regions / sizeof regions[0]));
        if (!trace) {
            fprintf(stderr, "cannot open %s\n", trace_path);
            return 1;
        }
    }

    uint8_t cgram[0x200];
    for (long frame = 0; frame < frames; frame++) {
        uint16_t pad = ct_movie_buttons(&movie, (uint32_t)frame);
        snes_run_frame(&snes, pad);

        if (trace) {
            for (int i = 0; i < 256; i++) {
                cgram[i * 2] = snes.ppu.cgram[i] & 0xFF;
                cgram[i * 2 + 1] = snes.ppu.cgram[i] >> 8;
            }
            const uint8_t *data[] = { snes.bus.wram, snes.ppu.vram, snes.bus.sram, cgram, snes.ppu.oam,
                                      wram_written };
            uint64_t fb_hash = ct_framebuffer_hash(renderer.pixels, RENDER_WIDTH, RENDER_HEIGHT);
            if (ct_trace_write_frame(trace, (uint32_t)frame, pad, data, RENDER_WIDTH, RENDER_HEIGHT, fb_hash) != 0) {
                fprintf(stderr, "error writing trace\n");
                return 1;
            }
        }
        if (shot_every > 0 && frame % shot_every == 0) {
            char path[1024];
            snprintf(path, sizeof path, "%s/frame_%06ld.png", shot_dir, frame);
            if (ct_png_write(path, renderer.pixels, RENDER_WIDTH, RENDER_HEIGHT) != 0)
                fprintf(stderr, "warning: cannot write %s\n", path);
        }
        if (status_every > 0 && frame % status_every == 0) {
            fprintf(stderr, "frame %6ld  pc %02X:%04X  apu %s pc %04X  spc cycles %llu\n", frame, snes.cpu.pb,
                    snes.cpu.pc, snes.apu.phase == APU_DRIVER ? "driver" : "ipl", snes.apu.spc.pc,
                    (unsigned long long)snes.apu.spc_cycles);
        }
    }

    int rc = 0;
    if (native_mode != NATIVE_OFF) {
        native_report(natives, stderr);
        if (native_failed(natives))
            rc = 1;
    }
    native_destroy(natives);
    if (ct_trace_close(trace) != 0)
        rc = 1;
    if (coverage_path && write_coverage(&cov, coverage_path) != 0) {
        fprintf(stderr, "cannot write %s\n", coverage_path);
        rc = 1;
    }
    ct_movie_free(&movie);
    free(cov.seen);
    free(rom);
    return rc;
}
