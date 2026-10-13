/* Scenes: the set pieces the game runs as programs of their own.
 *
 * The title screen with its pendulum and its logo, the attract sequence, the
 * jet bike race and the endings each run as a scene program: the field or the
 * game over starts one (scene/boot.c), which decompresses the program into
 * work RAM and runs it there. A program is a main loop, the frame's work its
 * NMI does (scene/frame.c), and the sequence scripts two channels run each
 * frame (scene/script.c), which move things, call the program's routines and
 * start and stop its tasks: routines the frame's work calls every frame, from
 * one of two lists.
 *
 * Where to look:
 *   boot.c        starting a scene program, and the copies of its resources
 *   frame.c       the frame: the NMI, pause, the song, the task lists
 *   script.c      the sequence scripts
 *   screen.c      the program's screen: its setup, and what it sends each frame
 *   title.c       the title screen: the pendulum, the logo, the ripple
 *   race.c        the jet bike race's setup and display, and its sounds
 *   race_rules.c, race_camera.c, race_projection.c, race_drawing.c
 *                 the race while it runs: the bikes, the camera, the bikes
 *                 placed on the screen, the racers and the panel drawn
 *                 (race.h, what they share)
 *   race_matrix.c the main loop's scaling of the racers' pictures, the
 *                 routine the race writes to regroup them into tiles, and
 *                 that routine
 *   perspective.c the Mode 7 plane in perspective, the race's and the endings'
 *   staff_roll.c  the telepod ending's last scene: the staff credits over the
 *                 stars, and The End
 *   stars.c       the star field the endings fly through
 *   captions.c    the captions over the shared endings
 *   random.c      the program's random numbers
 *   endings.c     the endings modes 10, 11, 14 and 15 share: their setup, the
 *                 zooming picture, and the scenes their scripts start
 *   bridge.c      where ports meet the program's calling conventions
 *   sound.c       the program's messages to the sound driver
 *   sprites.c, circle.c, scaler.c, math.c
 *                 what the scenes share: sprites, the iris, the bitmap
 *                 scaler, the arithmetic
 *   guards.c      what the ports assume of the machine they're entered on
 *
 * The endings are the program's scripts, using the iris, the scaler and the
 * arithmetic, with the perspective, the shared endings' setup and scenes and
 * the staff roll's setup and tasks.
 *
 * game/attract/ holds more of the same program: its main loop ($7E:3023, which
 * waits for scene_program_over) and the ports of its idle waits, written
 * before this subsystem was. They belong here and move when attract is next
 * reworked (docs/TODO.md). */
#ifndef CT_GAME_SCENE_H
#define CT_GAME_SCENE_H

#include <stdbool.h>

#include "platform/timing.h"
#include "platform/video.h"
#include "snes/system.h"
#include "state.h"
#include "vars.h"

/* scene_flags, which whoever starts a scene sets (the boot takes them in A). */
enum {
    /* The scene is the title as the game comes back to it (after a reset or a
     * game over, never from the field), which runs with the line interrupt:
     * the boot starts it, the frame's work leaves interrupts enabled so it can
     * fire, and the program's clear of its work RAM keeps what lies below $0920. */
    SCENE_FLAG_TITLE_WITH_LINE_IRQ = 0x80,
    /* The race is drawn without its panel, and closes behind a curtain: the
     * attract sequence's. */
    SCENE_FLAG_NO_PANEL = 0x40,
    /* The frame's work doesn't send the sound driver song changes. */
    SCENE_FLAG_NO_SONGS = 0x20,
};

/* ---- The scene program's memory and transfers ------------------------------------------------ */

/* The two banks of work RAM. The program runs in the first, and puts large
 * buffers (the scaler's bitmaps, some irises' tables) in the second. */
enum { SCENE_LOWER_WORK_RAM = 0x7E, SCENE_UPPER_WORK_RAM = 0x7F };

/* The tilemap the scene builds for vblank (scene_tilemap_buffer), where the
 * uploads read it: low RAM, through bank 0. */
enum { SCENE_TILEMAP_BUFFER = 0x000C00 };

/* The frame's sprite table, in low RAM through bank 0 as the uploads read it:
 * the 128 entries (scene_sprite_table) and the high table after them
 * (scene_sprite_high_table), whose byte scene_sprite_high_pointer holds the
 * address of. */
enum { SCENE_SPRITE_TABLE = 0x000700, SCENE_SPRITE_HIGH_TABLE = 0x000900 };

/* A word of the program the loaders fill a map's unused space with: the tile a
 * layer shows where nothing is drawn. */
enum { SCENE_BLANK_TILE = 0x7E350B };

/* How the loaders' transfer (scene_transfer) moves its bytes, as the program
 * keeps it for the SNES-only escape: into a register pair, a word at a time. */
enum { SCENE_TRANSFER_WORDS = 0x01 };

/* How VRAM writes step: a word at a time, stepping after the high byte (whole
 * words), or after the low byte (the low bytes alone, as the Mode 7 plane's
 * name table is written). */
