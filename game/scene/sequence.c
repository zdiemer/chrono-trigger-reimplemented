/* Scenes: the sequence scripts.
 *
 * The program at $7E:3000 (the attract sequence, the title, the endings) runs
 * two sequence channels each frame. A channel is a page of work RAM ($0A00 or
 * $0B00, which the program puts in D before it calls $C3:0AFF, with the data
 * bank $7E): sixteen word registers at $00-$1F, the script pointer at $20,
 * loop counters from $22 and a return address at $30. The interpreter reads
 * commands at the pointer (bank $7E) until one yields, and the script carries
 * on from there next frame. tools/disasm/seqscript.py has the command set:
 *
 * - $00-$7F, a register assignment: the low nibble picks the register, bit 5
 *   of the command whether the operand is a word (3 bytes) or another register
 *   (2 bytes: the byte's bits 1-4 name it), and bits 4-6 otherwise whether it
 *   is stored ($00, $20) or added;
 * - $80-$9F, a counted loop: the counter at $22 + 2n counts up to the word
 *   operand, jumping to the word after it until it gets there; $9n also yields;
 * - $A0-$FF, the low nibble picks the rest: call a routine of the program (by
 *   its address in $00F0), store a byte or a word to low RAM, call and return
 *   within the script, wait for the sound driver, send it a command, add or
 *   remove a task in either of the NMI's task lists, write a video register,
 *   yield, jump, and stop.
 *
 * What the interpreter leaves in A, the carry and overflow is what the program
 * sees after the call (its callers are RAM code liveness can't follow), so the
 * port keeps them: A is what the yielding command put there, the carry and
 * overflow are those of the last addition or compare. X is dead there.
 *
 * A command the port doesn't handle -- one no script has: a store outside low
 * RAM, a register write no call expresses, a task list search that never ends
 * -- is left to the original: the port puts the script back on the command and
 * runs the rest of the interpreter's run as a call-out. So do scripts that go
 * on for COMMAND_LIMIT commands without yielding, which would hang the
 * original. */
#include "scene/scene.h"

#include "platform/audio.h"
#include "platform/video.h"
#include "port/machine.h"
#include "ram.h"

/* A channel's cells, as offsets on its page. */
enum {
    REGISTERS = RAM_SCENE_CHANNELS_REGISTERS - RAM_SCENE_CHANNELS,
    SCRIPT = RAM_SCENE_CHANNELS_SCRIPT - RAM_SCENE_CHANNELS,
    LOOP_COUNTERS = RAM_SCENE_CHANNELS_LOOP_COUNTERS - RAM_SCENE_CHANNELS,
    RETURN_ADDRESS = RAM_SCENE_CHANNELS_RETURN_ADDRESS - RAM_SCENE_CHANNELS,
    SCRATCH = RAM_SCENE_CHANNELS_MATH_SCRATCH - RAM_SCENE_CHANNELS,
};

/* The only data bank the interpreter runs with: scripts, registers and the
 * sound command are all reached through it. */
enum { SCRIPT_BANK = 0x7E };

/* A channel's page is in low RAM (the program's are $0A00 and $0B00): with D
 * anywhere else the direct page reaches I/O space. */
enum { LOW_RAM_PAGES_END = 0x1F00 };

enum { COMMAND_LIMIT = 0x10000 };

/* The task lists are searched with a 16-bit index stepping by 2, which wraps:
 * after this many steps every index has been tried, and a list with no match
 * is searched forever. */
enum { SEARCH_STEPS = 0x8000 };

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

/* The page is reached through the direct page, in bank 0. */
static uint16_t page_word(Snes *s, uint16_t offset) { return m_read16(s, (uint16_t)(s->cpu.d + offset)); }
static void set_page_word(Snes *s, uint16_t offset, uint16_t v) { m_write16(s, (uint16_t)(s->cpu.d + offset), v); }

