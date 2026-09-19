#include "player.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "movie.h"
#include "natives.h"
#include "platform/audio.h"
#include "platform/input.h"
#include "platform/save.h"
#include "port/machine.h"
#include "port/recomp_table.h"
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
                (unsigned)snes->frame, what, pl->misses_name);
        return false;
    }
    if (misses_count(pl->misses, NULL) > before && before < MISSES_REPORTED) {
        misses_describe(snes, what, sizeof what);
        fprintf(stderr, "ct_play: frame %u: no generated code for %s; running it on the interpreter (logged to %s)\n",
                (unsigned)snes->frame, what, pl->misses_name);
    }
    return true;
}
#endif

int player_misses_merge(Player *pl, const char *text, size_t len)
{
#if CT_PLAYER_NO_INTERPRETER
    return misses_parse(pl->misses, text, len);
#else
    (void)pl, (void)text, (void)len;
    return 0;
#endif
}

char *player_misses_text(Player *pl, size_t *len)
{
#if CT_PLAYER_NO_INTERPRETER
    if (pl->miss_steps == pl->miss_steps_written)
        return NULL;
    return misses_format(pl->misses, len, NULL);
#else
    (void)pl, (void)len;
    return NULL;
#endif
}

void player_misses_stored(Player *pl)
{
#if CT_PLAYER_NO_INTERPRETER
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

const char *player_misses_name(const Player *pl)
{
#if CT_PLAYER_NO_INTERPRETER
    return pl->misses_name;
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

static uint8_t *sram_bytes(Player *pl) { return (uint8_t *)sv_host_bytes(m_platform(&pl->snes), NULL); }

void player_use_sram(Player *pl, const uint8_t sram[SNES_SRAM_SIZE], const char *name)
{
    memcpy(sram_bytes(pl), sram, SNES_SRAM_SIZE);
    pl->track.sram_name = name;
}

uint32_t player_sram_crc(Player *pl)
{
    pl->track.sram_crc = ct_crc32(sram_bytes(pl), SNES_SRAM_SIZE);
    return pl->track.sram_crc;
}

void player_load_sram(Player *pl, const uint8_t *sram)
{
    if (sram)
        memcpy(sram_bytes(pl), sram, SNES_SRAM_SIZE);
    memcpy(pl->sram_saved, sram_bytes(pl), SNES_SRAM_SIZE);
}

const uint8_t *player_sram_changed(Player *pl)
{
    bool dirty;
    const uint8_t *sram = sv_host_bytes(m_platform(&pl->snes), &dirty);
    if (!pl->persist_sram || !dirty || !memcmp(pl->sram_saved, sram, SNES_SRAM_SIZE))
        return NULL;
    return sram;
}

void player_sram_stored(Player *pl) { memcpy(pl->sram_saved, sram_bytes(pl), SNES_SRAM_SIZE); }

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
    pl->natives = natives_restart(pl->natives, &pl->table, mode, &pl->snes);
    pl->runtime_mode = mode;
}

/* In on mode a state can't hold routines suspended in call-outs, nor the time
 * ports have taken that the machine hasn't let pass yet (native_owed): saved
 * then, the routines would be left to the original code and the time dropped.
 * The first is never acceptable; the second is, once waiting for a frame end
 * without it has taken SAVE_EXACT_FRAMES (in a long battle there may be none).
 * In shadow mode the machine runs only the original code, so a save just drops
 * comparisons in progress. */
PlayerSaveReady player_save_ready(const Player *pl, long waited)
{
    if (pl->runtime_mode != NATIVE_ON || (!native_suspended(pl->natives) && !native_owed(pl->natives)))
        return PLAYER_SAVE_EXACT;
    if (native_suspended(pl->natives) || waited < SAVE_EXACT_FRAMES)
        return PLAYER_SAVE_WAIT;
    return PLAYER_SAVE_DROPS_TIME;
}

uint64_t player_owed(const Player *pl)
{
    return pl->runtime_mode == NATIVE_ON ? native_owed(pl->natives) : 0;
}

uint8_t *player_save_state(Player *pl, size_t *len)
{
    return snes_state_write(&pl->snes, len);
}

/* A state holds only the machine, so native routines suspended in call-outs when
 * it was saved can't resume, and time owed is lost; the player saves only where
 * player_save_ready allows. */
int player_load_state(Player *pl, const uint8_t *data, size_t len, char *why, size_t why_len)
{
    Track *t = &pl->track;
    if (t->record_name && (!t->state_ours || ct_crc32(data, len) != t->state_crc))
        return PLAYER_NOT_OURS;
    if (snes_state_read(&pl->snes, data, len, why, why_len) != 0)
        return -1;
    natives_start(pl, pl->runtime_mode);
    if (t->record_name && pl->snes.frame < t->frames) {
        t->frames = pl->snes.frame;
        if (t->replay_frames > t->frames)
            t->replay_frames = t->frames;
    }
    return 0;
}

int player_import_zst(Player *pl, const uint8_t *data, size_t len, char *why, size_t why_len)
{
    if (zst_import(&pl->snes, data, len, why, why_len) != 0)
        return -1;
    memcpy(pl->sram_saved, sram_bytes(pl), SNES_SRAM_SIZE);
    pl->snes.frame = 0;
    natives_start(pl, pl->runtime_mode);
    return 0;
}

void player_request_save(Player *pl)
{
    pl->save_pending = true;
    pl->save_wait = 0;
}

void player_cancel_save(Player *pl) { pl->save_pending = false; }

uint8_t *player_take_quick_save(Player *pl, size_t *len)
{
    uint8_t *data = pl->quick_save;
    *len = pl->quick_save_len;
    pl->quick_save = NULL;
    return data;
}

/* Take the quick save asked for. While recording, remember which state it is:
 * only that one loads. */
static void quick_save(Player *pl)
{
    size_t len;
    uint8_t *data = player_save_state(pl, &len);
    if (!data) {
        fprintf(stderr, "cannot save state: out of memory\n");
        return;
    }
    free(pl->quick_save);
    pl->quick_save = data;
    pl->quick_save_len = len;
    if (pl->track.record_name) {
        pl->track.state_crc = ct_crc32(data, len);
        pl->track.state_ours = true;
    }
}

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

uint32_t player_movie_unwritten(const Player *pl)
{
    const Track *t = &pl->track;
    return t->record_name ? t->frames - t->written_frames : 0;
}

char *player_movie_text(Player *pl, size_t *len)
{
    Track *t = &pl->track;
    if (!t->record_name || t->frames == t->written_frames)
        return NULL;
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
             (unsigned long)t->frames, t->record_name, t->sram_name ? " --sram <save>" : "");
    t->pending_frames = t->frames;
    return ct_movie_format(t->pads, t->frames, header, len);
}

