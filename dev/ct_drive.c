/* ct_drive: the development system as a scriptable server, for authoring input
 * movies from game state instead of from timing, and for testing the build
 * that ships with the route scripts that do (docs/MOVIES.md).
 *
 *   ct_drive [--native off|shadow|on] [--recomp off|on|only] <rom> [sram.srm]
 *
 * --native and --recomp mean what they mean to ct_play (frontend/natives.h
 * chooses the entries for both). The default, --native off, is the machine of
 * `ct_dev --native off`: the interpreter and the modeled hardware, with no
 * ported routines and no generated blocks, so a run here and a `ct_dev --native
 * off` replay of the same input reach the same state
 * (tools/movies/check_drive.py). --native on --recomp on is the mode the player
 * ships in: hand ports where they exist, generated code elsewhere, and time
 * that drifts from the original's where the ports estimate it, which is why
 * only a route can test it (a movie's presses land on the wrong frames).
 * --recomp defaults to on.
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
 *   native                  the native runtime's bookkeeping (see below)
 *   poke <addr> <hex>       DIAGNOSTIC: write bytes into work RAM
 *   quit                    exit
 *
 * `poke` exists to test what a flag does; it makes the run unreproducible from
 * input alone, so a run that used it must not become a movie.
 *
 * Answers: run/runw/movie/save/load/savefile/loadfile/frame answer with the
 * frame number, peek and runw with hex bytes, native with "ok <mode>
 * suspended <n> replayed <n> inexact <n> lost <n> mismatched <0|1>" (the
 * routines suspended now, how loads rebuilt suspended routines so far, and
 * whether shadow mode found a mismatch), the rest with "ok".
 *
 * Native routines and machine copies (docs/MOVIES.md, "Native code"). With
 * --native on, a hand port that calls out to code that isn't ported is
 * suspended on its own stack until the callee returns, and in battle one
 * usually is at a frame's end. A machine copy (a slot or a state file) can't
 * hold that stack, and restoring one with a fresh runtime would leave the rest
 * of the routine to the original code, which may expect registers the port
 * never set. Instead ct_drive keeps the last frame end where nothing was
 * suspended (a "base", where a fresh runtime is the runtime the run had) and
 * the buttons since. A copy made while routines are suspended keeps both, and
 * restoring it restores the base and replays the buttons, which brings the
 * routines back as they were ("replayed"; "inexact" if the replay differs from
 * the copy, see replay()). A state file keeps them beside it (state_save).
 * Only a copy with no base (after a poke) loads without its routines ("lost").
 * Shadow mode runs only the original code, so a load there restarts the
 * runtime, dropping comparisons in progress, as ct_play's does.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "media.h"
#include "movie.h"
#include "natives.h"
#include "snes/render.h"
#include "snes/system.h"

#define SLOTS 4096
#define WATCHES 32

/* A machine copy, shared between the slots and bases that hold it. */
typedef struct Snap {
    Snes snes;
    int refs;
} Snap;

/* A slot: the machine, and with --native on while routines were suspended,
 * the base and buttons that rebuild them. */
typedef struct Slot {
    Snap *machine;
    Snap *base;
    uint16_t *pads;
    size_t npads;
    bool suspended;   /* --native on: routines were suspended when it was saved */
} Slot;

static Snes snes;
static Renderer renderer;
static Slot slots[SLOTS];
static uint32_t watch_addr[WATCHES], watch_len[WATCHES];
static int nwatch;

static NativeMode native_mode = NATIVE_OFF;
static NativeTable table;
static NativeRuntime *natives;
static bool shadow_failed;
static unsigned long loads_replayed, loads_inexact, loads_lost;

/* --native on: the last frame end with no routine suspended (NULL if the run
 * since can't be replayed from one), and the buttons of every frame since. */
static Snap *base;
static uint64_t base_since;   /* snes_clone_since epoch while base is ours alone */
static uint16_t *log_pads;
static size_t log_len, log_cap;

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

/* ---- machine copies ------------------------------------------------------------- */

static Snap *snap_ref(Snap *p)
{
    if (p)
        p->refs++;
    return p;
}

static void snap_release(Snap *p)
{
    if (p && --p->refs == 0)
        free(p);
}

/* A copy of the machine now, in *p's memory if nothing else holds it. */
static Snap *snap_take(Snap *p)
{
    if (!p || p->refs > 1) {
        snap_release(p);
        p = malloc(sizeof *p);
        if (!p)
            return NULL;
        p->refs = 1;
    }
    snes_clone(&p->snes, &snes);
    return p;
}

static bool suspended(void) { return native_mode == NATIVE_ON && native_suspended(natives); }