#define SCENE_VRAM_WHOLE_WORDS VID_VRAM_WORD_PORT
#define SCENE_VRAM_LOW_BYTES (&(const VidVramPort){ .step = 1, .after_high = false })

/* A task: a routine of the program (its address), which the frame's work
 * calls every frame while it is on one of the two lists. 0 is an empty slot. */
typedef uint16_t SceneTask;
enum { NO_TASK = 0 };

typedef enum {
    SCENE_VBLANK_TASKS,   /* run first, while the frame's work has the screen blanked: the uploads */
    SCENE_FRAME_TASKS,    /* run after the controller is read, from a copy the frame's work makes */
} SceneTaskList;

/* scene/frame.c: put a task on a list (in its first empty slot), or take one
 * off; false if there is no such slot, where the original searches on past the
 * list through memory (no script does that). */
bool scene_add_task(GameState *g, SceneTaskList list, SceneTask task);
bool scene_remove_task(GameState *g, SceneTaskList list, SceneTask task);
/* Whether a list holds the task (NO_TASK: whether it has an empty slot). */
bool scene_task_listed(const GameState *g, SceneTaskList list, SceneTask task);

/* The tasks that are C, by the values the lists hold for them (the program's
 * routines, by their addresses: the lists' own encoding, which the scripts
 * write). */
enum {
    SCENE_TASK_ANIMATE_PALETTES = 0x37FA,     /* scene/screen.c */
    SCENE_TASK_FINISH_SPRITES = 0x380B,
    SCENE_TASK_BRIGHTEN = 0x347A,             /* scene/screen.c: the screen's fades */
    SCENE_TASK_DARKEN = 0x3495,
    SCENE_TASK_MUSIC_FADES = 0x34AB,
    SCENE_TASK_DARKEN_IN_SILENCE_AND_END = 0x34C2,
    SCENE_TASK_DARKEN_AND_END = 0x34E3,
    SCENE_TASK_WATCH_FOR_A_BUTTON = 0x5B99,   /* scene/title.c */
    SCENE_TASK_CALM_THE_RIPPLE = 0x5BBF,
    SCENE_TASK_SWING_PENDULUM = 0x5BCF,
    SCENE_TASK_PLACE_PENDULUM = 0x5C57,
    SCENE_TASK_MOVE_LOGO = 0x5C98,
    SCENE_TASK_DRAW_THE_CLOCK = 0x5D01,
    SCENE_TASK_DRAW_THE_HANDS = 0x5D20,
    SCENE_TASK_COUNT_THE_WAIT_DOWN = 0x5D32,
    SCENE_TASK_SLIDE_THE_NAME = 0x5D71,
    SCENE_TASK_DRAW_THE_COPYRIGHT = 0x5E1E,
    SCENE_TASK_RIPPLE = 0x5FFE,
    SCENE_TASK_UPLOAD_RIPPLE = 0x6212,
};

/* How a task's frame ends: it stays on its list, it is done (its slot
 * empties), or it hands its slot to another task; or, for a task the
 * original runs, it left the list some other way, and the original goes on
 * from there. */
typedef enum { TASK_GOES_ON, TASK_IS_DONE, TASK_HANDS_OVER, TASK_LEFT_TO_THE_ORIGINAL } TaskOutcome;
typedef struct {
    TaskOutcome outcome;
    SceneTask next;   /* TASK_HANDS_OVER: the task that takes the slot */
} TaskEnd;
static inline TaskEnd task_goes_on(void) { return (TaskEnd){ TASK_GOES_ON, NO_TASK }; }
static inline TaskEnd task_is_done(void) { return (TaskEnd){ TASK_IS_DONE, NO_TASK }; }

/* What a task leaves in the registers for the code after it, which the lists
 * hand on (scaffolding: the frame's work reads A's high byte after the last
 * frame task, and an original task Y, the next free sprite entry). */
typedef struct {
    bool a_set, y_set;
    uint16_t a, y;
    bool a_8bit;           /* its last byte store left A 8-bit (the list puts P back) */
    bool a_high_as_called; /* A's high byte is what the task was called with (the list's task) */
    bool overflow_clear;   /* a callee of its cleared the overflow (the list puts P back) */
} TaskLeftovers;

/* A task that is C: its frame, on the list it runs on; the guard that says
 * the program's code for it is the one the port stands for (it takes the
 * machine, for the code's hash and the page the lists run on); and what it
 * leaves. */
typedef struct {
    SceneTask task;
    SceneTaskList list;
    TaskEnd (*run)(GameState *g, Platform *pf);
    bool (*present)(Snes *s);
    TaskLeftovers (*leaves)(const GameState *g, TaskEnd end);
    /* It calls the original's code (the sound driver): with 8-bit A, and with
     * the stack as the original's call has it, the frames the list keeps under
     * the task's return, which shadow mode pairs the call with the original's
     * by. */
    bool calls_out;
} SceneTaskPort;

