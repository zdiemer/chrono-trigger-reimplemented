/* ct_dev: runs the ROM on the development system (interpreter CPU and modeled
 * hardware) from an input movie, writing the same .cttr traces as ct_oracle so
 * the two can be diffed with tools/trace.py.
 *
 *   ct_dev --rom roms/ct-usa.sfc --frames 1800 [--movie m.ctm] [--trace out.cttr]
 *          [--shot-every N --shot-dir dir] [--coverage out.txt] [--status-every N]
 *          [--native off|shadow|on] [--calls out.txt] [--load-state in.state] [--save-state out.state]
 *          [--sram save.srm] [--wav out.wav] [--exclude name-prefix]... [--recomp off|on]
 *          [--recomp-yield clocks] [--insn-stats] [--io-log out.txt]
 *
 * --native shadow runs the original code and checks each call of a ported
 * routine against its native version; --native on runs the ported routines.
 * --calls writes every executed JSR/JSL/JSR(abs,X) call site and target, so the
 * disassembler's liveness analysis knows about callers it can't see statically.
 * --load-state resumes from a state saved by --save-state (same build only);
 * frame numbers, and so movie input, continue from the saved frame.
 * --sram starts with that cartridge save (8 KiB) in place of blank SRAM; a movie
 * recorded from a save names its CRC-32 and must be replayed with the same one.
 * --wav writes the DSP's output (32000 Hz stereo) over the frames run.
 * --recomp on (the default when the build generated blocks from this ROM)
 * registers the generated blocks after the ported routines (docs/RECOMP.md);
 * --recomp-yield sets how long a generated loop runs before letting time pass
 * in on mode. --insn-stats reports how many instructions the interpreter and
 * generated blocks executed. --io-log writes every hardware register access
 * counted by what made it (dev/io_log.h; meaningful with --native on).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io_log.h"
#include "media.h"
#include "movie.h"
#include "port/recomp_table.h"
#include "registry.h"
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

static IoLog *io_log;

static void on_line(void *ctx, Snes *snes, uint16_t line)
{
    render_line(ctx, &snes->ppu, line);
    if (io_log)
        io_log_note_hdma(io_log);
}

/* Executed (address, M, X) triples, for seeding the disassembler. */
#define COVERAGE_BITS 26   /* 24-bit address + M + X */

#define CALL_SET_BITS 20

typedef struct {
    uint8_t *seen;       /* bitmap over COVERAGE_BITS, or NULL */
    uint64_t *calls;     /* open-addressed set of site << 24 | target, or NULL */
    uint64_t insns;      /* instructions the interpreter executed */
    uint64_t ram_insns;  /* of those, code in RAM (banks $7E-$7F, or below $8000 in banks $00-$3F/$80-$BF) */
    Snes *snes;
} Coverage;

static void record_call(Coverage *cov, uint32_t site, uint32_t target)
{
    uint64_t key = (uint64_t)site << 24 | target | (uint64_t)1 << 63;
    size_t mask = ((size_t)1 << CALL_SET_BITS) - 1;
    for (size_t i = (size_t)(key * 0x9E3779B97F4A7C15ull >> 40) & mask;; i = (i + 1) & mask) {
        if (cov->calls[i] == key)
            return;
        if (cov->calls[i] == 0) {
            cov->calls[i] = key;
            return;
        }
    }
}

static void on_insn(void *ctx, uint32_t pc, uint8_t p, bool e)
{
    Coverage *cov = ctx;
    if (io_log)
        io_log_note_instruction(io_log, pc);
    bool m = e || (p & CPU_FLAG_M), x = e || (p & CPU_FLAG_X);
    cov->insns++;
    if ((pc >> 16 & 0xFE) == 0x7E || (!(pc & 0x400000) && (pc & 0xFFFF) < 0x8000))
        cov->ram_insns++;
    if (cov->seen) {
        uint32_t key = pc << 2 | (uint32_t)m << 1 | (uint32_t)x;
        cov->seen[key >> 3] |= (uint8_t)(1 << (key & 7));
    }
    if (cov->calls) {
        const SnesBus *bus = &cov->snes->bus;
        uint8_t op = snes_bus_peek(bus, pc);
        if (op == 0x20) {
            record_call(cov, pc, (pc & 0xFF0000) | snes_bus_peek(bus, pc + 1) | snes_bus_peek(bus, pc + 2) << 8);
        } else if (op == 0x22) {
            record_call(cov, pc, snes_bus_peek(bus, pc + 1) | snes_bus_peek(bus, pc + 2) << 8 |
                                     (uint32_t)snes_bus_peek(bus, pc + 3) << 16);
        } else if (op == 0xFC) {
            uint16_t ptr = (uint16_t)((snes_bus_peek(bus, pc + 1) | snes_bus_peek(bus, pc + 2) << 8) +
                                      cov->snes->cpu.x);
            uint32_t bank = pc & 0xFF0000;
            record_call(cov, pc, bank | snes_bus_peek(bus, bank | ptr) |
                                     snes_bus_peek(bus, bank | (uint16_t)(ptr + 1)) << 8);
        }
    }
}

