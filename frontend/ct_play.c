/* ct_play: plays the game in a window, with the ported routines running natively
 * and the rest on the development system (docs/PLAYING.md).
 *
 *   ct_play [--rom] <rom> [--scale N] [--fullscreen] [--mute] [--native on|shadow|off] [--recomp only|on|off]
 *           [--audio snes|direct] [--renderer native|scanline] [--save-dir dir]
 *           [--movie m.ctm [--movie-frames N]] [--record out.ctm] [--sram save.srm] [--load-zst in.zst]
 *   ct_play --rom <rom> --frames N [--screenshot out.png] [--hold BUTTONS@FIRST-LAST]...
 *           [--load-state f] [--save-state f] [--movie m.ctm] [--record out.ctm] [--wav out.wav]
 *
 * With --frames it runs headless (no window) for a fixed number of frames, for
 * checking a build without a display. --movie replays an input movie before the
 * keyboard takes over; --record writes everything played from power-on as a
 * movie that ct_dev replays identically. In a window, the APU's 32000 Hz output
 * plays through SDL audio (see frontend/audio_fill.h).
 *
 * Built with CT_PLAYER_NO_INTERPRETER (CMake CT_PLAYER_INTERPRETER=FALLBACK or
 * OFF, docs/PLAYING.md), it runs only native code, the generated blocks: an
 * instruction without generated code is a miss, logged to <rom name>.misses.txt
 * and run on the interpreter (1, FALLBACK), or, with the interpreter not linked
 * at all (2, OFF), the game stops there.
 */
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver.h"
#include "movie.h"
#include "player.h"
#include "sram_find.h"
#include "trace.h"

#define MAX_HOLDS 64

static const struct {
    const char *name;
    uint16_t button;
} button_names[] = {
    { "B", CT_BTN_B }, { "Y", CT_BTN_Y }, { "SELECT", CT_BTN_SELECT }, { "START", CT_BTN_START },
    { "UP", CT_BTN_UP }, { "DOWN", CT_BTN_DOWN }, { "LEFT", CT_BTN_LEFT }, { "RIGHT", CT_BTN_RIGHT },
    { "A", CT_BTN_A }, { "X", CT_BTN_X }, { "L", CT_BTN_L }, { "R", CT_BTN_R },
};

