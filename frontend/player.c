#include "player.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "movie.h"
#include "platform/audio.h"
#include "platform/input.h"
#include "port/machine.h"
#include "port/recomp_table.h"
#include "registry.h"
#include "snes/zst.h"
#include "trace.h"

const char *const player_build_mode[3] = {
    "with the interpreter (every --native mode)",
    "generated code only, the interpreter as a logged fallback for misses (CT_PLAYER_INTERPRETER=FALLBACK)",
    "generated code only, without the interpreter (CT_PLAYER_INTERPRETER=OFF)",
};
static const char *const title_mode[] = { "", " [generated code, fallback]", " [generated code only]" };

/* Misses reported on stderr as they happen, at most. */
#define MISSES_REPORTED 20

static const char *file_name(const char *path)
{
    const char *name = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\')
            name = p + 1;
    }
    return name;
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

/* ---- misses ------------------------------------------------------------------- */

#if CT_PLAYER_NO_INTERPRETER
/* The system found no native code for the instruction at PC. */
static bool on_miss(void *ctx, Snes *snes)
{
    Player *pl = ctx;
    const Cpu *c = &snes->cpu;
    size_t before = misses_count(pl->misses, NULL);
    misses_note(pl->misses, cpu_pc24(c), c->p, c->e);
    pl->miss_steps++;
    char what[200];
    if (CT_PLAYER_NO_INTERPRETER == 2) {
        misses_describe(snes, what, sizeof what);
        snprintf(pl->halt_reason, sizeof pl->halt_reason, "no generated code for %s", what);
        fprintf(stderr, "ct_play: stopped at frame %u: %s. This player has no interpreter to run it; the address is "
                        "logged to %s for the build's CT_RECOMP_COVERAGE.\n",
                (unsigned)snes->frame, what, pl->misses_path);
        return false;
    }
    if (misses_count(pl->misses, NULL) > before && before < MISSES_REPORTED) {
        misses_describe(snes, what, sizeof what);
        fprintf(stderr, "ct_play: frame %u: no generated code for %s; running it on the interpreter (logged to %s)\n",
                (unsigned)snes->frame, what, pl->misses_path);
    }
    return true;
}
#endif

void player_misses_flush(Player *pl)
{
#if CT_PLAYER_NO_INTERPRETER
    if (pl->miss_steps == pl->miss_steps_written)
        return;
    if (misses_write(pl->misses, pl->misses_path, NULL) != 0) {
        fprintf(stderr, "cannot write %s\n", pl->misses_path);
        return;
    }
    pl->miss_steps_written = pl->miss_steps;
#else
    (void)pl;
#endif
}

const char *player_halt_reason(const Player *pl)
{
#if CT_PLAYER_NO_INTERPRETER
    return pl->halt_reason;
#else
    (void)pl;
    return "an instruction without native code";
#endif
}

const char *player_misses_path(const Player *pl)
{
#if CT_PLAYER_NO_INTERPRETER
    return pl->misses_path;
#else
    (void)pl;
    return "the miss report";
#endif
}

static void on_line(void *ctx, Snes *snes, uint16_t line)
{
    Player *pl = ctx;
    if (pl->scanline) {
        render_line(&pl->renderer, &snes->ppu, line);
        return;
    }
    vid_decode_line(pl->decoder, &snes->ppu, line);
    if (line != RENDER_HEIGHT)
        return;
    VidRenderFlags flags;
    vid_render_frame(pl->native_render, vid_decoder_frame(pl->decoder), pl->pixels, &flags);
    /* $213E's sprite flags, which the scanline renderer sets as the beam passes
     * them: a frame renderer only knows them once the frame is drawn. */
    snes->ppu.range_over = flags.range_over;
    snes->ppu.time_over = flags.time_over;
}

/* ---- saves -------------------------------------------------------------------- */

void player_sram_load(Player *pl)
{
    FILE *f = fopen(pl->sram_path, "rb");
    if (f) {
        uint8_t buf[SNES_SRAM_SIZE];
        size_t n = fread(buf, 1, sizeof buf, f);
        fclose(f);
        if (n == sizeof buf)
            memcpy(pl->snes.bus.sram, buf, sizeof buf);
        else
            fprintf(stderr, "ignoring %s: expected %d bytes\n", pl->sram_path, SNES_SRAM_SIZE);
    }
    memcpy(pl->sram_saved, pl->snes.bus.sram, SNES_SRAM_SIZE);
}

