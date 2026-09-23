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
 *            wait for the sound driver; send it a command; put a task on either
 *            of the frame's lists or take one off; a video setting; end the
 *            turn; jump (ending the turn, or not); and stop, for good
 *
 * The language is a table: each row says which opcodes it covers, which
 * operation they are and how their operands decode, and the interpreter decodes
 * a command once, by its row, into a ScriptCommand of named, typed parameters.
 * The operations are C over the channel. Two call the original's code -- a
 * routine of the program, and the sound driver's entry -- so the loop that runs
 * them is a bridge on the machine, which makes those calls with the registers the
 * original has there. A command the port doesn't take (one no script has: a
 * store outside low RAM, a video setting no call expresses, a task that isn't on
 * its list) is left to the original, as is a script that runs COMMAND_LIMIT
 * commands without ending its turn, which would hang it. */
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
enum { SCRIPT_BANK = 0x7E };

/* Low RAM, which the store commands write: the rest of bank 0 is the I/O
 * registers and the cartridge. */
enum { LOW_RAM_END = 0x2000 };

enum { COMMAND_LIMIT = 0x10000 };

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

/* ---- The language ---------------------------------------------------------------------------- */

typedef enum {
    SET,            /* register = value */
    ADD,            /* register += value */
    LOOP,           /* count a loop counter up to `limit`, jumping back to `target` until it gets there */
    CALL_ROUTINE,   /* a routine of the program */
    STORE,          /* a byte or a word to low RAM */
    GOSUB,          /* a part of the script, returning past this command */
    RETURN,
    WAIT_SOUND,     /* until the sound driver's playing status reaches `level` */
    SOUND,          /* a sound driver command */
    ADD_TASK,
    REMOVE_TASK,
    VIDEO,          /* a video setting */
    END_TURN,
    JUMP,
    STOP,           /* the script is over: it stays here, and the program's main loop is told */
} ScriptOperation;

/* Where an assignment's value is. */
typedef enum { NO_VALUE, VALUE_WORD, VALUE_REGISTER } ValueFrom;

typedef struct {
    uint8_t mask, match;   /* the opcodes the row covers: those whose bits under the mask are `match` */
    ScriptOperation operation;
    ValueFrom value;       /* SET, ADD */
    bool ends_turn;        /* LOOP, JUMP: the channel's turn ends after it */
    bool word;             /* STORE */
    SceneTaskList list;    /* ADD_TASK, REMOVE_TASK */
} ScriptRow;

/* The first row that covers an opcode is its. */
static const ScriptRow rows[] = {
    { .mask = 0xF0, .match = 0x00, .operation = SET, .value = VALUE_WORD },
    { .mask = 0xF0, .match = 0x20, .operation = SET, .value = VALUE_REGISTER },
    { .mask = 0xF0, .match = 0x10, .operation = ADD, .value = VALUE_WORD },
    { .mask = 0xF0, .match = 0x40, .operation = ADD, .value = VALUE_WORD },
    { .mask = 0xF0, .match = 0x50, .operation = ADD, .value = VALUE_WORD },
    { .mask = 0xF0, .match = 0x30, .operation = ADD, .value = VALUE_REGISTER },
    { .mask = 0xF0, .match = 0x60, .operation = ADD, .value = VALUE_REGISTER },
    { .mask = 0xF0, .match = 0x70, .operation = ADD, .value = VALUE_REGISTER },
    { .mask = 0xF0, .match = 0x80, .operation = LOOP },
    { .mask = 0xF0, .match = 0x90, .operation = LOOP, .ends_turn = true },
    /* $A0-$FF: the low nibble */
    { .mask = 0x0F, .match = 0x00, .operation = CALL_ROUTINE },
    { .mask = 0x0F, .match = 0x01, .operation = STORE },
    { .mask = 0x0F, .match = 0x02, .operation = STORE, .word = true },
    { .mask = 0x0F, .match = 0x03, .operation = GOSUB },
    { .mask = 0x0F, .match = 0x04, .operation = RETURN },
    { .mask = 0x0F, .match = 0x05, .operation = WAIT_SOUND },
    { .mask = 0x0F, .match = 0x06, .operation = SOUND },
    { .mask = 0x0F, .match = 0x07, .operation = REMOVE_TASK, .list = SCENE_VBLANK_TASKS },
    { .mask = 0x0F, .match = 0x08, .operation = ADD_TASK, .list = SCENE_VBLANK_TASKS },
    { .mask = 0x0F, .match = 0x09, .operation = REMOVE_TASK, .list = SCENE_FRAME_TASKS },
    { .mask = 0x0F, .match = 0x0A, .operation = ADD_TASK, .list = SCENE_FRAME_TASKS },
    { .mask = 0x0F, .match = 0x0B, .operation = VIDEO },
    { .mask = 0x0F, .match = 0x0C, .operation = END_TURN },
    { .mask = 0x0F, .match = 0x0D, .operation = JUMP, .ends_turn = true },
    { .mask = 0x0F, .match = 0x0E, .operation = JUMP },
    { .mask = 0x0F, .match = 0x0F, .operation = STOP },
};