/* The tasks each file has as C (scene/title.c, scene/screen.c), which the
 * lists' runner looks a task up in. */
extern const SceneTaskPort scene_title_tasks[];
extern const unsigned scene_title_task_count;
extern const SceneTaskPort scene_screen_tasks[];
extern const unsigned scene_screen_task_count;

/* The program's routines the scripts call (their command $A0), which are C,
 * by the values the scripts hold for them (the routines' addresses). */
enum {
    SCENE_ROUTINE_START_RIPPLE = 0x5DA7,          /* scene/title.c */
    SCENE_ROUTINE_DARKEN_SPRITE_PALETTE_4 = 0x5DB4,
    SCENE_ROUTINE_UNTINT_SPRITE_PALETTE_3 = 0x5DE9,
};

/* A routine a script calls, as C: its work on the calling channel; the guard
 * that says the program's code for it is the code the port stands for; and
 * what it leaves in A for the script's interpreter, which A held the
 * routine's address when it called. */
typedef struct {
    uint16_t routine;
    void (*run)(GameState *g, Platform *pf, uint8_t channel);
    bool (*present)(Snes *s);
    uint16_t (*leaves_a)(uint16_t called_with);
    bool leaves_the_program_bank;   /* it comes back with the data bank on the program's */
} SceneScriptRoutine;

extern const SceneScriptRoutine scene_title_routines[];
extern const unsigned scene_title_routine_count;

/* scene/frame.c: a task's registered entry, for a caller that reaches it by
 * its address (fuzzing; the lists run it as C): the row's frame, on the row's
 * list, and back as the original's would come back, or through the lists'
 * exits. */
void scene_task_alone(Snes *s, const SceneTaskPort *port);

/* scene/sprites.c: a sprite put straight into an entry of the frame's sprite
 * table (a byte offset), with no cursor and no offset, as the star field
 * places its own. */
typedef struct {
    uint8_t x, y, tile, attributes;
} SceneSprite;
void scene_place_sprite(GameState *g, uint16_t entry, SceneSprite sprite);
/* The same at the scene's cursor, which moves on, small or large; and whether
 * `sprites` more fit from the cursor. */
void scene_add_sprite_at_cursor(GameState *g, SceneSprite sprite, bool large);
bool scene_sprites_fit(const GameState *g, unsigned sprites);
/* How many sprites a list in the program holds; and where the lists are
 * drawn from next (X's 9th bit kept). */
uint8_t scene_sprite_list_length(const GameState *g, uint16_t list);
void scene_move_sprite_offset(GameState *g, uint8_t x, uint8_t y);
/* Where the lists are drawn from, both words as the program stores them. */
void scene_draw_lists_at(GameState *g, uint16_t x, uint16_t y);
/* A list drawn as the program's list routines draw it (scene_load_sprite_list,
 * its clipped form): for the program's routines ported as C. */
void scene_draw_sprite_list(GameState *g, uint16_t list, bool clipped);
uint16_t scene_clipped_list_leaves_a(const GameState *g, uint16_t list, uint16_t a);
/* The rest of the cursor's group of four sprites put off the screen, as the
 * frame's last sprites leave it. */
void scene_hide_rest_of_group(GameState *g);

/* scene/random.c: the program's random numbers -- the next of the game's
 * table of random bytes, and a stirred draw, a byte or a word. A draw is its
 * number with one bit more above it, the bit its sum spilled; the next draw
 * starts from it (scene_draw_from makes a first one). Whether the last draw's
 * final step lost its sign is the original's to leave behind (scaffolding). */
typedef uint32_t SceneDraw;
static inline SceneDraw scene_draw_from(uint16_t number, bool spilled, TmEntropyWidth width)
{
    unsigned bits = width == TM_ENTROPY_WORDS ? 16 : 8;
    return (SceneDraw)(number & ((1u << bits) - 1)) | (spilled ? 1u << bits : 0);
}
uint8_t scene_next_random_byte(GameState *g);
SceneDraw scene_draw_random(GameState *g, Platform *pf, PfOrigin o, SceneDraw last, uint16_t salt, TmEntropyWidth width);
bool scene_random_last_overflow(void);

/* scene/stars.c: every star of the field scattered at random, from a first
 * draw. */
void scene_scatter_stars(GameState *g, Platform *pf, SceneDraw first);

/* A sequence channel: one of the two the program runs a script on
 * (scene_channels), whose page its routines run on when a script calls them. */
typedef unsigned SceneChannel;

/* scene/script.c: a channel's registers, 0-15. */
uint16_t scene_channel_register(const GameState *g, SceneChannel ch, unsigned n);
void set_scene_channel_register(GameState *g, SceneChannel ch, unsigned n, uint16_t v);

/* scene/sound.c: how far the music has got. The sound driver shows it on a
 * port, which two reads that agree say is steady (not being changed as it is
 * read); its progress is a level from 0 to 15 that the song sets as it plays. */
