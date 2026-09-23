/* Scenes: the sequence scripts.
 *
 * The program at $7E:3000 (the attract sequence, the title, the endings) runs
 * two sequence channels each frame. A channel is a page of work RAM
 * (scene_channels: $0A00 or $0B00, which the program puts in D before it calls
 * $C3:0AFF, with the data bank $7E): sixteen word registers, the script pointer,
 * loop counters and a return address. The interpreter reads commands at the
 * pointer (bank $7E) until one yields, and the script carries on from there
 * next frame. tools/disasm/seqscript.py has the command set:
 *
 * - $00-$7F, a register assignment: the low nibble picks the register, bit 5
 *   of the command whether the operand is a word (3 bytes) or another register
 *   (2 bytes: the byte's bits 1-4 name it), and bits 4-6 otherwise whether it
 *   is stored ($00, $20) or added;
 * - $80-$9F, a counted loop: the counter n counts up to the word operand,
 *   jumping to the word after it until it gets there; $9n also yields;
 * - $A0-$FF, the low nibble picks the rest: call a routine of the program (by
 *   its address in the scene's scratch), store a byte or a word to low RAM,
 *   call and return within the script, wait for the sound driver, send it a
 *   command, add or remove a task in either of the NMI's task lists, write a
 *   video register, yield, jump, and stop.
 *
 * What the interpreter leaves in A, the carry and overflow is what the program
 * sees after the call (its callers are RAM code), so the port keeps them: A is
 * what the yielding command put there, the carry and overflow are those of the
 * last addition or compare. X is dead there.
 *
 * The commands are logic on the channel and the platform (`step`). Two of them
 * call the original's code -- a routine of the program, and the sound driver --
 * so the loop that runs them is a bridge on the machine (`run`), which makes
 * those calls with the registers the original has there. A command the port
 * doesn't handle -- one no script has: a store outside low RAM, a register
 * write no call expresses, a task that isn't in its list -- is left to the
 * original: the port puts the script back on the command and runs the rest of
 * the interpreter's run as a call-out. So do scripts that go on for
 * COMMAND_LIMIT commands without yielding, which would hang the original. */
#include "scene/scene.h"

#include "platform/audio.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* The data bank the interpreter runs with: scripts, registers and the sound
 * command are all reached through it, and the scripts are in it. */
enum { SCRIPT_BANK = 0x7E };

/* The channels' pages: an assignment leaves its register's address in the
 * scratch cell, which a script can read. */
enum { CHANNEL_PAGES = 0x0A00, CHANNEL_PAGE_SIZE = 0x100 };

enum { COMMAND_LIMIT = 0x10000 };

/* Low RAM, which the store commands write: the rest of bank 0 is I/O and ROM. */
enum { LOW_RAM_END = 0x2000 };

/* The carry and overflow the interpreter leaves (see above). */
typedef struct {
    bool carry, overflow;
} Flags;

static uint16_t add16(Flags *f, uint16_t a, uint16_t b)
{
    uint32_t sum = (uint32_t)a + b;
    f->carry = sum > 0xFFFF;
    f->overflow = (~(a ^ b) & (a ^ sum) & 0x8000) != 0;
    return (uint16_t)sum;
}

/* ---- The channel ------------------------------------------------------------------ */

static uint16_t script(const GameState *g, unsigned ch) { return scene_channels_script(g, ch); }
static void set_script(GameState *g, unsigned ch, uint16_t at) { set_scene_channels_script(g, ch, at); }
static void advance(GameState *g, unsigned ch, uint16_t n) { set_script(g, ch, (uint16_t)(script(g, ch) + n)); }

/* The script's bytes, in the program in bank $7E. */
static uint16_t script_word(const GameState *g, uint16_t at) { return wram_read16(g, (uint32_t)SCRIPT_BANK << 16 | at); }
static uint16_t operand(const GameState *g, unsigned ch) { return script_word(g, script(g, ch)); }

/* Register n (0-15), and loop counter n. */
static uint16_t reg(const GameState *g, unsigned ch, unsigned n)
{
    return (uint16_t)(scene_channels_registers_byte(g, ch, 2 * n) | scene_channels_registers_byte(g, ch, 2 * n + 1) << 8);
}