static const ScriptRow *row_of(uint8_t opcode)
{
    for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++)
        if ((opcode & rows[i].mask) == rows[i].match)
            return &rows[i];
    return NULL;
}

/* The video settings a script makes: it names each by its register, whose
 * number it carries as data. */
typedef enum {
    SCRIPT_MODE7_SETTINGS = 0x1A,     /* the plane's flips, and what lies outside it */
    SCRIPT_OBJ_WINDOWS = 0x25,        /* how the windows mask the sprites and the color window */
    SCRIPT_WINDOW1_LEFT = 0x26,
    SCRIPT_WINDOW1_RIGHT = 0x27,
    SCRIPT_MAIN_SCREEN = 0x2C,        /* the layers on the main screen */
    SCRIPT_SUB_SCREEN = 0x2D,
    SCRIPT_MAIN_WINDOWS = 0x2E,       /* the layers the windows mask on the main screen */
    SCRIPT_COLOR_MATH_LAYERS = 0x31,
    SCRIPT_VIDEO_CONFIG = 0x33,
} ScriptVideoSetting;

/* A command, decoded by its row. */
typedef struct {
    uint16_t at;                /* where it is */
    uint8_t opcode;
    const ScriptRow *row;
    uint8_t length;             /* its bytes, the opcode's included */
    uint8_t reg;                /* SET, ADD: the register; LOOP: the counter */
    uint16_t value;             /* SET, ADD: the value; STORE: what is stored */
    uint16_t address;           /* STORE: where; CALL_ROUTINE: the routine */
    uint16_t limit, target;     /* LOOP; GOSUB, JUMP: target */
    uint8_t level;              /* WAIT_SOUND */
    uint8_t sound[4];           /* SOUND: the command and its three parameters */
    SceneTask task;             /* ADD_TASK, REMOVE_TASK */
    ScriptVideoSetting setting; /* VIDEO */
    uint8_t setting_value;
} ScriptCommand;

static ScriptCommand decode(const GameState *g, SceneChannel ch, uint16_t at)
{
    ScriptCommand c = { .at = at, .opcode = script_byte(g, at), .length = 1 };
    c.row = row_of(c.opcode);
    uint16_t operand = script_word(g, (uint16_t)(at + 1));
    switch (c.row->operation) {
    case SET:
    case ADD:
        c.reg = c.opcode & 0x0F;
        if (c.row->value == VALUE_WORD) {
            c.value = operand;
            c.length = 3;
        } else {
            c.value = scene_channel_register(g, ch, (operand & 0x1E) / 2);
            c.length = 2;
        }
        break;
    case LOOP:
        c.reg = c.opcode & 0x0F;
        c.limit = operand;
        c.target = script_word(g, (uint16_t)(at + 3));
        c.length = 5;
        break;
    case CALL_ROUTINE:
        c.address = operand;
        c.length = 3;
        break;
    case STORE:
        c.address = operand;
        c.value = c.row->word ? script_word(g, (uint16_t)(at + 3)) : script_byte(g, (uint16_t)(at + 3));
        c.length = c.row->word ? 5 : 4;
        break;
    case GOSUB:
    case JUMP:
        c.target = operand;
        c.length = 3;
        break;
    case WAIT_SOUND:
        c.level = (uint8_t)operand;
        c.length = 2;
        break;
    case SOUND:
        for (unsigned i = 0; i < 4; i++)
            c.sound[i] = script_byte(g, (uint16_t)(at + 1 + i));
        c.length = 5;
        break;
    case ADD_TASK:
    case REMOVE_TASK:
        c.task = operand;
        c.length = 3;
        break;
    case VIDEO:
        c.setting_value = (uint8_t)operand;
        c.setting = (ScriptVideoSetting)(operand >> 8);
        c.length = 3;
        break;
    default:
        break;
    }
    return c;
}