typedef struct {
    uint8_t first, second;   /* the two reads */
    bool steady;
} MusicStatus;
MusicStatus scene_music_status(Platform *pf);
uint8_t scene_music_progress(MusicStatus music);
bool scene_music_reached(MusicStatus music, uint8_t level);

/* scene/circle.c: the iris, as a C call for the menus' sprite objects and the
 * field's screen effects. The mode says how the lower half is made from the
 * upper one -- mirrored, or from every other line of it for a circle squashed
 * to half its height -- and, with SCENE_CIRCLE_KEEPS_OUTSIDE, that the lines
 * outside the circle are left as they were rather than closed. */
enum { SCENE_CIRCLE_ROUND = 0, SCENE_CIRCLE_SQUASHED = 1, SCENE_CIRCLE_KEEPS_OUTSIDE = 0x80 };
void scene_circle_draw(GameState *g, uint8_t mode);

/* scene/math.c: the game's sine, as the scene programs take it: 1024 steps a
 * turn, the magnitude in 256ths from the table at $C0:F900, negative for the
 * first half turn; the cosine a quarter turn on. The lookup also leaves
 * whether the step is in the second half of the turn (scene_sine_second_half),
 * the carry its compare leaves, which some callers carry on into a divide. */
int16_t scene_sine(const GameState *g, uint16_t angle);
int16_t scene_cosine(const GameState *g, uint16_t angle);
bool scene_sine_second_half(uint16_t angle);

/* The CPU's divider, as the scene programs divide by it: a 16-bit dividend
 * over a byte, $FFFF for a divisor of 0. */
static inline uint16_t scene_divider_quotient(uint16_t dividend, uint8_t divisor)
{
    return divisor ? (uint16_t)(dividend / divisor) : 0xFFFF;
}

/* scene/math.c: the program's arithmetic as C calls, on a page's cells, each
 * timed as the routine it stands for: a 16 x 16-bit product; a signed 16-bit
 * value times a signed byte (the low word of the product); the 8.8 fraction of
 * a value's high byte over a divisor; and the first divisor from `from` whose
 * quotient of `dividend` falls under `limit`. */
uint32_t scene_multiply16(GameState *g, SceneProgramPage page, uint16_t a, uint16_t b);
/* Whether the program's multiply comes out as the product, with or without the
 * byte after the product's top bit carried in (the multiply's guard). */
bool scene_multiplies_exactly(uint16_t a, uint16_t b, bool carried);
/* The 32-bit divide's quotient and remainder of `dividend` over `divisor` (not
 * 0, under $80000000: the divide's guard), the caller's carry brought in as
 * the original brings it. Returns the quotient. */
uint32_t scene_divide32(GameState *g, SceneProgramPage page, uint32_t dividend, uint32_t divisor, bool carry);
int16_t scene_multiply_by_signed_byte(GameState *g, SceneProgramPage page, uint16_t a, uint16_t b);
uint16_t scene_divide_8_8_by(GameState *g, SceneProgramPage page, uint16_t dividend, uint16_t divisor);
uint8_t scene_find_divisor_from(GameState *g, uint16_t dividend, uint8_t from, uint16_t limit);

/* scene/screen.c: the program's transfer of what the unpacker made, as the
 * caller has set its mode and register (scene_call); and the per-line
 * effects from a table of descriptors (returns where it ends). */
void scene_send_unpacked(const GameState *g, Platform *pf);
uint16_t scene_set_up_line_effects(const GameState *g, Platform *pf, uint16_t at);
/* scene/screen.c: the working palette sent; the bank the unpacker's
 * destination names; `bytes` of a resource copied as they are to `to` in that
 * bank, the count handed over in `page`'s first cell; and the loaded palette
 * made the one the frame sends. */
void scene_send_palette(Platform *pf);
/* scene/screen.c: the program's work RAM cleared (true: the title's shorter
 * clear, which keeps what lies below the task lists), every sprite hidden, and
 * the screen every scene starts from. */
bool scene_clear_the_work_ram(GameState *g, Platform *pf, uint8_t matrix_left);
void scene_hide_every_sprite(Platform *pf);
void scene_set_up_first_screen(GameState *g, Platform *pf);
uint8_t scene_unpacked_bank(const GameState *g);
void scene_copy_resource_to(GameState *g, SceneProgramPage page, unsigned resource, uint16_t to, uint16_t bytes);
void scene_use_loaded_palette(GameState *g);

/* scene/race.c: the Mode 7 plane's name table the race and the endings share
 * -- half of it, from what was unpacked, each row of the track doubled across
 * the plane's 128 tiles; that half sent into VRAM's low bytes -- and rows of a
 * picture mirrored, from each source row's pixel `last` back to its first, the
 * rest of the row empty (returns where the next row goes). */
void scene_double_plane_rows(GameState *g);
void scene_upload_plane_half(Platform *pf);
uint32_t scene_mirror_rows(GameState *g, uint32_t to, uint32_t from, uint16_t last, unsigned rows);

