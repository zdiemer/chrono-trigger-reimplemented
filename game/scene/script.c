/* Scenes: the sequence scripts.
 *
 * A scene program runs two scripts at once, on its two sequence channels
 * (scene_channels): the title's, the attract's and the endings' timing and
 * choreography -- when a picture fades, which routine of the program runs,
 * what the sound driver plays. Each frame the program runs each channel's script
 * from where it stopped until a command ends the channel's turn. A channel has
 * sixteen word registers, sixteen loop counters and a return address, and its
 * routines work on its cells, so a script's registers are also the variables of
 * the routines it calls (the title keeps BG1's scroll in channel 0's).
 *
 * A command is an opcode byte and its operands, in the program's bank:
 *
 *   $00-$7F  set or add to a register (the low nibble): a value, or another
 *            register's (bit 5); set for $00 and $20, add for the rest
 *   $80-$9F  count loop counter n (the low nibble) up to a limit, jumping back
 *            until it gets there; $9n also ends the turn
 *   $A0-$FF  by the low nibble: call a routine of the program; store a byte or
 *            a word in low RAM; call a part of the script and return from it;
 *            wait for the music; send the sound driver a command; put a task on
 *            either of the frame's lists or take one off; a video setting; end
 *            the turn; jump (ending the turn, or not); and stop, for good
 *
 * The language is a table: each row says which opcodes it covers and runs
 * them, decoding the command's operands into the named parameters of an
 * operation over the channel. Two operations reach outside the script -- a
 * routine of the program, and a command to the sound driver -- and the turn's
 * loop makes those calls. A command the port doesn't take (one no script has:
 * a store outside low RAM, a video setting no call expresses, a task to take
 * off that isn't on its list or to put on a list that is full) stops the scene
 * there (program_fail). A script that never ends its turn hangs the game, in
 * the port as in the original. */
#include "scene/scene.h"

#include "charge.h"
#include "core/program.h"
#include "platform/audio.h"
#include "platform/video.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* The scripts are in the program's bank, which is also the bank the store
 * commands write to. */
enum { SCRIPT_BANK = SCENE_LOWER_WORK_RAM };

/* Low RAM, which the store commands write: past it lie the console's ports
 * and the cartridge. */
enum { LOW_RAM_END = 0x2000 };

/* ---- The channel ------------------------------------------------------------------------------ */

static uint16_t script(const GameState *g, SceneChannel ch) { return scene_channels_script(g, ch); }
static void set_script(GameState *g, SceneChannel ch, uint16_t at) { set_scene_channels_script(g, ch, at); }

uint16_t scene_channel_register(const GameState *g, SceneChannel ch, unsigned n)
{
    return (uint16_t)(scene_channels_registers_byte(g, ch, 2 * n) | scene_channels_registers_byte(g, ch, 2 * n + 1) << 8);
}

void set_scene_channel_register(GameState *g, SceneChannel ch, unsigned n, uint16_t v)
{
    set_scene_channels_registers_byte(g, ch, 2 * n, (uint8_t)v);
    set_scene_channels_registers_byte(g, ch, 2 * n + 1, (uint8_t)(v >> 8));
}

static uint16_t loop_counter(const GameState *g, SceneChannel ch, unsigned n)
{
    return (uint16_t)(scene_channels_loop_counters_byte(g, ch, 2 * n) |
                      scene_channels_loop_counters_byte(g, ch, 2 * n + 1) << 8);
}

static void set_loop_counter(GameState *g, SceneChannel ch, unsigned n, uint16_t v)
{
    set_scene_channels_loop_counters_byte(g, ch, 2 * n, (uint8_t)v);
    set_scene_channels_loop_counters_byte(g, ch, 2 * n + 1, (uint8_t)(v >> 8));
}

/* The script's bytes, in the program. */
static uint8_t script_byte(const GameState *g, uint16_t at) { return wram_read8(g, (uint32_t)SCRIPT_BANK << 16 | at); }

static uint16_t script_word(const GameState *g, uint16_t at)
{
    return wram_read16(g, (uint32_t)SCRIPT_BANK << 16 | at);
}

