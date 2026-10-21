/* Scenes: the sequence scripts.
 *
 * A scene program runs two scripts at once, on its two sequence channels
 * (scene_channels): the title's, the attract's and the endings' timing and
 * choreography -- when a picture fades, which routine of the program runs,
 * what the sound driver plays. Each frame the program runs each channel's script
 * from where it stopped until a command ends the channel's turn. A channel has
 * sixteen word registers, sixteen loop counters and a return address, and its
 * routines run on its page, so a script's registers are also the variables of
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
 * operation over the channel. Two operations call the original's code -- a
 * routine of the program, and the sound driver's entry -- so the loop that runs
 * them is a bridge on the machine. A command the port doesn't take (one no
 * script has: a store outside low RAM, a video setting no call expresses, a task
 * to take off that isn't on its list or to put on a list that is full) is handed
 * to the original's code for that command, which carries on with the rest of
 * the turn. A script that never ends its turn hangs the game, in the port as in
 * the original. */
#include "scene/scene.h"

#include "original_effects.h"
#include "platform/audio.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* The scripts are in the program's bank, which is also the data bank the
 * interpreter runs with. */
enum { SCRIPT_BANK = SCENE_LOWER_WORK_RAM };

/* Low RAM, which the store commands write: the rest of bank 0 is the I/O
 * registers and the cartridge. */
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
    WAITING,        /* waiting for the music ($C3:0C2A, an idle entry of its own) */
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

/* What a row's commands are, which the bridge reads for what the original
 * leaves after them. */
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
} ScriptRow;

/* Across a call to the original's code, the script stays on the operand word the
 * command read last -- a routine it calls can move it -- and goes on past that
 * word from wherever the callee left it. */
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
    return scene_task_listed(c->g, row->list, adding ? NO_TASK : operand16(c, 1));
}

