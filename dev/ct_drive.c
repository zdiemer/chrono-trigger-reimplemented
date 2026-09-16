/* ct_drive: the development system as a scriptable server, for authoring input
 * movies from game state instead of from timing (docs/MOVIES.md).
 *
 *   ct_drive <rom> [sram.srm]
 *
 * It runs the same machine as `ct_dev --native off`: the interpreter and the
 * modeled hardware, with no ported routines and no generated blocks, so a run
 * here and a `ct_dev --native off` replay of the same input reach the same
 * state (tests/test_movie_tools.py documents the 20000-frame check).
 *
 * Commands arrive one per line on stdin and answer with one line on stdout that
 * starts with "ok" or "err". Numbers are decimal unless noted.
 *
 *   run <n> <pad>           run n frames holding pad (JOY1 bit order, decimal)
 *   runw <n> <pad>          the same, and append the watched bytes as hex
 *   watch <addr:len> ...    set the byte ranges runw returns (hex addresses, up to 32)
 *   movie <path> <to>       run from the current frame to frame `to` with a .ctm's buttons
 *   peek <addr> <len>       hex dump of len bytes from the bus (addr in hex, e.g. 7E5E30)
 *   wram <path>             write the 128 KiB of work RAM to a file
 *   save <slot>             copy the machine into slot 0-4095 (in memory)
 *   load <slot>             restore a slot; drop <slot> frees one
 *   savefile <path>         write a save state (the format ct_dev --save-state writes)
 *   loadfile <path>         read one back
 *   shot <path>             write the last rendered frame as a PNG
 *   frame                   the current frame number
 *   poke <addr> <hex>       DIAGNOSTIC: write bytes into work RAM
 *   quit                    exit
 *
 * `poke` exists to test what a flag does; it makes the run unreproducible from
 * input alone, so a run that used it must not become a movie.
 *
 * Answers: run/runw/movie/save/load/savefile/loadfile/frame answer with the
 * frame number, peek and runw with hex bytes, the rest with "ok".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "media.h"
#include "movie.h"
#include "snes/render.h"
#include "snes/system.h"

#define SLOTS 4096
#define WATCHES 32

static Snes snes;
static Renderer renderer;
static Snes *slots[SLOTS];
static uint32_t watch_addr[WATCHES], watch_len[WATCHES];
static int nwatch;

static void on_line(void *ctx, Snes *s, uint16_t line)
{
    render_line(ctx, &s->ppu, line);
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
    if (n <= 0 || !data || fread(data, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(data);
        return NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return data;
}

/* Hooks aren't part of a state or a clone; put ours back after either. */
static void restore_hooks(void)
{
    snes.interpreter = cpu_step;
    snes.on_line = on_line;
    snes.on_line_ctx = &renderer;
}

static void print_watched(void)
{
    for (int k = 0; k < nwatch; k++)
        for (uint32_t i = 0; i < watch_len[k]; i++)
            printf("%02x", snes_bus_peek(&snes.bus, watch_addr[k] + i));
}