/* scene/race_matrix.c: the scaled picture regrouped into sprite tiles in the
 * tilemap buffer. */
void scene_gather_picture_into_tiles(GameState *g);

/* scene/title.c: a 32x32-tile picture's tile numbers, unpacked in
 * `picture_bank`, as the plane's name table. */
void scene_load_plane_map(GameState *g, Platform *pf, uint8_t picture_bank);

/* scene/staff_roll.c: the credits' letters' colors, and their map blanked
 * (the in-front half of every word from the caller's own code at
 * `in_front_site`). */
void scene_set_letter_colors(GameState *g);
void scene_blank_credits_map(Platform *pf, PfOrigin in_front_site);

/* scene/race_matrix.c: the regrouping routine written, through the work RAM
 * port as the caller has pointed it, for a picture at `source` into the page
 * at `destination`. */
void scene_write_matrix_code_for(GameState *g, uint16_t destination, uint16_t source);
/* The writer's Y as it comes back: past its table of strips, two bytes each
 * (scaffolding, for the code after a setup that ends with it). */
uint16_t scene_matrix_writer_leaves_y(void);

/* scene/perspective.c: the Mode 7 plane's perspective built again for a
 * channel's camera, as the race's camera check and the endings ask for it. */
void scene_build_perspective(GameState *g, SceneChannel ch);

/* ---- Entries, by file ------------------------------------------------------------------------ */

/* scene/guards.c */
bool scene_on_scene_page(Snes *s);
bool scene_on_scene_page_low_bank(Snes *s);
bool scene_on_scene_page_io_bank(Snes *s);
bool scene_on_program_page(Snes *s);
bool scene_on_program_page_io_bank(Snes *s);
bool scene_on_channel_page(Snes *s);
bool scene_channel_at(uint16_t d, SceneChannel *ch);
SceneProgramPage scene_program_page_at_entry(const Snes *s);

/* scene/boot.c */
void scene_boot(Snes *s);
bool scene_boot_bank_reaches_io(Snes *s);
void scene_boot_nmi(Snes *s);
void scene_boot_line_irq(Snes *s);
void scene_boot_line_irq_wait_hblank_end_loop(Snes *s);
void scene_boot_line_irq_wait_hblank_loop(Snes *s);
void scene_boot_line_irq_blank(Snes *s);
void scene_copy_resource(Snes *s);
void scene_copy_resource_to_upper_ram(Snes *s);
void scene_unpack_resource(Snes *s);
bool scene_unpack_present(Snes *s);
/* `count` bytes of bank $FE from `from` into `bank` at `to`, as those two
 * copy them. */
void scene_copy_resource_bytes(GameState *g, uint8_t bank, uint16_t from, uint16_t to, uint32_t count);

/* scene/frame.c */
void scene_nmi(Snes *s);
void scene_nmi_while_working(Snes *s);
void scene_line_irq_ack(Snes *s);
void scene_frame_work(Snes *s);
bool scene_pause_is_a_switch(Snes *s);
bool scene_frame_tasks_present(Snes *s);
bool scene_vblank_tasks_present(Snes *s);
void scene_run_frame_tasks(Snes *s);
void scene_run_vblank_tasks(Snes *s);


/* scene/script.c */
void scene_run_sequence(Snes *s);
void scene_run_channel_0_script(Snes *s);
void scene_run_channel_1_script(Snes *s);
bool scene_channel_script_start_present(Snes *s);
void scene_script_wait_for_sound(Snes *s);
bool scene_sequence_bank_is_scripts(Snes *s);
bool scene_sequence_waits_on_channel(Snes *s);

/* scene/screen.c (the program in RAM; each guarded by a hash of its code) */
bool scene_program_restart_present(Snes *s);
void scene_program_restart(Snes *s);
bool scene_clear_work_ram_present(Snes *s);
void scene_clear_work_ram(Snes *s);
bool scene_init_video_present(Snes *s);
void scene_init_video(Snes *s);
bool scene_upload_sprites_present(Snes *s);
void scene_upload_sprites(Snes *s);
void scene_finish_and_upload_sprites(Snes *s);
void scene_brighten(Snes *s);
void scene_darken(Snes *s);
void scene_fade_the_music_out(Snes *s);
void scene_darken_in_silence_and_end(Snes *s);
void scene_darken_and_end(Snes *s);
bool scene_brighten_present(Snes *s);
bool scene_darken_present(Snes *s);
bool scene_fade_the_music_out_present(Snes *s);
bool scene_darken_in_silence_and_end_present(Snes *s);
bool scene_darken_and_end_present(Snes *s);
void scene_animate_the_palettes(Snes *s);
bool scene_animate_the_palettes_present(Snes *s);
bool scene_finish_and_upload_sprites_present(Snes *s);
bool scene_clear_sprite_table_present(Snes *s);
void scene_clear_sprite_table(Snes *s);
bool scene_upload_palette_present(Snes *s);
void scene_upload_palette(Snes *s);
bool scene_transfer_present(Snes *s);
void scene_transfer(Snes *s);
bool scene_setup_line_effects_present(Snes *s);
void scene_setup_line_effects(Snes *s);