static void set_reg(GameState *g, unsigned ch, unsigned n, uint16_t v)
{
    set_scene_channels_registers_byte(g, ch, 2 * n, (uint8_t)v);
    set_scene_channels_registers_byte(g, ch, 2 * n + 1, (uint8_t)(v >> 8));
}

static uint16_t counter(const GameState *g, unsigned ch, unsigned n)
{
    return (uint16_t)(scene_channels_loop_counters_byte(g, ch, 2 * n) |
                      scene_channels_loop_counters_byte(g, ch, 2 * n + 1) << 8);
}

static void set_counter(GameState *g, unsigned ch, unsigned n, uint16_t v)
{
    set_scene_channels_loop_counters_byte(g, ch, 2 * n, (uint8_t)v);
    set_scene_channels_loop_counters_byte(g, ch, 2 * n + 1, (uint8_t)(v >> 8));
}

/* ---- Task lists -------------------------------------------------------------------- */

// $C3:0CB8, $C3:0CCD, $C3:0CE2, $C3:0CF8
/* Replace the first word equal to `from` in a task list with `to`: removing a
 * task is replacing it with 0, adding one is replacing the first 0. The early
 * list at $0940 has 8 tasks, the other at $0920 16. False if none matches: the
 * original's search runs on past the list through memory, which no script
 * needs, so that is left to it. */
static bool replace_task(GameState *g, bool early, uint16_t from, uint16_t to)
{
    unsigned count = early ? SCENE_EARLY_TASKS_COUNT : SCENE_TASKS_COUNT;
    for (unsigned slot = 0; slot < count; slot++) {
        if ((early ? scene_early_tasks(g, slot) : scene_tasks(g, slot)) == from) {
            if (early)
                set_scene_early_tasks(g, slot, to);
            else
                set_scene_tasks(g, slot, to);
            return true;
        }
    }
    return false;
}

/* ---- Video registers --------------------------------------------------------------- */

/* Command $AB writes a byte from the script to the video register $21xx. The
 * scripts write nine registers, each here as its service call; false for any
 * other register, or a value a call can't carry. */
static bool write_video_register(Platform *pf, uint8_t reg, uint8_t v)
{
    PfOrigin at = PF_AT(0xC30C75);
    VidWindows w = { 0 };
    VidColorMath math = { 0 };
    uint32_t window_field;
    switch (reg) {
    case 0x1A: /* the Mode 7 plane's flips and what lies outside it */
        if (v & 0x3C)
            return false;
        vid_set_mode7(pf, at, &(VidMode7){ .hflip = v & 1, .vflip = (v & 2) != 0, .outside = (uint8_t)(v >> 6) },
                      VID_M7_SELECT);
        return true;
    case 0x2C:
        vid_set_screens(pf, at, v, 0, VID_SCREEN_MAIN);
        return true;
    case 0x2D:
        vid_set_screens(pf, at, 0, v, VID_SCREEN_SUB);
        return true;
    case 0x31:
        vid_color_math_decode(&math, VID_MATH_LAYERS, v);
        vid_set_color_math(pf, at, &math, VID_MATH_LAYERS);
        return true;
    case 0x33: {
        VidVideoConfig config = vid_video_config_decode(v);
        vid_set_video_config(pf, at, &config);
        return true;
    }
    case 0x25: window_field = VID_WINDOWS_OBJ_BACK; break;
    case 0x26: window_field = VID_WINDOWS_W1_LEFT; break;
    case 0x27: window_field = VID_WINDOWS_W1_RIGHT; break;
    case 0x2E: window_field = VID_WINDOWS_MAIN; break;
    default:
        return false;
    }
    vid_windows_decode(&w, window_field, v);
    vid_set_windows(pf, at, &w, window_field);
    return true;
}

/* ---- The commands ------------------------------------------------------------------ */