/* ---- The operations -------------------------------------------------------------------------- */

/* How a command ends. */
typedef enum {
    GO_ON,          /* on to the next command */
    END_OF_TURN,    /* the interpreter returns */
    WAITING,        /* waiting for the music ($C3:0C2A) */
    ROUTINE_CALL,   /* the program's routine to call */
    SOUND_SEND,     /* the sound command buffer to send */
} ScriptFlowKind;

typedef struct {
    ScriptFlowKind kind;
    uint16_t routine;   /* ROUTINE_CALL */
} ScriptFlow;

static ScriptFlow go_on(void) { return (ScriptFlow){ GO_ON, 0 }; }
static ScriptFlow end_of_turn(void) { return (ScriptFlow){ END_OF_TURN, 0 }; }

static void assign(GameState *g, SceneChannel ch, unsigned reg, uint16_t value, bool add)
{
    uint16_t was = scene_channel_register(g, ch, reg);
    set_scene_channel_register(g, ch, reg, add ? (uint16_t)(was + value) : value);
}

/* A loop's counter reaches its limit when, counted up, it is at least the limit
 * -- compared by the sign of the difference, as the original compares it. A
 * counter at 0 has just started. */
static bool loop_done(uint16_t count, uint16_t limit) { return count != 0 && (int16_t)(count - limit) >= 0; }

/* Count a loop: short of its limit, count it up and go back to `back_to`; at
 * it, clear it and go on to `after`. */
static void count_loop(GameState *g, SceneChannel ch, unsigned counter, uint16_t limit, uint16_t back_to,
                       uint16_t after)
{
    uint16_t count = loop_counter(g, ch, counter);
    bool done = loop_done(count, limit);
    set_loop_counter(g, ch, counter, done ? 0 : (uint16_t)(count + 1));
    set_script(g, ch, done ? after : back_to);
}

/* Store a byte or a word in low RAM. A store can reach the script's own
 * pointer, which makes it a jump: the script goes on past the value from
 * wherever the store left the pointer. */
enum { VALUE_OPERAND = 3 };

static void store(GameState *g, SceneChannel ch, uint16_t on_the_value, uint16_t address, uint16_t value, bool word)
{
    set_script(g, ch, on_the_value);
    uint32_t to = (uint32_t)SCRIPT_BANK << 16 | address;   /* low RAM, which the program's bank starts with */
    if (word)
        wram_write16(g, to, value);
    else
        wram_write8(g, to, (uint8_t)value);
    set_script(g, ch, (uint16_t)(script(g, ch) + (word ? 2 : 1)));
}

/* A part of the script, which returns to `return_to`. */
static void gosub(GameState *g, SceneChannel ch, uint16_t target, uint16_t return_to)
{
    set_scene_channels_return_address(g, ch, return_to);
    set_script(g, ch, target);
}

static void return_from_gosub(GameState *g, SceneChannel ch) { set_script(g, ch, scene_channels_return_address(g, ch)); }

/* A sound for the driver: the command and its parameters into the sound
 * command buffer, and sent unless a song is on its way to the driver, which
 * takes one message at a time -- then the sound is lost, and the player doesn't
 * hear it. True if it is to be sent. */
static bool queue_sound(GameState *g, const uint8_t sound[4])
{
    set_sound_command(g, sound[0]);
    for (unsigned i = 0; i < 3; i++)
        set_sound_command_parameters(g, i, sound[1 + i]);
    return scene_song_sending(g) == 0;
}

/* The video settings a script makes. A script names each by the register the
 * original writes it to, a number the table below carries as the scripts'
 * encoding. */
typedef enum {
    MODE7_SETTINGS,      /* the plane's flips, and what lies outside it */
    OBJ_WINDOWS,         /* how the windows mask the sprites and the color window */
    WINDOW1_LEFT, WINDOW1_RIGHT,
    MAIN_SCREEN,         /* the layers on the main screen */
    SUB_SCREEN,
    MAIN_WINDOWS,        /* the layers the windows mask on the main screen */
    COLOR_MATH_LAYERS,
    VIDEO_CONFIG,
    NO_SETTING,
} ScriptVideoSetting;

