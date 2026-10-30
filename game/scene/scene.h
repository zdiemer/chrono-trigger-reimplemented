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
 *   credit_pages.c the attract sequence's pages of the game's staff
 *   rising_palace.c the palace rising out of the sea under the red moon
 *   planet.c      the planet seen from space: the Day of Lavos's end when the
 *                 party loses, and an ending's (modes 7 and 8): its colours'
 *                 steps and the stars that twinkle about it
 *   staff_roll.c  the telepod ending's last scene: the staff credits over the
 *                 stars, and The End
 *   lavos_falls.c Lavos falling to the planet (mode 4)
 *   epoch_dive.c  the Epoch's dive into Lavos (mode 6): the shell from the
 *                 cockpit and from outside
 *   beyond_time.c the Beyond Time ending's night: Taban's fireworks, the
 *                 town's lights, the Epoch's flight over the world's map and
 *                 the Mode 7 pictures between
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

#include "original_effects.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/native.h"
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
    /* The scene is the attract sequence's showing of it. The race is drawn
     * without its panel, runs the attract's scripts (its camera one of two,
     * by turns) and closes behind a curtain (scene/race.c, race_drawing.c);
     * the palace rising runs the attract's scripts, opens and closes behind a
     * curtain and fades faster (scene/rising_palace.c). */
    SCENE_FLAG_ATTRACT_SEQUENCE = 0x40,
    /* The frame's work doesn't send the sound driver song changes. */
    SCENE_FLAG_NO_SONGS = 0x20,
};

/* A scene's mode, the low bits of scene_flags, which picks its setup from the
 * program's table (scene/program.c). */
typedef enum {
    SCENE_MODE_RACE = 0, SCENE_MODE_CREDITS_PAGE = 1, SCENE_MODE_TITLE = 2, SCENE_MODE_RISING_PALACE = 3,
    SCENE_MODE_LAVOS_FALLS = 4, SCENE_MODE_EPOCH_DIVE = 6, SCENE_MODE_PLANET_LOST = 7, SCENE_MODE_PLANET_ENDING = 8,
    SCENE_MODE_ATTRACT_RACE = 9, SCENE_MODE_ENDING_CAPTIONED = 10, SCENE_MODE_ENDING_CAPTIONED_TOO = 11,
    SCENE_MODE_STAFF_ROLL = 13, SCENE_MODE_ENDING_AGAIN = 14, SCENE_MODE_ENDING_WITHOUT_CREDITS = 15,
} SceneMode;

/* The main loop's call each pass, which a setup names (scene_main_loop_call):
 * nothing, or the scaling of a picture the scene draws as it goes. */
typedef enum {
    SCENE_PASS_NOTHING = 0x3176, SCENE_PASS_RACE_SCALING = 0x51D1, SCENE_PASS_ENDING_SCALING = 0x8930,
    SCENE_PASS_DIVE_SCALING = 0x8278,
} ScenePass;

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

/* Where the setups have the unpacker leave what goes on to VRAM, in the
 * program's second bank, and where they build a Mode 7 plane's name table
 * (half of its 128 rows at a time). */
enum { SCENE_UNPACK_BUFFER = 0x7F0200, SCENE_PLANE_MAP = 0x7F2200 };

/* How the loaders' transfer (scene_transfer) moves its bytes, as the program
 * keeps it for the SNES-only escape: into one register, a byte at a time, or
 * into a register pair, a word at a time. */
enum { SCENE_TRANSFER_BYTES = 0x00, SCENE_TRANSFER_WORDS = 0x01 };

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
    SCENE_TASK_SHOW_THE_CREDITS_PAGE = 0x6BE5,  /* scene/credit_pages.c */
    SCENE_TASK_SEND_THE_PALACE_OFFSETS = 0x66DD,   /* scene/rising_palace.c */
    SCENE_TASK_RIPPLE_THE_PALACE = 0x69AF,
    SCENE_TASK_SHIMMER_THE_PALACE = 0x6A23,
    SCENE_TASK_DARKEN_THE_PALACE = 0x6A51,
    SCENE_TASK_WHITEN_THE_PALACE = 0x6A74,
    SCENE_TASK_CLEAR_THE_PALACE = 0x6A97,
    SCENE_TASK_WORK_OUT_THE_TURN = 0x73C6,
    SCENE_TASK_SEND_THE_MAP = 0x783C,
    SCENE_TASK_STEP_THE_PLANET_S_COLOURS = 0x6439,   /* scene/planet.c */
    SCENE_TASK_TWINKLE_THE_STARS = 0x65DD,
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
static inline TaskEnd task_hands_over(SceneTask next) { return (TaskEnd){ TASK_HANDS_OVER, next }; }

/* A program's task as C, in the build with no machine (scene/frame.c's
 * table of the tasks whose entries take the machine): its frame on the
 * list's slot, and how it ends. One that needs the machine says where
 * (program_needs_the_machine) and ends TASK_LEFT_TO_THE_ORIGINAL. */
