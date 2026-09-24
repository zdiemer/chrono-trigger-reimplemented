/* ct_play: plays the game in a window, with the ported routines running natively
 * and the rest on the development system (docs/PLAYING.md).
 *
 *   ct_play [--rom] <rom> [--scale N] [--fullscreen] [--mute] [--native on|shadow|off] [--recomp only|on|off]
 *           [--audio snes|direct] [--sound-driver original|c] [--renderer native|scanline] [--save-dir dir] [--keys f] [--bind IN=ACTION]...
 *           [--movie m.ctm [--movie-frames N]] [--record out.ctm] [--sram save.srm] [--load-zst in.zst]
 *   ct_play --rom <rom> --frames N [--screenshot out.png] [--hold BUTTONS@FIRST-LAST]...
 *           [--load-state f] [--save-state f] [--movie m.ctm] [--record out.ctm] [--wav out.wav]
 *
 * This file turns the options into a player (player.h), the desktop's storage
 * (files.h) and one of two drivers (driver.h): run_window, or with --frames
 * run_headless, which runs a fixed number of frames without a display, for
 * checking a build. --movie replays an input movie before the keyboard and pads
 * take over; --record writes everything played from power-on as a movie that
 * ct_dev replays identically. In a window, the APU's 32000 Hz output plays
 * through SDL audio (see audio_fill.h).
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

#include "bindings.h"
#include "driver.h"
#include "files.h"
#include "movie.h"
#include "player.h"
#include "sram_find.h"
#include "trace.h"

#define MAX_HOLDS 64
#define MAX_BINDS 64
/* The key file read when --keys isn't given, next to the executable. */
#define KEYS_FILE "ct_play.keys"

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
            "  --recomp MODE        on: the ported routines, and the blocks generated from the ROM where there\n"
            "                       is no port (default; the ports' timing is estimated, within the timing\n"
            "                       gate's bounds); only: the generated blocks alone, which keep the original\n"
            "                       timing exactly (what --movie and --record use unless --recomp is given);\n"
            "                       off: no generated blocks\n"
            "  --audio BACKEND      snes: the APU on the machine's timeline (default); direct: the sound driver's\n"
            "                       uploads and commands take no CPU time (APU-direct; needs --native on, and\n"
            "                       runs the ported routines, so --recomp on is implied)\n"
            "  --sound-driver D     original: the game's sound driver on the SPC700 (default); c: the C port of it\n"
            "                       (game/sound/) in its place, playing through the same S-DSP; the game's timing\n"
            "                       is not the original's, so no movies, and no saved states\n"
            "  --renderer R         native: draw each frame from the video service's frame state (default);\n"
            "                       scanline: the reference renderer, line by line from the PPU registers\n"
            "  --save-dir DIR       where <rom name>.srm and <rom name>.state go (default: the ROM's directory)\n"
            "  --keys F             read key and gamepad bindings from F, one INPUT = ACTION a line (default:\n"
            "                       " KEYS_FILE " next to ct_play, if there is one)\n"
            "  --bind INPUT=ACTION  bind a key or pad input, after the key file (repeatable); for example\n"
            "                       --bind \"Space=A\" --bind \"Pad guide=QuickSave\" --bind \"Tab=None\"\n"
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
            "Controls (keyboard and gamepad; --keys and --bind change them):\n");
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