static uint16_t script(Snes *s) { return page_word(s, SCRIPT); }
static void set_script(Snes *s, uint16_t at) { set_page_word(s, SCRIPT, at); }
static void advance(Snes *s, uint16_t n) { set_script(s, (uint16_t)(script(s) + n)); }

/* Script bytes, the registers' words and the task lists where the original
 * reaches them through the data bank. */
static uint16_t bank_word(Snes *s, uint16_t addr) { return m_read16(s, m_abs(s, addr)); }
static void set_bank_word(Snes *s, uint16_t addr, uint16_t v) { m_write16(s, m_abs(s, addr), v); }
static uint16_t operand(Snes *s) { return bank_word(s, script(s)); }

/* ---- Task lists -------------------------------------------------------------------- */

// $C3:0CB8, $C3:0CCD, $C3:0CE2, $C3:0CF8
/* Replace the first word equal to `from` in a task list with `to`: removing a
 * task is replacing it with 0, adding one is replacing the first 0. The index
 * steps by 2 from 0 and runs on past the list, through the data bank, as the
 * original's does. False if no word matches. */
static bool replace_task(Snes *s, uint16_t list, uint16_t from, uint16_t to)
{
    uint16_t index = 0;
    for (uint32_t i = 0; i < SEARCH_STEPS; i++, index = (uint16_t)(index + 2)) {
        uint32_t at = m_abs(s, list) + index;
        if (m_read16(s, at) == from) {
            m_write16(s, at, to);
            return true;
        }
    }
    return false;
}

/* ---- Video registers --------------------------------------------------------------- */

/* Command $AB writes a byte from the script to the video register $21xx. The
 * scripts write nine registers, each here as its service call; false for any
 * other register, or a value a call can't carry. */
static bool write_video_register(Snes *s, uint8_t reg, uint8_t v)
{
    Platform *pf = m_platform(s);
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

/* ---- Where the port meets the original's registers ---------------------------------
 *
 * The call-outs and the places the port hands the machine back: they set what
 * the original has there, and a build for the 65816 replaces them. */

static void set_widths16(Snes *s) { s->cpu.p &= (uint8_t)~(CPU_FLAG_M | CPU_FLAG_X); }

static void put_flags(Snes *s, const Flags *f)
{
    m_set_flag(s, CPU_FLAG_C, f->carry);
    m_set_flag(s, CPU_FLAG_V, f->overflow);
}

static void take_flags(Snes *s, Flags *f)
{
    f->carry = m_flag(s, CPU_FLAG_C);
    f->overflow = m_flag(s, CPU_FLAG_V);
}

/* Where the interpreter runs as the port has it: native mode, 16-bit index
 * registers, binary adds, the scripts' data bank and a channel page in low
 * RAM. */
static bool machine_as_expected(Snes *s)
{
    return !s->cpu.e && !(s->cpu.p & (CPU_FLAG_X | CPU_FLAG_D)) && s->cpu.db == SCRIPT_BANK &&
           s->cpu.d < LOW_RAM_PAGES_END;
}

/* After a call-out: the port carries on if the callee left the machine as
 * expected; otherwise the original does, with the carry and overflow the
 * callee left. */
static bool can_continue(Snes *s, Flags *f)
{
    take_flags(s, f);
    return machine_as_expected(s);
}

/* Command $A0: call the program's routine at `routine` through $00F0 ($7E:37F3
 * runs it). X is the command's table index, 0. */
static bool call_program(Snes *s, Flags *f, uint16_t routine)
{
    m_write16(s, m_abs(s, RAM_ABS(RAM_SCENE_MATH_SCRATCH_A)), routine);
    s->cpu.a = routine;
    s->cpu.x = 0;
    set_widths16(s);
    put_flags(s, f);
    m_jsl_dc(s, 0xC30C58, 0x7E37F3, DC_A | DC_N | DC_Z);
    return can_continue(s, f);
}

/* Command $A6: send the sound driver the command in `head` (the command and its
 * first parameter) and `tail` (the other two), unless a song is being sent. */
static bool send_sound(Snes *s, Flags *f, uint16_t head, uint16_t tail)
{
    m_write16(s, m_abs(s, RAM_ABS(RAM_SOUND_COMMAND)), head);
    m_write16(s, m_abs(s, RAM_ABS(RAM_SOUND_COMMAND) + 2), tail);
    if (m_read8(s, m_abs(s, RAM_ABS(RAM_SCENE_SONG_SENDING))) != 0)
        return true;
    /* the registers at the original's JSL: A from the LDA $0388, X the table
     * index */
    s->cpu.a = tail & 0xFF00;
    s->cpu.x = 0x000C;
    set_widths16(s);
    s->cpu.p |= CPU_FLAG_M;
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, true);
    put_flags(s, f);
    au_command(m_platform(s), PF_CALL(0xC30BE9, 0xC70004),
               (AudioCommand){ .command = (uint8_t)head, .p1 = (uint8_t)(head >> 8), .p2 = (uint8_t)tail,
                               .p3 = (uint8_t)(tail >> 8), .count = 3 });
    return can_continue(s, f);
}