void player_sram_flush(Player *pl)
{
    if (!pl->persist_sram || !memcmp(pl->sram_saved, pl->snes.bus.sram, SNES_SRAM_SIZE))
        return;
    FILE *f = fopen(pl->sram_path, "wb");
    int ok = f && fwrite(pl->snes.bus.sram, SNES_SRAM_SIZE, 1, f) == 1;
    if (f && fclose(f) != 0)
        ok = 0;
    if (!ok) {
        fprintf(stderr, "cannot write %s\n", pl->sram_path);
        return;
    }
    memcpy(pl->sram_saved, pl->snes.bus.sram, SNES_SRAM_SIZE);
}

/* In shadow mode, print what the runtime found if any routine mismatched. */
static void natives_report(Player *pl)
{
    if (pl->natives && pl->runtime_mode == NATIVE_SHADOW && native_failed(pl->natives)) {
        fprintf(stderr, "shadow mode found mismatches:\n");
        native_report(pl->natives, stderr);
    }
}

static void natives_start(Player *pl, NativeMode mode)
{
    natives_report(pl);
    native_destroy(pl->natives);
    pl->snes.intercept = NULL;
    pl->snes.intercept_ctx = NULL;
    pl->natives = native_create(pl->table, pl->table_count, mode);
    native_set_index(pl->natives, recomp_index, recomp_index_count);   /* used only with generated entries */
    pl->runtime_mode = mode;
    native_attach(pl->natives, &pl->snes);
}

/* In shadow mode the machine runs only the original code, so a save just drops
 * comparisons in progress. */
bool player_busy(const Player *pl)
{
    return pl->runtime_mode == NATIVE_ON && native_suspended(pl->natives);
}

static uint32_t file_crc(const char *path, bool *ok)
{
    size_t len;
    uint8_t *data = read_file(path, &len);
    *ok = data != NULL;
    uint32_t crc = data ? ct_crc32(data, len) : 0;
    free(data);
    return crc;
}

int player_save_state(Player *pl, const char *path)
{
    if (snes_state_save(&pl->snes, path) != 0) {
        fprintf(stderr, "cannot save state %s\n", path);
        return -1;
    }
    if (pl->track.record_path && path == pl->state_path)
        pl->track.state_crc = file_crc(path, &pl->track.state_ours);
    fprintf(stderr, "saved state %s\n", path);
    return 0;
}

/* A state holds only the machine, so native routines suspended in call-outs when
 * it was saved can't resume; the player only saves when none are. Loading
 * restarts the native runtime, abandoning any routines suspended now. While
 * recording, only the quick save made during the recording loads, and the
 * recording goes back to its frame. */
int player_load_state(Player *pl, const char *path)
{
    Track *t = &pl->track;
    if (t->record_path) {
        bool ok;
        uint32_t crc = file_crc(path, &ok);
        if (!ok || !t->state_ours || crc != t->state_crc) {
            fprintf(stderr, "not loading %s: while recording, only a quick save made in this recording loads\n",
                    path);
            return -1;
        }
    }
    if (snes_state_load(&pl->snes, path) != 0) {
        fprintf(stderr, "cannot load state %s (missing, or saved by a different build)\n", path);
        return -1;
    }
    natives_start(pl, pl->runtime_mode);
    if (t->record_path && pl->snes.frame < t->frames) {
        t->frames = pl->snes.frame;
        if (t->replay_frames > t->frames)
            t->replay_frames = t->frames;
    }
    fprintf(stderr, "loaded state %s\n", path);
    return 0;
}

int player_import_zst(Player *pl, const char *path)
{
    char why[160];
    if (zst_import_file(&pl->snes, path, why, sizeof why) != 0) {
        fprintf(stderr, "cannot import %s: %s\n", path, why);
        return -1;
    }
    memcpy(pl->sram_saved, pl->snes.bus.sram, SNES_SRAM_SIZE);
    pl->snes.frame = 0;
    natives_start(pl, pl->runtime_mode);
    fprintf(stderr, "imported ZSNES state %s (some state approximated; not for validation)\n", path);
    return 0;
}

void player_request_save(Player *pl)
{
    pl->save_pending = true;
    pl->save_wait = 0;
}

void player_cancel_save(Player *pl) { pl->save_pending = false; }

/* ---- input track ---------------------------------------------------------------- */

bool player_replaying(const Player *pl)
{
    return pl->snes.frame < pl->track.replay_frames;
}

/* Buttons for the frame about to run: the track's while replaying, else live. */
static uint16_t next_pad(Player *pl, uint16_t live)
{
    const Track *t = &pl->track;
    uint32_t fr = pl->snes.frame;
    if (fr < t->replay_frames)
        return fr < t->frames ? t->pads[fr] : 0;
    return live;
}