/* ---- Video settings ---------------------------------------------------------------------------- */

/* A mode 7 setting a VidMode7 can carry: flips and what lies outside. */
enum { MODE7_SETTINGS_UNUSED = 0x3C };

static bool video_setting_expressed(ScriptVideoSetting setting, uint8_t value)
{
    switch (setting) {
    case SCRIPT_MODE7_SETTINGS: return !(value & MODE7_SETTINGS_UNUSED);
    case SCRIPT_OBJ_WINDOWS:
    case SCRIPT_WINDOW1_LEFT:
    case SCRIPT_WINDOW1_RIGHT:
    case SCRIPT_MAIN_SCREEN:
    case SCRIPT_SUB_SCREEN:
    case SCRIPT_MAIN_WINDOWS:
    case SCRIPT_COLOR_MATH_LAYERS:
    case SCRIPT_VIDEO_CONFIG: return true;
    default: return false;
    }
}

/* The setting, each in the video service's terms: the script's byte is the
 * setting as the game keeps it, which the service's decoders read. */
static void make_video_setting(Platform *pf, ScriptVideoSetting setting, uint8_t v)
{
    PfOrigin at = PF_AT(0xC30C75);
    VidWindows w = { 0 };
    VidColorMath math = { 0 };
    uint32_t windows;
    switch (setting) {
    case SCRIPT_MODE7_SETTINGS:
        vid_set_mode7(pf, at, &(VidMode7){ .hflip = v & 1, .vflip = (v & 2) != 0, .outside = (uint8_t)(v >> 6) },
                      VID_M7_SELECT);
        return;
    case SCRIPT_MAIN_SCREEN: vid_set_screens(pf, at, v, 0, VID_SCREEN_MAIN); return;
    case SCRIPT_SUB_SCREEN: vid_set_screens(pf, at, 0, v, VID_SCREEN_SUB); return;
    case SCRIPT_COLOR_MATH_LAYERS:
        vid_color_math_decode(&math, VID_MATH_LAYERS, v);
        vid_set_color_math(pf, at, &math, VID_MATH_LAYERS);
        return;
    case SCRIPT_VIDEO_CONFIG: {
        VidVideoConfig config = vid_video_config_decode(v);
        vid_set_video_config(pf, at, &config);
        return;
    }
    case SCRIPT_OBJ_WINDOWS: windows = VID_WINDOWS_OBJ_BACK; break;
    case SCRIPT_WINDOW1_LEFT: windows = VID_WINDOWS_W1_LEFT; break;
    case SCRIPT_WINDOW1_RIGHT: windows = VID_WINDOWS_W1_RIGHT; break;
    default: windows = VID_WINDOWS_MAIN; break;
    }
    vid_windows_decode(&w, windows, v);
    vid_set_windows(pf, at, &w, windows);
}

/* ---- What the commands do ---------------------------------------------------------------------- */

/* How a command ends. */
typedef enum {
    GO_ON,          /* on to the next command */
    END_OF_TURN,    /* the interpreter returns */
    WAITING,        /* waiting for the sound driver ($C3:0C2A, an idle entry of its own) */
    ROUTINE_CALL,   /* the program's routine to call, then on past the command */
    SOUND_SEND,     /* the sound command buffer to send, then on past the command */
} ScriptFlow;

/* Whether the port runs the command in this state: what it can't express, or
 * what the original runs on past its bounds, is left to the original. */
