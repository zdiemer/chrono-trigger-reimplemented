/* The player (docs/PLAYING.md): the machine with its renderer and native
 * runtime, the input track that replays and records movies, and the saves,
 * advanced one frame per call.
 *
 * Below the driver. Nothing here owns a loop, sleeps, reads a clock, polls
 * events or opens a device: a driver (run_window and run_headless in
 * frontend/) calls player_step once a frame with the buttons it read, which
 * reach the game through in_host_set, and takes the frame's picture
 * (player_pixels) and sound (player_take_audio) to wherever it shows and plays
 * them. Keys, pads and window geometry are the driver's too. */
#ifndef CT_FRONTEND_PLAYER_H
#define CT_FRONTEND_PLAYER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "platform/native/render.h"
#include "platform/snes/decode.h"
#include "port/native.h"
#include "snes/render.h"
#include "snes/system.h"

#ifndef CT_PLAYER_NO_INTERPRETER
#define CT_PLAYER_NO_INTERPRETER 0
#endif
#if CT_PLAYER_NO_INTERPRETER
#include "misses.h"
#endif

/* What this build does with the interpreter, for --help, the title and stderr. */
extern const char *const player_build_mode[3];

/* Frames between writes of the movie being recorded, so a crash loses little. */
#define RECORD_FLUSH_FRAMES 600
/* How long a quick save waits for native routines suspended in call-outs to
 * finish before giving up (see native_suspended). In the field that's rarely
 * needed; in battle, stretches of several seconds are common. */
#define SAVE_WAIT_FRAMES 1800

/* Input by frame since power-on: the replayed movie's frames, then whatever was
 * played after them. */
typedef struct {
    uint16_t *pads;
    uint32_t frames, cap;
    uint32_t replay_frames;    /* frames taken from the movie before live input */
    const char *movie_name;    /* file name of the replayed movie, or NULL */
    const char *sram_name;     /* file name of the --sram save, or NULL */
    uint32_t sram_crc;         /* CRC-32 of the SRAM at power-on */
    const char *record_path;   /* movie being recorded, or NULL */
    uint32_t written_frames;   /* frames in the last write of record_path */
    uint32_t state_crc;        /* CRC of the quick save made during this recording */
    bool state_ours;
} Track;

typedef struct {
    Snes snes;
    /* The picture: the native renderer drawing the decoded VidFrame once a frame
     * (docs/PLATFORM.md, P3), or the scanline renderer line by line
     * (--renderer scanline). `pixels` is whichever one's framebuffer. */
    Renderer renderer;
    VidDecoder *decoder;
    VidRenderer *native_render;
    uint32_t *pixels;
    bool scanline;
    NativeRuntime *natives;
    NativeMode native_mode;    /* the mode asked for */
    NativeMode runtime_mode;   /* the mode natives runs in now */
    NativeEntry *table;        /* ported routines, then generated blocks */
    int table_count;
    bool persist_sram;
    char sram_path[1024];
    char state_path[1024];
    uint8_t sram_saved[SNES_SRAM_SIZE];   /* SRAM as last loaded or written */
    Track track;
    bool watch;                           /* replay the movie at normal speed, with sound */
    bool save_pending;                    /* a quick save waits for native routines */
    long save_wait;                       /* frames it has waited */
#if CT_PLAYER_NO_INTERPRETER
    Misses *misses;                       /* instructions without generated code, as in misses_path */
    char misses_path[1024];
    unsigned long miss_steps;             /* misses this session */
    unsigned long miss_steps_written;
    char halt_reason[256];                /* why the game stopped (snes.halted) */
#endif
} Player;

/* Build the machine for a ROM (without a copier header; it must outlive the
 * player) and power it on. native_mode, scanline and track.record_path are set
 * first. `recomp` uses the generated blocks if they are for this ROM,
 * `recomp_only` leaves the ported routines out in on mode. Returns 0, or the
 * exit status after printing why not. */
int player_init(Player *pl, const uint8_t *rom, size_t rom_size, bool recomp, bool recomp_only);

/* Replay these buttons from power-on before live input takes over. Before player_start. */
void player_replay(Player *pl, const uint16_t *pads, uint32_t frames, const char *movie_name);

/* Start the native runtime (after the SRAM is in place), and select the
 * APU-direct audio backend if asked. */
void player_start(Player *pl, bool audio_direct);

/* Run one frame. `live` is the buttons held, used once the replay is over. */
void player_step(Player *pl, uint16_t live);

/* Run one frame without input and without recording it, for letting native
 * routines finish before a state is saved. */
void player_idle_frame(Player *pl);

/* Whether the frame about to run comes from the replayed movie. */
bool player_replaying(const Player *pl);

/* Whether a state saved now would lose native routines suspended in call-outs. */
bool player_busy(const Player *pl);

/* The last frame, RENDER_WIDTH x RENDER_HEIGHT XRGB8888. */
static inline const uint32_t *player_pixels(const Player *pl) { return pl->pixels; }

/* Move up to max_frames of the sound produced so far (32000 Hz stereo) into
 * out; returns how many. player_discard_audio drops it. */
size_t player_take_audio(Player *pl, int16_t *out, size_t max_frames);
void player_discard_audio(Player *pl);

/* Quick saves. A request is carried out after the first frame that ends with
 * no native routine suspended, or dropped after SAVE_WAIT_FRAMES. */
void player_request_save(Player *pl);
void player_cancel_save(Player *pl);
static inline bool player_saving(const Player *pl) { return pl->save_pending; }

/* Save and load state files. */
int player_save_state(Player *pl, const char *path);
int player_load_state(Player *pl, const char *path);

/* Start from a ZSNES save state instead of power-on. */
int player_import_zst(Player *pl, const char *path);

/* Cartridge saves: read sram_path, and write it if the game changed it. */
void player_sram_load(Player *pl);
void player_sram_flush(Player *pl);

/* Write the movie being recorded, if it grew. Returns 0, or -1 on failure. */
int player_movie_flush(Player *pl);

/* Write the misses file, if this session found new misses. */
void player_misses_flush(Player *pl);

/* The window title's text, `paused` being the driver's. */
void player_title(const Player *pl, bool paused, char *out, size_t n);

/* Why the game stopped (snes.halted), and where misses are logged. */
const char *player_halt_reason(const Player *pl);
const char *player_misses_path(const Player *pl);

/* End of a session: report what shadow mode and misses found, and free. */
void player_finish(Player *pl);

#endif