/* Command $A5: the wait for the sound driver is an idle entry of its own
 * ($C3:0C2A); A and X are the command's table index. */
static void stop_at_sound_wait(Snes *s, const Flags *f)
{
    s->cpu.a = 0x000A;
    s->cpu.x = 0x000A;
    set_widths16(s);
    put_flags(s, f);
    m_stop_at(s, 0xC30C2A, DC_X | DC_N | DC_Z | DC_C);
}

/* ---- The commands ------------------------------------------------------------------ */

/* How a command ends. */
typedef enum {
    NEXT,        /* on to the next command */
    YIELD,       /* the interpreter returns */
    STOPPED,     /* the port handed the machine to the original */
    UNHANDLED,   /* left to the original, from the command's start */
    CARRY_ON,    /* left to the original, from the next command */
} Step;

/* $00-$7F: register n (the low nibble) is assigned or added to. The command
 * leaves the register's address on the page in $F0 (D + 2n, whose sum sets
 * the carry and overflow). */
static void assign(Snes *s, Flags *f, uint8_t command)
{
    uint8_t form = command & 0xF0;
    uint16_t reg = add16(f, s->cpu.d, (uint16_t)(REGISTERS + (command & 0x0F) * 2));
    set_page_word(s, SCRATCH, reg);
    uint16_t word = operand(s);
    uint16_t value;
    if (!(form & 0x20)) {
        /* a word operand */
        value = form == 0x00 ? word : add16(f, bank_word(s, reg), word);
        advance(s, 2);
    } else {
        /* another register, by the byte's bits 1-4 */
        uint16_t source = page_word(s, (uint16_t)(REGISTERS + (word & 0x1E)));
        value = form == 0x20 ? source : add16(f, bank_word(s, reg), source);
        advance(s, 1);
    }
    set_bank_word(s, reg, value);
}

// $C3:0C92
/* $80-$9F: loop n (the low nibble) counts its counter up to the word operand;
 * short of it the script jumps to the word after, and when it gets there the
 * counter is cleared and the script carries on after the command. Returns the
 * new script pointer, which the original leaves in A. */
static uint16_t count_loop(Snes *s, Flags *f, uint8_t command)
{
    uint16_t counter = (uint16_t)(LOOP_COUNTERS + (command & 0x0F) * 2);
    uint16_t count = page_word(s, counter);
    f->carry = false;   /* the ASL of the index */
    bool done = false;
    if (count != 0) {
        uint16_t limit = operand(s);
        f->carry = count >= limit;   /* CMP, whose sign the BMI takes */
        done = !((uint16_t)(count - limit) & 0x8000);
    }
    if (done) {
        set_page_word(s, counter, 0);
        uint16_t next = add16(f, script(s), 4);
        set_script(s, next);
        return next;
    }
    set_page_word(s, counter, (uint16_t)(count + 1));
    advance(s, 2);
    uint16_t target = operand(s);
    set_script(s, target);
    return target;
}