static void track_set(Track *t, uint32_t frame, uint16_t pad)
{
    if (frame >= t->cap) {
        t->cap = frame + 1 > t->cap * 2 ? frame + 1 : t->cap * 2;
        t->pads = realloc(t->pads, t->cap * sizeof *t->pads);
    }
    for (uint32_t fr = t->frames; fr < frame; fr++)
        t->pads[fr] = 0;
    t->pads[frame] = pad;
    if (frame >= t->frames)
        t->frames = frame + 1;
}

void player_replay(Player *pl, const uint16_t *pads, uint32_t frames, const char *movie_name)
{
    Track *t = &pl->track;
    for (uint32_t fr = 0; fr < frames; fr++)
        track_set(t, fr, pads[fr]);
    t->replay_frames = frames;
    t->movie_name = movie_name;
}

int player_movie_flush(Player *pl)
{
    Track *t = &pl->track;
    if (!t->record_path || t->frames == t->written_frames)
        return 0;
    char header[512];
    int n = snprintf(header, sizeof header, "# Recorded with ct_play from power-on");
    if (t->sram_name)
        n += snprintf(header + n, sizeof header - n, " with the save %s", t->sram_name);
    if (t->movie_name)
        n += snprintf(header + n, sizeof header - n, ", continuing %s", t->movie_name);
    n += snprintf(header + n, sizeof header - n, ".\n# frames %lu\n", (unsigned long)t->frames);
    if (t->sram_name)
        n += snprintf(header + n, sizeof header - n, "# sram crc32 %08X\n", (unsigned)t->sram_crc);
    snprintf(header + n, sizeof header - n, "# Replay: ct_dev --rom <rom> --frames %lu --movie %s%s --native shadow\n",
             (unsigned long)t->frames, file_name(t->record_path), t->sram_name ? " --sram <save>" : "");
    if (ct_movie_write(t->record_path, t->pads, t->frames, header) != 0) {
        fprintf(stderr, "cannot write %s\n", t->record_path);
        return -1;
    }
    t->written_frames = t->frames;
    return 0;
}

/* ---- frames --------------------------------------------------------------------- */

int player_init(Player *pl, const uint8_t *rom, size_t rom_size, bool recomp, bool recomp_only)
{
    Track *t = &pl->track;
    snes_init(&pl->snes, rom, rom_size);
    if (recomp && recomp_native_count > 0 && ct_crc32(rom, rom_size) != recomp_rom_crc) {
        fprintf(stderr, "warning: generated blocks are for ROM CRC-32 %08X, not this one; not using them\n",
                (unsigned)recomp_rom_crc);
        recomp = false;
    }
    int nrecomp = recomp ? recomp_native_count : 0;
#if CT_PLAYER_NO_INTERPRETER
    if (!nrecomp) {
        fprintf(stderr, "this player runs %s, and it has no generated blocks for this ROM (%s): build it with the "
                        "ROM as CT_ROM\n",
                player_build_mode[CT_PLAYER_NO_INTERPRETER], recomp_native_count ? "they are for another ROM" : "none");
        return 1;
    }
#if CT_PLAYER_NO_INTERPRETER == 1
    pl->snes.interpreter = cpu_step;   /* for misses only */
#endif
    pl->snes.native_only = true;
    pl->snes.on_miss = on_miss;
    pl->snes.on_miss_ctx = pl;
    pl->misses = misses_create(&pl->snes);
    if (misses_load(pl->misses, pl->misses_path) < 0)
        fprintf(stderr, "warning: cannot read %s; starting a new one\n", pl->misses_path);
    fprintf(stderr, "ct_play: %s; instructions without generated code are logged to %s\n",
            player_build_mode[CT_PLAYER_NO_INTERPRETER], pl->misses_path);
#else
    pl->snes.interpreter = cpu_step;
#endif
    /* In on mode, generated blocks alone keep the original's timing exactly;
     * ported routines charge estimated time (docs/RECOMP.md, M2). Shadow and off
     * modes don't depend on timing, so they keep the ported routines. Without
     * generated blocks, the ported routines run. */
    int nported = recomp_only && nrecomp > 0 && pl->native_mode == NATIVE_ON ? 0 : game_native_count;
    pl->table = malloc(sizeof *pl->table * (size_t)(nported + nrecomp + 1));
    memcpy(pl->table, game_natives, sizeof *pl->table * (size_t)nported);
    if (nrecomp)
        memcpy(pl->table + nported, recomp_natives, sizeof *pl->table * (size_t)nrecomp);
    pl->table_count = nported + nrecomp;
    if (t->record_path && pl->native_mode == NATIVE_ON && nported) {
        fprintf(stderr, "--record with --native on needs the generated blocks, which this build or ROM lacks\n");
        return 2;
    }
    render_init(&pl->renderer);
    pl->pixels = pl->renderer.pixels;
    if (!pl->scanline) {
        pl->decoder = vid_decoder_create();
        pl->native_render = vid_render_create();
        pl->pixels = calloc((size_t)RENDER_WIDTH * RENDER_HEIGHT, sizeof *pl->pixels);
        if (!pl->decoder || !pl->native_render || !pl->pixels) {
            fprintf(stderr, "out of memory\n");
            return 1;
        }
    }
    pl->snes.on_line = on_line;
    pl->snes.on_line_ctx = pl;
    snes_power_on(&pl->snes);
    return 0;
}