/* How a command ends. */
typedef enum {
    NEXT,          /* on to the next command */
    YIELD,         /* the interpreter returns */
    WAIT_SOUND,    /* wait for the sound driver ($C3:0C2A, an idle entry of its own) */
    CALL_PROGRAM,  /* call the program's routine in `Command.word`, then on past it */
    SEND_SOUND,    /* send the driver the command in its buffer, then on past it */
    UNHANDLED,     /* left to the original, from the command's start */
} Step;

typedef struct {
    Step step;
    uint16_t word;   /* A where it yields; the routine to call; the sound command */
} Command;

/* $00-$7F: register n (the low nibble) is assigned or added to. The command
 * leaves the register's address on the page's scratch cell (whose sum sets the
 * carry and overflow). */
static void assign(GameState *g, unsigned ch, Flags *f, uint8_t command)
{
    uint8_t form = command & 0xF0;
    unsigned n = command & 0x0F;
    uint16_t page = (uint16_t)(CHANNEL_PAGES + ch * CHANNEL_PAGE_SIZE);
    set_scene_scratch16(g, scene_math_channel(ch), SCRATCH_A, add16(f, page, (uint16_t)(2 * n)));
    uint16_t word = operand(g, ch);
    uint16_t value;
    if (!(form & 0x20)) {
        /* a word operand */
        value = form == 0x00 ? word : add16(f, reg(g, ch, n), word);
        advance(g, ch, 2);
    } else {
        /* another register, by the byte's bits 1-4 */
        uint16_t source = reg(g, ch, (word & 0x1E) / 2);
        value = form == 0x20 ? source : add16(f, reg(g, ch, n), source);
        advance(g, ch, 1);
    }
    set_reg(g, ch, n, value);
}

// $C3:0C92
/* $80-$9F: loop n (the low nibble) counts its counter up to the word operand;
 * short of it the script jumps to the word after, and when it gets there the
 * counter is cleared and the script carries on after the command. Returns the
 * new script pointer, which the original leaves in A. */
static uint16_t count_loop(GameState *g, unsigned ch, Flags *f, uint8_t command)
{
    unsigned n = command & 0x0F;
    uint16_t count = counter(g, ch, n);
    f->carry = false;   /* the ASL of the index */
    bool done = false;
    if (count != 0) {
        uint16_t limit = operand(g, ch);
        f->carry = count >= limit;   /* CMP, whose sign the BMI takes */
        done = !((uint16_t)(count - limit) & 0x8000);
    }
    if (done) {
        set_counter(g, ch, n, 0);
        uint16_t next = add16(f, script(g, ch), 4);
        set_script(g, ch, next);
        return next;
    }
    set_counter(g, ch, n, (uint16_t)(count + 1));
    advance(g, ch, 2);
    uint16_t target = operand(g, ch);
    set_script(g, ch, target);
    return target;
}