static const struct {
    uint8_t named_by;
    ScriptVideoSetting setting;
} video_settings[] = {
    { 0x1A, MODE7_SETTINGS }, { 0x25, OBJ_WINDOWS }, { 0x26, WINDOW1_LEFT }, { 0x27, WINDOW1_RIGHT },
    { 0x2C, MAIN_SCREEN },    { 0x2D, SUB_SCREEN },  { 0x2E, MAIN_WINDOWS }, { 0x31, COLOR_MATH_LAYERS },
    { 0x33, VIDEO_CONFIG },
};

static ScriptVideoSetting video_setting_named(uint8_t named_by)
{
    for (unsigned i = 0; i < sizeof video_settings / sizeof video_settings[0]; i++)
        if (video_settings[i].named_by == named_by)
            return video_settings[i].setting;
    return NO_SETTING;
}

/* A mode 7 setting a VidMode7 can carry: flips and what lies outside. */
enum { MODE7_SETTINGS_UNUSED = 0x3C };

static bool video_setting_expressed(ScriptVideoSetting setting, uint8_t value)
{
    return setting != NO_SETTING && !(setting == MODE7_SETTINGS && (value & MODE7_SETTINGS_UNUSED));
}

/* The setting, in the video service's terms: the script's byte is the setting
 * as the game keeps it, which the service's decoders read. */
static void make_video_setting(Platform *pf, ScriptVideoSetting setting, uint8_t v)
{
    PfOrigin at = PF_AT(0xC30C75);
    VidWindows w = { 0 };
    VidColorMath math = { 0 };
    uint32_t windows;
    switch (setting) {
    case MODE7_SETTINGS:
        vid_set_mode7(pf, at, &(VidMode7){ .hflip = v & 1, .vflip = (v & 2) != 0, .outside = (uint8_t)(v >> 6) },
                      VID_M7_SELECT);
        return;
    case MAIN_SCREEN: vid_set_screens(pf, at, v, 0, VID_SCREEN_MAIN); return;
    case SUB_SCREEN: vid_set_screens(pf, at, 0, v, VID_SCREEN_SUB); return;
    case COLOR_MATH_LAYERS:
        vid_color_math_decode(&math, VID_MATH_LAYERS, v);
        vid_set_color_math(pf, at, &math, VID_MATH_LAYERS);
        return;
    case VIDEO_CONFIG: {
        VidVideoConfig config = vid_video_config_decode(v);
        vid_set_video_config(pf, at, &config);
        return;
    }
    case OBJ_WINDOWS: windows = VID_WINDOWS_OBJ_BACK; break;
    case WINDOW1_LEFT: windows = VID_WINDOWS_W1_LEFT; break;
    case WINDOW1_RIGHT: windows = VID_WINDOWS_W1_RIGHT; break;
    default: windows = VID_WINDOWS_MAIN; break;
    }
    vid_windows_decode(&w, windows, v);
    vid_set_windows(pf, at, &w, windows);
}

/* The script is over: it stays on its stop, and the program's main loop is told. */
static void stop(GameState *g, SceneChannel ch, uint16_t at)
{
    set_script(g, ch, at);
    set_scene_program_over(g, 1);
}

/* ---- The table --------------------------------------------------------------------------------- */

/* The command being run: where it is and its opcode, and whose script. */
typedef struct {
    GameState *g;
    Platform *pf;
    SceneChannel ch;
    uint16_t at;
    uint8_t opcode;
} Command;

static uint8_t operand8(const Command *c, unsigned n) { return script_byte(c->g, (uint16_t)(c->at + n)); }
static uint16_t operand16(const Command *c, unsigned n) { return script_word(c->g, (uint16_t)(c->at + n)); }
static unsigned low_nibble(const Command *c) { return c->opcode & 0x0F; }
static uint16_t after(const Command *c, unsigned length) { return (uint16_t)(c->at + length); }