static bool port_runs(const GameState *g, const ScriptCommand *c)
{
    switch (c->row->operation) {
    case STORE: return c->address < LOW_RAM_END;
    case VIDEO: return video_setting_expressed(c->setting, c->setting_value);
    case REMOVE_TASK: return scene_task_listed(g, c->row->list, c->task);
    case ADD_TASK: return scene_task_listed(g, c->row->list, NO_TASK);
    default: return true;
    }
}

/* A loop counter reaches its limit when, counted up, it is at least the limit
 * -- compared by the sign of the difference, as the original compares it. A
 * counter at 0 has just started. */
static bool loop_done(uint16_t count, uint16_t limit) { return count != 0 && (int16_t)(count - limit) >= 0; }

/* The operand of a store: where the script goes on is past it, from wherever
 * the store leaves the script pointer (a store to it is a jump). */
enum { STORE_ADDRESS_BYTES = 3 };

static ScriptFlow perform(GameState *g, Platform *pf, SceneChannel ch, const ScriptCommand *c)
{
    uint16_t after = (uint16_t)(c->at + c->length);
    switch (c->row->operation) {
    case SET:
        set_scene_channel_register(g, ch, c->reg, c->value);
        break;
    case ADD:
        set_scene_channel_register(g, ch, c->reg, (uint16_t)(scene_channel_register(g, ch, c->reg) + c->value));
        break;
    case LOOP: {
        uint16_t count = loop_counter(g, ch, c->reg);
        if (loop_done(count, c->limit)) {
            set_loop_counter(g, ch, c->reg, 0);
        } else {
            set_loop_counter(g, ch, c->reg, (uint16_t)(count + 1));
            after = c->target;
        }
        set_script(g, ch, after);
        return c->row->ends_turn ? END_OF_TURN : GO_ON;
    }
    case CALL_ROUTINE:
        set_script(g, ch, (uint16_t)(c->at + 1));
        return ROUTINE_CALL;
    case STORE: {
        set_script(g, ch, (uint16_t)(c->at + STORE_ADDRESS_BYTES));
        uint32_t to = (uint32_t)SCRIPT_BANK << 16 | c->address;   /* low RAM, which bank $7E starts with */
        if (c->row->word)
            wram_write16(g, to, c->value);
        else
            wram_write8(g, to, (uint8_t)c->value);
        set_script(g, ch, (uint16_t)(script(g, ch) + c->length - STORE_ADDRESS_BYTES));
        return GO_ON;
    }
    case GOSUB:
        set_scene_channels_return_address(g, ch, after);
        after = c->target;
        break;
    case RETURN:
        after = scene_channels_return_address(g, ch);
        break;
    case WAIT_SOUND:
        set_script(g, ch, (uint16_t)(c->at + 1));
        return WAITING;
    case SOUND:
        set_sound_command(g, c->sound[0]);
        for (unsigned i = 0; i < 3; i++)
            set_sound_command_parameters(g, i, c->sound[1 + i]);
        if (scene_song_sending(g) != 0)
            break;
        set_script(g, ch, (uint16_t)(c->at + 3));
        return SOUND_SEND;
    case ADD_TASK:
        scene_add_task(g, c->row->list, c->task);
        break;
    case REMOVE_TASK:
        scene_remove_task(g, c->row->list, c->task);
        break;
    case VIDEO:
        make_video_setting(pf, c->setting, c->setting_value);
        break;
    case END_TURN:
        set_script(g, ch, after);
        return END_OF_TURN;
    case JUMP:
        set_script(g, ch, c->target);
        return c->row->ends_turn ? END_OF_TURN : GO_ON;
    case STOP:
        set_script(g, ch, c->at);
        set_scene_frame_ready(g, 1);
        return END_OF_TURN;
    }
    set_script(g, ch, after);
    return GO_ON;
}

/* ---- What the commands leave for the original ----------------------------------------------------
 *
 * The program's code after the interpreter reads the carry and overflow of the
 * last addition or compare a command made, and A as the command that ended the
 * turn left it; and each command keeps a word in its channel's scratch, which
 * only it reads. None of it is the scripts' doing, so it is written apart
 * (original_effects.h), and a build without the original's effects has none of
 * it. The bridge below puts the flags on the machine where it hands over. */