static int write_calls(const Coverage *cov, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fprintf(f, "# executed calls: <site BB:AAAA> <target BB:AAAA>\n");
    for (size_t i = 0; i < (size_t)1 << CALL_SET_BITS; i++) {
        uint64_t k = cov->calls[i];
        if (!k)
            continue;
        uint32_t site = (uint32_t)(k >> 24) & 0xFFFFFF, target = (uint32_t)k & 0xFFFFFF;
        fprintf(f, "%02X:%04X %02X:%04X\n", site >> 16, site & 0xFFFF, target >> 16, target & 0xFFFF);
    }
    return fclose(f);
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
    const char *calls_path = NULL, *load_state = NULL, *save_state = NULL, *sram_path = NULL;
    const char *shot_dir = NULL, *wav_path = NULL, *io_log_path = NULL;
    NativeMode native_mode = NATIVE_OFF;
    bool recomp = true, insn_stats = false;
    long frames = -1, status_every = 0, shot_every = 0;
    const char *excludes[64];
    int nexcludes = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--insn-stats")) {
            insn_stats = true;
            continue;
        }
        if (!v) {
            fprintf(stderr, "missing value for %s\n", a);
            return 2;
        }
        if (!strcmp(a, "--rom")) rom_path = v;
        else if (!strcmp(a, "--frames")) frames = strtol(v, NULL, 10);
        else if (!strcmp(a, "--movie")) movie_path = v;
        else if (!strcmp(a, "--trace")) trace_path = v;
        else if (!strcmp(a, "--coverage")) coverage_path = v;
        else if (!strcmp(a, "--calls")) calls_path = v;
        else if (!strcmp(a, "--load-state")) load_state = v;
        else if (!strcmp(a, "--save-state")) save_state = v;
        else if (!strcmp(a, "--sram")) sram_path = v;
        else if (!strcmp(a, "--status-every")) status_every = strtol(v, NULL, 10);
        else if (!strcmp(a, "--shot-every")) shot_every = strtol(v, NULL, 10);
        else if (!strcmp(a, "--shot-dir")) shot_dir = v;
        else if (!strcmp(a, "--wav")) wav_path = v;
        else if (!strcmp(a, "--io-log")) io_log_path = v;
        else if (!strcmp(a, "--exclude") && nexcludes < 64) excludes[nexcludes++] = v;
        else if (!strcmp(a, "--recomp-yield")) native_set_recomp_yield((uint32_t)strtoul(v, NULL, 10));
        else if (!strcmp(a, "--recomp")) {
            if (!strcmp(v, "off")) recomp = false;
            else if (!strcmp(v, "on")) recomp = true;
            else {
                fprintf(stderr, "--recomp must be off or on\n");
                return 2;
            }
        }
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
                        "[--native off|shadow|on] [--calls out.txt] [--load-state f] [--save-state f] "
                        "[--sram save.srm] [--wav out.wav] [--exclude name-prefix]... [--recomp off|on] "
                        "[--recomp-yield clocks] [--insn-stats] [--io-log out.txt]\n");
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
    if (sram_path) {
        size_t sram_len;
        uint8_t *sram = read_file(sram_path, &sram_len);
        if (!sram || sram_len != SNES_SRAM_SIZE) {
            fprintf(stderr, "cannot read %s as a %d-byte SRAM file\n", sram_path, SNES_SRAM_SIZE);
            return 1;
        }
        memcpy(snes.bus.sram, sram, SNES_SRAM_SIZE);
        free(sram);
    }
    if (movie.has_sram && (!sram_path || ct_crc32(snes.bus.sram, SNES_SRAM_SIZE) != movie.sram_crc)) {
        fprintf(stderr, "%s starts from SRAM with CRC-32 %08X; pass that save with --sram\n", movie_path,
                (unsigned)movie.sram_crc);
        return 1;
    }
    if (load_state && snes_state_load(&snes, load_state) != 0) {
        fprintf(stderr, "cannot load state %s (missing, or saved by a different build)\n", load_state);
        return 1;
    }
    /* --exclude drops ported routines (and generated blocks, recomp_BBAAAA_...)
     * whose names start with a prefix, for narrowing down which one causes a
     * difference. Generated blocks are only registered for the ROM they were
     * generated from. */
    if (recomp && recomp_native_count > 0 && ct_crc32(image, image_len) != recomp_rom_crc) {
        fprintf(stderr, "warning: generated blocks are for ROM CRC-32 %08X, not this one; not using them\n",
                (unsigned)recomp_rom_crc);
        recomp = false;
    }
    int nrecomp = recomp ? recomp_native_count : 0;
    NativeEntry *table = malloc(sizeof *table * (size_t)(game_native_count + nrecomp + 1));
    int table_count = 0;
    for (int i = 0; i < game_native_count + nrecomp; i++) {
        const NativeEntry *e = i < game_native_count ? &game_natives[i] : &recomp_natives[i - game_native_count];
        bool excluded = false;
        for (int k = 0; k < nexcludes; k++)
            excluded |= strncmp(e->name, excludes[k], strlen(excludes[k])) == 0;
        if (!excluded)
            table[table_count++] = *e;
    }
    NativeRuntime *natives = native_create(table, table_count, native_mode);
    native_attach(natives, &snes);
    if (shot_dir)
        make_dir(shot_dir);

    Coverage cov = { .snes = &snes };
    if (coverage_path)
        cov.seen = calloc((size_t)1 << (COVERAGE_BITS - 3), 1);
    if (calls_path)
        cov.calls = calloc((size_t)1 << CALL_SET_BITS, sizeof *cov.calls);
    if (io_log_path)
        io_log = io_log_create(&snes);
    if (coverage_path || calls_path || insn_stats || io_log_path) {
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

    static ct_wav wav;
    static int16_t audio[DSP_OUT_FRAMES * 2];
    if (wav_path) {
        if (ct_wav_open(&wav, wav_path, DSP_SAMPLE_RATE) != 0) {
            fprintf(stderr, "cannot open %s\n", wav_path);
            return 1;
        }
        apu_take_samples(&snes.apu, audio, DSP_OUT_FRAMES);   /* left over in a loaded state */
    }

    uint8_t cgram[0x200];
    for (long frame = (long)snes.frame; frame < frames; frame++) {
        uint16_t pad = ct_movie_buttons(&movie, (uint32_t)frame);
        snes_run_frame(&snes, pad);
        if (wav_path)
            ct_wav_write(&wav, audio, apu_take_samples(&snes.apu, audio, DSP_OUT_FRAMES));

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
    if (save_state && natives && native_suspended(natives))
        fprintf(stderr, "warning: saving with native routines in progress; they won't resume from the state\n");
    if (save_state && snes_state_save(&snes, save_state) != 0) {
        fprintf(stderr, "cannot save state %s\n", save_state);
        rc = 1;
    }
    if (native_mode != NATIVE_OFF) {
        native_report(natives, stderr);
        if (native_failed(natives))
            rc = 1;
    }
    if (insn_stats) {
        uint64_t generated = native_generated_instructions();
        fprintf(stderr, "instructions: interpreted %llu (%llu in RAM), generated %llu (%.1f%% generated)\n",
                (unsigned long long)cov.insns, (unsigned long long)cov.ram_insns, (unsigned long long)generated,
                cov.insns + generated ? 100.0 * (double)generated / (double)(cov.insns + generated) : 0.0);
    }
    if (io_log && io_log_write(io_log, io_log_path, stderr) != 0) {
        fprintf(stderr, "cannot write %s\n", io_log_path);
        rc = 1;
    }
    io_log_destroy(io_log);
    native_destroy(natives);
    free(table);
    if (ct_trace_close(trace) != 0)
        rc = 1;
    if (wav_path) {
        if (snes.apu.dsp.out_dropped)
            fprintf(stderr, "warning: %u audio frames dropped\n", (unsigned)snes.apu.dsp.out_dropped);
        if (ct_wav_close(&wav) != 0) {
            fprintf(stderr, "cannot write %s\n", wav_path);
            rc = 1;
        }
    }
    if (coverage_path && write_coverage(&cov, coverage_path) != 0) {
        fprintf(stderr, "cannot write %s\n", coverage_path);
        rc = 1;
    }
    if (calls_path && write_calls(&cov, calls_path) != 0) {
        fprintf(stderr, "cannot write %s\n", calls_path);
        rc = 1;
    }
    free(cov.calls);
    ct_movie_free(&movie);
    free(cov.seen);
    free(rom);
    return rc;
}