struct ScriptRow;
typedef ScriptFlow (*ScriptRun)(const Command *c, const struct ScriptRow *row);
typedef bool (*ScriptTakes)(const Command *c, const struct ScriptRow *row);

/* Where an assignment's value is. */
typedef enum { VALUE_WORD, VALUE_REGISTER } ValueFrom;

/* What a row's commands are. */
typedef enum {
    KIND_SET, KIND_ADD, KIND_LOOP, KIND_CALL_ROUTINE, KIND_STORE, KIND_GOSUB, KIND_RETURN, KIND_WAIT_FOR_MUSIC,
    KIND_SOUND, KIND_TASK, KIND_VIDEO, KIND_END_TURN, KIND_JUMP, KIND_STOP,
} ScriptKind;

typedef struct ScriptRow {
    uint8_t mask, match;   /* the opcodes the row covers: those whose bits under the mask are `match` */
    ScriptKind kind;
    ScriptRun run;
    ScriptTakes takes;     /* what the port needs of the state to run it, or NULL */
    ValueFrom value;       /* assignments */
    bool add;              /* tasks: put on the list, not taken off */
    bool ends_turn;        /* loops, jumps */
    bool word;             /* stores */
    SceneTaskList list;    /* tasks */
    uint32_t entry;        /* the original's handler the command is timed as, or 0 */
} ScriptRow;

/* Across a call of a routine or the sound driver, the script stays on the
 * operand word the command read last -- a routine can move it -- and goes on
 * past that word from wherever the call left it. */
enum { ROUTINE_OPERAND = 1, SOUND_TAIL_OPERAND = 3 };

static ScriptFlow run_assign(const Command *c, const ScriptRow *row)
{
    bool from_word = row->value == VALUE_WORD;
    uint16_t value = from_word ? operand16(c, 1) : scene_channel_register(c->g, c->ch, (operand8(c, 1) & 0x1E) / 2);
    assign(c->g, c->ch, low_nibble(c), value, row->kind == KIND_ADD);
    set_script(c->g, c->ch, after(c, from_word ? 3 : 2));
    return go_on();
}

static ScriptFlow run_loop(const Command *c, const ScriptRow *row)
{
    count_loop(c->g, c->ch, low_nibble(c), operand16(c, 1), operand16(c, 3), after(c, 5));
    return row->ends_turn ? end_of_turn() : go_on();
}

static ScriptFlow run_call_routine(const Command *c, const ScriptRow *row)
{
    (void)row;
    set_script(c->g, c->ch, after(c, ROUTINE_OPERAND));
    return (ScriptFlow){ ROUTINE_CALL, operand16(c, 1) };
}

static bool store_takes(const Command *c, const ScriptRow *row)
{
    (void)row;
    return operand16(c, 1) < LOW_RAM_END;
}

static ScriptFlow run_store(const Command *c, const ScriptRow *row)
{
    uint16_t value = row->word ? operand16(c, 3) : operand8(c, 3);
    store(c->g, c->ch, after(c, VALUE_OPERAND), operand16(c, 1), value, row->word);
    return go_on();
}

static ScriptFlow run_gosub(const Command *c, const ScriptRow *row)
{
    (void)row;
    gosub(c->g, c->ch, operand16(c, 1), after(c, 3));
    return go_on();
}

static ScriptFlow run_return(const Command *c, const ScriptRow *row)
{
    (void)row;
    return_from_gosub(c->g, c->ch);
    return go_on();
}

/* The wait reads the level after the command. */
static ScriptFlow run_wait_for_music(const Command *c, const ScriptRow *row)
{
    (void)row;
    set_script(c->g, c->ch, after(c, 1));
    return (ScriptFlow){ WAITING, 0 };
}

static ScriptFlow run_sound(const Command *c, const ScriptRow *row)
{
    (void)row;
    uint8_t sound[4] = { operand8(c, 1), operand8(c, 2), operand8(c, 3), operand8(c, 4) };
    if (!queue_sound(c->g, sound)) {
        set_script(c->g, c->ch, after(c, 5));
        return go_on();
    }
    set_script(c->g, c->ch, after(c, SOUND_TAIL_OPERAND));
    return (ScriptFlow){ SOUND_SEND, 0 };
}