/* $A0-$FF, by the low nibble `n`. */
static Command command_a0(GameState *g, Platform *pf, unsigned ch, Flags *f, uint8_t n)
{
    f->carry = false;   /* the ASL of the table index */
    uint16_t word = operand(g, ch);
    switch (n) {
    case 0x0: /* call a routine of the program */
        return (Command){ CALL_PROGRAM, word };
    case 0x1: /* store the byte after the address at it (bank 0) */
    case 0x2: /* store the word after the address at it */
    {
        if (word >= LOW_RAM_END)
            return (Command){ UNHANDLED, 0 };
        advance(g, ch, 2);
        uint16_t value = operand(g, ch);
        uint32_t at = (uint32_t)SCRIPT_BANK << 16 | word;   /* low RAM, which bank $7E starts with */
        if (n == 1) {
            wram_write8(g, at, (uint8_t)value);
            advance(g, ch, 1);
        } else {
            wram_write16(g, at, value);
            advance(g, ch, 2);
        }
        return (Command){ NEXT, 0 };
    }
    case 0x3: /* call: the return address after the operand, and on at the word */
        set_scene_channels_return_address(g, ch, (uint16_t)(script(g, ch) + 2));
        set_script(g, ch, word);
        return (Command){ NEXT, 0 };
    case 0x4: /* return */
        set_script(g, ch, scene_channels_return_address(g, ch));
        return (Command){ NEXT, 0 };
    case 0x5: /* wait for the sound driver */
        return (Command){ WAIT_SOUND, 0 };
    case 0x6: /* a sound driver command, the four bytes after it, unless a song is being sent */
    {
        advance(g, ch, 2);
        uint16_t tail = operand(g, ch);
        set_sound_command(g, (uint8_t)word);
        set_sound_command_parameters(g, 0, (uint8_t)(word >> 8));
        set_sound_command_parameters(g, 1, (uint8_t)tail);
        set_sound_command_parameters(g, 2, (uint8_t)(tail >> 8));
        if (scene_song_sending(g) != 0) {
            advance(g, ch, 2);
            return (Command){ NEXT, 0 };
        }
        return (Command){ SEND_SOUND, tail };
    }
    case 0x7: /* remove a task from the list at $0940 */
    case 0x8: /* add one to it */
    case 0x9: /* remove a task from the list at $0920 */
    case 0xA: /* add one to it */
    {
        bool add = n == 0x8 || n == 0xA;
        if (!replace_task(g, n <= 0x8, add ? 0 : word, add ? word : 0))
            return (Command){ UNHANDLED, 0 };
        advance(g, ch, 2);
        return (Command){ NEXT, 0 };
    }
    case 0xB: /* write the byte after it to the video register $21xx, xx the next */
        if (!write_video_register(pf, (uint8_t)(word >> 8), (uint8_t)word))
            return (Command){ UNHANDLED, 0 };
        f->carry = false;   /* the ADC #$2100 that makes the address */
        f->overflow = false;
        advance(g, ch, 2);
        return (Command){ NEXT, 0 };
    case 0xC: /* yield */
        return (Command){ YIELD, (uint16_t)(n * 2) };
    case 0xD: /* jump, and yield */
        set_script(g, ch, word);
        return (Command){ YIELD, word };
    case 0xE: /* jump */
        set_script(g, ch, word);
        return (Command){ NEXT, 0 };
    default: /* $F: stop on the command, and set the flag the program's loop waits on */
        advance(g, ch, (uint16_t)-1);
        set_scene_frame_ready(g, 1);
        return (Command){ YIELD, 1 };
    }
}

/* One command. */
static Command step(GameState *g, Platform *pf, unsigned ch, Flags *f)
{
    uint16_t word = operand(g, ch);   /* the command and the byte after it */
    uint8_t command = (uint8_t)word;
    advance(g, ch, 1);
    if (!(command & 0x80)) {
        assign(g, ch, f, command);
        return (Command){ NEXT, 0 };
    }
    set_scene_scratch16(g, scene_math_channel(ch), SCRATCH_A, word);
    switch (command & 0xF0) {
    case 0x80:
        count_loop(g, ch, f, command);
        return (Command){ NEXT, 0 };
    case 0x90:
        return (Command){ YIELD, count_loop(g, ch, f, command) };
    default:
        return command_a0(g, pf, ch, f, command & 0x0F);
    }
}

/* ---- Where the port meets the original's registers ---------------------------------
 *
 * The loop and its call-outs: they set what the original has there, and a build
 * for the 65816 replaces them. */

static void set_widths16(Snes *s) { s->cpu.p &= (uint8_t)~(CPU_FLAG_M | CPU_FLAG_X); }

static void put_flags(Snes *s, const Flags *f)
{
    m_set_flag(s, CPU_FLAG_C, f->carry);
    m_set_flag(s, CPU_FLAG_V, f->overflow);
}

/* The channel whose page D is on: a channel's page is the channel instance of
 * the math helpers' page kind (re/ram.toml, scene_math). */
static bool on_channel(uint16_t d, unsigned *ch)
{
    SceneMathPage page;
    if (!scene_math_page_of(d, &page) || page.kind != SCENE_MATH_CHANNEL)
        return false;
    *ch = page.index;
    return true;
}

/* Where the interpreter runs as the port has it: native mode, 16-bit index
 * registers, binary adds, the scripts' data bank and D on a channel's page. */
static bool machine_as_expected(Snes *s, unsigned *ch)
{
    return !s->cpu.e && !(s->cpu.p & (CPU_FLAG_X | CPU_FLAG_D)) && s->cpu.db == SCRIPT_BANK && on_channel(s->cpu.d, ch);
}

