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
 * with no emulator or machine entry layer. The game's state is held in the host's memory
 * (runtime/port/state_host.h) and its data read from the asset pack
 * (frontend/assets.h; game/data/assets.h, the store): <rom name>.assets beside
 * the ROM unless --assets names it, imported from the ROM where there is none
 * or it is of another catalogue. The ROM is read only to import it, and isn't
 * kept. The palettes, the fonts, the graphics, the maps, the event scripts,
 * the battle animation and menu object scripts, the text and the tables of
 * records are served from the text beside the pack, <pack name>.palettes,
 * .fonts, .graphics, .maps, .scripts, .commands, .text and .records
 * (frontend/palettes.h, frontend/fonts.h, frontend/tiles.h, frontend/maps.h,
 * frontend/scripts.h, frontend/commands.h, frontend/text.h, frontend/records.h),
 * written from the pack where there is none.
 *
 * It plays in the shared SDL window, or runs headless measurements with
 * --frames. It reports invalid game state with its frame and source origin, and what it
 * reached for that a host doesn't have (an address outside work RAM, save RAM
 * and the cartridge), and whether its picture, drawn every frame by the native
 * renderer, ends showing (tools/native_check.py holds the library to that:
 * reaching the last frame says nothing of the picture). --shot-every writes
 * frames as PNGs, --locations the places the run enters, --lag-log each wait
 * the time model makes last (its frame, the frames and the work's clocks),
 * --end-state work RAM and save RAM at the end. Exit status 0 when it ran every frame, 1 where the
 * program stopped short, 2 on bad arguments or files. This file is the driver:
 * the loop, the files and the report belong here. */
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "file_store.h"
#include "fonts.h"
#include "maps.h"
#include "records.h"
#include "commands.h"
#include "scripts.h"
#include "text.h"
#include "palettes.h"
#include "tiles.h"
#include "storage.h"
#include "track.h"
#include "snapshot.h"
#include "native_build.h"
#include "checksum.h"
#include "sram_find.h"
#include "menu/saves.h"
#include "core/program.h"
#include "media.h"
#include "outcome_picture.h"
#include "movie.h"
#include "data/assets.h"
#include "platform/native/host.h"
#include "platform/native/render.h"
#include "port/arena.h"
#include "port/state_host.h"
#include "port/vars_host.h"
#include "port/vars_ram.h"
#include "presentation.h"
#ifdef CT_HAVE_SDL
#define SDL_MAIN_HANDLED
#include "window.h"
#include "bindings.h"
#endif
#include "vars.h"
#include "sound_native.h"
#include "route_driver.h"
#include "measure.h"

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

static void run_main_thread(Platform *pf, void *ctx) { program_main_thread(ctx, pf); }
static void run_vblank(Platform *pf, void *ctx) { program_vblank(ctx, pf); }
static void run_line(Platform *pf, void *ctx) { program_line(ctx, pf); }
static uint32_t take_owed(void *ctx) { return state_host_take_owed(ctx); }
static void game_accessed(void *ctx, uint32_t clocks) { state_host_game_access(ctx, clocks); }
/* --lag-log: each wait the time model makes last, where the frames the
 * original's work took are to be found */
static void lagged(void *ctx, uint32_t frame, uint32_t frames, uint32_t owed)
{
    (void)ctx;
    fprintf(stderr, "lag: frame %u, %u frame(s) for %u clocks of work\n", (unsigned)frame, (unsigned)frames, (unsigned)owed);
}

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
    unsigned width;              /* the view's: pixels across a row */
    unsigned long dark_since;    /* the frame the current dark run began; 0 while showing */
    unsigned long longest_from, longest;
} Picture;

static bool picture_is_dark(const uint32_t *px, unsigned width)
{
    for (unsigned i = 0; i < width * VID_RENDER_HEIGHT; i++) {
        uint32_t c = px[i];
        if (((c >> 16) & 0xFF) > DARK_LEVEL || ((c >> 8) & 0xFF) > DARK_LEVEL || (c & 0xFF) > DARK_LEVEL)
            return false;
    }
    return true;
}