/* $A0-$FF, by the low nibble `n`. `*result` is A where the command yields. */
static Step command_a0(Snes *s, Flags *f, uint8_t n, uint16_t *result)
{
    f->carry = false;   /* the ASL of the table index */
    uint16_t word = operand(s);
    switch (n) {
    case 0x0: /* call a routine of the program */
    {
        bool can = call_program(s, f, word);
        advance(s, 2);
        return can ? NEXT : CARRY_ON;
    }
    case 0x1: /* store the byte at 3 at the word address at 1 (bank 0) */
    case 0x2: /* store the word at 3 there */
    {
        if (word >= LOW_RAM_END)
            return UNHANDLED;
        advance(s, 2);
        uint16_t value = operand(s);
        if (n == 1) {
            m_write8(s, word, (uint8_t)value);
            advance(s, 1);
        } else {
            m_write16(s, word, value);
            advance(s, 2);
        }
        return NEXT;
    }
    case 0x3: /* call: the return address after the operand, and on at the word */
        set_page_word(s, RETURN_ADDRESS, (uint16_t)(script(s) + 2));
        set_script(s, word);
        return NEXT;
    case 0x4: /* return */
        set_script(s, page_word(s, RETURN_ADDRESS));
        return NEXT;
    case 0x5: /* wait for the sound driver */
        stop_at_sound_wait(s, f);
        return STOPPED;
    case 0x6: /* a sound driver command, the four bytes at 1 */
    {
        advance(s, 2);
        uint16_t tail = operand(s);
        bool can = send_sound(s, f, word, tail);
        advance(s, 2);
        return can ? NEXT : CARRY_ON;
    }
    case 0x7: /* remove a task from the list at $0940 */
    case 0x8: /* add one to it */
    case 0x9: /* remove a task from the list at $0920 */
    case 0xA: /* add one to it */
    {
        uint16_t list = n <= 0x8 ? RAM_ABS(RAM_SCENE_EARLY_TASKS) : RAM_ABS(RAM_SCENE_TASKS);
        bool add = n == 0x8 || n == 0xA;
        if (!replace_task(s, list, add ? 0 : word, add ? word : 0))
            return UNHANDLED;
        advance(s, 2);
        return NEXT;
    }
    case 0xB: /* write the byte at 1 to the video register $21xx, xx at 2 */
        if (!write_video_register(s, (uint8_t)(word >> 8), (uint8_t)word))
            return UNHANDLED;
        f->carry = false;   /* the ADC #$2100 that makes the address */
        f->overflow = false;
        advance(s, 2);
        return NEXT;
    case 0xC: /* yield */
        *result = (uint16_t)(n * 2);
        return YIELD;
    case 0xD: /* jump, and yield */
        set_script(s, word);
        *result = word;
        return YIELD;
    case 0xE: /* jump */
        set_script(s, word);
        return NEXT;
    default: /* $F: stop on the command, and set the flag the program's loop waits on */
        advance(s, (uint16_t)-1);
        m_write16(s, RAM_ABS(RAM_SCENE_FRAME_READY), 1);
        *result = 1;
        return YIELD;
    }
}

/* One command. */
static Step step(Snes *s, Flags *f, uint16_t *result)
{
    uint16_t word = operand(s);   /* the command and the byte after it */
    uint8_t command = (uint8_t)word;
    advance(s, 1);
    if (!(command & 0x80)) {
        assign(s, f, command);
        return NEXT;
    }
    set_page_word(s, SCRATCH, word);
    switch (command & 0xF0) {
    case 0x80:
        count_loop(s, f, command);
        return NEXT;
    case 0x90:
        *result = count_loop(s, f, command);
        return YIELD;
    default:
        return command_a0(s, f, command & 0x0F, result);
    }
}