/* A fresh runtime, after the machine was replaced. Shadow mismatches found so
 * far are reported first, since the runtime that found them goes. */
static void restart_natives(void)
{
    if (native_mode == NATIVE_OFF)
        return;
    if (native_mode == NATIVE_SHADOW && natives && native_failed(natives)) {
        shadow_failed = true;
        fprintf(stderr, "shadow mode found mismatches:\n");
        native_report(natives, stderr);
    }
    natives = natives_restart(natives, &table, native_mode, &snes);
}

/* The machine as it stands is a base: nothing is suspended, so a fresh runtime
 * here is the runtime the run has. Refreshed frame by frame, the copy only
 * takes the pages written since (snes_clone_since). */
static void set_base_here(void)
{
    if (base && base->refs == 1 && base_since) {
        base_since = snes_clone_since(&base->snes, &snes, base_since);
    } else {
        if (!base || base->refs > 1) {
            snap_release(base);
            base = malloc(sizeof *base);
            if (!base) {
                base_since = 0;
                return;
            }
            base->refs = 1;
        }
        base_since = snes_clone_since(&base->snes, &snes, 0);
    }
    log_len = 0;
}

static void set_base(Snap *p)
{
    snap_release(base);
    base = snap_ref(p);
    base_since = 0;
    log_len = 0;
}

static void log_pad(uint16_t pad)
{
    if (log_len == log_cap) {
        log_cap = log_cap ? log_cap * 2 : 4096;
        log_pads = realloc(log_pads, log_cap * sizeof *log_pads);
    }
    log_pads[log_len++] = pad;
}

static void step(uint16_t pad)
{
    snes_run_frame(&snes, pad);
    if (native_mode != NATIVE_ON)
        return;
    if (!suspended())
        set_base_here();
    else if (base)
        log_pad(pad);
}

/* Put `from` (a base) in place with a fresh runtime and run the buttons since:
 * the machine and its suspended routines as they were when `want` was copied
 * from them. Checked against `want`, which it matches unless the ports owed
 * time at the base (native.c's debt, which a copy doesn't hold and ct_drive
 * can't see; docs/TODO.md): then the replay is kept anyway, since its routines
 * are intact and it differs from the copy only by that time, the approximation
 * a ct_play quick load makes. */
static void replay(Snap *from, const uint16_t *pads, size_t npads, const Snes *want)
{
    size_t want_len;
    uint8_t *want_state = snes_state_write(want, &want_len);
    snes_clone(&snes, &from->snes);
    snes_mark_all_written(&snes);
    restore_hooks();
    restart_natives();
    set_base(from);
    for (size_t i = 0; i < npads; i++)
        step(pads[i]);
    size_t len;
    uint8_t *now = snes_state_write(&snes, &len);
    if (now && want_state && len == want_len && !memcmp(now, want_state, len)) {
        loads_replayed++;
    } else {
        loads_inexact++;
        fprintf(stderr, "ct_drive: frame %u: replaying %lu frames from the last one with no native routine suspended "
                        "came out %s the saved machine; keeping the replay\n",
                (unsigned)snes.frame, (unsigned long)npads,
                memcmp(snes.bus.wram, want->bus.wram, SNES_WRAM_SIZE) ? "different in work RAM from"
                                                                       : "differing in time only from");
    }
    free(now);
    free(want_state);
}

/* After a load that didn't replay: continue from the machine with a fresh
 * runtime. `lost` if routines were suspended when it was saved: the original
 * code finishes them. From here on a fresh runtime is exact, so this is a
 * base. */
static void settle_loaded(bool lost)
{
    restart_natives();
    if (native_mode != NATIVE_ON)
        return;
    if (lost) {
        loads_lost++;
        fprintf(stderr, "ct_drive: frame %u: loaded without the native routines suspended when it was saved; "
                        "the original code finishes them\n", (unsigned)snes.frame);
    }
    base_since = 0;   /* the machine's write epochs are the loaded ones */
    set_base_here();
}

static void slot_clear(Slot *s)
{
    snap_release(s->base);
    s->base = NULL;
    free(s->pads);
    s->pads = NULL;
    s->npads = 0;
    s->suspended = false;
}

static int slot_save(Slot *s)
{
    slot_clear(s);
    s->machine = snap_take(s->machine);
    if (!s->machine)
        return -1;
    s->suspended = suspended();
    if (s->suspended && base) {
        s->base = snap_ref(base);
        s->pads = malloc((log_len ? log_len : 1) * sizeof *s->pads);
        if (!s->pads)
            return -1;
        memcpy(s->pads, log_pads, log_len * sizeof *s->pads);
        s->npads = log_len;
    }
    return 0;
}