/* After a call-out: the port carries on if the callee left the machine as
 * expected (on whichever channel's page it left D); otherwise the original
 * does, with the carry and overflow the callee left. */
static bool can_continue(Snes *s, Flags *f, unsigned *ch)
{
    f->carry = m_flag(s, CPU_FLAG_C);
    f->overflow = m_flag(s, CPU_FLAG_V);
    return machine_as_expected(s, ch);
}

/* Command $A0: the program's routine, which $7E:37F3 calls through the address
 * the scratch cell holds. X is the command's table index, 0. */
static bool call_program(Snes *s, Flags *f, unsigned *ch, uint16_t routine)
{
    set_scene_scratch16(state_of(s), scene_math_scene(), SCRATCH_A, routine);
    s->cpu.a = routine;
    s->cpu.x = 0;
    set_widths16(s);
    put_flags(s, f);
    m_jsl_dc(s, 0xC30C58, 0x7E37F3, DC_A | DC_N | DC_Z);
    return can_continue(s, f, ch);
}

/* Command $A6: the sound driver command in its buffer, with the registers at
 * the original's JSL: A from the load of the sending flag, X the table index. */
static bool send_sound(Snes *s, Flags *f, unsigned *ch, uint16_t tail)
{
    GameState *g = state_of(s);
    s->cpu.a = tail & 0xFF00;
    s->cpu.x = 0x000C;
    set_widths16(s);
    s->cpu.p |= CPU_FLAG_M;
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, true);
    put_flags(s, f);
    au_command(m_platform(s), PF_CALL(0xC30BE9, 0xC70004),
               (AudioCommand){ .command = sound_command(g), .p1 = sound_command_parameters(g, 0),
                               .p2 = sound_command_parameters(g, 1), .p3 = sound_command_parameters(g, 2),
                               .count = 3 });
    return can_continue(s, f, ch);
}

/* After a command's call-out, the script moves on past its operand on the page
 * the callee left in D: a channel's, or, where the callee left the machine
 * other than the port expects, wherever D is, as the original's does. */
enum { SCRIPT_CELL = 0x20 };
static bool past_call(Snes *s, unsigned *ch, bool go_on)
{
    if (go_on) {
        advance(state_of(s), *ch, 2);
        return true;
    }
    uint16_t cell = (uint16_t)(s->cpu.d + SCRIPT_CELL);
    m_write16(s, cell, (uint16_t)(m_read16(s, cell) + 2));
    return false;
}

/* How the run ends. */
typedef enum { RUN_YIELDED, RUN_STOPPED, RUN_CARRY_ON } RunEnd;

// $C3:0B03
/* Run commands until one yields; A is then what it leaves. Otherwise the
 * script is left on the command the original is to carry on from
 * (RUN_CARRY_ON), with the carry and overflow it starts with, or the port has
 * stopped at the sound wait (RUN_STOPPED). */
static RunEnd run(Snes *s, Flags *f, unsigned ch)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    for (uint32_t i = 0; i < COMMAND_LIMIT; i++) {
        uint16_t start = script(g, ch);
        Flags before = *f;
        Command c = step(g, pf, ch, f);
        switch (c.step) {
        case NEXT:
            continue;
        case YIELD:
            s->cpu.a = c.word;
            return RUN_YIELDED;
        case UNHANDLED:
            set_script(g, ch, start);
            *f = before;
            return RUN_CARRY_ON;
        case WAIT_SOUND:
            /* A and X are the command's table index */
            s->cpu.a = 0x000A;
            s->cpu.x = 0x000A;
            set_widths16(s);
            put_flags(s, f);
            m_stop_at(s, 0xC30C2A, DC_X | DC_N | DC_Z | DC_C);
            return RUN_STOPPED;
        case CALL_PROGRAM:
            if (!past_call(s, &ch, call_program(s, f, &ch, c.word)))
                return RUN_CARRY_ON;
            continue;
        case SEND_SOUND:
            if (!past_call(s, &ch, send_sound(s, f, &ch, c.word)))
                return RUN_CARRY_ON;
            continue;
        }
    }
    return RUN_CARRY_ON;
}