/* A whole binary file; NULL if it is missing, unreadable or empty. */
static uint8_t *read_file(const char *path, size_t *len)
{
    uint8_t *data = files_read(path, len, false, NULL);
    if (data && !*len) {
        free(data);
        return NULL;
    }
    return data;
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
    static Files files;
    bool native_given = false, recomp = true, recomp_only = false, recomp_given = false, audio_direct = false;
    bool c_sound_driver = false;
    pl.native_mode = NATIVE_ON;
    long frames = -1, scale = 3, movie_frames = -1;
    /* A TV showed the picture at 4:3 and hid about 8 rows at the top and bottom. */
    WindowOptions wo = { .display = { .crop_top = 8, .crop_bottom = 8,
                                      .pixel_aspect = 4.0 / 3.0 * RENDER_HEIGHT / RENDER_WIDTH } };
    Display *display = &wo.display;
    const char *wav_path = NULL;
    static Hold holds[MAX_HOLDS];
    int nholds = 0;
    const char *keys_path = NULL, *binds[MAX_BINDS];
    int nbinds = 0;
    bool help = false;

    SDL_SetMainReady();
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            help = true;
            continue;
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
        else if (!strcmp(a, "--keys")) keys_path = v;
        else if (!strcmp(a, "--bind")) {
            if (nbinds == MAX_BINDS) {
                fprintf(stderr, "more than %d --bind\n", MAX_BINDS);
                return 2;
            }
            binds[nbinds++] = v;
        }
        else if (!strcmp(a, "--frames")) frames = strtol(v, NULL, 10);
        else if (!strcmp(a, "--screenshot")) screenshot = v;
        else if (!strcmp(a, "--load-state")) load_state_path = v;
        else if (!strcmp(a, "--load-zst")) zst_path = v;
        else if (!strcmp(a, "--save-state")) save_state_path = v;
        else if (!strcmp(a, "--wav")) wav_path = v;
        else if (!strcmp(a, "--movie")) movie_path = v;
        else if (!strcmp(a, "--movie-frames")) movie_frames = strtol(v, NULL, 10);
        else if (!strcmp(a, "--record")) files.movie = v;
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
        else if (!strcmp(a, "--sound-driver")) {
            if (!strcmp(v, "original") || !strcmp(v, "c")) {
                c_sound_driver = strcmp(v, "c") == 0;
            } else {
                fprintf(stderr, "--sound-driver must be original or c\n");
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
    /* The window's controls: the defaults, the key file, then --bind. */
    bindings_reset();
    char default_keys[1024] = "";
    if (!keys_path) {
        char *base = SDL_GetBasePath();
        if (base) {
            snprintf(default_keys, sizeof default_keys, "%s%s", base, KEYS_FILE);
            SDL_free(base);
        }
    }
    const char *keys_file = keys_path ? keys_path : default_keys[0] ? default_keys : NULL;
    if (keys_file) {
        size_t len = 0;
        bool missing;
        char *text = (char *)files_read(keys_file, &len, true, &missing);
        if (!text && (keys_path || !missing)) {
            fprintf(stderr, "cannot read %s\n", keys_file);
            return 2;
        }
        if (text && bindings_load(text, len, keys_file) != 0) {
            free(text);
            return 2;
        }
        free(text);
    }
    for (int i = 0; i < nbinds; i++) {
        char err[160];
        if (bindings_set(binds[i], err, sizeof err) != 0) {
            fprintf(stderr, "bad --bind %s: %s\n", binds[i], err);
            return 2;
        }
    }
    if (help) {
        usage(stdout);
        return 0;
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
    const char *record_path = files.movie;
    /* A movie is button presses at frame numbers, recorded against the original's
     * timing: replayed or recorded with the ported routines' estimated timing it
     * would drift and go off its route, so a movie means the exact mode unless
     * --recomp says otherwise. */
    if ((movie_path || record_path) && !recomp_given)
        recomp_only = true;
    if (c_sound_driver && (movie_path || record_path || load_state_path || save_state_path || zst_path)) {
        /* the C driver answers the CPU half at once, and its state is its own */
        fprintf(stderr, "--sound-driver c changes the original's timing and keeps state outside the machine: it can't "
                        "replay or record a movie, or load or save a state\n");
        return 2;
    }
    if (audio_direct) {
        /* APU-direct runs in the sound driver's ported routines and changes timing */
        if (pl.native_mode != NATIVE_ON || record_path || (recomp_given && recomp_only)) {
            fprintf(stderr, "--audio direct needs --native on with the ported routines (not --recomp only), "
                            "and can't record: it changes the original's timing\n");
            return 2;
        }
        recomp_only = false;
    }
    if (record_path) {
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
        pl.track.record_name = files_name(record_path);
    }

    size_t rom_len;
    uint8_t *rom = read_file(rom_path, &rom_len);
    if (!rom) {
        fprintf(stderr, "cannot read %s\n", rom_path);
        return 1;
    }
    size_t header = rom_len % 0x8000 == 0x200 ? 0x200 : 0;

    /* What the saves are called and where they go. */
    files_side_path(files.sram, sizeof files.sram, rom_path, rom_path, save_dir, ".srm");
    files_side_path(files.state, sizeof files.state, record_path ? record_path : rom_path, rom_path, save_dir,
                    ".state");
    files_side_path(files.misses, sizeof files.misses, rom_path, rom_path, save_dir, ".misses.txt");

    if (movie_path) {
        ct_movie movie;
        if (ct_movie_load(&movie, movie_path) != 0)
            return 1;
        uint32_t n = movie_frames >= 0 ? (uint32_t)movie_frames : movie.length;
        uint16_t *pads = calloc(n ? n : 1, sizeof *pads);
        for (uint32_t fr = 0; fr < n; fr++)
            pads[fr] = ct_movie_buttons(&movie, fr);
        player_replay(&pl, pads, n, files_name(movie_path));
        free(pads);
        movie_sram = movie.has_sram;
        movie_sram_crc = movie.sram_crc;
        ct_movie_free(&movie);
    }

#if CT_PLAYER_NO_INTERPRETER
    pl.misses_name = files.misses;
#endif
    int rc = player_init(&pl, rom + header, rom_len - header, recomp, recomp_only);
    if (rc != 0)
        return rc;
    if (CT_PLAYER_NO_INTERPRETER) {
        size_t len = 0;
        bool missing;
        char *text = (char *)files_read(files.misses, &len, true, &missing);
        if (text)
            player_misses_merge(&pl, text, len);
        else if (!missing)
            fprintf(stderr, "warning: cannot read %s; starting a new one\n", files.misses);
        free(text);
        fprintf(stderr, "ct_play: %s; instructions without generated code are logged to %s\n",
                player_build_mode[CT_PLAYER_NO_INTERPRETER], files.misses);
    }
    /* A movie from a cartridge save: find the save by its CRC-32 when --sram
     * isn't given (the vendored saves next to the movies, or below the ROM). */
    static char found_sram[1024];
    if (movie_sram && !sram_file &&
        ct_sram_find(movie_sram_crc, movie_path, rom_path, found_sram, sizeof found_sram) == 0) {
        sram_file = found_sram;
        fprintf(stderr, "using save %s (CRC-32 %08X)\n", sram_file, (unsigned)movie_sram_crc);
    }
    /* --sram and recordings start from a fixed save and never write one back. */
    pl.persist_sram = !record_path && !sram_file && !zst_path;
    if (sram_file) {
        size_t sram_len;
        uint8_t *sram = read_file(sram_file, &sram_len);
        if (!sram || sram_len != SNES_SRAM_SIZE) {
            fprintf(stderr, "cannot read %s as a %d-byte SRAM file\n", sram_file, SNES_SRAM_SIZE);
            return 1;
        }
        player_use_sram(&pl, sram, files_name(sram_file));
        free(sram);
    }
    uint32_t sram_crc = player_sram_crc(&pl);
    if (movie_sram && (!sram_file || sram_crc != movie_sram_crc)) {
        fprintf(stderr, "%s starts from SRAM with CRC-32 %08X; pass that save with --sram\n", movie_path,
                (unsigned)movie_sram_crc);
        return 1;
    }
    if (pl.persist_sram) {
        size_t sram_len = 0;
        uint8_t *sram = files_read(files.sram, &sram_len, false, NULL);
        if (sram && sram_len != SNES_SRAM_SIZE)
            fprintf(stderr, "ignoring %s: expected %d bytes\n", files.sram, SNES_SRAM_SIZE);
        player_load_sram(&pl, sram && sram_len == SNES_SRAM_SIZE ? sram : NULL);
        free(sram);
    }
    if (player_start(&pl, audio_direct, c_sound_driver) != 0)
        return 1;
    if (load_state_path && files_load_state(&pl, load_state_path) != 0)
        return 1;
    if (zst_path) {
        size_t len;
        uint8_t *data = read_file(zst_path, &len);
        char why[160] = "cannot read the file";
        if (!data || player_import_zst(&pl, data, len, why, sizeof why) != 0) {
            fprintf(stderr, "cannot import %s: %s\n", zst_path, why);
            return 1;
        }
        free(data);
        fprintf(stderr, "imported ZSNES state %s (some state approximated; not for validation)\n", zst_path);
    }
    if (record_path)
        fprintf(stderr, "recording to %s in %s mode; quick saves go to %s\n", record_path,
                pl.native_mode == NATIVE_SHADOW ? "shadow" : pl.native_mode == NATIVE_ON ? "on" : "off",
                files.state);

    if (frames >= 0) {
        HeadlessOptions ho = { &files, frames, holds, nholds, screenshot, save_state_path, wav_path };
        rc = run_headless(&pl, &ho);
    } else {
        wo.files = &files;
        wo.scale = (int)scale;
        rc = run_window(&pl, &wo);
    }
    if (record_path)
        fprintf(stderr, "recorded %lu frames to %s\n", (unsigned long)pl.track.frames, record_path);
    files_flush(&files, &pl);
    player_finish(&pl);
    free(rom);
    return rc;
}