void player_start(Player *pl, bool audio_direct)
{
    /* Replaying for a recording runs uninterrupted interpretation, which keeps the
     * same timing as shadow mode and is several times faster. */
    natives_start(pl, pl->track.replay_frames && pl->native_mode == NATIVE_SHADOW ? NATIVE_OFF : pl->native_mode);
    if (audio_direct)
        au_select_backend(m_platform(&pl->snes), AU_BACKEND_DIRECT);
}

void player_step(Player *pl, uint16_t live)
{
    Track *t = &pl->track;
    if (!player_replaying(pl) && pl->runtime_mode != pl->native_mode)
        natives_start(pl, pl->native_mode);
    uint32_t fr = pl->snes.frame;
    uint16_t pad = next_pad(pl, live);
    in_host_set(m_platform(&pl->snes), 0, pad);
    snes_run_frame(&pl->snes, pad);
    if (t->record_path) {
        track_set(t, fr, pad);
        if (t->frames - t->written_frames >= RECORD_FLUSH_FRAMES)
            player_movie_flush(pl);
    }
    if (pl->save_pending && !pl->snes.halted) {
        if (!player_busy(pl)) {
            pl->save_pending = false;
            player_save_state(pl, pl->state_path);
        } else if (++pl->save_wait > SAVE_WAIT_FRAMES) {
            pl->save_pending = false;
            fprintf(stderr, "quick save skipped: native routines stayed suspended; try again\n");
        }
    }
}

void player_idle_frame(Player *pl) { snes_run_frame(&pl->snes, 0); }

size_t player_take_audio(Player *pl, int16_t *out, size_t max_frames)
{
    return au_take_samples(m_platform(&pl->snes), out, max_frames);
}

void player_discard_audio(Player *pl)
{
    int16_t discard[256 * 2];
    while (player_take_audio(pl, discard, 256))
        ;
}

/* ---- reporting ------------------------------------------------------------------- */

void player_title(const Player *pl, bool paused, char *out, size_t size)
{
    int n = snprintf(out, size, "ct_play%s", title_mode[CT_PLAYER_NO_INTERPRETER]);
#if CT_PLAYER_NO_INTERPRETER
    if (pl->snes.halted)
        n += snprintf(out + n, size - n, " (stopped: %s)", pl->halt_reason);
    else if (pl->miss_steps)
        n += snprintf(out + n, size - n, " (%lu misses)", pl->miss_steps);
#endif
    if (player_replaying(pl))
        n += snprintf(out + n, size - n, " (replaying %u/%u)", (unsigned)pl->snes.frame,
                      (unsigned)pl->track.replay_frames);
    else if (pl->track.record_path)
        n += snprintf(out + n, size - n, " (recording, frame %u)", (unsigned)pl->snes.frame);
    if (paused)
        n += snprintf(out + n, size - n, " (paused)");
    if (pl->save_pending)
        n += snprintf(out + n, size - n, " (saving state...)");
    if (pl->runtime_mode == NATIVE_SHADOW && native_failed(pl->natives))
        snprintf(out + n, size - n, " [shadow mismatch]");
}

void player_finish(Player *pl)
{
    Track *t = &pl->track;
    if (t->record_path)
        fprintf(stderr, "recorded %lu frames to %s\n", (unsigned long)t->frames, t->record_path);
    natives_report(pl);
    native_destroy(pl->natives);
    pl->natives = NULL;
#if CT_PLAYER_NO_INTERPRETER
    player_misses_flush(pl);
    if (pl->miss_steps) {
        uint64_t total;
        size_t distinct = misses_count(pl->misses, &total);
        fprintf(stderr, "ct_play: %lu instructions this session had no generated code; %s now lists %llu at %lu "
                        "addresses (feed it back with CT_RECOMP_COVERAGE)\n",
                pl->miss_steps, pl->misses_path, (unsigned long long)total, (unsigned long)distinct);
    }
    misses_destroy(pl->misses);
#endif
    free(t->pads);
    t->pads = NULL;
}