// $C3:0B03
/* Run commands until one yields; `*result` is A then. Otherwise the script is
 * left on the command the original is to carry on from (CARRY_ON), with the
 * carry and overflow it starts with. */
static Step run(Snes *s, Flags *f, uint16_t *result)
{
    for (uint32_t i = 0; i < COMMAND_LIMIT; i++) {
        uint16_t start = script(s);
        Flags before = *f;
        Step end = step(s, f, result);
        if (end == UNHANDLED) {
            set_script(s, start);
            *f = before;
            return CARRY_ON;
        }
        if (end != NEXT)
            return end;
    }
    return CARRY_ON;
}

/* ---- Entries ----------------------------------------------------------------------- */

/* The JSR at $C3:0AFF pushes this as its return address. */
enum { INTERPRETER_RETURN = 0x0B01 };

// $C3:0AFF
/* The channel whose page is in D runs until its script yields. */
void scene_run_sequence(Snes *s)
{
    Flags f = { m_flag(s, CPU_FLAG_C), m_flag(s, CPU_FLAG_V) };
    uint16_t result = 0;
    m_push16(s, INTERPRETER_RETURN);   /* the JSR's frame, there at every call-out */
    Step end = run(s, &f, &result);
    if (end == STOPPED)
        return;
    m_pull16(s);
    s->cpu.p &= (uint8_t)~CPU_FLAG_M;
    put_flags(s, &f);
    if (end == CARRY_ON) {
        /* the original's own loop, from the command the port left */
        m_jsr_dc(s, 0xC30AFF, 0xC30B03, DC_A | DC_X | DC_N | DC_Z);
    } else {
        s->cpu.a = result;
    }
    m_rtl(s);
}

bool scene_sequence_bank_is_scripts(Snes *s) { return machine_as_expected(s); }

/* $C3:0C28, where the wait for the sound driver goes on, isn't an entry: the
 * store command's own path runs through it, so the original entering it
 * inside a run would start a comparison of its own and take the call the
 * run's comparison waits for (docs/PORTING.md, Call-outs). After the wait, the
 * rest of that run is the original's. */

/* CMP, 8-bit: the loop's flags, which the pass leaves as the original's. */
static void compare8(Snes *s, uint8_t a, uint8_t b)
{
    uint8_t diff = (uint8_t)(a - b);
    m_set_flag(s, CPU_FLAG_C, a >= b);
    m_set_nz8(s, diff);
}

// $C3:0C2A
/* One pass of the wait: read the sound driver's port 2 until two reads agree;
 * while its low nibble is at least the script's operand byte, on at $C3:0C28;
 * otherwise the script stays on the command (the pointer back to it) and the
 * interpreter returns, with A the nibble and the carry from the compare. Idle
 * entry. */
void scene_script_wait_sound_port2(Snes *s)
{
    Platform *pf = m_platform(s);
    Cpu *c = &s->cpu;
    c->p |= CPU_FLAG_M;
    uint8_t first = au_status(pf, PF_AT(0xC30C2C), 2);
    uint8_t second = au_status(pf, PF_AT(0xC30C30), 2);
    c->a = (uint16_t)((c->a & 0xFF00) | first);
    compare8(s, first, second);
    if (first != second)
        return;
    uint8_t level = first & 0x0F;
    c->a = (uint16_t)((c->a & 0xFF00) | level);
    compare8(s, level, m_read8(s, m_abs(s, script(s))));
    if (!m_flag(s, CPU_FLAG_N)) {
        c->pc = 0x0C28;
        return;
    }
    c->p &= (uint8_t)~CPU_FLAG_M;
    advance(s, (uint16_t)-1);
    m_set_nz16(s, script(s));
    m_rts(s);
}