static bool task_takes(const Command *c, const ScriptRow *row)
{
    bool adding = row->add;
    return scene_script_can_replace_task(c->g, row->list, adding ? NO_TASK : operand16(c, 1));
}

static ScriptFlow run_task(const Command *c, const ScriptRow *row)
{
    SceneTask task = operand16(c, 1);
    if (row->add)
        (void)scene_script_replace_task(c->g, row->list, NO_TASK, task);
    else
        (void)scene_script_replace_task(c->g, row->list, task, NO_TASK);
    set_script(c->g, c->ch, after(c, 3));
    return go_on();
}

static bool video_takes(const Command *c, const ScriptRow *row)
{
    (void)row;
    return video_setting_expressed(video_setting_named(operand8(c, 2)), operand8(c, 1));
}

static ScriptFlow run_video(const Command *c, const ScriptRow *row)
{
    (void)row;
    make_video_setting(c->pf, video_setting_named(operand8(c, 2)), operand8(c, 1));
    set_script(c->g, c->ch, after(c, 3));
    return go_on();
}

static ScriptFlow run_end_turn(const Command *c, const ScriptRow *row)
{
    (void)row;
    set_script(c->g, c->ch, after(c, 1));
    return end_of_turn();
}

static ScriptFlow run_jump(const Command *c, const ScriptRow *row)
{
    set_script(c->g, c->ch, operand16(c, 1));
    return row->ends_turn ? end_of_turn() : go_on();
}

static ScriptFlow run_stop(const Command *c, const ScriptRow *row)
{
    (void)row;
    stop(c->g, c->ch, c->at);
    return end_of_turn();
}

/* The first row that covers an opcode is its. */
static const ScriptRow rows[] = {
    { .mask = 0xF0, .match = 0x00, .kind = KIND_SET, .run = run_assign, .value = VALUE_WORD },
    { .mask = 0xF0, .match = 0x20, .kind = KIND_SET, .run = run_assign, .value = VALUE_REGISTER },
    { .mask = 0xF0, .match = 0x10, .kind = KIND_ADD, .run = run_assign, .value = VALUE_WORD },
    { .mask = 0xF0, .match = 0x40, .kind = KIND_ADD, .run = run_assign, .value = VALUE_WORD },
    { .mask = 0xF0, .match = 0x50, .kind = KIND_ADD, .run = run_assign, .value = VALUE_WORD },
    { .mask = 0xF0, .match = 0x30, .kind = KIND_ADD, .run = run_assign, .value = VALUE_REGISTER },
    { .mask = 0xF0, .match = 0x60, .kind = KIND_ADD, .run = run_assign, .value = VALUE_REGISTER },
    { .mask = 0xF0, .match = 0x70, .kind = KIND_ADD, .run = run_assign, .value = VALUE_REGISTER },
    { .mask = 0xF0, .match = 0x80, .kind = KIND_LOOP, .run = run_loop },
    { .mask = 0xF0, .match = 0x90, .kind = KIND_LOOP, .run = run_loop, .ends_turn = true },
    /* $A0-$FF: the low nibble */
    { .mask = 0x0F, .match = 0x00, .kind = KIND_CALL_ROUTINE, .run = run_call_routine },
    { .mask = 0x0F, .match = 0x01, .kind = KIND_STORE, .run = run_store, .takes = store_takes, .entry = 0xC30C19 },
    { .mask = 0x0F, .match = 0x02, .kind = KIND_STORE, .run = run_store, .takes = store_takes, .word = true, .entry = 0xC30C41 },
    { .mask = 0x0F, .match = 0x03, .kind = KIND_GOSUB, .run = run_gosub },
    { .mask = 0x0F, .match = 0x04, .kind = KIND_RETURN, .run = run_return },
    { .mask = 0x0F, .match = 0x05, .kind = KIND_WAIT_FOR_MUSIC, .run = run_wait_for_music },
    { .mask = 0x0F, .match = 0x06, .kind = KIND_SOUND, .run = run_sound },
    { .mask = 0x0F, .match = 0x07, .kind = KIND_TASK, .run = run_task, .takes = task_takes, .list = SCENE_VBLANK_TASKS, .entry = 0xC30BCC },
    { .mask = 0x0F, .match = 0x08, .kind = KIND_TASK, .run = run_task, .takes = task_takes, .list = SCENE_VBLANK_TASKS, .add = true, .entry = 0xC30BC4 },
    { .mask = 0x0F, .match = 0x09, .kind = KIND_TASK, .run = run_task, .takes = task_takes, .list = SCENE_FRAME_TASKS, .entry = 0xC30BBC },
    { .mask = 0x0F, .match = 0x0A, .kind = KIND_TASK, .run = run_task, .takes = task_takes, .list = SCENE_FRAME_TASKS, .add = true, .entry = 0xC30BAB },
    { .mask = 0x0F, .match = 0x0B, .kind = KIND_VIDEO, .run = run_video, .takes = video_takes, .entry = 0xC30C5F },
    { .mask = 0x0F, .match = 0x0C, .kind = KIND_END_TURN, .run = run_end_turn },
    { .mask = 0x0F, .match = 0x0D, .kind = KIND_JUMP, .run = run_jump, .ends_turn = true },
    { .mask = 0x0F, .match = 0x0E, .kind = KIND_JUMP, .run = run_jump },
    { .mask = 0x0F, .match = 0x0F, .kind = KIND_STOP, .run = run_stop },
};