static void usage(FILE *out)
{
    fprintf(out,
            "usage: ct_play [--rom] <rom> [options]\n"
            "\n"
            "Options:\n"
            "  --scale N            window scale (integer, default 3)\n"
            "  --fullscreen         start fullscreen\n"
            "  --aspect A           4:3 (default, as on a TV), 8:7 (the SNES pixel aspect) or square\n"
            "  --crop N|T,B         hide rows at the top and bottom as a TV's overscan did (default 8; 0 shows all)\n"
            "  --mute               no sound\n"
            "  --native MODE        on: run ported routines natively (default); off: interpret everything;\n"
            "                       shadow: interpret, and check each ported routine against the original\n"
            "  --recomp MODE        only: in on mode, run the blocks generated from the ROM without the ported\n"
            "                       routines, which keeps the original timing exactly (default); on: generated\n"
            "                       blocks and ported routines (the ported routines' timing is estimated);\n"
            "                       off: no generated blocks\n"
            "  --audio BACKEND      snes: the APU on the machine's timeline (default); direct: the sound driver's\n"
            "                       uploads and commands take no CPU time (APU-direct; needs --native on, and\n"
            "                       runs the ported routines, so --recomp on is implied)\n"
            "  --renderer R         native: draw each frame from the video service's frame state (default);\n"
            "                       scanline: the reference renderer, line by line from the PPU registers\n"
            "  --save-dir DIR       where <rom name>.srm and <rom name>.state go (default: the ROM's directory)\n"
            "\n"
            "Movies:\n"
            "  --movie F.ctm        replay this input movie from power-on, fast, then hand over to the keyboard\n"
            "  --movie-frames N     replay N frames of it (default: up to its last input)\n"
            "  --watch              replay the movie at normal speed with sound instead of fast (Tab still\n"
            "                       fast-forwards); the keyboard takes over when it ends\n"
            "  --record F.ctm       record input from power-on (including the replayed movie) to F.ctm,\n"
            "                       for ct_dev to replay. Starts without the .srm save (or with --sram), runs in\n"
            "                       shadow mode unless --native off is given, and quick saves go to <F>.state\n"
            "  --sram F.srm         start with this cartridge save instead of <rom name>.srm, and don't write it\n"
            "  --load-zst F.zst     start from a ZSNES 1.51 save state (.zst, .zs1-.zs9), including its cartridge\n"
            "                       save, which isn't written back; some of its state is approximated, so it's\n"
            "                       for exploring and coverage, and can't be recorded\n"
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
    window_print_controls(out);
    fprintf(out, "\nSaves are written to <rom name>.srm when the game changes them and on exit.\n");
    fprintf(out, "\nThis build runs %s.\n", player_build_mode[CT_PLAYER_NO_INTERPRETER]);
    if (CT_PLAYER_NO_INTERPRETER)
        fprintf(out,
                "Only --native on works (with --recomp only or on). An instruction nothing was generated for is a\n"
                "miss: it is logged to <rom name>.misses.txt next to the saves, a coverage file to feed back into\n"
                "the build (CT_RECOMP_COVERAGE), and %s.\n",
                CT_PLAYER_NO_INTERPRETER == 1 ? "runs on the interpreter" : "stops the game");
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

static const char *file_name(const char *path)
{
    const char *name = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\')
            name = p + 1;
    }
    return name;
}

/* dir/<name_path's file name without extension><ext>; dir defaults to dir_of's directory. */
static void side_path(char *out, size_t n, const char *name_path, const char *dir_of, const char *dir,
                      const char *ext)
{
    const char *name = file_name(name_path);
    const char *dot = strrchr(name, '.');
    int stem = dot && dot != name ? (int)(dot - name) : (int)strlen(name);
    if (dir)
        snprintf(out, n, "%s/%.*s%s", dir, stem, name, ext);
    else
        snprintf(out, n, "%.*s%.*s%s", (int)(file_name(dir_of) - dir_of), dir_of, stem, name, ext);
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

int main(int argc, char **argv)
{
    const char *rom_path = NULL, *save_dir = NULL, *screenshot = NULL;
    const char *load_state_path = NULL, *save_state_path = NULL, *movie_path = NULL, *sram_file = NULL;
    const char *zst_path = NULL;
    int movie_sram = 0;
    uint32_t movie_sram_crc = 0;
    static Player pl;   /* large; keep it off the stack */
    bool native_given = false, recomp = true, recomp_only = true, recomp_given = false, audio_direct = false;
    pl.native_mode = NATIVE_ON;
    long frames = -1, scale = 3, movie_frames = -1;
    /* A TV showed the picture at 4:3 and hid about 8 rows at the top and bottom. */
    WindowOptions wo = { .display = { .crop_top = 8, .crop_bottom = 8,
                                      .pixel_aspect = 4.0 / 3.0 * RENDER_HEIGHT / RENDER_WIDTH } };
    Display *display = &wo.display;
    const char *wav_path = NULL;
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
            wo.fullscreen = true;
            continue;
        }
        if (!strcmp(a, "--watch")) {
            pl.watch = true;
            continue;
        }
        if (!strcmp(a, "--mute")) {
            wo.mute = true;
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
        else if (!strcmp(a, "--aspect")) {
            if (!strcmp(v, "4:3")) display->pixel_aspect = 4.0 / 3.0 * RENDER_HEIGHT / RENDER_WIDTH;
            else if (!strcmp(v, "8:7")) display->pixel_aspect = 8.0 / 7.0;
            else if (!strcmp(v, "square")) display->pixel_aspect = 1.0;
            else {
                fprintf(stderr, "--aspect must be 4:3, 8:7 or square\n");
                return 2;
            }
        }
        else if (!strcmp(a, "--crop")) {
            char *end;
            long top = strtol(v, &end, 10), bottom = top;
            if (*end == ',')
                bottom = strtol(end + 1, &end, 10);
            if (*end || top < 0 || bottom < 0 || top + bottom > RENDER_HEIGHT - 16) {
                fprintf(stderr, "--crop must be ROWS or TOP,BOTTOM\n");
                return 2;
            }
            display->crop_top = (int)top;
            display->crop_bottom = (int)bottom;
        }
        else if (!strcmp(a, "--renderer")) {
            if (!strcmp(v, "scanline")) pl.scanline = true;
            else if (!strcmp(v, "native")) pl.scanline = false;
            else {
                fprintf(stderr, "--renderer must be native or scanline\n");
                return 2;
            }
        }
        else if (!strcmp(a, "--save-dir")) save_dir = v;
        else if (!strcmp(a, "--frames")) frames = strtol(v, NULL, 10);
        else if (!strcmp(a, "--screenshot")) screenshot = v;
        else if (!strcmp(a, "--load-state")) load_state_path = v;
        else if (!strcmp(a, "--load-zst")) zst_path = v;
        else if (!strcmp(a, "--save-state")) save_state_path = v;
        else if (!strcmp(a, "--wav")) wav_path = v;
        else if (!strcmp(a, "--movie")) movie_path = v;
        else if (!strcmp(a, "--movie-frames")) movie_frames = strtol(v, NULL, 10);
        else if (!strcmp(a, "--record")) pl.track.record_path = v;
        else if (!strcmp(a, "--sram")) sram_file = v;
        else if (!strcmp(a, "--hold")) {
            if (nholds == MAX_HOLDS || parse_hold(v, &holds[nholds]) != 0) {
                fprintf(stderr, "bad --hold %s (expected BUTTON[+BUTTON]@FIRST-LAST)\n", v);
                return 2;
            }
            nholds++;
        }
        else if (!strcmp(a, "--audio")) {
            if (!strcmp(v, "snes") || !strcmp(v, "direct")) {
                audio_direct = strcmp(v, "direct") == 0;
            } else {
                fprintf(stderr, "--audio must be snes or direct\n");
                return 2;
            }
        }
        else if (!strcmp(a, "--recomp")) {
            recomp_given = true;
            if (!strcmp(v, "off")) recomp = false, recomp_only = false;
            else if (!strcmp(v, "on")) recomp = true, recomp_only = false;
            else if (!strcmp(v, "only")) recomp = true, recomp_only = true;
            else {
                fprintf(stderr, "--recomp must be only, on or off\n");
                return 2;
            }
        }
        else if (!strcmp(a, "--native")) {
            native_given = true;
            if (!strcmp(v, "off")) pl.native_mode = NATIVE_OFF;
            else if (!strcmp(v, "on")) pl.native_mode = NATIVE_ON;
            else if (!strcmp(v, "shadow")) pl.native_mode = NATIVE_SHADOW;
            else {
                fprintf(stderr, "--native must be on, shadow or off\n");
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
    if (CT_PLAYER_NO_INTERPRETER && (pl.native_mode != NATIVE_ON || !recomp)) {
        fprintf(stderr, "this player runs %s: it needs --native on and generated blocks (--recomp only or on)\n",
                player_build_mode[CT_PLAYER_NO_INTERPRETER]);
        return 2;
    }
    Track *t = &pl.track;
    if (audio_direct) {
        /* APU-direct runs in the sound driver's ported routines and changes timing */
        if (pl.native_mode != NATIVE_ON || t->record_path || (recomp_given && recomp_only)) {
            fprintf(stderr, "--audio direct needs --native on with the ported routines (not --recomp only), "
                            "and can't record: it changes the original's timing\n");
            return 2;
        }
        recomp_only = false;
    }
    if (t->record_path) {
        /* A movie replays the same only where the original code's timing is kept,
         * from power-on with the SRAM ct_dev starts with. */
        if (native_given && pl.native_mode == NATIVE_ON && !recomp_only) {
            fprintf(stderr, "--record needs --native shadow or off, or on with --recomp only: ported routines "
                            "change timing, so the movie wouldn't replay the same\n");
            return 2;
        }
        if (load_state_path || zst_path) {
            fprintf(stderr, "--record starts from power-on and can't be combined with --load-state or --load-zst\n");
            return 2;
        }
        if (!native_given && !CT_PLAYER_NO_INTERPRETER)
            pl.native_mode = NATIVE_SHADOW;
    }

    size_t rom_len;
    uint8_t *rom = read_file(rom_path, &rom_len);
    if (!rom) {
        fprintf(stderr, "cannot read %s\n", rom_path);
        return 1;
    }
    size_t header = rom_len % 0x8000 == 0x200 ? 0x200 : 0;

    side_path(pl.sram_path, sizeof pl.sram_path, rom_path, rom_path, save_dir, ".srm");
    if (t->record_path)
        side_path(pl.state_path, sizeof pl.state_path, t->record_path, rom_path, save_dir, ".state");
    else
        side_path(pl.state_path, sizeof pl.state_path, rom_path, rom_path, save_dir, ".state");

    if (movie_path) {
        ct_movie movie;
        if (ct_movie_load(&movie, movie_path) != 0)
            return 1;
        uint32_t n = movie_frames >= 0 ? (uint32_t)movie_frames : movie.length;
        uint16_t *pads = calloc(n ? n : 1, sizeof *pads);
        for (uint32_t fr = 0; fr < n; fr++)
            pads[fr] = ct_movie_buttons(&movie, fr);
        player_replay(&pl, pads, n, file_name(movie_path));
        free(pads);
        movie_sram = movie.has_sram;
        movie_sram_crc = movie.sram_crc;
        ct_movie_free(&movie);
    }

#if CT_PLAYER_NO_INTERPRETER
    side_path(pl.misses_path, sizeof pl.misses_path, rom_path, rom_path, save_dir, ".misses.txt");
#endif
    int rc = player_init(&pl, rom + header, rom_len - header, recomp, recomp_only);
    if (rc != 0)
        return rc;
    /* A movie from a cartridge save: find the save by its CRC-32 when --sram
     * isn't given (the vendored saves next to the movies, or below the ROM). */
    static char found_sram[1024];
    if (movie_sram && !sram_file &&
        ct_sram_find(movie_sram_crc, movie_path, rom_path, found_sram, sizeof found_sram) == 0) {
        sram_file = found_sram;
        fprintf(stderr, "using save %s (CRC-32 %08X)\n", sram_file, (unsigned)movie_sram_crc);
    }
    /* --sram and recordings start from a fixed save and never write one back. */
    pl.persist_sram = !t->record_path && !sram_file && !zst_path;
    if (sram_file) {
        size_t sram_len;
        uint8_t *sram = read_file(sram_file, &sram_len);
        if (!sram || sram_len != SNES_SRAM_SIZE) {
            fprintf(stderr, "cannot read %s as a %d-byte SRAM file\n", sram_file, SNES_SRAM_SIZE);
            return 1;
        }
        memcpy(pl.snes.bus.sram, sram, SNES_SRAM_SIZE);
        free(sram);
        t->sram_name = file_name(sram_file);
    }
    t->sram_crc = ct_crc32(pl.snes.bus.sram, SNES_SRAM_SIZE);
    if (movie_sram && (!sram_file || t->sram_crc != movie_sram_crc)) {
        fprintf(stderr, "%s starts from SRAM with CRC-32 %08X; pass that save with --sram\n", movie_path,
                (unsigned)movie_sram_crc);
        return 1;
    }
    if (pl.persist_sram)
        player_sram_load(&pl);
    player_start(&pl, audio_direct);
    if (load_state_path && player_load_state(&pl, load_state_path) != 0)
        return 1;
    if (zst_path && player_import_zst(&pl, zst_path) != 0)
        return 1;
    if (t->record_path)
        fprintf(stderr, "recording to %s in %s mode; quick saves go to %s\n", t->record_path,
                pl.native_mode == NATIVE_SHADOW ? "shadow" : pl.native_mode == NATIVE_ON ? "on" : "off",
                pl.state_path);

    if (frames >= 0) {
        HeadlessOptions ho = { frames, holds, nholds, screenshot, save_state_path, wav_path };
        rc = run_headless(&pl, &ho);
    } else {
        wo.scale = (int)scale;
        rc = run_window(&pl, &wo);
    }
    player_finish(&pl);
    free(rom);
    return rc;
}