void player_movie_stored(Player *pl) { pl->track.written_frames = pl->track.pending_frames; }

/* ---- frames --------------------------------------------------------------------- */

int player_init(Player *pl, const uint8_t *rom, size_t rom_size, bool recomp, bool recomp_only)
{
    Track *t = &pl->track;
    snes_init(&pl->snes, rom, rom_size);
    if (natives_select(&pl->table, rom, rom_size, pl->native_mode, recomp, recomp_only) != 0) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
#if CT_PLAYER_NO_INTERPRETER
    if (!pl->table.generated) {
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
#else
    pl->snes.interpreter = cpu_step;
#endif
    if (t->record_name && pl->native_mode == NATIVE_ON && pl->table.ported) {
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
    if (t->record_name)
        track_set(t, fr, pad);
    if (pl->save_pending && !pl->snes.halted) {
        PlayerSaveReady ready = player_save_ready(pl, pl->save_wait);
        if (ready != PLAYER_SAVE_WAIT) {
            pl->save_pending = false;
            if (ready == PLAYER_SAVE_DROPS_TIME)
                fprintf(stderr, "quick save: no frame ended with nothing owed in %d frames; the state leaves out "
                                "%llu master clocks the native routines owed\n",
                        SAVE_EXACT_FRAMES, (unsigned long long)player_owed(pl));
            quick_save(pl);
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
    else if (pl->track.record_name)
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
    natives_report(pl);
    native_destroy(pl->natives);
    pl->natives = NULL;
    natives_free(&pl->table);
#if CT_PLAYER_NO_INTERPRETER
    if (pl->miss_steps) {
        uint64_t total;
        size_t distinct = misses_count(pl->misses, &total);
        fprintf(stderr, "ct_play: %lu instructions this session had no generated code; %s now lists %llu at %lu "
                        "addresses (feed it back with CT_RECOMP_COVERAGE)\n",
                pl->miss_steps, pl->misses_name, (unsigned long long)total, (unsigned long)distinct);
    }
    misses_destroy(pl->misses);
#endif
    free(t->pads);
    t->pads = NULL;
    free(pl->quick_save);
    pl->quick_save = NULL;
}
