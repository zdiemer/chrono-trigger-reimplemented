/* The player (docs/PLAYING.md): the machine with its renderer and native
 * runtime, the input track that replays and records movies, and the saves,
 * advanced one frame per call.
 *
 * Below the driver. Nothing here owns a loop, sleeps, reads a clock, polls
 * events or opens a device: a driver (run_window and run_headless in
 * frontend/) calls player_step once a frame with the buttons it read, which
 * reach the game through in_host_set, and takes the frame's picture
 * (player_pixels) and sound (player_take_audio) to wherever it shows and plays
 * them. Keys, pads and window geometry are the driver's too.
 *
 * Nor does it know a path. What it persists -- the cartridge save, quick
 * saves, the movie being recorded, the misses log -- it takes and hands out as
 * bytes, and the driver decides where they are kept and what they are called
 * (frontend/files.h on the desktop). The names it is given are for messages. */
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
    const char *record_name;   /* file name of the movie being recorded, or NULL */
    uint32_t written_frames;   /* frames in the movie last handed out */
    uint32_t pending_frames;   /* frames in the movie handed out now */
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
    bool persist_sram;                    /* hand out the SRAM when the game changes it */
    uint8_t sram_saved[SNES_SRAM_SIZE];   /* SRAM as last loaded or stored */
    Track track;
    bool watch;                           /* replay the movie at normal speed, with sound */
    bool save_pending;                    /* a quick save waits for native routines */
    long save_wait;                       /* frames it has waited */
    uint8_t *quick_save;                  /* a quick save taken, until the driver takes it */
    size_t quick_save_len;
#if CT_PLAYER_NO_INTERPRETER
    Misses *misses;                       /* instructions without generated code */
    const char *misses_name;              /* where the driver keeps them, for messages */
    unsigned long miss_steps;             /* misses this session */
    unsigned long miss_steps_written;
    char halt_reason[256];                /* why the game stopped (snes.halted) */
#endif
} Player;

/* Build the machine for a ROM (without a copier header; it must outlive the
 * player) and power it on. native_mode, scanline and track.record_name are set
 * first (and misses_name, without the interpreter). `recomp` uses the
 * generated blocks if they are for this ROM, `recomp_only` leaves the ported
 * routines out in on mode. Returns 0, or the exit status after printing why
 * not. */
int player_init(Player *pl, const uint8_t *rom, size_t rom_size, bool recomp, bool recomp_only);

/* Replay these buttons from power-on before live input takes over. Before player_start. */
void player_replay(Player *pl, const uint16_t *pads, uint32_t frames, const char *movie_name);

/* The cartridge save at power-on. player_use_sram starts from a fixed save
 * (--sram, a movie's save), named in a recording's header; player_sram_crc is
 * the CRC-32 of the SRAM the game starts with. player_load_sram restores the
 * persisted save (NULL: none), which persist_sram then keeps up to date. */
void player_use_sram(Player *pl, const uint8_t sram[SNES_SRAM_SIZE], const char *name);
uint32_t player_sram_crc(Player *pl);
void player_load_sram(Player *pl, const uint8_t *sram);

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

/* ---- what the driver persists ----------------------------------------------------- */

/* Quick saves. A request is carried out after the first frame that ends with
 * no native routine suspended, or dropped after SAVE_WAIT_FRAMES; the state it
 * took is then player_take_quick_save's (malloc'd; the caller frees it). */
void player_request_save(Player *pl);
void player_cancel_save(Player *pl);
static inline bool player_saving(const Player *pl) { return pl->save_pending; }
uint8_t *player_take_quick_save(Player *pl, size_t *len);

/* The machine as a state, malloc'd (NULL if out of memory), and loading one.
 * A load restarts the native runtime. While recording, only the quick save
 * taken during the recording loads (PLAYER_NOT_OURS otherwise), and the
 * recording goes back to its frame. Returns 0, PLAYER_NOT_OURS, or -1 for a
 * state this build can't read. */
enum { PLAYER_NOT_OURS = -2 };
uint8_t *player_save_state(Player *pl, size_t *len);
int player_load_state(Player *pl, const uint8_t *data, size_t len);

/* Start from a ZSNES save state instead of power-on. Returns 0, or -1 with why. */
int player_import_zst(Player *pl, const uint8_t *data, size_t len, char *why, size_t why_len);

/* The SRAM if persist_sram and the game changed it since it was loaded or
 * stored, else NULL; player_sram_stored says it was kept. */
const uint8_t *player_sram_changed(Player *pl);
void player_sram_stored(Player *pl);

/* The movie being recorded, as .ctm text (malloc'd), if it grew since it was
 * last stored; player_movie_stored says it was kept. player_movie_unwritten
 * is how many frames that would add. */
char *player_movie_text(Player *pl, size_t *len);
void player_movie_stored(Player *pl);
uint32_t player_movie_unwritten(const Player *pl);

/* Without the interpreter: the misses log. player_misses_merge adds the counts
 * of a log kept earlier (returns entries read); player_misses_text is the log
 * (malloc'd) if this session found misses since it was last stored. Nothing
 * with the interpreter. */
int player_misses_merge(Player *pl, const char *text, size_t len);
char *player_misses_text(Player *pl, size_t *len);
void player_misses_stored(Player *pl);

/* ---- reporting ---------------------------------------------------------------------- */

/* The window title's text, `paused` being the driver's. */
void player_title(const Player *pl, bool paused, char *out, size_t n);

/* Why the game stopped (snes.halted), and where misses are logged. */
const char *player_halt_reason(const Player *pl);
const char *player_misses_name(const Player *pl);

/* End of a session: report what shadow mode and misses found, and free. The
 * driver stores the misses log first. */
void player_finish(Player *pl);

#endif