typedef struct {
    bool carry, overflow;
    uint16_t a;
} ScriptLeaves;

static ScriptLeaves *script_leaves;

static bool overflows16(uint16_t a, uint16_t b, uint16_t sum) { return ((a ^ sum) & (b ^ sum) & 0x8000) != 0; }

enum { CHANNEL_PAGES = 0x0A00, CHANNEL_PAGE_BYTES = 0x100 };

/* Before a command runs: the word it keeps in the scratch -- an assignment its
 * register's address, anything else the command and the byte after it -- and
 * the flags it leaves, from its operands. */
static void leave_before(GameState *g, SceneChannel ch, const ScriptCommand *c)
{
    ScriptLeaves *l = script_leaves;
    SceneMathPage page = scene_math_channel(ch);
    if (c->row->operation == SET || c->row->operation == ADD) {
        uint16_t d = (uint16_t)(CHANNEL_PAGES + ch * CHANNEL_PAGE_BYTES), reg = (uint16_t)(2 * c->reg);
        uint16_t address = (uint16_t)(d + reg);
        set_scene_math_scratch_script_scratch(g, page, address);
        l->carry = (uint32_t)d + reg > 0xFFFF;
        l->overflow = overflows16(d, reg, address);
        if (c->row->operation == ADD) {
            uint16_t was = scene_channel_register(g, ch, c->reg), sum = (uint16_t)(was + c->value);
            l->carry = (uint32_t)was + c->value > 0xFFFF;
            l->overflow = overflows16(was, c->value, sum);
        }
        return;
    }
    set_scene_math_scratch_script_scratch(g, page, script_word(g, c->at));
    l->carry = false;   /* the shift of the command's number that dispatches it */
    switch (c->row->operation) {
    case LOOP: {
        uint16_t count = loop_counter(g, ch, c->reg);
        if (loop_done(count, c->limit)) {
            uint16_t on = (uint16_t)(c->at + 1);
            l->carry = (uint32_t)on + 4 > 0xFFFF;
            l->overflow = overflows16(on, 4, (uint16_t)(on + 4));
        } else if (count != 0) {
            l->carry = count >= c->limit;
        }
        break;
    }
    case VIDEO:
        l->overflow = false;   /* the sum that makes the register's address */
        break;
    default:
        break;
    }
}

/* A where a command ends the turn: the loop's the script's new place, the jump
 * its target, the stop 1, and the plain end of turn its command's number in the
 * interpreter's table. */
enum { END_TURN_INDEX = 0x18 };

static void leave_a_at_end_of_turn(const GameState *g, SceneChannel ch, const ScriptCommand *c)
{
    switch (c->row->operation) {
    case LOOP: script_leaves->a = script(g, ch); break;
    case JUMP: script_leaves->a = c->target; break;
    case STOP: script_leaves->a = 1; break;
    default: script_leaves->a = END_TURN_INDEX; break;
    }
}

/* ---- Where the port meets the original's registers ------------------------------------------
 *
 * The loop and its call-outs: they set what the original has there, and a build
 * for the 65816 replaces them. */

static void set_widths16(Snes *s) { s->cpu.p &= (uint8_t)~(CPU_FLAG_M | CPU_FLAG_X); }

static void put_flags(Snes *s)
{
    m_set_flag(s, CPU_FLAG_C, script_leaves->carry);
    m_set_flag(s, CPU_FLAG_V, script_leaves->overflow);
}

/* The channel whose page D is on: a channel's page is the channel instance of
 * the math helpers' page kind (re/ram.toml, scene_math). */
static bool on_channel(uint16_t d, SceneChannel *ch)
{
    SceneMathPage page;
    if (!scene_math_page_of(d, &page) || page.kind != SCENE_MATH_CHANNEL)
        return false;
    *ch = page.index;
    return true;
}

/* Where the interpreter runs as the port has it: native mode, 16-bit index
 * registers, binary adds, the scripts' data bank and D on a channel's page. */