static const ScriptRow *row_of(uint8_t opcode)
{
    for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++)
        if ((opcode & rows[i].mask) == rows[i].match)
            return &rows[i];
    return NULL;
}

static ScriptFlow run_command(const Command *c, const ScriptRow *row)
{
    ScriptFlow flow;
    if (row->entry)
        TIMED_AS(c->g, row->entry, flow = row->run(c, row));
    else
        flow = row->run(c, row);
    return flow;
}

/* ---- The program's routines the scripts call ------------------------------------------------- */

/* The routines the scenes have (scene/title.c, scene/lavos_falls.c, scene/endings.c, scene/staff_roll.c,
 * scene/epoch_dive.c, scene/planet.c), by the value the script holds. */
static const struct {
    const SceneScriptRoutine *routines;
    const unsigned *count;
} routine_tables[] = { { scene_title_routines, &scene_title_routine_count }, { scene_lavos_routines, &scene_lavos_routine_count },
                       { scene_dive_routines, &scene_dive_routine_count }, { scene_planet_routines, &scene_planet_routine_count },
                       { scene_race_routines, &scene_race_routine_count }, { scene_ending_routines, &scene_ending_routine_count },
                       { scene_staff_roll_routines, &scene_staff_roll_routine_count },
                       { scene_turn_routines, &scene_turn_routine_count } };

static const SceneScriptRoutine *routine_port(uint16_t routine)
{
    for (unsigned t = 0; t < sizeof routine_tables / sizeof routine_tables[0]; t++)
        for (unsigned n = 0; n < *routine_tables[t].count; n++)
            if (routine_tables[t].routines[n].routine == routine)
                return &routine_tables[t].routines[n];
    return NULL;
}

/* The routines' addresses are in the program's bank. */
enum { PROGRAM_BANK = 0x7E0000 };

/* The call keeps the routine it calls where the scenes' random draws stir it
 * in after it (the Epoch's dive's). */
static void keep_the_routine_called(GameState *g, uint16_t routine) { scene_leave_for_the_draws(g, routine, TM_ENTROPY_WORDS); }

/* After a command's call, the script moves on past the operand word it was
 * on. */
enum { OPERAND_WORD = 2 };

static void past_the_call(GameState *g, SceneChannel ch)
{
    set_script(g, ch, (uint16_t)(script(g, ch) + OPERAND_WORD));
}

/* ---- A command the port doesn't take ----------------------------------------------------------
 *
 * The original's handler for each command the port may refuse (those whose
 * rows have a `takes`), by the opcode's low nibble: where the scene stops. */