static void slot_load(Slot *s)
{
    if (s->base) {
        replay(s->base, s->pads, s->npads, &s->machine->snes);
        return;
    }
    snes_clone(&snes, &s->machine->snes);
    snes_mark_all_written(&snes);
    restore_hooks();
    settle_loaded(s->suspended);
}

/* ---- state files ---------------------------------------------------------------- */

/* Beside a state saved while routines were suspended: <path>.base, the state
 * at the base, and <path>.base.pads, the buttons since, one decimal per line. */
static void sidecar_names(const char *path, char *state, char *pads, size_t size)
{
    snprintf(state, size, "%s.base", path);
    snprintf(pads, size, "%s.base.pads", path);
}

static int state_save(const char *path)
{
    if (snes_state_save(&snes, path) != 0)
        return -1;
    char state[1100], pads[1100];
    sidecar_names(path, state, pads, sizeof state);
    remove(state);
    remove(pads);
    if (!suspended())
        return 0;
    /* the buttons alone (no base, after a poke) mark the routines as lost */
    FILE *f = fopen(pads, "w");
    if (!f || (base && snes_state_save(&base->snes, state) != 0)) {
        if (f)
            fclose(f);
        return -1;
    }
    for (size_t i = 0; base && i < log_len; i++)
        fprintf(f, "%u\n", (unsigned)log_pads[i]);
    return fclose(f) == 0 ? 0 : -1;
}

/* The buttons in a .base.pads sidecar (malloc'd), or NULL. */
static uint16_t *read_pads(FILE *f, size_t *count)
{
    size_t n = 0, cap = 4096;
    uint16_t *list = malloc(cap * sizeof *list);
    unsigned pad;
    while (list && fscanf(f, "%u", &pad) == 1) {
        if (n == cap) {
            uint16_t *more = realloc(list, (cap *= 2) * sizeof *list);
            if (!more) {
                free(list);
                return NULL;
            }
            list = more;
        }
        list[n++] = (uint16_t)pad;
    }
    *count = n;
    return list;
}

/* A machine for this ROM from a state file, or NULL. */
static Snap *snap_load(const char *path)
{
    Snap *p = malloc(sizeof *p);
    if (!p)
        return NULL;
    p->refs = 1;
    snes_clone(&p->snes, &snes);   /* the ROM, which a state doesn't hold */
    if (snes_state_load(&p->snes, path) != 0) {
        free(p);
        return NULL;
    }
    return p;
}

static int state_load(const char *path)
{
    char state[1100], pads[1100];
    sidecar_names(path, state, pads, sizeof state);
    FILE *f = native_mode == NATIVE_ON ? fopen(pads, "r") : NULL;
    if (f) {
        size_t n = 0;
        uint16_t *list = read_pads(f, &n);
        fclose(f);
        Snap *want = snap_load(path), *from = snap_load(state);
        if (!want) {
            snap_release(from);
            free(list);
            return -1;
        }
        bool replayed = from && list;
        if (replayed)
            replay(from, list, n, &want->snes);
        snap_release(from);
        snap_release(want);
        free(list);
        if (replayed)
            return 0;
    }
    if (snes_state_load(&snes, path) != 0)
        return -1;
    restore_hooks();
    settle_loaded(f != NULL);
    return 0;
}

/* ---- commands ------------------------------------------------------------------- */

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

static const char *const mode_names[] = { "off", "shadow", "on" };

static int usage(void)
{
    fprintf(stderr, "usage: ct_drive [--native off|shadow|on] [--recomp off|on|only] <rom> [sram.srm]\n");
    return 2;
}