/* scene/title.c (the program in RAM) */
void scene_title_set_up(Snes *s);
/* The setups as the program's dispatch runs them (scene/program.c): each
 * leaves the registers as its routine returns them, and is true unless a
 * call-out of its stopped. The title's then waits for its song, in the
 * original's loop. */
bool scene_title_set_up_scene(Snes *s);
void scene_title_wait_for_its_song(Snes *s);
bool scene_race_set_up_scene(Snes *s);
bool scene_ending_set_up_scene(Snes *s);
bool scene_staff_roll_set_up_scene(Snes *s);
bool scene_title_set_up_present(Snes *s);
bool scene_title_loader_present(Snes *s);
void scene_title_load_sprite_graphics(Snes *s);
void scene_title_load_layer_tiles(Snes *s);
void scene_title_load_plane_graphics(Snes *s);
void scene_title_load_bg1_map(Snes *s);
void scene_title_load_bg2_map(Snes *s);
void scene_title_fill_bg1_map(Snes *s);
bool scene_title_set_up_pendulum_present(Snes *s);
void scene_title_set_up_pendulum(Snes *s);
bool scene_title_load_plane_map_present(Snes *s);
void scene_title_load_plane_map(Snes *s);
bool scene_title_place_pendulum_present(Snes *s);
void scene_title_swing_pendulum(Snes *s);
void scene_title_draw_the_clock(Snes *s);
bool scene_title_draw_the_clock_present(Snes *s);
void scene_title_draw_the_hands(Snes *s);
bool scene_title_draw_the_hands_present(Snes *s);
void scene_title_draw_the_copyright(Snes *s);
bool scene_title_draw_the_copyright_present(Snes *s);
void scene_title_slide_the_name(Snes *s);
bool scene_title_slide_the_name_present(Snes *s);
void scene_title_count_the_wait_down(Snes *s);
bool scene_title_count_the_wait_down_present(Snes *s);
void scene_title_watch_for_a_button(Snes *s);
bool scene_title_watch_for_a_button_present(Snes *s);
bool scene_title_swing_pendulum_present(Snes *s);
void scene_title_place_pendulum(Snes *s);
bool scene_title_move_logo_present(Snes *s);
void scene_title_move_logo(Snes *s);
bool scene_title_start_ripple_present(Snes *s);
void scene_title_start_ripple(Snes *s);
void scene_title_darken_sprite_palette_4(Snes *s);
void scene_title_untint_sprite_palette_3(Snes *s);
bool scene_title_darken_sprite_palette_4_present(Snes *s);
bool scene_title_untint_sprite_palette_3_present(Snes *s);
bool scene_title_expand_plane_tile_present(Snes *s);
void scene_title_expand_plane_tile(Snes *s);
bool scene_title_ripple_columns_present(Snes *s);
void scene_title_ripple_columns(Snes *s);
void scene_title_calm_the_ripple(Snes *s);
bool scene_title_calm_the_ripple_present(Snes *s);
bool scene_title_upload_ripple_present(Snes *s);
void scene_title_upload_ripple(Snes *s);

/* scene/race.c (the program in RAM) */
bool scene_race_set_up_present(Snes *s);
void scene_race_set_up(Snes *s);
void scene_race_set_up_entry(Snes *s);
bool scene_race_sweep_curtain_present(Snes *s);
void scene_race_sweep_curtain(Snes *s);
bool scene_race_move_camera_present(Snes *s);
void scene_race_move_camera(Snes *s);
bool scene_race_upload_column_present(Snes *s);
void scene_race_upload_column_rom(Snes *s);
void scene_race_upload_column_ram(Snes *s);
bool scene_race_upload_tilemap_present(Snes *s);
void scene_race_upload_tilemap(Snes *s);
bool scene_race_upload_strip_present(Snes *s);
void scene_race_upload_strip(Snes *s);
bool scene_race_upload_plane_present(Snes *s);
void scene_race_upload_plane(Snes *s);
bool scene_race_wait_for_tilemap_upload_present(Snes *s);
void scene_race_wait_for_tilemap_upload_loop(Snes *s);
bool scene_race_engine_present(Snes *s);
void scene_race_set_engine_pitch(Snes *s);

/* scene/perspective.c (the program in RAM): the entry the endings jump to,
 * and for the race's camera check, whether the port builds the perspective
 * for the channel at a page and what the original leaves in the registers. */
bool scene_build_perspective_present(Snes *s);
void scene_build_perspective_entry(Snes *s);
bool scene_perspective_as_ported(Snes *s, uint16_t d);
void scene_leave_perspective_registers(Snes *s, SceneChannel ch);

/* scene/bridge.c: a scene's song, from the program's routine that starts one,
 * called from a setup at `call`. */