static ScriptFlow run_task(const Command *c, const ScriptRow *row)
{
    SceneTask task = operand16(c, 1);
    if (row->add)
        scene_add_task(c->g, row->list, task);
    else
        scene_remove_task(c->g, row->list, task);
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
    { .mask = 0x0F, .match = 0x01, .kind = KIND_STORE, .run = run_store, .takes = store_takes },
    { .mask = 0x0F, .match = 0x02, .kind = KIND_STORE, .run = run_store, .takes = store_takes, .word = true },
    { .mask = 0x0F, .match = 0x03, .kind = KIND_GOSUB, .run = run_gosub },
    { .mask = 0x0F, .match = 0x04, .kind = KIND_RETURN, .run = run_return },
    { .mask = 0x0F, .match = 0x05, .kind = KIND_WAIT_FOR_MUSIC, .run = run_wait_for_music },
    { .mask = 0x0F, .match = 0x06, .kind = KIND_SOUND, .run = run_sound },
    { .mask = 0x0F, .match = 0x07, .kind = KIND_TASK, .run = run_task, .takes = task_takes, .list = SCENE_VBLANK_TASKS },
    { .mask = 0x0F, .match = 0x08, .kind = KIND_TASK, .run = run_task, .takes = task_takes, .list = SCENE_VBLANK_TASKS, .add = true },
    { .mask = 0x0F, .match = 0x09, .kind = KIND_TASK, .run = run_task, .takes = task_takes, .list = SCENE_FRAME_TASKS },
    { .mask = 0x0F, .match = 0x0A, .kind = KIND_TASK, .run = run_task, .takes = task_takes, .list = SCENE_FRAME_TASKS, .add = true },
    { .mask = 0x0F, .match = 0x0B, .kind = KIND_VIDEO, .run = run_video, .takes = video_takes },
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

/* ---- Where the port meets the original's registers ------------------------------------------
 *
 * The loop and its call-outs: they set what the original has there, and a build
 * for the 65816 replaces them. */

static void set_widths16(Snes *s) { s->cpu.p &= (uint8_t)~(CPU_FLAG_M | CPU_FLAG_X); }

/* ---- What the original leaves after a command ------------------------------------------------
 *
 * Scaffolding: the original's interpreter leaves behind a word in the channel's
 * scratch and its carry and overflow, which nothing in the scripts' language
 * means but some of the program's code reads. The bridge leaves them after
 * each command, from the command's kind and what it changed, the way the
 * original's code for that kind of command leaves them; a native build leaves
 * nothing.
 *
 * The word: every command keeps one in its channel's scratch, which only it
 * reads -- an assignment the address of its register, any other command itself
 * and the byte after it. What is left there is the last command's. */
enum { CHANNEL_PAGES = 0x0A00, CHANNEL_PAGE_BYTES = 0x100 };

static uint16_t kept_word(const Command *c, const ScriptRow *row)
{
    if (row->kind == KIND_SET || row->kind == KIND_ADD)
        return (uint16_t)(CHANNEL_PAGES + c->ch * CHANNEL_PAGE_BYTES + 2 * low_nibble(c));
    return operand16(c, 0);
}

static void keep_word(const Command *c, const ScriptRow *row)
{
    set_scene_call_script_scratch(c->g, scene_program_channel(c->ch), kept_word(c, row));
}

/* The flags. What reads them is the program's code after a sequence run, not
 * the scripts: the attract and title programs run their channels at $7E:54B4
 * and go straight on to $7E:5777, whose $7E:3443 calls the random number mixer
 * at $C3:02DD, which starts with a ROL of A and so mixes the carry into the
 * number; and $7E:62F0 reaches the ADC #$40 at $7E:65B3 with the carry the
 * sequence run left, since $7E:3448 between them saves and restores P. So the
 * carry is read. Nothing in either program or in bank C3 branches on the
 * overflow (no BVS or BVC), but the liveness analysis can't see that far and
 * counts it as read, so it is left too.
 *
 * Per kind of command, as the original's code for it leaves them:
 * - a set: both clear (the original's sum of the channel's page and the
 *   register's place, which carries and overflows on no channel's page);
 * - an add: the add's;
 * - a loop, dispatched through a doubled index that clears the carry: done, the
 *   flags of its step past the command, (at + 1) + 4; not done, the carry of
 *   comparing the counter (unsigned) with the limit, where a counter at 0 is
 *   short of it, and the overflow as it was;
 * - a video setting: both clear;
 * - any other command: the carry clear from the dispatch, the overflow as it
 *   was; and a call to the original's code leaves the callee's. */

/* What the flags are worked out from, before the command runs: an add's
 * register, a loop's counter and its limit (read before the loop runs, since a
 * script can lie over the counters it counts). */
typedef struct {
    uint16_t value, limit;
} Before;

static Before before(const Command *c, const ScriptRow *row)
{
    switch (row->kind) {
    case KIND_ADD: return (Before){ scene_channel_register(c->g, c->ch, low_nibble(c)), 0 };
    case KIND_LOOP: return (Before){ loop_counter(c->g, c->ch, low_nibble(c)), operand16(c, 1) };
    default: return (Before){ 0, 0 };
    }
}

static bool overflows16(uint16_t a, uint16_t b, uint16_t sum) { return ((a ^ sum) & (b ^ sum) & 0x8000) != 0; }

/* A loop's step past the command when it is done, (at + 1) + 4. */
enum { LOOP_STEP = 4 };

static void note_flags(Snes *s, const Command *c, const ScriptRow *row, Before before)
{
    uint16_t was = before.value, limit = before.limit;
    bool carry = false, overflow = m_flag(s, CPU_FLAG_V);
    if (row->kind == KIND_ADD) {
        uint16_t now = scene_channel_register(c->g, c->ch, low_nibble(c));
        carry = now < was;
        overflow = overflows16(was, (uint16_t)(now - was), now);
    } else if (row->kind == KIND_LOOP) {
        uint16_t step_from = (uint16_t)(c->at + 1);
        if (loop_done(was, limit)) {
            carry = (uint32_t)step_from + LOOP_STEP > 0xFFFF;
            overflow = overflows16(step_from, LOOP_STEP, (uint16_t)(step_from + LOOP_STEP));
        } else {
            carry = was != 0 && was >= limit;
        }
    } else if (row->kind == KIND_SET || row->kind == KIND_VIDEO) {
        overflow = false;
    }
    m_set_flag(s, CPU_FLAG_C, carry);
    m_set_flag(s, CPU_FLAG_V, overflow);
}

/* Where the interpreter runs as the port has it: native mode, 16-bit index
 * registers, binary adds, the scripts' data bank and D on a channel's page. */
static bool machine_as_expected(Snes *s, SceneChannel *ch)
{
    return !s->cpu.e && !(s->cpu.p & (CPU_FLAG_X | CPU_FLAG_D)) && s->cpu.db == SCRIPT_BANK &&
           scene_channel_at(s->cpu.d, ch);
}

/* After a call-out: the port carries on if the callee left the machine as
 * expected (on whichever channel's page it left D); otherwise the original
 * does. Either way the flags are the callee's. */
static bool can_continue(Snes *s, SceneChannel *ch) { return machine_as_expected(s, ch); }

/* ---- The program's routines the scripts call ------------------------------------------------- */

/* The routines that are C (scene/title.c, scene/lavos_falls.c,
 * scene/epoch_dive.c), by the value the script holds. */
static const struct {
    const SceneScriptRoutine *routines;
    const unsigned *count;
} routine_tables[] = { { scene_title_routines, &scene_title_routine_count }, { scene_lavos_routines, &scene_lavos_routine_count },
                       { scene_dive_routines, &scene_dive_routine_count } };

#if ORIGINAL_EFFECTS
/* What they leave, in tables parallel to theirs. */
static const SceneRoutineLeaves *const routine_leaves[] = { scene_title_routine_leaves, scene_lavos_routine_leaves,
                                                             scene_dive_routine_leaves };
#endif

static const SceneScriptRoutine *routine_port(uint16_t routine)
{
    for (unsigned t = 0; t < sizeof routine_tables / sizeof routine_tables[0]; t++)
        for (unsigned n = 0; n < *routine_tables[t].count; n++)
            if (routine_tables[t].routines[n].routine == routine)
                return &routine_tables[t].routines[n];
    return NULL;
}

/* A routine a caller reaches by its address (the interpreter runs them as C):
 * its work on the channel whose page D is, and back with 16-bit A. Some set
 * the data bank to the first bank's around their stores and, through the
 * stack's imbalance, come back with it on the program's. */
enum { PROGRAM_DATA_BANK = 0x7E };

/* The registers a routine leaves, from its row of the leaves' tables, for A
 * from the A it was called with. */
static void leave_the_registers(Snes *s, const SceneScriptRoutine *r, uint16_t called_with)
{
#if ORIGINAL_EFFECTS
    for (unsigned t = 0; t < sizeof routine_tables / sizeof routine_tables[0]; t++)
        for (unsigned n = 0; n < *routine_tables[t].count; n++)
            if (&routine_tables[t].routines[n] == r) {
                const SceneRoutineLeaves *l = &routine_leaves[t][n];
                s->cpu.a = l->a(called_with);
                if (l->rest)
                    l->rest(s);
                return;
            }
#else
    (void)s;
    (void)r;
    (void)called_with;
#endif
}

void scene_script_routine_alone(Snes *s, const SceneScriptRoutine *r)
{
    r->run(state_of(s), m_platform(s), scene_program_page_at_entry(s).index);
    ORIGINAL_EFFECT(leave_the_registers(s, r, s->cpu.a));
    if (r->leaves_the_program_bank)
        s->cpu.db = PROGRAM_DATA_BANK;
    s->cpu.p &= (uint8_t)~CPU_FLAG_M;
    m_rts(s);
}

/* The call reads the routine's address from the scene's own page, where the
 * interpreter puts it ($7E:37F3), and calls it with A the same. */
enum { PROGRAM_BANK = 0x7E0000 };

static bool call_present(Snes *s) { return m_code_matches(s, 0x7E37F3, 7, 0x7238FF62A389BBE0ull); }

/* The call keeps the routine it calls where the scenes' random draws stir it
 * in after it (the Epoch's dive's). */
static void keep_the_routine_called(GameState *g, uint16_t routine) { scene_leave_for_the_draws(g, routine, TM_ENTROPY_WORDS); }

/* A routine the port has, as C, where the program's code for the call and for
 * it is the code the port stands for: its work on the calling channel, timed
 * as the routine, with 16-bit A after. False where it isn't. */
static bool call_routine_as_c(Snes *s, SceneChannel ch, uint16_t routine)
{
    const SceneScriptRoutine *r = routine_port(routine);
    if (!r || !call_present(s) || !r->present(s))
        return false;
    GameState *g = state_of(s);
    keep_the_routine_called(g, routine);
    NATIVE_TIME_AS(s, PROGRAM_BANK | routine, r->run(g, m_platform(s), ch));
    ORIGINAL_EFFECT(leave_the_registers(s, r, routine));
    if (r->leaves_the_program_bank)
        s->cpu.db = PROGRAM_BANK >> 16;
    set_widths16(s);
    return true;
}

// $C3:0C51
/* A routine of the program, which $7E:37F3 calls through the scene's own page:
 * the routine's address is what that call reads. As C where the port has it;
 * the original's otherwise. */
static bool call_routine(Snes *s, SceneChannel *ch, uint16_t routine)
{
    if (call_routine_as_c(s, *ch, routine))
        return true;
    keep_the_routine_called(state_of(s), routine);
    s->cpu.a = routine;   /* as the original loaded it: some routines read it */
    set_widths16(s);
    m_jsl_dc(s, 0xC30C58, 0x7E37F3, DC_X | DC_N | DC_Z);
    return can_continue(s, ch);
}

// $C3:0BD4
/* The sound driver command in its buffer, as the original calls the driver
 * (8-bit A). The driver's entry saves A, which the liveness analysis counts as
 * reading its high byte: there the original has the command's last parameter,
 * which it loaded last. */
static bool send_sound(Snes *s, SceneChannel *ch)
{
    GameState *g = state_of(s);
    set_widths16(s);
    s->cpu.p |= CPU_FLAG_M;
    s->cpu.a = (uint16_t)(sound_command_parameters(g, 2) << 8);
    au_command(m_platform(s), PF_CALL_DC(0xC30BE9, 0xC70004, DC_AL | DC_X | DC_N | DC_Z),
               (AudioCommand){ .command = sound_command(g), .p1 = sound_command_parameters(g, 0),
                               .p2 = sound_command_parameters(g, 1), .p3 = sound_command_parameters(g, 2),
                               .count = 3 });
    return can_continue(s, ch);
}

/* After a command's call-out, the script moves on past the operand word it was
 * on. Where the callee left the machine other than the port expects, the port
 * stops where the call returns and the original's code moves the script on,
 * on whatever page the callee left in D. */
enum { OPERAND_WORD = 2 };

static void past_the_call(GameState *g, SceneChannel ch)
{
    set_script(g, ch, (uint16_t)(script(g, ch) + OPERAND_WORD));
}

/* Where each call-out returns, in the original's code. */
enum { ROUTINE_CALL_RETURN = 0xC30C5C, SOUND_CALL_RETURN = 0xC30BED };

/* ---- A command the port hands over -------------------------------------------------------------
 *
 * The original's code for each command the port may hand over (those whose
 * rows have a `takes`): where its dispatch jumps, by the opcode's low nibble. */
static const uint32_t handlers[] = {
    0xC30C19, 0xC30C41,                       /* the stores, a byte and a word */
    0xC30BCC, 0xC30BC4, 0xC30BBC, 0xC30BAB,   /* the tasks: off and on the vblank list, off and on the frame's */
    0xC30C5F,                                 /* the video setting */
};
static const uint8_t handler_of[16] = { [0x1] = 0, [0x2] = 1, [0x7] = 2, [0x8] = 3, [0x9] = 4, [0xA] = 5, [0xB] = 6 };

/* The original's code for the command takes over as its dispatch enters it:
 * the script past the opcode, and A and X the handler's place in the dispatch
 * table, with the flags of the shift that made it (N and the carry clear). The
 * scratch word the dispatch keeps is the command's. */
static void hand_over(Snes *s, const Command *c, const ScriptRow *row)
{
    unsigned n = low_nibble(c);
    uint16_t index = (uint16_t)(2 * n);
    ORIGINAL_EFFECT(keep_word(c, row));
    set_script(c->g, c->ch, after(c, 1));
    set_widths16(s);
    s->cpu.a = index;
    s->cpu.x = index;
    m_set_nz16(s, index);
    m_set_flag(s, CPU_FLAG_C, false);
    m_stop_at(s, handlers[handler_of[n]], 0);
}

/* A where a command ends the turn: the loop's and the jump's the script's new
 * place, the stop 1, and the plain end of turn its handler's place in the
 * interpreter's table. */
enum { END_TURN_INDEX = 0x18, STOPPED = 1 };

static uint16_t a_at_end_of_turn(const GameState *g, SceneChannel ch, const ScriptRow *row)
{
    switch (row->kind) {
    case KIND_LOOP:
    case KIND_JUMP: return script(g, ch);
    case KIND_STOP: return STOPPED;
    default: return END_TURN_INDEX;
    }
}

/* How the run ends: the turn over, or the port stopped where the original
 * carries on (the wait for the music, a command handed over, a call-out that
 * came back other than expected). */
typedef enum { RUN_ENDED_TURN, RUN_STOPPED } RunEnd;

/* The wait for the music's registers: A and X its handler's place in the
 * interpreter's table. */
enum { WAIT_TABLE_INDEX = 0x000A };

// $C3:0B03
/* Run commands until one ends the channel's turn. */
static RunEnd run(Snes *s, SceneChannel ch)
{
    GameState *g = state_of(s);
    for (;;) {
        uint16_t at = script(g, ch);
        Command c = { g, m_platform(s), ch, at, script_byte(g, at) };
        const ScriptRow *row = row_of(c.opcode);
        if (row->takes && !row->takes(&c, row)) {
            hand_over(s, &c, row);
            return RUN_STOPPED;
        }
        ORIGINAL_EFFECT(keep_word(&c, row));
        Before was = { 0, 0 };
        ORIGINAL_EFFECT(was = before(&c, row));
        ScriptFlow flow = row->run(&c, row);
        ORIGINAL_EFFECT(note_flags(s, &c, row, was));
        switch (flow.kind) {
        case GO_ON:
            continue;
        case END_OF_TURN:
            s->cpu.a = a_at_end_of_turn(g, ch, row);
            return RUN_ENDED_TURN;
        case WAITING:
            s->cpu.a = WAIT_TABLE_INDEX;
            s->cpu.x = WAIT_TABLE_INDEX;
            set_widths16(s);
            m_stop_at(s, 0xC30C2A, DC_X | DC_N | DC_Z | DC_C);
            return RUN_STOPPED;
        case ROUTINE_CALL:
            if (!call_routine(s, &ch, flow.routine)) {
                m_stop_at(s, ROUTINE_CALL_RETURN, 0);
                return RUN_STOPPED;
            }
            past_the_call(g, ch);
            continue;
        case SOUND_SEND:
            if (!send_sound(s, &ch)) {
                m_stop_at(s, SOUND_CALL_RETURN, 0);
                return RUN_STOPPED;
            }
            past_the_call(g, ch);
            continue;
        }
    }
}

/* The JSR at $C3:0AFF pushes this as its return address. */
enum { INTERPRETER_RETURN = 0x0B01 };

// $C3:0AFF
/* The channel whose page is in D runs until its script ends its turn. */
void scene_run_sequence(Snes *s)
{
    SceneChannel ch = 0;
    machine_as_expected(s, &ch);
    m_push16(s, INTERPRETER_RETURN);   /* the JSR's frame, there at every call-out and stop */
    if (run(s, ch) == RUN_STOPPED)
        return;
    m_pull16(s);
    s->cpu.p &= (uint8_t)~CPU_FLAG_M;
    m_rtl(s);
}

bool scene_sequence_bank_is_scripts(Snes *s)
{
    SceneChannel ch;
    return machine_as_expected(s, &ch);
}

/* The wait reads the script through the data bank, on the channel D is on. */
bool scene_sequence_waits_on_channel(Snes *s)
{
    SceneChannel ch;
    return s->cpu.db == SCRIPT_BANK && scene_channel_at(s->cpu.d, &ch);
}

/* ---- The wait for the music ------------------------------------------------------------------ */

/* $C3:0C28, where the wait goes on, isn't an entry, so after the wait the rest
 * of that run is the original's. It could be: the store command's own path runs
 * through it, but an entry the original reaches inside a run shadow mode is
 * comparing no longer takes the run's call-outs (docs/PORTING.md, Call-outs). */

// $C3:0C2A
/* One pass of the wait: the music's progress (scene/sound.c) against the level
 * after the command; at the level or past it the wait is over, and until then
 * the script stays on the command and the channel's turn ends. Idle entry: A is
 * what the original compared last, and the flags its compare's. */
void scene_script_wait_for_sound(Snes *s)
{
    GameState *g = state_of(s);
    SceneChannel ch = 0;
    scene_channel_at(s->cpu.d, &ch);   /* the guard has checked it is */
    s->cpu.p |= CPU_FLAG_M;
    MusicStatus music = scene_music_status(m_platform(s));
    uint8_t level = script_byte(g, script(g, ch));
    uint8_t compared = music.steady ? scene_music_progress(music) : music.first;
    uint8_t against = music.steady ? level : music.second;
    m_set_a8(s, compared);
    m_set_flag(s, CPU_FLAG_C, compared >= against);
    m_set_nz8(s, (uint8_t)(compared - against));
    if (!music.steady)
        return;
    if (scene_music_reached(music, level)) {
        s->cpu.pc = 0x0C28;
        return;
    }
    s->cpu.p &= (uint8_t)~CPU_FLAG_M;
    set_script(g, ch, (uint16_t)(script(g, ch) - 1));
    m_set_nz16(s, script(g, ch));
    m_rts(s);
}

/* ---- A channel's script started from the program ------------------------------------------------ */

/* The interpreter reads none of A, X and the flags it sets; Y, the overflow and
 * the carry pass through it to the routines its commands call. */
enum { INTERPRETER = 0xC30AFF, THE_INTERPRETER_PASSES_Y_THE_OVERFLOW_AND_THE_CARRY = DC_A | DC_X | DC_N | DC_Z };

// $7E:3749, $7E:3754
/* A channel's script run until it ends its turn, as the program's setups start
 * one: on the channel's page, which the original sets through A, with the
 * scripts' bank. */
static void run_from_the_program(Snes *s, SceneChannel ch, uint32_t call)
{
    s->cpu.d = (uint16_t)(CHANNEL_PAGES + ch * CHANNEL_PAGE_BYTES);
    s->cpu.a = s->cpu.d;
    s->cpu.db = SCRIPT_BANK;
    if (scene_run_long(s, call, INTERPRETER, THE_INTERPRETER_PASSES_Y_THE_OVERFLOW_AND_THE_CARRY, scene_run_sequence))
        m_rts(s);
}

void scene_run_channel_0_script(Snes *s) { run_from_the_program(s, 0, 0x7E374F); }
void scene_run_channel_1_script(Snes *s) { run_from_the_program(s, 1, 0x7E375A); }

bool scene_channel_script_start_present(Snes *s) { return m_code_matches(s, 0x7E3749, 22, 0xB1CDD688B2BFDC2Aull); }