int main(int argc, char **argv)
{
    const char *rom_path = NULL, *sram_path = NULL;
    bool recomp = true, recomp_only = false;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--native") || !strcmp(a, "--recomp")) {
            const char *v = i + 1 < argc ? argv[++i] : "";
            if (a[2] == 'n') {
                if (!strcmp(v, "off")) native_mode = NATIVE_OFF;
                else if (!strcmp(v, "shadow")) native_mode = NATIVE_SHADOW;
                else if (!strcmp(v, "on")) native_mode = NATIVE_ON;
                else {
                    fprintf(stderr, "--native must be off, shadow or on\n");
                    return 2;
                }
            } else {
                if (!strcmp(v, "off")) recomp = false, recomp_only = false;
                else if (!strcmp(v, "on")) recomp = true, recomp_only = false;
                else if (!strcmp(v, "only")) recomp = true, recomp_only = true;
                else {
                    fprintf(stderr, "--recomp must be off, on or only\n");
                    return 2;
                }
            }
        } else if (a[0] == '-' && a[1] == '-') {
            fprintf(stderr, "unknown argument %s\n", a);
            return usage();
        } else if (!rom_path) {
            rom_path = a;
        } else if (!sram_path) {
            sram_path = a;
        } else {
            return usage();
        }
    }
    if (!rom_path)
        return usage();
    size_t rom_len;
    uint8_t *rom = read_file(rom_path, &rom_len);
    if (!rom) {
        fprintf(stderr, "cannot read %s\n", rom_path);
        return 1;
    }
    size_t header = rom_len % 0x8000 == 0x200 ? 0x200 : 0;
    snes_init(&snes, rom + header, rom_len - header);
    render_init(&renderer);
    restore_hooks();
    snes_power_on(&snes);
    if (sram_path) {
        size_t sram_len;
        uint8_t *sram = read_file(sram_path, &sram_len);
        if (!sram || sram_len != SNES_SRAM_SIZE) {
            fprintf(stderr, "cannot read %s as a %d-byte SRAM file\n", sram_path, SNES_SRAM_SIZE);
            free(sram);
            return 1;
        }
        memcpy(snes.bus.sram, sram, SNES_SRAM_SIZE);
        free(sram);
    }
    if (native_mode != NATIVE_OFF) {
        if (natives_select(&table, rom + header, rom_len - header, native_mode, recomp, recomp_only) != 0) {
            fprintf(stderr, "out of memory\n");
            return 1;
        }
        if (recomp && !table.generated)
            fprintf(stderr, "ct_drive: no generated blocks for this ROM in this build; running the ported "
                            "routines only\n");
        restart_natives();
        if (native_mode == NATIVE_ON)
            set_base_here();
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
                step(pad);
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
                    step(ct_movie_buttons(&movie, snes.frame));
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
            snes_mark_all_written(&snes);
            if (native_mode == NATIVE_SHADOW) {
                restart_natives();   /* the copies of routines in progress didn't see it */
            } else if (native_mode == NATIVE_ON) {
                if (suspended())
                    set_base(NULL);   /* no base replays to this */
                else
                    set_base_here();   /* marked all written: the whole machine is copied */
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
                if (slot_save(&slots[k]) != 0)
                    printf("err memory\n");
                else
                    printf("ok %u\n", (unsigned)snes.frame);
            } else if (cmd[0] == 'l') {
                if (!slots[k].machine) {
                    printf("err empty\n");
                } else {
                    slot_load(&slots[k]);
                    printf("ok %u\n", (unsigned)snes.frame);
                }
            } else {
                slot_clear(&slots[k]);
                snap_release(slots[k].machine);
                slots[k].machine = NULL;
                printf("ok\n");
            }
        } else if (!strcmp(cmd, "savefile")) {
            printf(state_save(a1) == 0 ? "ok %u\n" : "err %u\n", (unsigned)snes.frame);
        } else if (!strcmp(cmd, "loadfile")) {
            int r = state_load(a1);
            printf(r == 0 ? "ok %u\n" : "err %u\n", (unsigned)snes.frame);
        } else if (!strcmp(cmd, "shot")) {
            printf(ct_png_write(a1, renderer.pixels, RENDER_WIDTH, RENDER_HEIGHT) == 0 ? "ok\n" : "err write\n");
        } else if (!strcmp(cmd, "frame")) {
            printf("ok %u\n", (unsigned)snes.frame);
        } else if (!strcmp(cmd, "native")) {
            printf("ok %s suspended %d replayed %lu inexact %lu lost %lu mismatched %d\n", mode_names[native_mode],
                   natives ? native_suspended(natives) : 0, loads_replayed, loads_inexact, loads_lost,
                   shadow_failed || (natives && native_mode == NATIVE_SHADOW && native_failed(natives)));
        } else if (!strcmp(cmd, "quit")) {
            printf("ok\n");
            fflush(stdout);
            break;
        } else {
            printf("err unknown command\n");
        }
        fflush(stdout);
    }
    if (native_mode == NATIVE_SHADOW && native_failed(natives)) {
        fprintf(stderr, "shadow mode found mismatches:\n");
        native_report(natives, stderr);
    }
    if (native_mode == NATIVE_ON && (loads_replayed || loads_inexact || loads_lost))
        fprintf(stderr, "ct_drive: loads with native routines suspended: %lu replayed exactly, %lu replayed "
                        "differing in owed time, %lu without the routines\n",
                loads_replayed, loads_inexact, loads_lost);
    native_destroy(natives);
    natives_free(&table);
    for (int i = 0; i < SLOTS; i++) {
        slot_clear(&slots[i]);
        snap_release(slots[i].machine);
    }
    snap_release(base);
    free(log_pads);
    free(rom);
    return 0;
}