static void set_watches(const char *line)
{
    nwatch = 0;
    const char *p = line;
    while (*p && nwatch < WATCHES) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p || *p == '\n' || *p == '\r')
            break;
        char *end;
        uint32_t addr = (uint32_t)strtoul(p, &end, 16);
        if (*end != ':')
            break;
        uint32_t len = (uint32_t)strtoul(end + 1, &end, 10);
        watch_addr[nwatch] = addr;
        watch_len[nwatch] = len;
        nwatch++;
        p = end;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: ct_drive <rom> [sram.srm]\n");
        return 2;
    }
    size_t rom_len;
    uint8_t *rom = read_file(argv[1], &rom_len);
    if (!rom) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    size_t header = rom_len % 0x8000 == 0x200 ? 0x200 : 0;
    snes_init(&snes, rom + header, rom_len - header);
    render_init(&renderer);
    restore_hooks();
    snes_power_on(&snes);
    if (argc > 2) {
        size_t sram_len;
        uint8_t *sram = read_file(argv[2], &sram_len);
        if (!sram || sram_len != SNES_SRAM_SIZE) {
            fprintf(stderr, "cannot read %s as a %d-byte SRAM file\n", argv[2], SNES_SRAM_SIZE);
            free(sram);
            return 1;
        }
        memcpy(snes.bus.sram, sram, SNES_SRAM_SIZE);
        free(sram);
    }

    char line[4096];
    while (fgets(line, sizeof line, stdin)) {
        char cmd[32], a1[2048] = "", a2[256] = "";
        if (sscanf(line, "%31s %2047s %255s", cmd, a1, a2) < 1)
            continue;
        if (!strcmp(cmd, "run") || !strcmp(cmd, "runw")) {
            long n = strtol(a1, NULL, 10);
            uint16_t pad = (uint16_t)strtol(a2, NULL, 10);
            for (long i = 0; i < n; i++)
                snes_run_frame(&snes, pad);
            printf("ok %u", (unsigned)snes.frame);
            if (cmd[3] == 'w') {
                putchar(' ');
                print_watched();
            }
            putchar('\n');
        } else if (!strcmp(cmd, "watch")) {
            set_watches(line + strlen("watch"));
            printf("ok %d\n", nwatch);
        } else if (!strcmp(cmd, "movie")) {
            ct_movie movie = { 0 };
            if (ct_movie_load(&movie, a1) != 0) {
                printf("err movie\n");
            } else {
                long to = strtol(a2, NULL, 10);
                while ((long)snes.frame < to)
                    snes_run_frame(&snes, ct_movie_buttons(&movie, snes.frame));
                ct_movie_free(&movie);
                printf("ok %u\n", (unsigned)snes.frame);
            }
        } else if (!strcmp(cmd, "peek")) {
            uint32_t addr = (uint32_t)strtoul(a1, NULL, 16);
            long n = strtol(a2, NULL, 10);
            printf("ok ");
            for (long i = 0; i < n; i++)
                printf("%02x", snes_bus_peek(&snes.bus, addr + (uint32_t)i));
            putchar('\n');
        } else if (!strcmp(cmd, "poke")) {
            /* diagnostic: see the header comment */
            uint32_t addr = (uint32_t)strtoul(a1, NULL, 16);
            size_t n = strlen(a2) / 2;
            for (size_t i = 0; i < n; i++) {
                char byte[3] = { a2[2 * i], a2[2 * i + 1], 0 };
                snes.bus.wram[(addr + (uint32_t)i) & (SNES_WRAM_SIZE - 1)] = (uint8_t)strtoul(byte, NULL, 16);
            }
            printf("ok\n");
        } else if (!strcmp(cmd, "wram")) {
            FILE *f = fopen(a1, "wb");
            int ok = f && fwrite(snes.bus.wram, SNES_WRAM_SIZE, 1, f) == 1;
            if (f)
                fclose(f);
            printf(ok ? "ok\n" : "err write\n");
        } else if (!strcmp(cmd, "save") || !strcmp(cmd, "load") || !strcmp(cmd, "drop")) {
            long k = strtol(a1, NULL, 10);
            if (k < 0 || k >= SLOTS) {
                printf("err slot\n");
            } else if (cmd[0] == 's') {
                if (!slots[k])
                    slots[k] = malloc(sizeof snes);
                if (!slots[k]) {
                    printf("err memory\n");
                } else {
                    snes_clone(slots[k], &snes);
                    printf("ok %u\n", (unsigned)snes.frame);
                }
            } else if (cmd[0] == 'l') {
                if (!slots[k]) {
                    printf("err empty\n");
                } else {
                    snes_clone(&snes, slots[k]);
                    restore_hooks();
                    printf("ok %u\n", (unsigned)snes.frame);
                }
            } else {
                free(slots[k]);
                slots[k] = NULL;
                printf("ok\n");
            }
        } else if (!strcmp(cmd, "savefile")) {
            printf(snes_state_save(&snes, a1) == 0 ? "ok %u\n" : "err %u\n", (unsigned)snes.frame);
        } else if (!strcmp(cmd, "loadfile")) {
            int r = snes_state_load(&snes, a1);
            restore_hooks();
            printf(r == 0 ? "ok %u\n" : "err %u\n", (unsigned)snes.frame);
        } else if (!strcmp(cmd, "shot")) {
            printf(ct_png_write(a1, renderer.pixels, RENDER_WIDTH, RENDER_HEIGHT) == 0 ? "ok\n" : "err write\n");
        } else if (!strcmp(cmd, "frame")) {
            printf("ok %u\n", (unsigned)snes.frame);
        } else if (!strcmp(cmd, "quit")) {
            printf("ok\n");
            fflush(stdout);
            break;
        } else {
            printf("err unknown command\n");
        }
        fflush(stdout);
    }
    for (int i = 0; i < SLOTS; i++)
        free(slots[i]);
    free(rom);
    return 0;
}