typedef TaskEnd (*SceneTaskInC)(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_task_needs_the_machine(GameState *g, uint32_t routine, const char *what);

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

/* The tasks each file has as C (scene/title.c, scene/screen.c,
 * scene/credit_pages.c, scene/rising_palace.c, scene/planet.c), which the lists' runner looks
 * a task up in. */
extern const SceneTaskPort scene_title_tasks[];
extern const unsigned scene_title_task_count;
extern const SceneTaskPort scene_screen_tasks[];
extern const unsigned scene_screen_task_count;
extern const SceneTaskPort scene_credit_pages_tasks[];
extern const unsigned scene_credit_pages_task_count;
extern const SceneTaskPort scene_palace_tasks[];
extern const unsigned scene_palace_task_count;
extern const SceneTaskPort scene_planet_tasks[];
extern const unsigned scene_planet_task_count;

/* The program's routines the scripts call (their command $A0), which are C,
 * by the values the scripts hold for them (the routines' addresses). */
enum {
    SCENE_ROUTINE_START_RIPPLE = 0x5DA7,          /* scene/title.c */
    SCENE_ROUTINE_DARKEN_SPRITE_PALETTE_4 = 0x5DB4,
    SCENE_ROUTINE_UNTINT_SPRITE_PALETTE_3 = 0x5DE9,
    SCENE_ROUTINE_LET_LAVOS_FALL = 0x5640,        /* scene/lavos_falls.c */
    SCENE_ROUTINE_SEND_LAVOS_BACK = 0x564D,
    SCENE_ROUTINE_LOOK_FROM_OUTSIDE = 0x7ED1,     /* scene/epoch_dive.c */
    SCENE_ROUTINE_LOOK_FROM_THE_COCKPIT = 0x7EE8,
    SCENE_ROUTINE_FADE_THE_SHELL_IN = 0x82F5,
    SCENE_ROUTINE_WHITE_OUT = 0x831F,
    SCENE_ROUTINE_FADE_THE_PLANET_THROUGH = 0x63FE,     /* scene/planet.c */
    SCENE_ROUTINE_FADE_THE_PLANET_OUT = 0x6403,
    SCENE_ROUTINE_FADE_THE_PLANET_OUT_SLOWLY = 0x6422,
    SCENE_ROUTINE_PICK_THE_RACE_S_OPENING = 0x3F59,     /* scene/race_camera.c */
    SCENE_ROUTINE_LOAD_THE_GROUND_AND_PICTURE = 0x8790,     /* scene/endings.c */
    SCENE_ROUTINE_LOAD_THE_PICTURE = 0x87CC,
    SCENE_ROUTINE_START_THE_STARS = 0x8BAF,
    SCENE_ROUTINE_START_THE_MODE7_PICTURE = 0x8C14,
    SCENE_ROUTINE_PREPARE_THE_CREDITS = 0x8CD0,
    SCENE_ROUTINE_SET_THE_LAST_COLOURS = 0x8DF3,
    SCENE_ROUTINE_STOP_THE_PICTURE = 0x96FB,
    SCENE_ROUTINE_SHOW_THE_GROUND_S_FIRST_LATER_COLORS = 0x9702,
    SCENE_ROUTINE_STEP_THE_GROUND_TOWARD_THE_FIRST = 0x98F6,
    SCENE_ROUTINE_STEP_THE_GROUND_TOWARD_THE_SECOND = 0x98FB,
    SCENE_ROUTINE_STEP_THE_GROUND_TOWARD_THE_THIRD = 0x9900,
    SCENE_ROUTINE_PLACE_THE_CAPTIONS = 0x9919,
    SCENE_ROUTINE_FADE_THE_SPRITES_OUT = 0x9F8A,
    SCENE_ROUTINE_FADE_THE_FIRST_LAYER_COLORS_OUT = 0x9FC8,
    SCENE_ROUTINE_FADE_MORE_LAYER_COLORS_OUT = 0x9FEA,
    SCENE_ROUTINE_GO_ON_AFTER_THE_CREDITS = 0x996F,     /* scene/staff_roll.c */
    SCENE_ROUTINE_FADE_IN_THE_PICTURE = 0x9FA9,
};

/* A routine a script calls, as C: its work on the calling channel; and the
 * guard that says the program's code for it is the code the port stands
 * for. */
typedef struct {
    uint16_t routine;
    void (*run)(GameState *g, Platform *pf, uint8_t channel);
    bool (*present)(Snes *s);
    bool leaves_the_program_bank;   /* it comes back with the data bank on the program's */
} SceneScriptRoutine;

#if ORIGINAL_EFFECTS
/* What a routine leaves in the registers for the original's code after it,
 * in a table parallel to its file's routines: A, from the A it was called
 * with (the routine's address, as the interpreter calls it), and the other
 * registers it changes (the interpreter hands Y on to the sound driver and
 * its caller). The build without the original's effects has none of it. */
typedef struct {
    uint16_t (*a)(uint16_t called_with);
    void (*rest)(Snes *s);   /* NULL: the rest as it was */
} SceneRoutineLeaves;
extern const SceneRoutineLeaves scene_title_routine_leaves[];
extern const SceneRoutineLeaves scene_lavos_routine_leaves[];
extern const SceneRoutineLeaves scene_dive_routine_leaves[];
extern const SceneRoutineLeaves scene_planet_routine_leaves[];
extern const SceneRoutineLeaves scene_race_routine_leaves[];
extern const SceneRoutineLeaves scene_ending_routine_leaves[];
extern const SceneRoutineLeaves scene_staff_roll_routine_leaves[];
#endif

/* The tables' rows, by routine. */
enum { SCENE_ROW_START_RIPPLE, SCENE_ROW_DARKEN_SPRITE_PALETTE_4, SCENE_ROW_UNTINT_SPRITE_PALETTE_3 };
enum { SCENE_ROW_SEND_LAVOS_BACK, SCENE_ROW_LET_LAVOS_FALL };
enum { SCENE_ROW_LOOK_FROM_OUTSIDE, SCENE_ROW_LOOK_FROM_THE_COCKPIT, SCENE_ROW_FADE_THE_SHELL_IN, SCENE_ROW_WHITE_OUT };
enum {
    SCENE_ROW_STOP_THE_PICTURE, SCENE_ROW_STEP_TOWARD_THE_FIRST, SCENE_ROW_STEP_TOWARD_THE_SECOND,
    SCENE_ROW_STEP_TOWARD_THE_THIRD, SCENE_ROW_SHOW_THE_FIRST_LATER_COLORS, SCENE_ROW_PLACE_THE_CAPTIONS,
    SCENE_ROW_FADE_THE_SPRITES_OUT, SCENE_ROW_FADE_THE_FIRST_LAYER_COLORS_OUT, SCENE_ROW_FADE_MORE_LAYER_COLORS_OUT,
};
extern const SceneScriptRoutine scene_title_routines[];
extern const unsigned scene_title_routine_count;
extern const SceneScriptRoutine scene_lavos_routines[];
extern const unsigned scene_lavos_routine_count;
extern const SceneScriptRoutine scene_dive_routines[];
extern const unsigned scene_dive_routine_count;
extern const SceneScriptRoutine scene_planet_routines[];
extern const unsigned scene_planet_routine_count;
extern const SceneScriptRoutine scene_race_routines[];
extern const unsigned scene_race_routine_count;
extern const SceneScriptRoutine scene_ending_routines[];
extern const unsigned scene_ending_routine_count;
extern const SceneScriptRoutine scene_staff_roll_routines[];
extern const unsigned scene_staff_roll_routine_count;
/* scene/endings.c: the routines that load a scene, which call the original's
 * unpacker: rows for the native program alone, each its own entry on the
 * machine. */
extern const SceneScriptRoutine scene_ending_loading_routines[];
extern const unsigned scene_ending_loading_routine_count;
/* scene/script.c: a routine of those tables reached by its address, as its
 * registered entry: its row's work. */
void scene_script_routine_alone(Snes *s, const SceneScriptRoutine *r);

/* scene/frame.c: a task's registered entry, for a caller that reaches it by
 * its address (fuzzing; the lists run it as C): the row's frame, on the row's
 * list, and back as the original's would come back, or through the lists'
 * exits. */
void scene_task_alone(Snes *s, const SceneTaskPort *port);
/* The registers a task leaves as it ends, for an entry that runs a task's
 * work called as a subroutine rather than from a list. */
void scene_leave_task_registers(Snes *s, const SceneTaskPort *port, TaskEnd end);

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
/* A small sprite drawn at the cursor as the program's routine draws one
 * (scene_add_sprite, as C scene_draw_sprite), moved by the offset, for a port
 * that has it as values. */
void scene_draw_sprite_moved(GameState *g, SceneSprite sprite);
uint16_t scene_clipped_list_leaves_a(const GameState *g, uint16_t list, uint16_t a);
/* The overflow a clipped list leaves (of its last sprite's Y). */
bool scene_clipped_list_leaves_overflow(const GameState *g, uint16_t list);
/* The rest of the cursor's group of four sprites put off the screen, as the
 * frame's last sprites leave it. */
void scene_hide_rest_of_group(GameState *g);
/* Where the cursor stands in its group of four sprites, whose bits the high
 * table keeps in one byte: its entry's byte offset in the group, 0-15. */
uint8_t scene_sprite_place_in_group(const GameState *g);

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
/* Whether a draw spilled a bit over its top. */
static inline bool scene_draw_spilled(SceneDraw d, TmEntropyWidth width)
{
    return (d >> (width == TM_ENTROPY_WORDS ? 16 : 8) & 1) != 0;
}
uint8_t scene_next_random_byte(GameState *g);
SceneDraw scene_draw_random(GameState *g, Platform *pf, PfOrigin o, SceneDraw last, uint16_t salt, TmEntropyWidth width);
/* The same draw with the palette's bytes for noise, as the program's code
 * draws with the data bank on work RAM, where the stir's reads of the beam
 * land on the palette. */
SceneDraw scene_draw_random_off_the_palette(GameState *g, SceneDraw last, uint16_t salt, TmEntropyWidth width);
bool scene_random_last_overflow(void);
/* Something left for the draws after it to stir in (scene_call_stirred), as
 * wide as the draw that takes it. */
void scene_leave_for_the_draws(GameState *g, uint16_t value, TmEntropyWidth width);

/* scene/stars.c: the star field's speed and drift, which the scripts and the
 * scenes' tasks set in channel 1's registers, and how its drawing is set: the
 * near stars in front of the layers, as the program comes, or every star
 * behind them (the branch in its code a scene writes). */
enum {
    SCENE_STAR_FIELD_CHANNEL = 1, SCENE_STAR_FIELD_SPEED = 0, SCENE_STAR_FIELD_DRIFT_ACROSS = 1,
    SCENE_STAR_FIELD_DRIFT_DOWN = 2,
};
enum { SCENE_NEAR_STARS_IN_FRONT = 0x0430, SCENE_EVERY_STAR_BEHIND = 0x0080 };

/* scene/stars.c: every star of the field scattered at random, from a first
 * draw. */
void scene_scatter_stars(GameState *g, Platform *pf, SceneDraw first);

/* A sequence channel: one of the two the program runs a script on
 * (scene_channels), whose page its routines run on when a script calls them. */
typedef unsigned SceneChannel;

/* scene/script.c: a channel's registers, 0-15. */
uint16_t scene_channel_register(const GameState *g, SceneChannel ch, unsigned n);
void set_scene_channel_register(GameState *g, SceneChannel ch, unsigned n, uint16_t v);

/* scene/script.c: a channel's turn as C, for a program with no machine under
 * it: its script run until a command ends the turn; or until a command, or a
 * routine it calls, is one only the machine runs yet, where the program goes
 * no further (core/program.h). */
typedef enum { SCENE_TURN_ENDED, SCENE_TURN_NEEDS_THE_MACHINE } SceneTurn;
SceneTurn scene_channel_turn(GameState *g, Platform *pf, SceneChannel ch);

/* scene/sound.c: how far the music has got. The sound driver shows it on a
 * port, which two reads that agree say is steady (not being changed as it is
 * read); its progress is a level from 0 to 15 that the song sets as it plays. */
typedef struct {
    uint8_t first, second;   /* the two reads */
    bool steady;
} MusicStatus;
MusicStatus scene_music_status(Platform *pf);
/* As C, for a program with no machine under it: the sound put back to normal
 * as a scene starts and ends, each command sent through the program's own
 * send and its echo waited for; that send alone, of the command in the
 * buffer; and whether the music has started playing. */
void scene_sound_back_to_normal(GameState *g, Platform *pf);
void scene_sends_the_command(GameState *g, Platform *pf);
bool scene_music_is_playing(Platform *pf);
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
enum { SCENE_FULL_TURN = 0x400, SCENE_HALF_TURN = 0x200, SCENE_QUARTER_TURN = 0x100 };
int16_t scene_sine(const GameState *g, uint16_t angle);
int16_t scene_cosine(const GameState *g, uint16_t angle);
bool scene_sine_second_half(uint16_t angle);
/* For the entry wrappers: whether an angle a quarter turn on passes the top
 * of a signed word (from $7F00 up to $7FFF), the overflow the original's
 * cosine leaves where it adds the quarter turn, and its perspective's builder
 * after the cosine of the plane's turn. */
bool scene_quarter_turn_on_overflows(uint16_t angle);

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
/* A signed `value` (a sine, mostly) scaled by `by` 256ths, through the
 * 16 x 16-bit product: the magnitude scaled and the sign put back, so a
 * negative value rounds toward 0. */
int16_t scene_scale_by_256ths(GameState *g, SceneProgramPage page, int16_t value, uint16_t by);
/* The 8.8 fraction of `dividend`'s high byte over `divisor`, the dividend
 * left in the first operand's cell, where the random draws stir it in. */
uint16_t scene_divide_8_8_by(GameState *g, SceneProgramPage page, uint16_t dividend, uint16_t divisor);
uint8_t scene_find_divisor_from(GameState *g, uint16_t dividend, uint8_t from, uint16_t limit);

/* scene/screen.c: the program's transfer of what the unpacker made, in the
 * mode and into the register given, or as the caller has set them in its
 * cells (scene_call); and the per-line effects from a table of descriptors
 * (returns where it ends). */
void scene_send_unpacked_as(const GameState *g, Platform *pf, uint8_t mode, uint8_t port_register);
void scene_send_unpacked(const GameState *g, Platform *pf);
/* The two ways the loaders send what was unpacked into VRAM: a byte at a
 * time into the high bytes (Mode 7's pixels), or a word at a time. A setup
 * chooses one before it unpacks what goes that way, and the choice stays in
 * the scene's call cells, as the original sets them, which is where a send
 * that is given none takes it from (scene_send_unpacked); the setups' ports
 * hand theirs to the send. */
typedef struct {
    uint8_t mode, vram_register;
} SceneTransfer;
extern const SceneTransfer scene_pixels_into_high_bytes, scene_whole_words;
void scene_choose_transfer(GameState *g, SceneTransfer t);
void scene_send_unpacked_by(GameState *g, Platform *pf, SceneTransfer t);
/* scene/screen.c: the program's transfer from its call cells: `bytes` from
 * `source` into a register in a mode; and a buffer cleared by a transfer out
 * of the Mode 7 multiplier's product (0 where the scenes do it), by the
 * program's code at `site`. */
void scene_send_from(GameState *g, Platform *pf, uint8_t mode, uint8_t port_register, uint32_t source, uint16_t bytes);
void scene_clear_buffer(Platform *pf, uint32_t buffer, uint16_t bytes, uint32_t site);
uint16_t scene_set_up_line_effects(const GameState *g, Platform *pf, uint16_t at);
/* scene/screen.c: the working palette sent; the bank the unpacker's
 * destination names; `bytes` of a resource copied as they are to `to` in that
 * bank, the count handed over in `page`'s first cell; and the loaded palette
 * made the one the frame sends. */
void scene_send_palette(Platform *pf);
/* scene/screen.c: a frame of the palette animations, as a channel's script
 * has the program run it itself (the Epoch's dive's fades). */
void scene_animate_the_palettes_for(GameState *g, SceneChannel ch);
/* scene/screen.c: the program's work RAM cleared (true: the title's shorter
 * clear, which keeps what lies below the task lists), every sprite hidden, and
 * the screen every scene starts from. */
bool scene_clear_the_work_ram(GameState *g, Platform *pf, uint8_t matrix_left);
void scene_hide_every_sprite(Platform *pf);
void scene_set_up_first_screen(GameState *g, Platform *pf);
uint8_t scene_unpacked_bank(const GameState *g);
void scene_copy_resource_to(GameState *g, SceneProgramPage page, unsigned resource, uint16_t to, uint16_t bytes);
void scene_use_loaded_palette(GameState *g);
void scene_use_loaded_palette_entry(Snes *s);
bool scene_use_loaded_palette_present(Snes *s);
/* scene/screen.c: a Mode 7 plane's name table, unpacked 64 tiles a row into
 * SCENE_UNPACK_BUFFER, widened to the plane's 128 into SCENE_PLANE_MAP; and
 * the plane's name table blank for `bytes` words past where VRAM's address
 * is, by the call at `site`. */
void scene_widen_the_plane_map(GameState *g);
void scene_blank_past_the_plane(Platform *pf, PfOrigin site, uint16_t bytes);

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
 * `picture_bank`, as the plane's name table; and a tile of four or eight
 * planes at `tile` turned into the plane's pixels. A tile's planes lie in
 * pairs, a pair a row of two bytes for each of its eight rows; a tile of
 * `planes` takes scene_plane_tile_bytes. */
void scene_load_plane_map(GameState *g, Platform *pf, uint8_t picture_bank);
enum { SCENE_MOST_PLANES = 8, SCENE_PLANE_PAIR_BYTES = 0x10 };
static inline uint16_t scene_plane_tile_bytes(unsigned planes) { return (uint16_t)(planes / 2 * SCENE_PLANE_PAIR_BYTES); }
void scene_expand_plane_tile(GameState *g, Platform *pf, uint8_t bank, uint16_t tile, unsigned planes);
/* `count` tiles of a picture in the second bank, from `first`, each `step`
 * bytes on from the last, expanded in turn (returns past the last). */
uint16_t scene_expand_tiles(GameState *g, Platform *pf, uint16_t first, uint16_t count, unsigned planes, uint16_t step);

/* scene/staff_roll.c: the credits' letters' colors, and their map blanked
 * (the in-front half of every word from the caller's own code at
 * `in_front_site`). */
void scene_set_letter_colors(GameState *g);
void scene_blank_credits_map(Platform *pf, PfOrigin in_front_site);

/* scene/race_matrix.c: the regrouping routine written into its place in the
 * program, for a picture at `source` into the page at `destination`. */
void scene_write_matrix_code_for(GameState *g, uint16_t destination, uint16_t source);
/* The writer's Y as it comes back: past its table of strips, two bytes each
 * (scaffolding, for the code after a setup that ends with it). */
uint16_t scene_matrix_writer_leaves_y(void);

/* scene/plane.c: the Mode 7 plane under channel 0's camera, in its
 * registers: the camera's height (the higher, the smaller the picture), its
 * place (the plane's scroll), the point the picture turns about, its turn,
 * and, shown flat, the matrix the turn and the height make. Shown flat, the
 * matrix worked out from the turn and the height, and the plane sent in the
 * vblank (matrix, centre and scroll); in perspective, its scroll and centre
 * sent. Seen in perspective (scene/perspective.c), the camera also has the
 * first line under the horizon, the distance of the plane's first line (the
 * larger, the farther the plane starts) and, in the register the flat
 * plane's B takes, whether the lines are built in pairs (the race's, while
 * nonzero); in the Epoch's dive, seen from the cockpit, the frames counted
 * as the shell above is built. */
enum { SCENE_CAMERA_CHANNEL = 0 };
enum {
    SCENE_PLANE_HEIGHT = 0, SCENE_PLANE_X = 1, SCENE_PLANE_Y = 2, SCENE_PLANE_CENTRE_X = 3, SCENE_PLANE_CENTRE_Y = 4,
    SCENE_PLANE_TURN = 5, SCENE_PLANE_HORIZON = 6, SCENE_PLANE_SHELL_FRAMES = 7, SCENE_PLANE_TOP = 8, SCENE_PLANE_A = 10,
    SCENE_PLANE_B = 11, SCENE_PLANE_LINES_PAIRED = 11, SCENE_PLANE_C = 12, SCENE_PLANE_D = 13,
};
void scene_turn_the_flat_plane(GameState *g);
void scene_send_the_flat_plane(const GameState *g, Platform *pf);
void scene_send_the_plane_s_place(const GameState *g, Platform *pf);

/* scene/perspective.c: the Mode 7 plane's perspective built again for a
 * channel's camera, as the race's camera check and the endings ask for it. */
void scene_build_perspective(GameState *g, SceneChannel ch);
/* Whether channel 0's camera differs from the one the perspective was last
 * built for, in its first `registers` of the five kept (height, turn,
 * horizon, depth, the lines paired): those from the first that differs on are
 * kept as the perspective's. Returns the first that differed, or `registers`
 * where none did. */
unsigned scene_keep_the_camera(GameState *g, unsigned registers);
/* The same for the Epoch's dive's camera, which the perspective keeps its
 * height, its turn and the count of the cockpit's builds of the shell above
 * for: whether any differed. */
bool scene_keep_the_dive_camera(GameState *g);
/* The Epoch's dive's perspective set to its view of the shell from outside,
 * or from the cockpit (perspective.c's lines). */
void scene_look_at_the_shell_from_outside(GameState *g);
void scene_look_at_the_shell_from_the_cockpit(GameState *g);

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
/* As C, for a program with no machine under it: the boot with its flags,
 * the program unpacked into work RAM (true where the scene runs with the line
 * interrupt); the boot's two handlers; and a resource unpacked by its place
 * in the program's table. */
bool scene_boots(GameState *g, Platform *pf, uint8_t flags);
void scene_boot_vblank(GameState *g, Platform *pf);
void scene_boot_line(GameState *g, Platform *pf);
void scene_unpacks(GameState *g, uint16_t resource);
bool scene_boot_bank_reaches_io(Snes *s);
void scene_boot_nmi(Snes *s);
void scene_boot_line_irq(Snes *s);
void scene_boot_line_irq_wait_hblank_end_loop(Snes *s);
void scene_boot_line_irq_wait_hblank_loop(Snes *s);
void scene_boot_line_irq_blank(Snes *s);
void scene_copy_resource(Snes *s);
void scene_copy_resource_to_upper_ram(Snes *s);
void scene_unpack_resource(Snes *s);
void scene_call_the_scripts_routine(Snes *s);
void scene_sequence_returns(Snes *s);
void scene_sequence_goes_on_after_the_wait(Snes *s);
void scene_channel_script_comes_back(Snes *s);
bool scene_scripts_routine_call_present(Snes *s);
bool scene_unpack_present(Snes *s);
/* `count` bytes of bank $FE from `from` into `bank` at `to`, as those two
 * copy them. */
void scene_copy_resource_bytes(GameState *g, uint8_t bank, uint16_t from, uint16_t to, uint32_t count);

/* scene/frame.c */
void scene_nmi(Snes *s);
/* As C, for a program with no machine under it: the frame's work, and the
 * NMI handlers -- the frame's (the work, then the song), and the one that
 * only acknowledges while it runs. The work is false where a task is one
 * only the machine runs yet, the program going no further there. */
bool scene_frame_work(GameState *g, Platform *pf);
void scene_vblank(GameState *g, Platform *pf);
void scene_vblank_while_working(GameState *g, Platform *pf);
enum { SCENE_FRAME_HANDLER = 0xC308B3, SCENE_HANDLER_WHILE_WORKING = 0xC308C9, SCENE_BOOT_VBLANK = 0xC30548,
       SCENE_BOOT_LINE = 0xC30529 };
void scene_nmi_while_working(Snes *s);
void scene_line_irq_ack(Snes *s);
void scene_frame_work_entry(Snes *s);
bool scene_pause_is_a_switch(Snes *s);
bool scene_frame_tasks_present(Snes *s);
bool scene_vblank_tasks_present(Snes *s);
/* A list going on where a task it called came back to it, frames after the
 * call, from under the frame the list keeps under each call (its data bank,
 * the slot, P and D), which the entries pull. */
enum { SCENE_TASK_CALL_FRAME = 6 };
void scene_frame_tasks_go_on(Snes *s);
void scene_vblank_tasks_go_on(Snes *s);
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
void scene_title_set_up_entry(Snes *s);
/* The setups as the program's dispatch runs them (scene/program.c): each
 * leaves the registers as its routine returns them, and is true unless a
 * call-out of its stopped. The title's then waits for its song, in the
 * original's loop. */
bool scene_title_set_up_scene(Snes *s);
/* The title's setup as C, for a program with no machine under it (the song
 * started, not waited for); false where a step is the machine's yet. */
bool scene_title_set_up(GameState *g, Platform *pf);
void scene_title_wait_for_its_song(Snes *s);
bool scene_race_set_up_scene(Snes *s);
bool scene_ending_set_up_scene(Snes *s);
bool scene_staff_roll_set_up_scene(Snes *s);
bool scene_credit_pages_set_up_scene(Snes *s);
/* The credits' page's setup as C, the letters unpacked by the unpacker as C. */
bool scene_credit_pages_set_up(GameState *g, Platform *pf);
bool scene_palace_set_up_scene(Snes *s);
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
bool scene_ending_expand_wide_tile_present(Snes *s);
void scene_ending_expand_wide_tile(Snes *s);
bool scene_title_ripple_columns_present(Snes *s);
void scene_expand_the_plane_tiles(Snes *s);
bool scene_expand_the_plane_tiles_present(Snes *s);
void scene_title_ripple_columns(Snes *s);
void scene_title_calm_the_ripple(Snes *s);
bool scene_title_calm_the_ripple_present(Snes *s);
bool scene_title_upload_ripple_present(Snes *s);
void scene_title_upload_ripple(Snes *s);

/* scene/race.c (the program in RAM) */
bool scene_race_set_up_present(Snes *s);
/* The race's setup as C (scene/program.c's setups with no machine). */
bool scene_race_set_up(GameState *g, Platform *pf);
void scene_race_set_up_entry(Snes *s);
bool scene_race_sweep_curtain_present(Snes *s);
void scene_race_sweep_curtain_entry(Snes *s);
TaskEnd scene_race_sweep_curtain(GameState *g, Platform *pf, unsigned slot);
bool scene_race_move_camera_present(Snes *s);
void scene_race_move_camera_entry(Snes *s);
TaskEnd scene_race_move_camera(GameState *g, Platform *pf, unsigned slot);
bool scene_race_upload_column_present(Snes *s);
void scene_race_upload_column_rom_entry(Snes *s);
TaskEnd scene_race_upload_column_rom(GameState *g, Platform *pf, unsigned slot);
void scene_race_upload_column_ram_entry(Snes *s);
TaskEnd scene_race_upload_column_ram(GameState *g, Platform *pf, unsigned slot);
bool scene_race_upload_tilemap_present(Snes *s);
void scene_race_upload_tilemap_entry(Snes *s);
TaskEnd scene_race_upload_tilemap(GameState *g, Platform *pf, unsigned slot);
bool scene_race_upload_strip_present(Snes *s);
void scene_race_upload_strip(Snes *s);
bool scene_race_upload_plane_present(Snes *s);
void scene_race_upload_plane(Snes *s);
bool scene_race_wait_for_tilemap_upload_present(Snes *s);
void scene_race_wait_for_tilemap_upload_loop(Snes *s);

/* scene/perspective.c (the program in RAM): the entry the endings jump to,
 * and for the race's camera check, whether the port builds the perspective
 * for the channel at a page and what the original leaves in the registers. */
bool scene_build_perspective_present(Snes *s);
void scene_build_perspective_entry(Snes *s);
bool scene_perspective_as_ported(Snes *s, uint16_t d);
void scene_leave_perspective_registers(Snes *s, SceneChannel ch);
/* Its state half, which the C path checks: the lines are ones the port builds. */
bool scene_perspective_fits(const GameState *g, SceneChannel ch);

/* scene/bridge.c: a scene's song, from the program's routine that starts one,
 * called from a setup at `call`. */
void scene_bridge_start_song(Snes *s, uint32_t call, uint8_t song);
/* The song started, as the program's call of the sound driver starts it. */
void scene_start_song(Platform *pf, uint8_t song);
/* scene/bridge.c: the frame a call from `call` leaves on the stack, for the
 * calls a port makes as the routine the call reached would. */
void scene_bridge_enter(Snes *s, uint32_t call);
/* scene/bridge.c: the stars scattered as a setup has them scattered, with
 * what the setup's last call left in A (`left_in_a`) and the carry. */
void scene_bridge_scatter_stars(Snes *s, uint16_t left_in_a);
void scene_bridge_leave(Snes *s);
/* scene/bridge.c: the program's unpacker ($7E:3691) from the JSR at `site`:
 * the resource's place in the program's list in X, unpacked into the
 * destination set; it reads X and the overflow, the data bank and the direct
 * page, which it hands back. And a script's first step ($7E:3749), from the
 * JSR at `site`, with 16-bit A and the bank the setup goes on with kept under
 * it (most keep their own data bank; the staff roll's 0), which reads Y (`y`,
 * as the setup's last load left it) and the overflow, as the setup leaves it;
 * the setups then go back on the scene's page with 8-bit A, all but the
 * Epoch's dive's, which goes on in 16-bit A. */
enum { SCENE_UNPACK = 0x7E3691, SCENE_UNPACK_IGNORES = DC_A | DC_Y | DC_N | DC_Z | DC_C };
enum { SCENE_FIRST_STEP = 0x7E3749, SCENE_FIRST_STEP_IGNORES = DC_A | DC_X | DC_N | DC_Z | DC_C };
void scene_bridge_unpack(Snes *s, uint32_t site, uint16_t resource);
void scene_bridge_take_the_first_script_step(Snes *s, uint32_t site, uint16_t y, uint8_t kept_bank);
void scene_bridge_back_on_the_scene_s_page(Snes *s);
/* Both, keeping the setup's own data bank. */
void scene_bridge_first_script_step(Snes *s, uint32_t site, uint16_t y);

/* What the program's palette made current ($7E:38AF) and the palette
 * animations' stop ($FD:FFEE) leave in Y, which a script's first step and the
 * driver's call after them read: past the working palette's copy, and past
 * the animations' last slot (scaffolding). */
enum { SCENE_Y_PAST_THE_WORKING_PALETTE = 0x2400, SCENE_Y_PAST_THE_ANIMATIONS_SLOTS = 0x0090 };
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


/* scene/program.c: the scene program as C, for a program with no machine
 * under it: booted with its flags, started, set up, its frames run until a
 * script ends it, and ended, the game going back to the field's start; false
 * where a step is the machine's yet, the program going no further there
 * (core/program.h). And the program's end's display stopped (screen.c). */
bool scene_runs(GameState *g, Platform *pf, uint8_t flags);
void scene_stops_the_display(Platform *pf);

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
/* The step as a scene's C path takes it, false where only the machine
 * sweeps its table. */
bool scene_curtain_stepped(GameState *g, Platform *pf, uint16_t salt);
void scene_curtain_jump(Snes *s);
bool scene_sweep_the_curtain_present(Snes *s);
bool scene_program_end_present(Snes *s);
/* The game back to the field's start on the game's fresh stack, as the
 * program ends. */
void scene_hand_back_to_the_field(Snes *s);
/* scene/credit_pages.c (the program in RAM) */
bool scene_credit_pages_set_up_present(Snes *s);
void scene_credit_pages_set_up_entry(Snes *s);
bool scene_show_the_credits_page_present(Snes *s);
void scene_show_the_credits_page(Snes *s);
/* scene/planet.c (the program in RAM) */
/* Colours that step toward others a step at a time, by the planet's step's
 * task: `colours` colours of the palette at `palette` (the program's bank),
 * toward those at `toward`, over `steps` steps. */
typedef struct {
    uint16_t toward, palette, colours;
    uint8_t steps;
} SceneColourStep;
void scene_step_the_colours_toward(GameState *g, SceneColourStep step);
bool scene_planet_set_up_scene(Snes *s);
bool scene_planet_set_up_present(Snes *s);
void scene_planet_set_up_entry(Snes *s);
/* As C, for a program with no machine (scene/program.c). */
bool scene_planet_set_up(GameState *g, Platform *pf);
void scene_fade_the_planet_through(Snes *s);
void scene_fade_the_planet_out(Snes *s);
void scene_fade_the_planet_out_slowly(Snes *s);
bool scene_fade_the_planet_through_present(Snes *s);
bool scene_fade_the_planet_out_present(Snes *s);
bool scene_fade_the_planet_out_slowly_present(Snes *s);
void scene_show_the_planet_s_caption_entry(Snes *s);
bool scene_planet_s_caption_present(Snes *s);
void scene_wait_for_a_button_to_restart_entry(Snes *s);
bool scene_wait_for_a_button_to_restart_present(Snes *s);
void scene_count_down_to_the_restart_entry(Snes *s);
/* The planet's end's tasks as C (scene/frame.c's table): the caption, the
 * wait for a button and the countdown to the console's reset. */
TaskEnd scene_show_the_planet_s_caption(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_wait_for_a_button_to_restart(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_count_down_to_the_restart(GameState *g, Platform *pf, unsigned slot);
bool scene_count_down_to_the_restart_present(Snes *s);
void scene_step_the_planet_s_colours(Snes *s);
bool scene_step_the_planet_s_colours_present(Snes *s);
void scene_twinkle_the_stars(Snes *s);
bool scene_twinkle_the_stars_present(Snes *s);
void scene_put_out_the_stars(Snes *s);
bool scene_put_out_the_stars_present(Snes *s);

/* scene/rising_palace.c (the program in RAM) */
bool scene_palace_set_up_present(Snes *s);
void scene_palace_set_up_entry(Snes *s);
/* The setup as C (scene/program.c's setups with no machine). */
bool scene_palace_set_up(GameState *g, Platform *pf);
void scene_send_the_palace_offsets(Snes *s);
bool scene_send_the_palace_offsets_present(Snes *s);
void scene_sweep_the_palace_curtain_entry(Snes *s);
TaskEnd scene_sweep_the_palace_curtain(GameState *g, Platform *pf, unsigned slot);
bool scene_sweep_the_palace_curtain_present(Snes *s);
void scene_ripple_the_palace(Snes *s);
bool scene_ripple_the_palace_present(Snes *s);
void scene_shimmer_the_palace(Snes *s);
bool scene_shimmer_the_palace_present(Snes *s);
void scene_darken_the_palace(Snes *s);
bool scene_darken_the_palace_present(Snes *s);
void scene_whiten_the_palace(Snes *s);
bool scene_whiten_the_palace_present(Snes *s);
void scene_clear_the_palace(Snes *s);
bool scene_clear_the_palace_present(Snes *s);
void scene_work_out_the_turn(Snes *s);
bool scene_work_out_the_turn_present(Snes *s);
void scene_send_the_map(Snes *s);
bool scene_send_the_map_present(Snes *s);
/* scene/staff_roll.c (the program in RAM) */
bool scene_staff_roll_set_up_present(Snes *s);
void scene_staff_roll_set_up_entry(Snes *s);
bool scene_staff_roll_set_up(GameState *g, Platform *pf);
bool scene_credits_roll_present(Snes *s);
void scene_roll_credits_entry(Snes *s);
bool scene_credits_scroll_present(Snes *s);
void scene_scroll_credits_entry(Snes *s);
bool scene_credits_upload_present(Snes *s);
void scene_upload_credits_entry(Snes *s);
bool scene_clear_sprite_high_bits_present(Snes *s);
void scene_clear_sprite_high_bits_entry(Snes *s);
TaskEnd scene_roll_credits(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_upload_credits(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_clear_sprite_high_bits(GameState *g, Platform *pf, unsigned slot);
bool scene_credits_test_present(Snes *s);
void scene_go_on_after_the_credits(Snes *s);
bool scene_fade_in_the_picture_present(Snes *s);
void scene_fade_in_the_picture(Snes *s);
/* scene/endings.c: the captions' letters, unpacked as 2-bit tiles, sent to
 * VRAM as 4-bit ones (the upper two planes empty), where the VRAM port is;
 * and the captions' colours, in the last sprite palette, with black behind
 * everything. The endings' and the planet's setups load both. */
void scene_send_letters_widened(GameState *g, Platform *pf);
void scene_set_caption_colours(GameState *g);

/* scene/endings.c (the program in RAM) */
bool scene_ending_set_up_present(Snes *s);
void scene_ending_set_up_entry(Snes *s);
/* The endings' tasks and the main loop's scaling of their picture as C, for a
 * program with no machine (each task a column of scene/frame.c's table). */
TaskEnd scene_ending_place_picture(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_ending_pick_picture_frame(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_ending_upload_picture(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_ending_show_credits(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_ending_show_last_scene(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_ending_scroll_bg2(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_send_the_credits_picture(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_send_the_pattern_s_map(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_send_the_pattern_s_tiles(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_blank_the_map(GameState *g, Platform *pf, unsigned slot);
void scene_send_the_credits_picture_entry(Snes *s);
void scene_send_the_pattern_s_map_entry(Snes *s);
void scene_send_the_pattern_s_tiles_entry(Snes *s);
void scene_blank_the_map_entry(Snes *s);
bool scene_send_the_credits_picture_present(Snes *s);
bool scene_send_the_pattern_s_map_present(Snes *s);
bool scene_send_the_pattern_s_tiles_present(Snes *s);
bool scene_blank_the_map_present(Snes *s);
TaskEnd scene_ending_draw_the_end(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_ending_sweep_window(GameState *g, Platform *pf, unsigned slot);
bool scene_ending_scale_picture(GameState *g, Platform *pf);
bool scene_ending_set_up(GameState *g, Platform *pf);
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
/* The scripts' other routines' entries and guards (scene_ending_routines). */
void scene_stop_the_picture(Snes *s);
bool scene_stop_the_picture_present(Snes *s);
void scene_step_the_ground_toward_the_first(Snes *s);
void scene_step_the_ground_toward_the_second(Snes *s);
void scene_step_the_ground_toward_the_third(Snes *s);
bool scene_step_the_ground_s_colors_present(Snes *s);
void scene_show_the_ground_s_first_later_colors(Snes *s);
bool scene_show_the_ground_s_colors_present(Snes *s);
void scene_place_the_captions(Snes *s);
bool scene_place_the_captions_present(Snes *s);
void scene_fade_the_sprites_out(Snes *s);
bool scene_fade_the_sprites_out_present(Snes *s);
void scene_fade_the_first_layer_colors_out(Snes *s);
bool scene_fade_the_first_layer_colors_out_present(Snes *s);
void scene_fade_more_layer_colors_out(Snes *s);
bool scene_fade_more_layer_colors_out_present(Snes *s);
bool scene_ending_show_credits_present(Snes *s);
void scene_ending_show_credits_entry(Snes *s);
bool scene_ending_show_last_scene_present(Snes *s);
void scene_ending_show_last_scene_entry(Snes *s);
bool scene_ending_sweep_window_present(Snes *s);
void scene_ending_sweep_window_entry(Snes *s);
bool scene_ending_scroll_bg2_present(Snes *s);
void scene_ending_scroll_bg2_entry(Snes *s);
bool scene_ending_upload_picture_present(Snes *s);
void scene_ending_upload_picture_entry(Snes *s);
bool scene_ending_place_picture_present(Snes *s);
void scene_ending_place_picture_entry(Snes *s);
bool scene_ending_pick_picture_frame_present(Snes *s);
void scene_ending_pick_picture_frame_entry(Snes *s);
bool scene_ending_scale_picture_present(Snes *s);
void scene_ending_scale_picture_entry(Snes *s);
bool scene_ending_gather_picture_present(Snes *s);
void scene_ending_gather_picture(Snes *s);
/* scene/bridge.c: where a scene's scaling of its picture goes on in the
 * original's scaler: the shared endings' or the Epoch's dive's. */
typedef enum { SCALING_THE_ENDING_PICTURE, SCALING_THE_EPOCH } ScenePictureScaling;
void scene_bridge_scale_scene_picture(Snes *s, ScenePictureScaling which);

/* scene/lavos_falls.c */
bool scene_lavos_falls_set_up_scene(Snes *s);
void scene_lavos_falls_set_up_entry(Snes *s);
bool scene_lavos_falls_set_up(GameState *g, Platform *pf);
bool scene_lavos_falls_set_up_present(Snes *s);
void scene_turn_lavos_entry(Snes *s);
TaskEnd scene_turn_lavos(GameState *g, Platform *pf, unsigned slot);
bool scene_turn_lavos_present(Snes *s);
void scene_turn_the_field_s_angle_entry(Snes *s);
TaskEnd scene_turn_the_field_s_angle(GameState *g, Platform *pf, unsigned slot);
bool scene_turn_the_field_s_angle_present(Snes *s);
void scene_swing_the_field_entry(Snes *s);
TaskEnd scene_swing_the_field(GameState *g, Platform *pf, unsigned slot);
bool scene_swing_the_field_present(Snes *s);
void scene_draw_the_planet_entry(Snes *s);
TaskEnd scene_draw_the_planet(GameState *g, Platform *pf, unsigned slot);
bool scene_draw_the_planet_present(Snes *s);
void scene_send_lavos_back(Snes *s);
bool scene_send_lavos_back_present(Snes *s);
void scene_let_lavos_fall(Snes *s);
bool scene_let_lavos_fall_present(Snes *s);

/* scene/epoch_dive.c: the setup, the frame's tasks and the main loop's
 * scaling as C (each task a column of scene/frame.c's table), and their
 * machine entries. */
bool scene_dive_set_up(GameState *g, Platform *pf);
TaskEnd scene_pass_the_shell_across(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_pass_the_shell_down(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_let_the_shell_fall_away(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_check_the_shell(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_send_the_epoch(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_draw_the_epoch(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_size_the_trail(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_draw_the_trail(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_fly_the_epoch_on(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_shake_the_cockpit(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_flicker_the_lights(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_follow_the_panel(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_open_the_light(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_set_the_screen_outside(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_set_the_screen_in_the_cockpit(GameState *g, Platform *pf, unsigned slot);
bool scene_scale_the_epoch(GameState *g, Platform *pf);
bool scene_dive_set_up_scene(Snes *s);
void scene_dive_set_up_entry(Snes *s);
bool scene_dive_set_up_present(Snes *s);
void scene_pass_the_shell_across_entry(Snes *s);
bool scene_pass_the_shell_across_present(Snes *s);
void scene_pass_the_shell_down_entry(Snes *s);
bool scene_pass_the_shell_down_present(Snes *s);
void scene_let_the_shell_fall_away_entry(Snes *s);
bool scene_let_the_shell_fall_away_present(Snes *s);
void scene_check_the_shell_entry(Snes *s);
bool scene_check_the_shell_present(Snes *s);
void scene_scale_the_epoch_entry(Snes *s);
bool scene_scale_the_epoch_present(Snes *s);
void scene_gather_the_epoch(Snes *s);
bool scene_gather_the_epoch_present(Snes *s);
void scene_send_the_epoch_entry(Snes *s);
bool scene_send_the_epoch_present(Snes *s);
void scene_draw_the_epoch_entry(Snes *s);
bool scene_draw_the_epoch_present(Snes *s);
void scene_size_the_trail_entry(Snes *s);
bool scene_size_the_trail_present(Snes *s);
void scene_draw_the_trail_entry(Snes *s);
bool scene_draw_the_trail_present(Snes *s);
void scene_fly_the_epoch_on_entry(Snes *s);
bool scene_fly_the_epoch_on_present(Snes *s);
void scene_shake_the_cockpit_entry(Snes *s);
bool scene_shake_the_cockpit_present(Snes *s);
void scene_flicker_the_lights_entry(Snes *s);
bool scene_flicker_the_lights_present(Snes *s);
void scene_follow_the_panel_entry(Snes *s);
bool scene_follow_the_panel_present(Snes *s);
void scene_open_the_light_entry(Snes *s);
bool scene_open_the_light_present(Snes *s);
void scene_set_the_screen_outside_entry(Snes *s);
bool scene_set_the_screen_outside_present(Snes *s);
void scene_set_the_screen_in_the_cockpit_entry(Snes *s);
bool scene_set_the_screen_in_the_cockpit_present(Snes *s);
void scene_look_from_outside(Snes *s);
bool scene_look_from_outside_present(Snes *s);
void scene_look_from_the_cockpit(Snes *s);
bool scene_look_from_the_cockpit_present(Snes *s);
void scene_fade_the_shell_in(Snes *s);
bool scene_fade_the_shell_in_present(Snes *s);
void scene_white_out(Snes *s);
bool scene_white_out_present(Snes *s);

/* scene/plane.c */
void scene_send_the_flat_plane_entry(Snes *s);
bool scene_send_the_flat_plane_present(Snes *s);
void scene_send_the_plane_s_place_entry(Snes *s);
bool scene_send_the_plane_s_place_present(Snes *s);

/* scene/endings.c: the zooming picture's frame for a turn of its animation
 * (1024 steps a turn, a frame each 32nd). */
void scene_ending_show_picture_frame(GameState *g, uint16_t turn);
bool scene_ending_draw_the_end_present(Snes *s);
void scene_ending_draw_the_end_entry(Snes *s);

/* scene/beyond_time.c (the program in RAM): the fireworks placed and their
 * first bursts picked (the shared setup's), and the night's tasks. */
void scene_set_the_fireworks_up(GameState *g);
void scene_set_the_fireworks_up_entry(Snes *s);
bool scene_set_the_fireworks_up_present(Snes *s);
void scene_keep_the_orbit_entry(Snes *s);
bool scene_keep_the_orbit_present(Snes *s);
void scene_pass_the_map_entry(Snes *s);
bool scene_pass_the_map_present(Snes *s);
void scene_check_the_flight_view_entry(Snes *s);
bool scene_check_the_flight_view_present(Snes *s);
void scene_place_the_flight_view_entry(Snes *s);
bool scene_place_the_flight_view_present(Snes *s);
void scene_send_the_flight_view_entry(Snes *s);
bool scene_send_the_flight_view_present(Snes *s);
void scene_set_the_flight_horizon_entry(Snes *s);
bool scene_set_the_flight_horizon_present(Snes *s);
void scene_circle_the_epoch_entry(Snes *s);
bool scene_circle_the_epoch_present(Snes *s);
void scene_sway_the_epoch_entry(Snes *s);
bool scene_sway_the_epoch_present(Snes *s);
void scene_twinkle_the_lights_entry(Snes *s);
bool scene_twinkle_the_lights_present(Snes *s);
void scene_turn_back_and_twinkle_entry(Snes *s);
bool scene_turn_back_and_twinkle_present(Snes *s);
void scene_burst_the_fireworks_entry(Snes *s);
bool scene_burst_the_fireworks_present(Snes *s);
void scene_send_the_picture_entry(Snes *s);
bool scene_send_the_picture_present(Snes *s);
void scene_send_the_picture_and_bg2_entry(Snes *s);
bool scene_send_the_picture_and_bg2_present(Snes *s);
/* The night's tasks as C, for a program with no machine. */
TaskEnd scene_keep_the_orbit(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_pass_the_map(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_check_the_flight_view(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_place_the_flight_view(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_send_the_flight_view(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_set_the_flight_horizon(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_circle_the_epoch(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_sway_the_epoch(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_twinkle_the_lights(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_turn_back_and_twinkle(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_burst_the_fireworks(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_send_the_picture(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_send_the_picture_and_bg2(GameState *g, Platform *pf, unsigned slot);

/* scene/stars.c (the program in RAM) */
bool scene_stars_scatter_code_present(Snes *s);
bool scene_stars_scatter_present(Snes *s);
void scene_stars_scatter(Snes *s);
bool scene_stars_fly_present(Snes *s);
void scene_stars_fly_entry(Snes *s);
TaskEnd scene_stars_fly(GameState *g, Platform *pf, unsigned slot);
bool scene_stars_fly_after_captions_present(Snes *s);
void scene_stars_fly_after_captions_entry(Snes *s);
bool scene_stars_drift_present(Snes *s);
bool scene_stars_start_drift_present(Snes *s);
void scene_stars_start_drift_entry(Snes *s);
void scene_stars_keep_drifting_entry(Snes *s);
TaskEnd scene_stars_fly_after_captions(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_stars_start_drift(GameState *g, Platform *pf, unsigned slot);
TaskEnd scene_stars_keep_drifting(GameState *g, Platform *pf, unsigned slot);

/* scene/captions.c (the program in RAM) */
bool scene_draw_caption_present(Snes *s);
void scene_draw_caption_entry(Snes *s);
/* A caption drawn from its text, as C, where its guard's state half takes
 * it: its text ends inside the program's bank and its letters fit the
 * sprite table. */
bool scene_caption_fits(const GameState *g, uint16_t text);
void scene_draw_caption(GameState *g, uint16_t text);
bool scene_turn_captions_present(Snes *s);
void scene_turn_captions_entry(Snes *s);
TaskEnd scene_turn_captions(GameState *g, Platform *pf, unsigned slot);

/* scene/race_matrix.c */
void scene_write_matrix_code(Snes *s);
bool scene_matrix_code_present(Snes *s);
void scene_gather_picture(Snes *s);
bool scene_race_scale_pictures_present(Snes *s);
void scene_race_scale_pictures_entry(Snes *s);
bool scene_race_scale_pictures(GameState *g, Platform *pf);
bool scene_race_scale_after_present(Snes *s);
void scene_race_scale_after_crono(Snes *s);
void scene_race_scale_after_johnny(Snes *s);
bool scene_race_gather_scaled_present(Snes *s);
void scene_race_gather_scaled(Snes *s);

/* scene/race_rules.c (the program in RAM) */
bool scene_race_ride_present(Snes *s);
bool scene_race_pitch_and_move_present(Snes *s);
void scene_race_pitch_and_move(Snes *s);
bool scene_race_ride_lists_fit(Snes *s);
void scene_race_ride_entry(Snes *s);
TaskEnd scene_race_ride(GameState *g, Platform *pf, unsigned slot);
bool scene_race_coast_present(Snes *s);
bool scene_race_coast_lists_fit(Snes *s);
void scene_race_coast_entry(Snes *s);
TaskEnd scene_race_coast(GameState *g, Platform *pf, unsigned slot);
bool scene_race_await_victory_song_present(Snes *s);
void scene_race_await_victory_song_entry(Snes *s);
TaskEnd scene_race_await_victory_song(GameState *g, Platform *pf, unsigned slot);
bool scene_race_wait_to_leave_present(Snes *s);
void scene_race_wait_to_leave_entry(Snes *s);
TaskEnd scene_race_wait_to_leave(GameState *g, Platform *pf, unsigned slot);
bool scene_race_leave_present(Snes *s);
void scene_race_leave_entry(Snes *s);
TaskEnd scene_race_leave(GameState *g, Platform *pf, unsigned slot);
bool scene_race_drain_score_present(Snes *s);
void scene_race_drain_score_entry(Snes *s);
TaskEnd scene_race_drain_score(GameState *g, Platform *pf, unsigned slot);

/* scene/race_projection.c (the program in RAM) */
bool scene_race_place_bikes_present(Snes *s);
void scene_race_place_bikes_entry(Snes *s);
TaskEnd scene_race_place_bikes(GameState *g, Platform *pf, unsigned slot);

/* scene/race_drawing.c (the program in RAM) */
bool scene_race_draw_present(Snes *s);
void scene_race_draw_entry(Snes *s);
TaskEnd scene_race_draw(GameState *g, Platform *pf, unsigned slot);

/* scene/race_camera.c (the program in RAM) */
bool scene_race_settle_camera_present(Snes *s);
void scene_race_settle_camera_entry(Snes *s);
TaskEnd scene_race_settle_camera(GameState *g, Platform *pf, unsigned slot);
bool scene_race_move_along_present(Snes *s);
void scene_race_move_along_entry(Snes *s);
TaskEnd scene_race_move_along(GameState *g, Platform *pf, unsigned slot);
bool scene_race_keep_controller_present(Snes *s);
void scene_race_keep_controller_entry(Snes *s);
TaskEnd scene_race_keep_controller(GameState *g, Platform *pf, unsigned slot);
bool scene_race_check_perspective_present(Snes *s);
void scene_race_check_perspective_entry(Snes *s);
TaskEnd scene_race_check_perspective(GameState *g, Platform *pf, unsigned slot);
bool scene_race_turn_camera_present(Snes *s);
void scene_race_turn_camera_entry(Snes *s);
TaskEnd scene_race_turn_camera(GameState *g, Platform *pf, unsigned slot);
bool scene_race_aim_camera_present(Snes *s);
void scene_race_aim_camera_entry(Snes *s);
TaskEnd scene_race_aim_camera(GameState *g, Platform *pf, unsigned slot);
bool scene_race_follow_finish_present(Snes *s);
void scene_race_follow_finish_entry(Snes *s);
TaskEnd scene_race_follow_finish(GameState *g, Platform *pf, unsigned slot);
bool scene_race_set_horizon_present(Snes *s);
void scene_race_set_horizon_entry(Snes *s);
TaskEnd scene_race_set_horizon(GameState *g, Platform *pf, unsigned slot);
bool scene_race_pick_opening_present(Snes *s);
void scene_race_pick_opening(Snes *s);

/* scene/sound.c (the program in RAM) */
void scene_sound_back_to_normal_entry(Snes *s);
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
/* The program's own list of large sprites ($7E:A03D), as a C call and as its
 * entry: every sprite kept (the fireworks' bursts), or those at the picture's
 * bottom or below left out, the form the callers write into its code. */
typedef enum { LIST_KEEPS_EVERY_SPRITE, LIST_LEAVES_OUT_BELOW_THE_PICTURE } SceneLargeList;
void scene_draw_large_sprite_list(GameState *g, uint16_t list, SceneLargeList form);
void scene_draw_large_sprite_list_entry(Snes *s);
bool scene_large_sprite_list_fits(Snes *s);
/* The program's list with signed offsets ($7E:3A97), each sprite's size in its
 * attributes, as a C call and as its entry: the endings' rider of the swept
 * window. */
void scene_draw_signed_sprite_list(GameState *g, uint16_t list);
void scene_draw_signed_sprite_list_entry(Snes *s);
bool scene_signed_sprite_list_fits(Snes *s);
bool scene_signed_sprite_list_code_present(Snes *s);
/* What the signed list leaves in A, for the ports' wrappers. */
uint16_t scene_signed_list_leaves_a(const GameState *g, uint16_t list);

/* scene/circle.c: whether the iris's table keeps clear of the cells its
 * original reads back (the guard's state half), and the guard. */
bool scene_circle_table_clear_of_the_cells(const GameState *g);
bool scene_circle_table_is_clear(Snes *s);
void scene_draw_circle(Snes *s);

/* scene/scaler.c: a bitmap handed to the scaler, which draws it from the
 * start of its output: `rows` rows of `pairs` pairs of 8-pixel groups from
 * the bitmap at `source`, each row's source `row_step` 128ths of a row on
 * from the last and its samples twice that apart, until the rows run out or
 * the row position reaches `limit`. */
typedef struct {
    uint16_t source, rows, pairs, row_step, limit;
} SceneScaling;
void scene_scaler_start(GameState *g, SceneScaling bitmap);
/* Whether the port draws the bitmap handed over (the scaler's guard's state
 * half); and the whole of it drawn at once, its first row nudged by
 * `row_nudge`, for a program with no machine (false where the port draws a
 * row no further, having asked for the machine). */
enum { SCENE_SCALER_ROWS = 0xC30309 };
bool scene_scaler_draws(const GameState *g);
bool scene_scale_the_bitmap(GameState *g, bool row_nudge);
bool scene_scaler_in_its_bank(Snes *s);
void scene_scale_bitmap_row(Snes *s);
void scene_scale_bitmap(Snes *s);

/* scene/random.c: the draw as the program's code calls it, through its own
 * call ($7E:3443) or the stir's ($C3:02DD). */
void scene_draw_random_entry(Snes *s);
bool scene_draw_random_present(Snes *s);
void scene_program_draw_random(Snes *s);
bool scene_program_draw_random_present(Snes *s);

/* scene/math.c */
bool scene_divide_present(Snes *s);
void scene_divide(Snes *s);
bool scene_divide_8_8_present(Snes *s);
void scene_divide_8_8(Snes *s);
bool scene_multiply_present(Snes *s);
void scene_multiply(Snes *s);
void scene_multiply_signed(Snes *s);
bool scene_multiply_signed16_present(Snes *s);
void scene_multiply_signed16_entry(Snes *s);
bool scene_sine_present(Snes *s);
void scene_sine_entry(Snes *s);
void scene_cosine_entry(Snes *s);
void scene_find_divisor(Snes *s);
bool scene_find_divisor_on_divider_page(Snes *s);

#endif