void scene_bridge_start_song(Snes *s, uint32_t call, uint8_t song);
/* scene/bridge.c: the frame a call from `call` leaves on the stack, for the
 * calls a port makes as the routine the call reached would. */
void scene_bridge_enter(Snes *s, uint32_t call);
void scene_bridge_leave(Snes *s);
/* scene/bridge.c: a sibling port's registered entry run as C, as the
 * original's JSR (JSL) at `site` calls `routine`: under the return address the
 * call pushes, where the sibling's own call-outs and stops need it, and timed
 * as its routine. Where the registry wouldn't run `entry` for the state (its
 * guard or its widths turn it away, or `entry` is NULL: the original's routine
 * has no port), the call is a real one, declaring `dont_care`. True if it came
 * back to the call's return with the stack as it was; otherwise it stopped,
 * and the original carries on from where it did. */
bool scene_run(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *));
bool scene_run_long(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care, void (*entry)(Snes *));


/* scene/program.c (the program in RAM): its start, the pieces of it its waits
 * cut, and its end. */
void scene_program_start(Snes *s);
void scene_program_goes_on(Snes *s);
void scene_setup_and_first_frame(Snes *s);
void scene_first_frame(Snes *s);
bool scene_program_start_present(Snes *s);
void scene_program_hands_back(Snes *s);
bool scene_setup_present(Snes *s);
/* scene/curtain.c: a frame's step of the curtain (the scene engine's $C3:01E4,
 * and its jump $C3:000E) */
void scene_sweep_the_curtain(Snes *s);
void scene_curtain_step(GameState *g, Platform *pf, uint16_t salt);
void scene_curtain_start(GameState *g, uint8_t sweep, uint16_t table, uint8_t bank);
bool scene_curtain_table_fits(uint8_t bank, uint16_t table);
void scene_curtain_jump(Snes *s);
bool scene_sweep_the_curtain_present(Snes *s);
bool scene_program_end_present(Snes *s);
/* The game back to the field's start on the game's fresh stack, as the
 * program ends. */
void scene_hand_back_to_the_field(Snes *s);
/* scene/staff_roll.c (the program in RAM) */
bool scene_staff_roll_set_up_present(Snes *s);
void scene_staff_roll_set_up(Snes *s);
bool scene_credits_roll_present(Snes *s);
void scene_roll_credits(Snes *s);
bool scene_credits_scroll_present(Snes *s);
void scene_scroll_credits_entry(Snes *s);
bool scene_credits_upload_present(Snes *s);
void scene_upload_credits(Snes *s);
bool scene_clear_sprite_high_bits_present(Snes *s);
void scene_clear_sprite_high_bits(Snes *s);
bool scene_credits_test_present(Snes *s);
void scene_go_on_after_the_credits(Snes *s);
bool scene_fade_in_the_picture_present(Snes *s);
void scene_fade_in_the_picture(Snes *s);
/* scene/endings.c (the program in RAM) */
bool scene_ending_set_up_present(Snes *s);
void scene_ending_set_up(Snes *s);
bool scene_ending_load_picture_present(Snes *s);
void scene_ending_load_picture(Snes *s);
bool scene_ending_load_ground_and_picture_present(Snes *s);
void scene_ending_load_ground_and_picture(Snes *s);
bool scene_ending_start_stars_present(Snes *s);
void scene_ending_start_stars(Snes *s);
bool scene_ending_start_mode7_picture_present(Snes *s);
void scene_ending_start_mode7_picture(Snes *s);
bool scene_ending_prepare_credits_present(Snes *s);
void scene_ending_prepare_credits(Snes *s);
bool scene_ending_set_last_colors_present(Snes *s);
void scene_ending_set_last_colors(Snes *s);
bool scene_ending_show_credits_present(Snes *s);
void scene_ending_show_credits(Snes *s);
bool scene_ending_show_last_scene_present(Snes *s);
void scene_ending_show_last_scene(Snes *s);
bool scene_ending_sweep_window_present(Snes *s);
void scene_ending_sweep_window(Snes *s);
bool scene_ending_scroll_bg2_present(Snes *s);
void scene_ending_scroll_bg2(Snes *s);
bool scene_ending_upload_picture_present(Snes *s);
void scene_ending_upload_picture(Snes *s);
bool scene_ending_place_picture_present(Snes *s);
void scene_ending_place_picture(Snes *s);
bool scene_ending_pick_picture_frame_present(Snes *s);
void scene_ending_pick_picture_frame(Snes *s);
bool scene_ending_scale_picture_present(Snes *s);
void scene_ending_scale_picture(Snes *s);
bool scene_ending_gather_picture_present(Snes *s);
void scene_ending_gather_picture(Snes *s);
/* scene/bridge.c: where the endings' scaling of their picture goes on in the
 * original's scaler. */
void scene_bridge_scale_ending_picture(Snes *s);