/* The JSR at $C3:0AFF pushes this as its return address. */
enum { INTERPRETER_RETURN = 0x0B01 };

// $C3:0AFF
/* The channel whose page is in D runs until its script yields. */
void scene_run_sequence(Snes *s)
{
    Flags f = { m_flag(s, CPU_FLAG_C), m_flag(s, CPU_FLAG_V) };
    unsigned ch = 0;
    machine_as_expected(s, &ch);
    m_push16(s, INTERPRETER_RETURN);   /* the JSR's frame, there at every call-out */
    RunEnd end = run(s, &f, ch);
    if (end == RUN_STOPPED)
        return;
    m_pull16(s);
    s->cpu.p &= (uint8_t)~CPU_FLAG_M;
    put_flags(s, &f);
    if (end == RUN_CARRY_ON) {
        /* the original's own loop, from the command the port left. Not a
         * stop at the loop's head, which shadow mode and the fuzzer could now
         * compare at the right pass: a command the port gives up on has
         * already written the scratch cell ($F0) and the flags the original
         * writes only once it runs that command, so the port would have to
         * put them back first (docs/TODO.md) */
        m_jsr_dc(s, 0xC30AFF, 0xC30B03, DC_A | DC_X | DC_N | DC_Z);
    }
    m_rtl(s);
}

bool scene_sequence_bank_is_scripts(Snes *s)
{
    unsigned ch;
    return machine_as_expected(s, &ch);
}

/* The wait reads the script through the data bank, on the channel D is on. */
bool scene_sequence_waits_on_channel(Snes *s)
{
    unsigned ch;
    return s->cpu.db == SCRIPT_BANK && on_channel(s->cpu.d, &ch);
}

/* $C3:0C28, where the wait for the sound driver goes on, isn't an entry, so
 * after the wait the rest of that run is the original's. It could be: the
 * store command's own path runs through it, but an entry the original reaches
 * inside a run shadow mode is comparing no longer takes the run's call-outs
 * (docs/PORTING.md, Call-outs). */

// $C3:0C2A
/* One pass of the wait: the sound driver's level (port 2's low nibble, once two
 * reads agree) against the byte at the script: at least it, and the wait is
 * over. Returns the level, or -1 while the two reads disagree. */
typedef struct {
    uint8_t first, second, level, wanted;
} SoundWait;

static SoundWait read_sound_level(const GameState *g, Platform *pf, unsigned ch)
{
    SoundWait w;
    w.first = au_status(pf, PF_AT(0xC30C2C), 2);
    w.second = au_status(pf, PF_AT(0xC30C30), 2);
    w.level = w.first & 0x0F;
    w.wanted = w.first == w.second ? (uint8_t)script_word(g, script(g, ch)) : 0;
    return w;
}

/* CMP, 8-bit: the loop's flags, which the pass leaves as the original's. */
static void compare8(Snes *s, uint8_t a, uint8_t b)
{
    m_set_flag(s, CPU_FLAG_C, a >= b);
    m_set_nz8(s, (uint8_t)(a - b));
}

/* While the level is short of the operand, the script stays on the command
 * (the pointer back to it) and the interpreter returns, with A the level and
 * the carry from the compare; at it, on at $C3:0C28. Idle entry. */
void scene_script_wait_sound_port2(Snes *s)
{
    GameState *g = state_of(s);
    unsigned ch = 0;
    on_channel(s->cpu.d, &ch);   /* the guard has checked it is */
    s->cpu.p |= CPU_FLAG_M;
    SoundWait w = read_sound_level(g, m_platform(s), ch);
    m_set_a8(s, w.first);
    compare8(s, w.first, w.second);
    if (w.first != w.second)
        return;
    m_set_a8(s, w.level);
    compare8(s, w.level, w.wanted);
    if (!m_flag(s, CPU_FLAG_N)) {
        s->cpu.pc = 0x0C28;
        return;
    }
    s->cpu.p &= (uint8_t)~CPU_FLAG_M;
    advance(g, ch, (uint16_t)-1);
    m_set_nz16(s, script(g, ch));
    m_rts(s);
}