static const uint32_t handlers[] = {
    0xC30C19, 0xC30C41,                       /* the stores, a byte and a word */
    0xC30BCC, 0xC30BC4, 0xC30BBC, 0xC30BAB,   /* the tasks: off and on the vblank list, off and on the frame's */
    0xC30C5F,                                 /* the video setting */
};
static const uint8_t handler_of[16] = { [0x1] = 0, [0x2] = 1, [0x7] = 2, [0x8] = 3, [0x9] = 4, [0xA] = 5, [0xB] = 6 };

/* Read a complete marker update before deciding whether this channel can
 * continue. The sound processor may replace the byte between the reads. */
static MusicStatus steady_music(Platform *pf)
{
    MusicStatus music;
    do {
        music = scene_music_status(pf);
    } while (!music.steady);
    return music;
}

static bool advance_music_wait(GameState *g, SceneChannel ch, MusicStatus music)
{
    bool reached = scene_music_reached(music, script_byte(g, script(g, ch)));
    set_script(g, ch, (uint16_t)(script(g, ch) + (reached ? 1 : -1)));
    return reached;
}

/* ---- A channel's turn ----------------------------------------------------------------------- */

/* A routine a script names: the scene's own, or one of the endings' loads. */
static const SceneScriptRoutine *script_routine(uint16_t routine)
{
    const SceneScriptRoutine *r = routine_port(routine);
    for (unsigned n = 0; !r && n < scene_ending_loading_routine_count; n++)
        if (scene_ending_loading_routines[n].routine == routine)
            r = &scene_ending_loading_routines[n];
    return r;
}

// $7E:37F3
/* The program's routine a script calls; false where the port doesn't have
 * it. */
static bool routine(GameState *g, Platform *pf, SceneChannel ch, uint16_t routine)
{
    const SceneScriptRoutine *r = script_routine(routine);
    if (!r) {
        program_fail(g, PROGRAM_BANK | routine, "a routine a scene's script calls");
        return false;
    }
    keep_the_routine_called(g, routine);
    TIMED_AS(g, PROGRAM_BANK | routine, r->run(g, pf, ch));
    return true;
}

/* The wait for the music, as the original's loop makes a pass of it: at the
 * level (or past it) the script goes on past the level; until then it stays
 * on the command, and the channel's turn ends. The driver a host plays shows
 * its status steadily. */
static bool music_waited_for(GameState *g, Platform *pf, SceneChannel ch)
{
    return advance_music_wait(g, ch, steady_music(pf));
}

// $C3:0AFF
SceneTurn scene_channel_turn(GameState *g, Platform *pf, SceneChannel ch)
{
    for (;;) {
        uint16_t at = script(g, ch);
        Command c = { g, pf, ch, at, script_byte(g, at) };
        const ScriptRow *row = row_of(c.opcode);
        if (row->takes && !row->takes(&c, row)) {
            program_fail(g, handlers[handler_of[low_nibble(&c)]], "a scene command the port doesn't take");
            return SCENE_TURN_GOES_NO_FURTHER;
        }
        ScriptFlow flow = run_command(&c, row);
        switch (flow.kind) {
        case GO_ON:
            continue;
        case END_OF_TURN:
            return SCENE_TURN_ENDED;
        case WAITING:
            if (!music_waited_for(g, pf, ch))
                return SCENE_TURN_ENDED;
            continue;
        case ROUTINE_CALL:
            if (!routine(g, pf, ch, flow.routine))
                return SCENE_TURN_GOES_NO_FURTHER;
            past_the_call(g, ch);
            continue;
        case SOUND_SEND:
            au_command(pf, PF_CALL(0xC30BE9, 0xC70004),
                       (AudioCommand){ .command = sound_command(g), .p1 = sound_command_parameters(g, 0),
                                       .p2 = sound_command_parameters(g, 1), .p3 = sound_command_parameters(g, 2),
                                       .count = 3 });
            past_the_call(g, ch);
            continue;
        }
    }
}