/* scene/stars.c (the program in RAM) */
bool scene_stars_scatter_code_present(Snes *s);
bool scene_stars_scatter_present(Snes *s);
void scene_stars_scatter(Snes *s);
bool scene_stars_fly_present(Snes *s);
void scene_stars_fly(Snes *s);
bool scene_stars_fly_after_captions_present(Snes *s);
void scene_stars_fly_after_captions(Snes *s);
bool scene_stars_drift_present(Snes *s);
bool scene_stars_start_drift_present(Snes *s);
void scene_stars_start_drift(Snes *s);
void scene_stars_keep_drifting(Snes *s);

/* scene/captions.c (the program in RAM) */
bool scene_draw_caption_present(Snes *s);
void scene_draw_caption(Snes *s);
bool scene_turn_captions_present(Snes *s);
void scene_turn_captions(Snes *s);

/* scene/race_matrix.c */
void scene_write_matrix_code(Snes *s);
bool scene_matrix_code_present(Snes *s);
void scene_gather_picture(Snes *s);
bool scene_race_scale_pictures_present(Snes *s);
void scene_race_scale_pictures(Snes *s);
bool scene_race_scale_after_present(Snes *s);
void scene_race_scale_after_crono(Snes *s);
void scene_race_scale_after_johnny(Snes *s);
bool scene_race_gather_scaled_present(Snes *s);
void scene_race_gather_scaled(Snes *s);

/* scene/race_rules.c (the program in RAM) */
bool scene_race_ride_present(Snes *s);
bool scene_race_ride_lists_fit(Snes *s);
void scene_race_ride(Snes *s);
bool scene_race_coast_present(Snes *s);
bool scene_race_coast_lists_fit(Snes *s);
void scene_race_coast(Snes *s);
bool scene_race_await_victory_song_present(Snes *s);
void scene_race_await_victory_song(Snes *s);
bool scene_race_wait_to_leave_present(Snes *s);
void scene_race_wait_to_leave(Snes *s);
bool scene_race_leave_present(Snes *s);
void scene_race_leave(Snes *s);
bool scene_race_drain_score_present(Snes *s);
void scene_race_drain_score(Snes *s);

/* scene/race_projection.c (the program in RAM) */
bool scene_race_place_bikes_present(Snes *s);
void scene_race_place_bikes(Snes *s);

/* scene/race_drawing.c (the program in RAM) */
bool scene_race_draw_present(Snes *s);
void scene_race_draw(Snes *s);

/* scene/race_camera.c (the program in RAM) */
bool scene_race_settle_camera_present(Snes *s);
void scene_race_settle_camera(Snes *s);
bool scene_race_move_along_present(Snes *s);
void scene_race_move_along(Snes *s);
bool scene_race_keep_controller_present(Snes *s);
void scene_race_keep_controller(Snes *s);
bool scene_race_check_perspective_present(Snes *s);
void scene_race_check_perspective(Snes *s);
bool scene_race_turn_camera_present(Snes *s);
void scene_race_turn_camera(Snes *s);
bool scene_race_aim_camera_present(Snes *s);
void scene_race_aim_camera(Snes *s);
bool scene_race_follow_finish_present(Snes *s);
void scene_race_follow_finish(Snes *s);
bool scene_race_set_horizon_present(Snes *s);
void scene_race_set_horizon(Snes *s);
bool scene_race_pick_opening_present(Snes *s);
void scene_race_pick_opening(Snes *s);

/* scene/sound.c (the program in RAM) */
void scene_sound_back_to_normal(Snes *s);
bool scene_sound_back_to_normal_present(Snes *s);
bool scene_wait_for_music_present(Snes *s);
void scene_wait_for_music_loop(Snes *s);
bool scene_send_sound_command_present(Snes *s);
void scene_send_sound_command(Snes *s);
void scene_send_sound_command_again(Snes *s);
void scene_wait_sound_command_echo_loop(Snes *s);

/* scene/sprites.c */
bool scene_hide_rest_of_group_present(Snes *s);
void scene_hide_rest_of_group_entry(Snes *s);
bool scene_sprite_list_fits(Snes *s);
bool scene_clipped_sprite_list_fits(Snes *s);
bool scene_sprite_fits(Snes *s);
void scene_load_sprite_list(Snes *s);
void scene_load_sprite_list_clipped(Snes *s);
void scene_add_sprite(Snes *s);
void scene_add_large_sprite(Snes *s);

/* scene/circle.c */
bool scene_circle_table_is_clear(Snes *s);
void scene_draw_circle(Snes *s);

/* scene/scaler.c */
bool scene_scaler_in_its_bank(Snes *s);
void scene_scale_bitmap_row(Snes *s);
void scene_scale_bitmap(Snes *s);

/* scene/math.c */
bool scene_divide_present(Snes *s);
void scene_divide(Snes *s);
bool scene_divide_8_8_present(Snes *s);
void scene_divide_8_8(Snes *s);
bool scene_multiply_present(Snes *s);
void scene_multiply(Snes *s);
void scene_multiply_signed(Snes *s);
void scene_find_divisor(Snes *s);
bool scene_find_divisor_on_divider_page(Snes *s);

#endif