static void picture_frame(Picture *p, Platform *pf, unsigned long frame)
{
    vid_render_frame(p->renderer, vid_host_picture(pf), p->pixels, NULL);
    if (!picture_is_dark(p->pixels, p->width)) {
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

/* --end-state: work RAM, then save RAM, as the outcome projection expects, for
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

static void report_crossing(const char *reader, const char *writer, uint32_t times, void *ctx)
{
    (void)ctx;
    fprintf(stderr, "overlay crossed: %s read bytes %s wrote, %lu time(s)\n", reader, writer, (unsigned long)times);
}

static void report_strays(const GameState *g)
{
    /* Owners the reimplementation keeps apart where the original shares bytes
     * (re/ram.toml's `overlays`): a read of bytes another owner wrote last is
     * one the original answered with that owner's value. */
    vars_host_crossings(state_host_vars_const(g), report_crossing, 0);
    uint32_t first[STATE_HOST_STRAYS_KEPT];
    unsigned kept;
    unsigned long n = state_host_strays(g, first, &kept);
    if (n) {
        fprintf(stderr, "strays: %lu access(es) outside the host's memory, the first at", n);
        for (unsigned i = 0; i < kept; i++)
            fprintf(stderr, " $%02X:%04X", (unsigned)(first[i] >> 16), (unsigned)(first[i] & 0xFFFF));
        fprintf(stderr, "\n");
    }
    const AssetFault *out = asset_outside_reads();
    if (out->count)
        fprintf(stderr, "strays: %lu cartridge read(s) outside the catalogue of resources, the first at $%02X:%04X\n",
                out->count, (unsigned)(out->first >> 16), (unsigned)(out->first & 0xFFFF));
}

/* The pack's bytes moved into the arena, where a snapshot's pointers into
 * them come back at the same place in a run laid out the same. */
static uint8_t *lasting(uint8_t *bytes, size_t len, AssetPack **pack)
{
    asset_pack_close(*pack);
    uint8_t *copy = arena_take(len);
    if (copy)
        memcpy(copy, bytes, len);
    free(bytes);
    const char *why = "";
    *pack = copy ? asset_pack_open(copy, len, &why) : NULL;
    return *pack ? copy : NULL;
}

/* The text of a kind of decoded resource beside the pack (frontend/decoded.h):
 * the file's, or where there is none, written from the pack once each
 * resource is seen to read back as the bytes it was written from. Two players
 * starting on one pack may both write it; the one that finds the file there
 * first reads it. NULL, after a warning, where there is no text to be had:
 * the pack's own resources serve. */
static char *decoded_text(const DecodedKind *kind, const char *path, const AssetStore *raw, size_t *len)
{
    const char *why = "";
    bool missing = false;
    char *text = (char *)files_read(path, len, true, &missing);
    if (text && !decoded_of_these_rows(kind, text, *len)) {
        fprintf(stderr, "ct_native: %s was written for another catalogue's %s, so its edits are not applied "
                        "and the pack's own %s play. To keep them, move the file away, start again to have "
                        "it written afresh, and make the edits again there.\n", path, kind->what, kind->what);
        free(text);
        return NULL;
    }
    if (text || !missing) {
        if (!text)
            fprintf(stderr, "ct_native: cannot read %s; playing the pack's own %s\n", path, kind->what);
        return text;
    }
    if (!decoded_round_trip(kind, raw, &why)) {
        fprintf(stderr, "ct_native: %s doesn't read back as its bytes; playing the pack's own %s\n", why, kind->what);
        return NULL;
    }
    text = decoded_export(kind, raw, len, &why);
    int written = text ? file_store_create(path, text, *len) : -1;
    if (written == 0)
        fprintf(stderr, "wrote the %s into %s\n", kind->what, path);
    else if (written > 0)
        free(text), text = (char *)files_read(path, len, true, NULL);
    else
        fprintf(stderr, "ct_native: cannot write %s; playing the pack's own %s\n", path, kind->what);
    return written < 0 ? (free(text), NULL) : text;
}

/* The pack made again with its palettes, fonts, graphics, maps, scripts, commands, text and records as the text beside it
 * says, each kind's store over the one before. True with the pack unchanged
 * where there is no text; false, the pack and its bytes gone, only where a
 * text is wrong or the remade pack can't be had. */
enum { DECODED_KINDS = 8 };

static bool serve_decoded(const Files *files, uint8_t **bytes, size_t *len, AssetPack **pack)
{
    const DecodedKind *kinds[DECODED_KINDS] = { &palette_text, &font_text, &tile_text, &map_text, &script_text,
                                                &command_text, &game_text, &record_text };
    const char *paths[DECODED_KINDS] = { files->palettes, files->fonts, files->graphics, files->maps, files->scripts,
                                         files->commands, files->text, files->records };
    Decoded *decoded[DECODED_KINDS] = { NULL };
    AssetStore stores[DECODED_KINDS + 1];
    asset_pack_store(*pack, &stores[0]);
    unsigned top = 0;
    bool wrong = false;
    const char *why = "";
    for (unsigned k = 0; k < DECODED_KINDS && !wrong; k++) {
        size_t text_len = 0;
        unsigned line = 0;
        char *text = decoded_text(kinds[k], paths[k], &stores[0], &text_len);
        if (!text)
            continue;
        decoded[k] = decoded_import(kinds[k], text, text_len, &stores[top], &line, &why);
        free(text);
        if (decoded[k])
            decoded_store(decoded[k], &stores[++top]);
        else
            fprintf(stderr, "ct_native: %s:%u: %s\n", paths[k], line, why), wrong = true;
    }
    if (!wrong && top == 0)
        return true;
    size_t made_len = 0;
    uint8_t *made = wrong ? NULL : asset_pack_build(&stores[top], ASSET_ROM_CRC32, &made_len, &why);
    for (unsigned k = 0; k < DECODED_KINDS; k++)
        decoded_close(decoded[k]);
    asset_pack_close(*pack);
    free(*bytes);
    *bytes = made;
    *len = made_len;
    *pack = made ? asset_pack_open(made, made_len, &why) : NULL;
    if (!*pack) {
        free(made);
        *bytes = NULL;
    }
    return *pack != NULL;
}

/* Read the shared pack, importing only when missing or incompatible. */
static uint8_t *load_pack(const Files *files, const char *rom_path, bool fresh, AssetPack **pack)
{
    size_t len = 0;
    const char *why = "";
    uint8_t *bytes = fresh ? NULL : files_read(files->assets, &len, false, NULL);
    *pack = bytes ? asset_pack_open(bytes, len, &why) : NULL;
    if (*pack && !asset_pack_is_the_cartridges(*pack)) {
        asset_pack_close(*pack);
        *pack = NULL;
        why = "its resources aren't the cartridge's";
    }
    if (*pack) return serve_decoded(files, &bytes, &len, pack) ? lasting(bytes, len, pack) : NULL;
    if (bytes) fprintf(stderr, "ct_native: %s: %s; importing again\n", files->assets, why);
    free(bytes);
    uint8_t *rom = rom_path ? files_read(rom_path, &len, false, NULL) : NULL;
    if (!rom) {
        fprintf(stderr, "ct_native: supply your Chrono Trigger (USA) ROM to import the assets\n");
        return NULL;
    }
    size_t size;
    const uint8_t *image = asset_rom_image(rom, len, &size);
    bytes = files_asset_pack(files, image, size, true, &len);
    free(rom);
    *pack = bytes ? asset_pack_open(bytes, len, &why) : NULL;
    if (!*pack) { free(bytes); return NULL; }
    if (!serve_decoded(files, &bytes, &len, pack)) return NULL;
    return lasting(bytes, len, pack);
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
    const void *pack;
    Track track;
    const char *state_path;
    uint32_t state_crc;
    bool state_ours, lag_log, timing_report;
    const char *record_path;
    const char *sram_path, *shot_dir;
    unsigned long frame, frames, shot_every;
    bool locations, outcomes, persist, save_pending;
    uint16_t location;
    uint8_t saved[STATE_HOST_SRAM_BYTES];
    int result;
    bool drive;
    uint16_t second_pad;
    unsigned long checks;       /* --check-snapshots' snapshots put back and played again */
    Measure *measure;           /* --measure */
} NativeDriver;

/* What the driver keeps in a snapshot beside the program: the frame it is
 * at, the picture's dark runs and the place last reported. */
typedef struct {
    uint64_t frame, dark_since, longest_from, longest;
    uint16_t location;
} DriverMark;

static SnapshotParts parts_of(const NativeDriver *d)
{
    return (SnapshotParts){ d->game, d->pf, d->sound, d->pack };
}

/* The program at the end of the last frame, as a snapshot's bytes. */
static uint8_t *native_snapshot(NativeDriver *d, size_t *len)
{
    if (d->result)
        return NULL;
    DriverMark mark = { d->frame, d->picture->dark_since, d->picture->longest_from, d->picture->longest, d->location };
    SnapshotParts parts = parts_of(d);
    return snapshot_take(&parts, CT_SNAPSHOT_BUILD, &mark, sizeof mark, len);
}

/* The program put back as a snapshot had it, and the picture drawn again
 * from the restored frame where the driver keeps one: the renderer's caches
 * describe the picture it drew last, so it starts afresh. */
static bool native_restore(NativeDriver *d, const uint8_t *bytes, size_t len)
{
    DriverMark mark;
    SnapshotParts parts = parts_of(d);
    if (!snapshot_restore(&parts, CT_SNAPSHOT_BUILD, bytes, len, &mark, sizeof mark))
        return false;
    d->frame = (unsigned long)mark.frame;
    d->picture->dark_since = (unsigned long)mark.dark_since;
    d->picture->longest_from = (unsigned long)mark.longest_from;
    d->picture->longest = (unsigned long)mark.longest;
    d->location = mark.location;
    d->result = 0;
    if (d->record_path)
        d->track.frames = (uint32_t)mark.frame;
    VidRenderer *renderer = vid_render_create();
    if (renderer) {
        vid_render_destroy(d->picture->renderer);
        d->picture->renderer = renderer;
    }
    if (!d->drive)
        vid_render_frame(d->picture->renderer, vid_host_picture(d->pf), d->picture->pixels, NULL);
    d->save_pending = true;
    return true;
}

static bool native_save_state(NativeDriver *d, const char *path)
{
    size_t len = 0, file_len = 0;
    uint8_t *snapshot = native_snapshot(d, &len);
    uint8_t *file = snapshot ? snapshot_file(snapshot, len, &file_len) : NULL;
    free(snapshot);
    if (!file || file_store_replace(path, file, file_len)) {
        fprintf(stderr, "ct_native: cannot save state %s%s\n", path, d->result ? ": the program has stopped" : "");
        free(file);
        return false;
    }
    d->state_crc = ct_crc32(file, file_len);
    d->state_ours = true;
    free(file);
    fprintf(stderr, "ct_native: saved frame %lu to %s\n", d->frame, path);
    return true;
}

static int native_load_state(NativeDriver *d, const char *path)
{
    size_t len = 0, snapshot_len = 0;
    uint8_t *data = files_read(path, &len, false, NULL);
    const uint8_t *snapshot = data ? snapshot_in_file(data, len, &snapshot_len) : NULL;
    bool ours = !d->record_path || (d->state_ours && ct_crc32(data, len) == d->state_crc);
    if (!snapshot || !ours || !native_restore(d, snapshot, snapshot_len)) {
        fprintf(stderr, "ct_native: cannot load %s: missing, damaged, of another build or memory layout, "
                        "or outside this recording\n", path);
        free(data);
        return -1;
    }
    free(data);
    fprintf(stderr, "ct_native: loaded frame %lu from %s\n", d->frame, path);
    return 0;
}

#ifdef CT_HAVE_SDL
static void native_quick_save(void *ctx)
{
    NativeDriver *d = ctx;
    native_save_state(d, d->state_path);
}
static void native_quick_load(void *ctx)
{
    NativeDriver *d = ctx;
    (void)native_load_state(d, d->state_path);
}
#endif

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
    if (d->result) return 1;
    if (d->record_path && d->track.frames != d->track.written_frames) {
        size_t len;
        char *text = track_text(&d->track, "ct_native (native timing)", &len);
        if (!text || file_store_replace(d->record_path, text, len)) {
            fprintf(stderr, "ct_native: cannot write recording %s\n", d->record_path);
            free(text);
            d->result = 2;
            return 1;
        }
        free(text);
        d->track.written_frames = d->track.pending_frames;
    }
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

/* The location the run is in, read as an observer: a look the game's time
 * doesn't count, so reporting the places never changes the run. */
static uint16_t observed_location(GameState *g)
{
    bool was = vars_peeking();
    vars_peek(true);
    uint16_t location = location_id(g);
    vars_peek(was);
    return location;
}

/* One frame of the program, with the pads it reads. */
static bool native_frame(void *ctx, uint32_t pads)
{
    NativeDriver *d = ctx;
    in_host_set(d->pf, 0, (uint16_t)pads);
    in_host_set(d->pf, 1, (uint16_t)(pads >> 16));
    uint64_t began = d->measure ? measure_now_ns() : 0;
    tm_host_frame(d->pf);
    uint64_t game_done = d->measure ? measure_now_ns() : 0;
    sound_native_frame(d->sound);
    uint64_t sound_done = d->measure ? measure_now_ns() : 0;
    d->frame++;
    if (!d->drive)
        picture_frame(d->picture, d->pf, d->frame);
    if (d->measure)
        measure_frame(d->measure, d->game, game_done - began, sound_done - game_done, measure_now_ns() - sound_done);
    if (d->outcomes && outcome_picture_due(d->frame, d->frames))
        outcome_picture(d->picture->pixels, d->picture->width, VID_RENDER_HEIGHT, d->frame);
    if (d->locations && observed_location(d->game) != d->location) {
        d->location = observed_location(d->game);
        fprintf(stderr, "location: frame %lu $%03X\n", d->frame, d->location & LOCATION_BITS);
    }
    if (d->shot_every && d->frame % d->shot_every == 0) {
        char path[1024];
        snprintf(path, sizeof path, "%s/frame_%06lu.png", d->shot_dir, d->frame);
        if (ct_png_write(path, d->picture->pixels, d->picture->width, VID_RENDER_HEIGHT) != 0)
            fprintf(stderr, "ct_native: cannot write %s\n", path);
    }
    uint32_t routine;
    const char *what;
    if (state_host_stopped(d->game, &routine, &what)) {
        fprintf(stderr, "frame %lu: the program's C goes no further: $%02X:%04X, %s\n",
                d->frame, (unsigned)(routine >> 16), (unsigned)(routine & 0xFFFF), what);
        d->result = 1;
    }
    return !d->result;
}

static void native_step(void *ctx, uint16_t buttons)
{
    NativeDriver *d = ctx;
    uint16_t second = d->second_pad;
    /* A movie owns input through its last event; live play then takes over. */
    if (d->movie && d->frame < d->movie->length) {
        buttons = ct_movie_buttons(d->movie, (uint32_t)d->frame);
        second = ct_movie_buttons2(d->movie, (uint32_t)d->frame);
    }
    if (d->record_path && (d->frame >= UINT32_MAX || !track_set(&d->track, (uint32_t)d->frame, buttons))) {
        fprintf(stderr, "ct_native: cannot extend recording\n");
        d->result = 2;
        return;
    }
    (void)native_frame(d, buttons | (uint32_t)second << 16);
    /* Persist only complete frames, immediately after the game saves. */
    if (d->save_pending || (d->record_path && d->frame % 600 == 0))
        native_flush(d);
}

static bool native_done(void *ctx)
{
    NativeDriver *d = ctx;
    return d->result || d->frame >= d->frames;
}

static bool route_step(void *ctx, uint32_t pads)
{
    NativeDriver *d = ctx;
    d->second_pad = (uint16_t)(pads >> 16);
    native_step(d, (uint16_t)pads);
    sound_native_discard_audio(d->sound);
    return !d->result;
}
static uint8_t route_read(void *ctx, uint32_t addr) { return state_host_read8(((NativeDriver *)ctx)->game, addr); }
static void route_write(void *ctx, uint32_t addr, uint8_t v) { state_host_write8(((NativeDriver *)ctx)->game, addr, v); }
static bool route_shot(void *ctx, const char *path)
{
    NativeDriver *d = ctx;
    picture_frame(d->picture, d->pf, d->frame);
    return ct_png_write(path, d->picture->pixels, d->picture->width, VID_RENDER_HEIGHT) == 0;
}
/* The frame's background layers in `layers` alone, for comparing a layer
 * with a map drawn from its resources (tests/test_map_pictures.py): every
 * line's screens hold just those layers, with no windows, color math or
 * mosaic, at full brightness, and its colors, drawn by a renderer of its own so the
 * player's picture keeps its cache. */
static bool route_layer_shot(void *ctx, const char *path, uint8_t layers, uint16_t scroll[8], uint16_t colors[256])
{
    NativeDriver *d = ctx;
    VidFrame *f = malloc(sizeof *f);
    VidRenderer *r = vid_render_create();
    uint32_t *pixels = malloc(VID_RENDER_MAX_PIXELS * sizeof *pixels);
    bool ok = f && r && pixels;
    if (ok) {
        *f = *vid_host_picture(d->pf);
        for (unsigned i = 0; i < f->span_count; i++) {
            VidLineState *s = &f->spans[i].state;
            s->main_layers = layers;
            s->sub_layers = 0;
            s->windows.win_main = s->windows.win_sub = 0;
            s->math.layers = 0;
            s->math.clip_black = s->math.prevent_math = VID_REGION_NEVER;
            s->mosaic_size = 1;
            s->display.brightness = 15;
            s->display.blank = false;
        }
        for (unsigned k = 0; k < 4; k++) {
            scroll[2 * k] = f->spans[0].state.bg[k].hofs;
            scroll[2 * k + 1] = f->spans[0].state.bg[k].vofs;
        }
        memcpy(colors, f->cgram, 256 * sizeof *colors);
        vid_render_frame(r, f, pixels, NULL);
        ok = ct_png_write(path, pixels, (unsigned)vid_render_width(f), VID_RENDER_HEIGHT) == 0;
    }
    free(pixels);
    if (r)
        vid_render_destroy(r);
    free(f);
    return ok;
}
static uint8_t *route_snapshot(void *ctx, size_t *len) { return native_snapshot(ctx, len); }
static bool route_restore(void *ctx, const uint8_t *bytes, size_t len) { return native_restore(ctx, bytes, len); }

#ifdef CT_HAVE_SDL
static const uint32_t *native_pixels(void *ctx) { return ((NativeDriver *)ctx)->picture->pixels; }
static size_t native_audio(void *ctx, int16_t *out, size_t frames) { return sound_native_take_audio(((NativeDriver *)ctx)->sound, out, frames); }
static void native_discard(void *ctx) { sound_native_discard_audio(((NativeDriver *)ctx)->sound); }
static const char *native_halt(void *ctx) { (void)ctx; return NULL; } /* stops are printed, then exit 1 */
static void native_title(void *ctx, bool paused, char *out, size_t size)
{
    NativeDriver *d = ctx;
    snprintf(out, size, "ct_native - frame %lu%s%s", d->frame, paused ? " [paused]" : "",
             d->record_path ? " [recording]" : d->movie && d->frame < d->movie->length ? " [movie]" : "");
}
#endif

/* --check-snapshots: every `every` frames, a snapshot is taken (through a
 * file's bytes), the next `span` frames played, the snapshot put back and the
 * same frames played again; each frame's game state, picture and sound must
 * come out the same both times. */
typedef struct {
    uint32_t state, picture, sound;
} FrameDigest;

static FrameDigest frame_digest(NativeDriver *d, uint8_t *state)
{
    state_host_snapshot(d->game, state);
    int16_t audio[8192 * 2];
    size_t count = sound_native_take_audio(d->sound, audio, 8192);
    return (FrameDigest){ ct_crc32(state, state_host_snapshot_bytes()),
                          ct_crc32((const uint8_t *)d->picture->pixels, d->picture->width * VID_RENDER_HEIGHT * sizeof(uint32_t)),
                          ct_crc32((const uint8_t *)audio, count * 2 * sizeof *audio) };
}

static void check_snapshot(NativeDriver *d, unsigned long span)
{
    size_t len = 0, file_len = 0, back_len = 0;
    uint8_t *snapshot = native_snapshot(d, &len);
    uint8_t *file = snapshot ? snapshot_file(snapshot, len, &file_len) : NULL;
    const uint8_t *back = file ? snapshot_in_file(file, file_len, &back_len) : NULL;
    FrameDigest *first = calloc(span, sizeof *first);
    uint8_t *state = malloc(state_host_snapshot_bytes());
    unsigned long from = d->frame, played = 0;
    if (!back || !first || !state) {
        fprintf(stderr, "snapshot check: no snapshot at frame %lu\n", from);
        d->result = 1;
    }
    sound_native_discard_audio(d->sound);
    for (; !d->result && played < span && !native_done(d); played++) {
        native_step(d, 0);
        first[played] = frame_digest(d, state);
    }
    if (!d->result && !native_restore(d, back, back_len)) {
        fprintf(stderr, "snapshot check: the snapshot of frame %lu didn't restore\n", from);
        d->result = 1;
    }
    sound_native_discard_audio(d->sound);
    for (unsigned long i = 0; !d->result && i < played; i++) {
        native_step(d, 0);
        FrameDigest again = frame_digest(d, state);
        const char *what = again.state != first[i].state ? "game state" : again.picture != first[i].picture ? "picture"
                         : again.sound != first[i].sound ? "sound" : NULL;
        if (what) {
            fprintf(stderr, "snapshot check: frame %lu's %s differs after restoring frame %lu\n", d->frame, what, from);
            d->result = 1;
        }
    }
    if (!d->result)
        d->checks++;
    free(state);
    free(first);
    free(file);
    free(snapshot);
}

static void usage(FILE *out)
{
    fprintf(out, "usage: ct_native [ROM | --rom ROM] [--assets pack] [--frames N] [--sram save.srm] [--movie in.ctm]\n"
                 "                 [--load-state FILE] [--save-state FILE] [--record out.ctm] [--save-dir DIR] [--import ROM] [--keys FILE] [--bind INPUT=ACTION]\n"
                 "                 [--headless | --window | --drive] [--scale N] [--fullscreen] [--fullscreen-mode borderless|exclusive]\n"
                 "                 [--scaling integer|fit] [--filter none|scanlines] [--pixels tv|square] [--mute]\n"
                 "                 [--view WIDTHx224|16:9|16:10|21:9|4:3] [--present WxH]\n"
                 "                 [--check-snapshots EVERY[,SPAN]] [--screenshot FILE] [--wav FILE] [--shot-every N --shot-dir DIR] [--end-state out.bin] [--locations] [--lag-log] [--timing-report] [--measure] [--outcomes] [--scene flags[,song]]...\n"
                 "The first launch imports your ROM; later launches use the remembered asset pack.\n"
                 "--frames selects headless unless --window is given; --headless defaults to 600 frames.\n"
                 "Window play writes cartridge saves; headless runs only read them. F5 saves a snapshot of the frame; F7 loads it.\n"
                 "The window remembers its scaling, filter and fullscreen; --present WxH makes --screenshot show the picture\n"
                 "as the window would on a WxH display.\n");
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

/* --view WIDTHx224: a view as wide as the original's screen or wider, up to
 * the widest the renderer draws, and as tall; or a screen shape (16:9, 16:10,
 * 21:9, 4:3) for the presentation to work the width out from. */
static bool view_from(const char *text, ViewShape *shape, int *out)
{
    char head[16];
    const char *x = strchr(text, 'x');
    size_t n = x ? (size_t)(x - text) : 0;
    unsigned long width, height;
    if (strchr(text, ':'))
        return present_view_from(text, shape, out) && *shape != VIEW_WIDTH;
    if (!x || n == 0 || n >= sizeof head)
        return false;
    memcpy(head, text, n);
    head[n] = 0;
    if (!count_from(head, &width) || !count_from(x + 1, &height) || width < VID_ORIGINAL_VIEW_WIDTH ||
        width > VID_MAX_VIEW_WIDTH || height != VID_ORIGINAL_VIEW_HEIGHT)
        return false;
    *shape = VIEW_WIDTH;
    *out = (int)width;
    return true;
}

#ifdef CT_HAVE_SDL
/* --present WIDTHxHEIGHT: an output for the window's presentation, from a
 * pixel square up to an 8K display. */
static bool size_from(const char *text, int *w, int *h)
{
    char head[16];
    const char *x = strchr(text, 'x');
    size_t n = x ? (size_t)(x - text) : 0;
    unsigned long width, height;
    if (!x || n == 0 || n >= sizeof head)
        return false;
    memcpy(head, text, n);
    head[n] = 0;
    if (!count_from(head, &width) || !count_from(x + 1, &height) || width < 1 || height < 1 || width > 7680 ||
        height > 4320)
        return false;
    *w = (int)width;
    *h = (int)height;
    return true;
}
#endif

/* --check-snapshots EVERY[,SPAN]: both whole numbers above 0. */
static bool checks_from(const char *text, unsigned long *every, unsigned long *span)
{
    char head[32];
    const char *comma = strchr(text, ',');
    size_t n = comma ? (size_t)(comma - text) : strlen(text);
    if (n >= sizeof head)
        return false;
    memcpy(head, text, n);
    head[n] = 0;
    return count_from(head, every) && *every && (!comma || (count_from(comma + 1, span) && *span));
}

int main(int argc, char **argv)
{
    const char *rom_path = NULL, *sram_path = NULL, *pack_path = NULL, *movie_path = NULL, *shot_dir = NULL;
    const char *load_state = NULL, *save_state = NULL, *wav_path = NULL, *screenshot = NULL;
    const char *end_state = NULL, *save_dir = NULL, *record_path = NULL;
    bool import_only = false, help = false;
#ifdef CT_HAVE_SDL
    const char *keys_path = NULL, *binds[128];
    int nbinds = 0;
#endif
    Files files = { 0 };
    unsigned long frames = 600, shot_every = 0, scale = 3, check_every = 0, check_span = 60;
    bool frames_given = false, mute = false;
    bool drive = false;
    VidView view = { VID_ORIGINAL_VIEW_WIDTH, VID_ORIGINAL_VIEW_HEIGHT };
    int display_mode = 0;   /* auto, headless, window */
    /* The presentation as given on the command line, over what the last
     * window play left (frontend/presentation.h): the view's width among it. */
    PresentSettings asked = PRESENT_SETTINGS_DEFAULT;
    bool scaling_asked = false, filter_asked = false, fullscreen_asked = false, mode_asked = false;
    bool pixels_asked = false, view_asked = false;
#ifdef CT_HAVE_SDL
    SDL_SetMainReady();
    bindings_reset();
    int present_w = 0, present_h = 0;
#endif
    bool locations = false, outcomes = false, lag_log = false, timing_report = false, measuring = false;
    unsigned scenes = 0;
    uint8_t scene[STATE_HOST_FIRST_SCENES], song[STATE_HOST_FIRST_SCENES];
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--rom") && v) rom_path = v, i++;
        else if (!strcmp(a, "--import") && v) rom_path = v, import_only = true, i++;
        else if (!strcmp(a, "--wav") && v) wav_path = v, i++;
        else if (!strcmp(a, "--screenshot") && v) screenshot = v, i++;
        else if (!strcmp(a, "--load-state") && v) load_state = v, i++;
        else if (!strcmp(a, "--save-state") && v) save_state = v, i++;
        else if (!strcmp(a, "--save-dir") && v) save_dir = v, i++;
        else if (!strcmp(a, "--record") && v) record_path = v, i++;
#ifdef CT_HAVE_SDL
        else if (!strcmp(a, "--keys") && v) keys_path = v, i++;
        else if (!strcmp(a, "--bind") && v && nbinds < 128) binds[nbinds++] = v, i++;
        else if (!strcmp(a, "--present") && v && size_from(v, &present_w, &present_h)) i++;
#endif
        else if (!strcmp(a, "--fullscreen")) asked.fullscreen = fullscreen_asked = true;
        else if (!strcmp(a, "--fullscreen-mode") && v && present_fullscreen_mode_from(v, &asked.fullscreen_mode)) mode_asked = true, i++;
        else if (!strcmp(a, "--scaling") && v && present_scaling_from(v, &asked.scaling)) scaling_asked = true, i++;
        else if (!strcmp(a, "--filter") && v && present_filter_from(v, &asked.filter)) filter_asked = true, i++;
        else if (!strcmp(a, "--pixels") && v && present_pixel_shape_from(v, &asked.pixels)) pixels_asked = true, i++;
        else if (a[0] != '-' && !rom_path) rom_path = a;
        else if (!strcmp(a, "--sram") && v) sram_path = v, i++;
        else if (!strcmp(a, "--assets") && v) pack_path = v, i++;
        else if (!strcmp(a, "--movie") && v) movie_path = v, i++;
        else if (!strcmp(a, "--frames") && v && count_from(v, &frames)) frames_given = true, i++;
        else if (!strcmp(a, "--headless")) display_mode = 1;
        else if (!strcmp(a, "--drive")) drive = true;
        else if (!strcmp(a, "--window")) display_mode = 2;
        else if (!strcmp(a, "--scale") && v && count_from(v, &scale) && scale >= 1 && scale <= 10) i++;
        else if (!strcmp(a, "--view") && v && view_from(v, &asked.view, &asked.view_width)) view_asked = true, i++;
        else if (!strcmp(a, "--mute")) mute = true;
        else if (!strcmp(a, "--help")) help = true;
        else if (!strcmp(a, "--shot-every") && v && count_from(v, &shot_every)) i++;
        else if (!strcmp(a, "--shot-dir") && v) shot_dir = v, i++;
        else if (!strcmp(a, "--check-snapshots") && v && checks_from(v, &check_every, &check_span)) i++;
        else if (!strcmp(a, "--end-state") && v) end_state = v, i++;
        else if (!strcmp(a, "--locations")) locations = true;
        else if (!strcmp(a, "--lag-log")) lag_log = true;
        else if (!strcmp(a, "--timing-report")) timing_report = true;
        else if (!strcmp(a, "--measure")) measuring = true;
        else if (!strcmp(a, "--outcomes")) outcomes = true;
        else if (!strcmp(a, "--scene") && v && scenes < STATE_HOST_FIRST_SCENES && scene_and_song(v, &scene[scenes], &song[scenes]))
            scenes++, i++;
        else {
            usage(stderr);
            return 2;
        }
    }
#ifdef CT_HAVE_SDL
    if (bindings_configure(keys_path, binds, nbinds)) return 2;
#endif
    if (help) { usage(stdout); return 0; }
    if (shot_every && !shot_dir) {
        fprintf(stderr, "ct_native: --shot-every needs --shot-dir\n");
        return 2;
    }
    if (record_path && (scenes || load_state)) {
        fprintf(stderr, "ct_native: recordings start from power-on; --scene and --load-state cannot be recorded\n");
        return 2;
    }
    if (drive && (movie_path || scenes || timing_report || display_mode == 2 || record_path || load_state || save_state || wav_path)) {
        fprintf(stderr, "ct_native: --drive cannot combine with movies, scenes, timing reports, window play, recording, state files or WAV output\n");
        return 2;
    }
    if (drive) display_mode = 1;
    bool windowed = display_mode == 2 || (display_mode == 0 && !frames_given);
#ifndef CT_HAVE_SDL
    if (display_mode == 2) {
        fprintf(stderr, "ct_native: --window needs a build with SDL2\n");
        return 2;
    }
    windowed = false;
    (void)mute;
#endif
    if (display_mode == 1)
        windowed = false;
    if (windowed && wav_path) { fprintf(stderr, "ct_native: --wav needs headless mode\n"); return 2; }
    if (check_every && (windowed || drive || wav_path || record_path)) {
        fprintf(stderr, "ct_native: --check-snapshots runs headless, without --wav or --record\n");
        return 2;
    }
    if (windowed && !frames_given)
        frames = ULONG_MAX;
    /* A stable per-user location lets later launches omit the ROM entirely.
     * Explicit ROM paths retain ct_native's adjacent save/pack naming. */
    const char *preferences = getenv("CT_NATIVE_HOME");
#ifdef CT_HAVE_SDL
    char *system_preferences = NULL;
    if (!preferences && (windowed || import_only || (!rom_path && !pack_path))) {
        system_preferences = SDL_GetPrefPath("chrono-trigger-reimplemented", "ct_native");
        preferences = system_preferences;
    }
#endif
    char remembered[4096] = "", config[1024] = "", window_settings[1024] = "";
    const char *remembered_sram = NULL;
    if (preferences) snprintf(config, sizeof config, "%s/assets-path.txt", preferences);
    if (preferences) snprintf(window_settings, sizeof window_settings, "%s/window.txt", preferences);
    /* The presentation: window play's remembered, then the command line's.
     * The view's width is worked out once, here: the game draws this view. */
    PresentSettings present = PRESENT_SETTINGS_DEFAULT;
    if (windowed && window_settings[0]) {
        size_t len = 0;
        uint8_t *text = files_read(window_settings, &len, true, NULL);
        if (text) present_settings_read(&present, (const char *)text, len);
        free(text);
    }
    if (scaling_asked) present.scaling = asked.scaling;
    if (filter_asked) present.filter = asked.filter;
    if (fullscreen_asked) present.fullscreen = true;
    if (mode_asked) present.fullscreen_mode = asked.fullscreen_mode;
    if (pixels_asked) present.pixels = asked.pixels;
    if (view_asked) present.view = asked.view, present.view_width = asked.view_width;
    view.width = (uint16_t)present_view_width(&present, VID_RENDER_HEIGHT, VID_ORIGINAL_VIEW_WIDTH, VID_MAX_VIEW_WIDTH);
#ifdef CT_HAVE_SDL
    const Display display = { view.width };
#endif
    if (!rom_path && !pack_path && config[0]) {
        size_t len;
        uint8_t *data = files_read(config, &len, false, NULL);
        if (data && len && len < sizeof remembered) {
            memcpy(remembered, data, len);
            remembered[len] = 0;
            char *newline = strchr(remembered, '\n');
            if (newline) {
                *newline++ = 0;
                remembered_sram = newline;
                newline = strchr(newline, '\n');
                if (newline) *newline = 0;
            }
            pack_path = remembered;
        }
        free(data);
    }
    const char *identity = rom_path ? rom_path : pack_path ? pack_path : "ct-usa";
    files_side_path(files.assets, sizeof files.assets, identity, identity, save_dir, ".assets");
    if (pack_path) snprintf(files.assets, sizeof files.assets, "%s", pack_path);
    files_side_path(files.palettes, sizeof files.palettes, files.assets, files.assets, NULL, ".palettes");
    files_side_path(files.fonts, sizeof files.fonts, files.assets, files.assets, NULL, ".fonts");
    files_side_path(files.graphics, sizeof files.graphics, files.assets, files.assets, NULL, ".graphics");
    files_side_path(files.maps, sizeof files.maps, files.assets, files.assets, NULL, ".maps");
    files_side_path(files.text, sizeof files.text, files.assets, files.assets, NULL, ".text");
    files_side_path(files.records, sizeof files.records, files.assets, files.assets, NULL, ".records");
    files_side_path(files.scripts, sizeof files.scripts, files.assets, files.assets, NULL, ".scripts");
    files_side_path(files.commands, sizeof files.commands, files.assets, files.assets, NULL, ".commands");
    files_side_path(files.sram, sizeof files.sram, identity, identity, save_dir, ".srm");
    if (remembered_sram && !save_dir) snprintf(files.sram, sizeof files.sram, "%s", remembered_sram);
    files_side_path(files.state, sizeof files.state, record_path ? record_path : files.sram, files.sram, save_dir, ".native-state");
    AssetPack *pack;
    uint8_t *pack_bytes = load_pack(&files, rom_path, import_only, &pack);
    if (!pack_bytes) return 2;
    if (config[0] && (windowed || import_only)) {
        char absolute[2048], save_absolute[2048], paths[4098];
        if (files_absolute(absolute, sizeof absolute, files.assets) &&
            files_absolute(save_absolute, sizeof save_absolute, files.sram)) {
            int count = snprintf(paths, sizeof paths, "%s\n%s\n", absolute, save_absolute);
            if (count < 0 || (size_t)count >= sizeof paths || file_store_replace(config, paths, (size_t)count))
                fprintf(stderr, "ct_native: cannot remember asset location %s\n", config);
        }
    }
#ifdef CT_HAVE_SDL
    SDL_free(system_preferences);
#endif
    if (import_only) { asset_pack_close(pack); return 0; }
    ct_movie movie;
    if (movie_path && ct_movie_load(&movie, movie_path) != 0) return 2;
    if (record_path && movie_path && ct_movie_uses_controller2(&movie)) {
        fprintf(stderr, "ct_native: the shared recorder supports controller 1 only\n");
        return 2;
    }
    char found_sram[1024];
    if (movie_path && movie.has_sram && !sram_path &&
        ct_sram_find(movie.sram_crc, movie_path, identity, found_sram, sizeof found_sram) == 0)
        sram_path = found_sram;
    static AssetSource source, counted;
    asset_pack_source(pack, &source);
    Measure *measure = measuring ? measure_create(&source, &counted) : NULL;
    assets_use(measure ? &counted : &source);
    bool fixed_sram = sram_path != NULL;
    if (windowed && !sram_path && !movie_path && !record_path) sram_path = files.sram;
    long sram_len = 0;
    errno = 0;
    uint8_t *sram = sram_path ? read_file(sram_path, STATE_HOST_SRAM_BYTES + 1, &sram_len) : NULL;
    bool new_save = windowed && !sram && errno == ENOENT;
    if (sram_path && !new_save && (!sram || sram_len != STATE_HOST_SRAM_BYTES)) {
        fprintf(stderr, "ct_native: %s is not an 8 KiB cartridge save\n", sram_path);
        return 2;
    }
    if (movie_path && movie.has_sram && (!sram || ct_crc32(sram, (size_t)sram_len) != movie.sram_crc)) {
        fprintf(stderr, "ct_native: movie needs --sram with CRC-32 %08X\n", (unsigned)movie.sram_crc);
        return 2;
    }
    GameState *g = state_host_create_lasting(sram);
    static Platform pf;
#ifdef CT_LEAN
    if (timing_report) {
        fprintf(stderr, "ct_native: the lean build (CT_LEAN) keeps no timing report\n");
        return 2;
    }
#endif
    if (!g || (timing_report && !state_host_record_time(g))) {
        fprintf(stderr, "ct_native: out of memory\n");
        return 2;
    }

    pf.save.bytes = state_host_sram(g);
    for (unsigned n = 0; n < scenes; n++)
        program_starts_in_a_scene(g, scene[n], song[n]);
    vid_host_start(&pf, read_game, g);
    SoundNative *sound = sound_native_create(g, &pf);
    if (!sound) {
        fprintf(stderr, "ct_native: out of memory\n");
        return 2;
    }
    static uint32_t pixels[VID_RENDER_MAX_PIXELS];
    vid_host_set_view(&pf, view);
    Picture picture = { vid_render_create(), pixels, view.width, 0, 0, 0 };
    if (!picture.renderer) {
        fprintf(stderr, "ct_native: out of memory\n");
        return 2;
    }
    tm_host_start(&pf, &(TmHostProgram){ run_main_thread, run_vblank, run_line, g, take_owed, game_accessed,
                                         lag_log ? lagged : NULL }, 1);
    NativeDriver driver = {
        .game = g, .pf = &pf, .sound = sound, .picture = &picture,
        .movie = movie_path ? &movie : NULL, .pack = pack_bytes, .sram_path = sram_path,
        .shot_dir = shot_dir, .frames = frames, .shot_every = shot_every,
        .locations = locations, .outcomes = outcomes, .persist = windowed && !movie_path && !record_path, .location = 0xFFFF, .drive = drive,
        .measure = measure
    };
    driver.state_path = files.state;
    driver.lag_log = lag_log;
    driver.timing_report = timing_report;
    driver.record_path = record_path;
    driver.track.record_name = record_path ? files_name(record_path) : NULL;
    driver.track.sram_name = fixed_sram ? files_name(sram_path) : NULL;
    driver.track.sram_crc = ct_crc32(state_host_sram(g), STATE_HOST_SRAM_BYTES);
    driver.track.movie_name = movie_path ? files_name(movie_path) : NULL;
    memcpy(driver.saved, state_host_sram(g), sizeof driver.saved);
    pf.save.ctx = &driver;
    pf.save.written = native_save_written;
    int rc = 0;
    if (load_state && native_load_state(&driver, load_state)) return 2;
    if (drive) {
        RouteHost host = { &driver, route_step, route_read, route_write, route_shot, route_layer_shot, route_snapshot, route_restore };
        rc = route_driver(&host, driver.saved, sizeof driver.saved, asset_catalogue_version, stdin, stdout);
        g = driver.game;
        sound = driver.sound;
        picture_frame(&picture, &pf, driver.frame);
    } else
#ifdef CT_HAVE_SDL
    if (windowed) {
        WindowOptions options = {
            .scale = (int)scale, .mute = mute, .display = display, .present = present
        };
        WindowClient client = {
            .ctx = &driver, .name = "ct_native", .step = native_step, .pixels = native_pixels,
            .audio = { &driver, native_audio, native_discard }, .title = native_title,
            .halt = native_halt, .done = native_done, .flush = native_flush,
            .quick_save = native_quick_save, .quick_load = native_quick_load
        };
        rc = window_run(&client, &options);
        char settings[256];
        size_t len = present_settings_write(&options.present, settings, sizeof settings);
        if (window_settings[0] && len && file_store_replace(window_settings, settings, len))
            fprintf(stderr, "ct_native: cannot remember the window's settings in %s\n", window_settings);
    } else
#endif
    {
        ct_wav wav = { 0 };
        if (wav_path && ct_wav_open(&wav, wav_path, AU_SAMPLE_RATE)) return 2;
        while (!native_done(&driver)) {
            if (check_every && driver.frame && driver.frame % check_every == 0)
                check_snapshot(&driver, check_span);
            if (native_done(&driver))
                break;
            native_step(&driver, 0);
            if (wav_path) {
                int16_t audio[8192 * 2];
                size_t count = sound_native_take_audio(driver.sound, audio, 8192);
                ct_wav_write(&wav, audio, count);
            } else sound_native_discard_audio(driver.sound);
        }
        if (wav_path && ct_wav_close(&wav)) driver.result = 2;
    }
#ifdef CT_HAVE_SDL
    if (screenshot && present_w) {
        uint32_t *shown = malloc((size_t)present_w * (size_t)present_h * sizeof *shown);
        if (!shown || window_present_picture(picture.pixels, &display, &present, present_w, present_h, shown) ||
            ct_png_write(screenshot, shown, (unsigned)present_w, (unsigned)present_h))
            driver.result = 2;
        free(shown);
    } else
#endif
    if (screenshot && ct_png_write(screenshot, picture.pixels, picture.width, VID_RENDER_HEIGHT)) driver.result = 2;
    if (save_state && !driver.result && !native_save_state(&driver, save_state)) driver.result = 2;
    native_flush(&driver);
    free(driver.track.pads);
    g = driver.game;
    sound = driver.sound;
    if (driver.result)
        rc = driver.result;
    unsigned long frame = driver.frame;
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
    if (check_every)
        fprintf(stderr, "snapshots: %lu put back and played again, each frame the same\n", driver.checks);
    report_strays(g);
    if (measure) {
        measure_report(measure, stderr, &pf);
        measure_destroy(measure);
    }
    if (end_state && write_end_state(end_state, g) != 0) {
        fprintf(stderr, "ct_native: cannot write %s\n", end_state);
        rc = 2;
    }
    vid_render_destroy(picture.renderer);
    fiber_destroy_tree(pf.timing.main);
    sound_native_destroy(sound);
    vid_host_stop(&pf);
    if (timing_report) {
        unsigned count;
        const StateHostTime *times = state_host_time(g, &count);
        for (unsigned k = 0; k < count; k++) {
            const StateHostTime *t = &times[k];
            if (t->tags || t->bus_clocks || t->charged_clocks)
                fprintf(stderr, "native time: %06X tags %" PRIu64 " bus %" PRIu64
                        " charged %" PRIu64 " milli %" PRIu64 "\n",
                        (unsigned)t->routine, t->tags, t->bus_clocks, t->charged_clocks, t->milli_clocks);
        }
    }
    state_host_destroy(g);
    free(sram);
    if (movie_path)
        ct_movie_free(&movie);
    assets_use(NULL);
    asset_pack_close(pack);
    return rc;
}