static bool machine_as_expected(Snes *s, SceneChannel *ch)
{
    return !s->cpu.e && !(s->cpu.p & (CPU_FLAG_X | CPU_FLAG_D)) && s->cpu.db == SCRIPT_BANK && on_channel(s->cpu.d, ch);
}

/* After a call-out: the port carries on if the callee left the machine as
 * expected (on whichever channel's page it left D); otherwise the original
 * does. Either way the flags are the callee's. */
static bool can_continue(Snes *s, SceneChannel *ch)
{
    script_leaves->carry = m_flag(s, CPU_FLAG_C);
    script_leaves->overflow = m_flag(s, CPU_FLAG_V);
    return machine_as_expected(s, ch);
}

// $C3:0C51
/* A routine of the program, which $7E:37F3 calls through the scene's scratch. */
static bool call_routine(Snes *s, SceneChannel *ch, uint16_t routine)
{
    set_scene_math_scratch_script_scratch(state_of(s), scene_math_scene(), routine);
    s->cpu.a = routine;
    set_widths16(s);
    put_flags(s);
    m_jsl_dc(s, 0xC30C58, 0x7E37F3, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z);
    return can_continue(s, ch);
}

// $C3:0BD4
/* The sound driver command in its buffer, with the registers at the original's
 * call: A's high byte the last parameter and its low byte the flag of a song
 * being sent, 0; X the command's place in the table. */
enum { SOUND_TABLE_INDEX = 0x000C };

static bool send_sound(Snes *s, SceneChannel *ch)
{
    GameState *g = state_of(s);
    s->cpu.a = (uint16_t)(sound_command_parameters(g, 2) << 8);
    s->cpu.x = SOUND_TABLE_INDEX;
    set_widths16(s);
    s->cpu.p |= CPU_FLAG_M;
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, true);
    put_flags(s);
    au_command(m_platform(s), PF_CALL(0xC30BE9, 0xC70004),
               (AudioCommand){ .command = sound_command(g), .p1 = sound_command_parameters(g, 0),
                               .p2 = sound_command_parameters(g, 1), .p3 = sound_command_parameters(g, 2),
                               .count = 3 });
    return can_continue(s, ch);
}

/* After a command's call-out, the script moves on past its operand on the page
 * the callee left in D: a channel's, or, where the callee left the machine
 * other than the port expects, wherever D is, as the original's does. */
enum { SCRIPT_CELL = 0x20, CALL_OPERAND_BYTES = 2 };

static bool past_the_call(Snes *s, SceneChannel ch, bool go_on)
{
    if (go_on) {
        set_script(state_of(s), ch, (uint16_t)(script(state_of(s), ch) + CALL_OPERAND_BYTES));
        return true;
    }
    uint16_t cell = (uint16_t)(s->cpu.d + SCRIPT_CELL);
    m_write16(s, cell, (uint16_t)(m_read16(s, cell) + CALL_OPERAND_BYTES));
    return false;
}

/* How the run ends. */
typedef enum { RUN_ENDED_TURN, RUN_WAITING, RUN_HANDED_BACK } RunEnd;

/* The wait for the sound driver's registers: A and X the command's place in
 * the interpreter's table. */
enum { WAIT_TABLE_INDEX = 0x000A };

// $C3:0B03
/* Run commands until one ends the channel's turn. Otherwise the script is left
 * on the command the original is to carry on from (RUN_HANDED_BACK), or the port
 * has stopped at the sound wait (RUN_WAITING). */
static RunEnd run(Snes *s, SceneChannel ch)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    for (uint32_t i = 0; i < COMMAND_LIMIT; i++) {
        ScriptCommand c = decode(g, ch, script(g, ch));
        if (!port_runs(g, &c))
            return RUN_HANDED_BACK;
        ORIGINAL_EFFECT(leave_before(g, ch, &c));
        switch (perform(g, pf, ch, &c)) {
        case GO_ON:
            continue;
        case END_OF_TURN:
            ORIGINAL_EFFECT(leave_a_at_end_of_turn(g, ch, &c));
            s->cpu.a = script_leaves->a;
            return RUN_ENDED_TURN;
        case WAITING:
            s->cpu.a = WAIT_TABLE_INDEX;
            s->cpu.x = WAIT_TABLE_INDEX;
            set_widths16(s);
            put_flags(s);
            m_stop_at(s, 0xC30C2A, DC_X | DC_N | DC_Z | DC_C);
            return RUN_WAITING;
        case ROUTINE_CALL:
            if (!past_the_call(s, ch, call_routine(s, &ch, c.address)))
                return RUN_HANDED_BACK;
            continue;
        case SOUND_SEND:
            if (!past_the_call(s, ch, send_sound(s, &ch)))
                return RUN_HANDED_BACK;
            continue;
        }
    }
    return RUN_HANDED_BACK;
}

/* The JSR at $C3:0AFF pushes this as its return address. */
enum { INTERPRETER_RETURN = 0x0B01 };

// $C3:0AFF
/* The channel whose page is in D runs until its script ends its turn. */
void scene_run_sequence(Snes *s)
{
    ScriptLeaves leaves = { m_flag(s, CPU_FLAG_C), m_flag(s, CPU_FLAG_V), s->cpu.a };
    script_leaves = &leaves;
    SceneChannel ch = 0;
    machine_as_expected(s, &ch);
    m_push16(s, INTERPRETER_RETURN);   /* the JSR's frame, there at every call-out */
    RunEnd end = run(s, ch);
    if (end == RUN_WAITING)
        return;
    m_pull16(s);
    s->cpu.p &= (uint8_t)~CPU_FLAG_M;
    put_flags(s);
    if (end == RUN_HANDED_BACK) {
        /* the original's own loop, from the command the port left. Not a
         * stop at the loop's head, which shadow mode and the fuzzer could now
         * compare at the right pass; docs/TODO.md */
        m_jsr_dc(s, 0xC30AFF, 0xC30B03, DC_A | DC_X | DC_N | DC_Z);
    }
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
    return s->cpu.db == SCRIPT_BANK && on_channel(s->cpu.d, &ch);
}

/* ---- The wait for the sound driver ---------------------------------------------------------- */

/* $C3:0C28, where the wait goes on, isn't an entry, so after the wait the rest
 * of that run is the original's. It could be: the store command's own path runs
 * through it, but an entry the original reaches inside a run shadow mode is
 * comparing no longer takes the run's call-outs (docs/PORTING.md, Call-outs). */

/* The sound driver's playing status (docs/SOUND.md, "What the game reads
 * back"): the script waits until its low nibble reaches the command's level. */
enum { STATUS_PORT = 2, PLAYING = 0x0F };

// $C3:0C2A
/* One pass of the wait: the status, read until two reads agree; at the level
 * or past it (compared by the sign of the difference, as the original compares
 * it), the wait is over, and until then the script stays on the command and the
 * channel's turn ends, with A the status and the carry from the compare. Idle
 * entry. */
void scene_script_wait_for_sound(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    SceneChannel ch = 0;
    on_channel(s->cpu.d, &ch);   /* the guard has checked it is */
    s->cpu.p |= CPU_FLAG_M;
    uint8_t first = au_status(pf, PF_AT(0xC30C2C), STATUS_PORT);
    uint8_t second = au_status(pf, PF_AT(0xC30C30), STATUS_PORT);
    m_set_a8(s, first);
    m_set_flag(s, CPU_FLAG_C, first >= second);
    m_set_nz8(s, (uint8_t)(first - second));
    if (first != second)
        return;
    uint8_t level = first & PLAYING, wanted = script_byte(g, script(g, ch));
    m_set_a8(s, level);
    m_set_flag(s, CPU_FLAG_C, level >= wanted);
    m_set_nz8(s, (uint8_t)(level - wanted));
    if ((int8_t)(level - wanted) >= 0) {
        s->cpu.pc = 0x0C28;
        return;
    }
    s->cpu.p &= (uint8_t)~CPU_FLAG_M;
    set_script(g, ch, (uint16_t)(script(g, ch) - 1));
    m_set_nz16(s, script(g, ch));
    m_rts(s);
}
